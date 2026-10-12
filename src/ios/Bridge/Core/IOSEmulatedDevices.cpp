#include "Cafe/OS/libs/nsyshid/Skylander.h"
#include "Cafe/OS/libs/nsyshid/Infinity.h"
#include "Cafe/OS/libs/nsyshid/Dimensions.h"

#include "Common/FileStream.h"

#include <array>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>

namespace
{
enum : int
{
	kSkylanders = 0,
	kInfinity = 1,
	kDimensions = 2,
};

struct Slot
{
	std::string name;
	fs::path path;
	uint8 portalSlot = 0xFF;
};

std::array<Slot, nsyshid::MAX_SKYLANDERS> s_skylanderSlots;
std::array<Slot, nsyshid::MAX_FIGURES> s_infinitySlots;
std::array<Slot, 7> s_dimensionsSlots;

constexpr std::array<uint8, 7> kDimensionsPads = {2, 1, 3, 2, 2, 3, 3};

std::span<Slot> SlotsFor(int device)
{
	switch (device)
	{
	case kSkylanders: return s_skylanderSlots;
	case kInfinity: return s_infinitySlots;
	case kDimensions: return s_dimensionsSlots;
	default: return {};
	}
}

bool ValidSlot(int device, int slot)
{
	auto slots = SlotsFor(device);
	return slot >= 0 && (size_t)slot < slots.size();
}

bool InfinityFigureFitsSlot(uint32 figure, int slot)
{
	if (slot == 0)
		return (figure > 0x1E8480 && figure < 0x2DC6BF) || (figure > 0x3D0900 && figure < 0x4C4B3F);
	if (slot == 1 || slot == 2)
		return figure > 0x3D0900 && figure < 0x4C4B3F;
	if (slot == 3 || slot == 6)
		return figure < 0x1E847F;
	return figure > 0x2DC6C0 && figure < 0x3D08FF;
}
}

int IOSEmulatedDevices_SlotCount(int device)
{
	return (int)SlotsFor(device).size();
}

std::string IOSEmulatedDevices_SlotNames(int device)
{
	std::ostringstream out;
	auto slots = SlotsFor(device);
	for (size_t i = 0; i < slots.size(); i++)
	{
		if (i != 0)
			out << '\x1E';
		out << slots[i].name;
	}
	return out.str();
}

std::string IOSEmulatedDevices_FigureList(int device, int slot)
{
	std::ostringstream out;
	bool first = true;
	auto add = [&](uint32 id, uint16 variant, const char* name) {
		if (!first)
			out << '\x1E';
		first = false;
		out << id << '\x1F' << variant << '\x1F' << name;
	};
	switch (device)
	{
	case kSkylanders:
		for (const auto& [ids, name] : nsyshid::SkylanderUSB::GetListSkylanders())
			add(ids.first, ids.second, name);
		break;
	case kInfinity:
		for (const auto& [id, info] : nsyshid::InfinityUSB::GetFigureList())
			if (InfinityFigureFitsSlot(id, slot))
				add(id, 0, info.second);
		break;
	case kDimensions:
		for (const auto& [id, name] : nsyshid::DimensionsUSB::GetListMinifigs())
			add(id, 0, name);
		break;
	}
	return out.str();
}

std::string IOSEmulatedDevices_Clear(int device, int slot)
{
	if (!ValidSlot(device, slot))
		return "Invalid figure slot.";
	auto& current = SlotsFor(device)[slot];
	if (current.path.empty())
		return {};
	switch (device)
	{
	case kSkylanders:
		nsyshid::g_skyportal.RemoveSkylander(current.portalSlot);
		break;
	case kInfinity:
		nsyshid::g_infinitybase.RemoveFigure((uint8)slot);
		break;
	case kDimensions:
		nsyshid::g_dimensionstoypad.RemoveFigure(kDimensionsPads[slot], (uint8)slot, true);
		break;
	}
	current = {};
	return {};
}

std::string IOSEmulatedDevices_Load(int device, int slot, const char* path)
{
	if (!ValidSlot(device, slot))
		return "Invalid figure slot.";
	const fs::path filePath(path ? path : "");

	for (int other : {kSkylanders, kInfinity, kDimensions})
	{
		auto slots = SlotsFor(other);
		for (size_t i = 0; i < slots.size(); i++)
		{
			if (slots[i].path == filePath)
			{
				if (other == device && (int)i == slot)
					return {};
				return "This figure file is already loaded in another slot. Clear it there first.";
			}
		}
	}

	std::unique_ptr<FileStream> file(FileStream::openFile2(filePath, true));
	if (!file)
		return "Unable to open the figure file for reading and writing.";

	std::array<uint8, nsyshid::SKY_FIGURE_SIZE> data{};
	const size_t size = device == kSkylanders ? nsyshid::SKY_FIGURE_SIZE
		: device == kInfinity ? nsyshid::INF_FIGURE_SIZE
							   : 0x2D * 0x04;
	if (file->readData(data.data(), size) != size)
		return "The figure file is too small for this device.";

	IOSEmulatedDevices_Clear(device, slot);
	auto& current = SlotsFor(device)[slot];
	switch (device)
	{
	case kSkylanders:
	{
		const uint8 portalSlot = nsyshid::g_skyportal.LoadSkylander(data.data(), std::move(file));
		if (portalSlot == 0xFF)
			return "The Skylanders portal has no free slots.";
		current.portalSlot = portalSlot;
		current.name = nsyshid::g_skyportal.FindSkylander(
			uint16(data[0x11]) << 8 | data[0x10], uint16(data[0x1D]) << 8 | data[0x1C]);
		break;
	}
	case kInfinity:
	{
		std::array<uint8, nsyshid::INF_FIGURE_SIZE> figureData;
		std::copy_n(data.begin(), figureData.size(), figureData.begin());
		const uint32 id = nsyshid::g_infinitybase.LoadFigure(figureData, std::move(file), (uint8)slot);
		current.name = nsyshid::g_infinitybase.FindFigure(id).second;
		break;
	}
	case kDimensions:
	{
		std::array<uint8, 0x2D * 0x04> figureData;
		std::copy_n(data.begin(), figureData.size(), figureData.begin());
		const uint32 id = nsyshid::g_dimensionstoypad.LoadFigure(figureData, std::move(file), kDimensionsPads[slot], (uint8)slot);
		current.name = nsyshid::g_dimensionstoypad.FindFigure(id);
		break;
	}
	}
	current.path = filePath;
	return {};
}

std::string IOSEmulatedDevices_Create(int device, uint32_t figureId, uint16_t variant, const char* path)
{
	const fs::path filePath(path ? path : "");
	std::error_code error;
	if (fs::exists(filePath, error) || error)
		return "The figure file already exists or cannot be accessed.";

	bool created = false;
	switch (device)
	{
	case kSkylanders:
		if (figureId > UINT16_MAX)
			return "Skylander IDs must be between 0 and 65535.";
		created = nsyshid::g_skyportal.CreateSkylander(filePath, (uint16)figureId, variant);
		break;
	case kInfinity:
		created = nsyshid::g_infinitybase.CreateFigure(filePath, figureId, nsyshid::g_infinitybase.FindFigure(figureId).first);
		break;
	case kDimensions:
		if (figureId > UINT16_MAX)
			return "Dimensions IDs must be between 0 and 65535.";
		created = nsyshid::g_dimensionstoypad.CreateFigure(filePath, figureId);
		break;
	}
	return created ? std::string{} : "Unable to create the figure file.";
}

std::string IOSEmulatedDevices_MoveDimensions(int fromSlot, int toSlot)
{
	if (!ValidSlot(kDimensions, fromSlot) || !ValidSlot(kDimensions, toSlot))
		return "Invalid toypad slot.";
	if (fromSlot == toSlot)
		return {};
	if (s_dimensionsSlots[fromSlot].path.empty())
		return "The source slot is empty.";
	if (!s_dimensionsSlots[toSlot].path.empty())
		return "Clear the destination slot before moving a figure there.";
	if (!nsyshid::g_dimensionstoypad.MoveFigure(kDimensionsPads[toSlot], (uint8)toSlot, kDimensionsPads[fromSlot], (uint8)fromSlot))
		return "Unable to move the figure.";
	s_dimensionsSlots[toSlot] = std::move(s_dimensionsSlots[fromSlot]);
	s_dimensionsSlots[fromSlot] = {};
	return {};
}

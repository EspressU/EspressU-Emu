#import "EmulatedUSBDevices.h"

#include "Cafe/OS/libs/nsyshid/Skylander.h"
#include "Cafe/OS/libs/nsyshid/Infinity.h"
#include "Cafe/OS/libs/nsyshid/Dimensions.h"

namespace {
struct Slot {
    std::string name;
    fs::path path;
    uint8 portalSlot = 0xFF;
};

std::array<Slot, nsyshid::MAX_SKYLANDERS> skySlots;
std::array<Slot, nsyshid::MAX_FIGURES> infinitySlots;
std::array<Slot, 7> dimensionsSlots;
constexpr std::array<uint8, 7> dimensionPads = {2, 1, 3, 2, 2, 3, 3};

std::span<Slot> slotsFor(CemuUSBDevice device)
{
    switch (device) {
    case CemuUSBDeviceSkylanders: return skySlots;
    case CemuUSBDeviceInfinity: return infinitySlots;
    case CemuUSBDeviceDimensions: return dimensionsSlots;
    }
    return {};
}

bool validSlot(CemuUSBDevice device, NSInteger slot)
{
    return slot >= 0 && slot < slotsFor(device).size();
}

bool infinityFigureFits(uint32 figure, NSInteger slot)
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

@implementation CemuUSBFigure
@end

@implementation CemuEmulatedUSBDevices

+ (NSArray<NSString *> *)slotNamesForDevice:(CemuUSBDevice)device
{
    NSMutableArray<NSString *> *names = [NSMutableArray array];
    for (const auto& slot : slotsFor(device))
        [names addObject:[NSString stringWithUTF8String:slot.name.c_str()]];
    return names;
}

+ (NSArray<CemuUSBFigure *> *)figuresForDevice:(CemuUSBDevice)device slot:(NSInteger)slot
{
    NSMutableArray<CemuUSBFigure *> *figures = [NSMutableArray array];
    auto add = [&](uint32 id, uint16 variant, const char *name) {
        CemuUSBFigure *figure = [CemuUSBFigure new];
        figure.figureID = id;
        figure.variant = variant;
        figure.name = [NSString stringWithUTF8String:name];
        [figures addObject:figure];
    };
    switch (device) {
    case CemuUSBDeviceSkylanders:
        for (const auto& [ids, name] : nsyshid::SkylanderUSB::GetListSkylanders())
            add(ids.first, ids.second, name);
        break;
    case CemuUSBDeviceInfinity:
        for (const auto& [id, info] : nsyshid::InfinityUSB::GetFigureList())
            if (infinityFigureFits(id, slot)) add(id, 0, info.second);
        break;
    case CemuUSBDeviceDimensions:
        for (const auto& [id, name] : nsyshid::DimensionsUSB::GetListMinifigs())
            add(id, 0, name);
        break;
    }
    return [figures sortedArrayUsingComparator:^NSComparisonResult(CemuUSBFigure *a, CemuUSBFigure *b) {
        return [a.name localizedStandardCompare:b.name];
    }];
}

+ (NSString *)clearDevice:(CemuUSBDevice)device slot:(NSInteger)slot
{
    if (!validSlot(device, slot)) return @"Invalid figure slot.";
    auto& current = slotsFor(device)[slot];
    if (current.path.empty()) return nil;
    switch (device) {
    case CemuUSBDeviceSkylanders:
        nsyshid::g_skyportal.RemoveSkylander(current.portalSlot);
        break;
    case CemuUSBDeviceInfinity:
        nsyshid::g_infinitybase.RemoveFigure(slot);
        break;
    case CemuUSBDeviceDimensions:
        nsyshid::g_dimensionstoypad.RemoveFigure(dimensionPads[slot], slot, true);
        break;
    }
    current = {};
    return nil;
}

+ (NSString *)loadDevice:(CemuUSBDevice)device slot:(NSInteger)slot path:(NSString *)path
{
    if (!validSlot(device, slot)) return @"Invalid figure slot.";
    const fs::path filePath(path.fileSystemRepresentation);
    for (CemuUSBDevice other : {CemuUSBDeviceSkylanders, CemuUSBDeviceInfinity, CemuUSBDeviceDimensions}) {
        auto slots = slotsFor(other);
        for (size_t i = 0; i < slots.size(); ++i) {
            if (slots[i].path == filePath) {
                if (other == device && i == slot) return nil;
                return @"This figure file is already loaded in another slot. Clear it there first.";
            }
        }
    }
    std::unique_ptr<FileStream> file(FileStream::openFile2(filePath, true));
    if (!file) return @"Unable to open the figure file for reading and writing.";

    std::array<uint8, nsyshid::SKY_FIGURE_SIZE> data{};
    size_t size = device == CemuUSBDeviceSkylanders ? nsyshid::SKY_FIGURE_SIZE :
        device == CemuUSBDeviceInfinity ? nsyshid::INF_FIGURE_SIZE : 0x2D * 0x04;
    if (file->readData(data.data(), size) != size)
        return @"The figure file is too small for this device.";

    [self clearDevice:device slot:slot];
    auto& current = slotsFor(device)[slot];
    switch (device) {
    case CemuUSBDeviceSkylanders: {
        uint8 portalSlot = nsyshid::g_skyportal.LoadSkylander(data.data(), std::move(file));
        if (portalSlot == 0xFF) return @"The Skylanders portal has no free slots.";
        current.portalSlot = portalSlot;
        current.name = nsyshid::g_skyportal.FindSkylander(
            uint16(data[0x11]) << 8 | data[0x10], uint16(data[0x1D]) << 8 | data[0x1C]);
        break;
    }
    case CemuUSBDeviceInfinity: {
        std::array<uint8, nsyshid::INF_FIGURE_SIZE> figureData;
        std::copy_n(data.begin(), figureData.size(), figureData.begin());
        auto id = nsyshid::g_infinitybase.LoadFigure(figureData, std::move(file), slot);
        current.name = nsyshid::g_infinitybase.FindFigure(id).second;
        break;
    }
    case CemuUSBDeviceDimensions: {
        std::array<uint8, 0x2D * 0x04> figureData;
        std::copy_n(data.begin(), figureData.size(), figureData.begin());
        auto id = nsyshid::g_dimensionstoypad.LoadFigure(figureData, std::move(file), dimensionPads[slot], slot);
        current.name = nsyshid::g_dimensionstoypad.FindFigure(id);
        break;
    }
    }
    current.path = filePath;
    return nil;
}

+ (NSString *)createDevice:(CemuUSBDevice)device figureID:(uint32_t)figureID variant:(uint16_t)variant path:(NSString *)path
{
    const fs::path filePath(path.fileSystemRepresentation);
    std::error_code error;
    if (fs::exists(filePath, error) || error) return @"The figure file already exists or cannot be accessed.";
    bool created = false;
    switch (device) {
    case CemuUSBDeviceSkylanders:
        if (figureID > UINT16_MAX) return @"Skylander IDs must be between 0 and 65535.";
        created = nsyshid::g_skyportal.CreateSkylander(filePath, figureID, variant);
        break;
    case CemuUSBDeviceInfinity:
        created = nsyshid::g_infinitybase.CreateFigure(filePath, figureID, nsyshid::g_infinitybase.FindFigure(figureID).first);
        break;
    case CemuUSBDeviceDimensions:
        if (figureID > UINT16_MAX) return @"Dimensions IDs must be between 0 and 65535.";
        created = nsyshid::g_dimensionstoypad.CreateFigure(filePath, figureID);
        break;
    }
    return created ? nil : @"Unable to create the figure file.";
}

+ (NSString *)moveDimensionsSlot:(NSInteger)source toSlot:(NSInteger)destination
{
    if (!validSlot(CemuUSBDeviceDimensions, source) || !validSlot(CemuUSBDeviceDimensions, destination))
        return @"Invalid toypad slot.";
    if (source == destination) return nil;
    if (dimensionsSlots[source].path.empty()) return @"The source slot is empty.";
    if (!dimensionsSlots[destination].path.empty()) return @"Clear the destination slot before moving a figure there.";
    if (!nsyshid::g_dimensionstoypad.MoveFigure(dimensionPads[destination], destination, dimensionPads[source], source))
        return @"Unable to move the figure.";
    dimensionsSlots[destination] = std::move(dimensionsSlots[source]);
    dimensionsSlots[source] = {};
    return nil;
}
@end

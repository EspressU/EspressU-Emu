#include "Cafe/HW/MMU/MMU.h"
#include "Cafe/GraphicPack/GraphicPack2.h"
#include "Cemu/Logging/CemuLogging.h"
#include "WindowSystem.h"
#include "util/MemMapper/MemMapper.h"
#include "config/ActiveSettings.h"
#if BOOST_OS_IOS
#include "util/MemMapper/SparseReservation.h"
#endif

uint8* memory_base = NULL;
uint8* memory_elfCodeArena = NULL;

#if BOOST_OS_IOS
static MemMapper::SparseReservation s_sparseReservation;
static bool s_usingSparseReservation = false;
static uintptr_t s_failedSparseAddress = 0;
static size_t s_failedSparseSize = 0;

static void memory_releaseSparseRange(uintptr_t address, size_t size)
{
	MemMapper::FreeReservation(reinterpret_cast<void*>(address), size);
}

static bool memory_reserveSparseRange(uintptr_t base, uint64_t offset, uint64_t size)
{
	return s_sparseReservation.Ensure(base, offset, size, MemMapper::GetPageSize(),
		[](uintptr_t address, size_t length) {
			if (MemMapper::ReserveMemory(reinterpret_cast<void*>(address), length,
				MemMapper::PAGE_PERMISSION::P_NONE) == reinterpret_cast<void*>(address))
				return true;
			s_failedSparseAddress = address;
			s_failedSparseSize = length;
			return false;
		}, memory_releaseSparseRange);
}
#endif

static void* memory_commitRange(uint32 offset, uint32 size)
{
#if BOOST_OS_IOS
	if (s_usingSparseReservation && !memory_reserveSparseRange(reinterpret_cast<uintptr_t>(memory_base), offset, size))
	{
		cemuLog_log(LogType::Force, "Sparse reservation failed: host {:#x}, size {:#x}", s_failedSparseAddress, s_failedSparseSize);
		return nullptr;
	}
#endif
	return MemMapper::AllocateMemory(memory_base + offset, size, MemMapper::PAGE_PERMISSION::P_RW, true);
}

void checkMemAlloc(void* result)
{
	if (result == nullptr)
		assert_dbg();
}

void memory_initPhysicalLayout()
{
	assert_dbg();

}

std::vector<struct MMURange*> g_mmuRanges;

std::vector<MMURange*> memory_getMMURanges()
{
	return g_mmuRanges;
}

MMURange* memory_getMMURangeByAddress(MPTR address)
{
	for (auto& itr : g_mmuRanges)
	{
		if (address >= itr->getBase() && address < itr->getEnd())
			return itr;
	}
	return nullptr;
}

MMURange::MMURange(const uint32 baseAddress, const uint32 size, MMU_MEM_AREA_ID areaId, const std::string_view name, MFLAG flags) : baseAddress(baseAddress), size(size), initSize(size), areaId(areaId), name(name), flags(flags)
{
	g_mmuRanges.emplace_back(this);
}

void MMURange::mapMem()
{
	cemu_assert_debug(!m_isMapped);
	if (memory_commitRange(baseAddress, size) == nullptr)
	{
		std::string errorMsg = _tr("Unable to allocate {} memory", name);
		WindowSystem::ShowErrorDialog(errorMsg, _tr("Error"));
		#if BOOST_OS_WINDOWS
		ExitProcess(-1);
		#else
		exit(-1);
		#endif
	}
	m_isMapped = true;
}

static std::mutex s_gpuMappingMutex;
static std::vector<std::pair<MPTR, uint32>> s_gpuMappings;
static size_t s_keptRangeCount = 0;

void memory_registerGpuMapping(MPTR baseAddress, uint32 size)
{
	std::lock_guard<std::mutex> lock(s_gpuMappingMutex);
	s_gpuMappings.emplace_back(baseAddress, size);
}

void memory_unregisterGpuMapping(MPTR baseAddress, uint32 size)
{
	std::lock_guard<std::mutex> lock(s_gpuMappingMutex);
	auto it = std::find(s_gpuMappings.begin(), s_gpuMappings.end(), std::make_pair(baseAddress, size));
	if (it != s_gpuMappings.end())
		s_gpuMappings.erase(it);
}

bool memory_isRangeGpuMapped(MPTR baseAddress, uint32 size)
{
	std::lock_guard<std::mutex> lock(s_gpuMappingMutex);
	for (auto& m : s_gpuMappings)
	{
		if ((uint64)baseAddress < (uint64)m.first + m.second && (uint64)m.first < (uint64)baseAddress + size)
			return true;
	}
	return false;
}

size_t memory_getKeptRangeCount()
{
	return s_keptRangeCount;
}

void MMURange::unmapMem()
{
	if (memory_isRangeGpuMapped(baseAddress, size))
	{
		MemMapper::FreeMemoryKeepPages(memory_base + baseAddress, size);
		s_keptRangeCount++;
		cemuLog_log(LogType::Force, "clean slate: kept {} pages (GPU mapping still alive)", name);
	}
	else
	{
		const auto start = std::chrono::steady_clock::now();
		MemMapper::FreeMemory(memory_base + baseAddress, size, true);
		const auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
		cemuLog_log(LogType::Force, "Guest memory: replaced {} ({} MiB) with fresh pages in {}.{} ms", name, size / (1024 * 1024), us / 1000, (us % 1000) / 100);
	}
	m_isMapped = false;
}

MMURange mmuRange_LOW0					{ 0x00010000, 0x000F0000, MMU_MEM_AREA_ID::CODE_LOW0, "CODE_LOW0" };
MMURange mmuRange_TRAMPOLINE_AREA		{ 0x00E00000, 0x00200000, MMU_MEM_AREA_ID::CODE_TRAMPOLINE, "TRAMPOLINE_AREA" };
MMURange mmuRange_CODECAVE				{ 0x01800000, 0x00400000, MMU_MEM_AREA_ID::CODE_CAVE, "CODECAVE" };
MMURange mmuRange_TEXT_AREA				{ 0x02000000, 0x0C000000, MMU_MEM_AREA_ID::CODE_MAIN, "TEXT_AREA" };
MMURange mmuRange_CEMU_AREA				{ 0x0E000000, 0x02000000, MMU_MEM_AREA_ID::CEMU_PRIVATE, "CEMU_AREA", MMURange::MFLAG::FLAG_MAP_EARLY };
MMURange mmuRange_MEM2					{ 0x10000000, 0x40000000, MMU_MEM_AREA_ID::MEM2_DATA, "MEM2" };
MMURange mmuRange_OVERLAY_AREA			{ 0xA0000000, 0x1C000000, MMU_MEM_AREA_ID::OVERLAY, "OVERLAY_AREA", MMURange::MFLAG::FLAG_OPTIONAL };
MMURange mmuRange_FGBUCKET				{ 0xE0000000, 0x04000000, MMU_MEM_AREA_ID::FGBUCKET, "FGBUCKET" };
MMURange mmuRange_TILINGAPERTURE		{ 0xE8000000, 0x02000000, MMU_MEM_AREA_ID::TILING_APERATURE, "TILINGAPERTURE" };
MMURange mmuRange_MEM1					{ 0xF4000000, 0x02000000, MMU_MEM_AREA_ID::MEM1, "MEM1" };
MMURange mmuRange_RPLLOADER				{ 0xF6000000, 0x02000000, MMU_MEM_AREA_ID::RPLLOADER, "RPLLOADER_AREA" };
MMURange mmuRange_SHARED_AREA			{ 0xF8000000, 0x02000000, MMU_MEM_AREA_ID::SHAREDDATA, "SHARED_AREA", MMURange::MFLAG::FLAG_MAP_EARLY };
MMURange mmuRange_CORE0_LC				{ 0xFFC00000, 0x00005000, MMU_MEM_AREA_ID::CPU_LC0, "CORE0_LC" };
MMURange mmuRange_CORE1_LC				{ 0xFFC40000, 0x00005000, MMU_MEM_AREA_ID::CPU_LC1, "CORE1_LC" };
MMURange mmuRange_CORE2_LC				{ 0xFFC80000, 0x00005000, MMU_MEM_AREA_ID::CPU_LC2, "CORE2_LC" };
MMURange mmuRange_HIGHMEM				{ 0xFFFFF000, 0x00001000, MMU_MEM_AREA_ID::CPU_PER_CORE, "PER-CORE" };

void memory_init()
{
	if (!memory_base)
		memory_base = (uint8*)MemMapper::ReserveMemory(nullptr, size_t{0x100000000ULL}, MemMapper::PAGE_PERMISSION::P_RW);
#if BOOST_OS_IOS
	if (!memory_base)
	{
		cemuLog_log(LogType::Force, "Contiguous guest reservation failed; trying sparse reservations (host page size: {})", MemMapper::GetPageSize());

		for (uintptr_t base = 0x10000000ULL; base < 0x1000000000ULL; base += 0x10000000ULL)
		{
			bool success = true;
			for (auto* range : g_mmuRanges)
			{
				if (!memory_reserveSparseRange(base, range->getBase(), range->getSize()))
				{
					success = false;
					break;
				}
			}

			if (success)
				success = memory_reserveSparseRange(base, 0x00800000, 0x00800000);
			if (success)
			{
				memory_base = reinterpret_cast<uint8*>(base);
				s_usingSparseReservation = true;
				cemuLog_log(LogType::Force, "Using sparse iOS guest memory reservations");
				break;
			}
			s_sparseReservation.ReleaseAll(base, memory_releaseSparseRange);
		}
		if (!memory_base)
			cemuLog_log(LogType::Force, "Sparse guest reservation failed: last host address {:#x}, size {:#x}", s_failedSparseAddress, s_failedSparseSize);
	}
#endif
	if( !memory_base )
	{
		debug_printf("memory_init(): Unable to reserve guest memory regions\n");
		debugBreakpoint();
		WindowSystem::ShowErrorDialog(_tr("Unable to reserve guest memory regions"), _tr("Error"));
		exit(-1);
	}
	for (auto& itr : g_mmuRanges)
	{
		if (itr->isMappedEarly())
			itr->mapMem();
	}
}

void memory_mapForCurrentTitle()
{
	for (auto& itr : g_mmuRanges)
		if(!itr->isMapped())
			itr->resetConfig();
	auto gfxPackMappings = GraphicPack2::GetActiveRAMMappings();
	for (auto& mapping : gfxPackMappings)
	{
		MMURange* mmuRange = nullptr;
		for (auto& itr : g_mmuRanges)
		{
			if (itr->getBase() == mapping.first)
			{
				mmuRange = itr;
				break;
			}
		}
		if (!mmuRange)
		{
			cemuLog_log(LogType::Force, fmt::format("Graphic pack error: Unable to apply modified RAM mapping {:08x}-{:08x}. Start address must match one of the existing MMU ranges:", mapping.first, mapping.second));
			for (auto& itr : g_mmuRanges)
			{
				if(itr->isMapped())
					continue;
				cemuLog_log(LogType::Force, fmt::format("{:08x}-{:08x} ({:})", itr->getBase(), itr->getEnd(), itr->getName()));
			}
			continue;
		}
		bool isOverlapping = false;
		for (auto& itr : g_mmuRanges)
		{
			if(itr == mmuRange)
				continue;
			if (mapping.first < itr->getEnd() && mapping.second > itr->getBase())
			{
				cemuLog_log(LogType::Force, fmt::format("Graphic pack error: Unable to apply modified memory range {:08x}-{:08x} since it is overlapping with {:08x}-{:08x} ({:})", mapping.first, mapping.second, itr->getBase(), itr->getEnd(), itr->getName()));
				isOverlapping = true;
			}
		}
		if(isOverlapping)
			continue;
		mmuRange->setEnd(mapping.second);
	}

	for (auto& itr : g_mmuRanges)
	{
		if (!itr->isOptional() && !itr->isMappedEarly())
			itr->mapMem();
	}
}

void memory_unmapForCurrentTitle()
{
	s_keptRangeCount = 0;
    for (auto& itr : g_mmuRanges)
    {
        if (itr->isMapped() && !itr->isMappedEarly())
            itr->unmapMem();
    }
}

void memory_logModifiedMemoryRanges()
{
	auto gfxPackMappings = GraphicPack2::GetActiveRAMMappings();
	for (auto& mapping : gfxPackMappings)
	{
		MMURange* mmuRange = nullptr;
		for (auto& itr : g_mmuRanges)
		{
			if (itr->getBase() == mapping.first)
			{
				mmuRange = itr;
				break;
			}
		}
		if (!mmuRange)
			continue;
		sint32 extraMem = (sint32)mapping.second - (sint32)(mmuRange->getBase() + mmuRange->getInitSize());
		extraMem = (extraMem + 1023) / 1024;
		std::string memAmountStr;
		if (extraMem >= 8 * 1024 * 1024)
			memAmountStr = fmt::format("{:+}MiB", (extraMem + 1023) / 1024);
		else
			memAmountStr = fmt::format("{:+}KiB", extraMem);
		cemuLog_log(LogType::Force, fmt::format("Graphic pack: Using modified RAM mapping {:08x}-{:08x} ({})", mapping.first, mapping.second, memAmountStr));
	}
}

void memory_enableOverlayArena()
{
	if (mmuRange_OVERLAY_AREA.isMapped())
		return;
	mmuRange_OVERLAY_AREA.mapMem();
}

void memory_enableHBLELFCodeArea()
{
	if (memory_elfCodeArena != NULL)
		return;
	memory_elfCodeArena = (uint8*)memory_commitRange(0x00800000, 0x00800000);
	if (memory_elfCodeArena == NULL)
	{
		debug_printf("memory_enableHBLELFCodeArea(): Unable to allocate memory for ELF arena\n");
		debugBreakpoint();
	}
}

bool memory_isAddressRangeAccessible(MPTR virtualAddress, uint32 size)
{
	for (auto& itr : g_mmuRanges)
	{
		if(!itr->isMapped())
			continue;
		if (virtualAddress >= itr->getBase() && virtualAddress < itr->getEnd())
		{
			uint32 remainingSize = itr->getEnd() - virtualAddress;
			return size <= remainingSize && itr->isMapped();
		}
	}
	return false;
}

uint32 memory_virtualToPhysical(uint32 virtualOffset)
{
	return virtualOffset;
}

uint32 memory_physicalToVirtual(uint32 physicalOffset)
{
	return physicalOffset;
}

uint8* memory_getPointerFromPhysicalOffset(uint32 physicalOffset)
{
	return memory_base + physicalOffset;
}

uint32 memory_getVirtualOffsetFromPointer(void* ptr)
{
	if( !ptr )
		return MPTR_NULL;
	return (uint32)((uint8*)ptr - (uint8*)memory_base);
}

uint8* memory_getPointerFromVirtualOffset(uint32 virtualOffset)
{
	return memory_base + virtualOffset;
}

uint8* memory_getPointerFromVirtualOffsetAllowNull(uint32 virtualOffset)
{
	if( virtualOffset == MPTR_NULL )
		return nullptr;
	return memory_getPointerFromVirtualOffset(virtualOffset);
}

void memory_writeDouble(uint32 address, double vf)
{
	uint64 v = *(uint64*)&vf;
	uint32 v1 = v&0xFFFFFFFF;
	uint32 v2 = v>>32;
	uint8* ptr = memory_getPointerFromVirtualOffset(address);
	*(uint32*)(ptr+4) = CPU_swapEndianU32(v1);
	*(uint32*)(ptr+0) = CPU_swapEndianU32(v2);
}

void memory_writeFloat(uint32 address, float vf)
{
	uint32 v = *(uint32*)&vf;
	*(uint32*)(memory_getPointerFromVirtualOffset(address)) = CPU_swapEndianU32(v);
}

void memory_writeU32(uint32 address, uint32 v)
{
	*(uint32*)(memory_getPointerFromVirtualOffset(address)) = CPU_swapEndianU32(v);
}

void memory_writeU64(uint32 address, uint64 v)
{
	*(uint64*)(memory_getPointerFromVirtualOffset(address)) = CPU_swapEndianU64(v);
}

void memory_writeU16(uint32 address, uint16 v)
{
	*(uint16*)(memory_getPointerFromVirtualOffset(address)) = CPU_swapEndianU16(v);
}

void memory_writeU8(uint32 address, uint8 v)
{
	*(uint8*)(memory_getPointerFromVirtualOffset(address)) = v;
}

double memory_readDouble(uint32 address)
{
	uint32 v[2];
	v[1] = *(uint32*)(memory_getPointerFromVirtualOffset(address));
	v[0] = *(uint32*)(memory_getPointerFromVirtualOffset(address)+4);
	v[0] = CPU_swapEndianU32(v[0]);
	v[1] = CPU_swapEndianU32(v[1]);
	return *(double*)v;
}

float memory_readFloat(uint32 address)
{
	uint32 v = *(uint32*)(memory_getPointerFromVirtualOffset(address));
	v = CPU_swapEndianU32(v);
	return *(float*)&v;
}

uint64 memory_readU64(uint32 address)
{
	uint64 v = *(uint64*)(memory_getPointerFromVirtualOffset(address));
	return CPU_swapEndianU64(v);
}

uint32 memory_readU32(uint32 address)
{
	uint32 v = *(uint32*)(memory_getPointerFromVirtualOffset(address));
	return CPU_swapEndianU32(v);
}

uint16 memory_readU16(uint32 address)
{
	uint16 v = *(uint16*)(memory_getPointerFromVirtualOffset(address));
	return CPU_swapEndianU16(v);
}

uint8 memory_readU8(uint32 address)
{
	return *(uint8*)(memory_getPointerFromVirtualOffset(address));
}

uint32 memory_getContiguousReadableBytes(uint32 virtualAddress)
{
    uint32 maxBytes = 0;

    for (auto& r : g_mmuRanges)
    {
        if (!r->isMapped())
            continue;

        if (virtualAddress >= r->getBase() && virtualAddress < r->getEnd())
        {
            uint32 remaining = r->getEnd() - virtualAddress;
            return remaining;
        }
    }

    return 0;
}

extern "C" DLLEXPORT void* memory_getBase()
{
	return memory_base;
}

void memory_writeDumpFile(uint32 startAddr, uint32 size, const fs::path& path)
{
	fs::path filePath = path;
	filePath /= fmt::format("{:08x}.bin", startAddr);
	FileStream* fs = FileStream::createFile2(filePath);
	if (fs)
	{
		fs->writeData(memory_base + startAddr, size);
		delete fs;
	}
}

void memory_createDump()
{
	const uint32 pageSize = MemMapper::GetPageSize();
	fs::path path = ActiveSettings::GetUserDataPath("dump/ramDump{:}", (uint32)time(nullptr));
	fs::create_directories(path);

	for (auto& itr : g_mmuRanges)
	{
		if(!itr->isMapped())
			continue;
		memory_writeDumpFile(itr->getBase(), itr->getSize(), path);
	}
}

namespace MMU
{

	std::unordered_map<PAddr, MMIOFuncWrite32>* g_mmioHandlerW32{};
	std::unordered_map<PAddr, MMIOFuncWrite16>* g_mmioHandlerW16{};
	std::unordered_map<PAddr, MMIOFuncRead32>* g_mmioHandlerR32{};
	std::unordered_map<PAddr, MMIOFuncRead16>* g_mmioHandlerR16{};

	void _initHandlers()
	{
		if (g_mmioHandlerW32)
			return;
		g_mmioHandlerW32 = new std::unordered_map<PAddr, MMIOFuncWrite32>();
		g_mmioHandlerW16 = new std::unordered_map<PAddr, MMIOFuncWrite16>();
		g_mmioHandlerR32 = new std::unordered_map<PAddr, MMIOFuncRead32>();
		g_mmioHandlerR16 = new std::unordered_map<PAddr, MMIOFuncRead16>();
	}

	PAddr _MakeMMIOAddress(MMIOInterface interfaceLocation, uint32 relativeAddress)
	{
		PAddr addr = 0;
		if (interfaceLocation == MMIOInterface::INTERFACE_0C000000)
			addr = 0x0C000000;
		else if (interfaceLocation == MMIOInterface::INTERFACE_0D000000)
			addr = 0x0D000000;
		else
			assert_dbg();
		return addr + relativeAddress;
	}

	void RegisterMMIO_W32(MMIOInterface interfaceLocation, uint32 relativeAddress, MMIOFuncWrite32 ptr)
	{
		_initHandlers();
		g_mmioHandlerW32->emplace(_MakeMMIOAddress(interfaceLocation, relativeAddress), ptr);
	}

	void RegisterMMIO_W16(MMIOInterface interfaceLocation, uint32 relativeAddress, MMIOFuncWrite16 ptr)
	{
		_initHandlers();
		g_mmioHandlerW16->emplace(_MakeMMIOAddress(interfaceLocation, relativeAddress), ptr);
	}

	void RegisterMMIO_R32(MMIOInterface interfaceLocation, uint32 relativeAddress, MMIOFuncRead32 ptr)
	{
		_initHandlers();
		PAddr addr = _MakeMMIOAddress(interfaceLocation, relativeAddress);
		g_mmioHandlerR32->emplace(addr, ptr);
	}

	void RegisterMMIO_R16(MMIOInterface interfaceLocation, uint32 relativeAddress, MMIOFuncRead16 ptr)
	{
		_initHandlers();
		g_mmioHandlerR16->emplace(_MakeMMIOAddress(interfaceLocation, relativeAddress), ptr);
	}

	void WriteMMIO_32(PAddr address, uint32 value)
	{
		cemu_assert_debug((address & 0x3) == 0);
		auto itr = g_mmioHandlerW32->find(address);
		if (itr == g_mmioHandlerW32->end())
		{
			return;
		}
		return itr->second(address, value);
	}

	void WriteMMIO_16(PAddr address, uint16 value)
	{
		cemu_assert_debug((address & 0x1) == 0);
		auto itr = g_mmioHandlerW16->find(address);
		if (itr == g_mmioHandlerW16->end())
		{
			return;
		}
		return itr->second(address, value);
	}

	uint32 ReadMMIO_32(PAddr address)
	{
		cemu_assert_debug((address & 0x3) == 0);
		auto itr = g_mmioHandlerR32->find(address);
		if(itr == g_mmioHandlerR32->end())
		{
			return 0;
		}
		return itr->second(address);
	}

	uint16 ReadMMIO_16(PAddr address)
	{
		cemu_assert_debug((address & 0x1) == 0);
		auto itr = g_mmioHandlerR16->find(address);
		if (itr == g_mmioHandlerR16->end())
		{
			return 0;
		}
		return itr->second(address);
	}

}

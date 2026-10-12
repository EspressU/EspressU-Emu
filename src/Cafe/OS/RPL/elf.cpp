#include <zlib.h>
#include "Cafe/OS/RPL/rpl.h"
#include "Cafe/OS/RPL/rpl_structs.h"
#include "util/VirtualHeap/VirtualHeap.h"
#include "Cafe/HW/Espresso/Recompiler/PPCRecompiler.h"
#include "Cafe/HW/Espresso/Debugger/Debugger.h"

typedef struct
{
	 uint32be magic;
	 uint8 eiClass;
	 uint8 eiData;
	 uint8 eiVersion;
	 uint8 eiOSABI;
	 uint8 eiOSABIVersion;
	 uint8 eiPadding[7];
	 uint16be eType;
	 uint16be eMachine;
	 uint32be eVersion;
	 uint32be entrypoint;
	 uint32be phOffset;
	 uint32be shOffset;

	 uint32be eFlags;
	 uint16be eHeaderSize;
	 uint16be ePHEntrySize;
	 uint16be ePHNum;
	 uint16be eSHEntrySize;
	 uint16be eSHNum;
	 uint16be eShStrIndex;
}elfHeader_t;

static_assert(sizeof(elfHeader_t) == 0x34, "");

typedef struct
{
	 uint32be nameOffset;
	 uint32be shType;
	 uint32be shFlags;
	 uint32be shAddr;
	 uint32be shOffset;
	 uint32be shSize;

	 uint32be shLink;
	 uint32be shInfo;

	 uint32be shAddrAlign;
	 uint32be shEntSize;
}elfSectionEntry_t;

static_assert(sizeof(elfSectionEntry_t) == 0x28, "");

#define PF_X		(1 << 0)
#define PF_W		(1 << 1)
#define PF_R		(1 << 2)

uint32 ELF_LoadFromMemory(uint8* elfData, sint32 size, const char* name)
{
	elfHeader_t* header = (elfHeader_t*)elfData;

	uint32 sectionCount = header->eSHNum;
	uint32 sectionTableOffset = header->shOffset;
	uint32 sectionTableEntrySize = header->eSHEntrySize;

	elfSectionEntry_t* sectionTable = (elfSectionEntry_t*)(elfData + (uint32)header->shOffset);
	memory_enableHBLELFCodeArea();
	for (uint32 i = 0; i < sectionCount; i++)
	{
		debug_printf("%02d Addr %08x Size %08x Offs %08x Flags %08x Type %08x EntSize %08x\n", i, (uint32)sectionTable[i].shAddr, (uint32)sectionTable[i].shSize, (uint32)sectionTable[i].shOffset, (uint32)sectionTable[i].shFlags, (uint32)sectionTable[i].shType, (uint32)sectionTable[i].shEntSize);
		uint32 shAddr = (uint32)sectionTable[i].shAddr;
		uint32 shSize = (uint32)sectionTable[i].shSize;
		uint32 shOffset = (uint32)sectionTable[i].shOffset;
		uint32 shType = (uint32)sectionTable[i].shType;
		uint32 shFlags = (uint32)sectionTable[i].shFlags;

		if (shOffset > (uint32)size)
		{
			cemuLog_log(LogType::Force, "ELF section {} out of bounds", i);
			continue;
		}
		uint32 copySize = std::min(shSize, size - shOffset);

		if (shAddr != 0)
		{
			if (shType == SHT_NOBITS)
			{
				memset(memory_getPointerFromVirtualOffset(shAddr), 0, shSize);
			}
			else
			{
				memcpy(memory_getPointerFromVirtualOffset(shAddr), elfData + shOffset, copySize);
			}
		}
		if((shFlags & PF_X) > 0)
			PPCRecompiler_allocateRange(shAddr, shSize);
	}
	return header->entrypoint;
}


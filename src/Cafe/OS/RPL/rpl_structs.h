#pragma once

#include "util/ChunkedHeap/ChunkedHeap.h"

#define RPL_MODULE_NAME_LENGTH	64
#define RPL_MODULE_PATH_LENGTH	256

#define SHT_RPL_EXPORTS			(0x80000001)
#define SHT_RPL_IMPORTS			(0x80000002)
#define SHT_RPL_CRCS			(0x80000003)
#define SHT_RPL_FILEINFO		(0x80000004)

#define SHT_PROGBITS			(0x00000001)
#define SHT_SYMTAB				(0x00000002)
#define SHT_STRTAB				(0x00000003)
#define SHT_RELA				(0x00000004)
#define SHT_HASH				(0x00000005)
#define SHT_DYNAMIC				(0x00000006)
#define SHT_NOTE				(0x00000007)
#define SHT_NOBITS				(0x00000008)
#define SHT_REL					(0x00000009)
#define SHT_SHLIB				(0x0000000A)
#define SHT_DYNSYM				(0x0000000B)

#define SHF_EXECUTE				0x00000004
#define SHF_RPL_COMPRESSED		0x08000000
#define SHF_TLS					0x04000000

#define RPL_RELOC_ADDR32	1
#define RPL_RELOC_LO16		4
#define RPL_RELOC_HI16		5
#define RPL_RELOC_HA16		6
#define RPL_RELOC_REL24		10
#define RPL_RELOC_REL14		11

#define R_PPC_DTPMOD32		68
#define R_PPC_DTPREL32		78

#define R_PPC_REL16_HA		251
#define R_PPC_REL16_HI		252
#define R_PPC_REL16_LO		253

#define HLE_MODULE_PTR		((RPLModule*)-1)

struct RPLFileRelocEntry
{
	 uint32be relocOffset;
	 uint32be symbolIndexAndType;
	 uint32be relocAddend;
};

struct RPLFileSectionEntry
{
	 uint32be nameOffset;
	 uint32be type;
	 uint32be flags;
	 uint32be virtualAddress;
	 uint32be fileOffset;
	 uint32be sectionSize;
	 uint32be symtabSectionIndex;
	 uint32be relocTargetSectionIndex;
	 uint32be alignment;
	 uint32be symbolEntrySize;
};

struct RPLFileHeader
{
	 uint32be magic1;
	 uint8    version04;
	 uint8    ukn05;
	 uint8    ukn06;
	 uint8    magic2_0;
	 uint8    magic2_1;
	 uint8    ukn09;
	 uint8    ukn0A;
	 uint8    ukn0B;
	 uint32be dataRegionSize;
	 uint8    ukn10;
	 uint8    ukn11;
	 uint16be ukn12;
	 uint32be ukn14;
	 uint32be entrypoint;
	 uint32be ukn1C;
	 uint32be sectionTableOffset;
	 uint32be ukn24;
	 uint16be ukn28;
	 uint16be programHeaderTableEntrySize;
	 uint16be programHeaderTableEntryCount;
	 uint16be sectionTableEntrySize;
	 uint16be sectionTableEntryCount;
	 uint16be nameSectionIndex;
};

static_assert(offsetof(RPLFileHeader, dataRegionSize) == 0xC);
static_assert(offsetof(RPLFileHeader, programHeaderTableEntrySize) == 0x2A);

struct RPLFileInfoData
{
	 uint32be fileInfoMagic;
	 uint32be textRegionSize;
	 uint32be ukn08;
	 uint32be dataRegionSize;
	 uint32be baseAlign;
	 uint32be ukn14;
	 uint32be ukn18;
	 uint32be ukn1C;
	 uint32be trampolineAdjustment;
	 uint32be sdataBase1;
	 uint32be sdataBase2;
	 uint32be ukn2C;
	 uint32be ukn30;
	 uint32be flags;
	 uint32be ukn38;
	 uint32be ukn3C;
	 uint32be minimumToolkitVersion;
	 uint32be ukn44;
	 uint32be ukn48;
	 uint32be ukn4C;
	 uint32be ukn50;
	 uint32be ukn54;
	 sint16be tlsModuleIndex;
};

static_assert(offsetof(RPLFileInfoData, tlsModuleIndex) == 0x58);

struct RPLSectionAddressEntry
{
	void* ptr;
};

struct RPLFileExportTableEntry
{
	uint32be virtualOffset;
	uint32be nameOffset;
};

struct RPLModule
{
	RPLFileHeader rplHeader;
	std::vector<RPLFileSectionEntry> sectionTable;
	uint32 entrypoint;

	MEMPTR<void> regionMappingBase_text;
	MPTR regionMappingBase_data;
	MPTR regionMappingBase_loaderInfo;
	MEMPTR<char> ppcName;
	uint8* tempRegionPtr;
	uint32 tempRegionAllocSize;

	uint32 exportDCount;
	RPLFileExportTableEntry* exportDDataPtr;
	uint32 exportFCount;
	RPLFileExportTableEntry* exportFDataPtr;

	std::string moduleName;

	std::vector<RPLSectionAddressEntry> sectionAddressTable;

	uint32 tlsStartAddress;
	uint32 tlsEndAddress;
	uint32 regionSize_text;
	uint32 regionOrigAddr_text;
	uint32 regionSize_data;
	uint32 regionOrigAddr_data;
	uint32 regionSize_loaderInfo;

	uint32 patchCRC;

	ChunkedFlatAllocator<16 * 1024> heapTrampolineArea;
	std::unordered_map<MPTR, MPTR> trampolineMap;

	std::vector<uint8> sectionData_fileInfo;
	std::vector<uint8> sectionData_crc;

	struct
	{
		uint32 textRegionSize;
		uint32 dataRegionSize;
		uint32 baseAlign;
		uint32 ukn14;
		uint32 trampolineAdjustment;
		uint32 ukn4C;
		sint16 tlsModuleIndex;

		uint32 sdataBase1;
		uint32 sdataBase2;

		uint32 flags;
	}fileInfo;
	std::vector<uint32> crcTable;

	uint32 GetSectionCRC(size_t sectionIndex) const
	{
		if (sectionIndex >= crcTable.size())
			return 0;
		return crcTable[sectionIndex];
	}

	bool IsRPX() const
	{
	    return fileInfo.flags & 2;
	}

	bool isLinked;
	bool entrypointCalled;

	betype<MPTR> funcAlloc;
	betype<MPTR> funcFree;

	std::span<uint8> RPLRawData;

	bool hasError{ false };
};

struct RPLDependency
{
	std::string moduleName;
	bool isMainExecutable{false};
	bool loadAttempted{false};
	bool hleEntrypointCalled{false};
	bool isCafeOSModule{false};
	RPLModule* rplLoaderContext{};
	class COSModule* rplHLEModule{};
	sint32 referenceCount;
	uint32 coreinitHandle;
	sint16 tlsModuleIndex;
};

RPLModule* RPLLoader_FindModuleByCodeAddr(uint32 addr);
RPLModule* RPLLoader_FindModuleByDataAddr(uint32 addr);
RPLModule* RPLLoader_FindModuleByName(std::string moduleName);

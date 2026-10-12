#include "Cafe/TitleList/TitleInfo.h"
#include "Cafe/TitleList/TitleList.h"
#include "Cafe/TitleList/TitleId.h"
#include "Cemu/ncrypto/ncrypto.h"
#include "Common/FileStream.h"

#include <cstdio>
#include <memory>
#include <vector>

bool IOSDlcUpdateImport_ReadTmdTitleId(const char* tmdPath, uint64* titleIdOut)
{
	if (!tmdPath || tmdPath[0] == '\0' || !titleIdOut)
		return false;
	std::unique_ptr<FileStream> tmdFile(FileStream::openFile2(fs::path(tmdPath)));
	if (!tmdFile)
		return false;
	std::vector<uint8> tmdData;
	tmdFile->extract(tmdData);
	NCrypto::TMDParser parser;
	if (!parser.parse(tmdData.data(), tmdData.size()))
		return false;
	*titleIdOut = parser.getTitleId();
	return true;
}

static bool IOSDlcUpdateImport_DeriveTitleId_Impl(const char* romPath, uint64* titleIdOut)
{
	if (!romPath || romPath[0] == '\0' || !titleIdOut)
		return false;

	TitleInfo titleInfo{fs::path(romPath)};
	if (!titleInfo.IsValid() || !titleInfo.HasValidXmlInfo())
		return false;

	*titleIdOut = (uint64)titleInfo.GetAppTitleId();
	return true;
}

static bool IOSDlcUpdateImport_Inspect_Impl(const char* romPath, uint64* outTitleId, uint16* outVersion,
	int* outRegion, int* outInvalidReason)
{
	if (outInvalidReason)
		*outInvalidReason = 0;

	if (!romPath || romPath[0] == '\0')
	{
		if (outInvalidReason)
			*outInvalidReason = 1;
		return false;
	}

	TitleInfo titleInfo{fs::path(romPath)};
	if (!titleInfo.IsValid())
	{
		if (outInvalidReason)
			*outInvalidReason = (int)titleInfo.GetInvalidReason();
		return false;
	}
	if (!titleInfo.HasValidXmlInfo())
	{
		if (outInvalidReason)
			*outInvalidReason = (int)TitleInfo::InvalidReason::MISSING_XML_FILES;
		return false;
	}

	if (outTitleId)
		*outTitleId = (uint64)titleInfo.GetAppTitleId();
	if (outVersion)
		*outVersion = titleInfo.GetAppTitleVersion();
	if (outRegion)
		*outRegion = (int)titleInfo.GetMetaRegion();
	return true;
}

uint64 IOSDlcUpdateImport_DeriveBaseTitleId(uint64 titleId)
{
	TitleId baseTitleId;
	if (!CafeTitleList::FindBaseTitleId((TitleId)titleId, baseTitleId))
		return titleId;
	return (uint64)baseTitleId;
}

uint64 IOSDlcUpdateImport_DeriveContentTitleId(uint64 baseTitleId, bool isUpdate)
{
	TitleIdParser tip((TitleId)baseTitleId);
	if (isUpdate)
	{
		if (!tip.CanHaveSeparateUpdateTitleId())
			return 0;
		return (uint64)tip.GetSeparateUpdateTitleId();
	}
	return baseTitleId | 0xC00000000ull;
}

void IOSDlcUpdateImport_GetMlcTitlePathComponents(uint64 titleId, char* outUpperHex, char* outLowerHex)
{
	uint32 upper = (uint32)(titleId >> 32);
	uint32 lower = (uint32)(titleId & 0xFFFFFFFFu);
	snprintf(outUpperHex, 9, "%08x", upper);
	snprintf(outLowerHex, 9, "%08x", lower);
}

int IOSDlcUpdateImport_GetTitleType(uint64 titleId)
{
	TitleIdParser parser((TitleId)titleId);
	return (int)parser.GetType();
}

bool IOSDlcUpdateImport_DeriveTitleId(const char* romPath, uint64* titleIdOut)
{
	try
	{
		return IOSDlcUpdateImport_DeriveTitleId_Impl(romPath, titleIdOut);
	}
	catch (...)
	{
		return false;
	}
}

bool IOSDlcUpdateImport_Inspect(const char* romPath, uint64* outTitleId, uint16* outVersion,
	int* outRegion, int* outInvalidReason)
{
	try
	{
		return IOSDlcUpdateImport_Inspect_Impl(romPath, outTitleId, outVersion, outRegion, outInvalidReason);
	}
	catch (...)
	{
		if (outInvalidReason)
			*outInvalidReason = (int)TitleInfo::InvalidReason::UNKNOWN_FORMAT;
		return false;
	}
}

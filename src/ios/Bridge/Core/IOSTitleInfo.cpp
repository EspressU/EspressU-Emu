// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

#include "Cafe/TitleList/TitleInfo.h"
#include "Cafe/TitleList/TitleList.h"
#include "Cafe/TitleList/TitleId.h"

#include <string>

static std::string IOSTitleInfo_GetTitleName_Impl(const char* romPath)
{
	if (!romPath || romPath[0] == '\0')
		return {};
	TitleInfo titleInfo{fs::path(romPath)};
	if (!titleInfo.IsValid() || !titleInfo.HasValidXmlInfo())
		return {};
	return titleInfo.GetMetaTitleName();
}

std::string IOSTitleInfo_GetTitleName(const char* romPath)
{
	try
	{
		return IOSTitleInfo_GetTitleName_Impl(romPath);
	}
	catch (...)
	{
		return {};
	}
}

bool IOSTitleInfo_DeriveTitleId(const char* romPath, uint64* titleIdOut)
{
	try
	{
		if (!romPath || romPath[0] == '\0' || !titleIdOut)
			return false;
		TitleInfo titleInfo{fs::path(romPath)};
		if (!titleInfo.IsValid() || !titleInfo.HasValidXmlInfo())
			return false;
		*titleIdOut = (uint64)titleInfo.GetAppTitleId();
		return true;
	}
	catch (...)
	{
		return false;
	}
}

uint64 IOSTitleInfo_DeriveBaseTitleId(uint64 titleId)
{
	TitleId baseTitleId;
	if (!CafeTitleList::FindBaseTitleId((TitleId)titleId, baseTitleId))
		return titleId;
	return (uint64)baseTitleId;
}

bool IOSTitleInfo_Inspect(const char* romPath, uint64* outTitleId, uint16* outVersion, int* outRegion, int* outInvalidReason)
{
	try
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
	catch (...)
	{
		return false;
	}
}

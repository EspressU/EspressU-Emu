// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

#include "Cafe/TitleList/TitleInfo.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace
{
std::string ToUpper(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::toupper(c); });
	return s;
}
}

static std::string IOSCoverArt_DeriveGameTdbId_Impl(const char* romPath)
{
	if (!romPath || romPath[0] == '\0')
		return {};

	TitleInfo titleInfo{fs::path(romPath)};
	if (!titleInfo.IsValid() || !titleInfo.HasValidXmlInfo())
		return {};

	ParsedMetaXml* meta = titleInfo.GetMetaInfo();
	if (!meta)
		return {};

	std::string productCode = ToUpper(meta->GetProductCode());
	std::string companyCode = ToUpper(meta->GetCompanyCode());

	if (productCode.size() < 4 || companyCode.size() < 2)
		return {};
	std::string gameCode = productCode.substr(productCode.size() - 4);
	std::string publisherSuffix = companyCode.substr(companyCode.size() - 2);

	if (gameCode == "ABCD" || companyCode == "ZZZZ")
		return {};

	return gameCode + publisherSuffix;
}

static std::string IOSCoverArt_GetTitleName_Impl(const char* romPath)
{
	if (!romPath || romPath[0] == '\0')
		return {};
	TitleInfo titleInfo{fs::path(romPath)};
	if (!titleInfo.IsValid() || !titleInfo.HasValidXmlInfo())
		return {};
	return titleInfo.GetMetaTitleName();
}

std::string IOSCoverArt_DeriveGameTdbId(const char* romPath)
{
	try
	{
		return IOSCoverArt_DeriveGameTdbId_Impl(romPath);
	}
	catch (...)
	{
		return {};
	}
}

std::string IOSCoverArt_GetTitleName(const char* romPath)
{
	try
	{
		return IOSCoverArt_GetTitleName_Impl(romPath);
	}
	catch (...)
	{
		return {};
	}
}

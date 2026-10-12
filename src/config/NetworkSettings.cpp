#include "NetworkSettings.h"
#include "ActiveSettings.h"
#include "LaunchSettings.h"
#include "CemuConfig.h"
#include "Common/FileStream.h"
#include <fstream>

XMLNetworkConfig_t n_config(L"network_services.xml");

void NetworkConfig::LoadOnce()
{
	n_config.SetFilename(ActiveSettings::GetConfigPath("network_services.xml").generic_wstring());
	if (XMLExists())
		n_config.Load();
}

void NetworkConfig::Load(XMLConfigParser& parser)
{
	auto config = parser.get("content");
	networkname = config.get("networkname", "Custom");
	disablesslver = config.get("disablesslverification", disablesslver);
	auto u = config.get("urls");
	urls.ACT = u.get("act", NintendoURLs::ACTURL);
	urls.ECS = u.get("ecs", NintendoURLs::ECSURL);
	urls.NUS = u.get("nus", NintendoURLs::NUSURL);
	urls.IAS = u.get("ias", NintendoURLs::IASURL);
	urls.CCSU = u.get("ccsu", NintendoURLs::CCSUURL);
	urls.CCS = u.get("ccs", NintendoURLs::CCSURL);
	urls.IDBE = u.get("idbe", NintendoURLs::IDBEURL);
	urls.BOSS = u.get("boss", NintendoURLs::BOSSURL);
	urls.TAGAYA = u.get("tagaya", NintendoURLs::TAGAYAURL);
	urls.OLV = u.get("olv", NintendoURLs::OLVURL);
}

static std::optional<bool> s_xmlUsable;

bool NetworkConfig::IsValidFile(const fs::path& path)
{
	std::error_code ec;
	if (!fs::exists(path, ec))
		return false;
	std::ifstream file(path, std::ios::binary);
	if (!file.is_open())
		return false;
	const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	if (text.empty() || text.size() > 256 * 1024)
		return false;
	tinyxml2::XMLDocument doc;
	if (doc.Parse(text.data(), text.size()) != tinyxml2::XML_SUCCESS)
		return false;
	const tinyxml2::XMLElement* content = doc.FirstChildElement("content");
	if (!content)
		return false;
	const tinyxml2::XMLElement* urls = content->FirstChildElement("urls");
	if (!urls)
		return false;
	for (const tinyxml2::XMLElement* e = urls->FirstChildElement(); e; e = e->NextSiblingElement())
	{
		const char* value = e->GetText();
		if (value && (strncmp(value, "http://", 7) == 0 || strncmp(value, "https://", 8) == 0))
			return true;
	}
	return false;
}

bool NetworkConfig::XMLExists()
{
	if (s_xmlUsable.has_value())
		return *s_xmlUsable;
	s_xmlUsable = IsValidFile(ActiveSettings::GetConfigPath("network_services.xml"));
	return *s_xmlUsable;
}

void NetworkConfig::Reload()
{
	s_xmlUsable.reset();
	n_config.SetFilename(ActiveSettings::GetConfigPath("network_services.xml").generic_wstring());
	if (XMLExists())
		n_config.Load();
}

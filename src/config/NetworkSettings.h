#pragma once

#include "ConfigValue.h"
#include "XMLConfig.h"

// EspressU contains no online services. The enum stays so that stored configs and game profiles still parse.
enum class NetworkService
{
	Offline,
	Online1,
	Online2,
	Custom,
	COUNT = Custom
};

struct NetworkConfig
{
	NetworkConfig() = default;
	NetworkConfig(const NetworkConfig&) = delete;

	ConfigValue<std::string> networkname;
	ConfigValue<bool> disablesslver = false;
	struct
	{
		ConfigValue<std::string> ACT;
		ConfigValue<std::string> ECS;
		ConfigValue<std::string> NUS;
		ConfigValue<std::string> IAS;
		ConfigValue<std::string> CCSU;
		ConfigValue<std::string> CCS;
		ConfigValue<std::string> IDBE;
		ConfigValue<std::string> BOSS;
		ConfigValue<std::string> TAGAYA;
		ConfigValue<std::string> OLV;
	} urls{};

public:
	static void LoadOnce() {}
	void Load(XMLConfigParser& parser) {}
	void Save(XMLConfigParser& parser) {}

	static bool XMLExists() { return false; }
	static void Reload() {}
	static bool IsValidFile(const fs::path& path) { return false; }
};

// Every service address is empty: no request can be built.
struct OfflineURLs {
	inline static std::string ACTURL;
	inline static std::string ECSURL;
	inline static std::string NUSURL;
	inline static std::string IASURL;
	inline static std::string CCSUURL;
	inline static std::string CCSURL;
	inline static std::string IDBEURL;
	inline static std::string BOSSURL;
	inline static std::string TAGAYAURL;
	inline static std::string OLVURL;
};

typedef XMLDataConfig<NetworkConfig> XMLNetworkConfig_t;
extern XMLNetworkConfig_t n_config;
inline NetworkConfig& GetNetworkConfig() { return n_config.data(); }

inline bool IsNetworkServiceSSLDisabled(NetworkService service)
{
	return false;
}

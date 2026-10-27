#pragma once

#include "ConfigValue.h"
#include "XMLConfig.h"

enum class NetworkService;

struct GameEntry
{
	GameEntry() = default;

	std::wstring rpx_file;
	std::wstring legacy_name;
	std::string product_code;
	std::string company_code;

	std::string custom_name;

	sint32 icon = -1;
	sint32 icon_small = -1;
	sint32 legacy_version = 0;
	sint32 legacy_update_version = -1;
	sint32 legacy_dlc_version = -1;
	uint64 title_id = 0;
	uint32 legacy_region = 0;

	std::wstring save_folder;
	std::wstring update_folder;
	std::wstring dlc_folder;

	uint64 legacy_time_played = 0;
	uint64 legacy_last_played = 0;

	bool favorite = false;

	bool is_update = false;
};

struct GraphicPackEntry
{
	GraphicPackEntry(std::wstring_view filename,std::string_view category, std::string_view preset)
		: filename(filename)
	{
		presets[std::string{category}] = preset;
	}

	GraphicPackEntry(std::wstring_view filename, std::string_view preset)
		: filename(filename)
	{
		presets[""] = preset;
	}

	GraphicPackEntry(std::wstring filename)
		: filename(std::move(filename))
	{}

	struct Preset
	{
		std::string name;
		std::string category;
	};
	std::unordered_map<std::string, std::string> presets;

	std::wstring filename;
	bool enabled = true;
};

enum GraphicAPI
{
	kOpenGL = 0,
	kMetal = 2, // 2 keeps saved settings working
	COUNT
};

#if defined(ENABLE_METAL)
constexpr GraphicAPI kDefaultGraphicsAPI = kMetal;
#elif defined(ENABLE_OPENGL)
constexpr GraphicAPI kDefaultGraphicsAPI = kOpenGL;
#endif

enum AudioChannels
{
	kMono = 0,
	kStereo,
	kSurround,
};

enum UpscalingFilter
{
	kLinearFilter,
	kBicubicFilter,
	kBicubicHermiteFilter,
	kNearestNeighborFilter,
};

enum FullscreenScaling
{
	kKeepAspectRatio,
	kStretch,
};

enum class ScreenPosition
{
	kDisabled = 0,
	kTopLeft,
	kTopCenter,
	kTopRight,
	kBottomLeft,
	kBottomCenter,
	kBottomRight,
};

enum class PrecompiledShaderOption
{
	Auto,
	Enable,
	Disable,
};
ENABLE_ENUM_ITERATORS(PrecompiledShaderOption, PrecompiledShaderOption::Auto, PrecompiledShaderOption::Disable);

enum class AccurateShaderMulOption
{
	False = 0,
	True = 1
};
ENABLE_ENUM_ITERATORS(AccurateShaderMulOption, AccurateShaderMulOption::False, AccurateShaderMulOption::True);

enum class MetalBufferCacheMode
{
    Auto,
    DevicePrivate,
    DeviceShared,
    Host,
};
ENABLE_ENUM_ITERATORS(MetalBufferCacheMode, MetalBufferCacheMode::Auto, MetalBufferCacheMode::Host);

enum class PositionInvariance
{
    Auto,
    False,
    True,
};
ENABLE_ENUM_ITERATORS(PositionInvariance, PositionInvariance::False, PositionInvariance::True);

enum class CPUMode
{
	SinglecoreInterpreter = 0,
	SinglecoreRecompiler = 1,
	DualcoreRecompiler = 2,
	MulticoreRecompiler = 3,
	Auto = 4,
	MulticoreInterpreter = 5,
};
ENABLE_ENUM_ITERATORS(CPUMode, CPUMode::SinglecoreInterpreter, CPUMode::MulticoreInterpreter);

enum class CPUModeLegacy
{
	SinglecoreInterpreter = 0,
	SinglecoreRecompiler = 1,
	DualcoreRecompiler = 2,
	TriplecoreRecompiler = 3,
	Auto = 4
};
ENABLE_ENUM_ITERATORS(CPUModeLegacy, CPUModeLegacy::SinglecoreInterpreter, CPUModeLegacy::Auto);

enum class CafeConsoleRegion
{
	JPN = 0x1,
	USA = 0x2,
	EUR = 0x4,
	AUS_DEPR = 0x8,
	CHN = 0x10,
	KOR = 0x20,
	TWN = 0x40,
	Auto = 0xFF,
};
ENABLE_BITMASK_OPERATORS(CafeConsoleRegion);

enum class CafeConsoleLanguage
{
	JA = 0,
	EN = 1,
	FR = 2,
	DE = 3,
	IT = 4,
	ES = 5,
	ZH = 6,
	KO = 7,
	NL = 8,
	PT = 9,
	RU = 10,
	TW = 11,
};
ENABLE_ENUM_ITERATORS(CafeConsoleLanguage, CafeConsoleLanguage::JA, CafeConsoleLanguage::TW);

#if BOOST_OS_WINDOWS
enum class CrashDump
{
	Disabled,
	Lite,
	Full
};
ENABLE_ENUM_ITERATORS(CrashDump, CrashDump::Disabled, CrashDump::Full);
#elif BOOST_OS_UNIX
enum class CrashDump
{
	Disabled,
	Enabled
};
ENABLE_ENUM_ITERATORS(CrashDump, CrashDump::Disabled, CrashDump::Enabled);
#endif

template <>
struct fmt::formatter<PrecompiledShaderOption> : formatter<string_view> {
	template <typename FormatContext>
	auto format(const PrecompiledShaderOption c, FormatContext &ctx) const {
		string_view name;
		switch (c)
		{
		case PrecompiledShaderOption::Auto: name = "auto"; break;
		case PrecompiledShaderOption::Enable: name = "true"; break;
		case PrecompiledShaderOption::Disable: name = "false"; break;
		default: name = "unknown"; break;
		}
		return formatter<string_view>::format(name, ctx);
	}
};
template <>
struct fmt::formatter<AccurateShaderMulOption> : formatter<string_view> {
	template <typename FormatContext>
	auto format(const AccurateShaderMulOption c, FormatContext &ctx) const {
		string_view name;
		switch (c)
		{
		case AccurateShaderMulOption::True: name = "true"; break;
		case AccurateShaderMulOption::False: name = "false"; break;
		default: name = "unknown"; break;
		}
		return formatter<string_view>::format(name, ctx);
	}
};
template <>
struct fmt::formatter<MetalBufferCacheMode> : formatter<string_view> {
	template <typename FormatContext>
	auto format(const MetalBufferCacheMode c, FormatContext &ctx) const {
		string_view name;
		switch (c)
		{
		case MetalBufferCacheMode::Auto: name = "auto"; break;
		case MetalBufferCacheMode::DevicePrivate: name = "device private"; break;
		case MetalBufferCacheMode::DeviceShared: name = "device shared"; break;
		case MetalBufferCacheMode::Host: name = "host"; break;
		default: name = "unknown"; break;
		}
		return formatter<string_view>::format(name, ctx);
	}
};
template <>
struct fmt::formatter<PositionInvariance> : formatter<string_view> {
	template <typename FormatContext>
	auto format(const PositionInvariance c, FormatContext &ctx) const {
		string_view name;
		switch (c)
		{
		case PositionInvariance::Auto: name = "auto"; break;
		case PositionInvariance::False: name = "false"; break;
		case PositionInvariance::True: name = "true"; break;
		default: name = "unknown"; break;
		}
		return formatter<string_view>::format(name, ctx);
	}
};
template <>
struct fmt::formatter<CPUMode> : formatter<string_view> {
	template <typename FormatContext>
	auto format(const CPUMode c, FormatContext &ctx) const {
		string_view name;
		switch (c)
		{
		case CPUMode::SinglecoreInterpreter: name = "Single-core interpreter"; break;
		case CPUMode::SinglecoreRecompiler: name = "Single-core recompiler"; break;
		case CPUMode::DualcoreRecompiler: name = "Dual-core recompiler"; break;
		case CPUMode::MulticoreRecompiler: name = "Multi-core recompiler"; break;
		case CPUMode::Auto: name = "Auto"; break;
		case CPUMode::MulticoreInterpreter: name = "Multi-core interpreter"; break;
		default: name = "unknown"; break;
		}
		return formatter<string_view>::format(name, ctx);
	}
};
template <>
struct fmt::formatter<CPUModeLegacy> : formatter<string_view> {
	template <typename FormatContext>
	auto format(const CPUModeLegacy c, FormatContext &ctx) const {
		string_view name;
		switch (c)
		{
		case CPUModeLegacy::SinglecoreInterpreter: name = "Singlecore-Interpreter"; break;
		case CPUModeLegacy::SinglecoreRecompiler: name = "Singlecore-Recompiler"; break;
		case CPUModeLegacy::DualcoreRecompiler: name = "Dualcore-Recompiler"; break;
		case CPUModeLegacy::TriplecoreRecompiler: name = "Triplecore-Recompiler"; break;
		case CPUModeLegacy::Auto: name = "Auto"; break;
		default: name = "unknown"; break;
		}
		return formatter<string_view>::format(name, ctx);
	}
};
template <>
struct fmt::formatter<CafeConsoleRegion> : formatter<string_view> {
	template <typename FormatContext>
	auto format(const CafeConsoleRegion v, FormatContext &ctx) const {
		string_view name;
		switch (v)
		{
		case CafeConsoleRegion::JPN: name = TR_NOOP("Japan"); break;
		case CafeConsoleRegion::USA: name = TR_NOOP("USA"); break;
		case CafeConsoleRegion::EUR: name = TR_NOOP("Europe"); break;
		case CafeConsoleRegion::AUS_DEPR: name = TR_NOOP("Australia"); break;
		case CafeConsoleRegion::CHN: name = TR_NOOP("China"); break;
		case CafeConsoleRegion::KOR: name = TR_NOOP("Korea"); break;
		case CafeConsoleRegion::TWN: name = TR_NOOP("Taiwan"); break;
		case CafeConsoleRegion::Auto: name = TR_NOOP("Auto"); break;
		default: name = TR_NOOP("many"); break;

		}
		return formatter<string_view>::format(name, ctx);
	}
};
template <>
struct fmt::formatter<CafeConsoleLanguage> : formatter<string_view> {
	template <typename FormatContext>
	auto format(const CafeConsoleLanguage v, FormatContext &ctx) {
		string_view name;
		switch (v)
		{
		case CafeConsoleLanguage::JA: name = "Japanese"; break;
		case CafeConsoleLanguage::EN: name = "English"; break;
		case CafeConsoleLanguage::FR: name = "French"; break;
		case CafeConsoleLanguage::DE: name = "German"; break;
		case CafeConsoleLanguage::IT: name = "Italian"; break;
		case CafeConsoleLanguage::ES: name = "Spanish"; break;
		case CafeConsoleLanguage::ZH: name = "Chinese"; break;
		case CafeConsoleLanguage::KO: name = "Korean"; break;
		case CafeConsoleLanguage::NL: name = "Dutch"; break;
		case CafeConsoleLanguage::PT: name = "Portugese"; break;
		case CafeConsoleLanguage::RU: name = "Russian"; break;
		case CafeConsoleLanguage::TW: name = "Taiwanese"; break;
		default: name = "unknown"; break;
		}
		return formatter<string_view>::format(name, ctx);
	}
};

#if BOOST_OS_WINDOWS
template <>
struct fmt::formatter<CrashDump> : formatter<string_view> {
	template <typename FormatContext>
	auto format(const CrashDump v, FormatContext &ctx) {
		string_view name;
		switch (v)
		{
		case CrashDump::Disabled: name = "Disabled"; break;
		case CrashDump::Lite: name = "Lite"; break;
		case CrashDump::Full: name = "Full"; break;
		default: name = "unknown"; break;

		}
		return formatter<string_view>::format(name, ctx);
	}
};
#elif BOOST_OS_UNIX
template <>
struct fmt::formatter<CrashDump> : formatter<string_view> {
	template <typename FormatContext>
	auto format(const CrashDump v, FormatContext &ctx) {
		string_view name;
		switch (v)
		{
		case CrashDump::Disabled: name = "Disabled"; break;
		case CrashDump::Enabled: name = "Enabled"; break;
		default: name = "unknown"; break;

		}
		return formatter<string_view>::format(name, ctx);
	}
};
#endif

struct CemuConfig
{
	CemuConfig()
	{

	};

	CemuConfig(const CemuConfig&) = delete;

	void SetMLCPath(fs::path path, bool save = true);

	ConfigValue<uint64> log_flag{ 0 };
	ConfigValue<bool> advanced_ppc_logging{ false };

	ConfigValue<bool> permanent_storage{ true };

	ConfigValue<std::string> mlc_path{};
	ConfigValue<std::string> proxy_server{};

#if BOOST_OS_MACOS || BOOST_OS_IOS
#define DISABLE_SCREENSAVER_DEFAULT false
#else
#define DISABLE_SCREENSAVER_DEFAULT true
#endif
	ConfigValue<bool> disable_screensaver{DISABLE_SCREENSAVER_DEFAULT};
#undef DISABLE_SCREENSAVER_DEFAULT
	ConfigValue<bool> play_boot_sound{true};

	std::vector<std::string> game_paths;
	std::mutex game_cache_entries_mutex;
	std::vector<GameEntry> game_cache_entries;

	std::set<uint64> game_cache_favorites;

	struct _path_hash {
		std::size_t operator()(const fs::path& path) const {
			return fs::hash_value(path);
		}
	};
	std::unordered_map<fs::path, std::unordered_map<std::string, std::string>, _path_hash> graphic_pack_entries;

	ConfigValueBounds<CPUMode> cpu_mode{ CPUMode::Auto };
	ConfigValueBounds<CafeConsoleLanguage> console_language{ CafeConsoleLanguage::EN };

	ConfigValue<GraphicAPI> graphic_api{ kDefaultGraphicsAPI };
	std::array<uint8, 16> legacy_graphic_device_uuid{};
	uint64 mtl_graphic_device_uuid{ 0 };
	ConfigValue<int> vsync{ 0 };
	ConfigValue<bool> gx2drawdone_sync { false };
	ConfigValue<bool> render_upside_down{ false };
	ConfigValue<bool> async_compile{ true };
#ifdef ENABLE_METAL
	ConfigValue<bool> force_mesh_shaders{ false };
#endif

	ConfigValue<bool> overrideAppGammaPreference{ false };
	ConfigValue<float> overrideGammaValue{ 2.2f };
	ConfigValue<float> userDisplayGamma { 2.2f };

	ConfigValue<bool> vk_accurate_barriers{ false };

	struct
	{
		ScreenPosition position = ScreenPosition::kDisabled;
		uint32 text_color = 0xFFFFFFFF;
		sint32 text_scale = 100;
		bool fps = true;
        bool cpu_mode = true;
		bool drawcalls = false;
		bool cpu_usage = false;
		bool cpu_per_core_usage = false;
		bool ram_usage = true;
		bool vram_usage = false;
		bool debug = true;
	} overlay{};

	struct
	{
		ScreenPosition position = ScreenPosition::kTopLeft;
		uint32 text_color = 0xFFFFFFFF;
		sint32 text_scale = 100;
		bool controller_profiles = true;
		bool controller_battery = false;
		bool shader_compiling = true;
		bool friends = true;
	} notification{};

	ConfigValueBounds<PrecompiledShaderOption> precompiled_shaders{ PrecompiledShaderOption::Auto };

	ConfigValue<sint32> upscale_filter{kBicubicFilter};
	ConfigValue<sint32> downscale_filter{kLinearFilter};
	ConfigValue<sint32> fullscreen_scaling{kKeepAspectRatio};
	ConfigValue<float> texture_resolution_scale{1.0f};

#if BOOST_OS_IOS
	sint32 audio_api = 4;
#else
	sint32 audio_api = 0;
#endif
	sint32 audio_delay = 2;
	bool microphone_enabled = false;
	bool tv_audio_enabled = true;
	bool pad_audio_enabled = false;
	AudioChannels tv_channels = kStereo, pad_channels = kStereo, input_channels = kMono;
	sint32 tv_volume = 50, pad_volume = 50, input_volume = 50, portal_volume = 50;
    std::wstring tv_device{ L"default" }, pad_device{ L"default" }, input_device{ L"default" }, portal_device;

	struct
	{
		ConfigValueBounds<uint32> m_persistent_id{0x80000001, 0x80000001, 0xFFFFFFFF};
		ConfigValue<bool> legacy_online_enabled{false};
		ConfigValue<int> legacy_active_service{0};
		std::unordered_map<uint32, NetworkService> service_select;
	}account{};

	ConfigValue<bool> disable_motion{ false };
	struct
	{
		ConfigValue<std::string> host{"127.0.0.1"};
		ConfigValue<uint16> port{ 26760 };
	}dsu_client{};

	ConfigValueBounds<CrashDump> crash_dump{ CrashDump::Disabled };
	ConfigValue<uint16> gdb_port{ 1337 };
	ConfigValue<bool> disable_streamout{ false };
#ifdef ENABLE_METAL
	ConfigValue<std::string> gpu_capture_dir{ "" };
	ConfigValue<bool> framebuffer_fetch{ true };
#endif

	XMLConfigParser Load(XMLConfigParser& parser);
	XMLConfigParser Save(XMLConfigParser& parser);

	bool IsGameListFavorite(uint64 titleId);
	void SetGameListFavorite(uint64 titleId, bool isFavorite);
	bool GetGameListCustomName(uint64 titleId, std::string& customName);
	void SetGameListCustomName(uint64 titleId, std::string customName);

	NetworkService GetAccountNetworkService(uint32 persistentId);
	void SetAccountSelectedService(uint32 persistentId, NetworkService serviceIndex);

	struct
	{
		ConfigValue<bool> emulate_skylander_portal{false};
		ConfigValue<bool> emulate_infinity_base{false};
		ConfigValue<bool> emulate_dimensions_toypad{false};
	}emulated_usb_devices{};

	static int AudioChannelsToNChannels(AudioChannels kStereo)
	{
		switch (kStereo)
		{
		case 0:
			return 1;
		case 2:
			return 6;
		default:
			return 2;
		}
	}

  private:
	GameEntry* GetGameEntryByTitleId(uint64 titleId);
	GameEntry* CreateGameEntry(uint64 titleId);
};

typedef XMLDataConfig<CemuConfig> XMLCemuConfig_t;

inline XMLCemuConfig_t& GetConfigHandle()
{
	static XMLCemuConfig_t config;
	return config;
}

inline CemuConfig& GetConfig()
{
	return GetConfigHandle().data();
}

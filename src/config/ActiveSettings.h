#pragma once
#include <atomic>
#include "config/CemuConfig.h"
#include "config/NetworkSettings.h"

class ActiveSettings
{
private:
	template <typename ...TArgs>
	static fs::path GetPath(const fs::path& path, std::string_view format, TArgs&&... args)
	{
		cemu_assert_debug(format.empty() || (format[0] != '/' && format[0] != '\\'));
		std::string tmpPathStr = fmt::format(fmt::runtime(format), std::forward<TArgs>(args)...);
		return path / _utf8ToPath(tmpPathStr);
	}

	template <typename ...TArgs>
	static fs::path GetPath(const fs::path& path, std::wstring_view format, TArgs&&... args)
	{
		cemu_assert_debug(format.empty() || (format[0] != L'/' && format[0] != L'\\'));
		return path / fmt::format(fmt::runtime(format), std::forward<TArgs>(args)...);
	}
	static fs::path GetPath(const fs::path& path, std::string_view p)
	{
		std::basic_string_view<char8_t> s((const char8_t*)p.data(), p.size());
		return path / fs::path(s);
	}
	static fs::path GetPath(const fs::path& path)
	{
		return path;
	}

public:
	static void
	SetPaths(bool isPortableMode,
		   const fs::path& executablePath,
		   const fs::path& userDataPath,
		   const fs::path& configPath,
		   const fs::path& cachePath,
		   const fs::path& dataPath,
		   std::set<fs::path>& failedWriteAccess);

	static void Init();

	[[nodiscard]] static bool ArePathsSet() { return s_setPathsCalled.load(std::memory_order_acquire); }

	[[nodiscard]] static fs::path GetExecutablePath() { return s_executable_path; }
	[[nodiscard]] static fs::path GetExecutableFilename() { return s_executable_filename; }
	template <typename ...TArgs>
	[[nodiscard]] static fs::path GetUserDataPath(TArgs&&... args){ return GetPath(s_user_data_path, std::forward<TArgs>(args)...); };
	template <typename ...TArgs>
	[[nodiscard]] static fs::path GetConfigPath(TArgs&&... args){ return GetPath(s_config_path, std::forward<TArgs>(args)...); };
	template <typename ...TArgs>
	[[nodiscard]] static fs::path GetCachePath(TArgs&&... args){ return GetPath(s_cache_path, std::forward<TArgs>(args)...); };
	template <typename ...TArgs>
	[[nodiscard]] static fs::path GetDataPath(TArgs&&... args){ return GetPath(s_data_path, std::forward<TArgs>(args)...); };

	[[nodiscard]] static fs::path GetMlcPath();

	template <typename ...TArgs>
	[[nodiscard]] static fs::path GetMlcPath(TArgs&&... args){ return GetPath(GetMlcPath(), std::forward<TArgs>(args)...); };
	static bool IsCustomMlcPath();
	static bool IsCommandLineMlcPath();

	[[nodiscard]] static fs::path GetDefaultMLCPath();

private:
	inline static bool s_isPortableMode{false};
	inline static fs::path s_executable_path;
	inline static fs::path s_user_data_path;
	inline static fs::path s_config_path;
	inline static fs::path s_cache_path;
	inline static fs::path s_data_path;
	inline static fs::path s_executable_filename;
	inline static fs::path s_mlc_path;

public:
	[[nodiscard]] static bool IsPortableMode();

	[[nodiscard]] static bool LoadSharedLibrariesEnabled();
	[[nodiscard]] static bool DisplayDRCEnabled();

	[[nodiscard]] static CPUMode GetCPUMode();
	[[nodiscard]] static uint8 GetTimerShiftFactor();

	static void SetTimerShiftFactor(uint8 shiftFactor);

	[[nodiscard]] static PrecompiledShaderOption GetPrecompiledShadersOption();
	[[nodiscard]] static bool RenderUpsideDownEnabled();
	[[nodiscard]] static bool WaitForGX2DrawDoneEnabled();
	[[nodiscard]] static GraphicAPI GetGraphicsAPI();

	[[nodiscard]] static float GetTVGamma();
	[[nodiscard]] static float GetDRCGamma();

	[[nodiscard]] static bool AudioOutputOnlyAux();
	static void EnableAudioOnlyAux(bool state);

	[[nodiscard]] static uint32 GetPersistentId();
	[[nodiscard]] static bool IsOnlineEnabled();
	[[nodiscard]] static bool HasRequiredOnlineFiles();
	[[nodiscard]] static NetworkService GetNetworkService();
	[[nodiscard]] static bool DumpShadersEnabled();
	[[nodiscard]] static bool DumpTexturesEnabled();
	[[nodiscard]] static bool DumpRecompilerFunctionsEnabled();
	[[nodiscard]] static bool DumpLibcurlRequestsEnabled();
	static void EnableDumpShaders(bool state);
	static void EnableDumpTextures(bool state);
	static void EnableDumpRecompilerFunctions(bool state);
	static void EnableDumpLibcurlRequests(bool state);

	[[nodiscard]] static bool VPADDelayEnabled();
	[[nodiscard]] static bool ShaderPreventInfiniteLoopsEnabled();
	[[nodiscard]] static bool FlushGPUCacheOnSwap();
	[[nodiscard]] static bool ForceSamplerRoundToPrecision();

private:
	inline static std::atomic<bool> s_setPathsCalled{false};
	inline static bool s_dump_shaders = false;
	inline static bool s_dump_textures = false;
	inline static bool s_dump_recompiler_functions = false;
	inline static bool s_dump_libcurl_requests = false;

	inline static uint8 s_timer_shift = 3;

	inline static bool s_audio_aux_only = false;

	inline static bool s_has_required_online_files = false;
};


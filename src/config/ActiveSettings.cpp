#include "Cafe/GameProfile/GameProfile.h"
#include "Cafe/IOSU/legacy/iosu_crypto.h"
#include "Cafe/HW/Latte/Core/Latte.h"
#include "Cafe/CafeSystem.h"
#include "Cemu/Logging/CemuLogging.h"
#include "config/ActiveSettings.h"
#include "config/LaunchSettings.h"
#include "Cafe/Account/Account.h"
#include "config/NetworkSettings.h"
#include "util/helpers/helpers.h"

void ActiveSettings::SetPaths(bool isPortableMode,
		const fs::path& executablePath,
		const fs::path& userDataPath,
		const fs::path& configPath,
		const fs::path& cachePath,
		const fs::path& dataPath,
		std::set<fs::path>& failedWriteAccess)
{
	cemu_assert_debug(!s_setPathsCalled);
	s_isPortableMode = isPortableMode;
	s_executable_path = executablePath;
	s_user_data_path = userDataPath;
	s_config_path = configPath;
	s_cache_path = cachePath;
	s_data_path = dataPath;
	failedWriteAccess.clear();
	for (auto&& path : {userDataPath, configPath, cachePath})
	{
		std::error_code ec;
		if (!fs::exists(path, ec))
			fs::create_directories(path, ec);
		if (!TestWriteAccess(path))
		{
			cemuLog_log(LogType::Force, "Failed to write to {}", _pathToUtf8(path));
			failedWriteAccess.insert(path);
		}
	}
	s_executable_filename = s_executable_path.filename();
	s_setPathsCalled = true;
}

[[nodiscard]] bool ActiveSettings::IsPortableMode()
{
	return s_isPortableMode;
}

void ActiveSettings::Init()
{
	cemu_assert_debug(s_setPathsCalled);
	std::string additionalErrorInfo;
	const sint32 onlineRequirement = iosuCrypt_checkRequirementsForOnlineMode(additionalErrorInfo);
	s_has_required_online_files = onlineRequirement == IOS_CRYPTO_ONLINE_REQ_OK;
}

bool ActiveSettings::LoadSharedLibrariesEnabled()
{
	return g_current_game_profile->ShouldLoadSharedLibraries().value_or(true);
}

bool ActiveSettings::DisplayDRCEnabled()
{
	return g_current_game_profile->StartWithGamepadView();
}

CPUMode ActiveSettings::GetCPUMode()
{
    // Interpreter only. The bridge picks one or three cores; anything else falls back to one core.
    if (g_current_game_profile)
    {
        const auto& profileMode = g_current_game_profile->GetCPUMode();
        if (profileMode && (*profileMode == CPUMode::SinglecoreInterpreter || *profileMode == CPUMode::MulticoreInterpreter))
            return *profileMode;
    }
    const CPUMode configured = GetConfig().cpu_mode.GetValue();
    if (configured == CPUMode::MulticoreInterpreter)
        return configured;
    return CPUMode::SinglecoreInterpreter;
}

uint8 ActiveSettings::GetTimerShiftFactor()
{
	return s_timer_shift;
}

void ActiveSettings::SetTimerShiftFactor(uint8 shiftFactor)
{
	s_timer_shift = shiftFactor;
}

PrecompiledShaderOption ActiveSettings::GetPrecompiledShadersOption()
{
	return g_current_game_profile->GetPrecompiledShadersState().value_or(GetConfig().precompiled_shaders);
}

bool ActiveSettings::RenderUpsideDownEnabled()
{
	return LaunchSettings::RenderUpsideDownEnabled().value_or(GetConfig().render_upside_down);
}

bool ActiveSettings::WaitForGX2DrawDoneEnabled()
{
	return GetConfig().gx2drawdone_sync;
}

GraphicAPI ActiveSettings::GetGraphicsAPI()
{
	const GraphicAPI api = g_current_game_profile->GetGraphicsAPI().value_or(GetConfig().graphic_api);
	std::optional<GraphicAPI> fallbackAPI;
#ifdef ENABLE_METAL
	if (api == kMetal)
		return api;
	fallbackAPI = fallbackAPI.value_or(kMetal);
#endif
#ifdef ENABLE_OPENGL
	if (api == kOpenGL)
		return api;
	fallbackAPI = fallbackAPI.value_or(kOpenGL);
#endif
	cemu_assert(fallbackAPI.has_value());
	return *fallbackAPI;
}

float ActiveSettings::GetTVGamma()
{
	const auto& config = GetConfig();
	return config.overrideGammaValue.GetValue() + LatteGPUState.tvGamma * !config.overrideAppGammaPreference.GetValue();
}

float ActiveSettings::GetDRCGamma()
{
	const auto& config = GetConfig();
	return config.overrideGammaValue.GetValue() + LatteGPUState.drcGamma * !config.overrideAppGammaPreference.GetValue();
}

bool ActiveSettings::AudioOutputOnlyAux()
{
	return s_audio_aux_only;
}

void ActiveSettings::EnableAudioOnlyAux(bool state)
{
	s_audio_aux_only = state;
}

uint32 ActiveSettings::GetPersistentId()
{
	return LaunchSettings::GetPersistentId().value_or(GetConfig().account.m_persistent_id);
}

bool ActiveSettings::IsOnlineEnabled()
{
	// EspressU never connects to any service.
	return false;
}

bool ActiveSettings::HasRequiredOnlineFiles()
{
	return false;
}

NetworkService ActiveSettings::GetNetworkService()
{
	return NetworkService::Offline;
}

bool ActiveSettings::DumpShadersEnabled()
{
	return s_dump_shaders;
}

bool ActiveSettings::DumpTexturesEnabled()
{
	return s_dump_textures;
}

bool ActiveSettings::DumpRecompilerFunctionsEnabled()
{
	return s_dump_recompiler_functions;
}

bool ActiveSettings::DumpLibcurlRequestsEnabled()
{
	return s_dump_libcurl_requests;
}

void ActiveSettings::EnableDumpShaders(bool state)
{
	s_dump_shaders = state;
}

void ActiveSettings::EnableDumpTextures(bool state)
{
	s_dump_textures = state;
}

void ActiveSettings::EnableDumpRecompilerFunctions(bool state)
{
	s_dump_recompiler_functions = state;
}

void ActiveSettings::EnableDumpLibcurlRequests(bool state)
{
	s_dump_libcurl_requests = state;
}

bool ActiveSettings::VPADDelayEnabled()
{
	const uint64 titleId = CafeSystem::GetForegroundTitleId();
	return   titleId == 0x000500001017BF00 ||
		 titleId == 0x000500001017BE00 ||
		 titleId == 0x000500001017B500;
}

bool ActiveSettings::ShaderPreventInfiniteLoopsEnabled()
{
	const uint64 titleId = CafeSystem::GetForegroundTitleId();
	return   titleId == 0x0005000010101C00 ||
		 titleId == 0x0005000010101D00 ||
		 titleId == 0x0005000010101E00 ||
		 titleId == 0x000500001014B700 ||
		 titleId == 0x000500001014B800 ||
		 titleId == 0x0005000010142300 ||
		 titleId == 0x0005000010142400 ||
	    titleId == 0x00050000101D0300 ||
	    titleId == 0x00050000101D0600 ||
	    titleId == 0x000500001014D200 ||
	    titleId == 0x00050000101D3F00;
}

bool ActiveSettings::FlushGPUCacheOnSwap()
{
	const uint64 titleId = CafeSystem::GetForegroundTitleId();
	return
		 titleId == 0x0005000010147900 ||
		 titleId == 0x0005000010146300 ||
		 titleId == 0x000500001017E500 ||
		 titleId == 0x000500001017E600;
}

bool ActiveSettings::ForceSamplerRoundToPrecision()
{
	const uint64 titleId = CafeSystem::GetForegroundTitleId();
	return
		 titleId == 0x000500001014E100 ||
		 titleId == 0x0005000010144000 ||
		 titleId == 0x0005000010129200 ||
		 titleId == 0x0005000010129000;
	return false;
}

fs::path ActiveSettings::GetMlcPath()
{
	cemu_assert_debug(s_setPathsCalled);
	if(const auto launch_mlc = LaunchSettings::GetMLCPath(); launch_mlc.has_value())
		return launch_mlc.value();

	if(const auto config_mlc = GetConfig().mlc_path.GetValue(); !config_mlc.empty())
		return _utf8ToPath(config_mlc);

	return GetDefaultMLCPath();
}

bool ActiveSettings::IsCustomMlcPath()
{
	cemu_assert_debug(s_setPathsCalled);
	return !GetConfig().mlc_path.GetValue().empty();
}

bool ActiveSettings::IsCommandLineMlcPath()
{
	return LaunchSettings::GetMLCPath().has_value();
}

fs::path ActiveSettings::GetDefaultMLCPath()
{
	return GetUserDataPath("mlc01");
}

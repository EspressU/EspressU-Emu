#include "WindowSystem.h"
#include "util/crypto/aes128.h"
#include "Cafe/OS/RPL/rpl.h"
#include "Cafe/OS/libs/gx2/GX2.h"
#include "Cafe/OS/libs/coreinit/coreinit_Thread.h"
#include "Cafe/GameProfile/GameProfile.h"
#include "Cafe/GraphicPack/GraphicPack2.h"
#include "config/CemuConfig.h"
#include "config/NetworkSettings.h"
#include "config/LaunchSettings.h"
#include "input/InputManager.h"
#include "Cafe/Filesystem/fsc.h"
#include "Cafe/Filesystem/FST/FST.h"

#include "Cafe/CafeSystem.h"
#include "Cafe/TitleList/TitleList.h"
#include "Cafe/TitleList/SaveList.h"

#include "Common/ExceptionHandler/ExceptionHandler.h"
#include "Common/cpu_features.h"

#include "Cemu/Logging/CemuLogging.h"
#include "util/helpers/helpers.h"
#include "config/ActiveSettings.h"
#ifdef ENABLE_METAL
#include "Cafe/HW/Latte/Renderer/Metal/MetalRenderer.h"
#endif

#include "Cafe/HW/Espresso/Recompiler/PPCRecompiler.h"

#include "Cafe/IOSU/legacy/iosu_crypto.h"
#include "Cafe/OS/libs/vpad/vpad.h"

#include "audio/IAudioAPI.h"
#include "audio/IAudioInputAPI.h"
#if BOOST_OS_WINDOWS
#pragma comment(lib,"Dbghelp.lib")
#endif

#ifdef HAS_SDL
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#ifdef __APPLE__
#include <dispatch/dispatch.h>
#include <pthread.h>
#endif
#include <SDL3/SDL_main.h>
#endif

#if BOOST_OS_LINUX
#define _putenv(__s) putenv((char*)(__s))
#include <sys/sysinfo.h>
#elif BOOST_OS_MACOS || BOOST_OS_IOS || BOOST_OS_BSD
#define _putenv(__s) putenv((char*)(__s))
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

#include <mutex>
#include <condition_variable>

struct GameInfo {
    unsigned long long titleId;
    const char* name;
    const char* path;
    bool isBaseGame;
};

static std::vector<GameInfo> g_games;
static std::mutex g_gamesMutex;
static std::condition_variable g_gamesCV;
static bool g_scanFinished = false;
static int g_callbackId = -1;

#if BOOST_OS_WINDOWS
extern "C"
{
	__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
	__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
}
#endif

std::atomic_bool g_isGPUInitFinished = false;

std::wstring executablePath;

std::vector<std::string*> sPutEnvMap;

void _putenvSafe(const char* c)
{
    auto s = new std::string(c);
    sPutEnvMap.emplace_back(s);
    _putenv(s->c_str());
}

void reconfigureGLDrivers()
{
#ifdef ENABLE_OPENGL
	const fs::path nvCacheDir = ActiveSettings::GetCachePath("shaderCache/driver/nvidia/");

	std::error_code err;
	fs::create_directories(nvCacheDir, err);

	std::string nvCacheDirEnvOption("__GL_SHADER_DISK_CACHE_PATH=");
	nvCacheDirEnvOption.append(_pathToUtf8(nvCacheDir));

#if BOOST_OS_WINDOWS
	std::wstring tmpW = boost::nowide::widen(nvCacheDirEnvOption);
	_wputenv(tmpW.c_str());
#else
    _putenvSafe(nvCacheDirEnvOption.c_str());
#endif
    _putenvSafe("__GL_SHADER_DISK_CACHE_SKIP_CLEANUP=1");
#endif
}

void WindowsInitCwd()
{
	#if BOOST_OS_WINDOWS
	executablePath.resize(4096);
	int i = GetModuleFileNameW(NULL, executablePath.data(), executablePath.size());
	if(i >= 0)
		executablePath.resize(i);
	else
		executablePath.clear();
	SetCurrentDirectoryW(executablePath.c_str());
	SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
	#endif
}

void CemuCommonInit()
{
	reconfigureGLDrivers();
	AES128_init();
	PPCTimer_init();

	WindowsInitCwd();
    ExceptionHandler_Init();
	GetConfigHandle().Load();
	if (NetworkConfig::XMLExists())
		n_config.Load();
	std::future<int> futureInitAudioAPI = std::async(std::launch::async, []{ IAudioAPI::InitializeStatic(); IAudioInputAPI::InitializeStatic(); return 0; });
	std::future<int> futureInitGraphicPacks = std::async(std::launch::async, []{ GraphicPack2::LoadAll(); return 0; });
	InputManager::instance().load();
	futureInitAudioAPI.wait();
	futureInitGraphicPacks.wait();
	CafeSystem::Initialize();
	CafeTitleList::Initialize(ActiveSettings::GetUserDataPath("title_list_cache.xml"));
	for (auto& it : GetConfig().game_paths)
		CafeTitleList::AddScanPath(_utf8ToPath(it));
	fs::path mlcPath = ActiveSettings::GetMlcPath();
	if (!mlcPath.empty())
		CafeTitleList::SetMLCPath(mlcPath);
	CafeTitleList::Refresh();
	CafeSaveList::Initialize();
	if (!mlcPath.empty())
	{
		CafeSaveList::SetMLCPath(mlcPath);
		CafeSaveList::Refresh();
	}
}

void mainEmulatorLLE();
void ppcAsmTest();
void gx2CopySurfaceTest();
void ExpressionParser_test();
void FSTVolumeTest();
void CRCTest();

void UnitTests()
{
	ExpressionParser_test();
	gx2CopySurfaceTest();
	ppcAsmTest();
	FSTVolumeTest();
	CRCTest();
}

bool isConsoleConnected = false;
void requireConsole()
{
	#if BOOST_OS_WINDOWS
	if (isConsoleConnected)
		return;

	if (AttachConsole(ATTACH_PARENT_PROCESS) != FALSE)
	{
		freopen("CONIN$", "r", stdin);
		freopen("CONOUT$", "w", stdout);
		freopen("CONOUT$", "w", stderr);
		isConsoleConnected = true;
	}
	#endif
}

void HandlePostUpdate()
{
	const auto filename = ActiveSettings::GetExecutablePath().replace_extension("exe.backup");
	if (fs::exists(filename))
	{
#if BOOST_OS_WINDOWS
		HANDLE lock;
		do
		{
			lock = CreateMutexW(nullptr, TRUE, L"Global\\cemu_update_lock");
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		} while (lock == nullptr);
		const DWORD wait_result = WaitForSingleObject(lock, 2000);
		CloseHandle(lock);

		if (wait_result == WAIT_OBJECT_0)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
			std::error_code ec;
			fs::remove(filename, ec);
		}
#else
		while (fs::exists(filename))
		{
			std::error_code ec;
			fs::remove(filename, ec);
			std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		}
#endif
	}
}

void ToolShaderCacheMerger();

#if BOOST_OS_WINDOWS

int wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nShowCmd)
{
	if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE)))
		cemuLog_log(LogType::Force, "CoInitializeEx() failed");
#ifdef HAS_SDL
	SDL_SetMainReady();
#endif
	if (!LaunchSettings::HandleCommandline(lpCmdLine))
		return 0;
	WindowSystem::Create();
	return 0;
}

int main(int argc, char* argv[])
{
	if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE)))
		cemuLog_log(LogType::Force, "CoInitializeEx() failed");
#ifdef HAS_SDL
	SDL_SetMainReady();
#endif
	if (!LaunchSettings::HandleCommandline(argc, argv))
		return 0;
	WindowSystem::Create();
	return 0;
}

#else

#if BOOST_OS_IOS
int main_cemu(int argc, char *argv[])
#else
int main(int argc, char *argv[])
#endif
{
#if BOOST_OS_LINUX || BOOST_OS_BSD
    XInitThreads();
#endif
    if (!LaunchSettings::HandleCommandline(argc, argv))
		return 0;
	WindowSystem::Create();
	return 0;
}
#endif

extern "C" DLLEXPORT uint64 gameMeta_getTitleId()
{
	return CafeSystem::GetForegroundTitleId();
}

static bool gInitialized = false;

std::vector<GameInfo> GetAllGames(bool includeUpdates = false, bool includeDLC = false)
{
    std::vector<GameInfo> result;
    CafeTitleList::WaitForMandatoryScan();
    auto list = CafeTitleList::AcquireInternalList();

    for (TitleInfo* t : list)
    {
        if (!t || !t->IsValid()) continue;
        if (t->IsSystemDataTitle()) continue;

        auto type = t->GetTitleType();
        bool isBase   = type == TitleIdParser::TITLE_TYPE::BASE_TITLE ||
                        type == TitleIdParser::TITLE_TYPE::HOMEBREW;
        bool isUpdate = type == TitleIdParser::TITLE_TYPE::BASE_TITLE_UPDATE;
        bool isDLC    = type == TitleIdParser::TITLE_TYPE::AOC;

        if (!includeUpdates && isUpdate) continue;
        if (!includeDLC && isDLC) continue;

        std::string nameStr = t->GetMetaTitleName();
        std::string pathStr = _pathToUtf8(t->GetPath());

        GameInfo g;
        g.titleId    = t->GetAppTitleId();
        g.name       = strdup(nameStr.c_str());
        g.path       = strdup(pathStr.c_str());
        g.isBaseGame = isBase;

        result.push_back(g);
    }

    CafeTitleList::ReleaseInternalList();
    return result;
}

extern "C"
{

void CemuInitialize(const char* execPath, const char* user_data_path, const char* config_path, const char* cache_path, const char*  data_path)
{
    if (gInitialized)
        return;

    SDL_SetMainReady();
    SDL_SetiOSEventPump(true);

    SDL_SetHint(SDL_HINT_APP_NAME, "EspressU");
    SDL_SetHint(SDL_HINT_JOYSTICK_ENHANCED_REPORTS, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_SWITCH_HOME_LED, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_JOY_CONS, "1");
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_COMBINE_JOY_CONS, "0");

    // SDL's iOS joystick backend reads UIApplication.connectedScenes, which UIKit only allows on the main
    // thread. The game launch calls us from a background task, so hop over to the main queue for this call.
    // Safe: nothing on the main thread waits for this function, so dispatch_sync cannot deadlock.
    bool sdlReady = false;
    auto initSdl = [](void* ready) {
        *static_cast<bool*>(ready) = SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD | SDL_INIT_HAPTIC | SDL_INIT_EVENTS);
    };
#ifdef __APPLE__
    if (pthread_main_np())
        initSdl(&sdlReady);
    else
        dispatch_sync_f(dispatch_get_main_queue(), &sdlReady, initSdl);
#else
    initSdl(&sdlReady);
#endif
    if (!sdlReady)
        throw std::runtime_error(fmt::format("couldn't initialize SDL: {}", SDL_GetError()));

    AES128_init();

    PPCTimer_init();

    IAudioAPI::InitializeStatic();

    std::set<fs::path> failedAccess;
    ActiveSettings::SetPaths(true, execPath, user_data_path, config_path, cache_path, data_path, failedAccess);
    cemuLog_createLogFile(false);

    fsc_init();
    CafeTitleList::SetMLCPath(ActiveSettings::GetMlcPath());
    CafeSaveList::SetMLCPath(ActiveSettings::GetMlcPath());

    CafeTitleList::Initialize(ActiveSettings::GetUserDataPath("title_list_cache.xml"));
    CafeTitleList::Refresh();

    ExceptionHandler_Init();
    GetConfigHandle().SetFilename(ActiveSettings::GetConfigPath("config.xml").generic_wstring());
    GetConfigHandle().Load();
    NetworkConfig::LoadOnce();

    ActiveSettings::Init();

    std::future<int> futureInitAudioAPI = std::async(std::launch::async, []{ IAudioAPI::InitializeStatic(); IAudioInputAPI::InitializeStatic(); return 0; });
    std::future<int> futureInitGraphicPacks = std::async(std::launch::async, []{ GraphicPack2::ClearGraphicPacks(); GraphicPack2::LoadAll(); return 0; });
    InputManager::instance().load();
    futureInitAudioAPI.wait();
    futureInitGraphicPacks.wait();

    CafeSaveList::Initialize();
    CafeSystem::Initialize();
    gInitialized = true;
}

bool CemuIsInitialized()
{
    return gInitialized;
}

bool CemuLoadTitle(uint64_t titleId)
{
    if (!gInitialized)
        return false;

    auto status = CafeSystem::PrepareForegroundTitle(titleId);

    return status == CafeSystem::PREPARE_STATUS_CODE::SUCCESS;
}

unsigned long long CemuLoadFile(const char* launchPath, bool load)
{

    TitleInfo launchTitle{ launchPath };
    CafeTitleList::AddTitleFromPath(launchPath);
    if (load) {
        if (launchTitle.IsValid())
        {
            TitleId baseTitleId;
            if (!CafeTitleList::FindBaseTitleId(launchTitle.GetAppTitleId(), baseTitleId))
            {
                return -1;
            }

            return baseTitleId;
        } else {
            CafeTitleFileType fileType = DetermineCafeSystemFileType(launchPath);

            if (fileType == CafeTitleFileType::UNKNOWN) {
                return -1;
            }

            if (load) {
                if (fileType == CafeTitleFileType::RPX || fileType == CafeTitleFileType::ELF)
                {
                    CafeSystem::PREPARE_STATUS_CODE r = CafeSystem::PrepareForegroundTitleFromStandaloneRPX(launchPath);
                    if (r != CafeSystem::PREPARE_STATUS_CODE::SUCCESS)
                    {
                        return -1;
                    }
                }
            }
        }
    }

    return 0;
}

struct GameIconResult {
    uint8_t* data;
    uint32_t size;
};

void Cemu_FreeGameIcon(GameIconResult* icon) {
    if (icon && icon->data) {
        free(icon->data);
        icon->data = nullptr;
        icon->size = 0;
    }
}

GameIconResult CemuGetGameIcon(uint64 titleId) {
    GameIconResult result{nullptr, 0};

    TitleInfo titleInfo;
    if (!CafeTitleList::GetFirstByTitleId(titleId, titleInfo))
        return result;

    std::string tempMountPath = TitleInfo::GetUniqueTempMountingPath();
    if (!titleInfo.Mount(tempMountPath, "", FSC_PRIORITY_BASE))
        return result;

    auto iconData = fsc_extractFile((tempMountPath + "/meta/iconTex.tga").c_str());

    if (!iconData) {
        iconData = fsc_extractFile((tempMountPath + "/meta/iconTex.tga.gz").c_str());
        if (iconData) {
            auto decompressed = zlibDecompress(*iconData, 70 * 1024);
            std::swap(iconData, decompressed);
        }
    }

    titleInfo.Unmount(tempMountPath);

    if (iconData && iconData->size() > 16) {
        result.size = static_cast<uint32_t>(iconData->size());
        result.data = static_cast<uint8_t*>(malloc(result.size));
        if (result.data) {
            memcpy(result.data, iconData->data(), result.size);
        } else {
            result.size = 0;
        }
    }

    return result;
}

GameInfo* CemuGetAllGames(bool includeUpdates, bool includeDLC, int* outCount)
{
    *outCount = 0;
    std::vector<GameInfo> cppGames = GetAllGames(includeUpdates, includeDLC);

    *outCount = static_cast<int>(cppGames.size());
    GameInfo* result = (GameInfo*)malloc(sizeof(GameInfo) * (*outCount));

    for (int i = 0; i < *outCount; i++) {
        result[i] = cppGames[i];
        cppGames[i].name = nullptr;
        cppGames[i].path = nullptr;
    }

    return result;
}

void CemuFreeGameList(GameInfo* list, int count)
{
    if (!list) return;
    for (int i = 0; i < count; i++) {
        free((void*)list[i].name);
        free((void*)list[i].path);
    }
    free(list);
}

void CemuUIKit_InitializeLayer(bool main);

bool SetInterpreter(bool interpreter) {
    return LaunchSettings::SetInterpreter(interpreter);
}

void CemuUIKit_SetMetal(bool metals);

void CemuPrepareRenderer()
{
    bool usingMetal = false;
#ifdef ENABLE_METAL
    if (ActiveSettings::GetGraphicsAPI() == kMetal)
    {
        g_renderer = std::make_unique<MetalRenderer>();
        usingMetal = true;
    }
#endif

    if (!g_renderer)
        throw std::runtime_error("No renderer could be created");
    CemuUIKit_SetMetal(usingMetal);
    CemuUIKit_InitializeLayer(true);
    CemuUIKit_InitializeLayer(false);
}

void CemuRun()
{
    CemuPrepareRenderer();
    CafeSystem::LaunchForegroundTitle();
}

bool CemuInitJIT() {
    return PPCRecompiler_Init26();
}

void CemuShutdown()
{
    CafeSystem::Shutdown();
}

}

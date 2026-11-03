#include "Cafe/HW/Latte/ISA/RegDefines.h"
#include "Common/DeviceCapabilities.h"
#include "Cafe/OS/libs/gx2/GX2.h"
#include "Cafe/HW/Latte/Core/Latte.h"
#include "Cafe/HW/Latte/Core/LatteDraw.h"
#include "Cafe/HW/Latte/Core/LatteShader.h"
#include "Cafe/HW/Latte/Core/LatteAsyncCommands.h"
#include "Cafe/GameProfile/GameProfile.h"
#include "Cafe/GraphicPack/GraphicPack2.h"
#include "WindowSystem.h"

#include "Cafe/HW/Latte/Core/LatteBufferCache.h"
#include "Cafe/HW/Latte/Core/LatteTiming.h"

#include "Cafe/HW/Latte/Renderer/Renderer.h"
#include "Cafe/HW/Latte/Core/LatteIndices.h"
#include "Cafe/HW/Latte/Core/LatteTexture.h"
#include "util/helpers/helpers.h"

#include <imgui.h>
#include "config/ActiveSettings.h"

#include "Cafe/CafeSystem.h"
#include <typeinfo>
#if BOOST_OS_IOS
#include <pthread/qos.h>
#endif
#ifdef ENABLE_METAL
#include "Cafe/HW/Latte/Renderer/Metal/MetalPipelineCache.h"
#endif

LatteGPUState_t LatteGPUState = {};

std::atomic_bool sLatteThreadRunning = false;
std::atomic_bool sLatteThreadFinishedInit = false;
static std::atomic_bool sLatteThreadStartedForTitle = false;

void LatteThread_Exit();

void Latte_LoadInitialRegisters()
{
	LatteGPUState.contextNew.CB_TARGET_MASK.set_MASK(0xFFFFFFFF);
	LatteGPUState.contextNew.VGT_MULTI_PRIM_IB_RESET_INDX.set_RESTART_INDEX(0xFFFFFFFF);
	LatteGPUState.contextNew.VGT_DMA_NUM_INSTANCES.set_NUM_INSTANCES(1);
	LatteGPUState.contextRegister[Latte::REGADDR::PA_CL_CLIP_CNTL] = 0;
	*(float*)&LatteGPUState.contextRegister[mmDB_DEPTH_CLEAR] = 1.0f;
}

extern bool gx2WriteGatherInited;

LatteTextureView* osScreenTVTex[2] = { nullptr };
LatteTextureView* osScreenDRCTex[2] = { nullptr };

LatteTextureView* LatteHandleOSScreen_getOrCreateScreenTex(MPTR physAddress, uint32 width, uint32 height, uint32 pitch)
{
	LatteTextureView* texView = LatteTextureViewLookupCache::lookup(physAddress, width, height, 1, pitch, 0, 1, 0, 1, Latte::E_GX2SURFFMT::R8_G8_B8_A8_UNORM, Latte::E_DIM::DIM_2D);
	if (texView)
		return texView;
	return LatteTexture_CreateTexture(Latte::E_DIM::DIM_2D, physAddress, 0, Latte::E_GX2SURFFMT::R8_G8_B8_A8_UNORM, width, height, 1, pitch, 1, 0, Latte::E_HWTILEMODE::TM_LINEAR_ALIGNED, false);
}

void LatteHandleOSScreen_prepareTextures()
{
	osScreenTVTex[0] = LatteHandleOSScreen_getOrCreateScreenTex(LatteGPUState.osScreen.screen[0].physPtr, 1280, 720, 1280);
	osScreenTVTex[1] = LatteHandleOSScreen_getOrCreateScreenTex(LatteGPUState.osScreen.screen[0].physPtr + 1280 * 720 * 4, 1280, 720, 1280);
	osScreenDRCTex[0] = LatteHandleOSScreen_getOrCreateScreenTex(LatteGPUState.osScreen.screen[1].physPtr, 854, 480, 0x380);
	osScreenDRCTex[1] = LatteHandleOSScreen_getOrCreateScreenTex(LatteGPUState.osScreen.screen[1].physPtr + 896 * 480 * 4, 854, 480, 0x380);
}

void LatteRenderTarget_copyToBackbuffer(LatteTextureView* textureView, bool isPadView);

bool LatteHandleOSScreen_TV()
{
	if (!LatteGPUState.osScreen.screen[0].isEnabled)
		return false;
	if (LatteGPUState.osScreen.screen[0].flipExecuteCount == LatteGPUState.osScreen.screen[0].flipRequestCount)
		return false;
	LatteHandleOSScreen_prepareTextures();

	sint32 bufferDisplayTV = (LatteGPUState.osScreen.screen[0].flipRequestCount & 1) ^ 1;
	sint32 bufferDisplayDRC = (LatteGPUState.osScreen.screen[1].flipRequestCount & 1) ^ 1;

	const uint32 bufferIndexTV = (bufferDisplayTV);
	const uint32 bufferIndexDRC = bufferDisplayDRC;

	LatteTexture_ReloadData(osScreenTVTex[bufferIndexTV]->baseTexture);

	LatteRenderTarget_copyToBackbuffer(osScreenTVTex[bufferIndexTV]->baseTexture->baseView, false);

	if (LatteGPUState.osScreen.screen[0].flipExecuteCount != LatteGPUState.osScreen.screen[0].flipRequestCount)
		LatteGPUState.osScreen.screen[0].flipExecuteCount.store(LatteGPUState.osScreen.screen[0].flipRequestCount);
	return true;
}

bool LatteHandleOSScreen_DRC()
{
	if (!LatteGPUState.osScreen.screen[1].isEnabled)
		return false;
	if (LatteGPUState.osScreen.screen[1].flipExecuteCount == LatteGPUState.osScreen.screen[1].flipRequestCount)
		return false;
	LatteHandleOSScreen_prepareTextures();

	sint32 bufferDisplayDRC = (LatteGPUState.osScreen.screen[1].flipRequestCount & 1) ^ 1;

	const uint32 bufferIndexDRC = bufferDisplayDRC;

	LatteTexture_ReloadData(osScreenDRCTex[bufferIndexDRC]->baseTexture);

	LatteRenderTarget_copyToBackbuffer(osScreenDRCTex[bufferIndexDRC]->baseTexture->baseView, true);

	if (LatteGPUState.osScreen.screen[1].flipExecuteCount != LatteGPUState.osScreen.screen[1].flipRequestCount)
		LatteGPUState.osScreen.screen[1].flipExecuteCount.store(LatteGPUState.osScreen.screen[1].flipRequestCount);
	return true;
}

void LatteThread_HandleOSScreen()
{
	bool swapTV = LatteHandleOSScreen_TV();
	bool swapDRC = LatteHandleOSScreen_DRC();
	if(swapTV || swapDRC)
		g_renderer->SwapBuffers(swapTV, swapDRC);
}

static int Latte_ThreadEntryImpl()
{
	SetThreadName("LatteThread");
#if BOOST_OS_IOS
	pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif

	if (!g_renderer)
	{
		sLatteThreadFinishedInit = true;
		g_isGPUInitFinished = true;
		return 0;
	}

	sint32 w,h;
	WindowSystem::GetWindowPhysSize(w,h);

	g_renderer->Initialize();
	RendererOutputShader::InitializeStatic();

	LatteTiming_Init();
	LatteTexture_init();
	LatteTC_Init();
	LatteBufferCache_init((size_t)DeviceCaps::GetBudgets().bufferCacheBytes);
	LatteQuery_Init();
	LatteSHRC_Init();
	LatteStreamout_InitCache();

	g_renderer->renderTarget_setViewport(0, 0, w, h, 0.0f, 1.0f);

	LatteGPUState.glVendor = GLVENDOR_UNKNOWN;
	switch(g_renderer->GetVendor())
	{
	case GfxVendor::AMD:
		LatteGPUState.glVendor = GLVENDOR_AMD;
		break;
	case GfxVendor::Intel:
		LatteGPUState.glVendor = GLVENDOR_INTEL;
		break;
	case GfxVendor::Nvidia:
		LatteGPUState.glVendor = GLVENDOR_NVIDIA;
		break;
	case GfxVendor::Apple:
		LatteGPUState.glVendor = GLVENDOR_APPLE;
	default:
		break;
	}

	sLatteThreadFinishedInit = true;

	if (cemuLog_isLoggingEnabled(LogType::OpenGLLogging))
		g_renderer->EnableDebugMode();

	while( true )
	{
		if( CafeSystem::IsTitleRunning() )
			break;

		g_renderer->DrawEmptyFrame(true);
		g_renderer->DrawEmptyFrame(false);
		g_renderer->CancelScreenshotRequest();
		std::this_thread::sleep_for(std::chrono::milliseconds(1000/60));
	}

	g_renderer->DrawEmptyFrame(true);

	GraphicPack2::WaitUntilReady();
	LatteGPUState.allowFramebufferSizeOptimization = true;
	for(auto& pack : GraphicPack2::GetActiveGraphicPacks())
	{
		if(pack->AllowRendertargetSizeOptimization())
			continue;
		for(auto& rule : pack->GetTextureRules())
		{
			if(rule.filter_settings.width >= 0 || rule.filter_settings.height >= 0 || rule.filter_settings.depth >= 0 ||
				rule.overwrite_settings.width >= 0 || rule.overwrite_settings.height >= 0 || rule.overwrite_settings.depth >= 0)
			{
				LatteGPUState.allowFramebufferSizeOptimization = false;
				cemuLog_log(LogType::Force, "Graphic pack \"{}\" prevents rendertarget size optimization. This warning can be ignored and is intended for graphic pack developers", pack->GetName());
				break;
			}
		}
	}
    LatteShaderCache_Load();
	Latte_LoadInitialRegisters();
	g_isGPUInitFinished = true;
	while (LatteGPUState.gx2InitCalled == 0)
	{
		std::this_thread::yield();
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		LatteThread_HandleOSScreen();
		if (Latte_GetStopSignal())
			LatteThread_Exit();
	}
	LatteCP_ProcessRingbuffer();
	cemu_assert_debug(false);
	return 0;
}

#if BOOST_OS_IOS
void IOSBridge_GPUThreadException(const char* type, const char* what);
#endif

int Latte_ThreadEntry()
{
	try
	{
		return Latte_ThreadEntryImpl();
	}
	catch (const std::exception& ex)
	{
		cemuLog_log(LogType::Force, "GPU thread: uncaught exception: {}. Stopping the title.", ex.what());
#if BOOST_OS_IOS
		IOSBridge_GPUThreadException(typeid(ex).name(), ex.what());
#endif
	}
	catch (...)
	{
		cemuLog_log(LogType::Force, "GPU thread: uncaught unknown exception. Stopping the title.");
#if BOOST_OS_IOS
		IOSBridge_GPUThreadException("non-standard exception", "");
#endif
	}
	sLatteThreadFinishedInit = true;
	g_isGPUInitFinished = true;
	while (!Latte_GetStopSignal())
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	try
	{
		LatteThread_Exit();
	}
	catch (...)
	{
		cemuLog_log(LogType::Force, "GPU thread: shutting the renderer down threw as well, leaving it");
	}
	return 0;
}

std::thread sLatteThread;
std::atomic<bool> g_latteFullSpeedRenders{false};
std::mutex sLatteThreadStateMutex;

void Latte_Start()
{
	std::unique_lock _lock(sLatteThreadStateMutex);
	cemu_assert_debug(!sLatteThreadRunning);
	sLatteThreadRunning = true;
	sLatteThreadFinishedInit = false;
	sLatteThreadStartedForTitle = true;
	g_isGPUInitFinished = false;
	sLatteThread = std::thread(Latte_ThreadEntry);
	while (!sLatteThreadFinishedInit)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

void Latte_Stop()
{
	std::unique_lock _lock(sLatteThreadStateMutex);
	if (!sLatteThreadRunning)
		return;
	sLatteThreadRunning = false;
	_lock.unlock();
	sLatteThread.join();
}

bool Latte_GetStopSignal()
{
	return !sLatteThreadRunning;
}

void Latte_ResetHostState()
{
	LatteIndices_forgetAll();
	LatteStreamout_Reset();
	LatteTiming_Reset();
	LatteCP_ResetState();
	LatteBufferCache_ResetHostState();
	osScreenTVTex[0] = osScreenTVTex[1] = nullptr;
	osScreenDRCTex[0] = osScreenDRCTex[1] = nullptr;
}

void Latte_CollectLeftovers(std::vector<std::string>& leftovers)
{
	if (sLatteThreadStartedForTitle.exchange(false) && g_renderer)
		leftovers.emplace_back("Latte: renderer object still exists");
	if (sLatteThreadRunning)
		leftovers.emplace_back("Latte: GPU thread still running");
	auto check = [&](const char* what, size_t count)
	{
		if (count != 0)
			leftovers.emplace_back(fmt::format("Latte: {} {}", count, what));
	};
	check("texture(s) alive", LatteTexture_GetLiveTextureCount());
	check("texture(s) registered in the texture cache", LatteTC_GetRegisteredTextureCount());
	check("texture view lookup entries", LatteTextureViewLookupCache::GetEntryCount());
	check("buffer cache node(s)", LatteBufferCache_GetNodeCount());
	check("shader(s) in the runtime shader cache", LatteSHRC_GetCachedShaderCount());
	check("cached index buffer reservation(s)", LatteIndices_GetCachedEntryCount());
	check("occlusion queries tracked", LatteQuery_GetTrackedCount());
	check("texture readback(s) pending", LatteTextureReadback_GetPendingCount());
	check("async GPU command(s) pending", LatteAsyncCommands_GetPendingCount());
#ifdef ENABLE_METAL
	if (MetalPipelineCache_LoaderAbandoned())
		leftovers.emplace_back("Metal: a pipeline cache loader thread did not stop and still uses the renderer");
	check("async Metal pipeline compile(s) in flight", MetalPipelineCache_GetAsyncCompileCount());
#endif
	if (LatteGPUState.gx2InitCalled != 0 || LatteGPUState.sharedArea != nullptr || LatteGPUState.frameCounter != 0)
		leftovers.emplace_back("Latte: LatteGPUState not cleared");
	if (LatteTiming_IsUsingHostDrivenVSync())
		leftovers.emplace_back("Latte: host-driven vsync still enabled");
}

void LatteThread_Exit()
{
	LatteQuery_Reset();
	LatteTextureReadback_Reset();
	LatteAsyncCommands_Reset();
	if (g_renderer)
		g_renderer->Shutdown();
    LatteBufferCache_UnloadAll();
	LatteTC_UnloadAllTextures();
    LatteSHRC_UnloadAll();
    LatteShaderCache_Close();
	GraphicPack2::ReleaseRendererObjects();
	RendererOutputShader::ShutdownStatic();
	if (g_renderer)
	{
		Renderer* renderer = g_renderer.get();
		delete renderer;
		g_renderer.release();
	}
	Latte_ResetHostState();
	std::memset(&LatteGPUState, 0, sizeof(LatteGPUState));
	#if BOOST_OS_WINDOWS
	ExitThread(0);
	#else
	pthread_exit(nullptr);
	#endif
	cemu_assert_unimplemented();
}

#pragma once
#include <atomic>
#include "Cafe/HW/Latte/Core/LatteConst.h"
#include "Cafe/HW/Latte/ISA/LatteReg.h"
#include "util/VirtualHeap/VirtualHeap.h"

struct LatteTextureDefinition;
class LatteTexture;
class LatteTextureView;

struct gx2GPUSharedArea_t
{
	volatile uint32 flipRequestCountBE;
	volatile uint32 flipExecuteCountBE;
	volatile uint32 swapInterval;
};

struct LatteGPUState_t
{
	union
	{
		uint32 contextRegister[LATTE_MAX_REGISTER];
		LatteContextRegister contextNew;
	};
	MPTR contextRegisterShadowAddr[LATTE_MAX_REGISTER];
	uint32 contextControl0;
	uint32 contextControl1;
	bool allowFramebufferSizeOptimization{false};
	uint32 frameCounter;
	uint32 flipCounter;
	uint32 currentDrawCallTick;
	uint32 drawCallCounter;
	uint32 textureBindCounter;
	std::atomic<uint64> flipRequestCount;
	uint64 timer_frequency;
	uint64 timer_bootUp;
	uint64 timer_nextVSync;
	gx2GPUSharedArea_t* sharedArea;
	MPTR sharedAreaAddr;
	uint32 gx2InitCalled;
	uint32 glVendor;
	bool isDRCPrimary = false;
	bool tvBufferUsesSRGB;
	bool drcBufferUsesSRGB;
	float tvGamma = 0.0f;
	float drcGamma = 0.0f;
	bool activeShaderHasError;
	bool repeatTextureInitialization;
	bool requiresTextureBarrier;
	struct
	{
		struct
		{
			bool isEnabled;
			MPTR physPtr;
			std::atomic<uint32> flipRequestCount;
			std::atomic<uint32> flipExecuteCount;
		}screen[2];
	}osScreen;
};

extern LatteGPUState_t LatteGPUState;

#include "Cafe/HW/Latte/Core/LatteTexture.h"

void LatteTextureLoader_estimateAccessedDataRange(LatteTexture* texture, sint32 sliceIndex, sint32 mipIndex, uint32& addrStart, uint32& addrEnd);

#define RENDER_TARGET_TV (1 << 0)
#define RENDER_TARGET_DRC (1 << 2)

void LatteRenderTarget_updateScissorBox();

void LatteRenderTarget_trackUpdates();

void LatteRenderTarget_getScreenImageArea(sint32* x, sint32* y, sint32* width, sint32* height, sint32* fullWidth, sint32* fullHeight, bool padView = false);
void LatteRenderTarget_copyToBackbuffer(LatteTextureView* textureView, bool isPadView);

void LatteRenderTarget_GetCurrentVirtualViewportSize(sint32* viewportWidth, sint32* viewportHeight);

void LatteRenderTarget_itHLESwapScanBuffer();
void LatteRenderTarget_itHLEClearColorDepthStencil(uint32 clearMask, MPTR colorBufferMPTR, Latte::E_GX2SURFFMT colorBufferFormat, Latte::E_HWTILEMODE colorBufferTilemode, uint32 colorBufferWidth, uint32 colorBufferHeight, uint32 colorBufferPitch, uint32 colorBufferViewFirstSlice, uint32 colorBufferViewNumSlice, MPTR depthBufferMPTR, Latte::E_GX2SURFFMT depthBufferFormat, Latte::E_HWTILEMODE depthBufferTileMode, sint32 depthBufferWidth, sint32 depthBufferHeight, sint32 depthBufferPitch, sint32 depthBufferViewFirstSlice, sint32 depthBufferViewNumSlice, float r, float g, float b, float a, float clearDepth, uint32 clearStencil);
void LatteRenderTarget_itHLECopyColorBufferToScanBuffer(MPTR colorBufferPtr, uint32 colorBufferWidth, uint32 colorBufferHeight, uint32 colorBufferSliceIndex, uint32 colorBufferFormat, uint32 colorBufferPitch, Latte::E_HWTILEMODE colorBufferTilemode, uint32 colorBufferSwizzle, uint32 renderTarget);

void LatteRenderTarget_unloadAll();

void LatteTC_Init();

void LatteTC_RegisterTexture(LatteTexture* tex);
void LatteTC_UnregisterTexture(LatteTexture* tex);

uint32 LatteTexture_CalculateTextureDataHash(LatteTexture* hostTexture);
void LatteTexture_ReloadData(LatteTexture* hostTexture);

bool LatteTC_HasTextureChanged(LatteTexture* hostTexture, bool force = false);
void LatteTC_ResetTextureChangeTracker(LatteTexture* hostTexture, bool force = false);
void LatteTC_FlagSliceAsGPUUpdated(LatteTexture* hostTexture, uint32 sliceIndex, uint32 mipIndex);

void LatteTC_MarkTextureStillInUse(LatteTexture* texture);
void LatteTC_CleanupUnusedTextures();

std::vector<LatteTexture*> LatteTC_GetDeleteableTextures();

void LatteTC_UnloadAllTextures();
void LatteTexture_ResetGlobalState();
size_t LatteTexture_GetLiveTextureCount();
size_t LatteTC_GetRegisteredTextureCount();

void LatteTextureReadback_Initate(LatteTextureView* textureView);
void LatteTextureReadback_StartTransfer(LatteTextureView* textureView);
bool LatteTextureReadback_Update(bool forceStart = false);
void LatteTextureReadback_NotifyTextureDeletion(LatteTexture* texture);
void LatteTextureReadback_UpdateFinishedTransfers(bool forceFinish);
void LatteTextureReadback_Reset();
size_t LatteTextureReadback_GetPendingCount();
bool LatteTextureReadback_ReadbackToLinearBlocking(LatteTextureView* sourceView, uint8* dstPtr, uint32 dstWidth, uint32 dstHeight, uint32 dstPitch);

void LatteQuery_Init();
void LatteQuery_BeginOcclusionQuery(MPTR queryMPTR);
void LatteQuery_EndOcclusionQuery(MPTR queryMPTR);
void LatteQuery_UpdateFinishedQueries();
void LatteQuery_UpdateFinishedQueriesForceFinishAll();
void LatteQuery_CancelActiveGPU7Queries();
void LatteQuery_Reset();
size_t LatteQuery_GetTrackedCount();

void LatteStreamout_InitCache();
void LatteStreamout_Reset();
sint32 LatteStreamout_GetRingBufferSize();
void LatteStreamout_PrepareDrawcall(uint32 count, uint32 instanceCount);
void LatteStreamout_FinishDrawcall(bool useDirectMemoryMode);

void LatteTiming_Init();
void LatteTiming_HandleTimedVsync();
void LatteTiming_Reset();

void LatteCP_ProcessRingbuffer();
void LatteCP_ResetState();

bool LatteBufferCache_Sync(uint32 minIndex, uint32 maxIndex, uint32 baseInstance, uint32 instanceCount);
void LatteBufferCache_LoadRemappedUniforms(struct LatteDecompilerShader* shader, float* uniformData);

void LatteRenderTarget_updateViewport();

#define LATTE_GLSL_DYNAMIC_UNIFORM_BLOCK_SIZE	(4096)

#define catchOpenGLError()

void Latte_Start();
void Latte_Stop();
bool Latte_GetStopSignal();
void LatteThread_Exit();
void Latte_ResetHostState();
void Latte_CollectLeftovers(std::vector<std::string>& leftovers);

extern std::atomic<bool> g_latteFullSpeedRenders;

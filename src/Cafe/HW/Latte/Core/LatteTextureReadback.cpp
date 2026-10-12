#include "Cafe/HW/Latte/Core/Latte.h"
#include "Cafe/HW/Latte/Core/LattePerformanceMonitor.h"
#include "Cafe/HW/Latte/Renderer/Renderer.h"
#include "Cafe/HW/Latte/Core/LatteTexture.h"
#include "Cafe/HW/Latte/Core/LatteWaitInfo.h"

#define LOG_READBACK_TIME

struct LatteTextureReadbackQueueEntry
{
	HRTick initiateTime;
	uint32 lastUpdateDrawcallIndex;
	LatteTextureView* textureView;
};

std::vector<LatteTextureReadbackQueueEntry> sTextureScheduledReadbacks;
std::queue<LatteTextureReadbackInfo*> sTextureActiveReadbackQueue;

void LatteTextureReadback_StartTransfer(LatteTextureView* textureView)
{
	cemuLog_log(LogType::TextureReadback, "[TextureReadback-Start] PhysAddr {:08x} Res {}x{} Fmt {} Slice {} Mip {}", textureView->baseTexture->physAddress, textureView->baseTexture->width, textureView->baseTexture->height, textureView->baseTexture->format, textureView->firstSlice, textureView->firstMip);
	HRTick currentTick = HighResolutionTimer().now().getTick();
	LatteTextureReadbackInfo* readbackInfo = g_renderer->texture_createReadback(textureView);
	if (!readbackInfo)
		return;
	sTextureActiveReadbackQueue.push(readbackInfo);
	readbackInfo->StartTransfer();
	readbackInfo->transferStartTime = currentTick;
}

bool LatteTextureReadback_Update(bool forceStart)
{
	bool hasStartedTransfer = false;
	for (size_t i = 0; i < sTextureScheduledReadbacks.size(); i++)
	{
		LatteTextureReadbackQueueEntry& entry = sTextureScheduledReadbacks[i];
		uint32 numElapsedDrawcalls = LatteGPUState.drawCallCounter - entry.lastUpdateDrawcallIndex;
		if (forceStart || numElapsedDrawcalls >= 5)
		{
#ifdef LOG_READBACK_TIME
			double elapsedSecondsSinceInitiate = HighResolutionTimer::getTimeDiff(entry.initiateTime, HighResolutionTimer().now().getTick());
			cemuLog_log(LogType::TextureReadback, "[TextureReadback-Update] Starting transfer for {:08x} after {} elapsed drawcalls. Time since initiate: {:.4} Force-start: {}", entry.textureView->baseTexture->physAddress, numElapsedDrawcalls, elapsedSecondsSinceInitiate, forceStart?"yes":"no");
#endif
			LatteTextureReadback_StartTransfer(entry.textureView);
			vectorRemoveByIndex(sTextureScheduledReadbacks, i);
			i--;
			hasStartedTransfer = true;
		}
	}
	return hasStartedTransfer;
}

void LatteTextureReadback_NotifyTextureDeletion(LatteTexture* texture)
{
	for (size_t i = 0; i < sTextureScheduledReadbacks.size(); i++)
	{
		LatteTextureReadbackQueueEntry& entry = sTextureScheduledReadbacks[i];
		if (entry.textureView->baseTexture == texture)
		{
			vectorRemoveByIndex(sTextureScheduledReadbacks, i);
			break;
		}
	}
}

void LatteTextureReadback_Initate(LatteTextureView* textureView)
{
	if (textureView->baseTexture->overwriteInfo.hasResolutionOverwrite)
	{
		cemuLog_log(LogType::Force, "Texture readback is not supported for textures with modified resolution. Texture: {:08x} {}x{}", textureView->baseTexture->physAddress, textureView->baseTexture->width, textureView->baseTexture->height);
		return;
	}
	for (size_t i = 0; i < sTextureScheduledReadbacks.size(); i++)
	{
		LatteTextureReadbackQueueEntry& entry = sTextureScheduledReadbacks[i];
		if (entry.textureView == textureView)
		{
			entry.lastUpdateDrawcallIndex = LatteGPUState.drawCallCounter;
			return;
		}
	}
	LatteTextureReadbackQueueEntry queueEntry;
	queueEntry.initiateTime = HighResolutionTimer().now().getTick();
	queueEntry.textureView = textureView;
	queueEntry.lastUpdateDrawcallIndex = LatteGPUState.drawCallCounter;
	sTextureScheduledReadbacks.emplace_back(queueEntry);
}

void LatteTextureReadback_UpdateFinishedTransfers(bool forceFinish)
{
	if (forceFinish)
	{
		LatteTextureReadback_Update(true);
	}
	performanceMonitor.gpuTime_waitForAsync.beginMeasuring();
	LatteWait::Get().readbacksPending.store((uint32)sTextureActiveReadbackQueue.size(), std::memory_order_relaxed);
	while (!sTextureActiveReadbackQueue.empty())
	{
		LatteTextureReadbackInfo* readbackInfo = sTextureActiveReadbackQueue.front();
		if (forceFinish)
		{
			if (!readbackInfo->IsFinished())
			{
				readbackInfo->waitStartTime = HighResolutionTimer().now().getTick();
#ifdef LOG_READBACK_TIME
				if (cemuLog_isLoggingEnabled(LogType::TextureReadback))
				{
					double elapsedSecondsTransfer = HighResolutionTimer::getTimeDiff(readbackInfo->transferStartTime, HighResolutionTimer().now().getTick());
					cemuLog_log(LogType::TextureReadback, "[Texture-Readback] Force-finish: {:08x} Res {:}/{:} TM {:} FMT {:04x} Transfer time so far: {:.4}ms", readbackInfo->hostTextureCopy.physAddress, readbackInfo->hostTextureCopy.width, readbackInfo->hostTextureCopy.height, readbackInfo->hostTextureCopy.tileMode, (uint32)readbackInfo->hostTextureCopy.format, elapsedSecondsTransfer * 1000.0);
				}
#endif
				readbackInfo->forceFinish = true;
				LatteWait::Scope waitScope("waiting for a texture readback");
				readbackInfo->ForceFinish();
				if (readbackInfo->IsFinished() || sTextureActiveReadbackQueue.empty() || sTextureActiveReadbackQueue.front() != readbackInfo)
				{
					continue;
				}
				cemuLog_log(LogType::Force, "Latte: texture readback did not finish in time, using stale data");
			}
		}
		else
		{
			if (!readbackInfo->IsFinished())
				break;
			readbackInfo->waitStartTime = HighResolutionTimer().now().getTick();
		}
#ifdef LOG_READBACK_TIME
		if (cemuLog_isLoggingEnabled(LogType::TextureReadback))
		{
			HRTick currentTick = HighResolutionTimer().now().getTick();
			double elapsedSecondsTransfer = HighResolutionTimer::getTimeDiff(readbackInfo->transferStartTime, currentTick);
			double elapsedSecondsWaiting = HighResolutionTimer::getTimeDiff(readbackInfo->waitStartTime, currentTick);
			cemuLog_log(LogType::TextureReadback, "[Texture-Readback] {:08x} Res {}/{} TM {} FMT {:04x} ReadbackLatency: {:6.3}ms WaitTime: {:6.3}ms ForcedWait {}", readbackInfo->hostTextureCopy.physAddress, readbackInfo->hostTextureCopy.width, readbackInfo->hostTextureCopy.height, readbackInfo->hostTextureCopy.tileMode, (uint32)readbackInfo->hostTextureCopy.format, elapsedSecondsTransfer * 1000.0, elapsedSecondsWaiting * 1000.0, readbackInfo->forceFinish ? "yes" : "no");
		}
#endif
		uint8* pixelData = readbackInfo->GetData();
		LatteTextureLoader_writeReadbackTextureToMemory(&readbackInfo->hostTextureCopy, readbackInfo->m_firstSlice, readbackInfo->m_firstMip, pixelData, readbackInfo->m_rowPitch);
		readbackInfo->ReleaseData();
		LatteTextureView* origTexView = LatteTextureViewLookupCache::lookupSlice(readbackInfo->hostTextureCopy.physAddress, readbackInfo->hostTextureCopy.width, readbackInfo->hostTextureCopy.height, readbackInfo->hostTextureCopy.pitch, 0, 0, readbackInfo->hostTextureCopy.format);
		if (origTexView)
			LatteTC_ResetTextureChangeTracker(origTexView->baseTexture, true);
		delete readbackInfo;
		cemu_assert_debug(!sTextureActiveReadbackQueue.empty());
		cemu_assert_debug(readbackInfo == sTextureActiveReadbackQueue.front());
		sTextureActiveReadbackQueue.pop();
	}
	LatteWait::Get().readbacksPending.store((uint32)sTextureActiveReadbackQueue.size(), std::memory_order_relaxed);
	performanceMonitor.gpuTime_waitForAsync.endMeasuring();
}

void LatteTextureReadback_Reset()
{
	sTextureScheduledReadbacks.clear();
	while (!sTextureActiveReadbackQueue.empty())
	{
		delete sTextureActiveReadbackQueue.front();
		sTextureActiveReadbackQueue.pop();
	}
	LatteWait::Get().readbacksPending.store(0, std::memory_order_relaxed);
}

size_t LatteTextureReadback_GetPendingCount()
{
	return sTextureScheduledReadbacks.size() + sTextureActiveReadbackQueue.size();
}

bool LatteTextureReadback_ReadbackToLinearBlocking(LatteTextureView* sourceView, uint8* dstPtr, uint32 dstWidth, uint32 dstHeight, uint32 dstPitch)
{
	LatteTextureReadbackInfo* info = g_renderer->texture_createReadback(sourceView);
	if (!info)
		return false;

	info->StartTransfer();
	info->ForceFinish();
	if (!info->IsFinished())
		cemuLog_log(LogType::Force, "Latte: blocking texture readback did not finish in time, using stale data");

	uint8* data = info->GetData();
	uint32 bpp = Latte::GetFormatBits(sourceView->baseTexture->format) / 8;
	uint32 srcRowBytes = info->m_rowPitch;
	uint32 dstRowBytes = dstWidth * bpp;
	for (uint32 y = 0; y < dstHeight; y++)
		memcpy(dstPtr + y * dstPitch * bpp, data + y * srcRowBytes, dstRowBytes);

	info->ReleaseData();
	delete info;
	return true;
}

#include "Cafe/HW/Latte/Core/Latte.h"
#include "Cafe/HW/Latte/Core/LatteDraw.h"
#include "Cafe/HW/Latte/Core/LatteTexture.h"
#include "Cafe/HW/Latte/Renderer/Renderer.h"
#include "Common/cpu_features.h"
#include "Cafe/HW/Latte/Core/PerfTelemetry.h"

std::unordered_set<LatteTexture*> g_allTextures;

void LatteTC_Init()
{
	cemu_assert_debug(g_allTextures.empty());
}

void LatteTC_RegisterTexture(LatteTexture* tex)
{
	g_allTextures.emplace(tex);
}

void LatteTC_UnregisterTexture(LatteTexture* tex)
{
	g_allTextures.erase(tex);
}

uint32 _quickStochasticHash(void* texData, uint32 memRange)
{
	uint64* texDataU64 = (uint64*)texData;

	uint64 hashVal = 0;
	memRange /= sizeof(uint64);

	uint32 memStep = memRange / 37;
	for (sint32 i = 0; i < 37; i++)
	{
		hashVal += *texDataU64;
		hashVal = (hashVal << 3) | (hashVal >> 61);
		texDataU64 += memStep;
	}
	return (uint32)hashVal ^ (uint32)(hashVal >> 32);
}

uint32 LatteTexture_CalculateTextureDataHash(LatteTexture* hostTexture)
{
	if( hostTexture->texDataPtrHigh == hostTexture->texDataPtrLow )
	{
		return 0;
	}
	if (hostTexture->format == Latte::E_GX2SURFFMT::R11_G11_B10_FLOAT)
	{

		if (hostTexture->tileMode == Latte::E_HWTILEMODE::TM_1D_TILED_THICK && hostTexture->depth == 8 && hostTexture->width == 8 && hostTexture->height == 8)
		{
			uint32* texDataU32 = (uint32*)memory_getPointerFromPhysicalOffset(hostTexture->texDataPtrLow);
			return texDataU32[0] ^ texDataU32[0x100/4] ^ texDataU32[0x200/4] ^ texDataU32[0x300/4];
		}
		uint32* texDataU32 = (uint32*)memory_getPointerFromPhysicalOffset(hostTexture->texDataPtrLow);
		return texDataU32[0] ^ texDataU32[1] ^ texDataU32[2] ^ texDataU32[3];
	}

	uint32 memRange = hostTexture->texDataPtrHigh - hostTexture->texDataPtrLow;
	uint32* texDataU32 = (uint32*)memory_getPointerFromPhysicalOffset(hostTexture->texDataPtrLow);
	uint32 hashVal = 0;
	uint32 pixelCount = hostTexture->width*hostTexture->height;

	bool isCompressedFormat = hostTexture->IsCompressedFormat();
	if (isCompressedFormat || hostTexture->useLightHash)
	{
		if (memRange < 256)
		{
			memRange /= sizeof(uint32);
			while (memRange--)
			{
				hashVal += *texDataU32;
				hashVal = (hashVal << 3) | (hashVal >> 29);
				texDataU32++;
			}
		}
		else
		{
			hashVal = _quickStochasticHash(texDataU32, memRange);
		}
		return hashVal;
	}

	if( pixelCount <= (700*700) )
	{
		bool isCompressedFormat = hostTexture->IsCompressedFormat();
		if( isCompressedFormat == false || memRange < 0x200 )
		{
			memRange /= (4*sizeof(uint32));
			while( memRange-- )
			{
				hashVal += *texDataU32;
				hashVal = (hashVal<<3)|(hashVal>>29);
				texDataU32 += 4;
			}
		}
		else
		{
			memRange /= (32*sizeof(uint32));
			while( memRange-- )
			{
				hashVal += *texDataU32;
				hashVal = (hashVal<<3)|(hashVal>>29);
				texDataU32 += 32;
			}
		}
	}
	else if( pixelCount <= (1200*1200) )
	{
		bool isCompressedFormat = hostTexture->IsCompressedFormat();
		if( isCompressedFormat == false )
		{
			memRange /= (12*sizeof(uint32));
			while( memRange-- )
			{
				hashVal += *texDataU32;
				hashVal = (hashVal<<3)|(hashVal>>29);
				texDataU32 += 12;
			}
		}
		else
		{
			memRange /= (96*sizeof(uint32));
			while( memRange-- )
			{
				hashVal += *texDataU32;
				hashVal = (hashVal<<3)|(hashVal>>29);
				texDataU32 += 96;
			}
		}
	}
	else
	{
		bool isCompressedFormat = hostTexture->IsCompressedFormat();
		if( isCompressedFormat == false )
		{
#if BOOST_OS_WINDOWS
			if (g_CPUFeatures.x86.avx2)
			{
				__m256i h256 = { 0 };
				__m256i* readPtr = (__m256i*)texDataU32;
				memRange /= (288);
				while (memRange--)
				{
					__m256i temp = _mm256_load_si256(readPtr);
					readPtr += (288 / 32);
					h256 = _mm256_xor_si256(h256, temp);
				}
#ifdef __clang__
				hashVal = h256[0] + h256[1] + h256[2] + h256[3] + h256[4] + h256[5] + h256[6] + h256[7];
#else
				hashVal = h256.m256i_u32[0] + h256.m256i_u32[1] + h256.m256i_u32[2] + h256.m256i_u32[3] + h256.m256i_u32[4] + h256.m256i_u32[5] + h256.m256i_u32[6] + h256.m256i_u32[7];
#endif
			}
#else
			if( false ) {}
#endif
			else
			{
				memRange /= (32 * sizeof(uint64));
				uint64 h64 = 0;
				uint64* texDataU64 = (uint64*)texDataU32;
				while (memRange--)
				{
					h64 += *texDataU64;
					h64 = (h64 << 3) | (h64 >> 61);
					texDataU64 += 32;
				}
				hashVal = (h64 & 0xFFFFFFFF) + (h64 >> 32);
			}
		}
		else
		{
			memRange /= (512*sizeof(uint32));
			while( memRange-- )
			{
				hashVal += *texDataU32;
				hashVal = (hashVal<<3)|(hashVal>>29);
				texDataU32 += 512;
			}
		}
	}

	return hashVal;
}

uint64 _botwLargeTexHax = 0;

bool LatteTC_HasTextureChanged(LatteTexture* hostTexture, bool force)
{
	if (hostTexture->forceInvalidate)
	{
		force = true;
		debug_printf("Force invalidate 0x%08x\n", hostTexture->physAddress);
		hostTexture->forceInvalidate = false;
	}
	if( hostTexture->lastDataUpdateFrameCounter == LatteGPUState.frameCounter && force == false)
		return false;
	hostTexture->lastDataUpdateFrameCounter = LatteGPUState.frameCounter;
	if (hostTexture->width == 1280 && hostTexture->format != Latte::E_GX2SURFFMT::R8_UNORM && force == false)
	{
		return false;
	}
	const uint64 hashStart = PerfTelemetry::NowNs();
	uint32 texDataHash = LatteTexture_CalculateTextureDataHash(hostTexture);
	{
		auto& perf = PerfTelemetry::Get();
		perf.gpuTexHashNs.fetch_add(PerfTelemetry::NowNs() - hashStart, std::memory_order_relaxed);
		perf.gpuTexHashes.fetch_add(1, std::memory_order_relaxed);
		perf.gpuTexHashBytes.fetch_add(hostTexture->texDataPtrHigh - hostTexture->texDataPtrLow, std::memory_order_relaxed);
	}
	if( texDataHash != hostTexture->texDataHash2 )
	{
		hostTexture->texDataHash2 = texDataHash;
		if (hostTexture->depth == 83 && hostTexture->width == 1024 && hostTexture->height == 1024)
		{
			_botwLargeTexHax = LatteGPUState.frameCounter;
		}
		return true;
	}
	if (_botwLargeTexHax != 0 && hostTexture->depth == 83 && hostTexture->width == 1024 && hostTexture->height == 1024 && _botwLargeTexHax != LatteGPUState.frameCounter)
	{
		_botwLargeTexHax = 0;
		return true;
	}
	return false;
}

void LatteTC_SwitchToLightHash(LatteTexture* hostTexture)
{
	if (hostTexture->useLightHash)
		return;
	hostTexture->useLightHash = true;
	hostTexture->texDataHash2 = LatteTexture_CalculateTextureDataHash(hostTexture);
}

void LatteTC_FlagSliceAsGPUUpdated(LatteTexture* hostTexture, uint32 sliceIndex, uint32 mipIndex)
{
	hostTexture->isUpdatedOnGPU = true;
	LatteTC_SwitchToLightHash(hostTexture);
}

void LatteTC_ResetTextureChangeTracker(LatteTexture* hostTexture, bool force)
{
	if( hostTexture->lastDataUpdateFrameCounter == LatteGPUState.frameCounter && force == false)
		return;
	hostTexture->lastDataUpdateFrameCounter = LatteGPUState.frameCounter;
	LatteTC_HasTextureChanged(hostTexture, true);
}

void LatteTC_MarkTextureStillInUse(LatteTexture* texture)
{
	texture->lastAccessTick = LatteGPUState.currentDrawCallTick;
	texture->lastAccessFrameCount = LatteGPUState.frameCounter;
}

bool LatteTC_IsTextureDataOverwritten(LatteTexture* texture)
{
	sint32 mipLevels = texture->mipLevels;
	sint32 sliceCount = texture->depth;
	mipLevels = std::min(mipLevels, 3);
	for (sint32 mipIndex = 0; mipIndex < mipLevels; mipIndex++)
	{
		sint32 mipSliceCount;
		if (texture->Is3DTexture())
			mipSliceCount = std::max(1, sliceCount >> mipIndex);
		else
			mipSliceCount = sliceCount;
		for (sint32 sliceIndex = 0; sliceIndex < mipSliceCount; sliceIndex++)
		{
			LatteTextureSliceMipInfo* sliceMipInfo = texture->sliceMipInfo + texture->GetSliceMipArrayIndex(sliceIndex, mipIndex);
			bool isSliceMipOutdated = false;
			for (auto& overlapData : sliceMipInfo->list_dataOverlap)
			{
				if (sliceMipInfo->lastDynamicUpdate < overlapData.destMipSliceInfo->lastDynamicUpdate)
				{
					isSliceMipOutdated = true;
					break;
				}
			}
			if (isSliceMipOutdated == false)
				return false;
		}
	}
	return true;
}

void LatteTexture_Delete(LatteTexture* texture)
{
	LatteTC_UnregisterTexture(texture);
	LatteMRT::NotifyTextureDeletion(texture);
	LatteTextureReadback_NotifyTextureDeletion(texture);
	LatteTexture_DeleteTextureRelations(texture);
	while (!texture->views.empty())
		delete texture->views[0];
	cemu_assert_debug(texture->views.empty());
	cemu_assert_debug(texture->baseView == nullptr);
	LatteTexture_DeleteDataOverlapTracking(texture);
	LatteTexture_UnregisterTextureMemoryOccupancy(texture);
	if (texture->sliceMipInfo)
	{
		delete[] texture->sliceMipInfo;
		texture->sliceMipInfo = nullptr;
	}
	delete texture;
}

bool LatteTC_CleanupCheckTexture(LatteTexture* texture, uint32 currentTick)
{
	uint32 currentFrameCount = LatteGPUState.frameCounter;
	uint32 ticksSinceLastAccess = currentTick - texture->lastAccessTick;
	uint32 framesSinceLastAccess = currentFrameCount - texture->lastAccessFrameCount;
	if( !texture->isUpdatedOnGPU )
	{
		if( ticksSinceLastAccess >= (120*1000) && framesSinceLastAccess >= 2000 )
		{
			LatteTexture_Delete(texture);
			return true;
		}
	}

	if ((LatteGPUState.currentDrawCallTick - texture->lastAccessTick) >= 100 &&
		LatteTC_IsTextureDataOverwritten(texture))
	{
		LatteTexture_Delete(texture);
		return true;
	}
	if (ticksSinceLastAccess >= 5 * 1000 && framesSinceLastAccess >= 30)
	{
		for (sint32 i = 0; i < 3; i++)
		{
			if (texture->views.size() <= 1)
				break;
			LatteTextureView* view = texture->views[0];
			if (view == texture->baseView)
				view = texture->views[1];
			delete view;
		}
	}
	return false;
}

void LatteTexture_RefreshInfoCache();

void LatteTC_CleanupUnusedTextures()
{
	static size_t currentScanIndex = 0;
	uint32 currentTick = GetTickCount();
	sint32 maxDelete = 10;
	std::vector<LatteTexture*>& allTextures = LatteTexture::GetAllTextures();
	if (!allTextures.empty())
	{
		for (sint32 c = 0; c < 25; c++)
		{
			if (currentScanIndex >= allTextures.size())
				currentScanIndex = 0;
			LatteTexture* texItr = allTextures[currentScanIndex];
			currentScanIndex++;
			if (!texItr)
				continue;
			if (LatteTC_CleanupCheckTexture(texItr, currentTick))
			{
				maxDelete--;
				if (maxDelete <= 0)
					break;
				if (allTextures.empty())
					break;
			}
		}
	}
	LatteTexture_RefreshInfoCache();
}

std::vector<LatteTexture*> LatteTC_GetDeleteableTextures()
{
	std::vector<LatteTexture*> texList;
	uint32 currentFrameCount = LatteGPUState.frameCounter;

	for (auto& itr : g_allTextures)
	{
		if(itr->lastAccessFrameCount == 0)
			continue;
		uint32 framesSinceLastAccess = currentFrameCount - itr->lastAccessFrameCount;
		if(framesSinceLastAccess < 3)
			continue;
		if (itr->isUpdatedOnGPU)
		{
			if (LatteTC_IsTextureDataOverwritten(itr))
				texList.emplace_back(itr);
		}
		else
		{
			texList.emplace_back(itr);
		}
	}

	return texList;
}

void LatteTC_UnloadAllTextures()
{
	std::vector<LatteTexture*> allTexturesCopy = LatteTexture::GetAllTextures();
	for (auto& itr : allTexturesCopy)
	{
		if(itr)
			LatteTexture_Delete(itr);
	}
	LatteRenderTarget_unloadAll();
	LatteTexture_ResetGlobalState();
	g_allTextures.clear();
	_botwLargeTexHax = 0;
}

size_t LatteTC_GetRegisteredTextureCount()
{
	return g_allTextures.size();
}
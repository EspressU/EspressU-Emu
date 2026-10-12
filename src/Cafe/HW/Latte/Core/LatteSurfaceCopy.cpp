#include "Cafe/HW/Latte/Core/Latte.h"
#include "Cafe/HW/Latte/Core/LatteDraw.h"
#include "Cafe/HW/Latte/Core/LatteShader.h"
#include "Cafe/HW/Latte/Core/LatteTexture.h"
#include "Cafe/HW/Latte/Core/LatteSurfaceCopy.h"

#include "Cafe/HW/Latte/Renderer/Renderer.h"

void gx2SurfaceCopySoftware(
	uint8* inputData, sint32 surfSrcHeight, sint32 srcPitch, sint32 srcDepth, uint32 srcSlice, uint32 srcSwizzle, uint32 srcHwTileMode,
	uint8* outputData, sint32 surfDstHeight, sint32 dstPitch, sint32 dstDepth, uint32 dstSlice, uint32 dstSwizzle, uint32 dstHwTileMode,
	uint32 copyWidth, uint32 copyHeight, uint32 copyBpp);

void LatteSurfaceCopy_CopyInRAM(const LatteSurfaceCopyParam& src, const LatteSurfaceCopyParam& dst, const LatteSurfaceCopyRect& rect)
{
	Latte::E_HWSURFFMT dstHwFormat = Latte::GetHWFormat(dst.surfaceFormat);

	sint32 copyWidth = rect.width;
	sint32 copyHeight = rect.height;
	if (Latte::IsCompressedFormat(dstHwFormat))
	{
		copyWidth = (copyWidth + 3) / 4;
		copyHeight = (copyHeight + 3) / 4;
	}
	uint32 dstBpp = Latte::GetFormatBits(dstHwFormat);

	gx2SurfaceCopySoftware((uint8*)MEMPTR<void>(src.physDataAddr).GetPtr(), src.heightInTexels, src.pitch, 1, src.sliceIndex, src.swizzle, (uint32)src.tilemode,
		(uint8*)MEMPTR<void>(dst.physDataAddr).GetPtr(), dst.heightInTexels, dst.pitch, 1, dst.sliceIndex, dst.swizzle, (uint32)dst.tilemode,
		copyWidth, copyHeight, dstBpp);
}

void LatteSurfaceCopy_copySurfaceNew(const LatteSurfaceCopyParam& src, const LatteSurfaceCopyParam& dst, const LatteSurfaceCopyRect& rect)
{
	cemu_assert_debug(rect.x == 0 && rect.y == 0);

	cemu_assert_debug((rect.x + rect.width) <= dst.pitch * (Latte::IsCompressedFormat(dst.surfaceFormat)?4:1));
	cemu_assert_debug((rect.x + rect.width) <= src.pitch * (Latte::IsCompressedFormat(src.surfaceFormat)?4:1));

	if (src.tilemode == Latte::E_GX2TILEMODE::TM_LINEAR_SPECIAL)
	{
		LatteSurfaceCopy_CopyInRAM(src, dst, rect);
		return;
	}

	LatteTexture* sourceTexture = nullptr;
	LatteTextureView* sourceView = LatteTC_GetTextureSliceViewOrTryCreate(src.physDataAddr, MPTR_NULL, src.surfaceFormat, Latte::MakeHWTileMode(src.tilemode), rect.x + rect.width, rect.y + rect.height, 1, src.pitch, src.swizzle, src.sliceIndex, 0);
	if (sourceView == nullptr)
	{
		LatteSurfaceCopy_CopyInRAM(src, dst, rect);
		return;
	}
	LatteTexture_UpdateDataToLatest(sourceView->baseTexture);
	sourceTexture = sourceView->baseTexture;
	LatteTC_MarkTextureStillInUse(sourceTexture);
	if (sourceTexture->reloadFromDynamicTextures)
	{
		LatteTexture_UpdateCacheFromDynamicTextures(sourceTexture);
		sourceTexture->reloadFromDynamicTextures = false;
	}
	if (dst.tilemode == Latte::E_GX2TILEMODE::TM_LINEAR_SPECIAL)
	{
		cemu_assert_debug(rect.x == 0 && rect.y == 0);
		LatteTextureReadback_ReadbackToLinearBlocking(sourceView, MEMPTR<uint8>(dst.physDataAddr).GetPtr(), rect.width, rect.height, dst.pitch);
		return;
	}
	LatteTexture* destinationTexture = nullptr;
	LatteTextureView* destinationView = LatteTextureViewLookupCache::lookupSliceMinSize(dst.physDataAddr, rect.x + rect.width, rect.y + rect.height, dst.pitch, 0, dst.sliceIndex, dst.surfaceFormat);
	if (destinationView)
		destinationTexture = destinationView->baseTexture;
	if (!destinationTexture)
	{
		uint32 dstSurfaceWidth = dst.pitch;
		uint32 dstSurfaceHeight = dst.heightInTexels;
		if (Latte::IsCompressedFormat(dst.surfaceFormat))
		{
			dstSurfaceWidth *= 4;
			dstSurfaceHeight *= 4;
		}
		destinationView = LatteTexture_CreateMapping(dst.physDataAddr, MPTR_NULL, dstSurfaceWidth, dstSurfaceHeight, dst.sliceIndex + 1, dst.pitch, Latte::MakeHWTileMode(dst.tilemode), dst.swizzle, 0, 1, dst.sliceIndex, 1, dst.surfaceFormat, dst.dim, Latte::IsMSAA(dst.dim) ? Latte::E_DIM::DIM_2D_MSAA : Latte::E_DIM::DIM_2D, false);
		destinationTexture = destinationView->baseTexture;
	}
	if (sourceTexture && destinationTexture)
	{
		LatteTC_MarkTextureStillInUse(destinationTexture);
		LatteTC_MarkTextureStillInUse(sourceTexture);
		sint32 realSrcSlice = src.sliceIndex;
		if (LatteTexture_doesEffectiveRescaleRatioMatch(sourceTexture, sourceView->firstMip, destinationTexture, destinationView->firstMip))
		{
			cemu_assert_debug(rect.x == 0 && rect.y == 0);
			sint32 copyWidth = rect.width;
			sint32 copyHeight = rect.height;
			sint32 effectiveCopyWidth = copyWidth;
			sint32 effectiveCopyHeight = copyHeight;
			LatteTexture_scaleToEffectiveSize(sourceTexture, &effectiveCopyWidth, &effectiveCopyHeight, 0);
			if (sourceView->baseTexture->isDepth != destinationView->baseTexture->isDepth)
				g_renderer->surfaceCopy_copySurfaceWithFormatConversion(sourceTexture, sourceView->firstMip, sourceView->firstSlice, destinationTexture, destinationView->firstMip, destinationView->firstSlice, copyWidth, copyHeight);
			else
				g_renderer->texture_copyImageSubData(sourceTexture, sourceView->firstMip, 0, 0, realSrcSlice, destinationTexture, destinationView->firstMip, 0, 0, destinationView->firstSlice, effectiveCopyWidth, effectiveCopyHeight, 1);
			const uint64 eventCounter = LatteTexture_getNextUpdateEventCounter();
			LatteTexture_MarkDynamicTextureAsChanged(destinationTexture->baseView, destinationView->firstSlice, destinationView->firstMip, eventCounter);
		}
		else
		{
			debug_printf("gx2CP_itHLECopySurface(): Copy texture with non-matching effective size\n");
		}
		LatteTC_ResetTextureChangeTracker(destinationTexture);
		destinationTexture->lastUpdateEventCounter = LatteTexture_getNextUpdateEventCounter();
		LatteTC_FlagSliceAsGPUUpdated(destinationTexture, destinationView->firstSlice, destinationView->firstMip);
	}
	else
		debug_printf("Source or destination texture does not exist\n");
	const bool sourceIsLinear = sourceTexture->tileMode == Latte::E_HWTILEMODE::TM_LINEAR_ALIGNED || sourceTexture->tileMode == Latte::E_HWTILEMODE::TM_LINEAR_GENERAL;
	const bool destinationIsLinear = destinationTexture->tileMode == Latte::E_HWTILEMODE::TM_LINEAR_ALIGNED || destinationTexture->tileMode == Latte::E_HWTILEMODE::TM_LINEAR_GENERAL;
	bool shouldReadback = !rect.skipCPUReadback && !sourceIsLinear && destinationIsLinear;
	if (destinationTexture->width == 8 && destinationTexture->height == 8 && destinationTexture->tileMode == Latte::E_HWTILEMODE::TM_1D_TILED_THIN1)
	{
		shouldReadback = true;
	}
	if (shouldReadback)
	{
		LatteTextureReadback_Initate(destinationView);
	}
}

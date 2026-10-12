#include "Cafe/OS/common/OSCommon.h"
#include "Cafe/HW/Latte/ISA/RegDefines.h"
#include "Cafe/HW/Latte/Core/LattePM4.h"
#include "Cafe/HW/Latte/Core/Latte.h"
#include "Cafe/HW/Latte/Core/LatteDraw.h"
#include "Cafe/HW/Latte/Core/LatteAsyncCommands.h"
#include "Cafe/HW/Latte/Core/LatteSurfaceCopy.h"
#include "Cafe/HW/Latte/LatteAddrLib/LatteAddrLib.h"
#include "util/highresolutiontimer/HighResolutionTimer.h"
#include "GX2.h"
#include "GX2_Resource.h"

template<uint32 copyBpp>
void gx2SurfaceCopySoftware_specialized(
	uint8* inputData, sint32 surfSrcHeight, sint32 srcPitch, sint32 srcDepth, uint32 srcSlice, uint32 srcSwizzle, uint32 srcHwTileMode,
	uint8* outputData, sint32 surfDstHeight, sint32 dstPitch, sint32 dstDepth, uint32 dstSlice, uint32 dstSwizzle, uint32 dstHwTileMode,
	uint32 copyWidth, uint32 copyHeight)
{
	uint32 srcPipeSwizzle = (srcSwizzle >> 8) & 1;
	uint32 srcBankSwizzle = ((srcSwizzle >> 9) & 3);
	uint32 dstPipeSwizzle = (dstSwizzle >> 8) & 1;
	uint32 dstBankSwizzle = ((dstSwizzle >> 9) & 3);

	for (uint32 y = 0; y < copyHeight; y++)
	{
		for (uint32 x = 0; x < copyWidth; x++)
		{
			uint32 srcOffset = 0;
			if (srcHwTileMode == 0 || srcHwTileMode == 1)
				srcOffset = LatteAddrLib::ComputeSurfaceAddrFromCoordLinear(x, y, srcSlice, 0, copyBpp, srcPitch, surfSrcHeight, srcDepth);
			else if (srcHwTileMode == 2 || srcHwTileMode == 3)
				srcOffset = LatteAddrLib::ComputeSurfaceAddrFromCoordMicroTiled(x, y, srcSlice, copyBpp, srcPitch, surfSrcHeight, (Latte::E_HWTILEMODE)srcHwTileMode, false);
			else
				srcOffset = LatteAddrLib::ComputeSurfaceAddrFromCoordMacroTiled(x, y, srcSlice, 0, copyBpp, srcPitch, surfSrcHeight, 1 * 1, (Latte::E_HWTILEMODE)srcHwTileMode, false, srcPipeSwizzle, srcBankSwizzle);
			uint8* inputBlockData = inputData + srcOffset;
			uint32 dstOffset = 0;
			if (dstHwTileMode == 0 || dstHwTileMode == 1)
				dstOffset = LatteAddrLib::ComputeSurfaceAddrFromCoordLinear(x, y, dstSlice, 0, copyBpp, dstPitch, surfDstHeight, dstDepth);
			else if (dstHwTileMode == 2 || dstHwTileMode == 3)
				dstOffset = LatteAddrLib::ComputeSurfaceAddrFromCoordMicroTiled(x, y, dstSlice, copyBpp, dstPitch, surfDstHeight, (Latte::E_HWTILEMODE)dstHwTileMode, false);
			else
				dstOffset = LatteAddrLib::ComputeSurfaceAddrFromCoordMacroTiled(x, y, dstSlice, 0, copyBpp, dstPitch, surfDstHeight, 1 * 1, (Latte::E_HWTILEMODE)dstHwTileMode, false, dstPipeSwizzle, dstBankSwizzle);
			uint8* outputBlockData = outputData + dstOffset;

			if constexpr (copyBpp == 8)
			{
				outputBlockData[0] = inputBlockData[0];
			}
			else if constexpr (copyBpp == 16)
			{
				*(uint16*)outputBlockData = *(uint16*)inputBlockData;
			}
			else if constexpr (copyBpp == 32)
			{
				*(uint32*)outputBlockData = *(uint32*)inputBlockData;
			}
			else if constexpr (copyBpp == 64)
			{
				*(uint32*)(outputBlockData + 0) = *(uint32*)(inputBlockData + 0);
				*(uint32*)(outputBlockData + 4) = *(uint32*)(inputBlockData + 4);
			}
			else if constexpr (copyBpp == 128)
			{
				*(uint32*)(outputBlockData + 0) = *(uint32*)(inputBlockData + 0);
				*(uint32*)(outputBlockData + 4) = *(uint32*)(inputBlockData + 4);
				*(uint32*)(outputBlockData + 8) = *(uint32*)(inputBlockData + 8);
				*(uint32*)(outputBlockData + 12) = *(uint32*)(inputBlockData + 12);
			}
		}
	}
}

void gx2SurfaceCopySoftware_fastPath_tm4Copy(uint8* inputData, sint32 surfSrcHeight, sint32 srcPitch, sint32 srcDepth, uint32 srcSlice, uint32 srcSwizzle,
	uint8* outputData, sint32 surfDstHeight, sint32 dstPitch, sint32 dstDepth, uint32 dstSlice, uint32 dstSwizzle,
	uint32 copyWidth, uint32 copyHeight, uint32 copyBpp)
{
	cemu_assert_debug((copyWidth & 7) == 0);
	cemu_assert_debug((copyHeight & 7) == 0);

	uint32 srcPipeSwizzle = (srcSwizzle >> 8) & 1;
	uint32 srcBankSwizzle = ((srcSwizzle >> 9) & 3);
	uint32 dstPipeSwizzle = (dstSwizzle >> 8) & 1;
	uint32 dstBankSwizzle = ((dstSwizzle >> 9) & 3);

	uint32 texelBytes = copyBpp / 8;

	if (srcSlice == dstSlice && srcSwizzle == dstSwizzle && surfSrcHeight == surfDstHeight && srcPitch == dstPitch)
	{
		for (uint32 y = 0; y < copyHeight; y += 8)
		{
			for (uint32 x = 0; x < copyWidth; x += 8)
			{
				uint32 offset = LatteAddrLib::ComputeSurfaceAddrFromCoordMacroTiled(x, y, srcSlice, 0, copyBpp, srcPitch, surfSrcHeight, 1 * 1, Latte::E_HWTILEMODE::TM_2D_TILED_THIN1, false, srcPipeSwizzle, srcBankSwizzle);
				uint8* inputBlockData = inputData + offset;
				uint8* outputBlockData = outputData + offset;
				memcpy(outputBlockData, inputBlockData, texelBytes * (8 * 8));
			}
		}
	}
	else
	{
		for (uint32 y = 0; y < copyHeight; y += 8)
		{
			for (uint32 x = 0; x < copyWidth; x += 8)
			{
				uint32 srcOffset = LatteAddrLib::ComputeSurfaceAddrFromCoordMacroTiled(x, y, srcSlice, 0, copyBpp, srcPitch, surfSrcHeight, 1 * 1, Latte::E_HWTILEMODE::TM_2D_TILED_THIN1, false, srcPipeSwizzle, srcBankSwizzle);
				uint8* inputBlockData = inputData + srcOffset;
				uint32 dstOffset = LatteAddrLib::ComputeSurfaceAddrFromCoordMacroTiled(x, y, dstSlice, 0, copyBpp, dstPitch, surfDstHeight, 1 * 1, Latte::E_HWTILEMODE::TM_2D_TILED_THIN1, false, dstPipeSwizzle, dstBankSwizzle);
				uint8* outputBlockData = outputData + dstOffset;
				memcpy(outputBlockData, inputBlockData, texelBytes * (8 * 8));
			}
		}
	}
}

void gx2SurfaceCopySoftware(
	uint8* inputData, sint32 surfSrcHeight, sint32 srcPitch, sint32 srcDepth, uint32 srcSlice, uint32 srcSwizzle, uint32 srcHwTileMode,
	uint8* outputData, sint32 surfDstHeight, sint32 dstPitch, sint32 dstDepth, uint32 dstSlice, uint32 dstSwizzle, uint32 dstHwTileMode,
	uint32 copyWidth, uint32 copyHeight, uint32 copyBpp)
{
	if (srcHwTileMode == 16)
		srcHwTileMode = 0;
	if (dstHwTileMode == 16)
		dstHwTileMode = 0;

	if (srcHwTileMode == 4 && dstHwTileMode == 4 && (copyWidth & 7) == 0 && (copyHeight & 7) == 0 && copyBpp <= 32)
	{
		gx2SurfaceCopySoftware_fastPath_tm4Copy(inputData, surfSrcHeight, srcPitch, srcDepth, srcSlice, srcSwizzle, outputData, surfDstHeight, dstPitch, dstDepth, dstSlice, dstSwizzle, copyWidth, copyHeight, copyBpp);
		return;
	}

	if (copyBpp == 8)
		gx2SurfaceCopySoftware_specialized<8>(inputData, surfSrcHeight, srcPitch, srcDepth, srcSlice, srcSwizzle, srcHwTileMode, outputData, surfDstHeight, dstPitch, dstDepth, dstSlice, dstSwizzle, dstHwTileMode, copyWidth, copyHeight);
	else if (copyBpp == 16)
		gx2SurfaceCopySoftware_specialized<16>(inputData, surfSrcHeight, srcPitch, srcDepth, srcSlice, srcSwizzle, srcHwTileMode, outputData, surfDstHeight, dstPitch, dstDepth, dstSlice, dstSwizzle, dstHwTileMode, copyWidth, copyHeight);
	else if (copyBpp == 32)
		gx2SurfaceCopySoftware_specialized<32>(inputData, surfSrcHeight, srcPitch, srcDepth, srcSlice, srcSwizzle, srcHwTileMode, outputData, surfDstHeight, dstPitch, dstDepth, dstSlice, dstSwizzle, dstHwTileMode, copyWidth, copyHeight);
	else if (copyBpp == 64)
		gx2SurfaceCopySoftware_specialized<64>(inputData, surfSrcHeight, srcPitch, srcDepth, srcSlice, srcSwizzle, srcHwTileMode, outputData, surfDstHeight, dstPitch, dstDepth, dstSlice, dstSwizzle, dstHwTileMode, copyWidth, copyHeight);
	else if (copyBpp == 128)
		gx2SurfaceCopySoftware_specialized<128>(inputData, surfSrcHeight, srcPitch, srcDepth, srcSlice, srcSwizzle, srcHwTileMode, outputData, surfDstHeight, dstPitch, dstDepth, dstSlice, dstSwizzle, dstHwTileMode, copyWidth, copyHeight);
	else
		cemu_assert_debug(false);
}

static bool GX2CopySurface_IsKnownExpensiveDynamicCopy(const GX2Surface* srcSurface, const GX2Surface* dstSurface, Latte::E_GX2SURFFMT srcFormat)
{
	bool isDynamicTexCopy = false;
	isDynamicTexCopy = isDynamicTexCopy || (srcSurface->imagePtr >= 0xF4000000 && srcSurface->width >= 800 && srcFormat == Latte::E_GX2SURFFMT::R11_G11_B10_FLOAT);
	isDynamicTexCopy = isDynamicTexCopy || (srcSurface->imagePtr >= 0xF4000000 && srcSurface->width >= 800 && srcFormat == Latte::E_GX2SURFFMT::R8_G8_B8_A8_UNORM);
	isDynamicTexCopy = isDynamicTexCopy || (srcSurface->imagePtr >= 0xF4000000 && srcSurface->width == 0xA0 && srcFormat == Latte::E_GX2SURFFMT::R32_FLOAT);
	isDynamicTexCopy = isDynamicTexCopy || (srcSurface->imagePtr >= 0xF4000000 && srcSurface->width == 1280 && srcFormat == Latte::E_GX2SURFFMT::R32_FLOAT);
	isDynamicTexCopy = isDynamicTexCopy || (srcSurface->imagePtr >= 0xF4000000 && srcSurface->width == 640 && srcSurface->height == 320 && srcFormat == Latte::E_GX2SURFFMT::R11_G11_B10_FLOAT);
	isDynamicTexCopy = isDynamicTexCopy || (srcSurface->width == 1280 && srcSurface->height == 720 && srcFormat == Latte::E_GX2SURFFMT::R8_G8_B8_A8_UNORM && srcSurface->tileMode != Latte::E_GX2TILEMODE::TM_LINEAR_ALIGNED);
	isDynamicTexCopy = isDynamicTexCopy || (srcSurface->width == 854 && srcSurface->height == 480 && srcFormat == Latte::E_GX2SURFFMT::R8_G8_B8_A8_UNORM);
	isDynamicTexCopy = isDynamicTexCopy || (srcSurface->imagePtr >= 0xF4000000 && srcSurface->width == 1152 && srcSurface->height == 720 && srcFormat == Latte::E_GX2SURFFMT::R11_G11_B10_FLOAT && (srcSurface->resFlag & GX2_RESFLAG_USAGE_COLOR_BUFFER) != 0);
	isDynamicTexCopy = isDynamicTexCopy || (srcSurface->imagePtr >= 0xF4000000 && srcSurface->width == 680 && srcSurface->height == 480 && srcFormat == Latte::E_GX2SURFFMT::R11_G11_B10_FLOAT && (srcSurface->resFlag & GX2_RESFLAG_USAGE_COLOR_BUFFER) != 0);
	isDynamicTexCopy = isDynamicTexCopy || (srcSurface->imagePtr >= 0xF4000000 && srcSurface->width == 1280 && srcSurface->height == 720 && srcFormat == Latte::E_GX2SURFFMT::R16_G16_B16_A16_FLOAT);
	isDynamicTexCopy = isDynamicTexCopy || (srcSurface->width == 322 && srcSurface->height == 182 && srcFormat == Latte::E_GX2SURFFMT::R16_G16_B16_A16_UNORM);
	isDynamicTexCopy = isDynamicTexCopy || (srcSurface->width == 640 && srcSurface->height == 360 && srcFormat == Latte::E_GX2SURFFMT::R16_G16_B16_A16_FLOAT);
	isDynamicTexCopy = isDynamicTexCopy || (srcSurface->width == 1920 && srcSurface->height == 1080 && srcFormat == Latte::E_GX2SURFFMT::R8_G8_B8_A8_UNORM && dstSurface->resFlag == 0x80000003);
	isDynamicTexCopy = isDynamicTexCopy || (srcSurface->width == 854 && srcSurface->height == 480 && srcFormat == Latte::E_GX2SURFFMT::R8_G8_B8_A8_UNORM && dstSurface->resFlag == 0x3);
	isDynamicTexCopy = isDynamicTexCopy || (srcSurface->width == 1280 && srcSurface->height == 720 && srcFormat == Latte::E_GX2SURFFMT::R8_G8_B8_A8_SRGB && dstSurface->resFlag == 0x3);
	isDynamicTexCopy = isDynamicTexCopy || (srcSurface->width == 854 && srcSurface->height == 480 && srcFormat == Latte::E_GX2SURFFMT::R8_G8_B8_A8_SRGB && dstSurface->resFlag == 0x3);
	isDynamicTexCopy = isDynamicTexCopy || (srcSurface->width == 1280 && srcSurface->height == 720 && srcFormat == Latte::E_GX2SURFFMT::R8_G8_B8_A8_SRGB && dstSurface->resFlag == 0x80000003);
	isDynamicTexCopy = isDynamicTexCopy || (srcSurface->width == 1280 && srcSurface->height == 720 && srcFormat == Latte::E_GX2SURFFMT::R8_G8_B8_A8_SRGB && srcSurface->resFlag == 0x80000003);
	isDynamicTexCopy = isDynamicTexCopy || (srcSurface->imagePtr >= 0xF4000000 && srcSurface->width == 1280 && srcSurface->height == 720 && srcFormat == Latte::E_GX2SURFFMT::R10_G10_B10_A2_UNORM);
	isDynamicTexCopy = isDynamicTexCopy || (srcSurface->imagePtr >= 0xF4000000 && srcSurface->width == 854 && srcSurface->height == 480 && srcFormat == Latte::E_GX2SURFFMT::R10_G10_B10_A2_UNORM);
	isDynamicTexCopy = isDynamicTexCopy || (srcSurface->width == 1920 && srcSurface->height == 1080 && srcFormat == Latte::E_GX2SURFFMT::R8_G8_B8_A8_UNORM && dstSurface->resFlag == 0x3);
	isDynamicTexCopy = isDynamicTexCopy || (srcSurface->width == 1024 && srcSurface->height == 1024 && srcFormat == Latte::E_GX2SURFFMT::R32_FLOAT && dstSurface->resFlag == 0x5);
	isDynamicTexCopy = isDynamicTexCopy || (srcSurface->width == 260 && srcSurface->height == 148 && srcFormat == Latte::E_GX2SURFFMT::R16_G16_B16_A16_FLOAT && dstSurface->resFlag == 0x3);
	isDynamicTexCopy = isDynamicTexCopy || (srcSurface->width == 1040 && srcSurface->height == 592 && srcFormat == Latte::E_GX2SURFFMT::R16_G16_B16_A16_FLOAT && dstSurface->resFlag == 0x3);
	isDynamicTexCopy = isDynamicTexCopy || (srcSurface->width == 854 && srcSurface->height == 480 && srcFormat == Latte::E_GX2SURFFMT::R8_G8_B8_A8_SRGB && srcSurface->resFlag == 0x3);
	isDynamicTexCopy = isDynamicTexCopy || (srcSurface->width == 1024 && srcSurface->height == 576 && srcFormat == Latte::E_GX2SURFFMT::D24_S8_UNORM && srcSurface->resFlag == 0x1);
	isDynamicTexCopy = isDynamicTexCopy || (srcSurface->width == 1152 && srcSurface->height == 648 && (srcFormat == Latte::E_GX2SURFFMT::R8_G8_B8_A8_UNORM || srcFormat == Latte::E_GX2SURFFMT::R16_G16_B16_A16_FLOAT) && srcSurface->resFlag == 0x1);
	isDynamicTexCopy = isDynamicTexCopy || (srcSurface->width == 576 && srcSurface->height == 324 && (srcFormat == Latte::E_GX2SURFFMT::R8_G8_B8_A8_UNORM || srcFormat == Latte::E_GX2SURFFMT::R16_G16_B16_A16_FLOAT) && srcSurface->resFlag == 0x1);
	return isDynamicTexCopy;
}

void GX2CopySurfaceInternal(GX2Surface* srcSurface, uint32 srcMip, uint32 srcSlice, GX2Surface* dstSurface, uint32 dstMip, uint32 dstSlice)
{
	sint32 dstWidth = dstSurface->width;
	sint32 dstHeight = dstSurface->height;
	sint32 srcWidth = srcSurface->width;
	sint32 srcHeight = srcSurface->height;

	sint32 dstMipWidth = std::max(dstWidth>>dstMip, 1);
	sint32 dstMipHeight = std::max(dstHeight>>dstMip, 1);
	sint32 srcMipWidth = std::max(srcWidth>>srcMip, 1);
	sint32 srcMipHeight = std::max(srcHeight>>srcMip, 1);

	if( dstMipWidth != srcMipWidth || dstMipHeight != srcMipHeight )
	{
		cemuLog_logDebugOnce(LogType::Force, "GX2CopySurface: Mismatching mip resolution");
		return;
	}
	Latte::E_GX2SURFFMT srcFormat = srcSurface->format;
	Latte::E_GX2SURFFMT dstFormat = dstSurface->format;
	auto srcHwFormat = Latte::GetHWFormat(srcFormat);
	auto dstHwFormat = Latte::GetHWFormat(dstFormat);
	LatteAddrLib::AddrSurfaceInfo_OUT surfOutSrc = {0};
	GX2::GX2CalculateSurfaceInfo(srcSurface, srcMip, &surfOutSrc);
	LatteAddrLib::AddrSurfaceInfo_OUT surfOutDst = {0};
	GX2::GX2CalculateSurfaceInfo(dstSurface, dstMip, &surfOutDst);
	if (srcSurface->numLevels == 0)
	{
		debug_printf("GX2CopySurface(): mip count is 0\n");
		return;
	}
	if( srcHwFormat != dstHwFormat )
	{
		cemuLog_logDebug(LogType::Force, "GX2CopySurface(): Format mismatch (src=0x{:04x} dst=0x{:04x})", (sint32)srcFormat, (sint32)dstFormat);
		return;
	}
	cemu_assert(srcMip < srcSurface->numLevels);
	uint8* srcDataPtr = nullptr;
	if( srcMip == 0 )
		srcDataPtr = (uint8*)memory_getPointerFromVirtualOffset(srcSurface->imagePtr);
	else if( srcMip == 1 )
		srcDataPtr = (uint8*)memory_getPointerFromVirtualOffset(srcSurface->mipPtr);
	else
		srcDataPtr = (uint8*)memory_getPointerFromVirtualOffset(srcSurface->mipPtr + srcSurface->mipOffset[srcMip - 1]);
	cemu_assert(dstMip < dstSurface->numLevels);
	uint8* dstDataPtr = nullptr;
	if( dstMip == 0 )
		dstDataPtr = (uint8*)memory_getPointerFromVirtualOffset(dstSurface->imagePtr);
	else if( dstMip == 1 )
		dstDataPtr = (uint8*)memory_getPointerFromVirtualOffset(dstSurface->mipPtr);
	else
		dstDataPtr = (uint8*)memory_getPointerFromVirtualOffset(dstSurface->mipPtr + dstSurface->mipOffset[dstMip - 1]);
	uint32 srcPitch = surfOutSrc.pitch;
	Latte::E_GX2TILEMODE srcTilemode = (srcSurface->tileMode == Latte::E_GX2TILEMODE::TM_LINEAR_SPECIAL) ? srcSurface->tileMode.value() : (Latte::E_GX2TILEMODE)surfOutSrc.hwTileMode;
	Latte::E_GX2TILEMODE dstTilemode = (dstSurface->tileMode == Latte::E_GX2TILEMODE::TM_LINEAR_SPECIAL) ? dstSurface->tileMode.value() : (Latte::E_GX2TILEMODE)surfOutDst.hwTileMode;

	if (srcTilemode == Latte::E_GX2TILEMODE::TM_LINEAR_GENERAL)
	{
		srcPitch = srcSurface->pitch >> srcMip;
		srcPitch = std::max<uint32>(srcPitch, 1);
	}
	uint32 copyWidth = std::max<uint32>(dstWidth>>dstMip, 1);
	uint32 copyHeight = std::max<uint32>(dstHeight>>dstMip, 1);

	cemu_assert(copyWidth <= std::max<uint32>(srcWidth>>srcMip, 1));
	cemu_assert(copyHeight <= std::max<uint32>(srcHeight>>srcMip, 1));

	cemuLog_log(LogType::GX2, "GX2CopySurface:");
	cemuLog_log(LogType::GX2,"srcSurface: imagePtr=0x{:08x} mipPtr=0x{:08x} swizzle=0x{:06x} width={} height={} depth={} pitch=0x{:x} tilemode=0x{:x} format=0x{:x} mip={} slice={}",
		(uint32)srcSurface->imagePtr, (uint32)srcSurface->mipPtr, (uint32)srcSurface->swizzle, (uint32)srcSurface->width, (uint32)srcSurface->height, (uint32)srcSurface->depth, (uint32)srcSurface->pitch,
		(uint32)srcSurface->tileMode.value(), (uint32)srcSurface->format.value(), srcMip, srcSlice);
	cemuLog_log(LogType::GX2, "dstSurface: imagePtr=0x{:08x} mipPtr=0x{:08x} swizzle=0x{:06x} width={} height={} depth={} pitch=0x{:x} tilemode=0x{:x} format=0x{:x} mip={} slice={}",
		(uint32)dstSurface->imagePtr, (uint32)dstSurface->mipPtr, (uint32)dstSurface->swizzle, (uint32)dstSurface->width, (uint32)dstSurface->height, (uint32)dstSurface->depth, (uint32)dstSurface->pitch,
		(uint32)dstSurface->tileMode.value(), (uint32)dstSurface->format.value(), dstMip, dstSlice);

	bool isGX2CPUCopy = srcSurface->tileMode == Latte::E_GX2TILEMODE::TM_LINEAR_SPECIAL || dstSurface->tileMode == Latte::E_GX2TILEMODE::TM_LINEAR_SPECIAL;
	if (isGX2CPUCopy)
	{
		LatteSurfaceCopyParam srcCopy{};
		srcCopy.physDataAddr = MEMPTR<void>(srcDataPtr).GetMPTR();
		srcCopy.swizzle = srcSurface->swizzle;
		srcCopy.surfaceFormat = srcSurface->format;
		srcCopy.heightInTexels = surfOutSrc.height;
		srcCopy.pitch = surfOutSrc.pitch;
		srcCopy.dim = srcSurface->dim;
		srcCopy.tilemode = srcTilemode;
		srcCopy.aa = srcSurface->aa;
		srcCopy.sliceIndex = (sint32)srcSlice;

		LatteSurfaceCopyParam dstCopy{};
		dstCopy.physDataAddr = MEMPTR<void>(dstDataPtr).GetMPTR();
		dstCopy.swizzle = dstSurface->swizzle;
		dstCopy.surfaceFormat = dstSurface->format;
		dstCopy.heightInTexels = surfOutDst.height;
		dstCopy.pitch = surfOutDst.pitch;
		dstCopy.dim = dstSurface->dim;
		dstCopy.tilemode = dstTilemode;
		dstCopy.aa = dstSurface->aa;
		dstCopy.sliceIndex = (sint32)dstSlice;

		LatteSurfaceCopyRect copyRect{};
		copyRect.x = 0;
		copyRect.y = 0;
		copyRect.width = copyWidth;
		copyRect.height = copyHeight;

		LatteAsyncCommand_queueTextureCopy(srcCopy, dstCopy, copyRect);
		LatteAsyncCommands_waitUntilAllProcessed();
		return;
	}

	const bool skipCPUReadback = GX2CopySurface_IsKnownExpensiveDynamicCopy(srcSurface, dstSurface, srcFormat);
	const uint32 copyFlags = skipCPUReadback ? LATTE_SURFACE_COPY_FLAG_SKIP_CPU_READBACK : 0;
	GX2::GX2ReserveCmdSpace(24);
	gx2WriteGather_submit(pm4HeaderType3(IT_HLE_COPY_SURFACE_NEW, 5+9*2),
	(uint32)0,
	(uint32)0,
	(uint32)copyWidth,
	(uint32)copyHeight,
	copyFlags,
	(uint32)MEMPTR<void>(srcDataPtr).GetMPTR(),
	(uint32)srcSurface->swizzle,
	(uint32)srcSurface->format.value(),
	(uint32)surfOutSrc.pitch,
	(uint32)surfOutSrc.height,
	srcSlice,
	(uint32)srcSurface->dim.value(),
	(uint32)srcTilemode,
	(uint32)srcSurface->aa,
	(uint32)MEMPTR<void>(dstDataPtr).GetMPTR(),
	(uint32)dstSurface->swizzle,
	(uint32)dstSurface->format.value(),
	(uint32)surfOutDst.pitch,
	(uint32)surfOutDst.height,
	dstSlice,
	(uint32)dstSurface->dim.value(),
	(uint32)dstTilemode,
	(uint32)dstSurface->aa
	);
}

void gx2Surface_GX2CopySurface(GX2Surface* srcSurface, uint32 srcMip, uint32 srcSlice, GX2Surface* dstSurface, uint32 dstMip, uint32 dstSlice)
{
	GX2CopySurfaceInternal(srcSurface, srcMip, srcSlice, dstSurface, dstMip, dstSlice);
}

void gx2Export_GX2CopySurface(PPCInterpreter_t* hCPU)
{
	GX2Surface* srcSurface = (GX2Surface*)memory_getPointerFromVirtualOffset(hCPU->gpr[3]);
	uint32 srcMip = hCPU->gpr[4];
	uint32 srcSlice = hCPU->gpr[5];
	GX2Surface* dstSurface = (GX2Surface*)memory_getPointerFromVirtualOffset(hCPU->gpr[6]);
	uint32 dstMip = hCPU->gpr[7];
	uint32 dstSlice = hCPU->gpr[8];
	gx2Surface_GX2CopySurface(srcSurface, srcMip, srcSlice, dstSurface, dstMip, dstSlice);
	osLib_returnFromFunction(hCPU, 0);
}

typedef struct
{
	sint32 left;
	sint32 top;
	sint32 right;
	sint32 bottom;
}GX2Rect_t;

typedef struct
{
	sint32 x;
	sint32 y;
}GX2Point_t;

void gx2Export_GX2CopySurfaceEx(PPCInterpreter_t* hCPU)
{
	cemuLog_logDebug(LogType::Force, "GX2CopySurfaceEx(0x{:08x},{},{},0x{:08x},{},{},{},0x{:08x},0x{:08x})", hCPU->gpr[3], hCPU->gpr[4], hCPU->gpr[5], hCPU->gpr[6], hCPU->gpr[7], hCPU->gpr[8], hCPU->gpr[9], hCPU->gpr[10], memory_readU32(hCPU->gpr[1]+0x8));
	GX2Surface* srcSurface = (GX2Surface*)memory_getPointerFromVirtualOffset(hCPU->gpr[3]);
	uint32 srcMip = hCPU->gpr[4];
	uint32 srcSlice = hCPU->gpr[5];
	GX2Surface* dstSurface = (GX2Surface*)memory_getPointerFromVirtualOffset(hCPU->gpr[6]);
	uint32 dstMip = hCPU->gpr[7];
	uint32 dstSlice = hCPU->gpr[8];
	sint32 rectCount = hCPU->gpr[9];
	MPTR rectSrcArrayMPTR = hCPU->gpr[10];
	MPTR pointDstArrayMPTR = memory_readU32(hCPU->gpr[1]+0x8);

	GX2Rect_t* rectSrc = (GX2Rect_t*)memory_getPointerFromVirtualOffset(rectSrcArrayMPTR);
	GX2Point_t* rectDst = (GX2Point_t*)memory_getPointerFromVirtualOffset(pointDstArrayMPTR);
	for (sint32 i = 0; i < rectCount; i++)
	{
		cemuLog_logDebug(LogType::Force, "rect left-top: {}/{} size: {}/{}", _swapEndianU32(rectSrc->left), _swapEndianU32(rectSrc->top), _swapEndianU32(rectSrc->right) - _swapEndianU32(rectSrc->left), _swapEndianU32(rectSrc->bottom) - _swapEndianU32(rectSrc->top));
	}

#ifdef CEMU_DEBUG_ASSERT
	if( rectCount != 1 )
		assert_dbg();
	if( srcMip != 0 )
		assert_dbg();
	if( srcSlice != 0 )
		assert_dbg();
	if( dstMip != 0 )
		assert_dbg();
	if( dstSlice != 0 )
		assert_dbg();
#endif

	for(sint32 i=0; i<rectCount; i++)
	{
		uint32 srcWidth = srcSurface->width;
		uint32 srcHeight = srcSurface->height;
		sint32 rectSrcX = (sint32)_swapEndianU32((uint32)rectSrc[i].left);
		sint32 rectSrcY = (sint32)_swapEndianU32((uint32)rectSrc[i].top);
		sint32 rectWidth = (sint32)_swapEndianU32((uint32)rectSrc[i].right) - rectSrcX;
		sint32 rectHeight = (sint32)_swapEndianU32((uint32)rectSrc[i].bottom) - rectSrcY;
		if( rectSrcX == 0 && rectSrcY == 0 && rectWidth == srcWidth && rectHeight == srcHeight )
		{
			gx2Surface_GX2CopySurface(srcSurface, srcMip, srcSlice, dstSurface, dstMip, dstSlice);
		}
	}

	osLib_returnFromFunction(hCPU, 0);
}

void gx2Export_GX2ResolveAAColorBuffer(PPCInterpreter_t* hCPU)
{
	debug_printf("GX2ResolveAAColorBuffer(0x%08x,0x%08x,%d,%d)\n", hCPU->gpr[3], hCPU->gpr[4], hCPU->gpr[5], hCPU->gpr[6]);
	GX2ColorBuffer* srcColorBuffer = (GX2ColorBuffer*)memory_getPointerFromVirtualOffset(hCPU->gpr[3]);
	GX2Surface* srcSurface = &srcColorBuffer->surface;
	GX2Surface* dstSurface = (GX2Surface*)memory_getPointerFromVirtualOffset(hCPU->gpr[4]);
	uint32 srcMip = srcColorBuffer->viewMip;
	uint32 dstMip = hCPU->gpr[5];
	uint32 srcSlice = srcColorBuffer->viewFirstSlice;
	uint32 dstSlice = hCPU->gpr[6];

#ifdef CEMU_DEBUG_ASSERT
	if( srcColorBuffer->viewMip != 0 || srcColorBuffer->viewFirstSlice != 0 )
		assert_dbg();
#endif

	sint32 dstWidth = dstSurface->width;
	sint32 dstHeight = dstSurface->height;
	sint32 srcWidth = srcSurface->width;
	sint32 srcHeight = srcSurface->height;

	uint32 dstMipWidth = std::max(dstWidth>>dstMip, 1);
	uint32 dstMipHeight = std::max(dstHeight>>dstMip, 1);
	uint32 srcMipWidth = std::max(srcWidth>>srcMip, 1);
	uint32 srcMipHeight = std::max(srcHeight>>srcMip, 1);

	if( srcSurface->width != dstSurface->width || srcSurface->height != dstSurface->height )
	{
		osLib_returnFromFunction(hCPU, 0);
		return;
	}
	if( dstMipWidth != srcMipWidth || dstMipHeight != srcMipHeight )
	{
		cemu_assert_suspicious();
		osLib_returnFromFunction(hCPU, 0);
		return;
	}
	Latte::E_GX2SURFFMT srcFormat = srcSurface->format;
	Latte::E_GX2SURFFMT dstFormat = dstSurface->format;
	sint32 srcStepX = 1;
	sint32 srcStepY = 1;
	sint32 dstStepX = 1;
	sint32 dstStepY = 1;
	auto srcHwFormat = Latte::GetHWFormat(srcFormat);
	auto dstHwFormat = Latte::GetHWFormat(dstFormat);
	if( Latte::IsCompressedFormat(srcHwFormat) )
	{
		srcStepX = 4;
		srcStepY = 4;
	}
	if(Latte::IsCompressedFormat(dstHwFormat) )
	{
		dstStepX = 4;
		dstStepY = 4;
	}
	cemu_assert_debug(srcStepX == dstStepX && srcStepY == dstStepY);

	if( srcHwFormat != dstHwFormat )
	{
		debug_printf("GX2ResolveAAColorBuffer(): Format mismatch\n");
		cemu_assert_unimplemented();
		osLib_returnFromFunction(hCPU, 0);
		return;
	}
	GX2CopySurfaceInternal(srcSurface, srcMip, srcSlice, dstSurface, dstMip, dstSlice);
	osLib_returnFromFunction(hCPU, 0);
}

void gx2Export_GX2ConvertDepthBufferToTextureSurface(PPCInterpreter_t* hCPU)
{
	cemuLog_log(LogType::GX2, "GX2ConvertDepthBufferToTextureSurface(0x{:x}, 0x{:x}, {}, {})", hCPU->gpr[3], hCPU->gpr[4], hCPU->gpr[5], hCPU->gpr[6]);
	GX2DepthBuffer* depthBuffer = (GX2DepthBuffer*)memory_getPointerFromVirtualOffset(hCPU->gpr[3]);
	GX2Surface* dstSurface = (GX2Surface*)memory_getPointerFromVirtualOffset(hCPU->gpr[4]);
	uint32 dstMip = hCPU->gpr[5];
	uint32 dstSlice = hCPU->gpr[6];

	if (dstMip != 0 || dstSlice != 0)
		debugBreakpoint();
	LatteAddrLib::AddrSurfaceInfo_OUT surfOutSrc = { 0 };
	GX2::GX2CalculateSurfaceInfo(&depthBuffer->surface, 0, &surfOutSrc);
	LatteAddrLib::AddrSurfaceInfo_OUT surfOutDst = { 0 };
	GX2::GX2CalculateSurfaceInfo(dstSurface, 0, &surfOutDst);

	if (depthBuffer->surface.imagePtr == dstSurface->imagePtr)
	{
		if (dstMip != 0 || dstSlice != 0)
			debugBreakpoint();
		debug_printf("In-place re-tiling\n");
		osLib_returnFromFunction(hCPU, 0);
		return;
	}

	uint32 numSlices = std::max<uint32>(depthBuffer->viewNumSlices, 1);
	for (uint32 subSliceIndex = 0; subSliceIndex < numSlices; subSliceIndex++)
	{
		GX2CopySurfaceInternal(&depthBuffer->surface, depthBuffer->viewMip.value(), depthBuffer->viewFirstSlice.value() + subSliceIndex, dstSurface, dstMip, dstSlice + subSliceIndex);
	}

	osLib_returnFromFunction(hCPU, 0);
}

namespace GX2
{
	void GX2SurfaceCopyInit()
	{
		osLib_addFunction("gx2", "GX2CopySurface", gx2Export_GX2CopySurface);
		osLib_addFunction("gx2", "GX2CopySurfaceEx", gx2Export_GX2CopySurfaceEx);
		osLib_addFunction("gx2", "GX2ResolveAAColorBuffer", gx2Export_GX2ResolveAAColorBuffer);
		osLib_addFunction("gx2", "GX2ConvertDepthBufferToTextureSurface", gx2Export_GX2ConvertDepthBufferToTextureSurface);
	}
};

void gx2CopySurfaceTest()
{

	return;

	BenchmarkTimer bt;

	bt.Start();
	for(sint32 i=0; i<100; i++)
		gx2SurfaceCopySoftware(
			memory_base + 0x10000000, 256, 256, 1, 0, 0, 4,
			memory_base + 0x20000000, 256, 256, 1, 0, 0, 4,
			64, 64, 32
		);
	bt.Stop();
	debug_printf("Copy 0 - %lfms\n", bt.GetElapsedMilliseconds());
	bt.Start();
	for (sint32 i = 0; i < 100; i++)
		gx2SurfaceCopySoftware(
			memory_base + 0x11000000, 256, 256, 1, 0, 0, 4,
			memory_base + 0x21000000, 256, 256, 1, 0, 0, 2,
			64, 64, 32
		);
	bt.Stop();
	debug_printf("Copy 1 - %lfms\n", bt.GetElapsedMilliseconds());
	bt.Start();
	for (sint32 i = 0; i < 100; i++)
		gx2SurfaceCopySoftware(
			memory_base + 0x12000000, 256, 256, 1, 0, 0, 1,
			memory_base + 0x22000000, 256, 256, 1, 0, 0, 4,
			64, 64, 128
		);
	bt.Stop();
	debug_printf("Copy 2 - %lfms\n", bt.GetElapsedMilliseconds());
	bt.Start();
	for (sint32 i = 0; i < 100; i++)
		gx2SurfaceCopySoftware(
			memory_base + 0x12000000, 256, 256, 1, 0, 0, 4,
			memory_base + 0x22000000, 256, 256, 1, 0, 0, 4,
			64, 512, 32
		);
	bt.Stop();
	debug_printf("Copy 3 - %lfms\n", bt.GetElapsedMilliseconds());

	cemu_assert_debug(false);

}

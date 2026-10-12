#pragma once
#include "Cafe/HW/Latte/ISA/LatteReg.h"
#include "Cafe/HW/Latte/LatteAddrLib/LatteAddrLib.h"

struct GX2Surface
{
	 betype<Latte::E_DIM> dim;
	 uint32be width;
	 uint32be height;
	 uint32be depth;
	 uint32be numLevels;
	 betype<Latte::E_GX2SURFFMT> format;
	 uint32be aa;
	 uint32be resFlag;
	 uint32be imageSize;
	 uint32be imagePtr;
	 uint32be mipSize;
	 uint32be mipPtr;
	 betype<Latte::E_GX2TILEMODE> tileMode;
	 uint32be swizzle;
	 uint32be alignment;
	 uint32be pitch;
	 uint32be mipOffset[13];
};

static_assert(sizeof(betype<Latte::E_DIM>) == 4);
static_assert(sizeof(betype<Latte::E_GX2TILEMODE>) == 4);
static_assert(sizeof(GX2Surface) == 0x74);

struct GX2ColorBuffer
{
	 GX2Surface		surface;
	 uint32be		viewMip;
	 uint32be		viewFirstSlice;
	 uint32be		viewNumSlices;
	 MEMPTR<void>	auxData;
	 uint32be		auxSize2;
	 uint32be		reg_size;
	 uint32be		reg_info;
	 uint32be		reg_view;
	 uint32be		reg_mask;
	 uint32be		reg4;
};

static_assert(sizeof(GX2ColorBuffer) == 0x9C);

struct GX2DepthBuffer
{
	 GX2Surface		surface;
	 uint32be		viewMip;
	 uint32be		viewFirstSlice;
	 uint32be		viewNumSlices;
	 MEMPTR<void>	hiZPtr;
	 uint32be		hiZSize;
	 float32be		clearDepth;
	 uint32be		clearStencil;
	 uint32be		reg_size;
	 uint32be		reg_view;
	 uint32be		reg_base;
	 uint32be		reg_htile_surface;
	 uint32be		reg_prefetch_limit;
	 uint32be		reg_preload_control;
	 uint32be		reg_poly_offset_db_fmt_cntl;
};

static_assert(sizeof(GX2DepthBuffer) == 0xAC);

namespace GX2
{
	void GX2CalculateSurfaceInfo(GX2Surface* surfacePtr, uint32 level, LatteAddrLib::AddrSurfaceInfo_OUT* pSurfOut);

	Latte::E_ENDIAN_SWAP GetSurfaceFormatSwapMode(Latte::E_GX2SURFFMT fmt);
	uint32 GetSurfaceColorBufferExportFormat(Latte::E_GX2SURFFMT fmt);

	void GX2CalcSurfaceSizeAndAlignment(GX2Surface* surface);

	void GX2SurfaceInit();
};
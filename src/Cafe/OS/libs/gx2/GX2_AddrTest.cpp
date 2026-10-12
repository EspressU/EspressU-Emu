#include "Cafe/OS/libs/coreinit/coreinit_DynLoad.h"
#include "Cafe/HW/Espresso/PPCCallback.h"
#include "Cafe/HW/Espresso/PPCState.h"
#include "Cafe/OS/common/OSCommon.h"
#include "Cafe/HW/Latte/ISA/LatteReg.h"
#include "Cafe/HW/Latte/Core/Latte.h"
#include "Cafe/HW/Latte/LatteAddrLib/LatteAddrLib.h"
#include "util/highresolutiontimer/HighResolutionTimer.h"

namespace GX2
{
	struct AddrCreate_INPUT
	{
		 uint32be structSize;
		 uint32be ukn04_maybeGen;
		 uint32be ukn08;
		 uint32be revision;
		 uint32be func_Alloc;
		 uint32be func_Free;
		 uint32be func_Debug;
		 uint32be ukn1C;
		 uint32be reg263C;
		 uint32be ukn24;
		 uint32be ukn28;
		 uint32be ukn2C;
		 uint32be ukn30;
		 uint32be ukn34;
		 uint32be ukn38;
		 uint32be ukn3C;
		 uint32be ukn40;
	};

	struct AddrCreate_OUTPUT
	{
		uint32be structSize;
		MEMPTR<void> addrLibPtr;
	};

	static_assert(sizeof(AddrCreate_INPUT) == 0x44);
	static_assert(sizeof(AddrCreate_OUTPUT) == 8);

	struct ADDRAllocParam
	{
		uint32be ukn00;
		uint32be ukn04;
		uint32be size;
	};

	struct ADDRComputeSurfaceInfo_INPUT
	{
		uint32be structSize;
		betype<Latte::E_HWTILEMODE> tileMode;
		betype<Latte::E_HWSURFFMT> format;
		uint32be bpp;
		uint32be numSamples;
		uint32be width;
		uint32be height;
		uint32be numSlices;
		uint32be slice;
		uint32be mipLevel;
		uint32be _flags;
		uint32be numFrags;
		MEMPTR<void> tileInfo;
		uint32be tileType;
		uint32be tileIndex;

		enum FLAG_BITS
		{
			FLAG_BIT_CUBE = (1 << 27),
			FLAG_BIT_VOLUME = (1 << 26),

			FLAG_BIT_OPT4SPACE = (1 << 19),
		};

		void SetFlagCube(bool f)
		{
			if (f) _flags |= FLAG_BIT_CUBE;
			else _flags &= ~FLAG_BIT_CUBE;
		}

		void SetFlagVolume(bool f)
		{
			if (f) _flags |= FLAG_BIT_VOLUME;
			else _flags &= ~FLAG_BIT_VOLUME;
		}

		void SetFlagOpt4Space(bool f)
		{
			if (f) _flags |= FLAG_BIT_OPT4SPACE;
			else _flags &= ~FLAG_BIT_OPT4SPACE;
		}
	};

	static_assert(sizeof(ADDRComputeSurfaceInfo_INPUT) == 0x3C);

	struct ADDRComputeSurfaceInfo_OUTPUT
	{
		 uint32be structSize;
		 uint32be pitch;
		 uint32be height;
		 uint32be depth;
		 uint64be surfSize;
		 uint32be tileMode;
		 uint32be baseAlign;
		 uint32be pitchAlign;
		 uint32be heightAlign;
		 uint32be depthAlign;
		 uint32be bpp;
		 uint32be pixelPitch;
		 uint32be pixelHeight;
		 uint32be pixelBits;
		 uint32be sliceSize;
		 uint32be pitchTileMax;
		 uint32be heightTileMax;
		 uint32be sliceTileMax;
		 MEMPTR<void> tileInfo;
		 uint32be tileType;
		 uint32be tileIndex;
		 MEMPTR<void> stereoInfo;
		 uint32be _padding;
	};

	static_assert(sizeof(ADDRComputeSurfaceInfo_OUTPUT) == 0x60);

	static void _cb_alloc(PPCInterpreter_t* hCPU)
	{
		ppcDefineParamStructPtr(param, ADDRAllocParam, 0);
		uint32 r = coreinit_allocFromSysArea(param->size, 0x10);
		osLib_returnFromFunction(hCPU, r);
	}

	static void _cb_free(PPCInterpreter_t* hCPU)
	{
		cemu_assert_unimplemented();
	}

	static void _cb_debug(PPCInterpreter_t* hCPU)
	{
		cemu_assert_unimplemented();
	}

	static void* sAddrLib{};
	static uint32be tclFunc_AddrCreate = 0;
	static uint32be tclFunc_AddrComputeSurfaceInfo = 0;

	void _TestAddrLib_Init()
	{
		uint32be tclHandle;
		uint32 r = coreinit::OSDynLoad_Acquire("tcl_addr_test.rpl", &tclHandle);
		cemu_assert_debug(r == 0);

		r = coreinit::OSDynLoad_FindExport(tclHandle, 0, "AddrCreate", &tclFunc_AddrCreate);
		cemu_assert_debug(r == 0);
		r = coreinit::OSDynLoad_FindExport(tclHandle, 0, "AddrComputeSurfaceInfo", &tclFunc_AddrComputeSurfaceInfo);
		cemu_assert_debug(r == 0);

		StackAllocator<AddrCreate_INPUT> addrCreateIn;
		memset(addrCreateIn.GetPointer(), 0, sizeof(addrCreateIn));
		addrCreateIn->structSize = sizeof(addrCreateIn);

		addrCreateIn->ukn04_maybeGen = 6;
		addrCreateIn->ukn08 = 0x51;
		addrCreateIn->revision = 71;
		addrCreateIn->reg263C = 0x44902;
		addrCreateIn->ukn24 = 0;

		addrCreateIn->func_Alloc = PPCInterpreter_makeCallableExportDepr(_cb_alloc);
		addrCreateIn->func_Free = PPCInterpreter_makeCallableExportDepr(_cb_free);
		addrCreateIn->func_Debug = PPCInterpreter_makeCallableExportDepr(_cb_debug);

		StackAllocator<AddrCreate_OUTPUT> addrCreateOut;
		memset(addrCreateOut.GetPointer(), 0, sizeof(addrCreateOut));
		addrCreateOut->structSize = sizeof(addrCreateOut);

		r = PPCCoreCallback((uint32)tclFunc_AddrCreate, addrCreateIn.GetPointer(), addrCreateOut.GetPointer());
		sAddrLib = addrCreateOut->addrLibPtr;
		cemu_assert_debug(r == 0 && sAddrLib != nullptr);
	}

	void _TestAddrLib_CalculateSurfaceInfo(Latte::E_GX2SURFFMT surfaceFormat, uint32 surfaceWidth, uint32 surfaceHeight, uint32 surfaceDepth, Latte::E_DIM surfaceDim, Latte::E_GX2TILEMODE surfaceTileMode, uint32 surfaceAA, uint32 level, ADDRComputeSurfaceInfo_OUTPUT* paramOut)
	{
		StackAllocator<ADDRComputeSurfaceInfo_INPUT> _paramIn;
		ADDRComputeSurfaceInfo_INPUT& paramIn = *_paramIn.GetPointer();
		memset(&paramIn, 0, sizeof(ADDRComputeSurfaceInfo_INPUT));
		memset(paramOut, 0, sizeof(ADDRComputeSurfaceInfo_OUTPUT));
		Latte::E_HWSURFFMT hwFormat = GetHWFormat(surfaceFormat);
		if (surfaceTileMode == Latte::E_GX2TILEMODE::TM_LINEAR_SPECIAL)
		{
			uint32 numSamples = 1 << surfaceAA;
			uint32 blockSize = IsCompressedFormat(surfaceFormat) ? 4 : 1;
			uint32 width = ((surfaceWidth >> level) + blockSize - 1) & ~(blockSize - 1);
			paramOut->bpp = GetFormatBits(hwFormat);
			paramOut->structSize = sizeof(ADDRComputeSurfaceInfo_OUTPUT);
			paramOut->pitch = width / blockSize;
			paramOut->pixelBits = paramOut->bpp;
			paramOut->baseAlign = 1;
			paramOut->pitchAlign = 1;
			paramOut->heightAlign = 1;
			paramOut->depthAlign = 1;
			switch (surfaceDim)
			{
			case Latte::E_DIM::DIM_1D:
				paramOut->height = 1;
				paramOut->depth = 1;
				break;
			case Latte::E_DIM::DIM_2D:
				paramOut->height = std::max<uint32>(surfaceHeight >> level, 1);
				paramOut->depth = 1;
				break;
			case Latte::E_DIM::DIM_3D:
				paramOut->height = surfaceHeight >> level;
				paramOut->height = std::max<uint32>(paramOut->height, 1);
				paramOut->depth = std::max<uint32>(surfaceDepth >> level, 1);
				break;
			case Latte::E_DIM::DIM_CUBEMAP:
				paramOut->height = std::max<uint32>(surfaceHeight >> level, 1);
				paramOut->depth = std::max<uint32>(surfaceDepth, 6);
				break;
			case Latte::E_DIM::DIM_1D_ARRAY:
				paramOut->height = 1;
				paramOut->depth = surfaceDepth;
				break;
			case Latte::E_DIM::DIM_2D_ARRAY:
				paramOut->height = std::max<uint32>(surfaceHeight >> level, 1);
				paramOut->depth = surfaceDepth;
				break;
			default:
				break;
			}
			paramOut->height = ((paramOut->height + blockSize - 1) & ~(blockSize - 1)) / (uint64)blockSize;
			paramOut->pixelPitch = ((surfaceWidth >> level) + blockSize - 1) & ~(blockSize - 1);
			paramOut->pixelPitch = std::max<uint32>(paramOut->pixelPitch, blockSize);
			paramOut->pixelHeight = ((surfaceHeight >> level) + blockSize - 1) & ~(blockSize - 1);
			paramOut->pixelHeight = std::max<uint32>(paramOut->pixelHeight, blockSize);;
			paramOut->pitch = std::max<uint32>(paramOut->pitch, 1);
			paramOut->height = std::max<uint32>(paramOut->height, 1);
			paramOut->surfSize = paramOut->bpp * numSamples * paramOut->depth * paramOut->height * paramOut->pitch >> 3;
			if (surfaceDim == Latte::E_DIM::DIM_3D)
				paramOut->sliceSize = (uint32)(paramOut->surfSize);
			else
			{
				if (paramOut->surfSize == 0 && paramOut->depth == 0)
					paramOut->sliceSize = 0;
				else
					paramOut->sliceSize = ((uint32)paramOut->surfSize.value() / paramOut->depth);
			}
			paramOut->pitchTileMax = (paramOut->pitch >> 3) - 1;
			paramOut->heightTileMax = (paramOut->height >> 3) - 1;
			paramOut->sliceTileMax = (paramOut->height * paramOut->pitch >> 6) - 1;
		}
		else
		{
			paramIn.structSize = sizeof(paramIn);
			paramIn.tileMode = Latte::MakeHWTileMode(surfaceTileMode);
			paramIn.format = hwFormat;
			paramIn.bpp = GetFormatBits(hwFormat);
			paramIn.numSamples = 1 << surfaceAA;
			paramIn.numFrags = paramIn.numSamples;
			paramIn.width = std::max<uint32>(surfaceWidth >> level, 1);
			switch (surfaceDim)
			{
			case Latte::E_DIM::DIM_1D:
				paramIn.height = 1;
				paramIn.numSlices = 1;
				break;
			case Latte::E_DIM::DIM_2D:
				paramIn.height = std::max<uint32>(surfaceHeight >> level, 1);
				paramIn.numSlices = 1;
				break;
			case Latte::E_DIM::DIM_3D:
				paramIn.height = std::max<uint32>(surfaceHeight >> level, 1);
				paramIn.numSlices = std::max<uint32>(surfaceDepth >> level, 1);
				break;
			case Latte::E_DIM::DIM_CUBEMAP:
				paramIn.height = std::max<uint32>(surfaceHeight >> level, 1);
				paramIn.numSlices = std::max<uint32>(surfaceDepth, 6);
				paramIn.SetFlagCube(true);
				break;
			case Latte::E_DIM::DIM_1D_ARRAY:
				paramIn.height = 1;
				paramIn.numSlices = surfaceDepth;
				break;
			case Latte::E_DIM::DIM_2D_ARRAY:
				paramIn.height = std::max<uint32>(surfaceHeight >> level, 1);
				paramIn.numSlices = surfaceDepth;
				break;
			case Latte::E_DIM::DIM_2D_MSAA:
				paramIn.height = std::max<uint32>(surfaceHeight >> level, 1);
				paramIn.numSlices = 1;
				break;
			case Latte::E_DIM::DIM_2D_ARRAY_MSAA:
				paramIn.height = std::max<uint32>(surfaceHeight >> level, 1);
				paramIn.numSlices = surfaceDepth;
				break;
			default:
				break;
			}
			paramIn.slice = 0;
			paramIn.mipLevel = level;
			if (surfaceDim == Latte::E_DIM::DIM_3D)
				paramIn.SetFlagVolume(true);
			paramIn.SetFlagOpt4Space(level == 0);
			paramOut->structSize = sizeof(ADDRComputeSurfaceInfo_OUTPUT);
			PPCCoreCallback((uint32)tclFunc_AddrComputeSurfaceInfo, sAddrLib, _paramIn.GetPointer(), paramOut);
		}
	}

	void _TestAddrLib_Compare(uint32 surfaceWidth, uint32 surfaceHeight, uint32 surfaceDepth, Latte::E_DIM surfaceDim, Latte::E_GX2SURFFMT surfaceFormat, Latte::E_GX2TILEMODE surfaceTileMode, uint32 surfaceAA, uint32 level)
	{
		StackAllocator<ADDRComputeSurfaceInfo_OUTPUT> _paramOut;
		ADDRComputeSurfaceInfo_OUTPUT& tclSurfInfo = *_paramOut.GetPointer();
		_TestAddrLib_CalculateSurfaceInfo(surfaceFormat, surfaceWidth, surfaceHeight, surfaceDepth, surfaceDim, surfaceTileMode, surfaceAA, level, _paramOut.GetPointer());
		LatteAddrLib::AddrSurfaceInfo_OUT ourSurfInfo;
		LatteAddrLib::GX2CalculateSurfaceInfo(surfaceFormat, surfaceWidth, surfaceHeight, surfaceDepth, surfaceDim, surfaceTileMode, surfaceAA, level, &ourSurfInfo);
		cemu_assert(tclSurfInfo.pitchAlign == ourSurfInfo.pitchAlign);
		cemu_assert((Latte::E_HWTILEMODE)tclSurfInfo.tileMode.value() == ourSurfInfo.hwTileMode);
		cemu_assert(tclSurfInfo.baseAlign == ourSurfInfo.baseAlign);
		cemu_assert(tclSurfInfo.surfSize == ourSurfInfo.surfSize);
		cemu_assert(tclSurfInfo.depthAlign == ourSurfInfo.depthAlign);
		cemu_assert(tclSurfInfo.pitch == ourSurfInfo.pitch);
		cemu_assert(tclSurfInfo.sliceSize == ourSurfInfo.sliceSize);
	}

	void _TestAddrLib_Run()
	{
		uint32 surfaceAA = 0;

		std::vector<Latte::E_DIM> dimList = {
			Latte::E_DIM::DIM_1D,
			Latte::E_DIM::DIM_2D,
			Latte::E_DIM::DIM_3D,
			Latte::E_DIM::DIM_CUBEMAP,
			Latte::E_DIM::DIM_1D_ARRAY,
			Latte::E_DIM::DIM_2D_ARRAY,
			Latte::E_DIM::DIM_2D_MSAA,
			Latte::E_DIM::DIM_2D_ARRAY_MSAA
		};

		std::vector<Latte::E_GX2TILEMODE> tilemodeList = {
			Latte::E_GX2TILEMODE::TM_LINEAR_GENERAL,
			Latte::E_GX2TILEMODE::TM_LINEAR_ALIGNED,
			Latte::E_GX2TILEMODE::TM_1D_TILED_THIN1,
			Latte::E_GX2TILEMODE::TM_1D_TILED_THICK,
			Latte::E_GX2TILEMODE::TM_2D_TILED_THIN1,
			Latte::E_GX2TILEMODE::TM_2D_TILED_THIN4,
			Latte::E_GX2TILEMODE::TM_2D_TILED_THIN2,
			Latte::E_GX2TILEMODE::TM_2D_TILED_THICK,
			Latte::E_GX2TILEMODE::TM_2B_TILED_THIN1,
			Latte::E_GX2TILEMODE::TM_2B_TILED_THIN2,
			Latte::E_GX2TILEMODE::TM_2B_TILED_THIN4,
			Latte::E_GX2TILEMODE::TM_2B_TILED_THICK,
			Latte::E_GX2TILEMODE::TM_3D_TILED_THIN1,
			Latte::E_GX2TILEMODE::TM_3D_TILED_THICK,
			Latte::E_GX2TILEMODE::TM_3B_TILED_THIN1,
			Latte::E_GX2TILEMODE::TM_3B_TILED_THICK,
			Latte::E_GX2TILEMODE::TM_LINEAR_SPECIAL,
			Latte::E_GX2TILEMODE::TM_32_SPECIAL,
		};

		std::vector<Latte::E_GX2SURFFMT> formatList = {
			Latte::E_GX2SURFFMT::HWFMT_8, Latte::E_GX2SURFFMT::HWFMT_8_8, Latte::E_GX2SURFFMT::HWFMT_8_8_8_8,
			Latte::E_GX2SURFFMT::R32_UINT, Latte::E_GX2SURFFMT::R32_G32_UINT, Latte::E_GX2SURFFMT::R32_G32_B32_A32_UINT,
			Latte::E_GX2SURFFMT::HWFMT_BC1, Latte::E_GX2SURFFMT::HWFMT_BC2, Latte::E_GX2SURFFMT::HWFMT_BC3, Latte::E_GX2SURFFMT::HWFMT_BC4, Latte::E_GX2SURFFMT::HWFMT_BC5
		};

		std::vector<uint32> resXYList = {
			0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 13, 14, 15, 16, 17,
			31, 32, 33, 50, 63, 64, 65, 127, 128, 129, 200, 253, 254, 255, 256, 257,
			511, 512, 513, 1023, 1024, 1025, 2047, 2048, 2049, 4095, 4096, 4097
		};

		debug_printf("Running AddrLib test...\n");

		BenchmarkTimer timer;
		timer.Start();
		size_t index = 0;
		for (auto dim : dimList)
		{
			debug_printf("%d/%d\n", (int)index, (int)dimList.size());
			index++;
			for (auto tileMode : tilemodeList)
			{
				for (auto format : formatList)
				{
					for (uint32 level = 0; level < 16; level++)
					{
						for (auto depth : resXYList)
						{
							for (auto height : resXYList)
							{
								for (auto width : resXYList)
								{
									_TestAddrLib_Compare(width, height, depth, dim, format, tileMode, surfaceAA, level);
								}
							}
						}
					}
				}
			}
		}
		timer.Stop();
		debug_printf("Test complete (in %d seconds)\n", (int)(timer.GetElapsedMilliseconds() * 0.001));
		assert_dbg();
	}

	void _test_AddrLib()
	{
		return;
		_TestAddrLib_Init();
		_TestAddrLib_Run();
	}
}

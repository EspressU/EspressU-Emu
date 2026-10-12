#include "Cafe/OS/common/OSCommon.h"
#include "Cafe/HW/Latte/ISA/RegDefines.h"
#include "Cafe/HW/Latte/Core/Latte.h"
#include "Cafe/HW/Latte/Core/LatteDraw.h"
#include "Cafe/HW/Latte/ISA/LatteReg.h"
#include "Cafe/HW/Latte/Core/LattePM4.h"

#include "GX2.h"
#include "GX2_Shader.h"

void gx2Export_GX2SetPixelShader(PPCInterpreter_t* hCPU)
{
	cemuLog_log(LogType::GX2, "GX2SetPixelShader(0x{:08x})", hCPU->gpr[3]);
	GX2PixelShader_t* pixelShader = (GX2PixelShader_t*)memory_getPointerFromVirtualOffset(hCPU->gpr[3]);

	uint32 numInputs = _swapEndianU32(pixelShader->regs[4]);
	if( numInputs > 0x20 )
		numInputs = 0x20;

	GX2::GX2ReserveCmdSpace(26 + numInputs);

	MPTR shaderProgramAddr;
	uint32 shaderProgramSize;

	if( _swapEndianU32(pixelShader->shaderPtr) != MPTR_NULL )
	{
		shaderProgramAddr = _swapEndianU32(pixelShader->shaderPtr);
		shaderProgramSize = _swapEndianU32(pixelShader->shaderSize);
	}
	else
	{
		shaderProgramAddr = pixelShader->rBuffer.GetVirtualAddr();
		shaderProgramSize = pixelShader->rBuffer.GetSize();
	}

	gx2WriteGather_submit(
		pm4HeaderType3(IT_SET_CONTEXT_REG, 6),
		mmSQ_PGM_START_PS - 0xA000,
		memory_virtualToPhysical(shaderProgramAddr)>>8,
		shaderProgramSize>>3,
		0x100000,
		0x100000,
		_swapEndianU32(pixelShader->regs[0]),
		pm4HeaderType3(IT_SET_CONTEXT_REG, 3),
		mmSPI_PS_IN_CONTROL_0-0xA000,
		_swapEndianU32(pixelShader->regs[2]),
		_swapEndianU32(pixelShader->regs[3]));
	gx2WriteGather_submitU32AsBE(pm4HeaderType3(IT_SET_CONTEXT_REG, 1+numInputs));
	gx2WriteGather_submitU32AsBE(mmSPI_PS_INPUT_CNTL_0-0xA000);
	for(uint32 i=0; i<numInputs; i++)
	{
		uint32 inputData = _swapEndianU32(pixelShader->regs[5+i]);
		gx2WriteGather_submitU32AsBE(inputData);
	}

	gx2WriteGather_submit(
		pm4HeaderType3(IT_SET_CONTEXT_REG, 2),
		mmCB_SHADER_MASK-0xA000,
		_swapEndianU32(pixelShader->regs[37]),
		pm4HeaderType3(IT_SET_CONTEXT_REG, 2),
		mmCB_SHADER_CONTROL-0xA000,
		_swapEndianU32(pixelShader->regs[38]),
		pm4HeaderType3(IT_SET_CONTEXT_REG, 2),
		mmDB_SHADER_CONTROL-0xA000,
		_swapEndianU32(pixelShader->regs[39]),
		pm4HeaderType3(IT_SET_CONTEXT_REG, 2),
		mmSPI_INPUT_Z-0xA000,
		_swapEndianU32(pixelShader->regs[40]));

	osLib_returnFromFunction(hCPU, 0);
}

void gx2Export_GX2SetGeometryShader(PPCInterpreter_t* hCPU)
{
	cemuLog_log(LogType::GX2, "GX2SetGeometryShader(0x{:08x})", hCPU->gpr[3]);

	GX2GeometryShader_t* geometryShader = (GX2GeometryShader_t*)memory_getPointerFromVirtualOffset(hCPU->gpr[3]);
	uint32 numOutputIds = _swapEndianU32(geometryShader->regs[7]);
	numOutputIds = std::min<uint32>(numOutputIds, 0xA);
	uint32 reserveSize = 38;
	if (numOutputIds != 0)
		reserveSize += 2 + numOutputIds;
	if( _swapEndianU32(geometryShader->useStreamout) != 0 )
		reserveSize += 2 + 12;

	GX2::GX2ReserveCmdSpace(reserveSize);

	MPTR shaderProgramAddr;
	uint32 shaderProgramSize;

	if( _swapEndianU32(geometryShader->shaderPtr) != MPTR_NULL )
	{
		shaderProgramAddr = _swapEndianU32(geometryShader->shaderPtr);
		shaderProgramSize = _swapEndianU32(geometryShader->shaderSize);
	}
	else
	{
		shaderProgramAddr = geometryShader->rBuffer.GetVirtualAddr();
		shaderProgramSize = geometryShader->rBuffer.GetSize();
	}

	gx2WriteGather_submitU32AsBE(pm4HeaderType3(IT_SET_CONTEXT_REG, 6));
	gx2WriteGather_submitU32AsBE(mmSQ_PGM_START_GS-0xA000);
	gx2WriteGather_submitU32AsBE(memory_virtualToPhysical(shaderProgramAddr)>>8);
	gx2WriteGather_submitU32AsBE(shaderProgramSize>>3);
	gx2WriteGather_submitU32AsBE(0x100000);
	gx2WriteGather_submitU32AsBE(0x100000);
	gx2WriteGather_submitU32AsBE(_swapEndianU32(geometryShader->regs[0]));

	uint32 primitiveOut = _swapEndianU32(geometryShader->regs[1]);

	gx2WriteGather_submitU32AsBE(pm4HeaderType3(IT_SET_CONTEXT_REG, 2));
	gx2WriteGather_submitU32AsBE(mmVGT_GS_OUT_PRIM_TYPE-0xA000);
	gx2WriteGather_submitU32AsLE(geometryShader->regs[1]);

	gx2WriteGather_submit(
		pm4HeaderType3(IT_SET_CONTEXT_REG, 2),
		Latte::REGADDR::VGT_GS_MODE - 0xA000,
		geometryShader->reg.VGT_GS_MODE
	);

	gx2WriteGather_submitU32AsBE(pm4HeaderType3(IT_SET_CONTEXT_REG, 2));
	gx2WriteGather_submitU32AsBE(mmSQ_PGM_RESOURCES_GS-0xA000);
	gx2WriteGather_submitU32AsLE(geometryShader->regs[0]);

	gx2WriteGather_submitU32AsBE(pm4HeaderType3(IT_SET_CONTEXT_REG, 2));
	gx2WriteGather_submitU32AsBE(mmSQ_GS_VERT_ITEMSIZE-0xA000);
	gx2WriteGather_submitU32AsLE(geometryShader->regs[5]);

	if( _swapEndianU32(geometryShader->useStreamout) != 0 )
	{
		gx2WriteGather_submitU32AsBE(pm4HeaderType3(IT_SET_CONTEXT_REG, 2));
		gx2WriteGather_submitU32AsBE(mmVGT_STRMOUT_VTX_STRIDE_0-0xA000);
		gx2WriteGather_submitU32AsBE(_swapEndianU32(geometryShader->streamoutStride[0])>>2);
		gx2WriteGather_submitU32AsBE(pm4HeaderType3(IT_SET_CONTEXT_REG, 2));
		gx2WriteGather_submitU32AsBE(mmVGT_STRMOUT_VTX_STRIDE_1-0xA000);
		gx2WriteGather_submitU32AsBE(_swapEndianU32(geometryShader->streamoutStride[1])>>2);
		gx2WriteGather_submitU32AsBE(pm4HeaderType3(IT_SET_CONTEXT_REG, 2));
		gx2WriteGather_submitU32AsBE(mmVGT_STRMOUT_VTX_STRIDE_2-0xA000);
		gx2WriteGather_submitU32AsBE(_swapEndianU32(geometryShader->streamoutStride[2])>>2);
		gx2WriteGather_submitU32AsBE(pm4HeaderType3(IT_SET_CONTEXT_REG, 2));
		gx2WriteGather_submitU32AsBE(mmVGT_STRMOUT_VTX_STRIDE_3-0xA000);
		gx2WriteGather_submitU32AsBE(_swapEndianU32(geometryShader->streamoutStride[3])>>2);
	}

	gx2WriteGather_submitU32AsBE(pm4HeaderType3(IT_SET_CONTEXT_REG, 2));
	gx2WriteGather_submitU32AsBE(mmVGT_STRMOUT_BUFFER_EN-0xA000);
	gx2WriteGather_submitU32AsBE(_swapEndianU32(geometryShader->regs[18]));

	MPTR copyShaderProgramAddr;
	uint32 copyShaderProgramSize;
	if( _swapEndianU32(geometryShader->copyShaderPtr) != MPTR_NULL )
	{
		copyShaderProgramAddr = _swapEndianU32(geometryShader->copyShaderPtr);
		copyShaderProgramSize = _swapEndianU32(geometryShader->copyShaderSize);
	}
	else
	{
		copyShaderProgramAddr = geometryShader->rBufferCopyProgram.GetVirtualAddr();
		copyShaderProgramSize = geometryShader->rBufferCopyProgram.GetSize();
	}

	cemu_assert_debug((copyShaderProgramAddr>>8) != 0);
	cemu_assert_debug((copyShaderProgramSize>>3) != 0);

	gx2WriteGather_submitU32AsBE(pm4HeaderType3(IT_SET_CONTEXT_REG, 6));
	gx2WriteGather_submitU32AsBE(mmSQ_PGM_START_VS-0xA000);
	gx2WriteGather_submitU32AsBE(memory_virtualToPhysical(copyShaderProgramAddr)>>8);
	gx2WriteGather_submitU32AsBE(copyShaderProgramSize>>3);
	gx2WriteGather_submitU32AsBE(0x100000);
	gx2WriteGather_submitU32AsBE(0x100000);
	gx2WriteGather_submitU32AsBE(_swapEndianU32(geometryShader->regs[4]));

	gx2WriteGather_submitU32AsBE(pm4HeaderType3(IT_SET_CONTEXT_REG, 2));
	gx2WriteGather_submitU32AsBE(mmPA_CL_VS_OUT_CNTL-0xA000);
	gx2WriteGather_submitU32AsBE(_swapEndianU32(geometryShader->regs[3]));

	if( numOutputIds != 0 )
	{
		gx2WriteGather_submitU32AsBE(pm4HeaderType3(IT_SET_CONTEXT_REG, 1+numOutputIds));
		gx2WriteGather_submitU32AsBE(mmSPI_VS_OUT_ID_0-0xA000);
		for(uint32 i=0; i<numOutputIds; i++)
		{
			gx2WriteGather_submitU32AsBE(_swapEndianU32(geometryShader->regs[8+i]));
		}
	}

	gx2WriteGather_submitU32AsBE(pm4HeaderType3(IT_SET_CONTEXT_REG, 2));
	gx2WriteGather_submitU32AsBE(mmSPI_VS_OUT_CONFIG-0xA000);
	gx2WriteGather_submitU32AsBE(_swapEndianU32(geometryShader->regs[6]));

	gx2WriteGather_submitU32AsBE(pm4HeaderType3(IT_SET_CONTEXT_REG, 2));
	gx2WriteGather_submitU32AsBE(mmSQ_GSVS_RING_ITEMSIZE-0xA000);
	gx2WriteGather_submitU32AsBE(_swapEndianU32(geometryShader->ringItemsize)&0x7FFF);

	osLib_returnFromFunction(hCPU, 0);
}

struct GX2ComputeShader
{
	 uint32be regs[12];
	 uint32be programSize;
	 uint32be programPtr;
	 uint32be ukn38;
	 uint32be ukn3C;
	 uint32be ukn40[8];
	 uint32be workgroupSizeX;
	 uint32be workgroupSizeY;
	 uint32be workgroupSizeZ;
	 uint32be workgroupSizeSpecial;
	 uint32be ukn70;
	 GX2RBuffer rBuffer;
};

static_assert(offsetof(GX2ComputeShader, programSize) == 0x30);
static_assert(offsetof(GX2ComputeShader, workgroupSizeX) == 0x60);
static_assert(offsetof(GX2ComputeShader, rBuffer) == 0x74);

void gx2Export_GX2SetComputeShader(PPCInterpreter_t* hCPU)
{
	ppcDefineParamTypePtr(computeShader, GX2ComputeShader, 0);
	cemuLog_log(LogType::GX2, "GX2SetComputeShader(0x{:08x})", hCPU->gpr[3]);

	MPTR shaderPtr;
	uint32 shaderSize;
	if (computeShader->programPtr)
	{
		shaderPtr = computeShader->programPtr;
		shaderSize = computeShader->programSize;
	}
	else
	{
		shaderPtr = computeShader->rBuffer.GetVirtualAddr();
		shaderSize = computeShader->rBuffer.GetSize();
	}
	GX2::GX2ReserveCmdSpace(0x11);

	gx2WriteGather_submit(pm4HeaderType3(IT_SET_CONTEXT_REG, 6),
		mmSQ_PGM_START_ES-0xA000,
		memory_virtualToPhysical(shaderPtr) >> 8,
		shaderSize >> 3,
		0x100000,
		0x100000,
		computeShader->regs[0]);

	osLib_returnFromFunction(hCPU, 0);
}

void _GX2SubmitUniformBlock(uint32 registerBase, uint32 index, MPTR virtualAddress, uint32 size)
{
	GX2::GX2ReserveCmdSpace(9);
	gx2WriteGather_submit(pm4HeaderType3(IT_SET_RESOURCE, 8),
		registerBase + index * 7,
		memory_virtualToPhysical(virtualAddress),
		size - 1,
		0,
		1,
		0,
		0,
		0xC0000000);
}

void gx2Export_GX2SetVertexUniformBlock(PPCInterpreter_t* hCPU)
{
	cemuLog_log(LogType::GX2, "GX2SetVertexUniformBlock(0x{:08x},0x{:x},0x{:08x})", hCPU->gpr[3], hCPU->gpr[4], hCPU->gpr[5]);
	_GX2SubmitUniformBlock(mmSQ_VTX_UNIFORM_BLOCK_START - mmSQ_TEX_RESOURCE_WORD0, hCPU->gpr[3], hCPU->gpr[5], hCPU->gpr[4]);
	osLib_returnFromFunction(hCPU, 0);
}

void gx2Export_GX2SetPixelUniformBlock(PPCInterpreter_t* hCPU)
{
	cemuLog_log(LogType::GX2, "GX2SetPixelUniformBlock(0x{:08x},0x{:x},0x{:08x})", hCPU->gpr[3], hCPU->gpr[4], hCPU->gpr[5]);
	_GX2SubmitUniformBlock(mmSQ_PS_UNIFORM_BLOCK_START - mmSQ_TEX_RESOURCE_WORD0, hCPU->gpr[3], hCPU->gpr[5], hCPU->gpr[4]);
	osLib_returnFromFunction(hCPU, 0);
}

void gx2Export_GX2SetGeometryUniformBlock(PPCInterpreter_t* hCPU)
{
	cemuLog_log(LogType::GX2, "GX2SetGeometryUniformBlock(0x{:08x},0x{:x},0x{:08x})", hCPU->gpr[3], hCPU->gpr[4], hCPU->gpr[5]);
	_GX2SubmitUniformBlock(mmSQ_GS_UNIFORM_BLOCK_START - mmSQ_TEX_RESOURCE_WORD0, hCPU->gpr[3], hCPU->gpr[5], hCPU->gpr[4]);
	osLib_returnFromFunction(hCPU, 0);
}

void gx2Export_GX2RSetVertexUniformBlock(PPCInterpreter_t* hCPU)
{
	GX2::GX2ReserveCmdSpace(9);

	GX2RBuffer* bufferPtr = (GX2RBuffer*)memory_getPointerFromVirtualOffset(hCPU->gpr[3]);
	uint32 index = hCPU->gpr[4];
	uint32 offset = hCPU->gpr[5];

	_GX2SubmitUniformBlock(mmSQ_VTX_UNIFORM_BLOCK_START - mmSQ_TEX_RESOURCE_WORD0, index, bufferPtr->GetVirtualAddr() + offset, bufferPtr->GetSize() - offset);

	osLib_returnFromFunction(hCPU, 0);
}

void gx2Export_GX2SetShaderModeEx(PPCInterpreter_t* hCPU)
{
	GX2::GX2ReserveCmdSpace(8+4);
	uint32 mode = hCPU->gpr[3];

	uint32 sqConfig = hCPU->gpr[3] == 0 ? 4 : 0;
	if (mode == GX2_SHADER_MODE_COMPUTE_SHADER)
		sqConfig |= 0xE4000000;

	gx2WriteGather_submit((uint32)(pm4HeaderType3(IT_SET_CONFIG_REG, 7)),
			(uint32)(mmSQ_CONFIG - 0x2000),
			sqConfig,
			0,
			0,
			0,
			0,
			0
		);

	if( mode != GX2_SHADER_MODE_GEOMETRY_SHADER )
	{
		gx2WriteGather_submitU32AsBE(pm4HeaderType3(IT_SET_CONTEXT_REG, 2));
		gx2WriteGather_submitU32AsBE(Latte::REGADDR::VGT_GS_MODE-0xA000);
		if (mode == GX2_SHADER_MODE_COMPUTE_SHADER)
			gx2WriteGather_submitU32AsBE(Latte::LATTE_VGT_GS_MODE().set_MODE(Latte::LATTE_VGT_GS_MODE::E_MODE::SCENARIO_G).set_COMPUTE_MODE(Latte::LATTE_VGT_GS_MODE::E_COMPUTE_MODE::ON).set_PARTIAL_THD_AT_EOI(true).getRawValueBE());
		else
			gx2WriteGather_submitU32AsBE(_swapEndianU32(0));
	}

	osLib_returnFromFunction(hCPU, 0);
}

void gx2Export_GX2CalcGeometryShaderInputRingBufferSize(PPCInterpreter_t* hCPU)
{
	uint32 size = (hCPU->gpr[3]*4) * 0x1000;
	osLib_returnFromFunction(hCPU, size);
}

void gx2Export_GX2CalcGeometryShaderOutputRingBufferSize(PPCInterpreter_t* hCPU)
{
	uint32 size = (hCPU->gpr[3]*4) * 0x1000;
	osLib_returnFromFunction(hCPU, size);
}

#pragma once
#include "Cafe/HW/Latte/ISA/LatteReg.h"
#include "GX2_Streamout.h"

struct GX2FetchShader
{
	enum class FetchShaderType : uint32
	{
		NO_TESSELATION = 0,
	};

	 betype<FetchShaderType> fetchShaderType;
	 betype<Latte::LATTE_SQ_PGM_RESOURCES_FS> reg_SQ_PGM_RESOURCES_FS;
	 uint32 shaderSize;
	 MPTR shaderPtr;
	 uint32 attribCount;
	 uint32 divisorCount;
	 uint32be divisors[2];

	MPTR GetProgramAddr() const
	{
		return _swapEndianU32(shaderPtr);
	}
};

static_assert(sizeof(GX2FetchShader) == 0x20);
static_assert(sizeof(betype<GX2FetchShader::FetchShaderType>) == 4);

namespace GX2
{

	void GX2ShaderInit();
}

#define GX2_SHADER_MODE_UNIFORM_REGISTER	0
#define GX2_SHADER_MODE_UNIFORM_BLOCK		1
#define GX2_SHADER_MODE_GEOMETRY_SHADER		2
#define GX2_SHADER_MODE_COMPUTE_SHADER		3

enum class GX2_SHADER_MODE : uint32
{
	UNIFORM_REGISTER = 0,
	UNIFORM_BLOCK = 1,
	GEOMETRY_SHADER = 2,
	COMPUTE_SHADER = 3,
};

struct GX2VertexShader
{
	struct
	{
		 betype<Latte::LATTE_SQ_PGM_RESOURCES_VS> SQ_PGM_RESOURCES_VS;
		 betype<Latte::LATTE_VGT_PRIMITIVEID_EN> VGT_PRIMITIVEID_EN;
		 betype<Latte::LATTE_SPI_VS_OUT_CONFIG> SPI_VS_OUT_CONFIG;
		 uint32be vsOutIdTableSize;
		 betype<Latte::LATTE_SPI_VS_OUT_ID_N> LATTE_SPI_VS_OUT_ID_N[10];
		 betype<Latte::LATTE_PA_CL_VS_OUT_CNTL> PA_CL_VS_OUT_CNTL;
		 uint32be uknReg15;
		 uint32be semanticTableSize;
		 betype<Latte::LATTE_SQ_VTX_SEMANTIC_X> SQ_VTX_SEMANTIC_N[32];
		 uint32be uknReg49;
		 uint32be uknReg50;
		 uint32be uknReg51;
	}regs;
	 uint32be shaderSize;
	 MEMPTR<void> shaderPtr;
	 betype<GX2_SHADER_MODE> shaderMode;
	 uint32 uniformBlockCount;
	 MPTR uniformBlockInfo;
	 uint32 uniformVarCount;
	 MPTR uniformVarInfo;
	 uint32 uknEC;
	 MPTR uknF0;
	 uint32 uknF4;
	 MPTR uknF8;
	 uint32	samplerCount;
	 MPTR samplerInfo;
	 uint32 attribCount;
	 MPTR attribInfo;
	 uint32be ringItemsize;
	 uint32be usesStreamOut;
	 uint32be streamOutVertexStride[GX2_MAX_STREAMOUT_BUFFERS];
	 GX2RBuffer rBuffer;

	MPTR GetProgramAddr() const
	{
		if (this->shaderPtr)
			return this->shaderPtr.GetMPTR();
		return this->rBuffer.GetVirtualAddr();
	}
};

static_assert(sizeof(GX2VertexShader) == 0x134);

typedef struct _GX2PixelShader
{
	uint32  regs[41];

	 uint32 shaderSize;
	 MPTR shaderPtr;
	 uint32 shaderMode;
	 uint32 uniformBlockCount;
	 MPTR uniformBlockInfo;
	 uint32 uniformVarCount;
	 MPTR uniformVarInfo;
	 uint32 uknC0;
	 MPTR uknC4;
	 uint32 uknC8;
	 MPTR uknCC;
	 uint32 samplerCount;
	 MPTR samplerInfo;
	 GX2RBuffer rBuffer;

	MPTR GetProgramAddr() const
	{
		if (_swapEndianU32(shaderPtr) != MPTR_NULL)
			return _swapEndianU32(shaderPtr);
		return rBuffer.GetVirtualAddr();
	}
}GX2PixelShader_t;

static_assert(sizeof(GX2PixelShader_t) == 0xE8);

struct GX2GeometryShader_t
{
	union
	{
		 uint32 regs[19];
		struct
		{
			uint32be reg0;
			uint32be reg1;
			uint32be VGT_GS_MODE;
			uint32be reg3;
			uint32be reg4;
			uint32be reg5;
			uint32be reg6;
			uint32be reg7;
		}reg;
	};
	 uint32 shaderSize;
	 MPTR shaderPtr;
	 uint32 copyShaderSize;
	 MPTR copyShaderPtr;
	 uint32 shaderMode;
	 uint32 uniformBlockCount;
	 MPTR uniformBlockInfo;
	 uint32 uniformVarCount;
	 MPTR uniformVarInfo;
	 uint32 ukn70;
	 MPTR ukn74;
	 uint32 ukn78;
	 MPTR ukn7C;
	 uint32 samplerCount;
	 MPTR samplerInfo;
	 uint32 ringItemsize;
	 uint32 useStreamout;
	 uint32 streamoutStride[GX2_MAX_STREAMOUT_BUFFERS];
	 GX2RBuffer rBuffer;
	 GX2RBuffer rBufferCopyProgram;

	MPTR GetGeometryProgramAddr() const
	{
		if (_swapEndianU32(shaderPtr) != MPTR_NULL)
			return _swapEndianU32(shaderPtr);
		return rBuffer.GetVirtualAddr();
	}

	MPTR GetCopyProgramAddr() const
	{
		if (_swapEndianU32(copyShaderPtr) != MPTR_NULL)
			return _swapEndianU32(copyShaderPtr);
		return rBufferCopyProgram.GetVirtualAddr();
	}

};

static_assert(sizeof(GX2GeometryShader_t) == 0xC0);

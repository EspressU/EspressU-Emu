#pragma once
#include "Cafe/HW/Latte/Core/LatteConst.h"
#include "Cafe/HW/Latte/Renderer/RendererShader.h"
#include <boost/container/static_vector.hpp>

namespace LatteDecompiler
{
	enum class DataType
	{
		UNDEFINED = 0,
		U32 = 0,
		S32 = 0,
		FLOAT = 0
	};
};

typedef struct
{
	bool	isRegister;
	uint8	kcacheBankId;
	uint32	index;
	uint32	mappedIndex;
}LatteDecompilerRemappedUniformEntry_t;

typedef struct
{
	uint32	indexOffset;
	uint32	mappedIndexOffset;
}LatteFastAccessRemappedUniformEntry_register_t;

typedef struct
{
	uint16	indexOffset;
	uint16	mappedIndexOffset;
}LatteFastAccessRemappedUniformEntry_buffer_t;

typedef struct
{
	uint32 texUnit;
	sint32 uniformLocation;
	float currentValue[2];
}LatteUniformTextureScaleEntry_t;

struct LatteDecompilerShaderResourceMapping
{
	LatteDecompilerShaderResourceMapping()
	{
		std::fill(textureUnitToBindingPoint, textureUnitToBindingPoint + LATTE_NUM_MAX_TEX_UNITS, -1);
		std::fill(textureUnitToSamplerBindingPoint, textureUnitToSamplerBindingPoint + LATTE_NUM_MAX_TEX_UNITS, -1);
		std::fill(uniformBuffersBindingPoint, uniformBuffersBindingPoint + LATTE_NUM_MAX_UNIFORM_BUFFERS, -1);
		std::fill(attributeMapping, attributeMapping + LATTE_NUM_MAX_ATTRIBUTE_LOCATIONS, -1);
	}
	static const sint8 UNUSED_BINDING = -1;
	sint8 setIndex{};
	sint8 textureUnitToBindingPoint[LATTE_NUM_MAX_TEX_UNITS];
	sint8 textureUnitToSamplerBindingPoint[LATTE_NUM_MAX_TEX_UNITS];
	sint8 uniformVarsBufferBindingPoint{-1};
	sint8 uniformBuffersBindingPoint[LATTE_NUM_MAX_UNIFORM_BUFFERS];
	sint8 tfStorageBindingPoint{-1};
	sint8 attributeMapping[LATTE_NUM_MAX_ATTRIBUTE_LOCATIONS];
	sint8 argumentBufferBindingPoint{-1};
	sint8 verticesPerInstanceBinding{-1};
	sint8 indexBufferBinding{-1};
	sint8 indexTypeBinding{-1};

	sint32 getTextureCount()
	{
		sint32 count = 0;
		for (sint32 i = 0; i < LATTE_NUM_MAX_TEX_UNITS; i++)
		{
			if (textureUnitToBindingPoint[i] >= 0)
				count++;
		}
		return count;
	}

	sint32 getTextureUnitFromBindingPoint(sint8 bindingPoint)
	{
		for (sint32 i = 0; i < LATTE_NUM_MAX_TEX_UNITS; i++)
		{
			if (textureUnitToBindingPoint[i] == bindingPoint)
				return i;
		}

		cemu_assert_debug(false);
		return -1;
	}

	sint32 getTextureBaseBindingPoint()
	{
		sint32 bindingPoint = 9999;
		for (sint32 i = 0; i < LATTE_NUM_MAX_TEX_UNITS; i++)
		{
			if (textureUnitToBindingPoint[i] >= 0)
				bindingPoint = std::min(bindingPoint, (sint32)textureUnitToBindingPoint[i]);
		}
		if (bindingPoint == 9999)
			return -1;
		return bindingPoint;
	}

	bool getUniformBufferBindingRange(sint32& minBinding, sint32& maxBinding)
	{
		sint32 minBindingPoint = 9999;
		sint32 maxBindingPoint = -9999;
		if (uniformVarsBufferBindingPoint >= 0)
		{
			minBindingPoint = std::min(minBindingPoint, (sint32)uniformVarsBufferBindingPoint);
			maxBindingPoint = std::max(maxBindingPoint, (sint32)uniformVarsBufferBindingPoint);
		}
		for (sint32 i = 0; i < LATTE_NUM_MAX_UNIFORM_BUFFERS; i++)
		{
			if (uniformBuffersBindingPoint[i] >= 0)
			{
				minBindingPoint = std::min(minBindingPoint, (sint32)uniformBuffersBindingPoint[i]);
				maxBindingPoint = std::max(maxBindingPoint, (sint32)uniformBuffersBindingPoint[i]);
			}
		}
		if (minBindingPoint == 9999)
			return false;
		minBinding = minBindingPoint;
		maxBinding = maxBindingPoint;
		return true;
	}

	sint32 getTFStorageBufferBindingPoint()
	{
		return tfStorageBindingPoint;
	}

	bool hasUniformBuffers()
	{
		for (sint32 i = 0; i < LATTE_NUM_MAX_UNIFORM_BUFFERS; i++)
		{
			if (uniformBuffersBindingPoint[i] >= 0)
				return true;
		}
		return false;
	}

	sint32 getAttribHostShaderIndex(uint32 semanticId)
	{
		cemu_assert_debug(semanticId < 0x100);
		return attributeMapping[semanticId];
	}
};

struct LatteDecompilerShader
{
	LatteDecompilerShader(LatteConst::ShaderType shaderType) : shaderType(shaderType) {}

	LatteDecompilerShader* next{nullptr};
	LatteConst::ShaderType shaderType;
	uint64 baseHash{0};
	uint64 auxHash{0};
	struct LatteFetchShader* compatibleFetchShader{};
	bool hasError{false};
	struct QuickBufferEntry
	{
		uint32 index : 8;
		uint32 size : 24;
	};
	boost::container::static_vector<QuickBufferEntry, LATTE_NUM_MAX_UNIFORM_BUFFERS> list_quickBufferList;
	uint8 textureUnitList[LATTE_NUM_MAX_TEX_UNITS];
	uint8 textureUnitListCount{ 0 };
	Latte::E_DIM textureUnitDim[LATTE_NUM_MAX_TEX_UNITS]{};
	bool textureIsIntegerFormat[LATTE_NUM_MAX_TEX_UNITS]{};
	uint8 uniformMode{0};
	uint64 uniformDataHash64[2]{0};
	std::vector<LatteDecompilerRemappedUniformEntry_t> list_remappedUniformEntries;
	std::bitset<LATTE_NUM_MAX_TEX_UNITS> textureUnitMask2;
	uint16 textureUnitSamplerAssignment[LATTE_NUM_MAX_TEX_UNITS]{ 0 };
	bool textureUsesDepthCompare[LATTE_NUM_MAX_TEX_UNITS]{};
	uint8 textureRenderTargetIndex[LATTE_NUM_MAX_TEX_UNITS];

	uint32 pixelColorOutputMask{ 0 };
	bool depthMask{ false };
	uint32 ringParameterCount{ 0 };
	uint32 ringParameterCountFromPrevStage{ 0 };
	std::bitset<LATTE_NUM_STREAMOUT_BUFFER> streamoutBufferWriteMask;
	bool hasStreamoutBufferWrite{ false };
	class StringBuf* strBuf_shaderSource{ nullptr };
	RendererShader* shader{ nullptr };
	bool isCustomShader{ false };

	uint32 outputParameterMask{ 0 };
	LatteDecompilerShaderResourceMapping resourceMapping{};
	struct
	{
		sint32 loc_remapped;
		sint32 loc_uniformRegister;
		sint32 count_uniformRegister;
		sint32 loc_windowSpaceToClipSpaceTransform;
		sint32 loc_alphaTestRef;
		sint32 loc_pointSize;
		sint32 loc_fragCoordScale;
		sint32 loc_baseVertex;
		sint32 loc_baseInstance;
		std::vector<LatteUniformTextureScaleEntry_t> list_ufTexRescale;
		float ufCurrentValueAlphaTestRef;
		float ufCurrentValueFragCoordScale[2];
		sint32 loc_verticesPerInstance;
		sint32 loc_streamoutBufferBase[LATTE_NUM_STREAMOUT_BUFFER];
		sint32 loc_streamoutBufferSize[LATTE_NUM_STREAMOUT_BUFFER];
		uint32 uniformRangeSize;
	}uniform{ 0 };
	struct _RemappedUniformBufferGroup
	{
		_RemappedUniformBufferGroup(uint32 _kcacheBankIdOffset) : kcacheBankIdOffset(_kcacheBankIdOffset) {};

		uint32 kcacheBankIdOffset;
		std::vector<LatteFastAccessRemappedUniformEntry_buffer_t> entries;
	};
	std::vector<LatteFastAccessRemappedUniformEntry_register_t>	list_remappedUniformEntries_register;
	std::vector<_RemappedUniformBufferGroup> list_remappedUniformEntries_bufferGroups;
};

struct LatteDecompilerOutputUniformOffsets
{
	sint32 offset_remapped;
	sint32 offset_uniformRegister;
	sint32 count_uniformRegister;
	sint32 offset_alphaTestRef;
	sint32 offset_pointSize;
	sint32 offset_fragCoordScale;
	sint32 offset_windowSpaceToClipSpaceTransform;
	sint32 offset_texScale[LATTE_NUM_MAX_TEX_UNITS];
	sint32 offset_baseVertex{-1};
	sint32 offset_baseInstance{-1};
	sint32 offset_verticesPerInstance{-1};
	sint32 offset_streamoutBufferBase[LATTE_NUM_STREAMOUT_BUFFER]{ -1, -1, -1, -1 };
	sint32 offset_streamoutBufferSize[LATTE_NUM_STREAMOUT_BUFFER]{ -1, -1, -1, -1 };
	sint32 offset_endOfBlock;

	LatteDecompilerOutputUniformOffsets()
	{
		offset_remapped = -1;
		offset_uniformRegister = -1;
		count_uniformRegister = 0;
		offset_alphaTestRef = -1;
		offset_pointSize = -1;
		offset_fragCoordScale = -1;
		offset_windowSpaceToClipSpaceTransform = -1;
		for (sint32 i = 0; i < LATTE_NUM_MAX_TEX_UNITS; i++)
			offset_texScale[i] = -1;
		offset_endOfBlock = 0;
	}
};

struct LatteDecompilerOptions
{
	bool usesGeometryShader{ false };
	bool strictMul{};
	bool useTFViaSSBO{ false };
};

struct LatteDecompilerOutput_t
{
	LatteDecompilerShader* shader;
	LatteConst::ShaderType shaderType;

	std::bitset<LATTE_NUM_MAX_TEX_UNITS> textureUnitMask;

	std::bitset<LATTE_NUM_STREAMOUT_BUFFER> streamoutBufferWriteMask;
	uint32 streamoutBufferStride[LATTE_NUM_STREAMOUT_BUFFER]{};

	LatteDecompilerOutputUniformOffsets uniformOffsetsGL;
	LatteDecompilerOutputUniformOffsets uniformOffsetsVK;
	LatteDecompilerShaderResourceMapping resourceMappingGL;
	LatteDecompilerShaderResourceMapping resourceMappingVK;
	LatteDecompilerShaderResourceMapping resourceMappingMTL;
};

struct LatteDecompilerSubroutineInfo;

void LatteDecompiler_DecompileVertexShader(uint64 shaderBaseHash, uint32* contextRegisters, uint8* programData, uint32 programSize, struct LatteFetchShader* fetchShader, LatteDecompilerOptions& options, LatteDecompilerOutput_t* output);
void LatteDecompiler_DecompileGeometryShader(uint64 shaderBaseHash, uint32* contextRegisters, uint8* programData, uint32 programSize, uint8* gsCopyProgramData, uint32 gsCopyProgramSize, uint32 vsRingParameterCount, LatteDecompilerOptions& options, LatteDecompilerOutput_t* output);
void LatteDecompiler_DecompilePixelShader(uint64 shaderBaseHash, uint32* contextRegisters, uint8* programData, uint32 programSize, LatteDecompilerOptions& options, LatteDecompilerOutput_t* output);

#define GPU7_COPY_SHADER_MAX_PARAMS	(32)

struct LatteGSCopyShaderStreamWrite_t
{
	uint8 bufferIndex;
	uint16 offset;
	uint32 exportArrayBase;
	uint32 memWriteArraySize;
	uint32 memWriteCompMask;
};

struct LatteParsedGSCopyShader
{
	struct
	{
		uint16 offset;
		uint16 gprIndex;
		uint8  exportType;
		uint8  exportParam;
	}paramMapping[GPU7_COPY_SHADER_MAX_PARAMS];
	sint32 numParam;
	std::vector<LatteGSCopyShaderStreamWrite_t> list_streamWrites;
};

LatteParsedGSCopyShader* LatteGSCopyShaderParser_parse(uint8* programData, uint32 programSize);
bool LatteGSCopyShaderParser_getExportTypeByOffset(LatteParsedGSCopyShader* shaderContext, uint32 offset, uint32* exportType, uint32* exportParam);

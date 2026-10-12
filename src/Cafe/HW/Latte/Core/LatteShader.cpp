#include "Cafe/HW/Latte/Core/LatteConst.h"
#include "Cafe/HW/Latte/Core/LatteShaderAssembly.h"
#include "Cafe/HW/Latte/ISA/RegDefines.h"
#include "Cafe/HW/Latte/ISA/LatteReg.h"
#include "Cafe/HW/Latte/Core/LatteShader.h"
#include "Cafe/HW/Latte/Core/LatteCachedFBO.h"
#include "Cafe/HW/Latte/LegacyShaderDecompiler/LatteDecompiler.h"
#include "Cafe/HW/Latte/Core/FetchShader.h"
#include "Cafe/HW/Latte/Core/LattePerformanceMonitor.h"
#ifdef ENABLE_VULKAN
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanRenderer.h"
#endif
#include "Cafe/GraphicPack/GraphicPack2.h"
#include "HW/Latte/Core/Latte.h"
#include "HW/Latte/Renderer/Renderer.h"
#include "config/ActiveSettings.h"
#include "Cafe/GameProfile/GameProfile.h"
#include "util/containers/flat_hash_map.hpp"
#ifdef ENABLE_METAL
#include "Cafe/HW/Latte/Renderer/Metal/LatteToMtl.h"
#endif
#include <cinttypes>

#include "util/Zir/EmitterGLSL/ZpIREmitGLSL.h"
#include "util/Zir/Core/ZpIRDebug.h"
#include "Cafe/HW/Latte/Transcompiler/LatteTC.h"
#include "Cafe/HW/Latte/ShaderInfo/ShaderInfo.h"

struct _ShaderHashCache
{
	uint64 prevHash1;
	uint64 prevHash2;
	uint32* prevProgramCode;
	uint32 prevProgramSize;
};

_ShaderHashCache hashCacheVS = { 0 };
_ShaderHashCache hashCacheGS = { 0 };
_ShaderHashCache hashCacheGSCopy = { 0 };
_ShaderHashCache hashCachePS = { 0 };

LatteFetchShader* _activeFetchShader = nullptr;
LatteDecompilerShader* _activeVertexShader = nullptr;
LatteDecompilerShader* _activeGeometryShader = nullptr;
LatteDecompilerShader* _activePixelShader = nullptr;

using SHRC_CACHE_TYPE = ska::flat_hash_map<uint64, LatteDecompilerShader*>;

SHRC_CACHE_TYPE sVertexShaders(512);
SHRC_CACHE_TYPE sGeometryShaders(512);
SHRC_CACHE_TYPE sPixelShaders(512);

uint64 _shaderBaseHash_vs;
uint64 _shaderBaseHash_gs;
uint64 _shaderBaseHash_ps;

std::atomic_int g_compiled_shaders_total = 0;
std::atomic_int g_compiled_shaders_async = 0;

LatteFetchShader* LatteSHRC_GetActiveFetchShader()
{
	return _activeFetchShader;
}

LatteDecompilerShader* LatteSHRC_GetActiveVertexShader()
{
	return _activeVertexShader;
}

LatteDecompilerShader* LatteSHRC_GetActiveGeometryShader()
{
	return _activeGeometryShader;
}

LatteDecompilerShader* LatteSHRC_GetActivePixelShader()
{
	return _activePixelShader;
}

inline ska::flat_hash_map<uint64, LatteDecompilerShader*>& LatteSHRC_GetCacheByType(LatteConst::ShaderType shaderType)
{
	if (shaderType == LatteConst::ShaderType::Vertex)
		return sVertexShaders;
	else if (shaderType == LatteConst::ShaderType::Geometry)
		return sGeometryShaders;
	cemu_assert_debug(shaderType == LatteConst::ShaderType::Pixel);
	return sPixelShaders;
}

void _calcShaderHashGeneric(uint32* programCode, uint32 programSize, uint64& outputHash1, uint64& outputHash2)
{
	outputHash1 = 0;
	outputHash2 = 0;
	for (uint32 i = 0; i < programSize / 4; i++)
	{
		uint32 temp = programCode[i];
		outputHash1 += (uint64)temp;
		outputHash2 ^= (uint64)temp;
		outputHash1 = (outputHash1 << 3) | (outputHash1 >> 61);
		outputHash2 = (outputHash2 >> 7) | (outputHash2 << 57);
	}
}

void _calculateShaderProgramHash(uint32* programCode, uint32 programSize, _ShaderHashCache* hashCache, uint64* outputHash1, uint64* outputHash2)
{
	uint64 progHash1 = 0;
	uint64 progHash2 = 0;
	if (!programCode)
	{
		hashCache->prevProgramCode = NULL;
		hashCache->prevProgramSize = 0;
		hashCache->prevHash1 = 0;
		hashCache->prevHash2 = 0;
	}
	else if (hashCache->prevProgramCode != programCode || hashCache->prevProgramSize != programSize)
	{
		_calcShaderHashGeneric(programCode, programSize, progHash1, progHash2);
		hashCache->prevProgramCode = programCode;
		hashCache->prevProgramSize = programSize;
		hashCache->prevHash1 = progHash1;
		hashCache->prevHash2 = progHash2;
	}
	else
	{
		progHash1 = hashCache->prevHash1;
		progHash2 = hashCache->prevHash2;
	}
	*outputHash1 = progHash1;
	*outputHash2 = progHash2;
}

void LatteSHRC_ResetCachedShaderHash()
{
	hashCacheVS.prevProgramCode = 0;
	hashCacheVS.prevProgramSize = 0;
	hashCacheGS.prevProgramCode = 0;
	hashCacheGS.prevProgramSize = 0;
	hashCacheGSCopy.prevProgramCode = 0;
	hashCacheGSCopy.prevProgramSize = 0;
	hashCachePS.prevProgramCode = 0;
	hashCachePS.prevProgramSize = 0;
}

LatteShaderPSInputTable _activePSImportTable;

LatteShaderPSInputTable* LatteSHRC_GetPSInputTable()
{
	return &_activePSImportTable;
}

void LatteSHRC_RemoveFromCache(LatteDecompilerShader* shader)
{
	bool removed = false;
	auto& cache = LatteSHRC_GetCacheByType(shader->shaderType);
	auto baseIt = cache.find(shader->baseHash);
	if (baseIt == cache.end())
	{
		cemu_assert_suspicious();
	}
	else if (baseIt->second == shader)
	{
        cemu_assert_debug(baseIt->second == shader);
        cache.erase(baseIt);
		if (shader->next)
        {
            cemu_assert_debug(shader->baseHash == shader->next->baseHash);
            cache.emplace(shader->baseHash, shader->next);
        }
        shader->next = 0;
		removed = true;
	}
	else
	{
		LatteDecompilerShader* shaderChain = baseIt->second;
		while (shaderChain->next)
		{
			if (shaderChain->next == shader)
			{
				shaderChain->next = shaderChain->next->next;
				removed = true;
				break;
			}
			shaderChain = shaderChain->next;
		}
	}
	cemu_assert(removed);
}

void LatteSHRC_RemoveFromCacheByHash(uint64 shader_base_hash, uint64 shader_aux_hash, LatteConst::ShaderType type)
{
	LatteDecompilerShader* shader = nullptr;
	if (type == LatteConst::ShaderType::Vertex)
		shader = LatteSHRC_FindVertexShader(shader_base_hash, shader_aux_hash);
	else if (type == LatteConst::ShaderType::Geometry)
		shader = LatteSHRC_FindGeometryShader(shader_base_hash, shader_aux_hash);
	else if (type == LatteConst::ShaderType::Pixel)
		shader = LatteSHRC_FindPixelShader(shader_base_hash, shader_aux_hash);
	if (shader)
		LatteSHRC_RemoveFromCache(shader);
}

void LatteShader_free(LatteDecompilerShader* shader)
{
	LatteSHRC_RemoveFromCache(shader);
	if (shader->shader)
		delete shader->shader;
	shader->shader = nullptr;
	delete shader;
}

void LatteShader_CreatePSInputTable(LatteShaderPSInputTable* psInputTable, uint32* contextRegisters)
{
	uint32 psControl0 = contextRegisters[mmSPI_PS_IN_CONTROL_0];
	uint32 spi0_positionEnable = (psControl0 >> 8) & 1;
	uint32 spi0_positionCentroid = (psControl0 >> 9) & 1;
	cemu_assert_debug(spi0_positionCentroid == 0);
	uint32 spi0_positionAddr = (psControl0 >> 10) & 0x1F;
	uint32 spi0_paramGen = (psControl0 >> 15) & 0xF;
	uint32 spi0_paramGenAddr = (psControl0 >> 19) & 0x7F;
	sint32 importIndex = 0;

	uint32 numPSInputs = contextRegisters[mmSPI_PS_IN_CONTROL_0] & 0x3F;
	uint64 key = 0;

	if (spi0_positionEnable)
	{
		key += (uint64)spi0_positionAddr + 1;
	}

	if (spi0_paramGen != 0)
	{
		key += std::rotr<uint64>(spi0_paramGen, 7);
		key += std::rotr<uint64>(spi0_paramGenAddr, 3);
		psInputTable->paramGen = spi0_paramGen;
		psInputTable->paramGenGPR = spi0_paramGenAddr;
	}
	else
	{
		psInputTable->paramGen = 0;
	}

#ifdef CEMU_DEBUG_ASSERT
	uint8 semanticMask[256 / 8] = { 0 };
#endif
	cemu_assert_debug(numPSInputs <= GPU7_PS_MAX_INPUTS);
	numPSInputs = std::min<uint32>(numPSInputs, GPU7_PS_MAX_INPUTS);

	for (uint32 f = 0; f < numPSInputs; f++)
	{
		uint32 psInputControl = contextRegisters[mmSPI_PS_INPUT_CNTL_0 + f];
		uint32 psSemanticId = (psInputControl & 0xFF);

		uint8 defaultValue = (psInputControl>>8)&3;
		cemu_assert_debug(defaultValue <= 1);

		uint32 uknBits = psInputControl & ~((0xFF)|(0x3<<8) | (1 << 10) | (1 << 12));
		uknBits &= ~0x800;
		cemu_assert_debug(psSemanticId != 0xFF);

		key += (uint64)psInputControl;
		key = std::rotl<uint64>(key, 7);
		if (spi0_positionEnable && f == spi0_positionAddr)
		{
			psInputTable->import[f].semanticId = LATTE_ANALYZER_IMPORT_INDEX_SPIPOSITION;
			psInputTable->import[f].isFlat = false;
			psInputTable->import[f].isNoPerspective = false;
			key += (uint64)0x33;
		}
		else
		{
#ifdef CEMU_DEBUG_ASSERT
			if (semanticMask[psSemanticId >> 3] & (1 << (psSemanticId & 7)))
			{
				cemuLog_logDebug(LogType::Force, "SemanticId already used");
			}
			semanticMask[psSemanticId >> 3] |= (1 << (psSemanticId & 7));
#endif

			psInputTable->import[f].semanticId = psSemanticId;
			psInputTable->import[f].isFlat = (psInputControl&(1 << 10)) != 0;
			psInputTable->import[f].isNoPerspective = (psInputControl&(1 << 12)) != 0;
		}
	}
	psInputTable->key = key;
	psInputTable->count = numPSInputs;
}

void LatteShader_UpdatePSInputs(uint32* contextRegisters)
{
	LatteShader_CreatePSInputTable(&_activePSImportTable, contextRegisters);
}

void LatteShader_CreateRendererShader(LatteDecompilerShader* shader, bool compileAsync)
{
	if (shader->hasError )
	{
		cemuLog_log(LogType::Force, "Unable to compile shader {:016x}", shader->baseHash);
		return;
	}

	GraphicPack2::GP_SHADER_TYPE gpShaderType;
	RendererShader::ShaderType shaderType;
	if (shader->shaderType == LatteConst::ShaderType::Vertex)
	{
		shaderType = RendererShader::ShaderType::kVertex;
		gpShaderType = GraphicPack2::GP_SHADER_TYPE::VERTEX;
	}
	else if (shader->shaderType == LatteConst::ShaderType::Geometry)
	{
		shaderType = RendererShader::ShaderType::kGeometry;
		gpShaderType = GraphicPack2::GP_SHADER_TYPE::GEOMETRY;
	}
	else if (shader->shaderType == LatteConst::ShaderType::Pixel)
	{
		shaderType = RendererShader::ShaderType::kFragment;
		gpShaderType = GraphicPack2::GP_SHADER_TYPE::PIXEL;
	}

	std::string shaderSrc;

	const std::string* customShaderSrc = GraphicPack2::FindCustomShaderSource(shader->baseHash, shader->auxHash, gpShaderType, g_renderer->GetType() == RendererAPI::Vulkan, g_renderer->GetType() == RendererAPI::Metal);
	if (customShaderSrc)
	{
		shaderSrc.assign(*customShaderSrc);
		shader->isCustomShader = true;
	}
	else
		shaderSrc.assign(shader->strBuf_shaderSource->c_str());

	if (shaderType == RendererShader::ShaderType::kVertex &&
		(shader->baseHash == 0x15bc7edf9de2ed30 || shader->baseHash == 0x83a697d61a3b9202 ||
		 shader->baseHash == 0x97bc44a5028381c6 || shader->baseHash == 0x24838b84d15a1da1))
	{
		cemuLog_logDebug(LogType::Force, "Filtered shader to avoid AMD crash");
		shader->shader = nullptr;
		shader->hasError = true;
		return;
	}

	shader->shader = g_renderer->shader_create(shaderType, shader->baseHash, shader->auxHash, shaderSrc, true, shader->isCustomShader);
	if (shader->shader == nullptr)
		shader->hasError = true;
	LatteShader_CleanupAfterCompile(shader);
}

void LatteShader_FinishCompilation(LatteDecompilerShader* shader)
{
	if (shader->hasError)
	{
		cemuLog_logDebug(LogType::Force, "LatteShader_finishCompilation(): Skipped because of error in shader {:x}", shader->baseHash);
		return;
	}
	shader->shader->WaitForCompiled();

#ifdef ENABLE_OPENGL
	LatteShader_prepareSeparableUniforms(shader);
#endif
	LatteShader_CleanupAfterCompile(shader);
}

void LatteSHRC_RegisterShader(LatteDecompilerShader* shader, uint64 baseHash, uint64 auxHash)
{
	auto& cache = LatteSHRC_GetCacheByType(shader->shaderType);
	shader->baseHash = baseHash;
	shader->auxHash = auxHash;

	auto it = cache.find(baseHash);
	if (it == cache.end())
	{
		shader->next = nullptr;
		cache.emplace(shader->baseHash, shader);
	}
	else
	{
		shader->next = it->second->next;
		it->second->next = shader;
	}
}

LatteDecompilerShader* LatteSHRC_GetFromChain(LatteDecompilerShader* baseShader, uint64 baseHash, uint64 auxHash)
{
	while (baseShader && baseShader->auxHash != auxHash)
		baseShader = baseShader->next;
	return baseShader;
}

LatteDecompilerShader* LatteSHRC_Get(SHRC_CACHE_TYPE& cache, uint64 baseHash, uint64 auxHash)
{
	auto it = cache.find(baseHash);
	if (it == cache.end())
		return nullptr;
	LatteDecompilerShader* baseShader = it->second;
	if (!baseShader)
		return nullptr;
	while (baseShader && baseShader->auxHash != auxHash)
		baseShader = baseShader->next;
	return baseShader;
}

LatteDecompilerShader* LatteSHRC_FindVertexShader(uint64 baseHash, uint64 auxHash)
{
	return LatteSHRC_Get(sVertexShaders, baseHash, auxHash);
}

LatteDecompilerShader* LatteSHRC_FindGeometryShader(uint64 baseHash, uint64 auxHash)
{
	return LatteSHRC_Get(sGeometryShaders, baseHash, auxHash);
}

LatteDecompilerShader* LatteSHRC_FindPixelShader(uint64 baseHash, uint64 auxHash)
{
	return LatteSHRC_Get(sPixelShaders, baseHash, auxHash);
}

void LatteShaderSHRC_UpdateFetchShader()
{
	_activeFetchShader = LatteFetchShader::FindByGPUState();
}

void LatteShader_CleanupAfterCompile(LatteDecompilerShader* shader)
{
	if (shader->strBuf_shaderSource)
	{
		delete shader->strBuf_shaderSource;
		shader->strBuf_shaderSource = nullptr;
	}
}

void LatteShader_DumpShader(uint64 baseHash, uint64 auxHash, LatteDecompilerShader* shader)
{
	if (!ActiveSettings::DumpShadersEnabled())
		return;

	const char* suffix = "";
	if (shader->shaderType == LatteConst::ShaderType::Vertex)
		suffix = "vs";
	else if (shader->shaderType == LatteConst::ShaderType::Geometry)
		suffix = "gs";
	else if (shader->shaderType == LatteConst::ShaderType::Pixel)
		suffix = "ps";

	FileStream* fs = FileStream::createFile2(ActiveSettings::GetUserDataPath("dump/shaders/{:016x}_{:016x}_{}.txt", baseHash, auxHash, suffix));
	if (fs)
	{
		if (shader->strBuf_shaderSource)
			fs->writeData(shader->strBuf_shaderSource->c_str(), shader->strBuf_shaderSource->getLen());
		delete fs;
	}
}

void LatteShader_DumpRawShader(uint64 baseHash, uint64 auxHash, uint32 type, uint8* programCode, uint32 programLen)
{
	if (!ActiveSettings::DumpShadersEnabled())
		return;
	const char* suffix = "";
	if (type == SHADER_DUMP_TYPE_FETCH)
		suffix = "fs";
	else if (type == SHADER_DUMP_TYPE_VERTEX)
		suffix = "vs";
	else if (type == SHADER_DUMP_TYPE_GEOMETRY)
		suffix = "gs";
	else if (type == SHADER_DUMP_TYPE_PIXEL)
		suffix = "ps";
	else if (type == SHADER_DUMP_TYPE_COPY)
		suffix = "copy";
	else if (type == SHADER_DUMP_TYPE_COMPUTE)
		suffix = "compute";

	FileStream* fs = FileStream::createFile2(ActiveSettings::GetUserDataPath("dump/shaders/{:016x}_{:016x}_{}.bin", baseHash, auxHash, suffix));
	if (fs)
	{
		fs->writeData(programCode, programLen);
		delete fs;
	}
}

void LatteSHRC_UpdateVSBaseHash(uint8* vertexShaderPtr, uint32 vertexShaderSize, bool usesGeometryShader)
{
	uint32* vsProgramCode = (uint32*)vertexShaderPtr;
	uint64 vsHash1 = 0;
	uint64 vsHash2 = 0;
	_calculateShaderProgramHash(vsProgramCode, vertexShaderSize, &hashCacheVS, &vsHash1, &vsHash2);
	uint64 vsHash = vsHash1 + vsHash2 + _activeFetchShader->key + _activePSImportTable.key + (usesGeometryShader ? 0x1111ULL : 0ULL);

	uint32 tmp = LatteGPUState.contextNew.PA_CL_VTE_CNTL.getRawValue() ^ 0x43F;
	vsHash += tmp;

	auto primitiveType = LatteGPUState.contextNew.VGT_PRIMITIVE_TYPE.get_PRIMITIVE_MODE();
	if (primitiveType == Latte::LATTE_VGT_PRIMITIVE_TYPE::E_PRIMITIVE_TYPE::RECTS)
	{
		vsHash += 13ULL;
	}
	else if (primitiveType == Latte::LATTE_VGT_PRIMITIVE_TYPE::E_PRIMITIVE_TYPE::POINTS)
	{
		vsHash += 71ULL;
	}
	vsHash += (LatteGPUState.contextRegister[mmVGT_STRMOUT_EN] ? 21 : 0);
	if (LatteGPUState.contextNew.PA_CL_CLIP_CNTL.get_DX_CLIP_SPACE_DEF())
		vsHash += 0x1537;

#ifdef ENABLE_METAL
	if (g_renderer->GetType() == RendererAPI::Metal)
	{
	    bool isRectVertexShader = (primitiveType == Latte::LATTE_VGT_PRIMITIVE_TYPE::E_PRIMITIVE_TYPE::RECTS);
		const bool usesSSBOStreamout = g_renderer->UseTFViaSSBO() && LatteGPUState.contextRegister[mmVGT_STRMOUT_EN] != 0;

	    if (usesGeometryShader || isRectVertexShader || usesSSBOStreamout || _activeFetchShader->mtlFetchVertexManually)
		{
      		for (sint32 g = 0; g < _activeFetchShader->bufferGroups.size(); g++)
            {
           	    LatteParsedFetchShaderBufferGroup_t& group = _activeFetchShader->bufferGroups[g];
          		uint32 bufferIndex = group.attributeBufferIndex;
          		uint32 bufferBaseRegisterIndex = mmSQ_VTX_ATTRIBUTE_BLOCK_START + bufferIndex * 7;
          		uint32 bufferStride = (LatteGPUState.contextRegister[bufferBaseRegisterIndex + 2] >> 11) & 0xFFFF;

                vsHash += (uint64)bufferStride;
          		vsHash = std::rotl<uint64>(vsHash, 7);
            }
		}

	    if (!(usesGeometryShader || isRectVertexShader))
		{
      		if (LatteGPUState.contextNew.IsRasterizationEnabled())
      		    vsHash += 51ULL;

			if (_activeFetchShader->mtlFetchVertexManually)
                vsHash += 349ULL;
		}
	}
#endif

	_shaderBaseHash_vs = vsHash;
}

void LatteSHRC_UpdateGSBaseHash(uint8* geometryShaderPtr, uint32 geometryShaderSize, uint8* geometryCopyShader, uint32 geometryCopyShaderSize)
{
	uint64 gsHash1 = 0;
	uint64 gsHash2 = 0;
	_calculateShaderProgramHash((uint32*)geometryShaderPtr, geometryShaderSize, &hashCacheGS, &gsHash1, &gsHash2);
	uint64 gsHash = gsHash1 + gsHash2;
#ifdef ENABLE_METAL
	if (g_renderer->GetType() == RendererAPI::Metal)
	{
		uint64 copyHash1 = 0;
		uint64 copyHash2 = 0;
		_calculateShaderProgramHash((uint32*)geometryCopyShader, geometryCopyShaderSize, &hashCacheGSCopy, &copyHash1, &copyHash2);
		gsHash = std::rotl<uint64>(gsHash, 17) + copyHash1;
		gsHash = std::rotl<uint64>(gsHash, 29) + copyHash2;
	}
#endif
	gsHash += (uint64)_activeVertexShader->ringParameterCount;
	gsHash += (LatteGPUState.contextRegister[mmVGT_STRMOUT_EN] ? 21 : 0);
	_shaderBaseHash_gs = gsHash;
}

void LatteSHRC_UpdatePSBaseHash(uint8* pixelShaderPtr, uint32 pixelShaderSize, bool usesGeometryShader)
{
	uint32* psProgramCode = (uint32*)pixelShaderPtr;
	uint64 psHash1 = 0;
	uint64 psHash2 = 0;
	_calculateShaderProgramHash(psProgramCode, pixelShaderSize, &hashCachePS, &psHash1, &psHash2);
	uint64 psHash = psHash1 + psHash2 + _activePSImportTable.key + (usesGeometryShader ? hashCacheGS.prevHash1 : 0ULL);

	_shaderBaseHash_ps = psHash;
}

uint64 LatteSHRC_CalcVSAuxHash(LatteDecompilerShader* vertexShader, uint32* contextRegisters)
{
	uint64 auxHash = 0;
	if(vertexShader->hasStreamoutBufferWrite)
	{
		for (uint32 i = 0; i < LATTE_NUM_STREAMOUT_BUFFER; i++)
		{
			if(!vertexShader->streamoutBufferWriteMask[i])
				continue;
			uint32 bufferStride = contextRegisters[mmVGT_STRMOUT_VTX_STRIDE_0 + i * 4];
			auxHash = std::rotl<uint64>(auxHash, 7);
			auxHash += (uint64)bufferStride;
		}
	}
	uint64 auxHashTex = 0;
	for (uint8 i = 0; i < vertexShader->textureUnitListCount; i++)
	{
		uint8 t = vertexShader->textureUnitList[i];
		uint32 word4 = contextRegisters[Latte::REGADDR::SQ_TEX_RESOURCE_WORD0_N_VS + t * 7 + 4];
		if ((word4 & 0x300) == 0x100)
		{
			auxHashTex = std::rotl<uint64>(auxHashTex, 7);
			auxHashTex += 0x333;
		}
	}

	return auxHash + auxHashTex;
}

uint64 LatteSHRC_CalcGSAuxHash(LatteDecompilerShader* geometryShader, uint32* contextRegisters)
{
	uint64 auxHash = 0;
#ifdef ENABLE_METAL
	if (g_renderer->GetType() == RendererAPI::Metal)
	{
		const auto addState = [&auxHash](uint32 value)
		{
			auxHash = std::rotl<uint64>(auxHash, 11);
			auxHash += value;
		};

		addState(contextRegisters[mmVGT_PRIMITIVE_TYPE]);
		addState(contextRegisters[mmVGT_GS_OUT_PRIM_TYPE]);
		addState(contextRegisters[mmSQ_GSVS_RING_ITEMSIZE] & 0x7FFF);
		addState(contextRegisters[mmSQ_GS_VERT_ITEMSIZE]);

		if (geometryShader->hasStreamoutBufferWrite)
		{
			for (uint32 i = 0; i < LATTE_NUM_STREAMOUT_BUFFER; i++)
			{
				if (!geometryShader->streamoutBufferWriteMask[i])
					continue;
				addState(contextRegisters[mmVGT_STRMOUT_VTX_STRIDE_0 + i * 4]);
			}
		}
	}
#endif
	return auxHash;
}

uint64 LatteSHRC_CalcPSAuxHash(LatteDecompilerShader* pixelShader, uint32* contextRegisters)
{
	[[maybe_unused]] const LatteContextRegister& lcr = *(const LatteContextRegister*)contextRegisters;
	uint64 auxHash = 0;
	auxHash = (auxHash >> 3) | (auxHash << 61);
	auxHash += (uint64)contextRegisters[mmCB_SHADER_MASK];
	uint8 alphaTestFunc = contextRegisters[Latte::REGADDR::SX_ALPHA_TEST_CONTROL] & 0x7;
	uint8 alphaTestEnable = (contextRegisters[Latte::REGADDR::SX_ALPHA_TEST_CONTROL] >> 3) & 1;
	if (alphaTestEnable)
	{
		auxHash += (uint64)alphaTestFunc;
		auxHash = (auxHash >> 3) | (auxHash << 61);
		auxHash += 1;
	}
	for (uint8 i = 0; i < pixelShader->textureUnitListCount; i++)
	{
		uint8 t = pixelShader->textureUnitList[i];
		uint32 word0 = contextRegisters[Latte::REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS + t * 7 + 0];
		uint32 dim = (word0 & 7);
		auxHash = (auxHash << 3) | (auxHash >> 61);
		auxHash += (uint64)dim;
	}

#ifdef ENABLE_VULKAN
	if (g_renderer->GetType() == RendererAPI::Vulkan)
	{
		const uint64 auxHashWithoutTargets = auxHash;
		bool hasIntegerTarget = false;
		for (uint8 i = 0; i < LATTE_NUM_COLOR_TARGET; i++)
		{
			const uint32 format = (uint32)LatteMRT::GetColorBufferFormat(i, lcr);
			const bool isInteger = (format & (uint32)Latte::E_GX2SURFFMT::FMT_BIT_INT) != 0;
			const bool isSigned = (format & (uint32)Latte::E_GX2SURFFMT::FMT_BIT_SIGNED) != 0;
			const uint8 dataType = isInteger ? (isSigned ? 2 : 1) : 0;

			auxHash = std::rotl<uint64>(auxHash, 7);
			auxHash += dataType;
			hasIntegerTarget |= isInteger;
		}
		if (!hasIntegerTarget)
			auxHash = auxHashWithoutTargets;
	}
#endif

#ifdef ENABLE_METAL
	if (g_renderer->GetType() == RendererAPI::Metal)
	{
		for (uint32 i = 0; i < pixelShader->textureUnitListCount; i++)
		{
		    uint8 t = pixelShader->textureUnitList[i];
		    auxHash = std::rotl<uint64>(auxHash, 11);
			auxHash += (uint64)pixelShader->textureRenderTargetIndex[t];
		}

        for (uint8 i = 0; i < LATTE_NUM_COLOR_TARGET; i++)
        {
            auto format = LatteMRT::GetColorBufferFormat(i, lcr);
            uint8 dataType = (uint8)GetMtlPixelFormatInfo(format, false).dataType;
            auxHash = std::rotl<uint64>(auxHash, 7);
            auxHash += (uint64)dataType;
        }

        bool hasDepthBuffer = LatteMRT::GetActiveDepthBufferMask(lcr);
        if (hasDepthBuffer)
        {
            auxHash = std::rotl<uint64>(auxHash, 5);
            auxHash += 13u;
        }
	}
#endif

	return auxHash;
}

static void InitUniformLayoutFromDecompiler(
    LatteDecompilerShader* shader,
    const LatteDecompilerOutput_t& decompilerOutput
)
{
	if (g_renderer->GetType() == RendererAPI::OpenGL)
	{
		shader->uniform.count_uniformRegister = decompilerOutput.uniformOffsetsGL.count_uniformRegister;
		return;
	}
    const auto& offsets = decompilerOutput.uniformOffsetsVK;

    shader->uniform.loc_remapped = offsets.offset_remapped;
    shader->uniform.loc_uniformRegister = offsets.offset_uniformRegister;
    shader->uniform.count_uniformRegister = offsets.count_uniformRegister;
    shader->uniform.loc_windowSpaceToClipSpaceTransform = offsets.offset_windowSpaceToClipSpaceTransform;
    shader->uniform.loc_alphaTestRef = offsets.offset_alphaTestRef;
    shader->uniform.loc_pointSize = offsets.offset_pointSize;
    shader->uniform.loc_fragCoordScale = offsets.offset_fragCoordScale;
	shader->uniform.loc_baseVertex = offsets.offset_baseVertex;
	shader->uniform.loc_baseInstance = offsets.offset_baseInstance;

    shader->uniform.list_ufTexRescale.clear();
    for (sint32 t = 0; t < LATTE_NUM_MAX_TEX_UNITS; t++)
    {
        if (offsets.offset_texScale[t] >= 0)
        {
            LatteUniformTextureScaleEntry_t entry{};
            entry.texUnit = t;
            entry.uniformLocation = offsets.offset_texScale[t];
            shader->uniform.list_ufTexRescale.push_back(entry);
        }
    }

    shader->uniform.loc_verticesPerInstance = offsets.offset_verticesPerInstance;

    for (sint32 t = 0; t < LATTE_NUM_STREAMOUT_BUFFER; t++)
    {
        shader->uniform.loc_streamoutBufferBase[t] = offsets.offset_streamoutBufferBase[t];
		shader->uniform.loc_streamoutBufferSize[t] = offsets.offset_streamoutBufferSize[t];
    }

    shader->uniform.uniformRangeSize = offsets.offset_endOfBlock;
}

LatteDecompilerShader* LatteShader_CreateShaderFromDecompilerOutput(LatteDecompilerOutput_t& decompilerOutput, uint64 baseHash, bool calculateAuxHash, uint64 optionalAuxHash, uint32* contextRegister)
{
	LatteDecompilerShader* shader = decompilerOutput.shader;
	shader->baseHash = baseHash;
	switch (g_renderer->GetType())
	{
	case RendererAPI::OpenGL:
		shader->resourceMapping = decompilerOutput.resourceMappingGL;
		break;
	case RendererAPI::Vulkan:
		shader->resourceMapping = decompilerOutput.resourceMappingVK;
		break;
	case RendererAPI::Metal:
		shader->resourceMapping = decompilerOutput.resourceMappingMTL;
		break;
	}
	shader->textureUnitMask2 = decompilerOutput.textureUnitMask;
	shader->streamoutBufferWriteMask = decompilerOutput.streamoutBufferWriteMask;
	shader->hasStreamoutBufferWrite = decompilerOutput.streamoutBufferWriteMask.any();
	InitUniformLayoutFromDecompiler(shader, decompilerOutput);
	if (calculateAuxHash)
	{
		if (decompilerOutput.shaderType == LatteConst::ShaderType::Vertex)
		{
			uint64 vsAuxHash = LatteSHRC_CalcVSAuxHash(shader, contextRegister);
			shader->auxHash = vsAuxHash;
		}
		else if (decompilerOutput.shaderType == LatteConst::ShaderType::Geometry)
		{
			uint64 gsAuxHash = LatteSHRC_CalcGSAuxHash(shader, contextRegister);
			shader->auxHash = gsAuxHash;
		}
		else if (decompilerOutput.shaderType == LatteConst::ShaderType::Pixel)
		{
			uint64 psAuxHash = LatteSHRC_CalcPSAuxHash(shader, contextRegister);
			shader->auxHash = psAuxHash;
		}
		else
			cemu_assert_debug(false);
	}
	else
	{
		shader->auxHash = optionalAuxHash;
	}
	return shader;
}

std::atomic<bool> g_latteRelaxShaderMul{false};

void LatteShader_GetDecompilerOptions(LatteDecompilerOptions& options, LatteConst::ShaderType shaderType, bool geometryShaderEnabled)
{
	options.usesGeometryShader = geometryShaderEnabled;
	options.spirvInstrinsics.hasRoundingModeRTEFloat32 = false;
	options.useTFViaSSBO = g_renderer->UseTFViaSSBO();
#ifdef ENABLE_VULKAN
	if (g_renderer->GetType() == RendererAPI::Vulkan)
	{
		options.spirvInstrinsics.hasRoundingModeRTEFloat32 = VulkanRenderer::GetInstance()->HasSPRIVRoundingModeRTE32();
	}
#endif
	options.strictMul = !g_latteRelaxShaderMul.load(std::memory_order_relaxed)
		&& g_current_game_profile->GetAccurateShaderMul() != AccurateShaderMulOption::False;
}

LatteDecompilerShader* LatteShader_CompileSeparableVertexShader2(uint64 baseHash, uint64& vsAuxHash, uint8* vertexShaderPtr, uint32 vertexShaderSize, bool usesGeometryShader, LatteFetchShader* fetchShader)
{
	Latte::ShaderDescription shaderDescription;
	if (!shaderDescription.analyzeShaderCode(vertexShaderPtr, vertexShaderSize, LatteConst::ShaderType::Vertex))
	{
		assert_dbg();
		return nullptr;
	}
	assert_dbg();

	ZpIR::DebugPrinter irDebugPrinter;
	LatteTCGenIR genIR;
	genIR.setVertexShaderContext(fetchShader, LatteGPUState.contextRegister + mmSQ_VTX_SEMANTIC_0);
	auto irObj = genIR.transcompileLatteToIR(vertexShaderPtr, vertexShaderSize, LatteTCGenIR::VERTEX);
	irDebugPrinter.setShowPhysicalRegisters(false);
	irDebugPrinter.debugPrint(irObj);
	ZirPass::RegisterAllocatorForGLSL ra(irObj);
	ra.applyPass();
	irDebugPrinter.setShowPhysicalRegisters(true);
	irDebugPrinter.setPhysicalRegisterNameSource(ZirPass::RegisterAllocatorForGLSL::DebugPrintHelper_getPhysRegisterName);
	irDebugPrinter.debugPrint(irObj);
	StringBuf glslSourceBuffer(64 * 1024);
	assert_dbg();
	ZirEmitter::GLSL emitter;
	emitter.Emit(irObj, &glslSourceBuffer);

	std::string dbg;
	dbg.insert(0, glslSourceBuffer.c_str(), glslSourceBuffer.getLen());
	assert_dbg();

	return nullptr;
}

LatteDecompilerShader* LatteShader_CompileSeparableVertexShader(uint64 baseHash, uint64& vsAuxHash, uint8* vertexShaderPtr, uint32 vertexShaderSize, bool usesGeometryShader, LatteFetchShader* fetchShader)
{

	LatteDecompilerOptions options;
	LatteShader_GetDecompilerOptions(options, LatteConst::ShaderType::Vertex, usesGeometryShader);

	LatteDecompilerOutput_t decompilerOutput{};
	LatteDecompiler_DecompileVertexShader(_shaderBaseHash_vs, LatteGPUState.contextRegister, vertexShaderPtr, vertexShaderSize, fetchShader, options, &decompilerOutput);
	LatteDecompilerShader* vertexShader = LatteShader_CreateShaderFromDecompilerOutput(decompilerOutput, baseHash, true, 0, LatteGPUState.contextRegister);
	vsAuxHash = vertexShader->auxHash;
	if (vertexShader->hasError == false)
	{
		uint8* fsProgramCode = (uint8*)memory_getPointerFromPhysicalOffset(LatteGPUState.contextRegister[mmSQ_PGM_START_FS + 0] << 8);
		uint32 fsProgramSize = LatteGPUState.contextRegister[mmSQ_PGM_START_FS + 1] << 3;
		LatteShaderCache_writeSeparableVertexShader(vertexShader->baseHash, vertexShader->auxHash, fsProgramCode, fsProgramSize, vertexShaderPtr, vertexShaderSize, LatteGPUState.contextRegister, usesGeometryShader);
	}
	LatteShader_DumpShader(vertexShader->baseHash, vertexShader->auxHash, vertexShader);
	LatteShader_DumpRawShader(vertexShader->baseHash, vertexShader->auxHash, SHADER_DUMP_TYPE_VERTEX, vertexShaderPtr, vertexShaderSize);
	LatteShader_CreateRendererShader(vertexShader, false);
	performanceMonitor.numCompiledVS++;

#ifdef ENABLE_OPENGL
	if (g_renderer->GetType() == RendererAPI::OpenGL)
	{
		if (vertexShader->shader)
			vertexShader->shader->PreponeCompilation(true);
		LatteShader_FinishCompilation(vertexShader);
	}
#endif

	LatteSHRC_RegisterShader(vertexShader, vertexShader->baseHash, vertexShader->auxHash);
	return vertexShader;
}

LatteDecompilerShader* LatteShader_CompileSeparableGeometryShader(uint64 baseHash, uint8* geometryShaderPtr, uint32 geometryShaderSize, uint8* geometryCopyShader, uint32 geometryCopyShaderSize)
{
	LatteDecompilerOptions options;
	LatteShader_GetDecompilerOptions(options, LatteConst::ShaderType::Geometry, true);

	LatteDecompilerOutput_t decompilerOutput{};
	LatteDecompiler_DecompileGeometryShader(_shaderBaseHash_gs, LatteGPUState.contextRegister, geometryShaderPtr, geometryShaderSize, geometryCopyShader, geometryCopyShaderSize, _activeVertexShader->ringParameterCount, options, &decompilerOutput);
	LatteDecompilerShader* geometryShader = LatteShader_CreateShaderFromDecompilerOutput(decompilerOutput, baseHash, true, 0, LatteGPUState.contextRegister);
	if (geometryShader->hasError == false)
	{
		LatteShaderCache_writeSeparableGeometryShader(geometryShader->baseHash, geometryShader->auxHash, geometryShaderPtr, geometryShaderSize, geometryCopyShader, geometryCopyShaderSize, LatteGPUState.contextRegister, LatteGPUState.contextNew.GetSpecialStateValues(), _activeVertexShader->ringParameterCount);
	}
	LatteShader_DumpShader(geometryShader->baseHash, geometryShader->auxHash, geometryShader);
	LatteShader_DumpRawShader(geometryShader->baseHash, geometryShader->auxHash, SHADER_DUMP_TYPE_GEOMETRY, geometryShaderPtr, geometryShaderSize);
	LatteShader_DumpRawShader(geometryShader->baseHash, geometryShader->auxHash, SHADER_DUMP_TYPE_COPY, geometryCopyShader, geometryCopyShaderSize);
	LatteShader_CreateRendererShader(geometryShader, false);
	performanceMonitor.numCompiledGS++;

#ifdef ENABLE_OPENGL
	if (g_renderer->GetType() == RendererAPI::OpenGL)
	{
		if (geometryShader->shader)
			geometryShader->shader->PreponeCompilation(true);
		LatteShader_FinishCompilation(geometryShader);
	}
#endif

	LatteSHRC_RegisterShader(geometryShader, geometryShader->baseHash, geometryShader->auxHash);
	return geometryShader;
}

LatteDecompilerShader* LatteShader_CompileSeparablePixelShader(uint64 baseHash, uint64& psAuxHash, uint8* pixelShaderPtr, uint32 pixelShaderSize, bool usesGeometryShader)
{
	LatteDecompilerOptions options;
	LatteShader_GetDecompilerOptions(options, LatteConst::ShaderType::Pixel, usesGeometryShader);

	LatteDecompilerOutput_t decompilerOutput{};
	LatteDecompiler_DecompilePixelShader(baseHash, LatteGPUState.contextRegister, pixelShaderPtr, pixelShaderSize, options, &decompilerOutput);
	LatteDecompilerShader* pixelShader = LatteShader_CreateShaderFromDecompilerOutput(decompilerOutput, baseHash, true, 0, LatteGPUState.contextRegister);
	psAuxHash = pixelShader->auxHash;
	LatteShader_DumpShader(_shaderBaseHash_ps, psAuxHash, pixelShader);
	LatteShader_DumpRawShader(_shaderBaseHash_ps, psAuxHash, SHADER_DUMP_TYPE_PIXEL, pixelShaderPtr, pixelShaderSize);
	LatteShader_CreateRendererShader(pixelShader, false);
	performanceMonitor.numCompiledPS++;
	if (pixelShader->hasError == false)
	{
		LatteShaderCache_writeSeparablePixelShader(_shaderBaseHash_ps, psAuxHash, pixelShaderPtr, pixelShaderSize, LatteGPUState.contextRegister, usesGeometryShader);
	}

#ifdef ENABLE_OPENGL
	if (g_renderer->GetType() == RendererAPI::OpenGL)
	{
		if (pixelShader->shader)
			pixelShader->shader->PreponeCompilation(true);
		LatteShader_FinishCompilation(pixelShader);
	}
#endif

	LatteSHRC_RegisterShader(pixelShader, _shaderBaseHash_ps, psAuxHash);
	return pixelShader;
}

void LatteSHRC_UpdateVertexShader(uint8* vertexShaderPtr, uint32 vertexShaderSize, bool usesGeometryShader)
{
	LatteSHRC_UpdateVSBaseHash(vertexShaderPtr, vertexShaderSize, usesGeometryShader);
	uint64 vsAuxHash = 0;
	auto itBaseShader = sVertexShaders.find(_shaderBaseHash_vs);
	LatteDecompilerShader* vertexShader = nullptr;
	if (itBaseShader != sVertexShaders.end())
	{
		vsAuxHash = LatteSHRC_CalcVSAuxHash(itBaseShader->second, LatteGPUState.contextRegister);
		vertexShader = LatteSHRC_GetFromChain(itBaseShader->second, _shaderBaseHash_vs, vsAuxHash);
	}
	if (!vertexShader)
		vertexShader = LatteShader_CompileSeparableVertexShader(_shaderBaseHash_vs, vsAuxHash, vertexShaderPtr, vertexShaderSize, usesGeometryShader, _activeFetchShader);
	if (vertexShader->hasError)
	{
		LatteGPUState.activeShaderHasError = true;
		return;
	}
	_activeVertexShader = vertexShader;
}

void LatteSHRC_UpdateGeometryShader(bool usesGeometryShader, uint8* geometryShaderPtr, uint32 geometryShaderSize, uint8* geometryCopyShader, uint32 geometryCopyShaderSize)
{
	if (!usesGeometryShader || !_activeVertexShader)
	{
		_shaderBaseHash_gs = 0;
		_activeGeometryShader = nullptr;
		return;
	}
	LatteSHRC_UpdateGSBaseHash(geometryShaderPtr, geometryShaderSize, geometryCopyShader, geometryCopyShaderSize);
	auto itBaseShader = sGeometryShaders.find(_shaderBaseHash_gs);
	LatteDecompilerShader* geometryShader = nullptr;
	if (itBaseShader != sGeometryShaders.end())
	{
		const uint64 gsAuxHash = LatteSHRC_CalcGSAuxHash(itBaseShader->second, LatteGPUState.contextRegister);
		geometryShader = LatteSHRC_GetFromChain(itBaseShader->second, _shaderBaseHash_gs, gsAuxHash);
	}
	if (!geometryShader)
		geometryShader = LatteShader_CompileSeparableGeometryShader(_shaderBaseHash_gs, geometryShaderPtr, geometryShaderSize, geometryCopyShader, geometryCopyShaderSize);
	if (geometryShader->hasError)
	{
		LatteGPUState.activeShaderHasError = true;
		return;
	}
	_activeGeometryShader = geometryShader;
}

void LatteSHRC_UpdatePixelShader(uint8* pixelShaderPtr, uint32 pixelShaderSize, bool usesGeometryShader)
{
	LatteSHRC_UpdatePSBaseHash(pixelShaderPtr, pixelShaderSize, usesGeometryShader);
	uint64 psAuxHash = 0;
	auto itBaseShader = sPixelShaders.find(_shaderBaseHash_ps);
	LatteDecompilerShader* pixelShader = nullptr;
	if (itBaseShader != sPixelShaders.end())
	{
		psAuxHash = LatteSHRC_CalcPSAuxHash(itBaseShader->second, LatteGPUState.contextRegister);
		pixelShader = LatteSHRC_GetFromChain(itBaseShader->second, _shaderBaseHash_ps, psAuxHash);
	}
	if (!pixelShader)
		pixelShader = LatteShader_CompileSeparablePixelShader(_shaderBaseHash_ps, psAuxHash, pixelShaderPtr, pixelShaderSize, usesGeometryShader);
	if (pixelShader->hasError)
	{
		LatteGPUState.activeShaderHasError = true;
		return;
	}
	_activePixelShader = pixelShader;
}

void LatteSHRC_UpdateActiveShaders()
{
	auto gsMode = LatteGPUState.contextNew.VGT_GS_MODE.get_MODE();

	cemu_assert_debug(LatteGPUState.contextNew.VGT_GS_MODE.get_ES_PASSTHRU() == false);

	bool geometryShaderUsed = false;
	if (gsMode == Latte::LATTE_VGT_GS_MODE::E_MODE::OFF)
	{
		geometryShaderUsed = false;
	}
	else if (gsMode == Latte::LATTE_VGT_GS_MODE::E_MODE::SCENARIO_G)
	{
		geometryShaderUsed = true;
	}
	else
	{
		cemu_assert_debug(false);
	}
	uint8* psProgramCode = (uint8*)memory_getPointerFromPhysicalOffset((LatteGPUState.contextRegister[mmSQ_PGM_START_PS] & 0xFFFFFF) << 8);
	uint32 psProgramSize = LatteGPUState.contextRegister[mmSQ_PGM_START_PS + 1] << 3;
	uint8* gsProgramCode = (uint8*)memory_getPointerFromPhysicalOffset((LatteGPUState.contextRegister[mmSQ_PGM_START_GS] & 0xFFFFFF) << 8);
	uint32 gsProgramSize = LatteGPUState.contextRegister[mmSQ_PGM_START_GS + 1] << 3;

	uint8* vsProgramCode;
	uint32 vsProgramSize;
	uint8* copyProgramCode = NULL;
	uint32 copyProgramSize = 0;
	if (geometryShaderUsed)
	{
		vsProgramCode = (uint8*)memory_getPointerFromPhysicalOffset((LatteGPUState.contextRegister[mmSQ_PGM_START_ES] & 0xFFFFFF) << 8);
		vsProgramSize = LatteGPUState.contextRegister[mmSQ_PGM_START_ES + 1] << 3;
		copyProgramCode = (uint8*)memory_getPointerFromPhysicalOffset((LatteGPUState.contextRegister[mmSQ_PGM_START_VS] & 0xFFFFFF) << 8);
		if (LatteGPUState.contextRegister[mmSQ_PGM_START_VS] == 0)
		{
			copyProgramCode = NULL;
			debug_printf("copyProgram is NULL but used. Might be because of unsupported vertex/geometry mode?");
		}
		copyProgramSize = LatteGPUState.contextRegister[mmSQ_PGM_START_VS + 1] << 3;
	}
	else
	{
		if (LatteGPUState.contextRegister[mmSQ_PGM_START_VS] == 0)
		{
			debug_printf("No vertex shader program set\n");
			LatteGPUState.activeShaderHasError = true;
			return;
		}
		vsProgramCode = (uint8*)memory_getPointerFromPhysicalOffset((LatteGPUState.contextRegister[mmSQ_PGM_START_VS] & 0xFFFFFF) << 8);
		vsProgramSize = LatteGPUState.contextRegister[mmSQ_PGM_START_VS + 1] << 3;
	}
	LatteGPUState.activeShaderHasError = false;
	LatteShader_UpdatePSInputs(LatteGPUState.contextRegister);
	LatteShaderSHRC_UpdateFetchShader();
	LatteSHRC_UpdateVertexShader(vsProgramCode, vsProgramSize, geometryShaderUsed);
	if (LatteGPUState.activeShaderHasError)
		return;
	LatteSHRC_UpdateGeometryShader(geometryShaderUsed, gsProgramCode, gsProgramSize, copyProgramCode, copyProgramSize);
	if (LatteGPUState.activeShaderHasError)
		return;
	LatteSHRC_UpdatePixelShader(psProgramCode, psProgramSize, geometryShaderUsed);
	if (LatteGPUState.activeShaderHasError)
		return;
}

sint32 LatteDecompiler_getTextureSamplerBaseIndex(LatteConst::ShaderType shaderType)
{
	uint32 samplerId = LATTE_DECOMPILER_SAMPLER_NONE;
	if (shaderType == LatteConst::ShaderType::Vertex)
		return Latte::SAMPLER_BASE_INDEX_VERTEX;
	else if (shaderType == LatteConst::ShaderType::Pixel)
		return Latte::SAMPLER_BASE_INDEX_PIXEL;
	else if (shaderType == LatteConst::ShaderType::Geometry)
		return Latte::SAMPLER_BASE_INDEX_GEOMETRY;
	else
		cemu_assert_suspicious();
	return 0;
}

void LatteSHRC_Init()
{
	cemu_assert_debug(sVertexShaders.empty());
	cemu_assert_debug(sGeometryShaders.empty());
	cemu_assert_debug(sPixelShaders.empty());
}

void LatteSHRC_UnloadAll()
{
    while(!sVertexShaders.empty())
        LatteShader_free(sVertexShaders.begin()->second);
    cemu_assert_debug(sVertexShaders.empty());
    while(!sGeometryShaders.empty())
        LatteShader_free(sGeometryShaders.begin()->second);
    cemu_assert_debug(sGeometryShaders.empty());
    while(!sPixelShaders.empty())
        LatteShader_free(sPixelShaders.begin()->second);
    cemu_assert_debug(sPixelShaders.empty());
    _activeFetchShader = nullptr;
    _activeVertexShader = nullptr;
    _activeGeometryShader = nullptr;
    _activePixelShader = nullptr;
    _shaderBaseHash_vs = 0;
    _shaderBaseHash_gs = 0;
    _shaderBaseHash_ps = 0;
    _activePSImportTable = LatteShaderPSInputTable{};
    LatteSHRC_ResetCachedShaderHash();
    LatteFetchShader::ResetLookupCache();
}

size_t LatteSHRC_GetCachedShaderCount()
{
    return sVertexShaders.size() + sGeometryShaders.size() + sPixelShaders.size();
}

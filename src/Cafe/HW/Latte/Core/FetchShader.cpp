#include "Cafe/HW/Latte/Core/LatteConst.h"
#include "Cafe/HW/Latte/Core/LatteShaderAssembly.h"
#include "Cafe/HW/Latte/ISA/RegDefines.h"
#include "Cafe/OS/libs/gx2/GX2.h"
#include "Cafe/HW/Latte/Core/Latte.h"
#include "Cafe/HW/Latte/Core/LatteDraw.h"
#include "Cafe/HW/Latte/LegacyShaderDecompiler/LatteDecompiler.h"
#include "Cafe/HW/Latte/LegacyShaderDecompiler/LatteDecompilerInstructions.h"
#include "Cafe/HW/Latte/Core/FetchShader.h"
#include "Cafe/HW/Latte/ISA/LatteInstructions.h"
#include "HW/Latte/Renderer/Renderer.h"
#include "util/containers/LookupTableL3.h"
#include "util/helpers/fspinlock.h"
#ifdef ENABLE_METAL
#include "Cafe/HW/Latte/Renderer/Metal/LatteToMtl.h"
#endif
#include <openssl/sha.h>
#include <openssl/evp.h>

uint32 LatteShaderRecompiler_getAttributeSize(LatteParsedFetchShaderAttribute_t* attrib)
{
	if (attrib->format == FMT_32_32_32_32 || attrib->format == FMT_32_32_32_32_FLOAT)
		return 4 * 4;
	else if (attrib->format == FMT_32_32_32 || attrib->format == FMT_32_32_32_FLOAT)
		return 3 * 4;
	else if (attrib->format == FMT_32_32 || attrib->format == FMT_32_32_FLOAT)
		return 2 * 4;
	else if (attrib->format == FMT_32 || attrib->format == FMT_32_FLOAT)
		return 1 * 4;
	else if (attrib->format == FMT_16_16_16_16 || attrib->format == FMT_16_16_16_16_FLOAT)
		return 4 * 2;
	else if (attrib->format == FMT_16_16 || attrib->format == FMT_16_16_FLOAT)
		return 2 * 2;
	else if (attrib->format == FMT_16 || attrib->format == FMT_16_FLOAT)
		return 1 * 2;
	else if (attrib->format == FMT_8_8_8_8)
		return 4 * 1;
	else if (attrib->format == FMT_8_8)
		return 2 * 1;
	else if (attrib->format == FMT_8)
		return 1 * 1;
	else if (attrib->format == FMT_2_10_10_10)
		return 4;
	else
		cemu_assert_unimplemented();
	return 0;
}

uint32 LatteShaderRecompiler_getAttributeAlignment(LatteParsedFetchShaderAttribute_t* attrib)
{
	if (attrib->format == FMT_32_32_32_32 || attrib->format == FMT_32_32_32_32_FLOAT)
		return 4;
	else if (attrib->format == FMT_32_32_32 || attrib->format == FMT_32_32_32_FLOAT)
		return 4;
	else if (attrib->format == FMT_32_32 || attrib->format == FMT_32_32_FLOAT)
		return 4;
	else if (attrib->format == FMT_32 || attrib->format == FMT_32_FLOAT)
		return 4;
	else if (attrib->format == FMT_16_16_16_16 || attrib->format == FMT_16_16_16_16_FLOAT)
		return 2;
	else if (attrib->format == FMT_16_16 || attrib->format == FMT_16_16_FLOAT)
		return 2;
	else if (attrib->format == FMT_16 || attrib->format == FMT_16_FLOAT)
		return 2;
	else if (attrib->format == FMT_8_8_8_8)
		return 1;
	else if (attrib->format == FMT_8_8)
		return 1;
	else if (attrib->format == FMT_8)
		return 1;
	else if (attrib->format == FMT_2_10_10_10)
		return 4;
	else
		cemu_assert_unimplemented();
	return 4;
}

void LatteShader_calculateFSKey(LatteFetchShader* fetchShader)
{
	uint64 key = 0;
	for (sint32 g = 0; g < fetchShader->bufferGroups.size(); g++)
	{
		LatteParsedFetchShaderBufferGroup_t& group = fetchShader->bufferGroups[g];
		for (sint32 f = 0; f < group.attribCount; f++)
		{
			LatteParsedFetchShaderAttribute_t* attrib = group.attrib + f;
			key += (uint64)attrib->endianSwap;
			key = std::rotl<uint64>(key, 3);
			key += (uint64)attrib->nfa;
			key = std::rotl<uint64>(key, 3);
			key += (uint64)(attrib->isSigned?1:0);
			key = std::rotl<uint64>(key, 1);
			key += (uint64)attrib->format;
			key = std::rotl<uint64>(key, 7);
			key += (uint64)attrib->fetchType;
			key = std::rotl<uint64>(key, 8);
			key += (uint64)attrib->ds[0];
			key = std::rotl<uint64>(key, 2);
			key += (uint64)attrib->ds[1];
			key = std::rotl<uint64>(key, 2);
			key += (uint64)attrib->ds[2];
			key = std::rotl<uint64>(key, 2);
			key += (uint64)attrib->ds[3];
			key = std::rotl<uint64>(key, 2);
			key += (uint64)(attrib->aluDivisor+1);
			key = std::rotl<uint64>(key, 2);
			key += (uint64)attrib->attributeBufferIndex;
			key = std::rotl<uint64>(key, 8);
			key += (uint64)attrib->semanticId;
			key = std::rotl<uint64>(key, 8);
			switch(g_renderer->GetType())
			{
#ifdef ENABLE_METAL
			case RendererAPI::Metal:
			{
			    key += (uint64)attrib->offset;
				key = std::rotl<uint64>(key, 7);
				break;
			}
#endif
			default:
			{
				key += (uint64)(attrib->offset & 3);
				key = std::rotl<uint64>(key, 2);
				break;
			}
			}
		}
	}

#ifdef ENABLE_METAL
	if (g_renderer->GetType() == RendererAPI::Metal)
	{
		for (sint32 g = 0; g < fetchShader->bufferGroups.size(); g++)
	{
			LatteParsedFetchShaderBufferGroup_t& group = fetchShader->bufferGroups[g];
			key += (uint64)group.attributeBufferIndex;
			key = std::rotl<uint64>(key, 5);
		}
	}
#endif

	fetchShader->key = key;
}

uint32 LatteParsedFetchShaderBufferGroup_t::getCurrentBufferStride(uint32* contextRegister) const
{
	uint32 bufferIndex = this->attributeBufferIndex;
	uint32 bufferBaseRegisterIndex = mmSQ_VTX_ATTRIBUTE_BLOCK_START + bufferIndex * 7;
	uint32 bufferStride = (contextRegister[bufferBaseRegisterIndex + 2] >> 11) & 0xFFFF;
	return bufferStride;
}

void LatteFetchShader::CalculateFetchShaderVkHash()
{
	EVP_MD_CTX *ctx = EVP_MD_CTX_new();
	EVP_DigestInit(ctx, EVP_sha1());
	for(auto& group : bufferGroups)
	{
		for (sint32 t = 0; t < group.attribCount; t++)
		{
			uint32 offset = group.attrib[t].offset;
			EVP_DigestUpdate(ctx, &t, sizeof(t));
			EVP_DigestUpdate(ctx, &offset, sizeof(offset));
		}
	}
	uint8 shaDigest[SHA_DIGEST_LENGTH];
	EVP_DigestFinal_ex(ctx, shaDigest, NULL);
	EVP_MD_CTX_free(ctx);

	uint64 h = *(uint64*)(shaDigest + 0);
	h += *(uint64*)(shaDigest + 8);
	h += (uint64)*(uint32*)(shaDigest + 16);
	this->vkPipelineHashFragment = h;
}

#ifdef ENABLE_METAL
void LatteFetchShader::CheckIfVerticesNeedManualFetchMtl(uint32* contextRegister)
{
	for (sint32 g = 0; g < bufferGroups.size(); g++)
	{
	    LatteParsedFetchShaderBufferGroup_t& group = bufferGroups[g];
		uint32 bufferIndex = group.attributeBufferIndex;
		uint32 bufferBaseRegisterIndex = mmSQ_VTX_ATTRIBUTE_BLOCK_START + bufferIndex * 7;
		uint32 bufferStride = (contextRegister[bufferBaseRegisterIndex + 2] >> 11) & 0xFFFF;

  		if (bufferStride % 4 != 0)
  		    mtlFetchVertexManually = true;

  		for (sint32 f = 0; f < group.attribCount; f++)
  		{
  		    auto& attr = group.attrib[f];
  		    if (attr.offset + GetMtlVertexFormatSize(attr.format) > bufferStride)
 			    mtlFetchVertexManually = true;
  		}
	}
}
#endif

void _fetchShaderDecompiler_parseInstruction_VTX_SEMANTIC(LatteFetchShader* parsedFetchShader, uint32* contextRegister, const LatteClauseInstruction_VTX* instr)
{
	uint32 semanticId = instr->getFieldSEM_SEMANTIC_ID();
	uint32 bufferId = instr->getField_BUFFER_ID();
	LatteConst::VertexFetchType2 fetchType = instr->getField_FETCH_TYPE();
	auto srcSelX = instr->getField_SRC_SEL_X();
	auto dsx = instr->getField_DST_SEL(0);
	auto dsy = instr->getField_DST_SEL(1);
	auto dsz = instr->getField_DST_SEL(2);
	auto dsw = instr->getField_DST_SEL(3);
	auto dataFormat = instr->getField_DATA_FORMAT();
	uint32 offset = instr->getField_OFFSET();
	auto nfa = instr->getField_NUM_FORMAT_ALL();
	bool isSigned = instr->getField_FORMAT_COMP_ALL() == LatteClauseInstruction_VTX::FORMAT_COMP::COMP_SIGNED;
	auto endianSwap = instr->getField_ENDIAN_SWAP();

	cemu_assert_debug(bufferId >= 0xA0 && bufferId < 0xB0);
	uint32 bufferIndex = (bufferId - 0xA0);

	LatteParsedFetchShaderBufferGroup_t* attribGroup = nullptr;
	if (LatteFetchShader::isValidBufferIndex(bufferIndex))
	{
		auto bufferGroupItr = std::find_if(parsedFetchShader->bufferGroups.begin(), parsedFetchShader->bufferGroups.end(), [bufferIndex](LatteParsedFetchShaderBufferGroup_t& bufferGroup) {return bufferGroup.attributeBufferIndex == bufferIndex; });
		if (bufferGroupItr != parsedFetchShader->bufferGroups.end())
			attribGroup = &(*bufferGroupItr);
	}
	else
	{
		auto bufferGroupItr = std::find_if(parsedFetchShader->bufferGroupsInvalid.begin(), parsedFetchShader->bufferGroupsInvalid.end(), [bufferIndex](LatteParsedFetchShaderBufferGroup_t& bufferGroup) {return bufferGroup.attributeBufferIndex == bufferIndex; });
		if (bufferGroupItr != parsedFetchShader->bufferGroupsInvalid.end())
			attribGroup = &(*bufferGroupItr);
	}
	if (attribGroup == nullptr)
	{
		if (LatteFetchShader::isValidBufferIndex(bufferIndex))
			attribGroup = &parsedFetchShader->bufferGroups.emplace_back();
		else
			attribGroup = &parsedFetchShader->bufferGroupsInvalid.emplace_back();

		attribGroup->attributeBufferIndex = bufferIndex;
		attribGroup->minOffset = offset;
		attribGroup->maxOffset = offset;
	}
	sint32 groupAttribIndex = attribGroup->attribCount;
	if (attribGroup->attribCount < (groupAttribIndex + 1))
	{
		attribGroup->attribCount = (groupAttribIndex + 1);
		attribGroup->attrib = (LatteParsedFetchShaderAttribute_t*)realloc(attribGroup->attrib, sizeof(LatteParsedFetchShaderAttribute_t) * attribGroup->attribCount);
	}
	attribGroup->attrib[groupAttribIndex].semanticId = semanticId;
	attribGroup->attrib[groupAttribIndex].format = (uint8)dataFormat;
	attribGroup->attrib[groupAttribIndex].fetchType = fetchType;
	attribGroup->attrib[groupAttribIndex].nfa = (uint8)nfa;
	attribGroup->attrib[groupAttribIndex].isSigned = isSigned;
	attribGroup->attrib[groupAttribIndex].offset = offset;
	attribGroup->attrib[groupAttribIndex].ds[0] = (uint8)dsx;
	attribGroup->attrib[groupAttribIndex].ds[1] = (uint8)dsy;
	attribGroup->attrib[groupAttribIndex].ds[2] = (uint8)dsz;
	attribGroup->attrib[groupAttribIndex].ds[3] = (uint8)dsw;
	attribGroup->attrib[groupAttribIndex].attributeBufferIndex = bufferIndex;
	attribGroup->attrib[groupAttribIndex].endianSwap = endianSwap;
	attribGroup->minOffset = (std::min)(attribGroup->minOffset, offset);
	attribGroup->maxOffset = (std::max)(attribGroup->maxOffset, offset);
	if (srcSelX == LatteClauseInstruction_VTX::SRC_SEL::SEL_X)
	{
		cemu_assert_debug(fetchType != LatteConst::VertexFetchType2::INSTANCE_DATA);
		attribGroup->attrib[groupAttribIndex].aluDivisor = -1;
	}
	else if (srcSelX == LatteClauseInstruction_VTX::SRC_SEL::SEL_W)
	{
		cemu_assert_debug(fetchType == LatteConst::VertexFetchType2::INSTANCE_DATA);
		attribGroup->attrib[groupAttribIndex].aluDivisor = 1;
	}
	else if (srcSelX == LatteClauseInstruction_VTX::SRC_SEL::SEL_Y)
	{
		attribGroup->attrib[groupAttribIndex].aluDivisor = (sint32)contextRegister[Latte::REGADDR::VGT_INSTANCE_STEP_RATE_0];
		cemu_assert_debug(attribGroup->attrib[groupAttribIndex].aluDivisor > 0);
	}
	else if (srcSelX == LatteClauseInstruction_VTX::SRC_SEL::SEL_Z)
	{
		attribGroup->attrib[groupAttribIndex].aluDivisor = (sint32)contextRegister[Latte::REGADDR::VGT_INSTANCE_STEP_RATE_1];
		cemu_assert_debug(attribGroup->attrib[groupAttribIndex].aluDivisor > 0);
	}
}

void _fetchShaderDecompiler_parseVTXClause(LatteFetchShader* parsedFetchShader, uint32* contextRegister, std::span<uint8> clauseCode, size_t numInstructions)
{
	const LatteClauseInstruction_VTX* instr = (LatteClauseInstruction_VTX*)clauseCode.data();
	const LatteClauseInstruction_VTX* end = instr + numInstructions;
	while (instr < end)
	{
		if (instr->getField_VTX_INST() == LatteClauseInstruction_VTX::VTX_INST::_VTX_INST_SEMANTIC)
		{
			_fetchShaderDecompiler_parseInstruction_VTX_SEMANTIC(parsedFetchShader, contextRegister, instr);
		}
		else
		{
			assert_dbg();
		}
		instr++;
	}
}

void _fetchShaderDecompiler_parseCF(LatteFetchShader* parsedFetchShader, uint32* contextRegister, std::span<uint8> programCode)
{
	size_t maxCountCFInstructions = programCode.size_bytes() / sizeof(LatteCFInstruction);
	const LatteCFInstruction* cfInstruction = (LatteCFInstruction*)programCode.data();
	const LatteCFInstruction* end = cfInstruction + maxCountCFInstructions;
	while (cfInstruction < end)
	{
		if (cfInstruction->getField_Opcode() == LatteCFInstruction::INST_VTX_TC)
		{
			auto vtxInstruction = cfInstruction->getParserIfOpcodeMatch<LatteCFInstruction_DEFAULT>();
			cemu_assert_debug(vtxInstruction->getField_COND() == LatteCFInstruction::CF_COND::CF_COND_ACTIVE);
			_fetchShaderDecompiler_parseVTXClause(parsedFetchShader, contextRegister, vtxInstruction->getClauseCode(programCode), vtxInstruction->getField_COUNT());
		}
		else if (cfInstruction->getField_Opcode() == LatteCFInstruction::INST_RETURN)
		{
			cemu_assert_debug(!cfInstruction->getField_END_OF_PROGRAM());
			return;
		}
		else
		{
			cemu_assert_debug(false);
		}
		if (cfInstruction->getField_END_OF_PROGRAM())
		{
			cemu_assert_debug(false);
			break;
		}
		cfInstruction++;
	}
	cemu_assert_debug(false);
}

LatteFetchShader* LatteShaderRecompiler_createFetchShader(LatteFetchShader::CacheHash fsHash, uint32* contextRegister, uint32* fsProgramCode, uint32 fsProgramSize)
{
	LatteFetchShader* newFetchShader = new LatteFetchShader();
	newFetchShader->m_cacheHash = fsHash;
	if( (fsProgramSize&0xF) != 0 )
		debugBreakpoint();
	uint32 index = 0;

	newFetchShader->bufferGroups.reserve(16);
	if (fsProgramSize == 0)
	{
		LatteShader_calculateFSKey(newFetchShader);
		newFetchShader->CalculateFetchShaderVkHash();
#ifdef ENABLE_METAL
		newFetchShader->CheckIfVerticesNeedManualFetchMtl(contextRegister);
#endif
		return newFetchShader;
	}

	if ((fsProgramCode[0] & 1) == 0 && fsProgramCode[0] <= 0x30 && (fsProgramCode[1]&~((3 << 10)| (1 << 19))) == 0x01800000)
	{
		_fetchShaderDecompiler_parseCF(newFetchShader, contextRegister, { (uint8*)fsProgramCode, fsProgramSize });
	}
	else
	{
		while (index < (fsProgramSize / 4))
		{
			uint32 dword0 = fsProgramCode[index];
			uint32 opcode = dword0 & 0x1F;
			index++;
			if (opcode == VTX_INST_MEM)
			{
				uint32 opcode2 = (dword0 >> 8) & 7;

				index += 3;
			}
			else if (opcode == VTX_INST_SEMANTIC)
			{
				_fetchShaderDecompiler_parseInstruction_VTX_SEMANTIC(newFetchShader, contextRegister, (const LatteClauseInstruction_VTX*)(fsProgramCode + index - 1));
				index += 3;
			}
		}
	}
	newFetchShader->bufferGroups.shrink_to_fit();
	uint32 vboOffset = 0;
	for (auto& bufferGroup : newFetchShader->bufferGroups)
	{
		for(sint32 i=0; i< bufferGroup.attribCount; i++)
		{
			uint32 attribSize = LatteShaderRecompiler_getAttributeSize(bufferGroup.attrib+i);
			uint32 attribAlignment = LatteShaderRecompiler_getAttributeAlignment(bufferGroup.attrib+i);
			vboOffset = (vboOffset+attribAlignment-1)&~(attribAlignment-1);
			vboOffset += attribSize;
			if(bufferGroup.attrib[i].fetchType == LatteConst::VERTEX_DATA)
				bufferGroup.hasVtxIndexAccess = true;
			else if (bufferGroup.attrib[i].fetchType == LatteConst::INSTANCE_DATA)
				bufferGroup.hasInstanceIndexAccess = true;
		}
		if(bufferGroup.attribCount > 0 )
		{
			uint32 attribAlignment = LatteShaderRecompiler_getAttributeAlignment(bufferGroup.attrib+0);
			vboOffset = (vboOffset+attribAlignment-1)&~(attribAlignment-1);
		}
		bufferGroup.vboStride = vboOffset;
	}
	LatteShader_calculateFSKey(newFetchShader);
	newFetchShader->CalculateFetchShaderVkHash();
#ifdef ENABLE_METAL
	newFetchShader->CheckIfVerticesNeedManualFetchMtl(contextRegister);
#endif

	LatteFetchShader* registeredFS = newFetchShader->RegisterInCache(fsHash);
	if (registeredFS)
	{
		delete newFetchShader;
		newFetchShader = registeredFS;
	}
	else
	{
		newFetchShader->m_isRegistered = true;
	}

	return newFetchShader;
}

LatteFetchShader::~LatteFetchShader()
{
	UnregisterInCache();
}

struct FetchShaderLookupInfo
{
	LatteFetchShader* fetchShader;
	uint32 programSize;
	uint32 lastFrameAccessed;
};

LookupTableL3<8, 8, 8, FetchShaderLookupInfo*> g_fetchShaderLookupCache;
static std::vector<FetchShaderLookupInfo*> s_fetchShaderLookupInfos;

void LatteFetchShader::ResetLookupCache()
{
	for (FetchShaderLookupInfo* info : s_fetchShaderLookupInfos)
	{
		info->fetchShader = nullptr;
		info->programSize = 0xFFFFFFFFu;
		info->lastFrameAccessed = 0;
	}
}

LatteFetchShader::CacheHash LatteFetchShader::CalculateCacheHash(void* programCode, uint32 programSize)
{
	uint32* programCodeU32 = (uint32*)programCode;
	uint64 progHash1 = 0;
	uint64 progHash2 = 0;
	for (uint32 i = 0; i < programSize / 4; i++)
	{
		uint32 temp = programCodeU32[i];
		progHash1 += (uint64)temp;
		progHash2 ^= (uint64)temp;
		progHash1 = (progHash1 << 3) | (progHash1 >> 61);
		progHash2 = (progHash2 >> 7) | (progHash2 << 57);
	}

	return progHash1 + progHash2;
}

LatteFetchShader* LatteFetchShader::FindInCacheByHash(LatteFetchShader::CacheHash fsHash)
{
	auto itr = s_fetchShaderByHash.find(fsHash);
	if (itr == s_fetchShaderByHash.end())
		return nullptr;
	return itr->second;
}

void* _getFSProgramPtr()
{
	return memory_getPointerFromPhysicalOffset(LatteGPUState.contextRegister[mmSQ_PGM_START_FS + 0] << 8);
}

uint32 _getFSProgramSize()
{
	return LatteGPUState.contextRegister[mmSQ_PGM_START_FS + 1] << 3;
}

LatteFetchShader* LatteFetchShader::FindByGPUState()
{
	uint32 fsPhysAddr24 = LatteGPUState.contextRegister[mmSQ_PGM_START_FS + 0];
	cemu_assert_debug(fsPhysAddr24 < 0x1000000);

	FetchShaderLookupInfo* lookupInfo = g_fetchShaderLookupCache.lookup(fsPhysAddr24);
	if (lookupInfo)
	{
		uint32 fsSize = _getFSProgramSize();
		uint32 framesSinceLastAccess = LatteGPUState.frameCounter - lookupInfo->lastFrameAccessed;
		if (lookupInfo->programSize == fsSize && framesSinceLastAccess == 0)
		{
			lookupInfo->lastFrameAccessed = LatteGPUState.frameCounter;
			return lookupInfo->fetchShader;
		}
		CacheHash fsHash = CalculateCacheHash(_getFSProgramPtr(), _getFSProgramSize());
		LatteFetchShader* fetchShader = FindInCacheByHash(fsHash);
		if (!fetchShader)
		{
			fetchShader = LatteShaderRecompiler_createFetchShader(fsHash, LatteGPUState.contextNew.GetRawView(), (uint32*)_getFSProgramPtr(), _getFSProgramSize());
			cemu_assert(fetchShader);
		}
		lookupInfo->fetchShader = fetchShader;
		lookupInfo->programSize = fsSize;
		lookupInfo->lastFrameAccessed = LatteGPUState.frameCounter;
		return fetchShader;
	}
	else
	{
		CacheHash fsHash = CalculateCacheHash(_getFSProgramPtr(), _getFSProgramSize());
		LatteFetchShader* fetchShader = FindInCacheByHash(fsHash);
		if (!fetchShader)
		{
			fetchShader = LatteShaderRecompiler_createFetchShader(fsHash, LatteGPUState.contextNew.GetRawView(), (uint32*)_getFSProgramPtr(), _getFSProgramSize());
			cemu_assert(fetchShader);
		}
		lookupInfo = new FetchShaderLookupInfo();
		s_fetchShaderLookupInfos.push_back(lookupInfo);
		lookupInfo->fetchShader = fetchShader;
		lookupInfo->programSize = _getFSProgramSize();
		lookupInfo->lastFrameAccessed = LatteGPUState.frameCounter;
		g_fetchShaderLookupCache.store(fsPhysAddr24, lookupInfo);
#ifdef CEMU_DEBUG_ASSERT
		cemu_assert_debug(g_fetchShaderLookupCache.lookup(fsPhysAddr24) == lookupInfo);
#endif
	}
	return lookupInfo->fetchShader;
}

FSpinlock s_spinlockFetchShaderCache;

LatteFetchShader* LatteFetchShader::RegisterInCache(CacheHash fsHash)
{
	s_spinlockFetchShaderCache.lock();
	auto itr = s_fetchShaderByHash.find(fsHash);
	if (itr != s_fetchShaderByHash.end())
	{
		LatteFetchShader* fs = itr->second;
		s_spinlockFetchShaderCache.unlock();
		return fs;
	}
	s_fetchShaderByHash.emplace(fsHash, this);
	s_spinlockFetchShaderCache.unlock();
	return nullptr;
}

void LatteFetchShader::UnregisterInCache()
{
	if (!m_isRegistered)
		return;
	s_spinlockFetchShaderCache.lock();
	auto itr = s_fetchShaderByHash.find(m_cacheHash);
	cemu_assert(itr == s_fetchShaderByHash.end());
	s_fetchShaderByHash.erase(itr);
	s_spinlockFetchShaderCache.unlock();
}

std::unordered_map<LatteFetchShader::CacheHash, LatteFetchShader*> LatteFetchShader::s_fetchShaderByHash;

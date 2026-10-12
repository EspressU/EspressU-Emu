#pragma once
#include "Cafe/HW/Latte/Core/LatteConst.h"

struct LatteParsedFetchShaderAttribute_t
{
	uint8					semanticId;
	uint8					format;
	LatteConst::VertexFetchType2	fetchType;
	uint8					nfa;
	uint8					isSigned;
	LatteConst::VertexFetchEndianMode	endianSwap;
	uint8					ds[4];
	sint32					aluDivisor;
	uint32					offset;
	uint32					attributeBufferIndex;
};

struct LatteParsedFetchShaderBufferGroup_t
{
	uint32 attributeBufferIndex{};
	LatteParsedFetchShaderAttribute_t* attrib{};
	sint32 attribCount{};
	uint32 minOffset{};
	uint32 maxOffset{};
	uint32  vboStride{};
	bool hasVtxIndexAccess{};
	bool hasInstanceIndexAccess{};

	uint32 getCurrentBufferStride(uint32* contextRegister) const;
};

struct LatteFetchShader
{
	using CacheHash = uint64;

	~LatteFetchShader();

	std::vector<LatteParsedFetchShaderBufferGroup_t> bufferGroups;
	std::vector<LatteParsedFetchShaderBufferGroup_t> bufferGroupsInvalid;

	uint64 key{};

	uint64 vkPipelineHashFragment{};

	bool mtlFetchVertexManually{};

	CacheHash m_cacheHash{};
	bool m_isRegistered{};

	void CalculateFetchShaderVkHash();

#ifdef ENABLE_METAL
	void CheckIfVerticesNeedManualFetchMtl(uint32* contextRegister);
#endif

	uint64 getVkPipelineHashFragment() const { return vkPipelineHashFragment; };

	static bool isValidBufferIndex(const uint32 index) { return index < 0x10; };

	LatteFetchShader* RegisterInCache(CacheHash fsHash);
	void UnregisterInCache();

	static CacheHash CalculateCacheHash(void* programCode, uint32 programSize);
	static LatteFetchShader* FindInCacheByHash(CacheHash fsHash);
	static LatteFetchShader* FindByGPUState();
	static void ResetLookupCache();

	static std::unordered_map<CacheHash, LatteFetchShader*> s_fetchShaderByHash;
};

LatteFetchShader* LatteShaderRecompiler_createFetchShader(LatteFetchShader::CacheHash fsHash, uint32* contextRegister, uint32* fsProgramCode, uint32 fsProgramSize);

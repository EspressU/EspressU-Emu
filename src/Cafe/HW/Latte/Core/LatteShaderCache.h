#pragma once

uint32 LatteShaderCache_getShaderCacheExtraVersion(uint64 titleId);
uint32 LatteShaderCache_getPipelineCacheExtraVersion(uint64 titleId);

struct LatteShaderCachePipelineRekey
{
	uint64 fromName1, fromName2;
	uint64 toName1, toName2;
	std::vector<uint8> data;
};

void LatteShaderCache_TranslatePipelineEntry(uint64 name1, uint64 name2, std::vector<uint8>& blob, std::vector<LatteShaderCachePipelineRekey>& rekeys);
void LatteShaderCache_ApplyPipelineRekeys(class FileCache* pipelineCache, std::vector<LatteShaderCachePipelineRekey>& rekeys);

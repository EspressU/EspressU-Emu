#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanRenderer.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/LatteTextureVk.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/RendererShaderVk.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanPipelineCompiler.h"

#include "Cafe/HW/Latte/Core/LattePerformanceMonitor.h"
#include "Cafe/HW/Latte/Core/LatteShader.h"
#include "Cafe/HW/Latte/Core/FetchShader.h"
#include "Cafe/HW/Latte/Core/LatteIndices.h"
#include "Cafe/OS/libs/gx2/GX2.h"
#include <atomic>
#include "imgui/imgui_impl_vulkan.h"
#include "Cafe/GameProfile/GameProfile.h"
#include "util/helpers/helpers.h"

extern bool hasValidFramebufferAttached;

uint64 VulkanRenderer::draw_calculateMinimalGraphicsPipelineHash(const LatteFetchShader* fetchShader, const LatteContextRegister& lcr)
{
	uint64 stateHash = 0;

	for (auto& group : fetchShader->bufferGroups)
	{
		uint32 bufferStride = group.getCurrentBufferStride(lcr.GetRawView());
		stateHash = std::rotl<uint64>(stateHash, 7);
		stateHash += bufferStride * 3;
	}

	stateHash += fetchShader->getVkPipelineHashFragment();
	stateHash = std::rotl<uint64>(stateHash, 7);

	stateHash += lcr.GetRawView()[mmVGT_PRIMITIVE_TYPE];
	stateHash = std::rotl<uint64>(stateHash, 7);

	stateHash += lcr.GetRawView()[mmVGT_STRMOUT_EN];
	stateHash = std::rotl<uint64>(stateHash, 7);

	if(lcr.PA_CL_CLIP_CNTL.get_DX_RASTERIZATION_KILL())
		stateHash += 0x333333;

	return stateHash;
}

uint64 VulkanRenderer::draw_calculateGraphicsPipelineHash(const LatteFetchShader* fetchShader, const LatteDecompilerShader* vertexShader, const LatteDecompilerShader* geometryShader, const LatteDecompilerShader* pixelShader, const VKRObjectRenderPass* renderPassObj, const LatteContextRegister& lcr)
{

	cemu_assert_debug(vertexShader->compatibleFetchShader->key == fetchShader->key);

	uint64 stateHash;
	stateHash = draw_calculateMinimalGraphicsPipelineHash(fetchShader, lcr);
	stateHash = (stateHash >> 8) + (stateHash * 0x370531ull) % 0x7F980D3BF9B4639Dull;

	uint32* ctxRegister = lcr.GetRawView();

	if (vertexShader)
		stateHash += vertexShader->baseHash;

	stateHash = std::rotl<uint64>(stateHash, 13);

	if (geometryShader)
		stateHash += geometryShader->baseHash;

	stateHash = std::rotl<uint64>(stateHash, 13);

	if (pixelShader)
		stateHash += pixelShader->baseHash + pixelShader->auxHash;

	stateHash = std::rotl<uint64>(stateHash, 13);

	uint32 polygonCtrl = lcr.PA_SU_SC_MODE_CNTL.getRawValue();
	stateHash += polygonCtrl;
	stateHash = std::rotl<uint64>(stateHash, 7);

	stateHash += ctxRegister[Latte::REGADDR::PA_CL_CLIP_CNTL];
	stateHash = std::rotl<uint64>(stateHash, 7);

	const auto colorControlReg = ctxRegister[Latte::REGADDR::CB_COLOR_CONTROL];
	stateHash += colorControlReg;

	stateHash += ctxRegister[Latte::REGADDR::CB_TARGET_MASK];

	const uint32 blendEnableMask = (colorControlReg >> 8) & 0xFF;
	if (blendEnableMask)
	{
		for (auto i = 0; i < 8; ++i)
		{
			if (((blendEnableMask & (1 << i))) == 0)
				continue;
			stateHash = std::rotl<uint64>(stateHash, 7);
			stateHash += ctxRegister[Latte::REGADDR::CB_BLEND0_CONTROL + i];
		}
	}

	stateHash += renderPassObj->m_hashForPipeline;

	uint32 depthControl = ctxRegister[Latte::REGADDR::DB_DEPTH_CONTROL];
	bool stencilTestEnable = depthControl & 1;
	if (stencilTestEnable)
	{
		stateHash += ctxRegister[mmDB_STENCILREFMASK];
		stateHash = std::rotl<uint64>(stateHash, 17);
		if(depthControl & (1<<7))
		{
			stateHash += ctxRegister[mmDB_STENCILREFMASK_BF];
			stateHash = std::rotl<uint64>(stateHash, 13);
		}
	}
	else
	{
		depthControl &= 0xFF;
	}

	stateHash = std::rotl<uint64>(stateHash, 17);
	stateHash += depthControl;

	if (polygonCtrl & (1 << 11))
	{
		stateHash += 0x1111;
	}

	return stateHash;
}

void VulkanRenderer::draw_debugPipelineHashState()
{
	cemu_assert_debug(false);
}

PipelineInfo* VulkanRenderer::draw_getCachedPipeline()
{
	const auto fetchShader = LatteSHRC_GetActiveFetchShader();
	const auto vertexShader = LatteSHRC_GetActiveVertexShader();
	const auto it = m_pipeline_info_cache.find(vertexShader->baseHash);
	if (it == m_pipeline_info_cache.cend())
		return nullptr;

	const auto geometryShader = LatteSHRC_GetActiveGeometryShader();
	const auto pixelShader = LatteSHRC_GetActivePixelShader();
	auto cachedFboVk = (CachedFBOVk*)m_state.activeFBO;

	const uint64 stateHash = draw_calculateGraphicsPipelineHash(fetchShader, vertexShader, geometryShader, pixelShader, cachedFboVk->GetRenderPassObj(), LatteGPUState.contextNew);

	const auto innerit = it->second.find(stateHash);
	if (innerit == it->second.cend())
		return nullptr;

	return innerit->second;
}

void VulkanRenderer::unregisterGraphicsPipeline(PipelineInfo* pipelineInfo)
{
	bool removedFromCache = false;
	for (auto& topMapItr : m_pipeline_info_cache)
	{
		auto& subMap = topMapItr.second;
		for (auto it = subMap.cbegin(); it != subMap.cend();)
		{
			if (it->second == pipelineInfo)
			{
				subMap.erase(it);
				removedFromCache = true;
				break;
			}
			++it;
		}
		if (removedFromCache)
			break;
	}
}

bool VulkanRenderer::IsAsyncPipelineAllowed(uint32 numIndices)
{
	if (IsTracingToolEnabled())
		return false;

	CachedFBOVk* currentFBO = m_state.activeFBO;
	auto fboExtend = currentFBO->GetExtend();

	if (fboExtend.width == 1600 && fboExtend.height == 1600)
		return false;

	if (currentFBO->hasDepthBuffer())
		return true;

	if (numIndices <= 6)
	{

		return false;
	}

	return true;
}

PipelineInfo* VulkanRenderer::draw_createGraphicsPipeline(uint32 indexCount)
{
	const auto fetchShader = LatteSHRC_GetActiveFetchShader();
	const auto vertexShader = LatteSHRC_GetActiveVertexShader();
	const auto geometryShader = LatteSHRC_GetActiveGeometryShader();
	const auto pixelShader = LatteSHRC_GetActivePixelShader();
	auto cachedFboVk = (CachedFBOVk*)m_state.activeFBO;

	uint64 minimalStateHash = draw_calculateMinimalGraphicsPipelineHash(fetchShader, LatteGPUState.contextNew);
	uint64 pipelineHash = draw_calculateGraphicsPipelineHash(fetchShader, vertexShader, geometryShader, pixelShader, cachedFboVk->GetRenderPassObj(), LatteGPUState.contextNew);

	auto vkFBO = (CachedFBOVk*)(VulkanRenderer::GetInstance()->m_state.activeFBO);
	PipelineInfo* pipelineInfo = new PipelineInfo(minimalStateHash, pipelineHash, fetchShader, vertexShader, pixelShader, geometryShader);

	uint64 vsBaseHash = vertexShader->baseHash;
	auto it = m_pipeline_info_cache.emplace(vsBaseHash, robin_hood::unordered_flat_map<uint64, PipelineInfo*>());
	auto& cache_map = it.first->second;
	cache_map.emplace(pipelineHash, pipelineInfo);

	PipelineCompiler* pipelineCompiler = new PipelineCompiler();

	bool requiresRobustBufferAccess = PipelineCompiler::CalcRobustBufferAccessRequirement(vertexShader, pixelShader, geometryShader);
	pipelineCompiler->InitFromCurrentGPUState(pipelineInfo, LatteGPUState.contextNew, vkFBO->GetRenderPassObj(), requiresRobustBufferAccess);
	pipelineCompiler->TrackAsCached(vsBaseHash, pipelineHash);

	bool allowAsyncCompile = false;
	if (GetConfig().async_compile)
		allowAsyncCompile = IsAsyncPipelineAllowed(indexCount);

	if (allowAsyncCompile)
	{
		if (pipelineCompiler->Compile(false, true, true) == false)
		{
			PipelineCompiler::CompileThreadPool_QueueCompilation(pipelineCompiler);
		}
		else
		{
			delete pipelineCompiler;
		}
	}
	else
	{
		pipelineCompiler->Compile(true, true, true);
		delete pipelineCompiler;
	}

	return pipelineInfo;
}

PipelineInfo* VulkanRenderer::draw_getOrCreateGraphicsPipeline(uint32 indexCount)
{
	auto cache_object = draw_getCachedPipeline();
	if (cache_object != nullptr)
	{

#ifdef CEMU_DEBUG_ASSERT
		cemu_assert_debug(cache_object->vertexShader == LatteSHRC_GetActiveVertexShader());
		cemu_assert_debug(cache_object->geometryShader == LatteSHRC_GetActiveGeometryShader());
		cemu_assert_debug(cache_object->pixelShader == LatteSHRC_GetActivePixelShader());
		if (cache_object->fetchShader->key != LatteSHRC_GetActiveFetchShader()->key ||
			cache_object->fetchShader->vkPipelineHashFragment != LatteSHRC_GetActiveFetchShader()->vkPipelineHashFragment)
		{
			debug_printf("Incompatible fetch shader %p %p\n", cache_object->fetchShader, LatteSHRC_GetActiveFetchShader());
			assert_dbg();
		}
		uint64 calcMinimalHash = draw_calculateMinimalGraphicsPipelineHash(LatteSHRC_GetActiveFetchShader(), LatteGPUState.contextNew);
		auto currentPrimitiveMode = LatteGPUState.contextNew.VGT_PRIMITIVE_TYPE.get_PRIMITIVE_MODE();
		cemu_assert_debug(cache_object->primitiveMode == currentPrimitiveMode);
		cemu_assert_debug(cache_object->minimalStateHash == calcMinimalHash);
#endif
		return cache_object;
	}

	return draw_createGraphicsPipeline(indexCount);
}

Renderer::IndexAllocation VulkanRenderer::indexData_reserveIndexMemory(uint32 size)
{
	VKRSynchronizedHeapAllocator::AllocatorReservation* resv = memoryManager->GetIndexAllocator().AllocateBufferMemory(size, 32);
	return { resv->memPtr, resv };
}

void VulkanRenderer::indexData_releaseIndexMemory(IndexAllocation& allocation)
{
	memoryManager->GetIndexAllocator().FreeReservation((VKRSynchronizedHeapAllocator::AllocatorReservation*)allocation.rendererInternal);
}

void VulkanRenderer::indexData_uploadIndexMemory(IndexAllocation& allocation)
{
	memoryManager->GetIndexAllocator().FlushReservation((VKRSynchronizedHeapAllocator::AllocatorReservation*)allocation.rendererInternal);
}

float s_vkUniformData[512 * 4];

uint32 VulkanRenderer::uniformData_uploadUniformDataBufferGetOffset(std::span<uint8> data)
{
	const uint32 bufferAlignmentM1 = std::max(m_featureControl.limits.minUniformBufferOffsetAlignment, m_featureControl.limits.nonCoherentAtomSize) - 1;
	const uint32 uniformSize = ((uint32)data.size() + bufferAlignmentM1) & ~bufferAlignmentM1;

	auto waitWhileCondition = [&](std::function<bool()> condition) {
		while (condition())
		{
			if (m_commandBufferSyncIndex == m_commandBufferIndex)
			{
				if (m_cmdBufferUniformRingbufIndices[m_commandBufferIndex] != m_uniformVarBufferReadIndex)
				{
					draw_endRenderPass();
					SubmitCommandBuffer();
				}
				else
				{
					cemuLog_log(LogType::Force, "draw call overflowed and corrupted uniform ringbuffer. expect visual corruption");
					cemu_assert_suspicious();
					break;
				}
			}
			WaitForNextFinishedCommandBuffer();
		}
	};

	if (m_uniformVarBufferWriteIndex + uniformSize > UNIFORMVAR_RINGBUFFER_SIZE)
	{
		waitWhileCondition([&]() {
			return m_uniformVarBufferReadIndex > m_uniformVarBufferWriteIndex || m_uniformVarBufferReadIndex == 0;
		});
		m_uniformVarBufferWriteIndex = 0;
	}

	auto ringBufRemaining = [&]() {
		ssize_t ringBufferUsedBytes = (ssize_t)m_uniformVarBufferWriteIndex - m_uniformVarBufferReadIndex;
		if (ringBufferUsedBytes < 0)
			ringBufferUsedBytes += UNIFORMVAR_RINGBUFFER_SIZE;
		return UNIFORMVAR_RINGBUFFER_SIZE - 1 - ringBufferUsedBytes;
	};
	waitWhileCondition([&]() {
		return ringBufRemaining() < uniformSize;
	});

	const uint32 uniformOffset = m_uniformVarBufferWriteIndex;
	memcpy(m_uniformVarBufferPtr + uniformOffset, data.data(), data.size());
	m_uniformVarBufferWriteIndex += uniformSize;
	if (!m_uniformVarBufferMemoryIsCoherent)
	{
		VkMappedMemoryRange flushedRange{};
		flushedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
		flushedRange.memory = m_uniformVarBufferMemory;
		flushedRange.offset = uniformOffset;
		flushedRange.size = uniformSize;
		vkFlushMappedMemoryRanges(m_logicalDevice, 1, &flushedRange);
	}
	return uniformOffset;
}

void VulkanRenderer::uniformData_updateUniformVars(uint32 shaderStageIndex, LatteDecompilerShader* shader)
{
	auto GET_UNIFORM_DATA_PTR = [](size_t index) { return s_vkUniformData + (index / 4); };

	sint32 shaderAluConst;

	switch (shader->shaderType)
	{
	case LatteConst::ShaderType::Vertex:
		shaderAluConst = 0x400;
		break;
	case LatteConst::ShaderType::Pixel:
		shaderAluConst = 0;
		break;
	case LatteConst::ShaderType::Geometry:
		shaderAluConst = 0;
		break;
	default:
		UNREACHABLE;
	}

	if (shader->resourceMapping.uniformVarsBufferBindingPoint >= 0)
	{
		if (shader->uniform.list_ufTexRescale.empty() == false)
		{
			for (auto& entry : shader->uniform.list_ufTexRescale)
			{
				float* xyScale = LatteTexture_getEffectiveTextureScale(shader->shaderType, entry.texUnit);
				memcpy(entry.currentValue, xyScale, sizeof(float) * 2);
				memcpy(GET_UNIFORM_DATA_PTR(entry.uniformLocation), xyScale, sizeof(float) * 2);
			}
		}
		if (shader->uniform.loc_alphaTestRef >= 0)
		{
			*GET_UNIFORM_DATA_PTR(shader->uniform.loc_alphaTestRef) = LatteGPUState.contextNew.SX_ALPHA_REF.get_ALPHA_TEST_REF();
		}
		if (shader->uniform.loc_pointSize >= 0)
		{
			const auto& pointSizeReg = LatteGPUState.contextNew.PA_SU_POINT_SIZE;
			float pointWidth = (float)pointSizeReg.get_WIDTH() / 8.0f;
			if (pointWidth == 0.0f)
				pointWidth = 1.0f / 8.0f;
			*GET_UNIFORM_DATA_PTR(shader->uniform.loc_pointSize) = pointWidth;
		}
		if (shader->uniform.loc_remapped >= 0)
		{
			LatteBufferCache_LoadRemappedUniforms(shader, GET_UNIFORM_DATA_PTR(shader->uniform.loc_remapped));
		}
		if (shader->uniform.loc_uniformRegister >= 0)
		{
			uint32* uniformRegData = (uint32*)(LatteGPUState.contextRegister + mmSQ_ALU_CONSTANT0_0 + shaderAluConst);
			memcpy(GET_UNIFORM_DATA_PTR(shader->uniform.loc_uniformRegister), uniformRegData, shader->uniform.count_uniformRegister * 16);
		}
		if (shader->uniform.loc_windowSpaceToClipSpaceTransform >= 0)
		{
			sint32 viewportWidth;
			sint32 viewportHeight;
			LatteRenderTarget_GetCurrentVirtualViewportSize(&viewportWidth, &viewportHeight);
			float* v = GET_UNIFORM_DATA_PTR(shader->uniform.loc_windowSpaceToClipSpaceTransform);
			v[0] = 2.0f / (float)viewportWidth;
			v[1] = 2.0f / (float)viewportHeight;
		}
		if (shader->uniform.loc_fragCoordScale >= 0)
		{
			LatteMRT::GetCurrentFragCoordScale(GET_UNIFORM_DATA_PTR(shader->uniform.loc_fragCoordScale));
		}
		if (shader->uniform.loc_verticesPerInstance >= 0)
		{
			*(int*)GET_UNIFORM_DATA_PTR(shader->uniform.loc_verticesPerInstance) = m_streamoutState.verticesPerInstance;
			for (sint32 b = 0; b < LATTE_NUM_STREAMOUT_BUFFER; b++)
			{
				if (shader->uniform.loc_streamoutBufferBase[b] >= 0)
				{
					*(uint32*)GET_UNIFORM_DATA_PTR(shader->uniform.loc_streamoutBufferBase[b]) = m_streamoutState.buffer[b].ringBufferOffset;
				}
			}
		}
		dynamicOffsetInfo.uniformVarBufferOffset[shaderStageIndex] = uniformData_uploadUniformDataBufferGetOffset({(uint8*)s_vkUniformData, shader->uniform.uniformRangeSize});
	}
}

void VulkanRenderer::draw_prepareDynamicOffsetsForDescriptorSet(uint32 shaderStageIndex, uint32* dynamicOffsets,
	sint32& numDynOffsets,
	const PipelineInfo* pipeline_info)
{
	numDynOffsets = 0;
	if (pipeline_info->dynamicOffsetInfo.hasUniformVar[shaderStageIndex])
	{
		dynamicOffsets[0] = dynamicOffsetInfo.uniformVarBufferOffset[shaderStageIndex];
		numDynOffsets++;
	}
	if (pipeline_info->dynamicOffsetInfo.hasUniformBuffers[shaderStageIndex])
	{
		for (auto& itr : pipeline_info->dynamicOffsetInfo.list_uniformBuffers[shaderStageIndex])
		{
			dynamicOffsets[numDynOffsets] = dynamicOffsetInfo.shaderUB[shaderStageIndex].uniformBufferOffset[itr];
			numDynOffsets++;
		}
	}
}

uint64 VulkanRenderer::GetDescriptorSetStateHash(LatteDecompilerShader* shader)
{
	uint64 hash = 0;

	const sint32 textureCount = shader->resourceMapping.getTextureCount();
	for (int i = 0; i < textureCount; ++i)
	{
		const auto relative_textureUnit = shader->resourceMapping.getTextureUnitFromBindingPoint(i);
		auto hostTextureUnit = relative_textureUnit;
		auto textureDim = shader->textureUnitDim[relative_textureUnit];
		auto texUnitRegIndex = hostTextureUnit * 7;
		switch (shader->shaderType)
		{
		case LatteConst::ShaderType::Vertex:
			hostTextureUnit += LATTE_CEMU_VS_TEX_UNIT_BASE;
			texUnitRegIndex += Latte::REGADDR::SQ_TEX_RESOURCE_WORD0_N_VS;
			break;
		case LatteConst::ShaderType::Pixel:
			hostTextureUnit += LATTE_CEMU_PS_TEX_UNIT_BASE;
			texUnitRegIndex += Latte::REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS;
			break;
		case LatteConst::ShaderType::Geometry:
			hostTextureUnit += LATTE_CEMU_GS_TEX_UNIT_BASE;
			texUnitRegIndex += Latte::REGADDR::SQ_TEX_RESOURCE_WORD0_N_GS;
			break;
		default:
			UNREACHABLE;
		}

		auto texture = m_state.boundTexture[hostTextureUnit];
		if (!texture)
			continue;

		const uint32 word4 = LatteGPUState.contextRegister[texUnitRegIndex + 4];

		uint32 samplerIndex = shader->textureUnitSamplerAssignment[relative_textureUnit];
		if (samplerIndex != LATTE_DECOMPILER_SAMPLER_NONE)
		{
			samplerIndex += LatteDecompiler_getTextureSamplerBaseIndex(shader->shaderType);
			hash += LatteGPUState.contextRegister[Latte::REGADDR::SQ_TEX_SAMPLER_WORD0_0 + samplerIndex * 3 + 0];
			hash = std::rotl<uint64>(hash, 7);
			hash += LatteGPUState.contextRegister[Latte::REGADDR::SQ_TEX_SAMPLER_WORD0_0 + samplerIndex * 3 + 1];
			hash = std::rotl<uint64>(hash, 7);
			hash += LatteGPUState.contextRegister[Latte::REGADDR::SQ_TEX_SAMPLER_WORD0_0 + samplerIndex * 3 + 2];
			hash = std::rotl<uint64>(hash, 7);
		}
		hash = std::rotl<uint64>(hash, 7);
		hash += (uint64)texture->GetUniqueId();
		hash = std::rotr<uint64>(hash, 21);
		hash += (uint64)(word4 & 0x0FFF0000);
	}

	return hash;
}

VkDescriptorSetInfo* VulkanRenderer::draw_getOrCreateDescriptorSet(PipelineInfo* pipeline_info, LatteDecompilerShader* shader)
{
	const uint64 stateHash = GetDescriptorSetStateHash(shader);

	VkDescriptorSetLayout descriptor_set_layout;
	switch (shader->shaderType)
	{
	case LatteConst::ShaderType::Vertex:
	{
		const auto it = pipeline_info->vertex_ds_cache.find(stateHash);
		if (it != pipeline_info->vertex_ds_cache.cend())
			return it->second;
		descriptor_set_layout = pipeline_info->m_vkrObjPipeline->m_vertexDSL;
		break;
	}
	case LatteConst::ShaderType::Pixel:
	{
		const auto it = pipeline_info->pixel_ds_cache.find(stateHash);
		if (it != pipeline_info->pixel_ds_cache.cend())
			return it->second;
		descriptor_set_layout = pipeline_info->m_vkrObjPipeline->m_pixelDSL;
		break;
	}
	case LatteConst::ShaderType::Geometry:
	{
		const auto it = pipeline_info->geometry_ds_cache.find(stateHash);
		if (it != pipeline_info->geometry_ds_cache.cend())
			return it->second;
		descriptor_set_layout = pipeline_info->m_vkrObjPipeline->m_geometryDSL;
		break;
	}
	default:
		UNREACHABLE;
	}

	VkDescriptorSetInfo* dsInfo = new VkDescriptorSetInfo();
	dsInfo->stateHash = stateHash;
	dsInfo->shaderType = shader->shaderType;
	dsInfo->pipeline_info = pipeline_info;

	dsInfo->m_vkObjDescriptorSet = new VKRObjectDescriptorSet();
	auto vkObjDS = dsInfo->m_vkObjDescriptorSet;

	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = m_descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &descriptor_set_layout;

	VkDescriptorSet result;
	if (vkAllocateDescriptorSets(m_logicalDevice, &allocInfo, &result) != VK_SUCCESS)
	{
		UnrecoverableError(fmt::format("Failed to allocate descriptor sets. Currently allocated: Descriptors={} TextureSamplers={} DynUniformBuffers={} StorageBuffers={}",
			performanceMonitor.vk.numDescriptorSets.get(),
			performanceMonitor.vk.numDescriptorSamplerTextures.get(),
			performanceMonitor.vk.numDescriptorDynUniformBuffers.get(),
			performanceMonitor.vk.numDescriptorStorageBuffers.get()
		).c_str());
	}
	vkObjDS->descriptorSet = result;

	sint32 textureCount = shader->resourceMapping.getTextureCount();

	std::vector<VkWriteDescriptorSet> descriptorWrites;
	std::vector<VkDescriptorImageInfo> textureArray;
	for (int i = 0; i < textureCount; ++i)
	{
		VkDescriptorImageInfo info{};
		const auto relative_textureUnit = shader->resourceMapping.getTextureUnitFromBindingPoint(i);
		auto hostTextureUnit = relative_textureUnit;
		auto textureDim = shader->textureUnitDim[relative_textureUnit];
		auto texUnitRegIndex = hostTextureUnit * 7;
		switch (shader->shaderType)
		{
		case LatteConst::ShaderType::Vertex:
			hostTextureUnit += LATTE_CEMU_VS_TEX_UNIT_BASE;
			texUnitRegIndex += Latte::REGADDR::SQ_TEX_RESOURCE_WORD0_N_VS;
			break;
		case LatteConst::ShaderType::Pixel:
			hostTextureUnit += LATTE_CEMU_PS_TEX_UNIT_BASE;
			texUnitRegIndex += Latte::REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS;
			break;
		case LatteConst::ShaderType::Geometry:
			hostTextureUnit += LATTE_CEMU_GS_TEX_UNIT_BASE;
			texUnitRegIndex += Latte::REGADDR::SQ_TEX_RESOURCE_WORD0_N_GS;
			break;
		default:
			UNREACHABLE;
		}

		auto textureView = m_state.boundTexture[hostTextureUnit];
		if (!textureView)
		{
			if (textureDim == Latte::E_DIM::DIM_1D)
			{
				info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
				info.imageView = nullTexture1D.view;
				info.sampler = nullTexture1D.sampler;
				textureArray.emplace_back(info);
			}
			else
			{
				info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
				info.imageView = nullTexture2D.view;
				info.sampler = nullTexture2D.sampler;
				textureArray.emplace_back(info);
			}
			continue;
		}
		if (textureDim == Latte::E_DIM::DIM_1D && (textureView->dim != Latte::E_DIM::DIM_1D))
		{
			info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
			info.imageView = nullTexture1D.view;
			info.sampler = nullTexture1D.sampler;
			textureArray.emplace_back(info);
			continue;
		}
		else if (textureDim == Latte::E_DIM::DIM_2D && (textureView->dim != Latte::E_DIM::DIM_2D && textureView->dim != Latte::E_DIM::DIM_2D_MSAA))
		{
			info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
			info.imageView = nullTexture2D.view;
			info.sampler = nullTexture2D.sampler;
			textureArray.emplace_back(info);
			continue;
		}

		info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

		VkSamplerCustomBorderColorCreateInfoEXT samplerCustomBorderColor{};

		VkSamplerCreateInfo samplerInfo{};
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

		LatteTexture* baseTexture = textureView->baseTexture;
		uint32 word4 = LatteGPUState.contextRegister[texUnitRegIndex + 4];

		auto imageViewObj = textureView->GetSamplerView(word4);
		info.imageView = imageViewObj->m_textureImageView;
		vkObjDS->addRef(imageViewObj);

		vectorAppendUnique(dsInfo->list_referencedViews, textureView);
		textureView->AddDescriptorSetReference(dsInfo);

		if (!baseTexture->IsCompressedFormat())
			vectorAppendUnique(dsInfo->list_fboCandidates, (LatteTextureVk*)baseTexture);

		uint32 stageSamplerIndex = shader->textureUnitSamplerAssignment[relative_textureUnit];
		if (stageSamplerIndex != LATTE_DECOMPILER_SAMPLER_NONE)
		{
			uint32 samplerIndex = stageSamplerIndex + LatteDecompiler_getTextureSamplerBaseIndex(shader->shaderType);
			const _LatteRegisterSetSampler* samplerWords = LatteGPUState.contextNew.SQ_TEX_SAMPLER + samplerIndex;

			uint32 iMinLOD = samplerWords->WORD1.get_MIN_LOD();
			uint32 iMaxLOD = samplerWords->WORD1.get_MAX_LOD();
			sint32 iLodBias = samplerWords->WORD1.get_LOD_BIAS();

			if (baseTexture->overwriteInfo.hasRelativeLodBias)
				iLodBias += baseTexture->overwriteInfo.relativeLodBias;
			if (baseTexture->overwriteInfo.hasLodBias)
				iLodBias = baseTexture->overwriteInfo.lodBias;

			auto filterMip = samplerWords->WORD0.get_MIP_FILTER();
			if (filterMip == Latte::LATTE_SQ_TEX_SAMPLER_WORD0_0::E_Z_FILTER::NONE)
			{
				samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
				samplerInfo.minLod = 0;
				samplerInfo.maxLod = 0.25f;
			}
			else if (filterMip == Latte::LATTE_SQ_TEX_SAMPLER_WORD0_0::E_Z_FILTER::POINT)
			{
				samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
				samplerInfo.minLod = (float)iMinLOD / 64.0f;
				samplerInfo.maxLod = (float)iMaxLOD / 64.0f;
			}
			else if (filterMip == Latte::LATTE_SQ_TEX_SAMPLER_WORD0_0::E_Z_FILTER::LINEAR)
			{
				samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
				samplerInfo.minLod = (float)iMinLOD / 64.0f;
				samplerInfo.maxLod = (float)iMaxLOD / 64.0f;
			}
			else
			{
				samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
				samplerInfo.minLod = (float)iMinLOD / 64.0f;
				samplerInfo.maxLod = (float)iMaxLOD / 64.0f;
			}

			auto filterMin = samplerWords->WORD0.get_XY_MIN_FILTER();
			cemu_assert_debug(filterMin != Latte::LATTE_SQ_TEX_SAMPLER_WORD0_0::E_XY_FILTER::BICUBIC);
			samplerInfo.minFilter = (filterMin == Latte::LATTE_SQ_TEX_SAMPLER_WORD0_0::E_XY_FILTER::POINT || filterMin == Latte::LATTE_SQ_TEX_SAMPLER_WORD0_0::E_XY_FILTER::ANISO_POINT) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;

			auto filterMag = samplerWords->WORD0.get_XY_MAG_FILTER();
			samplerInfo.magFilter = (filterMag == Latte::LATTE_SQ_TEX_SAMPLER_WORD0_0::E_XY_FILTER::POINT || filterMin == Latte::LATTE_SQ_TEX_SAMPLER_WORD0_0::E_XY_FILTER::ANISO_POINT) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;

			auto filterZ = samplerWords->WORD0.get_Z_FILTER();

			const bool hasMirrorClamp = m_featureControl.deviceExtensions.sampler_mirror_clamp_to_edge || m_featureControl.samplerMirrorClampToEdgeCore;
			if (!hasMirrorClamp)
			{
				static std::atomic_flag s_mirrorClampFallbackLogged;
				if (!s_mirrorClampFallbackLogged.test_and_set())
					cemuLog_log(LogType::Force, "Vulkan: sampler mirror-clamp is unavailable on this device, using mirrored repeat instead");
			}
			const VkSamplerAddressMode mirrorOnceLastTexel = hasMirrorClamp ? VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
			const VkSamplerAddressMode s_vkClampTable[] = {
				VK_SAMPLER_ADDRESS_MODE_REPEAT,
				VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
				VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
				mirrorOnceLastTexel,
				VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
				VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
				VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
				VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER
			};

			auto clampX = samplerWords->WORD0.get_CLAMP_X();
			auto clampY = samplerWords->WORD0.get_CLAMP_Y();
			auto clampZ = samplerWords->WORD0.get_CLAMP_Z();

			samplerInfo.addressModeU = s_vkClampTable[(size_t)clampX];
			samplerInfo.addressModeV = s_vkClampTable[(size_t)clampY];
			samplerInfo.addressModeW = s_vkClampTable[(size_t)clampZ];

			auto maxAniso = samplerWords->WORD0.get_MAX_ANISO_RATIO();

			if (baseTexture->overwriteInfo.anisotropicLevel >= 0)
				maxAniso = baseTexture->overwriteInfo.anisotropicLevel;

			if (maxAniso > 0)
			{
				samplerInfo.anisotropyEnable = VK_TRUE;
				samplerInfo.maxAnisotropy = (float)(1 << maxAniso);
			}
			else
			{
				samplerInfo.anisotropyEnable = VK_FALSE;
				samplerInfo.maxAnisotropy = 1.0f;
			}

			samplerInfo.mipLodBias = (float)iLodBias / 64.0f;

			uint8 depthCompareMode = shader->textureUsesDepthCompare[relative_textureUnit] ? 1 : 0;
			static const VkCompareOp s_vkCompareOps[]
			{
				VK_COMPARE_OP_NEVER,
				VK_COMPARE_OP_LESS,
				VK_COMPARE_OP_EQUAL,
				VK_COMPARE_OP_LESS_OR_EQUAL,
				VK_COMPARE_OP_GREATER,
				VK_COMPARE_OP_NOT_EQUAL,
				VK_COMPARE_OP_GREATER_OR_EQUAL,
				VK_COMPARE_OP_ALWAYS,
			};
			if (depthCompareMode == 1)
			{
				samplerInfo.compareEnable = VK_TRUE;
				samplerInfo.compareOp = s_vkCompareOps[(size_t)samplerWords->WORD0.get_DEPTH_COMPARE_FUNCTION()];
			}
			else
			{
				samplerInfo.compareEnable = VK_FALSE;
			}

			auto borderType = samplerWords->WORD0.get_BORDER_COLOR_TYPE();

			if (borderType == Latte::LATTE_SQ_TEX_SAMPLER_WORD0_0::E_BORDER_COLOR_TYPE::TRANSPARENT_BLACK)
				samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
			else if (borderType == Latte::LATTE_SQ_TEX_SAMPLER_WORD0_0::E_BORDER_COLOR_TYPE::OPAQUE_BLACK)
				samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
			else if (borderType == Latte::LATTE_SQ_TEX_SAMPLER_WORD0_0::E_BORDER_COLOR_TYPE::OPAQUE_WHITE)
				samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
			else
			{
				if (this->m_featureControl.deviceExtensions.custom_border_color_without_format)
				{
					samplerCustomBorderColor.sType = VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT;
					samplerCustomBorderColor.format = VK_FORMAT_UNDEFINED;

					_LatteRegisterSetSamplerBorderColor* borderColorReg;
					if (shader->shaderType == LatteConst::ShaderType::Vertex)
						borderColorReg = LatteGPUState.contextNew.TD_VS_SAMPLER_BORDER_COLOR + stageSamplerIndex;
					else if (shader->shaderType == LatteConst::ShaderType::Pixel)
						borderColorReg = LatteGPUState.contextNew.TD_PS_SAMPLER_BORDER_COLOR + stageSamplerIndex;
					else
						borderColorReg = LatteGPUState.contextNew.TD_GS_SAMPLER_BORDER_COLOR + stageSamplerIndex;
					samplerCustomBorderColor.customBorderColor.float32[0] = borderColorReg->red.get_channelValue();
					samplerCustomBorderColor.customBorderColor.float32[1] = borderColorReg->green.get_channelValue();
					samplerCustomBorderColor.customBorderColor.float32[2] = borderColorReg->blue.get_channelValue();
					samplerCustomBorderColor.customBorderColor.float32[3] = borderColorReg->alpha.get_channelValue();

					samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_CUSTOM_EXT;
					samplerInfo.pNext = &samplerCustomBorderColor;
				}
				else
				{
					samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
				}
			}
		}

		VKRObjectSampler* samplerObj = VKRObjectSampler::GetOrCreateSampler(&samplerInfo);
		vkObjDS->addRef(samplerObj);
		info.sampler = samplerObj->GetSampler();
		textureArray.emplace_back(info);
	}

	if (textureCount > 0)
	{
		for (sint32 i = 0; i < textureCount; i++)
		{
			VkWriteDescriptorSet write_descriptor{};
			write_descriptor.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write_descriptor.dstSet = result;
			write_descriptor.dstBinding = shader->resourceMapping.getTextureBaseBindingPoint() + i;
			write_descriptor.dstArrayElement = 0;
			write_descriptor.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			write_descriptor.descriptorCount = 1;
			write_descriptor.pImageInfo = textureArray.data() + i;
			descriptorWrites.emplace_back(write_descriptor);
			performanceMonitor.vk.numDescriptorSamplerTextures.increment();
			dsInfo->statsNumSamplerTextures++;
		}
	}

	VkDescriptorBufferInfo uniformVarsBufferInfo{};

	if (shader->resourceMapping.uniformVarsBufferBindingPoint >= 0)
	{
		uniformVarsBufferInfo.buffer = m_uniformVarBuffer;
		uniformVarsBufferInfo.offset = 0;
		uniformVarsBufferInfo.range = shader->uniform.uniformRangeSize;

		VkWriteDescriptorSet write_descriptor{};
		write_descriptor.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write_descriptor.dstSet = result;
		write_descriptor.dstBinding = shader->resourceMapping.uniformVarsBufferBindingPoint;
		write_descriptor.dstArrayElement = 0;
		write_descriptor.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		write_descriptor.descriptorCount = 1;
		write_descriptor.pBufferInfo = &uniformVarsBufferInfo;
		descriptorWrites.emplace_back(write_descriptor);
		performanceMonitor.vk.numDescriptorDynUniformBuffers.increment();
		dsInfo->statsNumDynUniformBuffers++;
	}

	VkDescriptorBufferInfo uniformBufferInfo{};
	uniformBufferInfo.buffer = m_useHostMemoryForCache ? m_importedMem : m_bufferCache;
	uniformBufferInfo.offset = 0;

	if (m_vendor == GfxVendor::AMD)
	{
		uniformBufferInfo.range = VK_WHOLE_SIZE;
	}
	else
	{
		uniformBufferInfo.range = 1024 * 16 * 4;
	}

	for (sint32 i = 0; i < LATTE_NUM_MAX_UNIFORM_BUFFERS; i++)
	{
		if (shader->resourceMapping.uniformBuffersBindingPoint[i] >= 0)
		{
			VkWriteDescriptorSet write_descriptor{};
			write_descriptor.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write_descriptor.dstSet = result;
			write_descriptor.dstBinding = shader->resourceMapping.uniformBuffersBindingPoint[i];
			write_descriptor.dstArrayElement = 0;
			write_descriptor.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
			write_descriptor.descriptorCount = 1;
			write_descriptor.pBufferInfo = &uniformBufferInfo;
			descriptorWrites.emplace_back(write_descriptor);
			performanceMonitor.vk.numDescriptorDynUniformBuffers.increment();
			dsInfo->statsNumDynUniformBuffers++;
		}
	}

	VkDescriptorBufferInfo tfStorageBufferInfo{};

	if (shader->resourceMapping.tfStorageBindingPoint >= 0)
	{
		tfStorageBufferInfo.buffer = m_xfbRingBuffer;
		tfStorageBufferInfo.offset = 0;
		tfStorageBufferInfo.range = VK_WHOLE_SIZE;

		VkWriteDescriptorSet write_descriptor{};
		write_descriptor.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write_descriptor.dstSet = result;
		write_descriptor.dstBinding = shader->resourceMapping.tfStorageBindingPoint;
		write_descriptor.dstArrayElement = 0;
		write_descriptor.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		write_descriptor.descriptorCount = 1;
		write_descriptor.pBufferInfo = &tfStorageBufferInfo;
		descriptorWrites.emplace_back(write_descriptor);
		performanceMonitor.vk.numDescriptorStorageBuffers.increment();
		dsInfo->statsNumStorageBuffers++;
	}

	if (!descriptorWrites.empty())
		vkUpdateDescriptorSets(m_logicalDevice, (uint32)descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);

	switch (shader->shaderType)
	{
	case LatteConst::ShaderType::Vertex:
	{
		pipeline_info->vertex_ds_cache[stateHash] = dsInfo;
		break;
	}
	case LatteConst::ShaderType::Pixel:
	{
		pipeline_info->pixel_ds_cache[stateHash] = dsInfo;
		break;
	}
	case LatteConst::ShaderType::Geometry:
	{
		pipeline_info->geometry_ds_cache[stateHash] = dsInfo;
		break;
	}
	default:
		UNREACHABLE;
	}

	return dsInfo;
}

void VulkanRenderer::sync_inputTexturesChanged()
{
	bool writeFlushRequired = false;

	if (m_state.activeVertexDS)
	{
		for (auto& tex : m_state.activeVertexDS->list_fboCandidates)
		{
			tex->m_vkFlushIndex_read = m_state.currentFlushIndex;
			if (tex->m_vkFlushIndex_write == m_state.currentFlushIndex)
				writeFlushRequired = true;
		}
	}
	if (m_state.activeGeometryDS)
	{
		for (auto& tex : m_state.activeGeometryDS->list_fboCandidates)
		{
			tex->m_vkFlushIndex_read = m_state.currentFlushIndex;
			if (tex->m_vkFlushIndex_write == m_state.currentFlushIndex)
				writeFlushRequired = true;
		}
	}
	if (m_state.activePixelDS)
	{
		for (auto& tex : m_state.activePixelDS->list_fboCandidates)
		{
			tex->m_vkFlushIndex_read = m_state.currentFlushIndex;
			if (tex->m_vkFlushIndex_write == m_state.currentFlushIndex)
				writeFlushRequired = true;
		}
	}
	if (writeFlushRequired)
	{
		VkMemoryBarrier memoryBarrier{};
		memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		memoryBarrier.srcAccessMask = 0;
		memoryBarrier.dstAccessMask = 0;

		VkPipelineStageFlags srcStage = 0;
		VkPipelineStageFlags dstStage = 0;

		srcStage |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		memoryBarrier.srcAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		srcStage |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		memoryBarrier.srcAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		dstStage |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		memoryBarrier.dstAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;

		dstStage |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		memoryBarrier.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;

		vkCmdPipelineBarrier(m_state.currentCommandBuffer, srcStage, dstStage, 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr);

		performanceMonitor.vk.numDrawBarriersPerFrame.increment();

		m_state.currentFlushIndex++;
	}
}

void VulkanRenderer::sync_RenderPassLoadTextures(CachedFBOVk* fboVk)
{
	bool readFlushRequired = false;
	for (auto& tex : fboVk->GetTextures())
	{
		LatteTextureVk* texVk = (LatteTextureVk*)tex;
		if (texVk->m_vkFlushIndex_write == m_state.currentFlushIndex)
			readFlushRequired = true;

		texVk->m_vkFlushIndex_write = m_state.currentFlushIndex;
		if (texVk->m_vkFlushIndex_read == m_state.currentFlushIndex)
			readFlushRequired = true;
	}
	if (readFlushRequired)
	{
		VkMemoryBarrier memoryBarrier{};
		memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		memoryBarrier.srcAccessMask = 0;
		memoryBarrier.dstAccessMask = 0;

		VkPipelineStageFlags srcStage = 0;
		VkPipelineStageFlags dstStage = 0;

		srcStage |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		memoryBarrier.srcAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		srcStage |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		memoryBarrier.srcAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		dstStage |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		memoryBarrier.dstAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;

		dstStage |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		memoryBarrier.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;

		vkCmdPipelineBarrier(m_state.currentCommandBuffer, srcStage, dstStage, 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr);

		performanceMonitor.vk.numDrawBarriersPerFrame.increment();

		m_state.currentFlushIndex++;
	}
}

void VulkanRenderer::sync_RenderPassStoreTextures(CachedFBOVk* fboVk)
{
	uint32 flushIndex = m_state.currentFlushIndex;
	for (auto& tex : fboVk->GetTextures())
	{
		LatteTextureVk* texVk = (LatteTextureVk*)tex;
		texVk->m_vkFlushIndex_write = flushIndex;
	}
}

void VulkanRenderer::draw_prepareDescriptorSets(PipelineInfo* pipeline_info, VkDescriptorSetInfo*& vertexDS, VkDescriptorSetInfo*& pixelDS, VkDescriptorSetInfo*& geometryDS)
{
	const auto vertexShader = LatteSHRC_GetActiveVertexShader();
	const auto geometryShader = LatteSHRC_GetActiveGeometryShader();
	const auto pixelShader = LatteSHRC_GetActivePixelShader();

	auto prepareShaderDescriptors = [this, &pipeline_info](LatteDecompilerShader* shader) -> VkDescriptorSetInfo* {
		if (!shader)
			return nullptr;
		auto descriptorSetInfo = draw_getOrCreateDescriptorSet(pipeline_info, shader);
		descriptorSetInfo->m_vkObjDescriptorSet->flagForCurrentCommandBuffer();
		return descriptorSetInfo;
	};

	vertexDS = prepareShaderDescriptors(vertexShader);
	pixelDS = prepareShaderDescriptors(pixelShader);
	geometryDS = prepareShaderDescriptors(geometryShader);
}

void VulkanRenderer::draw_updateVkBlendConstants()
{
	uint32* blendColorConstant = LatteGPUState.contextRegister + Latte::REGADDR::CB_BLEND_RED;
	vkCmdSetBlendConstants(m_state.currentCommandBuffer, (const float*)blendColorConstant);
}

void VulkanRenderer::draw_updateDepthBias(bool forceUpdate)
{
	uint32 frontScaleU32 = LatteGPUState.contextNew.PA_SU_POLY_OFFSET_FRONT_SCALE.getRawValue();
	uint32 frontOffsetU32 = LatteGPUState.contextNew.PA_SU_POLY_OFFSET_FRONT_OFFSET.getRawValue();
	uint32 offsetClampU32 = LatteGPUState.contextNew.PA_SU_POLY_OFFSET_CLAMP.getRawValue();

	if (forceUpdate == false &&
		m_state.prevPolygonFrontScaleU32 == frontScaleU32 &&
		m_state.prevPolygonFrontOffsetU32 == frontOffsetU32 &&
		m_state.prevPolygonFrontClampU32 == offsetClampU32)
		return;

	m_state.prevPolygonFrontScaleU32 = frontScaleU32;
	m_state.prevPolygonFrontOffsetU32 = frontOffsetU32;
	m_state.prevPolygonFrontClampU32 = offsetClampU32;

	float frontScale = LatteGPUState.contextNew.PA_SU_POLY_OFFSET_FRONT_SCALE.get_SCALE();
	float frontOffset = LatteGPUState.contextNew.PA_SU_POLY_OFFSET_FRONT_OFFSET.get_OFFSET();
	float offsetClamp = LatteGPUState.contextNew.PA_SU_POLY_OFFSET_CLAMP.get_CLAMP();

	frontScale /= 16.0f;

	vkCmdSetDepthBias(m_state.currentCommandBuffer, frontOffset, offsetClamp, frontScale);
}

bool s_syncOnNextDraw = false;

void VulkanRenderer::draw_setRenderPass()
{
	CachedFBOVk* fboVk = m_state.activeFBO;

	if (m_state.descriptorSetsChanged || m_state.activeRenderpassFBO != fboVk)
	{
		m_state.hasRenderSelfDependency = fboVk->CheckForCollision(m_state.activeVertexDS, m_state.activeGeometryDS, m_state.activePixelDS);
	}

	auto vkObjRenderPass = fboVk->GetRenderPassObj();
	auto vkObjFramebuffer = fboVk->GetFramebufferObj();

	bool overridePassReuse = m_state.hasRenderSelfDependency && (GetConfig().vk_accurate_barriers || m_state.activePipelineInfo->neverSkipAccurateBarrier);

	if (!overridePassReuse && m_state.activeRenderpassFBO == fboVk)
	{
		if (m_state.descriptorSetsChanged)
			sync_inputTexturesChanged();
		return;
	}
	draw_endRenderPass();
	if (m_state.descriptorSetsChanged)
		sync_inputTexturesChanged();

	m_state.hasRenderSelfDependency = fboVk->CheckForCollision(m_state.activeVertexDS, m_state.activeGeometryDS, m_state.activePixelDS);

	sync_RenderPassLoadTextures(fboVk);

	if (m_featureControl.deviceExtensions.dynamic_rendering)
	{
		vkCmdBeginRenderingKHR(m_state.currentCommandBuffer, fboVk->GetRenderingInfo());
	}
	else
	{
		VkRenderPass renderPass = vkObjRenderPass->m_renderPass;
		VkFramebuffer framebuffer = vkObjFramebuffer->m_frameBuffer;
		VkExtent2D extend = fboVk->GetExtend();

		VkRenderPassBeginInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassInfo.renderPass = renderPass;
		renderPassInfo.framebuffer = framebuffer;
		renderPassInfo.renderArea.offset = { 0, 0 };
		renderPassInfo.renderArea.extent = extend;
		renderPassInfo.clearValueCount = 0;
		vkCmdBeginRenderPass(m_state.currentCommandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
	}

	m_state.activeRenderpassFBO = fboVk;

	vkObjRenderPass->flagForCurrentCommandBuffer();
	vkObjFramebuffer->flagForCurrentCommandBuffer();

	performanceMonitor.vk.numBeginRenderpassPerFrame.increment();
}

void VulkanRenderer::draw_endRenderPass()
{
	if (!m_state.activeRenderpassFBO)
		return;
	if (m_featureControl.deviceExtensions.dynamic_rendering)
		vkCmdEndRenderingKHR(m_state.currentCommandBuffer);
	else
		vkCmdEndRenderPass(m_state.currentCommandBuffer);
	sync_RenderPassStoreTextures(m_state.activeRenderpassFBO);
	m_state.activeRenderpassFBO = nullptr;
}

void LatteDraw_handleSpecialState8_clearAsDepth();

void VulkanRenderer::draw_handleSpecialState5()
{
	LatteMRT::UpdateCurrentFBO();
	LatteRenderTarget_updateViewport();

	LatteTextureView* colorBuffer = LatteMRT::GetColorAttachment(0);
	LatteTextureView* depthBuffer = LatteMRT::GetDepthAttachment();

	sint32 vpWidth, vpHeight;
	LatteMRT::GetVirtualViewportDimensions(vpWidth, vpHeight);

	surfaceCopy_copySurfaceWithFormatConversion(
		depthBuffer->baseTexture, depthBuffer->firstMip, depthBuffer->firstSlice,
		colorBuffer->baseTexture, colorBuffer->firstMip, colorBuffer->firstSlice,
		vpWidth, vpHeight);
}

void VulkanRenderer::draw_beginSequence()
{
	m_state.drawSequenceSkip = false;

	bool streamoutEnable = LatteGPUState.contextRegister[mmVGT_STRMOUT_EN] != 0;

	LatteSHRC_UpdateActiveShaders();
	if (LatteGPUState.activeShaderHasError)
	{
		cemuLog_logDebugOnce(LogType::Force, "Skipping drawcalls due to shader error");
		m_state.drawSequenceSkip = true;
		cemu_assert_debug(false);
		return;
	}

	LatteGPUState.requiresTextureBarrier = false;
	while (true)
	{
		LatteGPUState.repeatTextureInitialization = false;
		if (!LatteMRT::UpdateCurrentFBO())
		{
			debug_printf("Rendertarget invalid\n");
			m_state.drawSequenceSkip = true;
			return;
		}

		if (!hasValidFramebufferAttached && !streamoutEnable)
		{
			debug_printf("Drawcall with no color buffer or depth buffer attached\n");
			m_state.drawSequenceSkip = true;
			return;
		}
		LatteTexture_updateTextures();
		if (!LatteGPUState.repeatTextureInitialization)
			break;
	}

	LatteMRT::ApplyCurrentState();

	LatteRenderTarget_updateViewport();
	LatteRenderTarget_updateScissorBox();

	bool rasterizerEnable = LatteGPUState.contextNew.PA_CL_CLIP_CNTL.get_DX_RASTERIZATION_KILL() == false;

	if (!LatteGPUState.contextNew.PA_CL_VTE_CNTL.get_VPORT_X_OFFSET_ENA())
		rasterizerEnable = true;

	if (rasterizerEnable == false && streamoutEnable == false)
		m_state.drawSequenceSkip = true;
}

void VulkanRenderer::draw_execute(uint32 baseVertex, uint32 baseInstance, uint32 instanceCount, uint32 count, MPTR indexDataMPTR, Latte::LATTE_VGT_DMA_INDEX_TYPE::E_INDEX_TYPE indexType, bool isFirst)
{
	if (m_state.drawSequenceSkip)
	{
		LatteGPUState.drawCallCounter++;
		return;
	}

	if (LatteGPUState.contextNew.GetSpecialStateValues()[8] != 0)
	{
		LatteDraw_handleSpecialState8_clearAsDepth();
		LatteGPUState.drawCallCounter++;
		return;
	}
	else if (LatteGPUState.contextNew.GetSpecialStateValues()[5] != 0)
	{
		draw_handleSpecialState5();
		LatteGPUState.drawCallCounter++;
		return;
	}

	m_streamoutState.verticesPerInstance = count;
	LatteStreamout_PrepareDrawcall(count, instanceCount);

	LatteDecompilerShader* vertexShader = LatteSHRC_GetActiveVertexShader();
	LatteDecompilerShader* pixelShader = LatteSHRC_GetActivePixelShader();
	LatteDecompilerShader* geometryShader = LatteSHRC_GetActiveGeometryShader();

	if (vertexShader)
		uniformData_updateUniformVars(VulkanRendererConst::SHADER_STAGE_INDEX_VERTEX, vertexShader);
	if (pixelShader)
		uniformData_updateUniformVars(VulkanRendererConst::SHADER_STAGE_INDEX_FRAGMENT, pixelShader);
	if (geometryShader)
		uniformData_updateUniformVars(VulkanRendererConst::SHADER_STAGE_INDEX_GEOMETRY, geometryShader);
	m_cmdBufferUniformRingbufIndices[m_commandBufferIndex] = m_uniformVarBufferWriteIndex;

	const LattePrimitiveMode primitiveMode = static_cast<LattePrimitiveMode>(LatteGPUState.contextRegister[mmVGT_PRIMITIVE_TYPE]);

	Renderer::INDEX_TYPE hostIndexType;
	uint32 hostIndexCount;
	uint32 indexMin = 0;
	uint32 indexMax = 0;
	Renderer::IndexAllocation indexAllocation;
	LatteIndices_decode(memory_getPointerFromVirtualOffset(indexDataMPTR), indexType, count, primitiveMode, indexMin, indexMax, hostIndexType, hostIndexCount, indexAllocation);
	VKRSynchronizedHeapAllocator::AllocatorReservation* indexReservation = (VKRSynchronizedHeapAllocator::AllocatorReservation*)indexAllocation.rendererInternal;
	bool isPrevIndexData = false;
	if (hostIndexType != INDEX_TYPE::NONE)
	{
		uint32 indexBufferIndex = indexReservation->bufferIndex;
		uint32 indexBufferOffset = indexReservation->bufferOffset;
		if (m_state.activeIndexBufferOffset != indexBufferOffset || m_state.activeIndexBufferIndex != indexBufferIndex || m_state.activeIndexType != hostIndexType)
		{
			m_state.activeIndexType = hostIndexType;
			m_state.activeIndexBufferOffset = indexBufferOffset;
			m_state.activeIndexBufferIndex = indexBufferIndex;
			VkIndexType vkType;
			if (hostIndexType == INDEX_TYPE::U16)
				vkType = VK_INDEX_TYPE_UINT16;
			else if (hostIndexType == INDEX_TYPE::U32)
				vkType = VK_INDEX_TYPE_UINT32;
			else
				cemu_assert(false);
			vkCmdBindIndexBuffer(m_state.currentCommandBuffer, indexReservation->vkBuffer, indexBufferOffset, vkType);
		}
		else
			isPrevIndexData = true;
	}

	if (m_useHostMemoryForCache)
	{
		draw_updateVertexBuffersDirectAccess();
		LatteDecompilerShader* vertexShader = LatteSHRC_GetActiveVertexShader();
		if (vertexShader)
			draw_updateUniformBuffersDirectAccess(vertexShader, mmSQ_VTX_UNIFORM_BLOCK_START, LatteConst::ShaderType::Vertex);
		LatteDecompilerShader* geometryShader = LatteSHRC_GetActiveGeometryShader();
		if (geometryShader)
			draw_updateUniformBuffersDirectAccess(geometryShader, mmSQ_GS_UNIFORM_BLOCK_START, LatteConst::ShaderType::Geometry);
		LatteDecompilerShader* pixelShader = LatteSHRC_GetActivePixelShader();
		if (pixelShader)
			draw_updateUniformBuffersDirectAccess(pixelShader, mmSQ_PS_UNIFORM_BLOCK_START, LatteConst::ShaderType::Pixel);
	}
	else
	{
		LatteBufferCache_Sync(indexMin + baseVertex, indexMax + baseVertex, baseInstance, instanceCount);
	}

	PipelineInfo* pipeline_info;

	if (!isFirst)
	{
		if (m_state.activePipelineInfo->minimalStateHash != draw_calculateMinimalGraphicsPipelineHash(vertexShader->compatibleFetchShader, LatteGPUState.contextNew))
		{
			pipeline_info = draw_getOrCreateGraphicsPipeline(count);
			m_state.activePipelineInfo = pipeline_info;
		}
		else
		{
			pipeline_info = m_state.activePipelineInfo;
#ifdef CEMU_DEBUG_ASSERT
			auto pipeline_info2 = draw_getOrCreateGraphicsPipeline(count);
			if (pipeline_info != pipeline_info2)
			{
				cemu_assert_debug(false);
			}
#endif
		}
	}
	else
	{
		pipeline_info = draw_getOrCreateGraphicsPipeline(count);
		m_state.activePipelineInfo = pipeline_info;
	}

	auto vkObjPipeline = pipeline_info->m_vkrObjPipeline;
	if (vkObjPipeline->GetPipeline() == VK_NULL_HANDLE)
	{
		m_state.activeVertexDS = nullptr;
		LatteStreamout_FinishDrawcall(m_useHostMemoryForCache);
		LatteGPUState.drawCallCounter++;
		return;
	}

	VkDescriptorSetInfo *vertexDS = nullptr, *pixelDS = nullptr, *geometryDS = nullptr;
	if (!isFirst && m_state.activeVertexDS)
	{
		vertexDS = m_state.activeVertexDS;
		pixelDS = m_state.activePixelDS;
		geometryDS = m_state.activeGeometryDS;
		m_state.descriptorSetsChanged = false;
	}
	else
	{
		draw_prepareDescriptorSets(pipeline_info, vertexDS, pixelDS, geometryDS);
		m_state.activeVertexDS = vertexDS;
		m_state.activePixelDS = pixelDS;
		m_state.activeGeometryDS = geometryDS;
		m_state.descriptorSetsChanged = true;
	}

	draw_setRenderPass();

	if (m_state.currentPipeline != vkObjPipeline->GetPipeline())
	{
		vkCmdBindPipeline(m_state.currentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vkObjPipeline->GetPipeline());
		vkObjPipeline->flagForCurrentCommandBuffer();
		m_state.currentPipeline = vkObjPipeline->GetPipeline();
		if (pipeline_info->usesDepthBias)
			draw_updateDepthBias(true);
	}
	else
	{
		if (pipeline_info->usesDepthBias)
			draw_updateDepthBias(false);
	}

	if (pipeline_info->usesBlendConstants)
		draw_updateVkBlendConstants();

	uint32_t dynamicOffsets[17 * 2];
	if (vertexDS && pixelDS)
	{
		sint32 numDynOffsetsVS;
		sint32 numDynOffsetsPS;
		draw_prepareDynamicOffsetsForDescriptorSet(VulkanRendererConst::SHADER_STAGE_INDEX_VERTEX, dynamicOffsets, numDynOffsetsVS,
			pipeline_info);
		draw_prepareDynamicOffsetsForDescriptorSet(VulkanRendererConst::SHADER_STAGE_INDEX_FRAGMENT, dynamicOffsets + numDynOffsetsVS, numDynOffsetsPS,
			pipeline_info);

		VkDescriptorSet dsArray[2];
		dsArray[0] = vertexDS->m_vkObjDescriptorSet->descriptorSet;
		dsArray[1] = pixelDS->m_vkObjDescriptorSet->descriptorSet;

		vkCmdBindDescriptorSets(m_state.currentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			vkObjPipeline->m_pipelineLayout, 0, 2, dsArray, numDynOffsetsVS + numDynOffsetsPS,
			dynamicOffsets);
	}
	else if (vertexDS)
	{
		sint32 numDynOffsets;
		draw_prepareDynamicOffsetsForDescriptorSet(VulkanRendererConst::SHADER_STAGE_INDEX_VERTEX, dynamicOffsets, numDynOffsets,
			pipeline_info);
		vkCmdBindDescriptorSets(m_state.currentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			vkObjPipeline->m_pipelineLayout, 0, 1, &vertexDS->m_vkObjDescriptorSet->descriptorSet, numDynOffsets,
			dynamicOffsets);
	}
	else if (pixelDS)
	{
		sint32 numDynOffsets;
		draw_prepareDynamicOffsetsForDescriptorSet(VulkanRendererConst::SHADER_STAGE_INDEX_FRAGMENT, dynamicOffsets, numDynOffsets,
			pipeline_info);
		vkCmdBindDescriptorSets(m_state.currentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			vkObjPipeline->m_pipelineLayout, 1, 1, &pixelDS->m_vkObjDescriptorSet->descriptorSet, numDynOffsets,
			dynamicOffsets);
	}
	if (geometryDS)
	{
		sint32 numDynOffsets;
		draw_prepareDynamicOffsetsForDescriptorSet(VulkanRendererConst::SHADER_STAGE_INDEX_GEOMETRY, dynamicOffsets, numDynOffsets,
			pipeline_info);
		vkCmdBindDescriptorSets(m_state.currentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			vkObjPipeline->m_pipelineLayout, 2, 1, &geometryDS->m_vkObjDescriptorSet->descriptorSet, numDynOffsets,
			dynamicOffsets);
	}

	if (hostIndexType != INDEX_TYPE::NONE)
		vkCmdDrawIndexed(m_state.currentCommandBuffer, hostIndexCount, instanceCount, 0, baseVertex, baseInstance);
	else
		vkCmdDraw(m_state.currentCommandBuffer, count, instanceCount, baseVertex, baseInstance);

	LatteStreamout_FinishDrawcall(m_useHostMemoryForCache);

	LatteGPUState.drawCallCounter++;
}

void VulkanRenderer::draw_updateVertexBuffersDirectAccess()
{
	LatteFetchShader* parsedFetchShader = LatteSHRC_GetActiveFetchShader();
	if (!parsedFetchShader)
		return;
	for (auto& bufferGroup : parsedFetchShader->bufferGroups)
	{
		uint32 bufferIndex = bufferGroup.attributeBufferIndex;
		uint32 bufferBaseRegisterIndex = mmSQ_VTX_ATTRIBUTE_BLOCK_START + bufferIndex * 7;
		MPTR bufferAddress = LatteGPUState.contextRegister[bufferBaseRegisterIndex + 0];
		uint32 bufferSize = LatteGPUState.contextRegister[bufferBaseRegisterIndex + 1] + 1;
		uint32 bufferStride = (LatteGPUState.contextRegister[bufferBaseRegisterIndex + 2] >> 11) & 0xFFFF;

		if (bufferAddress == MPTR_NULL) [[unlikely]]
		{
			bufferAddress = 0x10000000;
		}
		if (m_state.currentVertexBinding[bufferIndex].offset == bufferAddress)
			continue;
		cemu_assert_debug(bufferAddress < 0x50000000);
		VkBuffer attrBuffer = m_importedMem;
		VkDeviceSize attrOffset = bufferAddress - m_importedMemBaseAddress;
		vkCmdBindVertexBuffers(m_state.currentCommandBuffer, bufferIndex, 1, &attrBuffer, &attrOffset);
	}
}

void VulkanRenderer::draw_updateUniformBuffersDirectAccess(LatteDecompilerShader* shader, const uint32 uniformBufferRegOffset, LatteConst::ShaderType shaderType)
{
	if (shader->uniformMode == LATTE_DECOMPILER_UNIFORM_MODE_FULL_CBANK)
	{
		for(const auto& buf : shader->list_quickBufferList)
		{
			sint32 i = buf.index;
			MPTR physicalAddr = LatteGPUState.contextRegister[uniformBufferRegOffset + i * 7 + 0];
			uint32 uniformSize = LatteGPUState.contextRegister[uniformBufferRegOffset + i * 7 + 1] + 1;

			if (physicalAddr == MPTR_NULL) [[unlikely]]
			{
				cemu_assert_unimplemented();
				continue;
			}
			uniformSize = std::min<uint32>(uniformSize, buf.size);

			cemu_assert_debug(physicalAddr < 0x50000000);

			uint32 bufferIndex = i;
			cemu_assert_debug(bufferIndex < 16);

			switch (shaderType)
			{
			case LatteConst::ShaderType::Vertex:
				dynamicOffsetInfo.shaderUB[VulkanRendererConst::SHADER_STAGE_INDEX_VERTEX].uniformBufferOffset[bufferIndex] = physicalAddr - m_importedMemBaseAddress;
				break;
			case LatteConst::ShaderType::Geometry:
				dynamicOffsetInfo.shaderUB[VulkanRendererConst::SHADER_STAGE_INDEX_GEOMETRY].uniformBufferOffset[bufferIndex] = physicalAddr - m_importedMemBaseAddress;
				break;
			case LatteConst::ShaderType::Pixel:
				dynamicOffsetInfo.shaderUB[VulkanRendererConst::SHADER_STAGE_INDEX_FRAGMENT].uniformBufferOffset[bufferIndex] = physicalAddr - m_importedMemBaseAddress;
				break;
			default:
				UNREACHABLE;
			}
		}
	}
}

void VulkanRenderer::draw_endSequence()
{
	LatteDecompilerShader* pixelShader = LatteSHRC_GetActivePixelShader();
	if (pixelShader)
		LatteRenderTarget_trackUpdates();
	bool hasReadback = LatteTextureReadback_Update();
	m_recordedDrawcalls++;
	if (m_recordedDrawcalls >= m_submitThreshold || hasReadback)
	{
		SubmitCommandBuffer();
	}
}

void VulkanRenderer::debug_genericBarrier()
{
	draw_endRenderPass();

	VkMemoryBarrier memoryBarrier{};
	memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	memoryBarrier.srcAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT |
		VK_ACCESS_INDEX_READ_BIT |
		VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT |
		VK_ACCESS_UNIFORM_READ_BIT |
		VK_ACCESS_INPUT_ATTACHMENT_READ_BIT |
		VK_ACCESS_SHADER_READ_BIT |
		VK_ACCESS_SHADER_WRITE_BIT |
		VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
		VK_ACCESS_TRANSFER_READ_BIT |
		VK_ACCESS_TRANSFER_WRITE_BIT |
		VK_ACCESS_HOST_READ_BIT |
		VK_ACCESS_HOST_WRITE_BIT |
		VK_ACCESS_MEMORY_READ_BIT |
		VK_ACCESS_MEMORY_WRITE_BIT;

	memoryBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT |
		VK_ACCESS_INDEX_READ_BIT |
		VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT |
		VK_ACCESS_UNIFORM_READ_BIT |
		VK_ACCESS_INPUT_ATTACHMENT_READ_BIT |
		VK_ACCESS_SHADER_READ_BIT |
		VK_ACCESS_SHADER_WRITE_BIT |
		VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
		VK_ACCESS_TRANSFER_READ_BIT |
		VK_ACCESS_TRANSFER_WRITE_BIT |
		VK_ACCESS_HOST_READ_BIT |
		VK_ACCESS_HOST_WRITE_BIT |
		VK_ACCESS_MEMORY_READ_BIT |
		VK_ACCESS_MEMORY_WRITE_BIT;

	vkCmdPipelineBarrier(m_state.currentCommandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr);
}

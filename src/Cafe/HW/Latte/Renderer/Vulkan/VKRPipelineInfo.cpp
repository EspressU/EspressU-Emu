#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanRenderer.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/LatteTextureVk.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/RendererShaderVk.h"

#include "Cafe/HW/Latte/LegacyShaderDecompiler/LatteDecompiler.h"
#include "Cafe/HW/Latte/Core/LattePerformanceMonitor.h"

#include "imgui/imgui_impl_vulkan.h"
#include "imgui/imgui_extension.h"
#include "config/CemuConfig.h"

PipelineInfo::PipelineInfo(uint64 minimalStateHash, uint64 pipelineHash, LatteFetchShader* fetchShader, LatteDecompilerShader* vertexShader, LatteDecompilerShader* pixelShader, LatteDecompilerShader* geometryShader)
{
	this->minimalStateHash = minimalStateHash;
	this->stateHash = pipelineHash;

	this->fetchShader = fetchShader;
	this->vertexShader = vertexShader;
	this->geometryShader = geometryShader;
	this->pixelShader = pixelShader;

	this->vertexShaderVk = vertexShader ? (RendererShaderVk*)vertexShader->shader : nullptr;
	this->geometryShaderVk = geometryShader ? (RendererShaderVk*)geometryShader->shader : nullptr;
	this->pixelShaderVk = pixelShader ? (RendererShaderVk*)pixelShader->shader : nullptr;

	m_vkrObjPipeline = new VKRObjectPipeline();

	if (vertexShaderVk)
		vertexShaderVk->TrackDependency(this);
	if (geometryShaderVk)
		geometryShaderVk->TrackDependency(this);
	if (pixelShaderVk)
		pixelShaderVk->TrackDependency(this);

	if (pixelShader)
	{
		if (pixelShader->baseHash == 0x6f6f6e7b9aae57af && pixelShader->auxHash == 0x00078787f9249249)
			neverSkipAccurateBarrier = true;
		if (pixelShader->baseHash == 0x4c0bd596e3aef4a6 && pixelShader->auxHash == 0x003c3c3fc9269249)
			neverSkipAccurateBarrier = true;
		if (pixelShader->baseHash == 0x3a7e6b48dae31305 && pixelShader->auxHash == 0x003c3c3fc9269249)
			neverSkipAccurateBarrier = true;
	}
}

PipelineInfo::~PipelineInfo()
{
	if (rectEmulationGS)
	{
		delete rectEmulationGS;
		rectEmulationGS = nullptr;
	}

	while (!pixel_ds_cache.empty())
	{
		VkDescriptorSetInfo* dsInfo = pixel_ds_cache.begin()->second;
		delete dsInfo;
	}
	while (!geometry_ds_cache.empty())
	{
		VkDescriptorSetInfo* dsInfo = geometry_ds_cache.begin()->second;
		delete dsInfo;
	}
	while (!vertex_ds_cache.empty())
	{
		VkDescriptorSetInfo* dsInfo = vertex_ds_cache.begin()->second;
		delete dsInfo;
	}

	if (vertexShaderVk)
		vertexShaderVk->RemoveDependency(this);
	if (geometryShaderVk)
		geometryShaderVk->RemoveDependency(this);
	if (pixelShaderVk)
		pixelShaderVk->RemoveDependency(this);

	if (m_vkrObjPipeline)
	{
		VulkanRenderer::GetInstance()->ReleaseDestructibleObject(m_vkrObjPipeline);
		m_vkrObjPipeline = nullptr;
	}

	VulkanRenderer::GetInstance()->unregisterGraphicsPipeline(this);
}

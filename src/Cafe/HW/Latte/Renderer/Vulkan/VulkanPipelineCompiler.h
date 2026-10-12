#pragma once
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanRenderer.h"
#include "VKRBase.h"

class PipelineCompiler : public VKRMoveableRefCounter
{
private:
	VkFormat GetVertexFormat(uint8 format);
	bool ConsumesBlendConstants(VkBlendFactor blendFactor);

	void CreateDescriptorSetLayout(VulkanRenderer* vkRenderer, LatteDecompilerShader* shader, VkDescriptorSetLayout& layout, PipelineInfo* vkrPipelineInfo);

private:

	RendererShaderVk* m_rectEmulationGS{};
	bool m_rectNeedsGeometryShader{};

	bool InitShaderStages(VulkanRenderer* vkRenderer, RendererShaderVk* vkVertexShader, RendererShaderVk* vkPixelShader, RendererShaderVk* vkGeometryShader);

	void InitVertexInputState(const LatteContextRegister& latteRegister, LatteDecompilerShader* vertexShader, LatteFetchShader* fetchShader);
	void InitInputAssemblyState(const Latte::LATTE_VGT_PRIMITIVE_TYPE::E_PRIMITIVE_TYPE primitiveMode);
	void InitViewportState();
	void InitRasterizerState(const LatteContextRegister& latteRegister, VulkanRenderer* vkRenderer, bool isPrimitiveRect, bool& usesDepthBias);
	void InitBlendState(const LatteContextRegister& latteRegister, PipelineInfo* pipelineInfo, bool& usesBlendConstants, VKRObjectRenderPass* renderPassObj);
	void InitDescriptorSetLayouts(VulkanRenderer* vkRenderer, PipelineInfo* vkrPipelineInfo, LatteDecompilerShader* vertexShader, LatteDecompilerShader* pixelShader, LatteDecompilerShader* geometryShader);
	void InitDepthStencilState();
	void InitDynamicState(PipelineInfo* pipelineInfo, bool usesBlendConstants, bool usesDepthBias);

public:
	PipelineCompiler();
	~PipelineCompiler();

	VKRObjectPipeline* m_vkrObjPipeline{};
	LatteFetchShader* m_fetchShader{};
	RendererShaderVk* m_vkVertexShader{};
	RendererShaderVk* m_vkPixelShader{};
	RendererShaderVk* m_vkGeometryShader{};

	bool InitFromCurrentGPUState(PipelineInfo* pipelineInfo, const LatteContextRegister& latteRegister, VKRObjectRenderPass* renderPassObj, bool requireRobustBufferAccess);
	void TrackAsCached(uint64 baseHash, uint64 pipelineStateHash);

	static bool CalcRobustBufferAccessRequirement(LatteDecompilerShader* vertexShader, LatteDecompilerShader* pixelShader, LatteDecompilerShader* geometryShader);

	static void CompileThreadPool_Start();
	static void CompileThreadPool_Stop();
	static void CompileThreadPool_QueueCompilation(PipelineCompiler* v);

	VkPipelineLayout m_pipelineLayout;
	VKRObjectRenderPass* m_renderPassObj{};
	bool m_requestRobustBufferAccess{false};

	std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

	std::vector<VkVertexInputAttributeDescription> vertexInputAttributeDescription;
	std::vector<VkVertexInputBindingDescription> vertexInputBindingDescription;
	VkPipelineVertexInputStateCreateInfo vertexInputInfo{};

	VkPipelineInputAssemblyStateCreateInfo inputAssembly{};

	VkPipelineViewportStateCreateInfo viewportState{};

	VkPipelineRasterizationStateCreateInfo rasterizer{};
	VkPipelineRasterizationDepthClipStateCreateInfoEXT rasterizerExt{};
	VkPipelineMultisampleStateCreateInfo multisampling{};

	std::array<VkPipelineColorBlendAttachmentState, 8> colorBlendAttachments{};
	VkPipelineColorBlendStateCreateInfo colorBlending{};

	VkDescriptorSetLayout descriptorSetLayout[3];
	sint32 descriptorSetLayoutCount = 0;

	VkPipelineDepthStencilStateCreateInfo depthStencilState{};

	std::vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicState = {};

	bool Compile(bool forceCompile, bool isRenderThread, bool showInOverlay);

};
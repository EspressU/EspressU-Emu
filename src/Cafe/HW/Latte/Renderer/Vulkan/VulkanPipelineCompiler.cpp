#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanRenderer.h"
#if BOOST_OS_IOS
#include <sys/sysctl.h>
#endif
#include "Cafe/HW/Latte/Core/FetchShader.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanPipelineCompiler.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanPipelineStableCache.h"
#include "Cafe/HW/Latte/Core/LatteShader.h"
#include "Cafe/HW/Latte/Core/LattePerformanceMonitor.h"
#include "Cafe/OS/libs/gx2/GX2.h"
#include "config/ActiveSettings.h"
#include "util/helpers/helpers.h"
#include "util/helpers/Serializer.h"
#include "Cafe/HW/Latte/Common/RegisterSerializer.h"
#include "HW/Latte/Renderer/RendererCore.h"

RendererShaderVk* rectsEmulationGS_generate(LatteDecompilerShader* vertexShader, const LatteContextRegister& latteRegister)
{
	std::string gsSrc;

	gsSrc.append("#version 450\r\n");

	LatteShaderPSInputTable* psInputTable = LatteSHRC_GetPSInputTable();

	gsSrc.append("layout(triangles) in;\r\n");
	gsSrc.append("layout(triangle_strip) out;\r\n");
	gsSrc.append("layout(max_vertices = 4) out;\r\n");

	auto parameterMask = vertexShader->outputParameterMask;
	for (sint32 f = 0; f < 2; f++)
	{
		for (uint32 i = 0; i < 32; i++)
		{
			if ((parameterMask & (1 << i)) == 0)
				continue;
			sint32 vsSemanticId = psInputTable->getVertexShaderOutParamSemanticId(latteRegister.GetRawView(), i);
			if (vsSemanticId < 0)
				continue;
			auto psImport = psInputTable->getPSImportBySemanticId(vsSemanticId);
			if (psImport == nullptr)
				continue;

			gsSrc.append(fmt::format("layout(location = {}) ", psInputTable->getPSImportLocationBySemanticId(vsSemanticId)));
			if (psImport->isFlat)
				gsSrc.append("flat ");
			if (psImport->isNoPerspective)
				gsSrc.append("noperspective ");

			if (f == 0)
				gsSrc.append("in");
			else
				gsSrc.append("out");

			if (f == 0)
				gsSrc.append(fmt::format(" vec4 passParameterSem{}In[];\r\n", vsSemanticId));
			else
				gsSrc.append(fmt::format(" vec4 passParameterSem{}Out;\r\n", vsSemanticId));
		}
	}

	gsSrc.append("vec4 gen4thVertexA(vec4 a, vec4 b, vec4 c)\r\n");
	gsSrc.append("{\r\n");
	gsSrc.append("return b - (c - a);\r\n");
	gsSrc.append("}\r\n");

	gsSrc.append("vec4 gen4thVertexB(vec4 a, vec4 b, vec4 c)\r\n");
	gsSrc.append("{\r\n");
	gsSrc.append("return c - (b - a);\r\n");
	gsSrc.append("}\r\n");

	gsSrc.append("vec4 gen4thVertexC(vec4 a, vec4 b, vec4 c)\r\n");
	gsSrc.append("{\r\n");
	gsSrc.append("return c + (b - a);\r\n");
	gsSrc.append("}\r\n");

	gsSrc.append("void main()\r\n");
	gsSrc.append("{\r\n");

	gsSrc.append("float dist0_1 = length(gl_in[1].gl_Position.xy - gl_in[0].gl_Position.xy);\r\n");
	gsSrc.append("float dist0_2 = length(gl_in[2].gl_Position.xy - gl_in[0].gl_Position.xy);\r\n");
	gsSrc.append("float dist1_2 = length(gl_in[2].gl_Position.xy - gl_in[1].gl_Position.xy);\r\n");

	gsSrc.append("if(dist0_1 > dist0_2 && dist0_1 > dist1_2)\r\n");
	gsSrc.append("{\r\n");
	rectsEmulationGS_outputVerticesCode(gsSrc, vertexShader, psInputTable, 2, 1, 0, 3, "A", latteRegister);
	gsSrc.append("} else if ( dist0_2 > dist0_1 && dist0_2 > dist1_2 ) {\r\n");
	rectsEmulationGS_outputVerticesCode(gsSrc, vertexShader, psInputTable, 1, 2, 0, 3, "B", latteRegister);
	gsSrc.append("} else {\r\n");
	rectsEmulationGS_outputVerticesCode(gsSrc, vertexShader, psInputTable, 0, 1, 2, 3, "C", latteRegister);
	gsSrc.append("}\r\n");

	gsSrc.append("}\r\n");

	auto vkShader = new RendererShaderVk(RendererShader::ShaderType::kGeometry, 0, 0, false, false, gsSrc);
	vkShader->PreponeCompilation(true);
	return vkShader;
}

extern std::atomic_int g_compiling_pipelines;
extern std::atomic_int g_compiling_pipelines_async;
extern std::atomic_uint64_t g_compiling_pipelines_syncTimeSum;

PipelineCompiler::PipelineCompiler() {};
PipelineCompiler::~PipelineCompiler()
{
	if (m_vkrObjPipeline)
		m_vkrObjPipeline->decRef();
	if (m_renderPassObj)
		m_renderPassObj->decRef();
};

VkFormat PipelineCompiler::GetVertexFormat(uint8 format)
{
	switch (format)
	{
	case FMT_32_32_32_32_FLOAT:
		return VK_FORMAT_R32G32B32A32_UINT;
	case FMT_32_32_32_FLOAT:
		return VK_FORMAT_R32G32B32_UINT;
	case FMT_32_32_FLOAT:
		return VK_FORMAT_R32G32_UINT;
	case FMT_32_FLOAT:
		return VK_FORMAT_R32_UINT;
	case FMT_8_8_8_8:
		return VK_FORMAT_R8G8B8A8_UINT;
	case FMT_8_8_8:
		return VK_FORMAT_R8G8B8_UINT;
	case FMT_8_8:
		return VK_FORMAT_R8G8_UINT;
	case FMT_8:
		return VK_FORMAT_R8_UINT;
	case FMT_32_32_32_32:
		return VK_FORMAT_R32G32B32A32_UINT;
	case FMT_32_32_32:
		return VK_FORMAT_R32G32B32_UINT;
	case FMT_32_32:
		return VK_FORMAT_R32G32_UINT;
	case FMT_32:
		return VK_FORMAT_R32_UINT;
	case FMT_16_16_16_16:
		return VK_FORMAT_R16G16B16A16_UINT;
	case FMT_16_16_16:
		return VK_FORMAT_R16G16B16_UINT;
	case FMT_16_16:
		return VK_FORMAT_R16G16_UINT;
	case FMT_16:
		return VK_FORMAT_R16_UINT;
	case FMT_16_16_16_16_FLOAT:
		return VK_FORMAT_R16G16B16A16_UINT;
	case FMT_16_16_16_FLOAT:
		return VK_FORMAT_R16G16B16_UINT;
	case FMT_16_16_FLOAT:
		return VK_FORMAT_R16G16_UINT;
	case FMT_16_FLOAT:
		return VK_FORMAT_R16_UINT;
	case FMT_2_10_10_10:
		return VK_FORMAT_R32_UINT;
	default:
		cemuLog_log(LogType::Force, "Unsupported vertex format: {:02x}", format);
		assert_dbg();
		return VK_FORMAT_UNDEFINED;
	}
}

static VkBlendOp GetVkBlendOp(Latte::LATTE_CB_BLENDN_CONTROL::E_COMBINEFUNC combineFunc)
{
	switch (combineFunc)
	{
	case Latte::LATTE_CB_BLENDN_CONTROL::E_COMBINEFUNC::DST_PLUS_SRC:
		return VK_BLEND_OP_ADD;
	case Latte::LATTE_CB_BLENDN_CONTROL::E_COMBINEFUNC::SRC_MINUS_DST:
		return VK_BLEND_OP_SUBTRACT;
	case Latte::LATTE_CB_BLENDN_CONTROL::E_COMBINEFUNC::MIN_DST_SRC:
		return VK_BLEND_OP_MIN;
	case Latte::LATTE_CB_BLENDN_CONTROL::E_COMBINEFUNC::MAX_DST_SRC:
		return VK_BLEND_OP_MAX;
	case Latte::LATTE_CB_BLENDN_CONTROL::E_COMBINEFUNC::DST_MINUS_SRC:
		return VK_BLEND_OP_REVERSE_SUBTRACT;
	default:
		cemu_assert_suspicious();
		return VK_BLEND_OP_ADD;
	}
}

static VkBlendFactor GetVkBlendFactor(Latte::LATTE_CB_BLENDN_CONTROL::E_BLENDFACTOR factor)
{
	const VkBlendFactor factors[] =
	{
		 VK_BLEND_FACTOR_ZERO,
		 VK_BLEND_FACTOR_ONE,
		 VK_BLEND_FACTOR_SRC_COLOR,
		 VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
		 VK_BLEND_FACTOR_SRC_ALPHA,
		 VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		 VK_BLEND_FACTOR_DST_ALPHA,
		 VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
		 VK_BLEND_FACTOR_DST_COLOR,
		 VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
		 VK_BLEND_FACTOR_SRC_ALPHA_SATURATE,
		 VK_BLEND_FACTOR_MAX_ENUM,
		 VK_BLEND_FACTOR_MAX_ENUM,
		 VK_BLEND_FACTOR_CONSTANT_COLOR,
		 VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR,
		 VK_BLEND_FACTOR_SRC1_COLOR,
		 VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR,
		 VK_BLEND_FACTOR_SRC1_ALPHA,
		 VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA,
		 VK_BLEND_FACTOR_CONSTANT_ALPHA,
		 VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA
	};
	cemu_assert_debug((uint32)factor < std::size(factors));
	return factors[(uint32)factor];
}

bool PipelineCompiler::ConsumesBlendConstants(VkBlendFactor blendFactor)
{
	if (blendFactor == VK_BLEND_FACTOR_CONSTANT_COLOR ||
		blendFactor == VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR ||
		blendFactor == VK_BLEND_FACTOR_CONSTANT_ALPHA ||
		blendFactor == VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA)
		return true;
	return false;
}

void PipelineCompiler::CreateDescriptorSetLayout(VulkanRenderer* vkRenderer, LatteDecompilerShader* shader, VkDescriptorSetLayout& layout, PipelineInfo* vkrPipelineInfo)
{
	std::vector<VkDescriptorSetLayoutBinding> descriptorSetLayoutBindings;

	VkShaderStageFlags stageFlags = 0;
	uint32 stageIndex = 0;
	if (shader->shaderType == LatteConst::ShaderType::Vertex)
	{
		stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		stageIndex = VulkanRendererConst::SHADER_STAGE_INDEX_VERTEX;
	}
	else if (shader->shaderType == LatteConst::ShaderType::Pixel)
	{
		stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		stageIndex = VulkanRendererConst::SHADER_STAGE_INDEX_FRAGMENT;
	}
	else if (shader->shaderType == LatteConst::ShaderType::Geometry)
	{
		stageFlags = VK_SHADER_STAGE_GEOMETRY_BIT;
		stageIndex = VulkanRendererConst::SHADER_STAGE_INDEX_GEOMETRY;
	}

	sint32 textureBindingBase = shader->resourceMapping.getTextureBaseBindingPoint();
	if (textureBindingBase >= 0)
	{
		sint32 textureCount = shader->resourceMapping.getTextureCount();
		for (sint32 i = 0; i < textureCount; i++)
		{
			VkDescriptorSetLayoutBinding entry{};
			entry.binding = (uint32)textureBindingBase + i;
			entry.descriptorCount = 1;
			entry.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			entry.pImmutableSamplers = nullptr;
			entry.stageFlags = stageFlags;
			descriptorSetLayoutBindings.emplace_back(entry);
		}
	}

	if (shader->resourceMapping.uniformVarsBufferBindingPoint >= 0)
	{
		VkDescriptorSetLayoutBinding entry{};
		entry.binding = shader->resourceMapping.uniformVarsBufferBindingPoint;
		entry.descriptorCount = 1;
		entry.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		entry.pImmutableSamplers = nullptr;
		entry.stageFlags = stageFlags;
		descriptorSetLayoutBindings.emplace_back(entry);
	}

	for (sint32 i = 0; i < LATTE_NUM_MAX_UNIFORM_BUFFERS; i++)
	{
		if (shader->resourceMapping.uniformBuffersBindingPoint[i] >= 0)
		{
			VkDescriptorSetLayoutBinding entry{};
			entry.binding = shader->resourceMapping.uniformBuffersBindingPoint[i];
			entry.descriptorCount = 1;
			entry.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
			entry.pImmutableSamplers = nullptr;
			entry.stageFlags = stageFlags;
			descriptorSetLayoutBindings.emplace_back(entry);

			vkrPipelineInfo->dynamicOffsetInfo.list_uniformBuffers[stageIndex].emplace_back((uint8)i);
		}
	}

	if (shader->resourceMapping.tfStorageBindingPoint >= 0)
	{
		VkDescriptorSetLayoutBinding entry{};
		entry.binding = shader->resourceMapping.tfStorageBindingPoint;
		entry.descriptorCount = 1;
		entry.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		entry.pImmutableSamplers = nullptr;
		entry.stageFlags = stageFlags;
		descriptorSetLayoutBindings.emplace_back(entry);
	}

	if (shader->resourceMapping.uniformVarsBufferBindingPoint >= 0)
		vkrPipelineInfo->dynamicOffsetInfo.hasUniformVar[stageIndex] = true;
	if (shader->resourceMapping.hasUniformBuffers())
		vkrPipelineInfo->dynamicOffsetInfo.hasUniformBuffers[stageIndex] = true;

	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = descriptorSetLayoutBindings.size();
	layoutInfo.pBindings = descriptorSetLayoutBindings.data();

	if (vkCreateDescriptorSetLayout(vkRenderer->m_logicalDevice, &layoutInfo, nullptr, &layout) != VK_SUCCESS)
		vkRenderer->UnrecoverableError(fmt::format("Failed to create descriptor set layout for shader {0:#x}", shader->baseHash).c_str());
}

bool PipelineCompiler::InitShaderStages(VulkanRenderer* vkRenderer, RendererShaderVk* vkVertexShader, RendererShaderVk* vkPixelShader, RendererShaderVk* vkGeometryShader)
{
	if (!vkRenderer->m_featureControl.geometryShader && (vkGeometryShader || m_rectNeedsGeometryShader))
	{
		static std::atomic<uint32> s_skipped{0};
		const uint32 n = s_skipped.fetch_add(1) + 1;
		if (n <= 5 || n % 500 == 0)
			cemuLog_log(LogType::Force, "Vulkan: skipping a pipeline that needs a geometry shader ({}), this device has none. Skipped so far: {}", vkGeometryShader ? "game geometry shader" : "RECTS primitive emulation", n);
		return false;
	}

	cemu_assert_debug(vkVertexShader == nullptr || vkVertexShader->IsCompiled());
	cemu_assert_debug(vkPixelShader == nullptr || vkPixelShader->IsCompiled());
	cemu_assert_debug(vkGeometryShader == nullptr || vkGeometryShader->IsCompiled());

	if ((vkVertexShader && vkVertexShader->GetShaderModule() == VK_NULL_HANDLE) ||
		(vkGeometryShader && vkGeometryShader->GetShaderModule() == VK_NULL_HANDLE) ||
		(vkPixelShader && vkPixelShader->GetShaderModule() == VK_NULL_HANDLE))
	{
		cemuLog_log(LogType::Force, "Vulkan-Info: Pipeline creation failed due to invalid shader(s)");
		return false;
	}

	if (vkVertexShader)
		shaderStages.emplace_back(vkRenderer->CreatePipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, vkVertexShader->GetShaderModule(), "main"));

	if (vkGeometryShader)
		shaderStages.emplace_back(vkRenderer->CreatePipelineShaderStageCreateInfo(VK_SHADER_STAGE_GEOMETRY_BIT, vkGeometryShader->GetShaderModule(), "main"));
	else if (m_rectEmulationGS)
		shaderStages.emplace_back(vkRenderer->CreatePipelineShaderStageCreateInfo(VK_SHADER_STAGE_GEOMETRY_BIT, m_rectEmulationGS->GetShaderModule(), "main"));

	if (vkPixelShader)
		shaderStages.emplace_back(vkRenderer->CreatePipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, vkPixelShader->GetShaderModule(), "main"));

	return true;
}

static bool NarrowerVertexFormat(VkFormat format, uint32& sizeBytes, VkFormat& narrower)
{
	switch (format)
	{
	case VK_FORMAT_R32G32B32A32_UINT: sizeBytes = 16; narrower = VK_FORMAT_R32G32B32_UINT; return true;
	case VK_FORMAT_R32G32B32_UINT: sizeBytes = 12; narrower = VK_FORMAT_R32G32_UINT; return true;
	case VK_FORMAT_R32G32_UINT: sizeBytes = 8; narrower = VK_FORMAT_R32_UINT; return true;
	case VK_FORMAT_R32_UINT: sizeBytes = 4; return false;
	case VK_FORMAT_R16G16B16A16_UINT: sizeBytes = 8; narrower = VK_FORMAT_R16G16B16_UINT; return true;
	case VK_FORMAT_R16G16B16_UINT: sizeBytes = 6; narrower = VK_FORMAT_R16G16_UINT; return true;
	case VK_FORMAT_R16G16_UINT: sizeBytes = 4; narrower = VK_FORMAT_R16_UINT; return true;
	case VK_FORMAT_R16_UINT: sizeBytes = 2; return false;
	case VK_FORMAT_R8G8B8A8_UINT: sizeBytes = 4; narrower = VK_FORMAT_R8G8B8_UINT; return true;
	case VK_FORMAT_R8G8B8_UINT: sizeBytes = 3; narrower = VK_FORMAT_R8G8_UINT; return true;
	case VK_FORMAT_R8G8_UINT: sizeBytes = 2; narrower = VK_FORMAT_R8_UINT; return true;
	case VK_FORMAT_R8_UINT: sizeBytes = 1; return false;
	default: sizeBytes = 0; return false;
	}
}

void PipelineCompiler::InitVertexInputState(const LatteContextRegister& latteRegister, LatteDecompilerShader* vertexShader, LatteFetchShader* fetchShader)
{
	vertexInputAttributeDescription.reserve(16);
	vertexInputBindingDescription.reserve(fetchShader->bufferGroups.size());

	for (auto& bufferGroup : fetchShader->bufferGroups)
	{
		std::optional<LatteConst::VertexFetchType2> fetchType;
		const size_t groupFirstAttribute = vertexInputAttributeDescription.size();

		for (sint32 j = 0; j < bufferGroup.attribCount; ++j)
		{
			auto& attr = bufferGroup.attrib[j];

			uint32 semanticId = vertexShader->resourceMapping.attributeMapping[attr.semanticId];
			if (semanticId == (uint32)-1)
				continue;

			VkVertexInputAttributeDescription entry{};
			entry.location = semanticId;
			entry.offset = attr.offset;
			entry.binding = attr.attributeBufferIndex;
			entry.format = GetVertexFormat(attr.format);
			vertexInputAttributeDescription.emplace_back(entry);

			if (fetchType.has_value())
				cemu_assert_debug(fetchType == attr.fetchType);
			else
				fetchType = attr.fetchType;

			if (attr.fetchType == LatteConst::INSTANCE_DATA)
			{
				cemu_assert_debug(attr.aluDivisor == 1);
			}
		}

		uint32 bufferIndex = bufferGroup.attributeBufferIndex;
		uint32 bufferBaseRegisterIndex = mmSQ_VTX_ATTRIBUTE_BLOCK_START + bufferIndex * 7;
		uint32 bufferStride = (latteRegister.GetRawView()[bufferBaseRegisterIndex + 2] >> 11) & 0xFFFF;

		VkVertexInputBindingDescription entry{};
#if BOOST_OS_MACOS || BOOST_OS_IOS
		if (bufferStride % 4 != 0) {
			bufferStride = bufferStride + (4-(bufferStride % 4));
		}
#endif
		entry.stride = bufferStride;
		if (bufferStride != 0)
		{
			for (size_t a = groupFirstAttribute; a < vertexInputAttributeDescription.size(); ++a)
			{
				auto& attrDesc = vertexInputAttributeDescription[a];
				uint32 attrSize;
				VkFormat narrower;
				while (NarrowerVertexFormat(attrDesc.format, attrSize, narrower) && attrDesc.offset + attrSize > bufferStride)
					attrDesc.format = narrower;
			}
		}
		if (!fetchType.has_value() || fetchType == LatteConst::VertexFetchType2::VERTEX_DATA)
			entry.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		else if (fetchType == LatteConst::VertexFetchType2::INSTANCE_DATA)
			entry.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
		else
		{
			cemu_assert(false);
		}
		entry.binding = bufferIndex;
		vertexInputBindingDescription.emplace_back(entry);
	}

	vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputInfo.vertexBindingDescriptionCount = vertexInputBindingDescription.size();
	vertexInputInfo.pVertexBindingDescriptions = vertexInputBindingDescription.data();
	vertexInputInfo.vertexAttributeDescriptionCount = vertexInputAttributeDescription.size();
	vertexInputInfo.pVertexAttributeDescriptions = vertexInputAttributeDescription.data();
}

void PipelineCompiler::InitInputAssemblyState(const Latte::LATTE_VGT_PRIMITIVE_TYPE::E_PRIMITIVE_TYPE primitiveMode)
{
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.primitiveRestartEnable = VK_TRUE;
	switch (primitiveMode)
	{
	case Latte::LATTE_VGT_PRIMITIVE_TYPE::E_PRIMITIVE_TYPE::POINTS:
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
		inputAssembly.primitiveRestartEnable = false;
		break;
	case Latte::LATTE_VGT_PRIMITIVE_TYPE::E_PRIMITIVE_TYPE::LINES:
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
		inputAssembly.primitiveRestartEnable = false;
		break;
	case Latte::LATTE_VGT_PRIMITIVE_TYPE::E_PRIMITIVE_TYPE::LINE_STRIP:
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
		break;
	case Latte::LATTE_VGT_PRIMITIVE_TYPE::E_PRIMITIVE_TYPE::LINE_LOOP:
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
		break;
	case Latte::LATTE_VGT_PRIMITIVE_TYPE::E_PRIMITIVE_TYPE::LINE_STRIP_ADJACENT:
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY;
		break;
	case Latte::LATTE_VGT_PRIMITIVE_TYPE::E_PRIMITIVE_TYPE::TRIANGLES:
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		inputAssembly.primitiveRestartEnable = false;
		break;
	case Latte::LATTE_VGT_PRIMITIVE_TYPE::E_PRIMITIVE_TYPE::TRIANGLE_FAN:
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
		break;
	case Latte::LATTE_VGT_PRIMITIVE_TYPE::E_PRIMITIVE_TYPE::TRIANGLE_STRIP:
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
		break;
	case Latte::LATTE_VGT_PRIMITIVE_TYPE::E_PRIMITIVE_TYPE::QUADS:
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		inputAssembly.primitiveRestartEnable = false;
		break;
	case Latte::LATTE_VGT_PRIMITIVE_TYPE::E_PRIMITIVE_TYPE::QUAD_STRIP:
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		inputAssembly.primitiveRestartEnable = false;
		break;
	case Latte::LATTE_VGT_PRIMITIVE_TYPE::E_PRIMITIVE_TYPE::RECTS:
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		inputAssembly.primitiveRestartEnable = false;
		break;
	default:
		cemuLog_logDebug(LogType::Force, "Vulkan-Unsupported: Graphics pipeline with primitive mode {} created", primitiveMode);
		cemu_assert_debug(false);
	}
}

void PipelineCompiler::InitViewportState()
{
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;
}

void PipelineCompiler::InitRasterizerState(const LatteContextRegister& latteRegister, VulkanRenderer* vkRenderer, bool isPrimitiveRect, bool& usesDepthBias)
{
	const auto& polygonControlReg = latteRegister.PA_SU_SC_MODE_CNTL;
	const auto frontFace = polygonControlReg.get_FRONT_FACE();
	uint32 cullFront = polygonControlReg.get_CULL_FRONT();
	uint32 cullBack = polygonControlReg.get_CULL_BACK();
	uint32 polyOffsetFrontEnable = polygonControlReg.get_OFFSET_FRONT_ENABLED();

	cemu_assert_debug(LatteGPUState.contextNew.PA_CL_CLIP_CNTL.get_ZCLIP_NEAR_DISABLE() == LatteGPUState.contextNew.PA_CL_CLIP_CNTL.get_ZCLIP_FAR_DISABLE());
	bool zClipEnable = LatteGPUState.contextNew.PA_CL_CLIP_CNTL.get_ZCLIP_FAR_DISABLE() == false;

	rasterizerExt.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_DEPTH_CLIP_STATE_CREATE_INFO_EXT;
	rasterizerExt.depthClipEnable = zClipEnable;
	rasterizerExt.flags = 0;

	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.rasterizerDiscardEnable = LatteGPUState.contextNew.PA_CL_CLIP_CNTL.get_DX_RASTERIZATION_KILL();
	rasterizer.pNext = VulkanRenderer::GetInstance()->m_featureControl.deviceExtensions.depth_clip_enable ? &rasterizerExt : nullptr;
	if (!LatteGPUState.contextNew.PA_CL_VTE_CNTL.get_VPORT_X_OFFSET_ENA())
		rasterizer.rasterizerDiscardEnable = false;

	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	if (vkRenderer->m_featureControl.deviceExtensions.nv_fill_rectangle && isPrimitiveRect)
		rasterizer.polygonMode = VK_POLYGON_MODE_FILL_RECTANGLE_NV;

	rasterizer.depthClampEnable = VK_TRUE;

	rasterizer.lineWidth = 1.0f;

	usesDepthBias = polyOffsetFrontEnable;
	if (polyOffsetFrontEnable)
	{
		rasterizer.depthBiasEnable = VK_TRUE;
		rasterizer.depthBiasConstantFactor = 0.0f;
		rasterizer.depthBiasSlopeFactor = 0.0f;
		rasterizer.depthBiasClamp = 0.0f;
	}
	else
		rasterizer.depthBiasEnable = VK_FALSE;

	if (isPrimitiveRect)
	{
		if (frontFace == Latte::LATTE_PA_SU_SC_MODE_CNTL::E_FRONTFACE::CW)
			cullFront = cullBack;
		else
			cullBack = cullFront;
	}

	if (cullFront && cullBack)
		rasterizer.cullMode = VK_CULL_MODE_FRONT_AND_BACK;
	else if (cullFront)
		rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;
	else if (cullBack)
		rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
	else
		rasterizer.cullMode = VK_CULL_MODE_NONE;

	if (frontFace == Latte::LATTE_PA_SU_SC_MODE_CNTL::E_FRONTFACE::CCW)
		rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	else
		rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

	multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.sampleShadingEnable = VK_FALSE;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
}

bool _IsVkIntegerFormat(VkFormat fmt)
{
	return
		fmt == VK_FORMAT_R8_UINT || fmt == VK_FORMAT_R8_SINT ||
		fmt == VK_FORMAT_R8G8_UINT || fmt == VK_FORMAT_R8G8_SINT ||
		fmt == VK_FORMAT_R8G8B8_UINT || fmt == VK_FORMAT_R8G8B8_SINT ||
		fmt == VK_FORMAT_R8G8B8A8_UINT || fmt == VK_FORMAT_R8G8B8A8_SINT ||
		fmt == VK_FORMAT_B8G8R8A8_UINT || fmt == VK_FORMAT_B8G8R8A8_SINT ||
		fmt == VK_FORMAT_R16_UINT || fmt == VK_FORMAT_R16_SINT ||
		fmt == VK_FORMAT_R16G16_UINT || fmt == VK_FORMAT_R16G16_SINT ||
		fmt == VK_FORMAT_R16G16B16_UINT || fmt == VK_FORMAT_R16G16B16_SINT ||
		fmt == VK_FORMAT_R16G16B16A16_UINT || fmt == VK_FORMAT_R16G16B16A16_SINT ||
		fmt == VK_FORMAT_R32_UINT || fmt == VK_FORMAT_R32_SINT ||
		fmt == VK_FORMAT_R32G32_UINT || fmt == VK_FORMAT_R32G32_SINT ||
		fmt == VK_FORMAT_R32G32B32_UINT || fmt == VK_FORMAT_R32G32B32_SINT ||
		fmt == VK_FORMAT_R32G32B32A32_UINT || fmt == VK_FORMAT_R32G32B32A32_SINT;
}

void PipelineCompiler::InitBlendState(const LatteContextRegister& latteRegister, PipelineInfo* pipelineInfo, bool& usesBlendConstants, VKRObjectRenderPass* renderPassObj)
{
	const Latte::LATTE_CB_COLOR_CONTROL& colorControlReg = latteRegister.CB_COLOR_CONTROL;
	uint32 blendEnableMask = colorControlReg.get_BLEND_MASK();
	uint32 renderTargetMask = latteRegister.CB_TARGET_MASK.get_MASK();

	usesBlendConstants = false;

	for (size_t i = 0; i < colorBlendAttachments.size(); i++)
	{
		auto& entry = colorBlendAttachments[i];
		if (((blendEnableMask & (1 << i))) != 0)
			entry.blendEnable = VK_TRUE;
		else
			entry.blendEnable = VK_FALSE;

		if (entry.blendEnable != VK_FALSE && _IsVkIntegerFormat(renderPassObj->GetColorFormat(i)))
		{
			entry.blendEnable = VK_FALSE;
		}

		const auto& blendControlReg = latteRegister.CB_BLENDN_CONTROL[i];

		entry.colorWriteMask = (renderTargetMask >> (i * 4)) & 0xF;
		entry.colorBlendOp = GetVkBlendOp(blendControlReg.get_COLOR_COMB_FCN());
		entry.srcColorBlendFactor = GetVkBlendFactor(blendControlReg.get_COLOR_SRCBLEND());
		entry.dstColorBlendFactor = GetVkBlendFactor(blendControlReg.get_COLOR_DSTBLEND());
		if (blendControlReg.get_SEPARATE_ALPHA_BLEND())
		{
			entry.alphaBlendOp = GetVkBlendOp(blendControlReg.get_ALPHA_COMB_FCN());
			entry.srcAlphaBlendFactor = GetVkBlendFactor(blendControlReg.get_ALPHA_SRCBLEND());
			entry.dstAlphaBlendFactor = GetVkBlendFactor(blendControlReg.get_ALPHA_DSTBLEND());
		}
		else
		{
			entry.alphaBlendOp = entry.colorBlendOp;
			entry.srcAlphaBlendFactor = entry.srcColorBlendFactor;
			entry.dstAlphaBlendFactor = entry.dstColorBlendFactor;
		}

		usesBlendConstants |= ConsumesBlendConstants(entry.srcColorBlendFactor);
		usesBlendConstants |= ConsumesBlendConstants(entry.dstColorBlendFactor);
		usesBlendConstants |= ConsumesBlendConstants(entry.srcAlphaBlendFactor);
		usesBlendConstants |= ConsumesBlendConstants(entry.dstAlphaBlendFactor);
	}

	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;

	const auto logicOp = colorControlReg.get_ROP();
	if (logicOp == Latte::LATTE_CB_COLOR_CONTROL::E_LOGICOP::COPY)
	{
		colorBlending.logicOpEnable = VK_FALSE;
		colorBlending.logicOp = VK_LOGIC_OP_COPY;
	}
	else
	{
		colorBlending.logicOpEnable = VK_TRUE;
		switch (logicOp)
		{
		case Latte::LATTE_CB_COLOR_CONTROL::E_LOGICOP::SET:
			colorBlending.logicOp = VK_LOGIC_OP_SET;
			break;
		case Latte::LATTE_CB_COLOR_CONTROL::E_LOGICOP::CLEAR:
			colorBlending.logicOp = VK_LOGIC_OP_CLEAR;
			break;
		case Latte::LATTE_CB_COLOR_CONTROL::E_LOGICOP::OR:
			colorBlending.logicOp = VK_LOGIC_OP_OR;
			break;
		default:
			colorBlending.logicOp = VK_LOGIC_OP_COPY;
			cemu_assert_unimplemented();
		}
	}

	colorBlending.attachmentCount = colorBlendAttachments.size();
	colorBlending.pAttachments = colorBlendAttachments.data();

	colorBlending.blendConstants[0] = 0;
	colorBlending.blendConstants[1] = 0;
	colorBlending.blendConstants[2] = 0;
	colorBlending.blendConstants[3] = 0;
}

void PipelineCompiler::InitDescriptorSetLayouts(VulkanRenderer* vkRenderer, PipelineInfo* vkrPipelineInfo, LatteDecompilerShader* vertexShader, LatteDecompilerShader* pixelShader, LatteDecompilerShader* geometryShader)
{
	auto vkObjPipeline = vkrPipelineInfo->m_vkrObjPipeline;

	if (vertexShader)
	{
		cemu_assert_debug(descriptorSetLayoutCount == 0);
		CreateDescriptorSetLayout(vkRenderer, vertexShader, descriptorSetLayout[descriptorSetLayoutCount], vkrPipelineInfo);
		vkObjPipeline->m_vertexDSL = descriptorSetLayout[descriptorSetLayoutCount];
		descriptorSetLayoutCount++;
	}

	if (pixelShader)
	{
		cemu_assert_debug(descriptorSetLayoutCount == 1);
		CreateDescriptorSetLayout(vkRenderer, pixelShader, descriptorSetLayout[descriptorSetLayoutCount], vkrPipelineInfo);
		vkObjPipeline->m_pixelDSL = descriptorSetLayout[descriptorSetLayoutCount];
		descriptorSetLayoutCount++;
	}
	else if (geometryShader)
	{
		VkDescriptorSetLayoutCreateInfo layoutInfo = {};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = 0;
		layoutInfo.pBindings = nullptr;
		if (vkCreateDescriptorSetLayout(vkRenderer->m_logicalDevice, &layoutInfo, nullptr, &descriptorSetLayout[descriptorSetLayoutCount]) != VK_SUCCESS)
			vkRenderer->UnrecoverableError(fmt::format("Failed to create placeholder descriptor set layout for shader {0:#x}", geometryShader->baseHash).c_str());
		descriptorSetLayoutCount++;
	}

	if (geometryShader)
	{
		cemu_assert_debug(descriptorSetLayoutCount == 2);
		CreateDescriptorSetLayout(vkRenderer, geometryShader, descriptorSetLayout[descriptorSetLayoutCount], vkrPipelineInfo);
		vkObjPipeline->m_geometryDSL = descriptorSetLayout[descriptorSetLayoutCount];
		descriptorSetLayoutCount++;
	}
}

void PipelineCompiler::InitDepthStencilState()
{
	bool depthEnable = LatteGPUState.contextNew.DB_DEPTH_CONTROL.get_Z_ENABLE();
	auto depthFunc = LatteGPUState.contextNew.DB_DEPTH_CONTROL.get_Z_FUNC();
	bool depthWriteEnable = LatteGPUState.contextNew.DB_DEPTH_CONTROL.get_Z_WRITE_ENABLE();

	depthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencilState.depthTestEnable = depthEnable ? VK_TRUE : VK_FALSE;
	depthStencilState.depthWriteEnable = depthWriteEnable ? VK_TRUE : VK_FALSE;

	static const VkCompareOp vkDepthCompareTable[8] =
	{
		VK_COMPARE_OP_NEVER,
		VK_COMPARE_OP_LESS,
		VK_COMPARE_OP_EQUAL,
		VK_COMPARE_OP_LESS_OR_EQUAL,
		VK_COMPARE_OP_GREATER,
		VK_COMPARE_OP_NOT_EQUAL,
		VK_COMPARE_OP_GREATER_OR_EQUAL,
		VK_COMPARE_OP_ALWAYS
	};

	depthStencilState.depthCompareOp = vkDepthCompareTable[(size_t)depthFunc];

	depthStencilState.depthBoundsTestEnable = false;
	depthStencilState.minDepthBounds = 0.0f;
	depthStencilState.maxDepthBounds = 1.0f;

	bool stencilEnable = LatteGPUState.contextNew.DB_DEPTH_CONTROL.get_STENCIL_ENABLE();
	bool backStencilEnable = LatteGPUState.contextNew.DB_DEPTH_CONTROL.get_BACK_STENCIL_ENABLE();
	auto frontStencilFunc = LatteGPUState.contextNew.DB_DEPTH_CONTROL.get_STENCIL_FUNC_F();
	auto frontStencilZPass = LatteGPUState.contextNew.DB_DEPTH_CONTROL.get_STENCIL_ZPASS_F();
	auto frontStencilZFail = LatteGPUState.contextNew.DB_DEPTH_CONTROL.get_STENCIL_ZFAIL_F();
	auto frontStencilFail = LatteGPUState.contextNew.DB_DEPTH_CONTROL.get_STENCIL_FAIL_F();
	auto backStencilFunc = LatteGPUState.contextNew.DB_DEPTH_CONTROL.get_STENCIL_FUNC_B();
	auto backStencilZPass = LatteGPUState.contextNew.DB_DEPTH_CONTROL.get_STENCIL_ZPASS_B();
	auto backStencilZFail = LatteGPUState.contextNew.DB_DEPTH_CONTROL.get_STENCIL_ZFAIL_B();
	auto backStencilFail = LatteGPUState.contextNew.DB_DEPTH_CONTROL.get_STENCIL_FAIL_B();
	uint32 stencilCompareMaskFront = LatteGPUState.contextNew.DB_STENCILREFMASK.get_STENCILMASK_F();
	uint32 stencilWriteMaskFront = LatteGPUState.contextNew.DB_STENCILREFMASK.get_STENCILWRITEMASK_F();
	uint32 stencilRefFront = LatteGPUState.contextNew.DB_STENCILREFMASK.get_STENCILREF_F();
	uint32 stencilCompareMaskBack = LatteGPUState.contextNew.DB_STENCILREFMASK_BF.get_STENCILMASK_B();
	uint32 stencilWriteMaskBack = LatteGPUState.contextNew.DB_STENCILREFMASK_BF.get_STENCILWRITEMASK_B();
	uint32 stencilRefBack = LatteGPUState.contextNew.DB_STENCILREFMASK_BF.get_STENCILREF_B();

	static const VkStencilOp stencilOpTable[8] = {
		VK_STENCIL_OP_KEEP,
		VK_STENCIL_OP_ZERO,
		VK_STENCIL_OP_REPLACE,
		VK_STENCIL_OP_INCREMENT_AND_CLAMP,
		VK_STENCIL_OP_DECREMENT_AND_CLAMP,
		VK_STENCIL_OP_INVERT,
		VK_STENCIL_OP_INCREMENT_AND_WRAP,
		VK_STENCIL_OP_DECREMENT_AND_WRAP
	};

	depthStencilState.stencilTestEnable = stencilEnable ? VK_TRUE : VK_FALSE;

	depthStencilState.front.reference = stencilRefFront;
	depthStencilState.front.compareMask = stencilCompareMaskFront;
	depthStencilState.front.writeMask = stencilWriteMaskFront;
	depthStencilState.front.compareOp = vkDepthCompareTable[(size_t)frontStencilFunc];
	depthStencilState.front.depthFailOp = stencilOpTable[(size_t)frontStencilZFail];
	depthStencilState.front.failOp = stencilOpTable[(size_t)frontStencilFail];
	depthStencilState.front.passOp = stencilOpTable[(size_t)frontStencilZPass];

	if (backStencilEnable)
	{
		depthStencilState.back.reference = stencilRefBack;
		depthStencilState.back.compareMask = stencilCompareMaskBack;
		depthStencilState.back.writeMask = stencilWriteMaskBack;
		depthStencilState.back.compareOp = vkDepthCompareTable[(size_t)backStencilFunc];
		depthStencilState.back.depthFailOp = stencilOpTable[(size_t)backStencilZFail];
		depthStencilState.back.failOp = stencilOpTable[(size_t)backStencilFail];
		depthStencilState.back.passOp = stencilOpTable[(size_t)backStencilZPass];
	}
	else
	{
		depthStencilState.back.reference = stencilRefFront;
		depthStencilState.back.compareMask = stencilCompareMaskFront;
		depthStencilState.back.writeMask = stencilWriteMaskFront;
		depthStencilState.back.compareOp = vkDepthCompareTable[(size_t)frontStencilFunc];
		depthStencilState.back.depthFailOp = stencilOpTable[(size_t)frontStencilZFail];
		depthStencilState.back.failOp = stencilOpTable[(size_t)frontStencilFail];
		depthStencilState.back.passOp = stencilOpTable[(size_t)frontStencilZPass];
	}
}

void PipelineCompiler::InitDynamicState(PipelineInfo* pipelineInfo, bool usesBlendConstants, bool usesDepthBias)
{

	if (usesBlendConstants)
	{
		dynamicStates.emplace_back(VK_DYNAMIC_STATE_BLEND_CONSTANTS);
		pipelineInfo->usesBlendConstants = true;
	}
	if (usesDepthBias)
	{
		dynamicStates.emplace_back(VK_DYNAMIC_STATE_DEPTH_BIAS);
		pipelineInfo->usesDepthBias = true;
	}

	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount = dynamicStates.size();
	dynamicState.pDynamicStates = dynamicStates.data();
}

bool PipelineCompiler::InitFromCurrentGPUState(PipelineInfo* pipelineInfo, const LatteContextRegister& latteRegister, VKRObjectRenderPass* renderPassObj, bool requireRobustBufferAccess)
{
	VulkanRenderer* vkRenderer = VulkanRenderer::GetInstance();

	bool isPrimitiveRect = false;
	const auto primitiveMode = latteRegister.VGT_PRIMITIVE_TYPE.get_PRIMITIVE_MODE();
	isPrimitiveRect = (primitiveMode == Latte::LATTE_VGT_PRIMITIVE_TYPE::E_PRIMITIVE_TYPE::RECTS);

	m_fetchShader = pipelineInfo->fetchShader;
	m_vkVertexShader = pipelineInfo->vertexShaderVk;
	m_vkPixelShader = pipelineInfo->pixelShaderVk;
	m_vkGeometryShader = pipelineInfo->geometryShaderVk;
	m_vkrObjPipeline = pipelineInfo->m_vkrObjPipeline;
	m_renderPassObj = renderPassObj;
	m_requestRobustBufferAccess = requireRobustBufferAccess;

	if (!vkRenderer->m_featureControl.deviceExtensions.nv_fill_rectangle && isPrimitiveRect)
	{
		if (!vkRenderer->m_featureControl.geometryShader)
		{
			m_rectNeedsGeometryShader = true;
		}
		else
		{
			cemu_assert(m_vkGeometryShader == nullptr);
			m_rectEmulationGS = rectsEmulationGS_generate(pipelineInfo->vertexShader, latteRegister);
			pipelineInfo->rectEmulationGS = m_rectEmulationGS;
		}
	}

	pipelineInfo->primitiveMode = primitiveMode;
	InitVertexInputState(latteRegister, pipelineInfo->vertexShader, pipelineInfo->fetchShader);
	InitInputAssemblyState(primitiveMode);
	InitViewportState();
	bool usesDepthBias = false;
	InitRasterizerState(latteRegister, vkRenderer, isPrimitiveRect, usesDepthBias);
	bool usesBlendConstants = false;
	InitBlendState(latteRegister, pipelineInfo, usesBlendConstants, renderPassObj);
	InitDescriptorSetLayouts(vkRenderer, pipelineInfo, pipelineInfo->vertexShader, pipelineInfo->pixelShader, pipelineInfo->geometryShader);

	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = descriptorSetLayoutCount;
	pipelineLayoutInfo.pSetLayouts = descriptorSetLayout;
	pipelineLayoutInfo.pPushConstantRanges = nullptr;
	pipelineLayoutInfo.pushConstantRangeCount = 0;

	VkResult result = vkCreatePipelineLayout(vkRenderer->m_logicalDevice, &pipelineLayoutInfo, nullptr, &m_pipelineLayout);
	if (result != VK_SUCCESS)
	{
		cemuLog_log(LogType::Force, "Failed to create pipeline layout: {}", result);
		return false;
	}

	InitDepthStencilState();

	InitDynamicState(pipelineInfo, usesBlendConstants, usesDepthBias);

	pipelineInfo->m_vkrObjPipeline->m_pipelineLayout = m_pipelineLayout;

	m_vkrObjPipeline->incRef();
	m_renderPassObj->incRef();
	return true;
}

bool PipelineCompiler::Compile(bool forceCompile, bool isRenderThread, bool showInOverlay)
{
	VulkanRenderer* vkRenderer = VulkanRenderer::GetInstance();

	if (!vkRenderer->m_featureControl.deviceExtensions.pipeline_creation_cache_control)
		forceCompile = true;

	if (!forceCompile)
	{
		if (m_vkVertexShader && m_vkVertexShader->IsCompiled() == false)
			return false;
		if (m_vkPixelShader && m_vkPixelShader->IsCompiled() == false)
			return false;
		if (m_vkGeometryShader && m_vkGeometryShader->IsCompiled() == false)
			return false;
	}
	else
	{
		if (m_vkVertexShader && m_vkVertexShader->IsCompiled() == false)
			m_vkVertexShader->PreponeCompilation(isRenderThread);
		if (m_vkPixelShader && m_vkPixelShader->IsCompiled() == false)
			m_vkPixelShader->PreponeCompilation(isRenderThread);
		if (m_vkGeometryShader && m_vkGeometryShader->IsCompiled() == false)
			m_vkGeometryShader->PreponeCompilation(isRenderThread);
	}

	if (shaderStages.empty())
	{
		if (!InitShaderStages(vkRenderer, m_vkVertexShader, m_vkPixelShader, m_vkGeometryShader))
			return true;
	}

	VkGraphicsPipelineCreateInfo pipelineInfo{};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.stageCount = shaderStages.size();
	pipelineInfo.pStages = shaderStages.data();
	pipelineInfo.pVertexInputState = &vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pDynamicState = &dynamicState;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisampling;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.layout = m_pipelineLayout;
	pipelineInfo.renderPass = m_renderPassObj->m_renderPass;
	pipelineInfo.pDepthStencilState = &depthStencilState;
	pipelineInfo.subpass = 0;
	pipelineInfo.basePipelineHandle = nullptr;
	pipelineInfo.flags = 0;
	if (!forceCompile)
		pipelineInfo.flags |= VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT_EXT;

	void* prevStruct = nullptr;

	VkPipelineCreationFeedbackCreateInfoEXT creationFeedbackInfo;
	VkPipelineCreationFeedbackEXT creationFeedback;
	boost::container::static_vector<VkPipelineCreationFeedbackEXT, 3> creationStageFeedback;
	if (vkRenderer->m_featureControl.deviceExtensions.pipeline_feedback)
	{
		creationFeedback = {};
		creationFeedback.flags = VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT_EXT;

		for (uint32_t i = 0; i < pipelineInfo.stageCount; ++i)
			creationStageFeedback.push_back({VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT_EXT, 0});

		creationFeedbackInfo = {};
		creationFeedbackInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CREATION_FEEDBACK_CREATE_INFO_EXT;
		creationFeedbackInfo.pPipelineCreationFeedback = &creationFeedback;
		creationFeedbackInfo.pPipelineStageCreationFeedbacks = creationStageFeedback.data();
		creationFeedbackInfo.pipelineStageCreationFeedbackCount = pipelineInfo.stageCount;
		creationFeedbackInfo.pNext = prevStruct;
		prevStruct = &creationFeedbackInfo;
	}

	VkPipelineRobustnessCreateInfoEXT pipelineRobustnessCreateInfo{};
	#if BOOST_OS_MACOS || BOOST_OS_IOS
	const bool wantRobustAccess = true;
#else
	const bool wantRobustAccess = m_requestRobustBufferAccess;
#endif
	if (vkRenderer->m_featureControl.deviceExtensions.pipeline_robustness && wantRobustAccess)
	{
		pipelineRobustnessCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_ROBUSTNESS_CREATE_INFO_EXT;
		pipelineRobustnessCreateInfo.pNext = prevStruct;
		prevStruct = &pipelineRobustnessCreateInfo;
		pipelineRobustnessCreateInfo.storageBuffers = VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_EXT;
		pipelineRobustnessCreateInfo.uniformBuffers = VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_EXT;
#if BOOST_OS_MACOS || BOOST_OS_IOS
		pipelineRobustnessCreateInfo.vertexInputs = VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_EXT;
#else
		pipelineRobustnessCreateInfo.vertexInputs = VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DEVICE_DEFAULT_EXT;
#endif
		pipelineRobustnessCreateInfo.images = VK_PIPELINE_ROBUSTNESS_IMAGE_BEHAVIOR_DEVICE_DEFAULT_EXT;
	}

	pipelineInfo.pNext = prevStruct;

	VkPipeline pipeline = VK_NULL_HANDLE;
	VkResult result;
	uint8 retryCount = 0;
	while (retryCount < 3)
	{
		std::shared_lock lock(vkRenderer->m_pipeline_cache_save_mutex);
		result = vkCreateGraphicsPipelines(vkRenderer->m_logicalDevice, vkRenderer->m_pipeline_cache, 1, &pipelineInfo, nullptr, &pipeline);
		lock.unlock();
		if (result != VK_ERROR_OUT_OF_DEVICE_MEMORY)
			break;
		retryCount++;
	}

	if (result == VK_ERROR_PIPELINE_COMPILE_REQUIRED_EXT)
	{
		return false;
	}
	else if (result == VK_SUCCESS)
	{
		m_vkrObjPipeline->SetPipeline(pipeline);
	}
	else
	{
		cemuLog_log(LogType::Force, "Failed to create graphics pipeline. Error {}", (sint32)result);
		cemu_assert_debug(false);
		return true;
	}
	vkRenderer->m_pipeline_cache_semaphore.notify();

	if (vkRenderer->m_featureControl.deviceExtensions.pipeline_feedback)
	{
		if (HAS_FLAG(creationFeedback.flags, VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT_EXT))
		{
			bool hasCacheHit = HAS_FLAG(creationFeedback.flags, VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT_EXT);
			if (!hasCacheHit)
			{
				if (showInOverlay)
				{
					if (isRenderThread)
						g_compiling_pipelines_syncTimeSum += creationFeedback.duration;
					else
						g_compiling_pipelines_async++;
					g_compiling_pipelines++;
				}
			}
		}
	}
	return true;
}

void PipelineCompiler::TrackAsCached(uint64 baseHash, uint64 pipelineStateHash)
{
	auto& pipelineCache = VulkanPipelineStableCache::GetInstance();
	if (pipelineCache.HasPipelineCached(baseHash, pipelineStateHash))
		return;
	pipelineCache.AddCurrentStateToCache(baseHash, pipelineStateHash);
}

bool PipelineCompiler::CalcRobustBufferAccessRequirement(LatteDecompilerShader* vertexShader, LatteDecompilerShader* pixelShader, LatteDecompilerShader* geometryShader)
{
	bool requiresRobustBufferAcces = false;
	if (vertexShader)
	{
		cemu_assert_debug(vertexShader->shaderType == LatteConst::ShaderType::Vertex);
		requiresRobustBufferAcces |= vertexShader->hasStreamoutBufferWrite;
		requiresRobustBufferAcces |= vertexShader->uniformMode == LATTE_DECOMPILER_UNIFORM_MODE_FULL_CBANK;
	}
	if (geometryShader)
	{
		cemu_assert_debug(geometryShader->shaderType == LatteConst::ShaderType::Geometry);
		requiresRobustBufferAcces |= geometryShader->hasStreamoutBufferWrite;
		requiresRobustBufferAcces |= geometryShader->uniformMode == LATTE_DECOMPILER_UNIFORM_MODE_FULL_CBANK;
	}
	if (pixelShader)
	{
		cemu_assert_debug(pixelShader->shaderType == LatteConst::ShaderType::Pixel);
		requiresRobustBufferAcces |= pixelShader->uniformMode == LATTE_DECOMPILER_UNIFORM_MODE_FULL_CBANK;
	}
	return requiresRobustBufferAcces;
}

static std::vector<std::thread> s_compileThreads;
static std::atomic_bool s_compileThreadsShutdownSignal{};
static ConcurrentQueue<PipelineCompiler*> s_pipelineCompileRequests;

static void compilePipeline_thread(sint32 threadIndex)
{
	SetThreadName("compilePl");
#ifdef _WIN32
	if(threadIndex != 0)
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
	while (!s_compileThreadsShutdownSignal)
	{
		PipelineCompiler* request = s_pipelineCompileRequests.pop();
		if (!request)
			continue;
		request->Compile(true, false, true);
		delete request;
	}
}

void PipelineCompiler::CompileThreadPool_Start()
{
	cemu_assert_debug(s_compileThreads.empty());
	s_compileThreadsShutdownSignal = false;
	uint32 numCompileThreads;

	uint32 cpuCoreCount = GetPhysicalCoreCount();
	if (cpuCoreCount <= 2)
		numCompileThreads = 1;
	else
		numCompileThreads = 2 + (cpuCoreCount - 3);

	numCompileThreads = std::min(numCompileThreads, 8u);
#if BOOST_OS_IOS
	// Each pipeline being compiled holds its SPIR-V, the MSL MoltenVK makes from it and the Metal
	// compiler's reply in this process. On iOS the app is ended at a fixed footprint, and a scene that
	// queues many pipelines at once (Mario Kart 8: 160 at a race start on 7 threads, +250 MB in
	// 10 s, then the app was ended) costs far more on 7 threads than on 2 or 3. The limit follows the
	// device's RAM so every iPhone and iPad gets a size that fits it.
	{
		uint64_t memBytes = 0;
		size_t len = sizeof(memBytes);
		sysctlbyname("hw.memsize", &memBytes, &len, nullptr, 0);
		const uint32 cap = memBytes > (7ull << 30) ? 3u : 2u;
		numCompileThreads = std::min(numCompileThreads, cap);
		cemuLog_log(LogType::Force, "Vulkan: {} pipeline compile threads ({} MB RAM)", numCompileThreads, memBytes >> 20);
	}
#endif

	for (uint32_t i = 0; i < numCompileThreads; i++)
	{
		s_compileThreads.emplace_back(compilePipeline_thread, i);
	}
}

void PipelineCompiler::CompileThreadPool_Stop()
{
	s_compileThreadsShutdownSignal = true;
	{
		for (auto& thread : s_compileThreads)
			s_pipelineCompileRequests.push(nullptr);
	}
	for (auto& thread : s_compileThreads)
		thread.join();
	while (!s_pipelineCompileRequests.empty())
	{
		PipelineCompiler* pipelineCompiler = s_pipelineCompileRequests.pop();
		if (pipelineCompiler)
			delete pipelineCompiler;
	}
	s_compileThreads.clear();
}

void PipelineCompiler::CompileThreadPool_QueueCompilation(PipelineCompiler* v)
{
	s_pipelineCompileRequests.push(v);
}

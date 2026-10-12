#include "Cafe/HW/Latte/Core/LatteConst.h"
#include "Cafe/HW/Latte/Core/LatteShaderAssembly.h"
#include "Cafe/HW/Latte/ISA/RegDefines.h"
#include "Cafe/HW/Latte/Core/Latte.h"
#include "Cafe/HW/Latte/LegacyShaderDecompiler/LatteDecompiler.h"
#include "Cafe/HW/Latte/LegacyShaderDecompiler/LatteDecompilerInternal.h"
#include "Cafe/HW/Latte/LegacyShaderDecompiler/LatteDecompilerInstructions.h"
#include "Cafe/HW/Latte/Core/FetchShader.h"
#include "Cafe/HW/Latte/Core/LatteShader.h"
#include "Cafe/HW/Latte/Renderer/Renderer.h"
#include "Common/MemPtr.h"
#include "HW/Latte/ISA/LatteReg.h"
#ifdef ENABLE_METAL
#include "HW/Latte/Renderer/Metal/MetalCommon.h"
#include "HW/Latte/Renderer/Metal/MetalRenderer.h"
#include "HW/Latte/Renderer/Metal/LatteToMtl.h"
#endif

Latte::E_GX2SURFFMT LatteTexture_ReconstructGX2Format(const Latte::LATTE_SQ_TEX_RESOURCE_WORD1_N& texUnitWord1, const Latte::LATTE_SQ_TEX_RESOURCE_WORD4_N& texUnitWord4);

sint32 LatteDecompiler_getColorOutputIndexFromExportIndex(LatteDecompilerShaderContext* shaderContext, sint32 exportIndex)
{
	sint32 colorOutputIndex = -1;
	sint32 outputCounter = 0;
	uint32 cbShaderMask = shaderContext->contextRegisters[mmCB_SHADER_MASK];
	uint32 cbShaderControl = shaderContext->contextRegisters[mmCB_SHADER_CONTROL];
	for(sint32 m=0; m<8; m++)
	{
		uint32 outputMask = (cbShaderMask>>(m*4))&0xF;
		if( outputMask == 0 )
			continue;
		cemu_assert_debug(outputMask == 0xF);
		if( outputCounter == exportIndex )
		{
			colorOutputIndex = m;
			break;
		}
		outputCounter++;
	}
	cemu_assert_debug(colorOutputIndex != -1);
	return colorOutputIndex;
}

void _remapUniformAccess(LatteDecompilerShaderContext* shaderContext, bool isRegisterUniform, uint32 kcacheBankId, uint32 uniformIndex)
{
	auto& list_uniformMapping = shaderContext->shader->list_remappedUniformEntries;
	for(uint32 i=0; i<list_uniformMapping.size(); i++)
	{
		LatteDecompilerRemappedUniformEntry_t* ufMapping = list_uniformMapping.data()+i;
		if( isRegisterUniform )
		{
			if( ufMapping->isRegister == true && ufMapping->index == uniformIndex )
			{
				return;
			}
		}
		else
		{
			if( ufMapping->isRegister == false && ufMapping->kcacheBankId == kcacheBankId && ufMapping->index == uniformIndex )
			{
				return;
			}
		}
	}
	LatteDecompilerRemappedUniformEntry_t newMapping = {0};
	if( isRegisterUniform )
	{
		newMapping.isRegister = true;
		newMapping.index = uniformIndex;
		newMapping.mappedIndex = (uint32)list_uniformMapping.size();
	}
	else
	{
		newMapping.isRegister = false;
		newMapping.kcacheBankId = kcacheBankId;
		newMapping.index = uniformIndex;
		newMapping.mappedIndex = (uint32)list_uniformMapping.size();
	}
	list_uniformMapping.emplace_back(newMapping);
}

bool _isIntegerInstruction(const LatteDecompilerALUInstruction& aluInstruction)
{
	if (aluInstruction.isOP3 == false)
	{
		switch (aluInstruction.opcode)
		{
		case ALU_OP2_INST_ADD:
		case ALU_OP2_INST_MUL:
		case ALU_OP2_INST_MUL_IEEE:
		case ALU_OP2_INST_MAX:
		case ALU_OP2_INST_MIN:
		case ALU_OP2_INST_FLOOR:
		case ALU_OP2_INST_FRACT:
		case ALU_OP2_INST_TRUNC:
		case ALU_OP2_INST_MOV:
		case ALU_OP2_INST_NOP:
		case ALU_OP2_INST_DOT4:
		case ALU_OP2_INST_DOT4_IEEE:
		case ALU_OP2_INST_CUBE:
		case ALU_OP2_INST_EXP_IEEE:
		case ALU_OP2_INST_LOG_CLAMPED:
		case ALU_OP2_INST_LOG_IEEE:
		case ALU_OP2_INST_SQRT_IEEE:
		case ALU_OP2_INST_SIN:
		case ALU_OP2_INST_COS:
		case ALU_OP2_INST_RNDNE:
		case ALU_OP2_INST_MAX_DX10:
		case ALU_OP2_INST_MIN_DX10:
		case ALU_OP2_INST_SETGT:
		case ALU_OP2_INST_SETGE:
		case ALU_OP2_INST_SETNE:
		case ALU_OP2_INST_SETE:
		case ALU_OP2_INST_PRED_SETE:
		case ALU_OP2_INST_PRED_SETGT:
		case ALU_OP2_INST_PRED_SETGE:
		case ALU_OP2_INST_PRED_SETNE:
		case ALU_OP2_INST_KILLE:
		case ALU_OP2_INST_KILLGT:
		case ALU_OP2_INST_KILLGE:
		case ALU_OP2_INST_RECIP_FF:
		case ALU_OP2_INST_RECIP_IEEE:
		case ALU_OP2_INST_RECIPSQRT_CLAMPED:
		case ALU_OP2_INST_RECIPSQRT_FF:
		case ALU_OP2_INST_RECIPSQRT_IEEE:
			return false;
		case ALU_OP2_INST_FLT_TO_INT:
		case ALU_OP2_INST_INT_TO_FLOAT:
		case ALU_OP2_INST_UINT_TO_FLOAT:
		case ALU_OP2_INST_ASHR_INT:
		case ALU_OP2_INST_LSHR_INT:
		case ALU_OP2_INST_LSHL_INT:
		case ALU_OP2_INST_MULLO_INT:
		case ALU_OP2_INST_MULLO_UINT:
		case ALU_OP2_INST_FLT_TO_UINT:
		case ALU_OP2_INST_AND_INT:
		case ALU_OP2_INST_OR_INT:
		case ALU_OP2_INST_XOR_INT:
		case ALU_OP2_INST_NOT_INT:
		case ALU_OP2_INST_ADD_INT:
		case ALU_OP2_INST_SUB_INT:
		case ALU_OP2_INST_MAX_INT:
		case ALU_OP2_INST_MIN_INT:
		case ALU_OP2_INST_MAX_UINT:
		case ALU_OP2_INST_MIN_UINT:
		case ALU_OP2_INST_SETE_INT:
		case ALU_OP2_INST_SETGT_INT:
		case ALU_OP2_INST_SETGE_INT:
		case ALU_OP2_INST_SETNE_INT:
		case ALU_OP2_INST_SETGT_UINT:
		case ALU_OP2_INST_SETGE_UINT:
		case ALU_OP2_INST_PRED_SETE_INT:
		case ALU_OP2_INST_PRED_SETGT_INT:
		case ALU_OP2_INST_PRED_SETGE_INT:
		case ALU_OP2_INST_PRED_SETNE_INT:
		case ALU_OP2_INST_KILLE_INT:
		case ALU_OP2_INST_KILLGT_INT:
		case ALU_OP2_INST_KILLNE_INT:
		case ALU_OP2_INST_MOVA_FLOOR:
		case ALU_OP2_INST_MOVA_INT:
			return true;
		case ALU_OP2_INST_SETE_DX10:
		case ALU_OP2_INST_SETGT_DX10:
		case ALU_OP2_INST_SETGE_DX10:
		case ALU_OP2_INST_SETNE_DX10:
			return true;
		default:
#ifdef CEMU_DEBUG_ASSERT
			debug_printf("_isIntegerInstruction(): OP3=%s opcode=%02x\n", aluInstruction.isOP3 ? "true" : "false", aluInstruction.opcode);
			cemu_assert_debug(false);
#endif
			break;
		}
	}
	else
	{
		switch (aluInstruction.opcode)
		{
		case ALU_OP3_INST_MULADD:
		case ALU_OP3_INST_MULADD_D2:
		case ALU_OP3_INST_MULADD_M2:
		case ALU_OP3_INST_MULADD_M4:
		case ALU_OP3_INST_MULADD_IEEE:
		case ALU_OP3_INST_CMOVE:
		case ALU_OP3_INST_CMOVGT:
		case ALU_OP3_INST_CMOVGE:
			return false;
		case ALU_OP3_INST_CNDE_INT:
		case ALU_OP3_INST_CNDGT_INT:
		case ALU_OP3_INST_CMOVGE_INT:
			return true;
		default:
#ifdef CEMU_DEBUG_ASSERT
			debug_printf("_isIntegerInstruction(): OP3=%s opcode=%02x\n", aluInstruction.isOP3?"true":"false", aluInstruction.opcode);
#endif
			break;
		}
	}
	return false;
}

void LatteDecompiler_analyzeALUClause(LatteDecompilerShaderContext* shaderContext, LatteDecompilerCFInstruction* cfInstruction)
{
	if( cfInstruction->type == GPU7_CF_INST_ALU_PUSH_BEFORE || cfInstruction->type == GPU7_CF_INST_ALU_POP_AFTER || cfInstruction->type == GPU7_CF_INST_ALU_POP2_AFTER || cfInstruction->type == GPU7_CF_INST_ALU_BREAK || cfInstruction->type == GPU7_CF_INST_ALU_ELSE_AFTER )
	{
		shaderContext->analyzer.modifiesPixelActiveState = true;
	}
	for(auto& aluInstruction : cfInstruction->instructionsALU)
	{
		if( !aluInstruction.isOP3 && aluInstruction.opcode == ALU_OP2_INST_NOP )
			continue;
		if( !aluInstruction.isOP3 && aluInstruction.opcode == ALU_OP2_INST_CUBE )
		{
			shaderContext->analyzer.hasRedcCUBE = true;
		}
		if (_isIntegerInstruction(aluInstruction))
			shaderContext->analyzer.usesIntegerValues = true;
		for(sint32 f=0; f<3; f++)
		{
			if( aluInstruction.sourceOperand[f].sel == 0xFFFFFFFF )
				continue;
			if( GPU7_ALU_SRC_IS_CFILE(aluInstruction.sourceOperand[f].sel) )
			{
				if (aluInstruction.sourceOperand[f].rel)
				{
					shaderContext->analyzer.uniformRegisterAccessTracker.TrackAccess(GPU7_ALU_SRC_GET_CFILE_INDEX(aluInstruction.sourceOperand[f].sel), true);
				}
				else
				{
					_remapUniformAccess(shaderContext, true, 0, GPU7_ALU_SRC_GET_CFILE_INDEX(aluInstruction.sourceOperand[f].sel));
					shaderContext->analyzer.uniformRegisterAccessTracker.TrackAccess(GPU7_ALU_SRC_GET_CFILE_INDEX(aluInstruction.sourceOperand[f].sel), false);
				}
			}
			else if( GPU7_ALU_SRC_IS_CBANK0(aluInstruction.sourceOperand[f].sel) )
			{
				uint32 uniformBufferIndex = cfInstruction->cBank0Index;
				cemu_assert(uniformBufferIndex < LATTE_NUM_MAX_UNIFORM_BUFFERS);
				uint32 offset = GPU7_ALU_SRC_GET_CBANK0_INDEX(aluInstruction.sourceOperand[f].sel)+cfInstruction->cBank0AddrBase;
				_remapUniformAccess(shaderContext, false, uniformBufferIndex, offset);
				shaderContext->analyzer.uniformBufferAccessTracker[uniformBufferIndex].TrackAccess(offset, aluInstruction.sourceOperand[f].rel);
			}
			else if( GPU7_ALU_SRC_IS_CBANK1(aluInstruction.sourceOperand[f].sel) )
			{
				uint32 uniformBufferIndex = cfInstruction->cBank1Index;
				cemu_assert(uniformBufferIndex < LATTE_NUM_MAX_UNIFORM_BUFFERS);
				uint32 offset = GPU7_ALU_SRC_GET_CBANK1_INDEX(aluInstruction.sourceOperand[f].sel)+cfInstruction->cBank1AddrBase;
				_remapUniformAccess(shaderContext, false, uniformBufferIndex, offset);
				shaderContext->analyzer.uniformBufferAccessTracker[uniformBufferIndex].TrackAccess(offset, aluInstruction.sourceOperand[f].rel);
			}
			else if( GPU7_ALU_SRC_IS_GPR(aluInstruction.sourceOperand[f].sel) )
			{
				sint32 gprIndex = GPU7_ALU_SRC_GET_GPR_INDEX(aluInstruction.sourceOperand[f].sel);
				shaderContext->analyzer.gprUseMask[gprIndex/8] |= (1<<(gprIndex%8));
				if( aluInstruction.sourceOperand[f].rel != 0 )
				{
					shaderContext->analyzer.usesRelativeGPRRead = true;
					continue;
				}

			}
		}
		if( aluInstruction.destRel != 0 )
			shaderContext->analyzer.usesRelativeGPRWrite = true;
		shaderContext->analyzer.gprUseMask[aluInstruction.destGpr/8] |= (1<<(aluInstruction.destGpr%8));
	}
}

void LatteDecompiler_analyzeTEXClause(LatteDecompilerShaderContext* shaderContext, LatteDecompilerCFInstruction* cfInstruction)
{
	LatteDecompilerShader* shader = shaderContext->shader;
	for(auto& texInstruction : cfInstruction->instructionsTEX)
	{
		if( texInstruction.opcode == GPU7_TEX_INST_SAMPLE ||
			texInstruction.opcode == GPU7_TEX_INST_SAMPLE_L ||
			texInstruction.opcode == GPU7_TEX_INST_SAMPLE_LB ||
			texInstruction.opcode == GPU7_TEX_INST_SAMPLE_LZ ||
			texInstruction.opcode == GPU7_TEX_INST_SAMPLE_C ||
			texInstruction.opcode == GPU7_TEX_INST_SAMPLE_C_L ||
			texInstruction.opcode == GPU7_TEX_INST_SAMPLE_C_LZ ||
			texInstruction.opcode == GPU7_TEX_INST_FETCH4 ||
			texInstruction.opcode == GPU7_TEX_INST_SAMPLE_G ||
			texInstruction.opcode == GPU7_TEX_INST_LD )
		{
			if (texInstruction.textureFetch.textureIndex < 0 || texInstruction.textureFetch.textureIndex >= LATTE_NUM_MAX_TEX_UNITS)
			{
				cemuLog_logDebug(LogType::Force, "Shader {:16x} has out of bounds texture access (texture {})", shaderContext->shader->baseHash, (sint32)texInstruction.textureFetch.textureIndex);
				continue;
			}
			if( texInstruction.textureFetch.samplerIndex < 0 || texInstruction.textureFetch.samplerIndex >= 0x12 )
				cemu_assert_debug(false);
			if(shaderContext->output->textureUnitMask[texInstruction.textureFetch.textureIndex] && shader->textureUnitSamplerAssignment[texInstruction.textureFetch.textureIndex] != texInstruction.textureFetch.samplerIndex && shader->textureUnitSamplerAssignment[texInstruction.textureFetch.textureIndex] != LATTE_DECOMPILER_SAMPLER_NONE )
			{
				cemu_assert_debug(false);
			}
			shaderContext->output->textureUnitMask[texInstruction.textureFetch.textureIndex] = true;
			shader->textureUnitSamplerAssignment[texInstruction.textureFetch.textureIndex] = texInstruction.textureFetch.samplerIndex;
			if( texInstruction.opcode == GPU7_TEX_INST_SAMPLE_C || texInstruction.opcode == GPU7_TEX_INST_SAMPLE_C_L || texInstruction.opcode == GPU7_TEX_INST_SAMPLE_C_LZ)
				shader->textureUsesDepthCompare[texInstruction.textureFetch.textureIndex] = true;

			bool useTexelCoords = false;
			if (texInstruction.opcode == GPU7_TEX_INST_SAMPLE && (texInstruction.textureFetch.unnormalized[0] && texInstruction.textureFetch.unnormalized[1] && texInstruction.textureFetch.unnormalized[2] && texInstruction.textureFetch.unnormalized[3]))
				useTexelCoords = true;
			else if (texInstruction.opcode == GPU7_TEX_INST_LD)
				useTexelCoords = true;
			if (useTexelCoords)
			{
				shaderContext->analyzer.texUnitUsesTexelCoordinates.set(texInstruction.textureFetch.textureIndex);
			}
		}
		else if( texInstruction.opcode == GPU7_TEX_INST_GET_COMP_TEX_LOD || texInstruction.opcode == GPU7_TEX_INST_GET_TEXTURE_RESINFO )
		{
			if( texInstruction.textureFetch.textureIndex < 0 || texInstruction.textureFetch.textureIndex >= LATTE_NUM_MAX_TEX_UNITS )
				debugBreakpoint();
			if( texInstruction.textureFetch.samplerIndex != 0 )
				debugBreakpoint();
			shaderContext->output->textureUnitMask[texInstruction.textureFetch.textureIndex] = true;
		}
		else if( texInstruction.opcode == GPU7_TEX_INST_SET_CUBEMAP_INDEX )
		{
		}
		else if (texInstruction.opcode == GPU7_TEX_INST_GET_GRADIENTS_H || texInstruction.opcode == GPU7_TEX_INST_GET_GRADIENTS_V)
		{
		}
		else if (texInstruction.opcode == GPU7_TEX_INST_SET_GRADIENTS_H || texInstruction.opcode == GPU7_TEX_INST_SET_GRADIENTS_V)
		{
			shaderContext->analyzer.hasGradientLookup = true;
		}
		else if( texInstruction.opcode == GPU7_TEX_INST_VFETCH )
		{
			if( texInstruction.textureFetch.textureIndex >= 0x80 && texInstruction.textureFetch.textureIndex <= 0x8F )
			{
				uint32 uniformBufferIndex = texInstruction.textureFetch.textureIndex - 0x80;
				shaderContext->analyzer.uniformBufferAccessTracker[uniformBufferIndex].TrackAccess(0, true);
			}
			else if( texInstruction.textureFetch.textureIndex == 0x9F && shader->shaderType == LatteConst::ShaderType::Geometry )
			{
			}
			else
				debugBreakpoint();
		}
		else if (texInstruction.opcode == GPU7_TEX_INST_MEM)
		{
			shaderContext->analyzer.hasSSBORead = true;
		}
		else
			debugBreakpoint();
		if(texInstruction.dstGpr < LATTE_NUM_GPR)
			shaderContext->analyzer.gprUseMask[texInstruction.dstGpr/8] |= (1<<(texInstruction.dstGpr%8));
		if(texInstruction.srcGpr < LATTE_NUM_GPR)
			shaderContext->analyzer.gprUseMask[texInstruction.srcGpr/8] |= (1<<(texInstruction.srcGpr%8));
	}
}

void LatteDecompiler_analyzeExport(LatteDecompilerShaderContext* shaderContext, LatteDecompilerCFInstruction* cfInstruction)
{
	LatteDecompilerShader* shader = shaderContext->shader;
	if( shader->shaderType == LatteConst::ShaderType::Pixel )
	{
		if (cfInstruction->exportType == 0 && cfInstruction->exportArrayBase < 8)
		{
			for(uint32 i=0; i<(cfInstruction->exportBurstCount+1); i++)
			{
				sint32 colorOutputIndex = LatteDecompiler_getColorOutputIndexFromExportIndex(shaderContext, cfInstruction->exportArrayBase+i);
				shader->pixelColorOutputMask |= (1<<colorOutputIndex);
			}
		}
		else if (cfInstruction->exportType == 0 && cfInstruction->exportArrayBase == 61)
		{
			if (g_renderer->GetType() != RendererAPI::Metal || LatteMRT::GetActiveDepthBufferMask(*shaderContext->contextRegistersNew))
				shader->depthMask = true;
		}
		else
			debugBreakpoint();
	}
	else if (shader->shaderType == LatteConst::ShaderType::Vertex)
	{
		if (cfInstruction->exportType == 2 && cfInstruction->exportArrayBase < 32)
		{
			shaderContext->shader->outputParameterMask |= (1<<cfInstruction->exportArrayBase);
		}
		else if (cfInstruction->exportType == 1 && cfInstruction->exportArrayBase == GPU7_DECOMPILER_CF_EXPORT_POINT_SIZE)
		{
			shaderContext->analyzer.writesPointSize = true;
		}
	}
	for(uint32 i=0; i<(cfInstruction->exportBurstCount+1); i++)
	{
		shaderContext->analyzer.gprUseMask[(cfInstruction->exportSourceGPR+i)/8] |= (1<<((cfInstruction->exportSourceGPR+i)%8));
	}
}

void LatteDecompiler_analyzeSubroutine(LatteDecompilerShaderContext* shaderContext, uint32 cfAddr)
{

	cemu_assert_debug(false);

	for(auto& cfInstruction : shaderContext->cfInstructions)
	{
		if (cfInstruction.type == GPU7_CF_INST_ALU || cfInstruction.type == GPU7_CF_INST_ALU_PUSH_BEFORE || cfInstruction.type == GPU7_CF_INST_ALU_POP_AFTER || cfInstruction.type == GPU7_CF_INST_ALU_POP2_AFTER || cfInstruction.type == GPU7_CF_INST_ALU_BREAK || cfInstruction.type == GPU7_CF_INST_ALU_ELSE_AFTER)
		{
			LatteDecompiler_analyzeALUClause(shaderContext, &cfInstruction);
		}
		else if (cfInstruction.type == GPU7_CF_INST_TEX)
		{
			LatteDecompiler_analyzeTEXClause(shaderContext, &cfInstruction);
		}
		else if (cfInstruction.type == GPU7_CF_INST_EXPORT || cfInstruction.type == GPU7_CF_INST_EXPORT_DONE)
		{
			LatteDecompiler_analyzeExport(shaderContext, &cfInstruction);
		}
		else if (cfInstruction.type == GPU7_CF_INST_ELSE || cfInstruction.type == GPU7_CF_INST_POP)
		{
			shaderContext->analyzer.modifiesPixelActiveState = true;
		}
		else if (cfInstruction.type == GPU7_CF_INST_LOOP_START_DX10 || cfInstruction.type == GPU7_CF_INST_LOOP_END ||
				 cfInstruction.type == GPU7_CF_INST_LOOP_START_NO_AL)
		{
			shaderContext->analyzer.modifiesPixelActiveState = true;
		}
		else if (cfInstruction.type == GPU7_CF_INST_LOOP_BREAK)
		{
			shaderContext->analyzer.modifiesPixelActiveState = true;
		}
		else if (cfInstruction.type == GPU7_CF_INST_EMIT_VERTEX)
		{
		}
		else if (cfInstruction.type == GPU7_CF_INST_CALL)
		{
			cemu_assert_debug(false);
		}
		else
		{
			cemu_assert_unimplemented();
		}
	}
}

namespace LatteDecompiler
{
	void _initTextureBindingPointsGL(LatteDecompilerShaderContext* decompilerContext)
	{
		for (sint32 i = 0; i < LATTE_NUM_MAX_TEX_UNITS; i++)
		{
			if (!decompilerContext->output->textureUnitMask[i])
				continue;
			sint32 textureBindingPoint;
			if (decompilerContext->shaderType == LatteConst::ShaderType::Vertex)
				textureBindingPoint = i + LATTE_CEMU_VS_TEX_UNIT_BASE;
			else if (decompilerContext->shaderType == LatteConst::ShaderType::Geometry)
				textureBindingPoint = i + LATTE_CEMU_GS_TEX_UNIT_BASE;
			else if (decompilerContext->shaderType == LatteConst::ShaderType::Pixel)
				textureBindingPoint = i + LATTE_CEMU_PS_TEX_UNIT_BASE;

			decompilerContext->output->resourceMappingGL.textureUnitToBindingPoint[i] = textureBindingPoint;
		}
	}

	void _initTextureBindingPointsVK(LatteDecompilerShaderContext* decompilerContext)
	{
		for (sint32 i = 0; i < LATTE_NUM_MAX_TEX_UNITS; i++)
		{
			if (!decompilerContext->output->textureUnitMask[i])
				continue;
			decompilerContext->output->resourceMappingVK.textureUnitToBindingPoint[i] = decompilerContext->currentBindingPointVK;
			decompilerContext->currentBindingPointVK++;
		}
	}

#ifdef ENABLE_METAL
	static bool _useFramebufferFetchMTL(LatteDecompilerShaderContext* decompilerContext, sint32 textureUnit)
	{
		if (!g_renderer ||
			g_renderer->GetType() != RendererAPI::Metal ||
			!static_cast<MetalRenderer*>(g_renderer.get())->SupportsFramebufferFetch())
			return false;

		uint8 renderTargetIndex = decompilerContext->shader->textureRenderTargetIndex[textureUnit];
		if (renderTargetIndex == 255)
			return false;

		auto format = LatteMRT::GetColorBufferFormat(renderTargetIndex, *decompilerContext->contextRegistersNew);
		return GetMtlPixelFormat(format, false) != MTL::PixelFormatInvalid;
	}

	void _initTextureBindingPointsMTL(LatteDecompilerShaderContext* decompilerContext)
		{
			sint8 currentSamplerBindingPoint = 0;
			std::map<uint16, sint8> samplerBindings;

			for (sint32 i = 0; i < LATTE_NUM_MAX_TEX_UNITS; i++)
		{
			if (!decompilerContext->output->textureUnitMask[i])
				continue;
			if (_useFramebufferFetchMTL(decompilerContext, i))
				continue;
			decompilerContext->output->resourceMappingMTL.textureUnitToBindingPoint[i] = decompilerContext->currentTextureBindingPointMTL;
			decompilerContext->currentTextureBindingPointMTL++;

            uint16 samplerUnit =
                decompilerContext->shader->textureUnitSamplerAssignment[i];

            if (samplerUnit == LATTE_DECOMPILER_SAMPLER_NONE)
                continue;

			auto existingSampler = samplerBindings.find(samplerUnit);
			if (existingSampler != samplerBindings.end())
			{
				decompilerContext->output->resourceMappingMTL.textureUnitToSamplerBindingPoint[i] = existingSampler->second;
					continue;
				}

            if (currentSamplerBindingPoint >= MAX_MTL_SAMPLERS)
            {
                cemuLog_logOnce(
                    LogType::Force,
                    "Metal shader 0x{:016x} exceeds the {}-sampler limit",
                    decompilerContext->shaderBaseHash,
                    MAX_MTL_SAMPLERS);
                continue;
            }

			decompilerContext->output->resourceMappingMTL.textureUnitToSamplerBindingPoint[i] = currentSamplerBindingPoint;
			samplerBindings.emplace(samplerUnit, currentSamplerBindingPoint);
			currentSamplerBindingPoint++;
		}
	}
#endif

	void _initHasUniformVarBlock(LatteDecompilerShaderContext* decompilerContext)
	{
		decompilerContext->hasUniformVarBlock = false;
		if (decompilerContext->shader->uniformMode == LATTE_DECOMPILER_UNIFORM_MODE_REMAPPED)
			decompilerContext->hasUniformVarBlock = true;
		else if (decompilerContext->shader->uniformMode == LATTE_DECOMPILER_UNIFORM_MODE_FULL_CFILE)
			decompilerContext->hasUniformVarBlock = true;

		bool hasAnyViewportScaleDisabled =
			!decompilerContext->contextRegistersNew->PA_CL_VTE_CNTL.get_VPORT_X_SCALE_ENA() ||
			!decompilerContext->contextRegistersNew->PA_CL_VTE_CNTL.get_VPORT_Y_SCALE_ENA() ||
			!decompilerContext->contextRegistersNew->PA_CL_VTE_CNTL.get_VPORT_Z_SCALE_ENA();
		cemu_assert_debug(decompilerContext->contextRegistersNew->PA_CL_VTE_CNTL.get_VPORT_X_SCALE_ENA() == !hasAnyViewportScaleDisabled);
		cemu_assert_debug(decompilerContext->contextRegistersNew->PA_CL_VTE_CNTL.get_VPORT_Y_SCALE_ENA() == !hasAnyViewportScaleDisabled);
		cemu_assert_debug(decompilerContext->contextRegistersNew->PA_CL_VTE_CNTL.get_VPORT_Z_SCALE_ENA() == !hasAnyViewportScaleDisabled);
		cemu_assert_debug(decompilerContext->contextRegistersNew->PA_CL_VTE_CNTL.get_VPORT_X_OFFSET_ENA() == !hasAnyViewportScaleDisabled);
		cemu_assert_debug(decompilerContext->contextRegistersNew->PA_CL_VTE_CNTL.get_VPORT_Y_OFFSET_ENA() == !hasAnyViewportScaleDisabled);
		cemu_assert_debug(decompilerContext->contextRegistersNew->PA_CL_VTE_CNTL.get_VPORT_Z_OFFSET_ENA() == !hasAnyViewportScaleDisabled);

		if (decompilerContext->shaderType == LatteConst::ShaderType::Vertex && hasAnyViewportScaleDisabled)
			decompilerContext->hasUniformVarBlock = true;
		bool alphaTestEnable = decompilerContext->contextRegistersNew->SX_ALPHA_TEST_CONTROL.get_ALPHA_TEST_ENABLE();
		if (decompilerContext->shaderType == LatteConst::ShaderType::Pixel && alphaTestEnable != 0)
			decompilerContext->hasUniformVarBlock = true;
		if (decompilerContext->shaderType == LatteConst::ShaderType::Pixel)
			decompilerContext->hasUniformVarBlock = true;
		if (decompilerContext->shaderType == LatteConst::ShaderType::Vertex && decompilerContext->analyzer.outputPointSize && decompilerContext->analyzer.writesPointSize == false)
			decompilerContext->hasUniformVarBlock = true;
		if (decompilerContext->shaderType == LatteConst::ShaderType::Geometry && decompilerContext->analyzer.outputPointSize && decompilerContext->analyzer.writesPointSize == false)
			decompilerContext->hasUniformVarBlock = true;
		if (decompilerContext->analyzer.useSSBOForStreamout &&
			((decompilerContext->shaderType == LatteConst::ShaderType::Vertex && !decompilerContext->options->usesGeometryShader) ||
			(decompilerContext->shaderType == LatteConst::ShaderType::Geometry)))
		{
			decompilerContext->hasUniformVarBlock = true;
		}
#ifdef ENABLE_METAL
		if (g_renderer->GetType() == RendererAPI::Metal)
		{
            bool usesGeometryShader = UseGeometryShader(*decompilerContext->contextRegistersNew, decompilerContext->options->usesGeometryShader);

		    if (decompilerContext->shaderType == LatteConst::ShaderType::Vertex && usesGeometryShader)
				decompilerContext->hasUniformVarBlock = true;
		}
#endif
	}

	void _initUniformBindingPoints(LatteDecompilerShaderContext* decompilerContext)
	{
		_initHasUniformVarBlock(decompilerContext);

		if (decompilerContext->shaderType == LatteConst::ShaderType::Pixel)
		{
			for (sint32 t = 0; t < LATTE_NUM_MAX_TEX_UNITS; t++)
			{
				if (decompilerContext->analyzer.texUnitUsesTexelCoordinates.test(t) == false)
					continue;
				decompilerContext->hasUniformVarBlock = true;
			}
		}
		if (decompilerContext->hasUniformVarBlock)
		{
			decompilerContext->output->resourceMappingVK.uniformVarsBufferBindingPoint = decompilerContext->currentBindingPointVK;
			decompilerContext->currentBindingPointVK++;
			decompilerContext->output->resourceMappingMTL.uniformVarsBufferBindingPoint = decompilerContext->currentBufferBindingPointMTL;
			decompilerContext->currentBufferBindingPointMTL++;
		}
		if (decompilerContext->shader->uniformMode == LATTE_DECOMPILER_UNIFORM_MODE_FULL_CBANK)
		{
			for (uint32 i = 0; i < LATTE_NUM_MAX_UNIFORM_BUFFERS; i++)
			{
				if (!decompilerContext->analyzer.uniformBufferAccessTracker[i].HasAccess())
					continue;
				sint32 uniformBindingPoint = i;
				if (decompilerContext->shaderType == LatteConst::ShaderType::Geometry)
					uniformBindingPoint += 64;
				else if (decompilerContext->shaderType == LatteConst::ShaderType::Vertex)
					uniformBindingPoint += 0;
				else if (decompilerContext->shaderType == LatteConst::ShaderType::Pixel)
					uniformBindingPoint += 32;

				decompilerContext->output->resourceMappingVK.uniformBuffersBindingPoint[i] = decompilerContext->currentBindingPointVK;
				decompilerContext->currentBindingPointVK++;
				decompilerContext->output->resourceMappingMTL.uniformBuffersBindingPoint[i] = decompilerContext->currentBufferBindingPointMTL;
				decompilerContext->currentBufferBindingPointMTL++;
			}
			for (uint32 i = 0; i < LATTE_NUM_MAX_UNIFORM_BUFFERS; i++)
			{
				if (!decompilerContext->analyzer.uniformBufferAccessTracker[i].HasAccess())
					continue;
				sint32 uniformBindingPoint = i;
				if (decompilerContext->shaderType == LatteConst::ShaderType::Geometry)
					uniformBindingPoint += 64;
				else if (decompilerContext->shaderType == LatteConst::ShaderType::Vertex)
					uniformBindingPoint += 0;
				else if (decompilerContext->shaderType == LatteConst::ShaderType::Pixel)
					uniformBindingPoint += 32;
				decompilerContext->output->resourceMappingGL.uniformBuffersBindingPoint[i] = uniformBindingPoint;
			}
		}
		if (decompilerContext->analyzer.useSSBOForStreamout)
		{
			decompilerContext->output->resourceMappingVK.tfStorageBindingPoint = decompilerContext->currentBindingPointVK;
			decompilerContext->currentBindingPointVK++;
			decompilerContext->output->resourceMappingMTL.tfStorageBindingPoint = decompilerContext->currentBufferBindingPointMTL;
			decompilerContext->currentBufferBindingPointMTL++;
		}
	}

	void _initAttributeBindingPoints(LatteDecompilerShaderContext* decompilerContext)
	{
		if (decompilerContext->shaderType != LatteConst::ShaderType::Vertex)
			return;
		sint8 bindingIndex = 0;
		for (sint32 i = 0; i < LATTE_NUM_MAX_ATTRIBUTE_LOCATIONS; i++)
		{
			if (decompilerContext->analyzer.inputAttributSemanticMask[i])
			{
				decompilerContext->output->resourceMappingGL.attributeMapping[i] = bindingIndex;
				decompilerContext->output->resourceMappingVK.attributeMapping[i] = bindingIndex;
				decompilerContext->output->resourceMappingMTL.attributeMapping[i] = bindingIndex;
				bindingIndex++;
			}
		}
	}

}

void LatteDecompiler_analyze(LatteDecompilerShaderContext* shaderContext, LatteDecompilerShader* shader)
{
	shaderContext->analyzer.isPointsPrimitive = shaderContext->contextRegistersNew->VGT_PRIMITIVE_TYPE.get_PRIMITIVE_MODE() == Latte::LATTE_VGT_PRIMITIVE_TYPE::E_PRIMITIVE_TYPE::POINTS;
	shaderContext->analyzer.hasStreamoutEnable = shaderContext->contextRegisters[mmVGT_STRMOUT_EN] != 0;
	if (shaderContext->shaderType == LatteConst::ShaderType::Vertex && !shaderContext->options->usesGeometryShader)
		shaderContext->analyzer.outputPointSize = shaderContext->analyzer.isPointsPrimitive;
	else if (shaderContext->shaderType == LatteConst::ShaderType::Geometry)
	{
		uint32 gsOutPrimType = shaderContext->contextRegisters[mmVGT_GS_OUT_PRIM_TYPE];
		if (gsOutPrimType == 0)
			shaderContext->analyzer.outputPointSize = true;
	}
	if (shader->shaderType == LatteConst::ShaderType::Vertex || shader->shaderType == LatteConst::ShaderType::Geometry)
	{
		if(shaderContext->fetchShader)
		{
			LatteFetchShader* parsedFetchShader = shaderContext->fetchShader;
			for(auto& bufferGroup : parsedFetchShader->bufferGroups)
			{
				for (sint32 i = 0; i < bufferGroup.attribCount; i++)
				{
					uint8 semanticId = bufferGroup.attrib[i].semanticId;
					if (semanticId == 0xFF)
					{
						continue;
					}
					cemu_assert_debug(semanticId < 0x80);
					shaderContext->analyzer.inputAttributSemanticMask[semanticId] = true;
				}
			}
		}
	}
	std::vector<uint32> list_subroutineAddrs;
	for(auto& cfInstruction : shaderContext->cfInstructions)
	{
		if (cfInstruction.type == GPU7_CF_INST_ALU || cfInstruction.type == GPU7_CF_INST_ALU_PUSH_BEFORE || cfInstruction.type == GPU7_CF_INST_ALU_POP_AFTER || cfInstruction.type == GPU7_CF_INST_ALU_POP2_AFTER || cfInstruction.type == GPU7_CF_INST_ALU_BREAK || cfInstruction.type == GPU7_CF_INST_ALU_ELSE_AFTER)
		{
			LatteDecompiler_analyzeALUClause(shaderContext, &cfInstruction);
		}
		else if (cfInstruction.type == GPU7_CF_INST_TEX)
		{
			LatteDecompiler_analyzeTEXClause(shaderContext, &cfInstruction);
		}
		else if (cfInstruction.type == GPU7_CF_INST_EXPORT || cfInstruction.type == GPU7_CF_INST_EXPORT_DONE)
		{
			LatteDecompiler_analyzeExport(shaderContext, &cfInstruction);
		}
		else if (cfInstruction.type == GPU7_CF_INST_ELSE || cfInstruction.type == GPU7_CF_INST_POP)
		{
			shaderContext->analyzer.modifiesPixelActiveState = true;
		}
		else if (cfInstruction.type == GPU7_CF_INST_LOOP_START_DX10 || cfInstruction.type == GPU7_CF_INST_LOOP_END ||
				 cfInstruction.type == GPU7_CF_INST_LOOP_START_NO_AL)
		{
			shaderContext->analyzer.modifiesPixelActiveState = true;
			shaderContext->analyzer.hasLoops = true;
		}
		else if (cfInstruction.type == GPU7_CF_INST_LOOP_BREAK)
		{
			shaderContext->analyzer.modifiesPixelActiveState = true;
			shaderContext->analyzer.hasLoops = true;
		}
		else if (cfInstruction.type == GPU7_CF_INST_MEM_STREAM0_WRITE ||
			cfInstruction.type == GPU7_CF_INST_MEM_STREAM1_WRITE)
		{
			uint32 streamoutBufferIndex;
			if (cfInstruction.type == GPU7_CF_INST_MEM_STREAM0_WRITE)
				streamoutBufferIndex = 0;
			else if (cfInstruction.type == GPU7_CF_INST_MEM_STREAM1_WRITE)
				streamoutBufferIndex = 1;
			else
				cemu_assert_debug(false);
			shaderContext->analyzer.hasStreamoutWrite = true;
			cemu_assert(streamoutBufferIndex < shaderContext->output->streamoutBufferWriteMask.size());
			shaderContext->output->streamoutBufferWriteMask[streamoutBufferIndex] = true;
			uint32 vectorWriteSize = 0;
			for (sint32 f = 0; f < 4; f++)
			{
				if ((cfInstruction.memWriteCompMask & (1 << f)) != 0)
					vectorWriteSize = (f + 1) * 4;
				shaderContext->output->streamoutBufferStride[f] = shaderContext->contextRegisters[mmVGT_STRMOUT_VTX_STRIDE_0 + f * 4] << 2;
			}

			cemu_assert_debug((cfInstruction.exportArrayBase * 4 + vectorWriteSize) <= shaderContext->output->streamoutBufferStride[streamoutBufferIndex]);
		}
		else if (cfInstruction.type == GPU7_CF_INST_MEM_RING_WRITE)
		{
			if (cfInstruction.memWriteElemSize != 3)
				debugBreakpoint();
			if (cfInstruction.exportBurstCount != 0 && cfInstruction.memWriteElemSize != 3)
			{
				debugBreakpoint();
			}
			uint32 dwordWriteCount = (cfInstruction.exportBurstCount + 1) * 4;
			uint32 numRingParameter = (cfInstruction.exportArrayBase + dwordWriteCount) / 4;
			shader->ringParameterCount = std::max(shader->ringParameterCount, numRingParameter);
			for (uint32 i = 0; i < (cfInstruction.exportBurstCount + 1); i++)
			{
				shaderContext->analyzer.gprUseMask[(cfInstruction.exportSourceGPR + i) / 8] |= (1 << ((cfInstruction.exportSourceGPR + i) % 8));
			}
		}
		else if (cfInstruction.type == GPU7_CF_INST_EMIT_VERTEX)
		{
			shaderContext->analyzer.numEmitVertex++;
		}
		else if (cfInstruction.type == GPU7_CF_INST_CALL)
		{
		}
		else
			cemu_assert_unimplemented();
	}
	for (auto subroutineAddr : list_subroutineAddrs)
	{
		LatteDecompiler_analyzeSubroutine(shaderContext, subroutineAddr);
	}
	bool hasAnyDynamicBufferAccess = false;
	bool hasAnyBufferAccess = false;
	for(auto& it : shaderContext->analyzer.uniformBufferAccessTracker)
	{
		if( it.HasRelativeAccess() )
			hasAnyDynamicBufferAccess = true;
		if( it.HasAccess() )
			hasAnyBufferAccess = true;
	}
	if (hasAnyDynamicBufferAccess)
	{
		shader->uniformMode = LATTE_DECOMPILER_UNIFORM_MODE_FULL_CBANK;
	}
	else if(shaderContext->analyzer.uniformRegisterAccessTracker.HasRelativeAccess() )
	{
		shader->uniformMode = LATTE_DECOMPILER_UNIFORM_MODE_FULL_CFILE;
	}
	else if(hasAnyBufferAccess || shaderContext->analyzer.uniformRegisterAccessTracker.HasAccess() )
	{
		shader->uniformMode = LATTE_DECOMPILER_UNIFORM_MODE_REMAPPED;
	}
	else
	{
		shader->uniformMode = LATTE_DECOMPILER_UNIFORM_MODE_NONE;
	}
	cemu_assert_debug(shader->list_quickBufferList.empty());
	for (uint32 i = 0; i < LATTE_NUM_MAX_UNIFORM_BUFFERS; i++)
	{
		if( !shaderContext->analyzer.uniformBufferAccessTracker[i].HasAccess() )
			continue;
		LatteDecompilerShader::QuickBufferEntry entry;
		entry.index = i;
		entry.size = shaderContext->analyzer.uniformBufferAccessTracker[i].DetermineSize(shaderContext->shaderBaseHash, LATTE_GLSL_DYNAMIC_UNIFORM_BLOCK_SIZE) * 16;
		shader->list_quickBufferList.push_back(entry);
	}
	_LatteRegisterSetTextureUnit* texRegs = nullptr;
	if( shader->shaderType == LatteConst::ShaderType::Vertex )
		texRegs = shaderContext->contextRegistersNew->SQ_TEX_START_VS;
	else if( shader->shaderType == LatteConst::ShaderType::Pixel )
		texRegs = shaderContext->contextRegistersNew->SQ_TEX_START_PS;
	else if( shader->shaderType == LatteConst::ShaderType::Geometry )
		texRegs = shaderContext->contextRegistersNew->SQ_TEX_START_GS;

	for(sint32 i=0; i<LATTE_NUM_MAX_TEX_UNITS; i++)
	{
		if (!shaderContext->output->textureUnitMask[i])
		{
			shader->textureUnitDim[i] = (Latte::E_DIM)0xFF;
			continue;
		}
		auto& texUnit = texRegs[i];
		auto dim = texUnit.word0.get_DIM();
		shader->textureUnitDim[i] = dim;
		if(dim == Latte::E_DIM::DIM_CUBEMAP)
			shaderContext->analyzer.hasCubeMapTexture = true;
		shader->textureIsIntegerFormat[i] = texUnit.word4.get_NUM_FORM_ALL() == Latte::LATTE_SQ_TEX_RESOURCE_WORD4_N::E_NUM_FORMAT_ALL::NUM_FORMAT_INT;
	}
	shader->textureUnitListCount = 0;
	for (sint32 i = 0; i < LATTE_NUM_MAX_TEX_UNITS; i++)
	{
		if (shaderContext->output->textureUnitMask[i])
		{
			shader->textureUnitList[shader->textureUnitListCount] = i;
			shader->textureUnitListCount++;
		}
		shader->textureRenderTargetIndex[i] = 255;
	}
	if (shader->shaderType == LatteConst::ShaderType::Pixel)
	{
		struct {
		    sint32 index;
		    MPTR physAddr;
			Latte::E_GX2SURFFMT format;
			Latte::E_HWTILEMODE tileMode;
		} colorBuffers[LATTE_NUM_COLOR_TARGET]{};

        uint8 colorBufferMask = LatteMRT::GetActiveColorBufferMask(shader, *shaderContext->contextRegistersNew);
        sint32 colorBufferCount = 0;
		for (sint32 i = 0; i < LATTE_NUM_COLOR_TARGET; i++)
        {
            auto& colorBuffer = colorBuffers[colorBufferCount];
            if (((colorBufferMask) & (1 << i)) == 0)
                continue;

            uint32* colorBufferRegBase = shaderContext->contextRegisters + (mmCB_COLOR0_BASE + i);
           	uint32 regColorBufferBase = colorBufferRegBase[mmCB_COLOR0_BASE - mmCB_COLOR0_BASE] & 0xFFFFFF00;

            uint32 regColorInfo = colorBufferRegBase[mmCB_COLOR0_INFO - mmCB_COLOR0_BASE];

           	MPTR colorBufferPhysMem = regColorBufferBase;
            Latte::E_HWTILEMODE colorBufferTileMode = (Latte::E_HWTILEMODE)((regColorInfo >> 8) & 0xF);

            Latte::E_GX2SURFFMT colorBufferFormat = LatteMRT::GetColorBufferFormat(i, *shaderContext->contextRegistersNew);

            colorBuffer = {i, colorBufferPhysMem, colorBufferFormat, colorBufferTileMode};
            colorBufferCount++;
        }

	    for (sint32 i = 0; i < shader->textureUnitListCount; i++)
        {
            sint32 textureIndex = shader->textureUnitList[i];
      		const auto& texRegister = texRegs[textureIndex];

      		MPTR physAddr = (texRegister.word2.get_BASE_ADDRESS() << 8);
      		if (physAddr == MPTR_NULL)
                continue;

            auto tileMode = texRegister.word0.get_TILE_MODE();

            auto dim = shader->textureUnitDim[textureIndex];
            if (dim != Latte::E_DIM::DIM_2D)
                continue;

            auto lastMip = texRegister.word5.get_LAST_LEVEL();
            if (lastMip != 0)
                continue;

            Latte::E_GX2SURFFMT format = LatteTexture_ReconstructGX2Format(texRegister.word1, texRegister.word4);

            for (sint32 j = 0; j < colorBufferCount; j++)
            {
                const auto& colorBuffer = colorBuffers[j];

                if (physAddr == colorBuffer.physAddr && format == colorBuffer.format && tileMode == colorBuffer.tileMode)
                {
                    shader->textureRenderTargetIndex[textureIndex] = colorBuffer.index;
                    break;
                }
            }
        }
	}
	if (shader->shaderType == LatteConst::ShaderType::Geometry && shaderContext->parsedGSCopyShader->list_streamWrites.empty() == false)
	{
		shaderContext->analyzer.hasStreamoutWrite = true;
		if (shaderContext->contextRegisters[mmVGT_STRMOUT_EN] != 0)
			shaderContext->analyzer.hasStreamoutEnable = true;
		for (auto& it : shaderContext->parsedGSCopyShader->list_streamWrites)
		{
			shaderContext->output->streamoutBufferWriteMask[it.bufferIndex] = true;
			shaderContext->output->streamoutBufferStride[it.bufferIndex] =
				shaderContext->contextRegisters[mmVGT_STRMOUT_VTX_STRIDE_0 + it.bufferIndex * 4] << 2;
		}
	}
	if(shaderContext->analyzer.usesRelativeGPRRead && (shader->shaderType == LatteConst::ShaderType::Vertex || shader->shaderType == LatteConst::ShaderType::Geometry) )
	{
		if(shaderContext->fetchShader)
		{
			LatteFetchShader* parsedFetchShader = shaderContext->fetchShader;
			for(auto& bufferGroup : parsedFetchShader->bufferGroups)
			{
				for (sint32 i = 0; i < bufferGroup.attribCount; i++)
				{
					uint32 registerIndex;
					uint32 attributeShaderLoc = 0xFFFFFFFF;
					for (sint32 f = 0; f < 32; f++)
					{
						if (shaderContext->contextRegisters[mmSQ_VTX_SEMANTIC_0 + f] == bufferGroup.attrib[i].semanticId)
						{
							attributeShaderLoc = f;
							break;
						}
					}
					if (attributeShaderLoc == 0xFFFFFFFF)
						continue;
					registerIndex = attributeShaderLoc + 1;
					shaderContext->analyzer.gprUseMask[registerIndex / 8] |= (1 << (registerIndex % 8));
				}
			}
		}
	}
	else if (shaderContext->analyzer.usesRelativeGPRRead && shader->shaderType == LatteConst::ShaderType::Pixel)
	{
		LatteShaderPSInputTable* psInputTable = LatteSHRC_GetPSInputTable();
		for (sint32 i = 0; i < psInputTable->count; i++)
		{
			shaderContext->analyzer.gprUseMask[i / 8] |= (1 << (i % 8));
		}
	}
	sint32 cfCurrentStackDepth = 0;
	sint32 cfCurrentMaxStackDepth = 0;
	for(auto& cfInstruction : shaderContext->cfInstructions)
	{
		if (cfInstruction.type == GPU7_CF_INST_ALU)
		{
			cfInstruction.activeStackDepth = cfCurrentStackDepth;
		}
		else if (cfInstruction.type == GPU7_CF_INST_ALU_PUSH_BEFORE )
		{
			cfCurrentStackDepth++;
			cfCurrentMaxStackDepth = std::max(cfCurrentMaxStackDepth, cfCurrentStackDepth);
			cfInstruction.activeStackDepth = cfCurrentStackDepth;
		}
		else if (cfInstruction.type == GPU7_CF_INST_ALU_POP_AFTER)
		{
			cfInstruction.activeStackDepth = cfCurrentStackDepth;
			cfCurrentStackDepth--;
		}
		else if (cfInstruction.type == GPU7_CF_INST_ALU_POP2_AFTER)
		{
			cfInstruction.activeStackDepth = cfCurrentStackDepth;
			cfCurrentStackDepth -= 2;
		}
		else if (cfInstruction.type == GPU7_CF_INST_ALU_BREAK )
		{
			cfInstruction.activeStackDepth = cfCurrentStackDepth;
		}
		else if (cfInstruction.type == GPU7_CF_INST_ALU_ELSE_AFTER)
		{
			if (cfInstruction.popCount != 0)
				debugBreakpoint();
			cfInstruction.activeStackDepth = cfCurrentStackDepth;
		}
		else if (cfInstruction.type == GPU7_CF_INST_ELSE )
		{
			cfInstruction.activeStackDepth = cfCurrentStackDepth;
		}
		else if (cfInstruction.type == GPU7_CF_INST_POP)
		{
			cfInstruction.activeStackDepth = cfCurrentStackDepth;
			cfCurrentStackDepth -= cfInstruction.popCount;
			if (cfCurrentStackDepth < 0)
				debugBreakpoint();
		}
		else if (cfInstruction.type == GPU7_CF_INST_LOOP_START_DX10 || cfInstruction.type == GPU7_CF_INST_LOOP_END ||
				 cfInstruction.type == GPU7_CF_INST_LOOP_START_NO_AL)
		{
			cfInstruction.activeStackDepth = cfCurrentStackDepth;
		}
		else if (cfInstruction.type == GPU7_CF_INST_LOOP_BREAK)
		{
			cfInstruction.activeStackDepth = cfCurrentStackDepth;
		}
		else if (cfInstruction.type == GPU7_CF_INST_TEX)
		{
			cfInstruction.activeStackDepth = cfCurrentStackDepth;
		}
		else if (cfInstruction.type == GPU7_CF_INST_EXPORT || cfInstruction.type == GPU7_CF_INST_EXPORT_DONE)
		{
			cfInstruction.activeStackDepth = cfCurrentStackDepth;
		}
		else if (cfInstruction.type == GPU7_CF_INST_MEM_STREAM0_WRITE ||
			cfInstruction.type == GPU7_CF_INST_MEM_STREAM1_WRITE)
		{
			cfInstruction.activeStackDepth = cfCurrentStackDepth;
		}
		else if (cfInstruction.type == GPU7_CF_INST_MEM_RING_WRITE)
		{
			cfInstruction.activeStackDepth = cfCurrentStackDepth;
		}
		else if (cfInstruction.type == GPU7_CF_INST_EMIT_VERTEX)
		{
			cfInstruction.activeStackDepth = cfCurrentStackDepth;
		}
		else if (cfInstruction.type == GPU7_CF_INST_CALL)
		{
			cfInstruction.activeStackDepth = cfCurrentStackDepth;
		}
		else
		{
			cemu_assert_debug(false);
		}
	}
	shaderContext->analyzer.activeStackMaxDepth = cfCurrentMaxStackDepth;
	if (cfCurrentStackDepth != 0)
	{
		debug_printf("cfCurrentStackDepth is not zero after all CF instructions. depth is %d\n", cfCurrentStackDepth);
		cemu_assert_debug(false);
	}
	if(list_subroutineAddrs.empty() == false)
		cemuLog_logDebug(LogType::Force, "Todo - analyze shader subroutine CF stack");
	if (shaderContext->options->useTFViaSSBO && shaderContext->output->streamoutBufferWriteMask.any())
	{
		shaderContext->analyzer.useSSBOForStreamout = true;
	}
	if (shaderContext->shaderType == LatteConst::ShaderType::Vertex)
		shaderContext->output->resourceMappingVK.setIndex = 0;
	else if (shaderContext->shaderType == LatteConst::ShaderType::Pixel)
		shaderContext->output->resourceMappingVK.setIndex = 1;
	else if (shaderContext->shaderType == LatteConst::ShaderType::Geometry)
		shaderContext->output->resourceMappingVK.setIndex = 2;
	LatteDecompiler::_initTextureBindingPointsGL(shaderContext);
	LatteDecompiler::_initTextureBindingPointsVK(shaderContext);
#ifdef ENABLE_METAL
	LatteDecompiler::_initTextureBindingPointsMTL(shaderContext);
#endif
	LatteDecompiler::_initUniformBindingPoints(shaderContext);
	LatteDecompiler::_initAttributeBindingPoints(shaderContext);
	shaderContext->output->resourceMappingMTL.verticesPerInstanceBinding = shaderContext->currentBufferBindingPointMTL++;
	shaderContext->output->resourceMappingMTL.indexBufferBinding = shaderContext->currentBufferBindingPointMTL++;
	shaderContext->output->resourceMappingMTL.indexTypeBinding = shaderContext->currentBufferBindingPointMTL++;
#ifdef ENABLE_METAL
	if (g_renderer && g_renderer->GetType() == RendererAPI::Metal)
		shaderContext->output->resourceMappingMTL.argumentBufferBindingPoint = MetalArgumentBuffer::BindingIndex;
#endif
}

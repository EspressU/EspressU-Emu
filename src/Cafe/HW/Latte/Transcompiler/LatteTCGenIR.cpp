#include "Cafe/HW/Latte/Transcompiler/LatteTC.h"
#include "Cafe/HW/Latte/ISA/LatteInstructions.h"
#include "util/Zir/Core/ZpIRBuilder.h"

void LatteTCGenIR::CF_CALL_FS_emitFetchAttribute(LatteParsedFetchShaderAttribute_t& attribute, Latte::GPRType dstGPR)
{
	auto irBuilder = m_irGenContext.irBuilder;

	for (sint32 t = 0; t < 4; t++)
	{
		uint32 gprElementIndex = (uint32)dstGPR * 4 + t;
		LatteConst::VertexFetchDstSel ds = (LatteConst::VertexFetchDstSel)attribute.ds[t];

		switch (ds)
		{
		case LatteConst::VertexFetchDstSel::X:
		case LatteConst::VertexFetchDstSel::Y:
		case LatteConst::VertexFetchDstSel::Z:
		case LatteConst::VertexFetchDstSel::W:
		{
			uint8 channelIndex = (uint8)((uint32)ds - (uint32)LatteConst::VertexFetchDstSel::X);

			ZpIR::IRReg resultHolder = m_irGenContext.irBuilder->createReg(ZpIR::DataType::U32);
			ZpIR::LocationSymbolName importSource = ZpIR::ShaderSubset::ShaderImportLocation().SetVertexAttribute(attribute.semanticId, channelIndex);
			m_irGenContext.irBuilder->emit_IMPORT(importSource, resultHolder);

			if (attribute.endianSwap == LatteConst::VertexFetchEndianMode::SWAP_U32)
			{
				ZpIR::IRReg elementResult;
				irBuilder->emit_RR(ZpIR::IR::OpCode::SWAP_ENDIAN, irBuilder->createReg(elementResult, ZpIR::DataType::U32), resultHolder);
				resultHolder = elementResult;
			}

			bool isSigned = attribute.isSigned;

			LatteConst::VertexFetchFormat fmt = (LatteConst::VertexFetchFormat)attribute.format;
			LatteClauseInstruction_VTX::NUM_FORMAT_ALL nfa = (LatteClauseInstruction_VTX::NUM_FORMAT_ALL)attribute.nfa;

			if (fmt == LatteConst::VertexFetchFormat::VTX_FMT_32_32_32_FLOAT ||
				fmt == LatteConst::VertexFetchFormat::VTX_FMT_32_32_FLOAT)
			{
				uint32 numComp;
				if (fmt == LatteConst::VertexFetchFormat::VTX_FMT_32_32_32_FLOAT)
					numComp = 3;
				else if (fmt == LatteConst::VertexFetchFormat::VTX_FMT_32_32_FLOAT)
					numComp = 2;
				else
				{
					cemu_assert_debug(false);
				}

				cemu_assert_debug(attribute.endianSwap == LatteConst::VertexFetchEndianMode::SWAP_U32);
				cemu_assert_debug(nfa == LatteClauseInstruction_VTX::NUM_FORMAT_ALL::NUM_FORMAT_SCALED);
				cemu_assert_debug(channelIndex < numComp);

				ZpIR::IRReg elementResult;
				irBuilder->emit_RR(ZpIR::IR::OpCode::BITCAST, irBuilder->createReg(elementResult, ZpIR::DataType::F32), resultHolder);
				resultHolder = elementResult;
			}
			else if (fmt == LatteConst::VertexFetchFormat::VTX_FMT_8_8_8_8)
			{
				uint32 numComp;
				switch (fmt)
				{
				case LatteConst::VertexFetchFormat::VTX_FMT_8_8_8_8:
					numComp = 4;
					break;
				case LatteConst::VertexFetchFormat::VTX_FMT_8_8_8:
					numComp = 3;
					break;
				case LatteConst::VertexFetchFormat::VTX_FMT_8_8:
					numComp = 2;
					break;
				case LatteConst::VertexFetchFormat::VTX_FMT_8:
					numComp = 1;
					break;
				}

				cemu_assert_debug(attribute.endianSwap == LatteConst::VertexFetchEndianMode::SWAP_NONE);
				cemu_assert_debug(channelIndex < numComp);

				if (nfa == LatteClauseInstruction_VTX::NUM_FORMAT_ALL::NUM_FORMAT_NORM)
				{
					if (isSigned)
					{
						assert_dbg();
					}
					else
					{
						resultHolder = irBuilder->emit_RR(ZpIR::IR::OpCode::CONVERT_INT_TO_FLOAT, ZpIR::DataType::F32, resultHolder);
						resultHolder = irBuilder->emit_RRR(ZpIR::IR::OpCode::DIV, ZpIR::DataType::F32, resultHolder, irBuilder->createConstF32(255.0f));
					}
				}
				else
				{
					assert_dbg();
				}
			}
			else
			{
				assert_dbg();
			}

			this->m_irGenContext.activeVars.set(dstGPR, t, resultHolder);
			break;
		}
		case LatteConst::VertexFetchDstSel::CONST_0F:
		{
			ZpIR::IRReg resultHolder;
			irBuilder->emit_RR(ZpIR::IR::OpCode::MOV, irBuilder->createReg(resultHolder, ZpIR::DataType::F32), irBuilder->createConstF32(0.0f));
			this->m_irGenContext.activeVars.set(dstGPR, t, resultHolder);
			break;
		}
		case LatteConst::VertexFetchDstSel::CONST_1F:
		{
			ZpIR::IRReg resultHolder;
			irBuilder->emit_RR(ZpIR::IR::OpCode::MOV, irBuilder->createReg(resultHolder, ZpIR::DataType::F32), irBuilder->createConstF32(1.0f));
			this->m_irGenContext.activeVars.set(dstGPR, t, resultHolder);
			break;
		}
		default:
			assert_dbg();
		}
	}
}

void LatteTCGenIR::processCF_CALL_FS(const LatteCFInstruction_DEFAULT& cfInstruction)
{
	auto fetchShader = m_vertexShaderCtx.parsedFetchShader;
	auto semanticTable = m_vertexShaderCtx.vtxSemanticTable;

	cemu_assert_debug(fetchShader->bufferGroupsInvalid.size() == 0);
	for(auto& bufferGroup : fetchShader->bufferGroups)
	{
		for (sint32 i = 0; i < bufferGroup.attribCount; i++)
		{
			auto& attribute = bufferGroup.attrib[i];

			uint32 dstGPR = 0;
			uint32 attributeShaderLoc = 0xFFFFFFFF;
			for (sint32 f = 0; f < 32; f++)
			{
				if (semanticTable[f] == attribute.semanticId)
				{
					attributeShaderLoc = f;
					break;
				}
			}
			if (attributeShaderLoc == 0xFFFFFFFF)
				continue;
			dstGPR = attributeShaderLoc + 1;

			CF_CALL_FS_emitFetchAttribute(attribute, dstGPR);
		}
	}
}

ZpIR::IRReg LatteTCGenIR::getIRRegFromGPRElement(uint32 gprIndex, uint32 channel, ZpIR::DataType typeHint)
{
	ZpIR::IRReg r;
	if (m_irGenContext.activeVars.get(gprIndex, channel, r))
		return r;

	if (m_irGenContext.isEntryBasicBlock)
	{
		if (typeHint == ZpIR::DataType::F32)
			return m_irGenContext.irBuilder->createConstF32(0.0f);
		else if (typeHint == ZpIR::DataType::U32)
			return m_irGenContext.irBuilder->createConstU32(0);
		else if (typeHint == ZpIR::DataType::S32)
			return m_irGenContext.irBuilder->createConstS32(0);
		cemu_assert_debug(false);
	}

	r = m_irGenContext.irBuilder->createReg(typeHint);
	m_irGenContext.irBuilder->addImport(r, gprIndex*4 + channel);

	return r;
}

ZpIR::IRReg LatteTCGenIR::getTypedIRRegFromGPRElement(uint32 gprIndex, uint32 channel, ZpIR::DataType type)
{
	auto irReg = getIRRegFromGPRElement(gprIndex, channel, type);
	if (m_irGenContext.irBuilder->getRegType(irReg) == type)
		return irReg;
	auto newReg = m_irGenContext.irBuilder->createReg(type);
	m_irGenContext.irBuilder->emit_RR(ZpIR::IR::OpCode::BITCAST, newReg, irReg);
	m_irGenContext.activeVars.set(gprIndex, channel, newReg);
	return newReg;
}

ZpIR::DataType _guessTypeFromConstantValue(uint32 bits)
{
	if (bits == 0x3F800000)
		return ZpIR::DataType::F32;
	return ZpIR::DataType::S32;
}

ZpIR::IRReg LatteTCGenIR::loadALUOperand(LatteALUSrcSel srcSel, uint8 srcChan, bool isNeg, bool isAbs, bool isRel, uint8 indexMode, const uint32* literalData, ZpIR::DataType typeHint, bool convertOnTypeMismatch)
{
	if (srcSel.isGPR())
	{
		if (isRel)
			assert_dbg();

		ZpIR::IRReg reg;

		if(convertOnTypeMismatch)
			reg = getTypedIRRegFromGPRElement(srcSel.getGPR(), srcChan, typeHint);
		else
			reg = getIRRegFromGPRElement(srcSel.getGPR(), srcChan, typeHint);

		if (isAbs || isNeg)
		{
			assert_dbg();
		}

		return reg;
	}
	else if (srcSel.isAnyConst())
	{
		if (srcSel.isConst_0F())
		{
			return m_irGenContext.irBuilder->createConstF32(0.0f);
		}
		else
			assert_dbg();
	}
	else if (srcSel.isLiteral())
	{
		return m_irGenContext.irBuilder->createTypedConst(literalData[srcChan], _guessTypeFromConstantValue(literalData[srcChan]));
	}
	else if (srcSel.isCFile())
	{
		uint32 cfileIndex = srcSel.getCFile();
		auto newReg = m_irGenContext.irBuilder->createReg(typeHint);
		ZpIR::LocationSymbolName importSource = ZpIR::ShaderSubset::ShaderImportLocation().SetUniformRegister(cfileIndex*4 + srcChan);
		m_irGenContext.irBuilder->emit_IMPORT(importSource, newReg);
		return newReg;
	}
	else
		assert_dbg();

	return 0;
}

void LatteTCGenIR::emitALUGroup(const LatteClauseInstruction_ALU* aluUnit[5], const uint32* literalData)
{

	ZpIR::BasicBlockBuilder* irBuilder = m_irGenContext.irBuilder;

	auto getMOVSourceType = [&](const LatteClauseInstruction_ALU_OP2* instrOP2) -> ZpIR::DataType
	{
		auto sel = instrOP2->getSrc0Sel();
		if (sel.isGPR())
		{
			ZpIR::IRReg r;
			if (!m_irGenContext.activeVars.get(sel.getGPR(), instrOP2->getSrc0Chan(), r))
			{
				return ZpIR::DataType::F32;
			}
			return m_irGenContext.irBuilder->getRegType(r);
		}
		else if (sel.isLiteral())
		{
			return _guessTypeFromConstantValue(literalData[instrOP2->getSrc0Chan()]);
		}
		else
			assert_dbg();
		return ZpIR::DataType::S32;
	};

	auto getOp0Reg = [&](const LatteClauseInstruction_ALU_OP2* instrOP2, ZpIR::DataType type) -> ZpIR::IRReg
	{
		ZpIR::IRReg r = loadALUOperand(instrOP2->getSrc0Sel(), instrOP2->getSrc0Chan(), instrOP2->isSrc0Neg(), false, instrOP2->isSrc0Rel(), instrOP2->getIndexMode(), literalData, type, true);
		cemu_assert_debug(irBuilder->getRegType(r) == type);
		return r;
	};

	auto getOp1Reg = [&](const LatteClauseInstruction_ALU_OP2* instrOP2, ZpIR::DataType type) -> ZpIR::IRReg
	{
		ZpIR::IRReg r = loadALUOperand(instrOP2->getSrc1Sel(), instrOP2->getSrc1Chan(), instrOP2->isSrc1Neg(), false, instrOP2->isSrc1Rel(), instrOP2->getIndexMode(), literalData, type, true);
		cemu_assert_debug(irBuilder->getRegType(r) == type);
		return r;
	};

	auto getResultReg = [&](uint8 aluUnit, const LatteClauseInstruction_ALU_OP2* instrOP2, ZpIR::DataType type) -> ZpIR::IRReg
	{
		ZpIR::IRReg r = m_irGenContext.irBuilder->createReg(type);

		cemu_assert_debug(instrOP2->getDestClamp() == 0);
		cemu_assert_debug(instrOP2->getDestRel() == 0);
		cemu_assert_debug(instrOP2->getOMod() == 0);

		if (instrOP2->getWriteMask())
		{
			m_irGenContext.activeVars.setAfterGroup(aluUnit, instrOP2->getDestGpr(), instrOP2->getDestElem(), r);
		}
		else
		{
			assert_dbg();
		}

		return r;
	};

	for (sint32 aluUnitIndex = 0; aluUnitIndex < 5; aluUnitIndex++)
	{
		const LatteClauseInstruction_ALU* instr = aluUnit[aluUnitIndex];
		if (instr == nullptr)
			continue;
		if (instr->isOP3())
		{
			assert_dbg();
		}
		else
		{
			auto opcode2 = instr->getOP2Code();
			auto instrOP2 = instr->getOP2Instruction();

			uint8 indexMode = instrOP2->getIndexMode();

			switch (opcode2)
			{
			case LatteClauseInstruction_ALU::OPCODE_OP2::MUL:
			case LatteClauseInstruction_ALU::OPCODE_OP2::MUL_IEEE:
			{
				irBuilder->emit_RRR(ZpIR::IR::OpCode::MUL, getResultReg(aluUnitIndex, instrOP2, ZpIR::DataType::F32), getOp0Reg(instrOP2, ZpIR::DataType::F32), getOp1Reg(instrOP2, ZpIR::DataType::F32));
				break;
			}
			case LatteClauseInstruction_ALU::OPCODE_OP2::MOV:
			{
				ZpIR::DataType guessedType = getMOVSourceType(instrOP2);

				irBuilder->emit_RR(ZpIR::IR::OpCode::MOV, getResultReg(aluUnitIndex, instrOP2, guessedType), getOp0Reg(instrOP2, guessedType));
				break;
			}
			case LatteClauseInstruction_ALU::OPCODE_OP2::DOT4:
			{
				cemu_assert_debug(aluUnitIndex == 0);
				cemu_assert_debug(aluUnit[0]->getOP2Code() == aluUnit[1]->getOP2Code());
				cemu_assert_debug(aluUnit[1]->getOP2Code() == aluUnit[2]->getOP2Code());
				cemu_assert_debug(aluUnit[2]->getOP2Code() == aluUnit[3]->getOP2Code());

				auto unit_x = instrOP2;
				auto unit_y = aluUnit[1]->getOP2Instruction();
				auto unit_z = aluUnit[2]->getOP2Instruction();
				auto unit_w = aluUnit[3]->getOP2Instruction();

				cemu_assert_debug(unit_x->getDestClamp() == false);
				cemu_assert_debug(unit_x->getOMod() == 0);
				cemu_assert_debug(unit_x->getDestRel() == false);

				ZpIR::IRReg productX = irBuilder->emit_RRR(ZpIR::IR::OpCode::MUL, ZpIR::DataType::F32, getOp0Reg(unit_x, ZpIR::DataType::F32), getOp1Reg(unit_x, ZpIR::DataType::F32));
				ZpIR::IRReg productY = irBuilder->emit_RRR(ZpIR::IR::OpCode::MUL, ZpIR::DataType::F32, getOp0Reg(unit_y, ZpIR::DataType::F32), getOp1Reg(unit_y, ZpIR::DataType::F32));
				ZpIR::IRReg productZ = irBuilder->emit_RRR(ZpIR::IR::OpCode::MUL, ZpIR::DataType::F32, getOp0Reg(unit_z, ZpIR::DataType::F32), getOp1Reg(unit_z, ZpIR::DataType::F32));
				ZpIR::IRReg productW = irBuilder->emit_RRR(ZpIR::IR::OpCode::MUL, ZpIR::DataType::F32, getOp0Reg(unit_w, ZpIR::DataType::F32), getOp1Reg(unit_w, ZpIR::DataType::F32));

				ZpIR::IRReg sum = irBuilder->emit_RRR(ZpIR::IR::OpCode::ADD, ZpIR::DataType::F32, productX, productY);
				sum = irBuilder->emit_RRR(ZpIR::IR::OpCode::ADD, ZpIR::DataType::F32, sum, productZ);
				sum = irBuilder->emit_RRR(ZpIR::IR::OpCode::ADD, ZpIR::DataType::F32, sum, productW);

				if (unit_x->getWriteMask())
					m_irGenContext.activeVars.setAfterGroup(0, unit_x->getDestGpr(), unit_x->getDestElem(), sum);
				if (unit_y->getWriteMask())
					m_irGenContext.activeVars.setAfterGroup(1, unit_y->getDestGpr(), unit_y->getDestElem(), sum);
				if (unit_z->getWriteMask())
					m_irGenContext.activeVars.setAfterGroup(2, unit_z->getDestGpr(), unit_z->getDestElem(), sum);
				if (unit_w->getWriteMask())
					m_irGenContext.activeVars.setAfterGroup(3, unit_w->getDestGpr(), unit_w->getDestElem(), sum);

				m_irGenContext.activeVars.setAfterGroupPVPS(0, sum);

				aluUnitIndex += 3;
				continue;;
			}
			default:
				assert_dbg();
			}

			if (instrOP2->getDestClamp())
			{
				assert_dbg();
			}

		}
	}
	m_irGenContext.activeVars.applyDelayedAfterGroup();
}

void LatteTCGenIR::processCF_ALU(const LatteCFInstruction_ALU& cfInstruction)
{
	uint32 aluAddr = cfInstruction.getField_ADDR();
	uint32 aluCount = cfInstruction.getField_COUNT();

	const uint32* clauseCode = m_ctx.programData + aluAddr * 2;
	uint32 clauseLength = aluCount;

	const LatteClauseInstruction_ALU* aluCode = (const LatteClauseInstruction_ALU*)clauseCode;

	const LatteClauseInstruction_ALU* aluUnit[5] = {};

	const LatteClauseInstruction_ALU* instr = aluCode;
	const LatteClauseInstruction_ALU* instrLast = aluCode + clauseLength;

	uint8 literalMask = 0;
	while (instr < instrLast)
	{
		if (instr->isOP3())
		{
			assert_dbg();
		}
		else
		{
			LatteClauseInstruction_ALU::OPCODE_OP2 opcode2 = instr->getOP2Code();
			const LatteClauseInstruction_ALU_OP2* op = instr->getOP2Instruction();
			uint32 unitIndex = 0;
			if (op->isTranscedentalUnit())
				unitIndex = 4;
			else
			{
				unitIndex = op->getDestElem();
				if (aluUnit[unitIndex])
					unitIndex = 4;
			}
			cemu_assert_debug(!aluUnit[unitIndex]);
			aluUnit[unitIndex] = op;

			if (op->getSrc0Sel().isLiteral())
				literalMask |= (op->getSrc0Chan() >= 2 ? 2 : 1);
			if (op->getSrc1Sel().isLiteral())
				literalMask |= (op->getSrc1Chan() >= 2 ? 2 : 1);
		}

		if (instr->isLastInGroup())
		{
			const uint32* literalData = nullptr;
			if (literalMask)
			{
				literalData = (const uint32*)(instr + 1);
				if (literalMask & 2)
					instr += 2;
				else
					instr += 1;
				if ((instr + 1) > instrLast)
					assert_dbg();
			}
			emitALUGroup(aluUnit, literalData);
			std::fill(aluUnit, aluUnit + 5, nullptr);
			literalMask = 0;
		}
		instr++;
	}
	if (aluUnit[0] || aluUnit[1] || aluUnit[2] || aluUnit[3] || aluUnit[4])
		assert_dbg();
}

void LatteTCGenIR::processCF_EXPORT(const LatteCFInstruction_EXPORT_IMPORT& cfInstruction)
{
	auto exportType = cfInstruction.getField_TYPE();

	cemu_assert_debug(cfInstruction.getField_BURST_COUNT() == 1);

	uint32 arrayBase = cfInstruction.getField_ARRAY_BASE();

	cemu_assert_debug(cfInstruction.isEncodingBUF() == false);

	LatteCFInstruction_EXPORT_IMPORT::COMPSEL sel[4];
	sel[0] = cfInstruction.getSwizField_SEL_X();
	sel[1] = cfInstruction.getSwizField_SEL_Y();
	sel[2] = cfInstruction.getSwizField_SEL_Z();
	sel[3] = cfInstruction.getSwizField_SEL_W();

	uint32 sourceGPR = cfInstruction.getField_RW_GPR();

	ZpIR::DataType typeHint;
	if (exportType == LatteCFInstruction_EXPORT_IMPORT::EXPORT_TYPE::POSITION)
		typeHint = ZpIR::DataType::F32;
	else if (exportType == LatteCFInstruction_EXPORT_IMPORT::EXPORT_TYPE::PARAMETER)
	{
		typeHint = ZpIR::DataType::F32;
	}
	else
		assert_dbg();

	ZpIR::IRReg regArray[4];
	size_t regExportCount = 0;
	for (size_t i = 0; i < 4; i++)
	{
		switch (sel[i])
		{
		case LatteCFInstruction_EXPORT_IMPORT::COMPSEL::X:
		case LatteCFInstruction_EXPORT_IMPORT::COMPSEL::Y:
		case LatteCFInstruction_EXPORT_IMPORT::COMPSEL::Z:
		case LatteCFInstruction_EXPORT_IMPORT::COMPSEL::W:
		{
			uint32 channelIndex = (uint32)sel[i];
			regArray[regExportCount] = getTypedIRRegFromGPRElement(sourceGPR, channelIndex, typeHint);
			regExportCount++;
			break;
		}
		default:
		{
			assert_dbg();
			break;
		}
		}
	}

	ZpIR::ShaderSubset::ShaderExportLocation loc;

	if (exportType == LatteCFInstruction_EXPORT_IMPORT::EXPORT_TYPE::POSITION)
	{
		loc.SetPosition();
	}
	else if (exportType == LatteCFInstruction_EXPORT_IMPORT::EXPORT_TYPE::PARAMETER)
	{
		loc.SetOutputAttribute(arrayBase);
	}
	else
	{
		assert_dbg();
	}

	cemu_assert_debug(regExportCount == 4);

	m_irGenContext.irBuilder->emit_EXPORT(loc, std::span(regArray, regArray + regExportCount));

}

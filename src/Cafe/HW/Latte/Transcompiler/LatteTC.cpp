#include "Cafe/HW/Latte/Transcompiler/LatteTC.h"
#include "Cafe/HW/Latte/ISA/LatteInstructions.h"

#include "util/Zir/Core/IR.h"
#include "util/Zir/Core/ZpIRBuilder.h"
#include "util/Zir/Core/ZpIRDebug.h"

class CFBlockNode
{
public:
	CFBlockNode(uint32 cfAddress, const LatteCFInstruction& cfInstruction) : cfAddress(cfAddress), cfNext(cfAddress + sizeof(LatteCFInstruction))
	{
		m_cfInstructions.emplace_back(cfInstruction);
		irBasicBlock = new ZpIR::ZpIRBasicBlock();
	};

	void addInstruction(const LatteCFInstruction& cfInstruction)
	{
		m_cfInstructions.emplace_back(cfInstruction);
	}

	void setNextAddress(uint32 addr)
	{
		cfNext = addr;
	}

	uint32 cfAddress;
	uint32 cfNext{ 0 };

	ZpIR::ZpIRBasicBlock* irBasicBlock{};

	std::vector<LatteCFInstruction> m_cfInstructions;

private:
};

void LatteTCGenIR::genIRForNode(CFBlockNode& node)
{
	m_irGenContext.reset();
	m_irGenContext.irBuilder = new ZpIR::BasicBlockBuilder(node.irBasicBlock);
	m_irGenContext.isEntryBasicBlock = (&node == m_ctx.mainFunctionDAG.GetEntryNode());

	if (&node == m_ctx.mainFunctionDAG.GetEntryNode() && m_ctx.shaderType == SHADER_TYPE::VERTEX)
	{
		for (uint32 channel = 0; channel < 4; channel++)
		{
			ZpIR::IRReg irReg = m_irGenContext.irBuilder->createReg(ZpIR::DataType::U32);
			m_irGenContext.irBuilder->emit_RR(ZpIR::IR::OpCode::MOV, irReg, m_irGenContext.irBuilder->createConstU32(0));
			this->m_irGenContext.activeVars.set(0, channel, irReg);
		}
	}

	for (auto& itr : node.m_cfInstructions)
	{
		const auto opcode = itr.getField_Opcode();
		if (const auto cfInstr = itr.getParserIfOpcodeMatch<LatteCFInstruction_DEFAULT>())
		{
			if(opcode == LatteCFInstruction::OPCODE::INST_CALL_FS)
				processCF_CALL_FS(*cfInstr);
			else
				assert_dbg();
		}
		else if (const auto cfInstr = itr.getParserIfOpcodeMatch<LatteCFInstruction_ALU>())
		{
			if (opcode == LatteCFInstruction::OPCODE::INST_ALU)
				processCF_ALU(*cfInstr);
			else
				assert_dbg();
		}
		else if (const auto cfInstr = itr.getParserIfOpcodeMatch<LatteCFInstruction_EXPORT_IMPORT>())
		{
			if (opcode == LatteCFInstruction::OPCODE::INST_EXPORT ||
				opcode == LatteCFInstruction::OPCODE::INST_EXPORT_DONE)
				processCF_EXPORT(*cfInstr);
			else
				assert_dbg();
		}
		else
		{
			debug_printf("Missing implementation for CF opcode 0x%02x\n", itr.getField_Opcode());
			assert_dbg();
		}
	}
}

void LatteTCGenIR::parseCF_createNodes(NodeDAG& nodeDAG)
{
	const LatteCFInstruction* cfCode = (const LatteCFInstruction*)m_ctx.programData;
	const size_t cfMaxCount = m_ctx.programSize / 8;

	cemu_assert_debug(cfMaxCount != 0);

	CFBlockNode* activeNode = new CFBlockNode(0, cfCode[0]);
	size_t cfIndex = 1;
	while (cfIndex < cfMaxCount)
	{
		const LatteCFInstruction* baseInstr = cfCode + cfIndex;
		cfIndex++;
		bool canMerge;
		bool isALU = false;
		if (const auto cfInstr = baseInstr->getParserIfOpcodeMatch<LatteCFInstruction_DEFAULT>())
		{
			cemu_assert_debug(cfInstr->getField_WHOLE_QUAD_MODE() == 0);

			cemu_assert_debug(cfInstr->getField_CALL_COUNT() == 0);
			cemu_assert_debug(cfInstr->getField_POP_COUNT() == 0);

			auto cond = cfInstr->getField_COND();

			assert_dbg();
		}
		else if (const auto cfInstr = baseInstr->getParserIfOpcodeMatch<LatteCFInstruction_ALU>())
		{
			canMerge = true;
			isALU = true;
		}
		else if (const auto cfInstr = baseInstr->getParserIfOpcodeMatch<LatteCFInstruction_EXPORT_IMPORT>())
		{
			canMerge = true;
		}
		else
		{
			debug_printf("Missing implementation for CF opcode 0x%02x\n", baseInstr->getField_Opcode());
			assert_dbg();
		}

		if (canMerge)
		{
			activeNode->addInstruction(*baseInstr);
		}
		else
		{
			activeNode->setNextAddress((uint32)cfIndex - 1);
			nodeDAG.m_nodes.emplace_back(activeNode);
			activeNode = new CFBlockNode((uint32)cfIndex - 1, *baseInstr);
		}

		if (!isALU && baseInstr->getField_END_OF_PROGRAM())
			break;
	}
	cemu_assert_debug(!activeNode->m_cfInstructions.empty());
	nodeDAG.m_nodes.emplace_back(activeNode);
}

void LatteTCGenIR::parseCFToDAG()
{
	parseCF_createNodes(m_ctx.mainFunctionDAG);
	cemu_assert_debug(m_ctx.mainFunctionDAG.m_nodes.size() == 1);
	for (auto& itr : m_ctx.mainFunctionDAG.m_nodes)
		m_ctx.irObject->m_basicBlocks.emplace_back(itr->irBasicBlock);
	m_ctx.irObject->m_entryBlocks.emplace_back(m_ctx.mainFunctionDAG.m_nodes[0]->irBasicBlock);
}

void LatteTCGenIR::emitIR()
{
	cemu_assert_debug(m_ctx.mainFunctionDAG.m_nodes.size() == 1);

	for (auto& itr : m_ctx.mainFunctionDAG.m_nodes)
	{
		genIRForNode(*itr);
	}

}

void LatteTCGenIR::cleanup()
{
}

void LatteTCGenIR::setVertexShaderContext(const LatteFetchShader* parsedFetchShader, const uint32* vtxSemanticTable)
{
	m_vertexShaderCtx.parsedFetchShader = parsedFetchShader;
	m_vertexShaderCtx.vtxSemanticTable = vtxSemanticTable;
}

ZpIR::ZpIRFunction* LatteTCGenIR::transcompileLatteToIR(const void* programData, uint32 programSize, SHADER_TYPE shaderType)
{
	ZpIR::ZpIRFunction* irObject = new ZpIR::ZpIRFunction();

	m_ctx = {};
	m_ctx.programData = (const uint32*)programData;
	m_ctx.programSize = programSize;
	m_ctx.irObject = irObject;
	m_ctx.shaderType = shaderType;

	parseCFToDAG();
	emitIR();
	cleanup();
	return irObject;
}
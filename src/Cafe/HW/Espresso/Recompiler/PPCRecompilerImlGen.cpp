#include "Cafe/HW/Espresso/Interpreter/PPCInterpreterInternal.h"
#include "Cafe/HW/Espresso/Interpreter/PPCInterpreterHelper.h"
#include "Cafe/HW/Espresso/EspressoISA.h"
#include "PPCRecompiler.h"
#include "PPCRecompilerIml.h"
#include "IML/IML.h"
#include "IML/IMLRegisterAllocatorRanges.h"
#include "PPCFunctionBoundaryTracker.h"
#include "Cafe/HW/Latte/Core/LatteBufferCache.h"
#include "Cafe/OS/libs/coreinit/coreinit_Time.h"

bool PPCRecompiler_decodePPCInstruction(ppcImlGenContext_t* ppcImlGenContext);

struct PPCBasicBlockInfo
{
	PPCBasicBlockInfo(uint32 startAddress, const std::set<uint32>& entryAddresses) : startAddress(startAddress), lastAddress(startAddress)
	{
		isEnterable = entryAddresses.find(startAddress) != entryAddresses.end();
	}

	uint32 startAddress;
	uint32 lastAddress;
	bool isEnterable{ false };
	bool hasContinuedFlow{ true };
	bool hasBranchTarget{ false };
	uint32 branchTarget{};

	IMLSegment* firstSegment{};
	IMLSegment* appendSegment{};

	void SetInitialSegment(IMLSegment* seg)
	{
		cemu_assert_debug(!firstSegment);
		cemu_assert_debug(!appendSegment);
		firstSegment = seg;
		appendSegment = seg;
	}

	IMLSegment* GetFirstSegmentInChain()
	{
		return firstSegment;
	}

	IMLSegment* GetSegmentForInstructionAppend()
	{
		return appendSegment;
	}
};

IMLInstruction* PPCRecompilerImlGen_generateNewEmptyInstruction(ppcImlGenContext_t* ppcImlGenContext)
{
	IMLInstruction& inst = ppcImlGenContext->currentOutputSegment->imlList.emplace_back();
	memset(&inst, 0x00, sizeof(IMLInstruction));
	return &inst;
}

void PPCRecompilerImlGen_generateNewInstruction_r_memory_indexed(ppcImlGenContext_t* ppcImlGenContext, IMLReg registerDestination, IMLReg registerMemory1, IMLReg registerMemory2, uint32 copyWidth, bool signExtend, bool switchEndian)
{
	cemu_assert_debug(registerMemory1.IsValid());
	cemu_assert_debug(registerMemory2.IsValid());
	cemu_assert_debug(registerDestination.IsValid());
	IMLInstruction* imlInstruction = PPCRecompilerImlGen_generateNewEmptyInstruction(ppcImlGenContext);
	imlInstruction->type = PPCREC_IML_TYPE_LOAD_INDEXED;
	imlInstruction->operation = 0;
	imlInstruction->op_storeLoad.registerData = registerDestination;
	imlInstruction->op_storeLoad.registerMem = registerMemory1;
	imlInstruction->op_storeLoad.registerMem2 = registerMemory2;
	imlInstruction->op_storeLoad.copyWidth = copyWidth;
	imlInstruction->op_storeLoad.flags2.swapEndian = switchEndian;
	imlInstruction->op_storeLoad.flags2.signExtend = signExtend;
}

void PPCRecompilerImlGen_generateNewInstruction_memory_r_indexed(ppcImlGenContext_t* ppcImlGenContext, IMLReg registerDestination, IMLReg registerMemory1, IMLReg registerMemory2, uint32 copyWidth, bool signExtend, bool switchEndian)
{
	cemu_assert_debug(registerMemory1.IsValid());
	cemu_assert_debug(registerMemory2.IsValid());
	cemu_assert_debug(registerDestination.IsValid());
	IMLInstruction* imlInstruction = PPCRecompilerImlGen_generateNewEmptyInstruction(ppcImlGenContext);
	imlInstruction->type = PPCREC_IML_TYPE_STORE_INDEXED;
	imlInstruction->operation = 0;
	imlInstruction->op_storeLoad.registerData = registerDestination;
	imlInstruction->op_storeLoad.registerMem = registerMemory1;
	imlInstruction->op_storeLoad.registerMem2 = registerMemory2;
	imlInstruction->op_storeLoad.copyWidth = copyWidth;
	imlInstruction->op_storeLoad.flags2.swapEndian = switchEndian;
	imlInstruction->op_storeLoad.flags2.signExtend = signExtend;
}

void PPCIMLGen_CreateSegmentBranchedPath(ppcImlGenContext_t& ppcImlGenContext, PPCBasicBlockInfo& basicBlockInfo, const std::function<void(ppcImlGenContext_t&)>& genSegmentBranchTaken, const std::function<void(ppcImlGenContext_t&)>& genSegmentBranchNotTaken)
{
	IMLSegment* currentWriteSegment = basicBlockInfo.GetSegmentForInstructionAppend();

	std::span<IMLSegment*> segments = ppcImlGenContext.InsertSegments(ppcImlGenContext.GetSegmentIndex(currentWriteSegment) + 1, 3);
	IMLSegment* segBranchNotTaken = segments[0];
	IMLSegment* segBranchTaken = segments[1];
	IMLSegment* segMerge = segments[2];

	segMerge->SetLinkBranchTaken(currentWriteSegment->GetBranchTaken());
	segMerge->SetLinkBranchNotTaken(currentWriteSegment->GetBranchNotTaken());
	currentWriteSegment->SetLinkBranchTaken(segBranchTaken);
	currentWriteSegment->SetLinkBranchNotTaken(segBranchNotTaken);
	segBranchTaken->SetLinkBranchNotTaken(segMerge);
	segBranchNotTaken->SetLinkBranchTaken(segMerge);
	ppcImlGenContext.currentOutputSegment = segBranchTaken;
	genSegmentBranchTaken(ppcImlGenContext);
	cemu_assert_debug(ppcImlGenContext.currentOutputSegment == segBranchTaken);
	ppcImlGenContext.currentOutputSegment = segBranchNotTaken;
	genSegmentBranchNotTaken(ppcImlGenContext);
	cemu_assert_debug(ppcImlGenContext.currentOutputSegment == segBranchNotTaken);
	ppcImlGenContext.emitInst().make_jump();
	ppcImlGenContext.currentOutputSegment = segMerge;
	basicBlockInfo.appendSegment = segMerge;
}

void PPCIMLGen_CreateSegmentBranchedPath(ppcImlGenContext_t& ppcImlGenContext, PPCBasicBlockInfo& basicBlockInfo, const std::function<void(ppcImlGenContext_t&)>& genSegmentBranchNotTaken)
{
	IMLSegment* currentWriteSegment = basicBlockInfo.GetSegmentForInstructionAppend();

	std::span<IMLSegment*> segments = ppcImlGenContext.InsertSegments(ppcImlGenContext.GetSegmentIndex(currentWriteSegment) + 1, 2);
	IMLSegment* segBranchNotTaken = segments[0];
	IMLSegment* segMerge = segments[1];

	segMerge->SetLinkBranchTaken(currentWriteSegment->GetBranchTaken());
	segMerge->SetLinkBranchNotTaken(currentWriteSegment->GetBranchNotTaken());
	currentWriteSegment->SetLinkBranchTaken(segMerge);
	currentWriteSegment->SetLinkBranchNotTaken(segBranchNotTaken);
	segBranchNotTaken->SetLinkBranchNotTaken(segMerge);
	ppcImlGenContext.currentOutputSegment = segBranchNotTaken;
	genSegmentBranchNotTaken(ppcImlGenContext);
	cemu_assert_debug(ppcImlGenContext.currentOutputSegment == segBranchNotTaken);
	ppcImlGenContext.currentOutputSegment = segMerge;
	basicBlockInfo.appendSegment = segMerge;
}

IMLReg _GetRegTemporaryS8(ppcImlGenContext_t* ppcImlGenContext, uint32 index);

IMLRedirectInstOutput::IMLRedirectInstOutput(ppcImlGenContext_t* ppcImlGenContext, IMLSegment* outputSegment) : m_context(ppcImlGenContext)
{
	m_prevSegment = ppcImlGenContext->currentOutputSegment;
	cemu_assert_debug(ppcImlGenContext->currentOutputSegment == ppcImlGenContext->currentBasicBlock->appendSegment);
	if (outputSegment == ppcImlGenContext->currentOutputSegment)
	{
		m_prevSegment = nullptr;
		return;
	}
	m_context->currentBasicBlock->appendSegment = outputSegment;
	m_context->currentOutputSegment = outputSegment;
}

IMLRedirectInstOutput::~IMLRedirectInstOutput()
{
	if (m_prevSegment)
	{
		m_context->currentBasicBlock->appendSegment = m_prevSegment;
		m_context->currentOutputSegment = m_prevSegment;
	}
}

void PPCIMLGen_CreateSegmentBranchedPathMultiple(ppcImlGenContext_t& ppcImlGenContext, PPCBasicBlockInfo& basicBlockInfo, IMLSegment** segmentsOut, IMLReg compareReg, sint32* compareValues, sint32 count, sint32 defaultCaseIndex)
{
	IMLSegment* currentWriteSegment = basicBlockInfo.GetSegmentForInstructionAppend();
	cemu_assert_debug(!currentWriteSegment->HasSuffixInstruction());

	const sint32 numBranchSegments = count + 1;
	const sint32 numCaseSegments = count;

	std::span<IMLSegment*> segments = ppcImlGenContext.InsertSegments(ppcImlGenContext.GetSegmentIndex(currentWriteSegment) + 1, numBranchSegments - 1 + numCaseSegments + 1);
	IMLSegment** extraBranchSegments = segments.data();
	IMLSegment** caseSegments = segments.data() + numBranchSegments - 1;
	IMLSegment* mergeSegment = segments[numBranchSegments - 1 + numCaseSegments];

	mergeSegment->SetLinkBranchTaken(currentWriteSegment->GetBranchTaken());
	mergeSegment->SetLinkBranchNotTaken(currentWriteSegment->GetBranchNotTaken());
	currentWriteSegment->SetLinkBranchTaken(nullptr);
	currentWriteSegment->SetLinkBranchNotTaken(nullptr);

	for (sint32 i=0; i<numCaseSegments; i++)
		segmentsOut[i] = caseSegments[i];

	IMLReg tmpBoolReg = _GetRegTemporaryS8(&ppcImlGenContext, 2);

	auto GetBranchSegment = [&](sint32 index) {
		if (index == 0)
			return currentWriteSegment;
		else
			return extraBranchSegments[index - 1];
	};
	for (sint32 i=0; i<numBranchSegments; i++)
	{
		IMLSegment* seg = GetBranchSegment(i);
		if (i < numBranchSegments - 1)
		{
			cemu_assert_debug(i < numCaseSegments);
			seg->SetLinkBranchTaken(caseSegments[i]);
			seg->SetLinkBranchNotTaken(GetBranchSegment(i + 1));
			seg->AppendInstruction()->make_compare_s32(compareReg, compareValues[i], tmpBoolReg, IMLCondition::EQ);
			seg->AppendInstruction()->make_conditional_jump(tmpBoolReg, true);
		}
		else
		{
			cemu_assert_debug(defaultCaseIndex < numCaseSegments);
			seg->SetLinkBranchTaken(caseSegments[defaultCaseIndex]);
			seg->AppendInstruction()->make_jump();
		}
	}
	for (sint32 i=0; i<numCaseSegments; i++)
	{
		IMLSegment* seg = caseSegments[i];
		if (i < numCaseSegments - 1)
		{
			seg->SetLinkBranchTaken(mergeSegment);
		}
		else
		{
			seg->SetLinkBranchTaken(mergeSegment);
		}
	}
	ppcImlGenContext.currentOutputSegment = mergeSegment;
	basicBlockInfo.appendSegment = mergeSegment;
}

IMLReg PPCRecompilerImlGen_LookupReg(ppcImlGenContext_t* ppcImlGenContext, IMLName mappedName, IMLRegFormat regFormat)
{
	auto it = ppcImlGenContext->mappedRegs.find(mappedName);
	if (it != ppcImlGenContext->mappedRegs.end())
		return it->second;
	IMLRegFormat baseFormat;
	if (regFormat == IMLRegFormat::F64)
		baseFormat = IMLRegFormat::F64;
	else if (regFormat == IMLRegFormat::I32)
		baseFormat = IMLRegFormat::I64;
	else
	{
		cemu_assert_suspicious();
	}
	IMLRegID newRegId = ppcImlGenContext->mappedRegs.size();
	IMLReg newReg(baseFormat, regFormat, 0, newRegId);
	ppcImlGenContext->mappedRegs.try_emplace(mappedName, newReg);
	return newReg;
}

IMLName PPCRecompilerImlGen_GetRegName(ppcImlGenContext_t* ppcImlGenContext, IMLReg reg)
{
	for (auto& it : ppcImlGenContext->mappedRegs)
	{
		if (it.second.GetRegID() == reg.GetRegID())
			return it.first;
	}
	cemu_assert(false);
	return 0;
}

uint32 PPCRecompilerImlGen_getAndLockFreeTemporaryFPR(ppcImlGenContext_t* ppcImlGenContext, uint32 mappedName)
{
	DEBUG_BREAK;
	return 0;
}

uint32 PPCRecompilerImlGen_findFPRRegisterByMappedName(ppcImlGenContext_t* ppcImlGenContext, uint32 mappedName)
{
	DEBUG_BREAK;
	return PPC_REC_INVALID_REGISTER;
}

IMLReg PPCRecompilerImlGen_loadRegister(ppcImlGenContext_t* ppcImlGenContext, uint32 mappedName)
{
	return PPCRecompilerImlGen_LookupReg(ppcImlGenContext, mappedName, IMLRegFormat::I32);
}

IMLReg _GetRegGPR(ppcImlGenContext_t* ppcImlGenContext, uint32 index)
{
	cemu_assert_debug(index < 32);
	return PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_R0 + index);
}

IMLReg _GetRegCR(ppcImlGenContext_t* ppcImlGenContext, uint32 index)
{
	cemu_assert_debug(index < 32);
	return PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_CR + index);
}

IMLReg _GetRegCR(ppcImlGenContext_t* ppcImlGenContext, uint8 crReg, uint8 crBit)
{
	cemu_assert_debug(crReg < 8);
	cemu_assert_debug(crBit < 4);
	return _GetRegCR(ppcImlGenContext, (crReg * 4) + crBit);
}

IMLReg _GetRegTemporary(ppcImlGenContext_t* ppcImlGenContext, uint32 index)
{
	cemu_assert_debug(index < 4);
	return PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_TEMPORARY + index);
}

IMLReg _GetRegTemporaryS8(ppcImlGenContext_t* ppcImlGenContext, uint32 index)
{
	cemu_assert_debug(index < 4);
	return PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_TEMPORARY + index);
}

bool PPCRecompiler_canInlineFunction(MPTR functionPtr, sint32* functionInstructionCount)
{
	for (sint32 i = 0; i < 6; i++)
	{
		uint32 opcode = memory_readU32(functionPtr + i * 4);
		switch ((opcode >> 26))
		{
		case 14:
		case 15:
			continue;
		case 19:
			switch (PPC_getBits(opcode, 30, 10))
			{
			case 16:
				if (opcode == 0x4E800020)
				{
					*functionInstructionCount = i;
					return true;
				}
				return false;
			}
			return false;
		case 32:
		case 33:
		case 34:
		case 35:
		case 36:
		case 37:
		case 38:
		case 39:
		case 40:
		case 41:
		case 42:
		case 43:
		case 44:
		case 45:
		case 46:
		case 47:
		case 48:
		case 49:
		case 50:
		case 51:
		case 52:
		case 53:
		case 54:
		case 55:
			continue;
		default:
			return false;
		}
	}
	return false;
}

void PPCRecompiler_generateInlinedCode(ppcImlGenContext_t* ppcImlGenContext, uint32 startAddress, sint32 instructionCount)
{
	for (sint32 i = 0; i < instructionCount; i++)
	{
		ppcImlGenContext->ppcAddressOfCurrentInstruction = startAddress + i * 4;
		ppcImlGenContext->cyclesSinceLastBranch++;
		if (PPCRecompiler_decodePPCInstruction(ppcImlGenContext))
		{
			cemu_assert_suspicious();
		}
	}
	cemu_assert_unimplemented();
}

void PPCImlGen_UpdateCR0(ppcImlGenContext_t* ppcImlGenContext, IMLReg regR)
{
	IMLReg crBitRegLT = _GetRegCR(ppcImlGenContext, 0, Espresso::CR_BIT::CR_BIT_INDEX_LT);
	IMLReg crBitRegGT = _GetRegCR(ppcImlGenContext, 0, Espresso::CR_BIT::CR_BIT_INDEX_GT);
	IMLReg crBitRegEQ = _GetRegCR(ppcImlGenContext, 0, Espresso::CR_BIT::CR_BIT_INDEX_EQ);

	ppcImlGenContext->emitInst().make_compare_s32(regR, 0, crBitRegLT, IMLCondition::SIGNED_LT);
	ppcImlGenContext->emitInst().make_compare_s32(regR, 0, crBitRegGT, IMLCondition::SIGNED_GT);
	ppcImlGenContext->emitInst().make_compare_s32(regR, 0, crBitRegEQ, IMLCondition::EQ);

}

void PPCRecompilerImlGen_TW(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	PPCIMLGen_CreateSplitSegmentAtEnd(*ppcImlGenContext, *ppcImlGenContext->currentBasicBlock);
	ppcImlGenContext->currentOutputSegment->SetEnterable(ppcImlGenContext->ppcAddressOfCurrentInstruction);
	PPCRecompilerImlGen_generateNewEmptyInstruction(ppcImlGenContext)->make_macro(PPCREC_IML_MACRO_LEAVE, ppcImlGenContext->ppcAddressOfCurrentInstruction, 0, 0, IMLREG_INVALID);
	IMLSegment* middleSeg = PPCIMLGen_CreateSplitSegmentAtEnd(*ppcImlGenContext, *ppcImlGenContext->currentBasicBlock);
	middleSeg->SetLinkBranchTaken(nullptr);
	middleSeg->SetLinkBranchNotTaken(nullptr);
}

bool PPCRecompilerImlGen_MTSPR(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	uint32 rD, spr1, spr2, spr;
	PPC_OPC_TEMPL_XO(opcode, rD, spr1, spr2);
	spr = spr1 | (spr2<<5);
	IMLReg gprReg = _GetRegGPR(ppcImlGenContext, rD);
	if (spr == SPR_CTR || spr == SPR_LR)
	{
		IMLReg sprReg = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_SPR0 + spr);
		ppcImlGenContext->emitInst().make_r_r(PPCREC_IML_OP_ASSIGN, sprReg, gprReg);
	}
	else if (spr >= SPR_UGQR0 && spr <= SPR_UGQR7)
	{
		IMLReg sprReg = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_SPR0 + spr);
		ppcImlGenContext->emitInst().make_r_r(PPCREC_IML_OP_ASSIGN, sprReg, gprReg);
		ppcImlGenContext->tracking.modifiesGQR[spr - SPR_UGQR0] = true;
	}
	else
		return false;
	return true;
}

bool PPCRecompilerImlGen_MFSPR(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	uint32 rD, spr1, spr2, spr;
	PPC_OPC_TEMPL_XO(opcode, rD, spr1, spr2);
	spr = spr1 | (spr2<<5);
	IMLReg gprReg = _GetRegGPR(ppcImlGenContext, rD);
	if (spr == SPR_LR || spr == SPR_CTR)
	{
		IMLReg sprReg = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_SPR0 + spr);
		ppcImlGenContext->emitInst().make_r_r(PPCREC_IML_OP_ASSIGN, gprReg, sprReg);
	}
	else if (spr >= SPR_UGQR0 && spr <= SPR_UGQR7)
	{
		IMLReg sprReg = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_SPR0 + spr);
		ppcImlGenContext->emitInst().make_r_r(PPCREC_IML_OP_ASSIGN, gprReg, sprReg);
	}
	else
		return false;
	return true;
}

ATTR_MS_ABI uint32 PPCRecompiler_GetTBL()
{
	return (uint32)coreinit::OSGetSystemTime();
}

ATTR_MS_ABI uint32 PPCRecompiler_GetTBU()
{
	return (uint32)(coreinit::OSGetSystemTime() >> 32);
}

ATTR_MS_ABI void PPCRecompiler_LatteBufferCache_notifyDCFlush(uint32 address, uint32 size)
{
	LatteBufferCache_notifyDCFlush(address, size);
}

bool PPCRecompilerImlGen_MFTB(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	uint32 rD, spr1, spr2, spr;
	PPC_OPC_TEMPL_XO(opcode, rD, spr1, spr2);
	spr = spr1 | (spr2<<5);

	if( spr == SPR_TBL || spr == SPR_TBU )
	{
		IMLReg resultReg = _GetRegGPR(ppcImlGenContext, rD);
		ppcImlGenContext->emitInst().make_call_imm(spr == SPR_TBL ? (uintptr_t)PPCRecompiler_GetTBL : (uintptr_t)PPCRecompiler_GetTBU, IMLREG_INVALID, IMLREG_INVALID, IMLREG_INVALID, resultReg);
		return true;
	}
	return false;
}

void PPCRecompilerImlGen_MCRF(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	uint32 crD, crS, b;
	PPC_OPC_TEMPL_X(opcode, crD, crS, b);
	cemu_assert_debug((crD&3) == 0);
	cemu_assert_debug((crS&3) == 0);
	crD >>= 2;
	crS >>= 2;
	for (sint32 i = 0; i<4; i++)
	{
		IMLReg regCrSrcBit = _GetRegCR(ppcImlGenContext, crS * 4 + i);
		IMLReg regCrDstBit = _GetRegCR(ppcImlGenContext, crD * 4 + i);
		ppcImlGenContext->emitInst().make_r_r(PPCREC_IML_OP_ASSIGN, regCrDstBit, regCrSrcBit);
	}
}

bool PPCRecompilerImlGen_MFCR(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	sint32 rD, rA, rB;
	PPC_OPC_TEMPL_X(opcode, rD, rA, rB);
	IMLReg regD = _GetRegGPR(ppcImlGenContext, rD);
	ppcImlGenContext->emitInst().make_r_s32(PPCREC_IML_OP_ASSIGN, regD, 0);
	for (sint32 i = 0; i < 32; i++)
	{
		IMLReg regCrBit = _GetRegCR(ppcImlGenContext, i);
		cemu_assert_debug(regCrBit.GetRegFormat() == IMLRegFormat::I32);
		ppcImlGenContext->emitInst().make_r_r_s32(PPCREC_IML_OP_LEFT_SHIFT, regD, regD, 1);
		ppcImlGenContext->emitInst().make_r_r_r(PPCREC_IML_OP_ADD, regD, regD, regCrBit);
	}
	return true;
}

bool PPCRecompilerImlGen_MTCRF(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	uint32 rS;
	uint32 crMask;
	PPC_OPC_TEMPL_XFX(opcode, rS, crMask);
	IMLReg regS = _GetRegGPR(ppcImlGenContext, rS);
	IMLReg regTmp = _GetRegTemporary(ppcImlGenContext, 0);
	uint32 crBitMask = ppc_MTCRFMaskToCRBitMask(crMask);
	for (sint32 f = 0; f < 32; f++)
	{
		if(((crBitMask >> f) & 1) == 0)
			continue;
		IMLReg regCrBit = _GetRegCR(ppcImlGenContext, f);
		cemu_assert_debug(regCrBit.GetRegFormat() == IMLRegFormat::I32);
		ppcImlGenContext->emitInst().make_r_r_s32(PPCREC_IML_OP_RIGHT_SHIFT_U, regTmp, regS, (31-f));
		ppcImlGenContext->emitInst().make_r_r_s32(PPCREC_IML_OP_AND, regCrBit, regTmp, 1);
	}
	return true;
}

void PPCRecompilerImlGen_CMP(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode, bool isUnsigned)
{
	uint32 cr;
	int rA, rB;
	PPC_OPC_TEMPL_X(opcode, cr, rA, rB);
	cr >>= 2;

	IMLReg gprRegisterA = _GetRegGPR(ppcImlGenContext, rA);
	IMLReg gprRegisterB = _GetRegGPR(ppcImlGenContext, rB);
	IMLReg regXerSO = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_XER_SO);

	IMLReg crBitRegLT = _GetRegCR(ppcImlGenContext, cr, Espresso::CR_BIT::CR_BIT_INDEX_LT);
	IMLReg crBitRegGT = _GetRegCR(ppcImlGenContext, cr, Espresso::CR_BIT::CR_BIT_INDEX_GT);
	IMLReg crBitRegEQ = _GetRegCR(ppcImlGenContext, cr, Espresso::CR_BIT::CR_BIT_INDEX_EQ);
	IMLReg crBitRegSO = _GetRegCR(ppcImlGenContext, cr, Espresso::CR_BIT::CR_BIT_INDEX_SO);

	ppcImlGenContext->emitInst().make_compare(gprRegisterA, gprRegisterB, crBitRegLT, isUnsigned ? IMLCondition::UNSIGNED_LT : IMLCondition::SIGNED_LT);
	ppcImlGenContext->emitInst().make_compare(gprRegisterA, gprRegisterB, crBitRegGT, isUnsigned ? IMLCondition::UNSIGNED_GT : IMLCondition::SIGNED_GT);
	ppcImlGenContext->emitInst().make_compare(gprRegisterA, gprRegisterB, crBitRegEQ, IMLCondition::EQ);
	ppcImlGenContext->emitInst().make_r_r(PPCREC_IML_OP_ASSIGN, crBitRegSO, regXerSO);
}

bool PPCRecompilerImlGen_CMPI(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode, bool isUnsigned)
{
	uint32 cr;
	int rA;
	uint32 imm;
	if (isUnsigned)
	{
		PPC_OPC_TEMPL_D_UImm(opcode, cr, rA, imm);
	}
	else
	{
		PPC_OPC_TEMPL_D_SImm(opcode, cr, rA, imm);
	}
	cr >>= 2;

	IMLReg regA = _GetRegGPR(ppcImlGenContext, rA);
	IMLReg regXerSO = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_XER_SO);

	IMLReg crBitRegLT = _GetRegCR(ppcImlGenContext, cr, Espresso::CR_BIT::CR_BIT_INDEX_LT);
	IMLReg crBitRegGT = _GetRegCR(ppcImlGenContext, cr, Espresso::CR_BIT::CR_BIT_INDEX_GT);
	IMLReg crBitRegEQ = _GetRegCR(ppcImlGenContext, cr, Espresso::CR_BIT::CR_BIT_INDEX_EQ);
	IMLReg crBitRegSO = _GetRegCR(ppcImlGenContext, cr, Espresso::CR_BIT::CR_BIT_INDEX_SO);

	ppcImlGenContext->emitInst().make_compare_s32(regA, (sint32)imm, crBitRegLT, isUnsigned ? IMLCondition::UNSIGNED_LT : IMLCondition::SIGNED_LT);
	ppcImlGenContext->emitInst().make_compare_s32(regA, (sint32)imm, crBitRegGT, isUnsigned ? IMLCondition::UNSIGNED_GT : IMLCondition::SIGNED_GT);
	ppcImlGenContext->emitInst().make_compare_s32(regA, (sint32)imm, crBitRegEQ, IMLCondition::EQ);
	ppcImlGenContext->emitInst().make_r_r(PPCREC_IML_OP_ASSIGN, crBitRegSO, regXerSO);

	return true;
}

bool PPCRecompilerImlGen_B(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	uint32 li;
	PPC_OPC_TEMPL_I(opcode, li);
	uint32 jumpAddressDest = li;
	if( (opcode&PPC_OPC_AA) == 0 )
	{
		jumpAddressDest = li + (unsigned int)ppcImlGenContext->ppcAddressOfCurrentInstruction;
	}
	if( opcode&PPC_OPC_LK )
	{
		ppcImlGenContext->emitInst().make_macro(PPCREC_IML_MACRO_BL, ppcImlGenContext->ppcAddressOfCurrentInstruction, jumpAddressDest, ppcImlGenContext->cyclesSinceLastBranch, IMLREG_INVALID);
		return true;
	}
	if (ppcImlGenContext->boundaryTracker->ContainsAddress(jumpAddressDest))
		ppcImlGenContext->emitInst().make_jump();
	else
		ppcImlGenContext->emitInst().make_macro(PPCREC_IML_MACRO_B_FAR, ppcImlGenContext->ppcAddressOfCurrentInstruction, jumpAddressDest, ppcImlGenContext->cyclesSinceLastBranch, IMLREG_INVALID);
	return true;
}

bool PPCRecompilerImlGen_BC(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	PPCIMLGen_AssertIfNotLastSegmentInstruction(*ppcImlGenContext);

	uint32 BO, BI, BD;
	PPC_OPC_TEMPL_B(opcode, BO, BI, BD);

	Espresso::BOField boField(BO);

	uint32 crRegister = BI/4;
	uint32 crBit = BI%4;
	uint32 jumpCondition = 0;
	bool conditionMustBeTrue = (BO&8)!=0;
	bool useDecrementer = (BO&4)==0;
	bool decrementerMustBeZero = (BO&2)!=0;
	bool ignoreCondition = (BO&16)!=0;

	IMLReg regCRBit;
	if (!ignoreCondition)
		regCRBit = _GetRegCR(ppcImlGenContext, crRegister, crBit);

	uint32 jumpAddressDest = BD;
	if( (opcode&PPC_OPC_AA) == 0 )
	{
		jumpAddressDest = BD + (unsigned int)ppcImlGenContext->ppcAddressOfCurrentInstruction;
	}

	if( opcode&PPC_OPC_LK )
	{
		if (useDecrementer)
			return false;
		if( ignoreCondition == false )
		{
			PPCBasicBlockInfo* currentBasicBlock = ppcImlGenContext->currentBasicBlock;
			IMLSegment* blSeg = PPCIMLGen_CreateNewSegmentAsBranchTarget(*ppcImlGenContext, *currentBasicBlock);
			ppcImlGenContext->emitInst().make_conditional_jump(regCRBit, conditionMustBeTrue);
			blSeg->AppendInstruction()->make_macro(PPCREC_IML_MACRO_BL, ppcImlGenContext->ppcAddressOfCurrentInstruction, jumpAddressDest, ppcImlGenContext->cyclesSinceLastBranch, IMLREG_INVALID);
			return true;
		}
		return false;
	}
	if( useDecrementer )
	{
		if( ignoreCondition == false )
			return false;
		IMLReg ctrRegister = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_SPR0+SPR_CTR);
		IMLReg tmpBoolReg = _GetRegTemporaryS8(ppcImlGenContext, 1);
		ppcImlGenContext->emitInst().make_r_r_s32(PPCREC_IML_OP_SUB, ctrRegister, ctrRegister, 1);
		ppcImlGenContext->emitInst().make_compare_s32(ctrRegister, 0, tmpBoolReg, decrementerMustBeZero ? IMLCondition::EQ : IMLCondition::NEQ);
		ppcImlGenContext->emitInst().make_conditional_jump(tmpBoolReg, true);
		return true;
	}
	else
	{
		if( ignoreCondition )
		{
			return false;
		}
		else
		{
			if (ppcImlGenContext->boundaryTracker->ContainsAddress(jumpAddressDest))
			{
				ppcImlGenContext->emitInst().make_conditional_jump(regCRBit, conditionMustBeTrue);
			}
			else
			{
				debug_printf("PPCRecompilerImlGen_BC(): Far jump not supported yet");
				return false;
			}
		}
	}
	return true;
}

bool PPCRecompilerImlGen_BCSPR(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode, uint32 sprReg)
{
	PPCIMLGen_AssertIfNotLastSegmentInstruction(*ppcImlGenContext);

	Espresso::BOField BO;
	uint32 BI;
	bool LK;
	Espresso::decodeOp_BCSPR(opcode, BO, BI, LK);
	uint32 crRegister = BI/4;
	uint32 crBit = BI%4;

	IMLReg regCRBit;
	if (!BO.conditionIgnore())
		regCRBit = _GetRegCR(ppcImlGenContext, crRegister, crBit);

	IMLReg branchDestReg = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_SPR0 + sprReg);
	if (LK)
	{
		if (sprReg == SPR_LR)
		{
			cemu_assert_suspicious();
			IMLReg tmpRegister = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_TEMPORARY);
			ppcImlGenContext->emitInst().make_r_r(PPCREC_IML_OP_ASSIGN, tmpRegister, branchDestReg);
			branchDestReg = tmpRegister;
		}
		IMLReg registerLR = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_SPR0 + SPR_LR);
		ppcImlGenContext->emitInst().make_r_s32(PPCREC_IML_OP_ASSIGN, registerLR, ppcImlGenContext->ppcAddressOfCurrentInstruction + 4);
	}

	if (!BO.decrementerIgnore())
	{
		cemu_assert_unimplemented();
		return false;
	}
	else if (!BO.conditionIgnore())
	{
		cemu_assert_debug(ppcImlGenContext->currentBasicBlock->hasContinuedFlow);
		cemu_assert_debug(!ppcImlGenContext->currentBasicBlock->hasBranchTarget);
		PPCBasicBlockInfo* currentBasicBlock = ppcImlGenContext->currentBasicBlock;
		IMLSegment* bctrSeg = PPCIMLGen_CreateNewSegmentAsBranchTarget(*ppcImlGenContext, *currentBasicBlock);
		ppcImlGenContext->emitInst().make_conditional_jump(regCRBit, !BO.conditionInverted());
		bctrSeg->AppendInstruction()->make_macro(PPCREC_IML_MACRO_B_TO_REG, 0, 0, 0, branchDestReg);
	}
	else
	{
		cemu_assert_debug(!ppcImlGenContext->currentBasicBlock->hasContinuedFlow);
		cemu_assert_debug(!ppcImlGenContext->currentBasicBlock->hasBranchTarget);
		ppcImlGenContext->emitInst().make_macro(PPCREC_IML_MACRO_B_TO_REG, 0, 0, 0, branchDestReg);
	}
	return true;
}

bool PPCRecompilerImlGen_ISYNC(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	return true;
}

bool PPCRecompilerImlGen_SYNC(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	return true;
}

bool PPCRecompilerImlGen_DCBFlush(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	sint32 rS, rA, rB;
	PPC_OPC_TEMPL_X(opcode, rS, rA, rB);

	IMLReg regEA = _GetRegTemporary(ppcImlGenContext, 0);
	IMLReg regB = _GetRegGPR(ppcImlGenContext, rB);
	if (rA)
	{
		IMLReg regA = _GetRegGPR(ppcImlGenContext, rA);
		ppcImlGenContext->emitInst().make_r_r_r(PPCREC_IML_OP_ADD, regEA, regA, regB);
	}
	else
	{
		ppcImlGenContext->emitInst().make_r_r(PPCREC_IML_OP_ASSIGN, regEA, regB);
	}

	IMLReg regSize = _GetRegTemporary(ppcImlGenContext, 1);
	ppcImlGenContext->emitInst().make_r_s32(PPCREC_IML_OP_ASSIGN, regSize, 32);
	ppcImlGenContext->emitInst().make_call_imm((uintptr_t)PPCRecompiler_LatteBufferCache_notifyDCFlush, regEA, regSize, IMLREG_INVALID, IMLREG_INVALID);
	return true;
}

bool PPCRecompilerImlGen_ADD(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	sint32 rD, rA, rB;
	PPC_OPC_TEMPL_XO(opcode, rD, rA, rB);
	IMLReg regA = _GetRegGPR(ppcImlGenContext, rA);
	IMLReg regB = _GetRegGPR(ppcImlGenContext, rB);
	IMLReg regD = _GetRegGPR(ppcImlGenContext, rD);
	ppcImlGenContext->emitInst().make_r_r_r(PPCREC_IML_OP_ADD, regD, regA, regB);
	if (opcode & PPC_OPC_RC)
		PPCImlGen_UpdateCR0(ppcImlGenContext, regD);
	return true;
}

bool PPCRecompilerImlGen_ADDI(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	sint32 rD, rA;
	uint32 imm;
	PPC_OPC_TEMPL_D_SImm(opcode, rD, rA, imm);
	IMLReg regD = _GetRegGPR(ppcImlGenContext, rD);
	if (rA != 0)
	{
		IMLReg regA = _GetRegGPR(ppcImlGenContext, rA);
		ppcImlGenContext->emitInst().make_r_r_s32(PPCREC_IML_OP_ADD, regD, regA, imm);
	}
	else
	{
		ppcImlGenContext->emitInst().make_r_s32(PPCREC_IML_OP_ASSIGN, regD, imm);
	}
	return true;
}

bool PPCRecompilerImlGen_ADDIS(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	int rD, rA;
	uint32 imm;
	PPC_OPC_TEMPL_D_Shift16(opcode, rD, rA, imm);
	IMLReg regD = _GetRegGPR(ppcImlGenContext, rD);
	if (rA != 0)
	{
		IMLReg regA = _GetRegGPR(ppcImlGenContext, rA);
		ppcImlGenContext->emitInst().make_r_r_s32(PPCREC_IML_OP_ADD, regD, regA, (sint32)imm);
	}
	else
	{
		ppcImlGenContext->emitInst().make_r_s32(PPCREC_IML_OP_ASSIGN, regD, (sint32)imm);
	}
	return true;
}

bool PPCRecompilerImlGen_ADDC(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	sint32 rD, rA, rB;
	PPC_OPC_TEMPL_XO(opcode, rD, rA, rB);
	IMLReg regRA = _GetRegGPR(ppcImlGenContext, rA);
	IMLReg regRB = _GetRegGPR(ppcImlGenContext, rB);
	IMLReg regRD = _GetRegGPR(ppcImlGenContext, rD);
	IMLReg regCa = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_XER_CA);
	ppcImlGenContext->emitInst().make_r_r_r_carry(PPCREC_IML_OP_ADD, regRD, regRA, regRB, regCa);
	if (opcode & PPC_OPC_RC)
		PPCImlGen_UpdateCR0(ppcImlGenContext, regRD);
	return true;
}

bool PPCRecompilerImlGen_ADDIC_(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode, bool updateCR0)
{
	sint32 rD, rA;
	uint32 imm;
	PPC_OPC_TEMPL_D_SImm(opcode, rD, rA, imm);
	IMLReg regA = _GetRegGPR(ppcImlGenContext, rA);
	IMLReg regD = _GetRegGPR(ppcImlGenContext, rD);
	IMLReg regCa = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_XER_CA);
	ppcImlGenContext->emitInst().make_r_r_s32_carry(PPCREC_IML_OP_ADD, regD, regA, (sint32)imm, regCa);
	if(updateCR0)
		PPCImlGen_UpdateCR0(ppcImlGenContext, regD);
	return true;
}

bool PPCRecompilerImlGen_ADDE(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	sint32 rD, rA, rB;
	PPC_OPC_TEMPL_XO(opcode, rD, rA, rB);
	IMLReg regRA = _GetRegGPR(ppcImlGenContext, rA);
	IMLReg regRB = _GetRegGPR(ppcImlGenContext, rB);
	IMLReg regRD = _GetRegGPR(ppcImlGenContext, rD);
	IMLReg regCa = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_XER_CA);
	ppcImlGenContext->emitInst().make_r_r_r_carry(PPCREC_IML_OP_ADD_WITH_CARRY, regRD, regRA, regRB, regCa);
	if (opcode & PPC_OPC_RC)
		PPCImlGen_UpdateCR0(ppcImlGenContext, regRD);
	return true;
}

bool PPCRecompilerImlGen_ADDZE(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	sint32 rD, rA, rB;
	PPC_OPC_TEMPL_XO(opcode, rD, rA, rB);
	IMLReg regRA = _GetRegGPR(ppcImlGenContext, rA);
	IMLReg regRD = _GetRegGPR(ppcImlGenContext, rD);
	IMLReg regCa = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_XER_CA);
	ppcImlGenContext->emitInst().make_r_r_s32_carry(PPCREC_IML_OP_ADD_WITH_CARRY, regRD, regRA, 0, regCa);
	if (opcode & PPC_OPC_RC)
		PPCImlGen_UpdateCR0(ppcImlGenContext, regRD);
	return true;
}

bool PPCRecompilerImlGen_ADDME(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	sint32 rD, rA, rB;
	PPC_OPC_TEMPL_XO(opcode, rD, rA, rB);
	IMLReg regRA = _GetRegGPR(ppcImlGenContext, rA);
	IMLReg regRD = _GetRegGPR(ppcImlGenContext, rD);
	IMLReg regCa = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_XER_CA);
	ppcImlGenContext->emitInst().make_r_r_s32_carry(PPCREC_IML_OP_ADD_WITH_CARRY, regRD, regRA, -1, regCa);
	if (opcode & PPC_OPC_RC)
		PPCImlGen_UpdateCR0(ppcImlGenContext, regRD);
	return true;
}

bool PPCRecompilerImlGen_SUBF(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	sint32 rD, rA, rB;
	PPC_OPC_TEMPL_XO(opcode, rD, rA, rB);
	IMLReg regA = _GetRegGPR(ppcImlGenContext, rA);
	IMLReg regB = _GetRegGPR(ppcImlGenContext, rB);
	IMLReg regD = _GetRegGPR(ppcImlGenContext, rD);
	ppcImlGenContext->emitInst().make_r_r_r(PPCREC_IML_OP_SUB, regD, regB, regA);
	if ((opcode & PPC_OPC_RC))
		PPCImlGen_UpdateCR0(ppcImlGenContext, regD);
	return true;
}

bool PPCRecompilerImlGen_SUBFE(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	sint32 rD, rA, rB;
	PPC_OPC_TEMPL_XO(opcode, rD, rA, rB);
	IMLReg regA = _GetRegGPR(ppcImlGenContext, rA);
	IMLReg regB = _GetRegGPR(ppcImlGenContext, rB);
	IMLReg regD = _GetRegGPR(ppcImlGenContext, rD);
	IMLReg regTmp = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_TEMPORARY + 0);
	IMLReg regCa = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_XER_CA);
	ppcImlGenContext->emitInst().make_r_r(PPCREC_IML_OP_NOT, regTmp, regA);
	ppcImlGenContext->emitInst().make_r_r_r_carry(PPCREC_IML_OP_ADD_WITH_CARRY, regD, regTmp, regB, regCa);
	if (opcode & PPC_OPC_RC)
		PPCImlGen_UpdateCR0(ppcImlGenContext, regD);
	return true;
}

bool PPCRecompilerImlGen_SUBFZE(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	sint32 rD, rA, rB;
	PPC_OPC_TEMPL_XO(opcode, rD, rA, rB);
	IMLReg regA = _GetRegGPR(ppcImlGenContext, rA);
	IMLReg regD = _GetRegGPR(ppcImlGenContext, rD);
	IMLReg regTmp = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_TEMPORARY + 0);
	IMLReg regCa = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_XER_CA);
	ppcImlGenContext->emitInst().make_r_r(PPCREC_IML_OP_NOT, regTmp, regA);
	ppcImlGenContext->emitInst().make_r_r_s32_carry(PPCREC_IML_OP_ADD_WITH_CARRY, regD, regTmp, 0, regCa);
	if (opcode & PPC_OPC_RC)
		PPCImlGen_UpdateCR0(ppcImlGenContext, regD);
	return true;
}

bool PPCRecompilerImlGen_SUBFC(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	sint32 rD, rA, rB;
	PPC_OPC_TEMPL_XO(opcode, rD, rA, rB);
	IMLReg regA = _GetRegGPR(ppcImlGenContext, rA);
	IMLReg regB = _GetRegGPR(ppcImlGenContext, rB);
	IMLReg regD = _GetRegGPR(ppcImlGenContext, rD);
	IMLReg regTmp = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_TEMPORARY + 0);
	IMLReg regCa = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_XER_CA);
	ppcImlGenContext->emitInst().make_r_r(PPCREC_IML_OP_NOT, regTmp, regA);
	ppcImlGenContext->emitInst().make_r_s32(PPCREC_IML_OP_ASSIGN, regCa, 1);
	ppcImlGenContext->emitInst().make_r_r_r_carry(PPCREC_IML_OP_ADD_WITH_CARRY, regD, regTmp, regB, regCa);
	if ((opcode & PPC_OPC_RC))
		PPCImlGen_UpdateCR0(ppcImlGenContext, regD);
	return true;
}

bool PPCRecompilerImlGen_SUBFIC(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	sint32 rD, rA;
	uint32 imm;
	PPC_OPC_TEMPL_D_SImm(opcode, rD, rA, imm);
	IMLReg regA = _GetRegGPR(ppcImlGenContext, rA);
	IMLReg regD = _GetRegGPR(ppcImlGenContext, rD);
	IMLReg regCa = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_XER_CA);
	IMLReg regTmp = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_TEMPORARY + 0);
	ppcImlGenContext->emitInst().make_r_r(PPCREC_IML_OP_NOT, regTmp, regA);
	ppcImlGenContext->emitInst().make_r_s32(PPCREC_IML_OP_ASSIGN, regCa, 1);
	ppcImlGenContext->emitInst().make_r_r_s32_carry(PPCREC_IML_OP_ADD_WITH_CARRY, regD, regTmp, (sint32)imm, regCa);
	return true;
}

bool PPCRecompilerImlGen_MULLI(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	int rD, rA;
	uint32 imm;
	PPC_OPC_TEMPL_D_SImm(opcode, rD, rA, imm);
	IMLReg regD = _GetRegGPR(ppcImlGenContext, rD);
	IMLReg regA = _GetRegGPR(ppcImlGenContext, rA);
	ppcImlGenContext->emitInst().make_r_r_s32(PPCREC_IML_OP_MULTIPLY_SIGNED, regD, regA, (sint32)imm);
	return true;
}

bool PPCRecompilerImlGen_MULLW(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	sint32 rD, rA, rB;
	PPC_OPC_TEMPL_XO(opcode, rD, rA, rB);
	IMLReg regD = _GetRegGPR(ppcImlGenContext, rD);
	IMLReg regA = _GetRegGPR(ppcImlGenContext, rA);
	IMLReg regB = _GetRegGPR(ppcImlGenContext, rB);
	if (opcode & PPC_OPC_OE)
	{
		return false;
	}
	ppcImlGenContext->emitInst().make_r_r_r(PPCREC_IML_OP_MULTIPLY_SIGNED, regD, regA, regB);
	if (opcode & PPC_OPC_RC)
		PPCImlGen_UpdateCR0(ppcImlGenContext, regD);
	return true;
}

bool PPCRecompilerImlGen_MULHW(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	sint32 rD, rA, rB;
	PPC_OPC_TEMPL_XO(opcode, rD, rA, rB);
	IMLReg regD = _GetRegGPR(ppcImlGenContext, rD);
	IMLReg regA = _GetRegGPR(ppcImlGenContext, rA);
	IMLReg regB = _GetRegGPR(ppcImlGenContext, rB);
	ppcImlGenContext->emitInst().make_r_r_r(PPCREC_IML_OP_MULTIPLY_HIGH_SIGNED, regD, regA, regB);
	if (opcode & PPC_OPC_RC)
		PPCImlGen_UpdateCR0(ppcImlGenContext, regD);
	return true;
}

bool PPCRecompilerImlGen_MULHWU(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	sint32 rD, rA, rB;
	PPC_OPC_TEMPL_XO(opcode, rD, rA, rB);
	IMLReg regD = _GetRegGPR(ppcImlGenContext, rD);
	IMLReg regA = _GetRegGPR(ppcImlGenContext, rA);
	IMLReg regB = _GetRegGPR(ppcImlGenContext, rB);
	ppcImlGenContext->emitInst().make_r_r_r(PPCREC_IML_OP_MULTIPLY_HIGH_UNSIGNED, regD, regA, regB);
	if (opcode & PPC_OPC_RC)
		PPCImlGen_UpdateCR0(ppcImlGenContext, regD);
	return true;
}

bool PPCRecompilerImlGen_DIVW(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	sint32 rD, rA, rB;
	PPC_OPC_TEMPL_XO(opcode, rD, rA, rB);
	IMLReg regR = _GetRegGPR(ppcImlGenContext, rD);
	IMLReg regA = _GetRegGPR(ppcImlGenContext, rA);
	IMLReg regB = _GetRegGPR(ppcImlGenContext, rB);
	ppcImlGenContext->emitInst().make_r_r_r(PPCREC_IML_OP_DIVIDE_SIGNED, regR, regA, regB);
	if (opcode & PPC_OPC_RC)
		PPCImlGen_UpdateCR0(ppcImlGenContext, regR);
	return true;
}

bool PPCRecompilerImlGen_DIVWU(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	sint32 rD, rA, rB;
	PPC_OPC_TEMPL_XO(opcode, rD, rA, rB);
	IMLReg regD = _GetRegGPR(ppcImlGenContext, rD);
	IMLReg regA = _GetRegGPR(ppcImlGenContext, rA);
	IMLReg regB = _GetRegGPR(ppcImlGenContext, rB);
	ppcImlGenContext->emitInst().make_r_r_r(PPCREC_IML_OP_DIVIDE_UNSIGNED, regD, regA, regB);
	if (opcode & PPC_OPC_RC)
		PPCImlGen_UpdateCR0(ppcImlGenContext, regD);
	return true;
}

bool PPCRecompilerImlGen_RLWINM(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	sint32 rS, rA, SH, MB, ME;
	PPC_OPC_TEMPL_M(opcode, rS, rA, SH, MB, ME);
	uint32 mask = ppc_mask(MB, ME);

	IMLReg regS = _GetRegGPR(ppcImlGenContext, rS);
	IMLReg regA = _GetRegGPR(ppcImlGenContext, rA);
	if( ME == (31-SH) && MB == 0 )
	{
		ppcImlGenContext->emitInst().make_r_r_s32(PPCREC_IML_OP_LEFT_SHIFT, regA, regS, SH);
	}
	else if( SH == (32-MB) && ME == 31 )
	{
		ppcImlGenContext->emitInst().make_r_r_s32(PPCREC_IML_OP_RIGHT_SHIFT_U, regA, regS, MB);
	}
	else
	{
		if (rA != rS)
			ppcImlGenContext->emitInst().make_r_r(PPCREC_IML_OP_ASSIGN, regA, regS);
		if (SH != 0)
			ppcImlGenContext->emitInst().make_r_s32(PPCREC_IML_OP_LEFT_ROTATE, regA, SH);
		if (mask != 0xFFFFFFFF)
			ppcImlGenContext->emitInst().make_r_r_s32(PPCREC_IML_OP_AND, regA, regA, (sint32)mask);
	}
	if (opcode & PPC_OPC_RC)
		PPCImlGen_UpdateCR0(ppcImlGenContext, regA);
	return true;
}

bool PPCRecompilerImlGen_RLWIMI(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	sint32 rS, rA, SH, MB, ME;
	PPC_OPC_TEMPL_M(opcode, rS, rA, SH, MB, ME);
	IMLReg regS = _GetRegGPR(ppcImlGenContext, rS);
	IMLReg regR = _GetRegGPR(ppcImlGenContext, rA);
	IMLReg regTmp = _GetRegTemporary(ppcImlGenContext, 0);
	uint32 mask = ppc_mask(MB, ME);
	ppcImlGenContext->emitInst().make_r_r(PPCREC_IML_OP_ASSIGN, regTmp, regS);
	if (SH)
		ppcImlGenContext->emitInst().make_r_s32(PPCREC_IML_OP_LEFT_ROTATE, regTmp, SH);
	if (mask != 0)
		ppcImlGenContext->emitInst().make_r_r_s32(PPCREC_IML_OP_AND, regR, regR, (sint32)~mask);
	if (mask != 0xFFFFFFFF)
		ppcImlGenContext->emitInst().make_r_r_s32(PPCREC_IML_OP_AND, regTmp, regTmp, (sint32)mask);
	ppcImlGenContext->emitInst().make_r_r_r(PPCREC_IML_OP_OR, regR, regR, regTmp);
	if (opcode & PPC_OPC_RC)
		PPCImlGen_UpdateCR0(ppcImlGenContext, regR);
	return true;
}

bool PPCRecompilerImlGen_RLWNM(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	sint32 rS, rA, rB, MB, ME;
	PPC_OPC_TEMPL_M(opcode, rS, rA, rB, MB, ME);
	uint32 mask = ppc_mask(MB, ME);
	IMLReg regS = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_R0+rS);
	IMLReg regB = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_R0+rB);
	IMLReg regA = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_R0+rA);
	ppcImlGenContext->emitInst().make_r_r_r(PPCREC_IML_OP_LEFT_ROTATE, regA, regS, regB);
	if( mask != 0xFFFFFFFF )
		ppcImlGenContext->emitInst().make_r_r_s32(PPCREC_IML_OP_AND, regA, regA, (sint32)mask);
	if (opcode & PPC_OPC_RC)
		PPCImlGen_UpdateCR0(ppcImlGenContext, regA);
	return true;
}

bool PPCRecompilerImlGen_SRAW(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	sint32 rS, rA, rB;
	PPC_OPC_TEMPL_X(opcode, rS, rA, rB);
	IMLReg regS = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_R0+rS);
	IMLReg regB = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_R0+rB);
	IMLReg regA = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_R0+rA);
	IMLReg regCarry = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_XER_CA);

	IMLReg regTmpShiftAmount = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_TEMPORARY + 0);
	IMLReg regTmpCondBool = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_TEMPORARY + 1);
	IMLReg regTmp1 = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_TEMPORARY + 2);
	IMLReg regTmp2 = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_TEMPORARY + 3);

	ppcImlGenContext->emitInst().make_r_r_s32(PPCREC_IML_OP_AND, regTmpShiftAmount, regB, 0x3F);
	ppcImlGenContext->emitInst().make_compare_s32(regTmpShiftAmount, 31, regTmpCondBool, IMLCondition::UNSIGNED_GT);
	ppcImlGenContext->emitInst().make_conditional_jump(regTmpCondBool, true);

	PPCIMLGen_CreateSegmentBranchedPath(*ppcImlGenContext, *ppcImlGenContext->currentBasicBlock,
		[&](ppcImlGenContext_t& genCtx)
		{
			genCtx.emitInst().make_r_r_s32(PPCREC_IML_OP_RIGHT_SHIFT_S, regA, regS, 31);
			genCtx.emitInst().make_compare_s32(regA, 0, regCarry, IMLCondition::NEQ);
		},
		[&](ppcImlGenContext_t& genCtx)
		{
			genCtx.emitInst().make_r_r_s32(PPCREC_IML_OP_RIGHT_SHIFT_S, regTmp1, regS, 31);
			genCtx.emitInst().make_r_s32(PPCREC_IML_OP_ASSIGN, regTmp2, 1);
			genCtx.emitInst().make_r_r_r(PPCREC_IML_OP_LEFT_SHIFT, regTmp2, regTmp2, regTmpShiftAmount);
			genCtx.emitInst().make_r_r_s32(PPCREC_IML_OP_SUB, regTmp2, regTmp2, 1);
			genCtx.emitInst().make_r_r_r(PPCREC_IML_OP_AND, regTmp1, regTmp1, regTmp2);
			genCtx.emitInst().make_r_r_r(PPCREC_IML_OP_AND, regTmp1, regTmp1, regS);
			genCtx.emitInst().make_compare_s32(regTmp1, 0, regCarry, IMLCondition::NEQ);
			genCtx.emitInst().make_r_r_r(PPCREC_IML_OP_RIGHT_SHIFT_S, regA, regS, regTmpShiftAmount);
		}
	);
	if (opcode & PPC_OPC_RC)
		PPCImlGen_UpdateCR0(ppcImlGenContext, regA);
	return true;
}

bool PPCRecompilerImlGen_SRAWI(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	int rS, rA;
	uint32 SH;
	PPC_OPC_TEMPL_X(opcode, rS, rA, SH);
	cemu_assert_debug(SH < 32);
	if (SH == 0)
		return false;
	IMLReg regS = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_R0 + rS);
	IMLReg regA = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_R0 + rA);
	IMLReg regCarry = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_XER_CA);
	IMLReg regTmp = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_TEMPORARY + 0);
	ppcImlGenContext->emitInst().make_r_r_s32(PPCREC_IML_OP_RIGHT_SHIFT_S, regTmp, regS, 31);
	ppcImlGenContext->emitInst().make_r_r_r(PPCREC_IML_OP_AND, regTmp, regTmp, regS);
	ppcImlGenContext->emitInst().make_r_r_s32(PPCREC_IML_OP_AND, regTmp, regTmp, ((1 << SH) - 1));
	ppcImlGenContext->emitInst().make_compare_s32(regTmp, 0, regCarry, IMLCondition::NEQ);
	ppcImlGenContext->emitInst().make_r_r_s32(PPCREC_IML_OP_RIGHT_SHIFT_S, regA, regS, (sint32)SH);
	if (opcode & PPC_OPC_RC)
		PPCImlGen_UpdateCR0(ppcImlGenContext, regA);
	return true;
}

bool PPCRecompilerImlGen_SLW(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	int rS, rA, rB;
	PPC_OPC_TEMPL_X(opcode, rS, rA, rB);

	IMLReg regS = _GetRegGPR(ppcImlGenContext, rS);
	IMLReg regB = _GetRegGPR(ppcImlGenContext, rB);
	IMLReg regA = _GetRegGPR(ppcImlGenContext, rA);
	ppcImlGenContext->emitInst().make_r_r_r(PPCREC_IML_OP_SLW, regA, regS, regB);
	if ((opcode & PPC_OPC_RC))
		PPCImlGen_UpdateCR0(ppcImlGenContext, regA);
	return true;
}

bool PPCRecompilerImlGen_SRW(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	int rS, rA, rB;
	PPC_OPC_TEMPL_X(opcode, rS, rA, rB);
	IMLReg regS = _GetRegGPR(ppcImlGenContext, rS);
	IMLReg regB = _GetRegGPR(ppcImlGenContext, rB);
	IMLReg regA = _GetRegGPR(ppcImlGenContext, rA);
	ppcImlGenContext->emitInst().make_r_r_r(PPCREC_IML_OP_SRW, regA, regS, regB);
	if (opcode & PPC_OPC_RC)
		PPCImlGen_UpdateCR0(ppcImlGenContext, regA);
	return true;
}

bool PPCRecompilerImlGen_EXTSH(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	int rS, rA, rB;
	PPC_OPC_TEMPL_X(opcode, rS, rA, rB);
	IMLReg regS = _GetRegGPR(ppcImlGenContext, rS);
	IMLReg regA = _GetRegGPR(ppcImlGenContext, rA);
	ppcImlGenContext->emitInst().make_r_r(PPCREC_IML_OP_ASSIGN_S16_TO_S32, regA, regS);
	if (opcode & PPC_OPC_RC)
		PPCImlGen_UpdateCR0(ppcImlGenContext, regA);
	return true;
}

bool PPCRecompilerImlGen_EXTSB(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	sint32 rS, rA, rB;
	PPC_OPC_TEMPL_X(opcode, rS, rA, rB);
	IMLReg regS = _GetRegGPR(ppcImlGenContext, rS);
	IMLReg regA = _GetRegGPR(ppcImlGenContext, rA);
	ppcImlGenContext->emitInst().make_r_r(PPCREC_IML_OP_ASSIGN_S8_TO_S32, regA, regS);
	if ((opcode & PPC_OPC_RC))
		PPCImlGen_UpdateCR0(ppcImlGenContext, regA);
	return true;
}

bool PPCRecompilerImlGen_CNTLZW(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	sint32 rS, rA, rB;
	PPC_OPC_TEMPL_X(opcode, rS, rA, rB);
	IMLReg regS = _GetRegGPR(ppcImlGenContext, rS);
	IMLReg regA = _GetRegGPR(ppcImlGenContext, rA);
	ppcImlGenContext->emitInst().make_r_r(PPCREC_IML_OP_CNTLZW, regA, regS);
	if ((opcode & PPC_OPC_RC))
		PPCImlGen_UpdateCR0(ppcImlGenContext, regA);
	return true;
}

bool PPCRecompilerImlGen_NEG(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	sint32 rD, rA, rB;
	PPC_OPC_TEMPL_XO(opcode, rD, rA, rB);
	IMLReg regA = _GetRegGPR(ppcImlGenContext, rA);
	IMLReg regD = _GetRegGPR(ppcImlGenContext, rD);
	ppcImlGenContext->emitInst().make_r_r(PPCREC_IML_OP_NEG, regD, regA);
	if (opcode & PPC_OPC_RC)
		PPCImlGen_UpdateCR0(ppcImlGenContext, regD);
	return true;
}

bool PPCRecompilerImlGen_LOAD(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode, uint32 bitWidth, bool signExtend, bool isBigEndian, bool updateAddrReg)
{
	int rA, rD;
	uint32 imm;
	PPC_OPC_TEMPL_D_SImm(opcode, rD, rA, imm);
	IMLReg regMemAddr;
	if (rA == 0)
	{
		if (updateAddrReg)
			return false;
		regMemAddr = _GetRegTemporary(ppcImlGenContext, 0);
		ppcImlGenContext->emitInst().make_r_s32(PPCREC_IML_OP_ASSIGN, regMemAddr, 0);
	}
	else
	{
		if (updateAddrReg && rA == rD)
			return false;
		regMemAddr = _GetRegGPR(ppcImlGenContext, rA);
	}
	if (updateAddrReg)
	{
		ppcImlGenContext->emitInst().make_r_r_s32(PPCREC_IML_OP_ADD, regMemAddr, regMemAddr, (sint32)imm);
		imm = 0;
	}
	IMLReg regDst = _GetRegGPR(ppcImlGenContext, rD);
	ppcImlGenContext->emitInst().make_r_memory(regDst, regMemAddr, (sint32)imm, bitWidth, signExtend, isBigEndian);
	return true;
}

void PPCRecompilerImlGen_LOAD_INDEXED(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode, uint32 bitWidth, bool signExtend, bool isBigEndian, bool updateAddrReg)
{
	sint32 rA, rD, rB;
	PPC_OPC_TEMPL_X(opcode, rD, rA, rB);
	updateAddrReg = updateAddrReg && (rA != 0);
	IMLReg regA = rA != 0 ? _GetRegGPR(ppcImlGenContext, rA) : IMLREG_INVALID;
	IMLReg regB = _GetRegGPR(ppcImlGenContext, rB);
	IMLReg regDst = _GetRegGPR(ppcImlGenContext, rD);
	if (updateAddrReg)
	{
		ppcImlGenContext->emitInst().make_r_r_r(PPCREC_IML_OP_ADD, regA, regA, regB);
		regB = regA;
		regA = IMLREG_INVALID;
	}
	if(regA.IsValid())
		PPCRecompilerImlGen_generateNewInstruction_r_memory_indexed(ppcImlGenContext, regDst, regA, regB, bitWidth, signExtend, isBigEndian);
	else
		ppcImlGenContext->emitInst().make_r_memory(regDst, regB, 0, bitWidth, signExtend, isBigEndian);
}

bool PPCRecompilerImlGen_STORE(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode, uint32 bitWidth, bool isBigEndian, bool updateAddrReg)
{
	int rA, rD;
	uint32 imm;
	PPC_OPC_TEMPL_D_SImm(opcode, rD, rA, imm);
	IMLReg regA;
	if (rA != 0)
	{
		regA = _GetRegGPR(ppcImlGenContext, rA);
	}
	else
	{
		if (updateAddrReg)
			return false;
		regA = _GetRegTemporary(ppcImlGenContext, 0);
		ppcImlGenContext->emitInst().make_r_s32(PPCREC_IML_OP_ASSIGN, regA, 0);
	}
	IMLReg regD = _GetRegGPR(ppcImlGenContext, rD);
	if (updateAddrReg)
	{
		if (rD == rA)
		{
			regD = _GetRegTemporary(ppcImlGenContext, 0);
			ppcImlGenContext->emitInst().make_r_r(PPCREC_IML_OP_ASSIGN, regD, regA);
		}
		ppcImlGenContext->emitInst().make_r_r_s32(PPCREC_IML_OP_ADD, regA, regA, (sint32)imm);
		imm = 0;
	}
	ppcImlGenContext->emitInst().make_memory_r(regD, regA, (sint32)imm, bitWidth, isBigEndian);
	return true;
}

bool PPCRecompilerImlGen_STORE_INDEXED(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode, uint32 bitWidth, bool isBigEndian, bool updateAddrReg)
{
	sint32 rA, rS, rB;
	PPC_OPC_TEMPL_X(opcode, rS, rA, rB);
	IMLReg regA = rA != 0 ? _GetRegGPR(ppcImlGenContext, rA) : IMLREG_INVALID;
	IMLReg regB = _GetRegGPR(ppcImlGenContext, rB);
	IMLReg regSrc = _GetRegGPR(ppcImlGenContext, rS);
	if (updateAddrReg)
	{
		if(rA == 0)
			return false;
		if (regSrc == regA)
		{
			regSrc = _GetRegTemporary(ppcImlGenContext, 0);
			ppcImlGenContext->emitInst().make_r_r(PPCREC_IML_OP_ASSIGN, regSrc, regA);
		}
		ppcImlGenContext->emitInst().make_r_r_r(PPCREC_IML_OP_ADD, regA, regA, regB);
		regB = regA;
		regA = IMLREG_INVALID;
	}
	if (regA.IsInvalid())
		ppcImlGenContext->emitInst().make_memory_r(regSrc, regB, 0, bitWidth, isBigEndian);
	else
		PPCRecompilerImlGen_generateNewInstruction_memory_r_indexed(ppcImlGenContext, regSrc, regA, regB, bitWidth, false, isBigEndian);
	return true;
}

void PPCRecompilerImlGen_LMW(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	sint32 rD, rA;
	uint32 imm;
	PPC_OPC_TEMPL_D_SImm(opcode, rD, rA, imm);
	cemu_assert_debug(rA != 0);
	sint32 index = 0;
	while (rD <= 31)
	{
		IMLReg regA = _GetRegGPR(ppcImlGenContext, rA);
		IMLReg regD = _GetRegGPR(ppcImlGenContext, rD);
		ppcImlGenContext->emitInst().make_r_memory(regD, regA, (sint32)imm + index * 4, 32, false, true);
		rD++;
		index++;
	}
}

void PPCRecompilerImlGen_STMW(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	sint32 rS, rA;
	uint32 imm;
	PPC_OPC_TEMPL_D_SImm(opcode, rS, rA, imm);
	cemu_assert_debug(rA != 0);
	sint32 index = 0;
	while( rS <= 31 )
	{
		IMLReg regA = _GetRegGPR(ppcImlGenContext, rA);
		IMLReg regS = _GetRegGPR(ppcImlGenContext, rS);
		ppcImlGenContext->emitInst().make_memory_r(regS, regA, (sint32)imm + index * 4, 32, true);
		rS++;
		index++;
	}
}

bool PPCRecompilerImlGen_LSWI(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	int rA, rD, nb;
	PPC_OPC_TEMPL_X(opcode, rD, rA, nb);
	if( nb == 0 )
		nb = 32;

	if (rA == 0)
	{
		cemu_assert_unimplemented();
		return false;
	}

	IMLReg memReg = _GetRegGPR(ppcImlGenContext, rA);
	IMLReg regTmp = _GetRegTemporary(ppcImlGenContext, 0);
	uint32 memOffset = 0;
	while (nb > 0)
	{
		if (rD == rA)
			return false;
		cemu_assert(rD < 32);
		IMLReg regDst = _GetRegGPR(ppcImlGenContext, rD);
		for (sint32 b = 0; b < 4; b++)
		{
			ppcImlGenContext->emitInst().make_r_memory(regTmp, memReg, memOffset + b, 8, false, false);
			sint32 shiftAmount = (3 - b) * 8;
			if(shiftAmount)
				ppcImlGenContext->emitInst().make_r_r_s32(PPCREC_IML_OP_LEFT_SHIFT, regTmp, regTmp, shiftAmount);
			if(b == 0)
				ppcImlGenContext->emitInst().make_r_r(PPCREC_IML_OP_ASSIGN, regDst, regTmp);
			else
				ppcImlGenContext->emitInst().make_r_r_r(PPCREC_IML_OP_OR, regDst, regDst, regTmp);
			nb--;
			if (nb == 0)
				break;
		}
		memOffset += 4;
		rD++;
	}
	return true;
}

bool PPCRecompilerImlGen_STSWI(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	int rA, rS, nb;
	PPC_OPC_TEMPL_X(opcode, rS, rA, nb);
	if( nb == 0 )
		nb = 32;

	IMLReg regMem = _GetRegGPR(ppcImlGenContext, rA);
	IMLReg regTmp = _GetRegTemporary(ppcImlGenContext, 0);
	uint32 memOffset = 0;
	while (nb > 0)
	{
		if (rS == rA)
			return false;
		cemu_assert(rS < 32);
		IMLReg regSrc = _GetRegGPR(ppcImlGenContext, rS);
		for (sint32 b = 0; b < 4; b++)
		{
			ppcImlGenContext->emitInst().make_r_r(PPCREC_IML_OP_ASSIGN, regTmp, regSrc);
			sint32 shiftAmount = (3 - b) * 8;
			if (shiftAmount)
				ppcImlGenContext->emitInst().make_r_r_s32(PPCREC_IML_OP_RIGHT_SHIFT_U, regTmp, regTmp, shiftAmount);
			ppcImlGenContext->emitInst().make_memory_r(regTmp, regMem, memOffset + b, 8, false);
			nb--;
			if (nb == 0)
				break;
		}
		memOffset += 4;
		rS++;
	}
	return true;
}

bool PPCRecompilerImlGen_LWARX(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	sint32 rA, rD, rB;
	PPC_OPC_TEMPL_X(opcode, rD, rA, rB);

	IMLReg regA = rA != 0 ? PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_R0 + rA) : IMLREG_INVALID;
	IMLReg regB = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_R0 + rB);
	IMLReg regD = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_R0 + rD);
	IMLReg regMemResEA = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_CPU_MEMRES_EA);
	IMLReg regMemResVal = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_CPU_MEMRES_VAL);
	if (regA.IsValid())
		ppcImlGenContext->emitInst().make_r_r_r(PPCREC_IML_OP_ADD, regMemResEA, regA, regB);
	else
		ppcImlGenContext->emitInst().make_r_r(PPCREC_IML_OP_ASSIGN, regMemResEA, regB);
	ppcImlGenContext->emitInst().make_r_memory(regD, regMemResEA, 0, 32, false, true);
	ppcImlGenContext->emitInst().make_r_r(PPCREC_IML_OP_ASSIGN, regMemResVal, regD);
	return true;
}

bool PPCRecompilerImlGen_STWCX(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	sint32 rA, rS, rB;
	PPC_OPC_TEMPL_X(opcode, rS, rA, rB);
	IMLReg regA = rA != 0 ? PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_R0 + rA) : IMLREG_INVALID;
	IMLReg regB = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_R0 + rB);
	IMLReg regData = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_R0 + rS);
	IMLReg regTmpDataBE = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_TEMPORARY + 2);
	IMLReg regTmpCompareBE = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_TEMPORARY + 3);
	IMLReg regCalcEA = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_TEMPORARY);
	if (regA.IsValid())
		ppcImlGenContext->emitInst().make_r_r_r(PPCREC_IML_OP_ADD, regCalcEA, regA, regB);
	else
		ppcImlGenContext->emitInst().make_r_r(PPCREC_IML_OP_ASSIGN, regCalcEA, regB);
	IMLReg regCrLT = _GetRegCR(ppcImlGenContext, 0, Espresso::CR_BIT_INDEX_LT);
	IMLReg regCrGT = _GetRegCR(ppcImlGenContext, 0, Espresso::CR_BIT_INDEX_GT);
	IMLReg regCrEQ = _GetRegCR(ppcImlGenContext, 0, Espresso::CR_BIT_INDEX_EQ);
	IMLReg regCrSO = _GetRegCR(ppcImlGenContext, 0, Espresso::CR_BIT_INDEX_SO);
	IMLReg regXerSO = _GetRegCR(ppcImlGenContext, 0, Espresso::CR_BIT_INDEX_SO);
	ppcImlGenContext->emitInst().make_r_s32(PPCREC_IML_OP_ASSIGN, regCrLT, 0);
	ppcImlGenContext->emitInst().make_r_s32(PPCREC_IML_OP_ASSIGN, regCrGT, 0);
	ppcImlGenContext->emitInst().make_r_r(PPCREC_IML_OP_ASSIGN, regCrSO, regXerSO);
	IMLReg regMemResEA = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_CPU_MEMRES_EA);
	IMLReg regMemResVal = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_CPU_MEMRES_VAL);
	IMLReg regTmpBool = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_TEMPORARY + 1);
	ppcImlGenContext->emitInst().make_compare(regCalcEA, regMemResEA, regTmpBool, IMLCondition::EQ);
	ppcImlGenContext->emitInst().make_conditional_jump(regTmpBool, true);

	PPCIMLGen_CreateSegmentBranchedPath(*ppcImlGenContext, *ppcImlGenContext->currentBasicBlock,
		[&](ppcImlGenContext_t& genCtx)
		{
			ppcImlGenContext->emitInst().make_r_r(PPCREC_IML_OP_ENDIAN_SWAP, regTmpDataBE, regData);
			ppcImlGenContext->emitInst().make_r_r(PPCREC_IML_OP_ENDIAN_SWAP, regTmpCompareBE, regMemResVal);
			ppcImlGenContext->emitInst().make_atomic_cmp_store(regMemResEA, regTmpCompareBE, regTmpDataBE, regCrEQ);
		},
		[&](ppcImlGenContext_t& genCtx)
		{
			ppcImlGenContext->emitInst().make_r_s32(PPCREC_IML_OP_ASSIGN, regCrEQ, 0);
		}
	);

	ppcImlGenContext->emitInst().make_r_s32(PPCREC_IML_OP_ASSIGN, regMemResEA, 0);
	ppcImlGenContext->emitInst().make_r_s32(PPCREC_IML_OP_ASSIGN, regMemResVal, 0);

	return true;
}

bool PPCRecompilerImlGen_DCBZ(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	sint32 rA, rB;
	rA = (opcode>>16)&0x1F;
	rB = (opcode>>11)&0x1F;
	IMLReg regA = rA!=0?PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_R0+rA):IMLREG_INVALID;
	IMLReg regB = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_R0+rB);
	IMLReg regZero = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_TEMPORARY + 0);
	ppcImlGenContext->emitInst().make_r_s32(PPCREC_IML_OP_ASSIGN, regZero, 0);
	IMLReg regMemResEA = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_TEMPORARY + 1);
	if(rA != 0)
		ppcImlGenContext->emitInst().make_r_r_r(PPCREC_IML_OP_ADD, regMemResEA, regA, regB);
	else
		ppcImlGenContext->emitInst().make_r_r(PPCREC_IML_OP_ASSIGN, regMemResEA, regB);
	ppcImlGenContext->emitInst().make_r_r_s32(PPCREC_IML_OP_AND, regMemResEA, regMemResEA, ~31);
	for(sint32 i = 0; i < 32; i += 4)
		ppcImlGenContext->emitInst().make_memory_r(regZero, regMemResEA, i, 32, false);
	IMLReg regSize = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_TEMPORARY + 2);
	ppcImlGenContext->emitInst().make_r_s32(PPCREC_IML_OP_ASSIGN, regSize, 32);
	ppcImlGenContext->emitInst().make_call_imm((uintptr_t)PPCRecompiler_LatteBufferCache_notifyDCFlush, regMemResEA, regSize, IMLREG_INVALID, IMLREG_INVALID);
	return true;
}

bool PPCRecompilerImlGen_OR_NOR(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode, bool complementResult)
{
	int rS, rA, rB;
	PPC_OPC_TEMPL_X(opcode, rS, rA, rB);
	IMLReg regA = _GetRegGPR(ppcImlGenContext, rA);
	IMLReg regS = _GetRegGPR(ppcImlGenContext, rS);
	IMLReg regB = _GetRegGPR(ppcImlGenContext, rB);
	if(rS == rB)
		ppcImlGenContext->emitInst().make_r_r(PPCREC_IML_OP_ASSIGN, regA, regS);
	else
		ppcImlGenContext->emitInst().make_r_r_r(PPCREC_IML_OP_OR, regA, regS, regB);
	if(complementResult)
		ppcImlGenContext->emitInst().make_r_r(PPCREC_IML_OP_NOT, regA, regA);
	if (opcode & PPC_OPC_RC)
		PPCImlGen_UpdateCR0(ppcImlGenContext, regA);
	return true;
}

bool PPCRecompilerImlGen_ORC(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	sint32 rS, rA, rB;
	PPC_OPC_TEMPL_X(opcode, rS, rA, rB);
	IMLReg regS = _GetRegGPR(ppcImlGenContext, rS);
	IMLReg regB = _GetRegGPR(ppcImlGenContext, rB);
	IMLReg regTmp = _GetRegTemporary(ppcImlGenContext, 0);
	IMLReg regA = _GetRegGPR(ppcImlGenContext, rA);
	ppcImlGenContext->emitInst().make_r_r(PPCREC_IML_OP_NOT, regTmp, regB);
	ppcImlGenContext->emitInst().make_r_r_r(PPCREC_IML_OP_OR, regA, regS, regTmp);
	if (opcode & PPC_OPC_RC)
		PPCImlGen_UpdateCR0(ppcImlGenContext, regA);
	return true;
}

bool PPCRecompilerImlGen_AND_NAND(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode, bool complementResult)
{
	int rS, rA, rB;
	PPC_OPC_TEMPL_X(opcode, rS, rA, rB);
	IMLReg regA = _GetRegGPR(ppcImlGenContext, rA);
	IMLReg regS = _GetRegGPR(ppcImlGenContext, rS);
	IMLReg regB = _GetRegGPR(ppcImlGenContext, rB);
	if (regS == regB)
		ppcImlGenContext->emitInst().make_r_r(PPCREC_IML_OP_ASSIGN, regA, regS);
	else
		ppcImlGenContext->emitInst().make_r_r_r(PPCREC_IML_OP_AND, regA, regS, regB);
	if (complementResult)
		ppcImlGenContext->emitInst().make_r_r(PPCREC_IML_OP_NOT, regA, regA);
	if (opcode & PPC_OPC_RC)
		PPCImlGen_UpdateCR0(ppcImlGenContext, regA);
	return true;
}

bool PPCRecompilerImlGen_ANDC(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	sint32 rS, rA, rB;
	PPC_OPC_TEMPL_X(opcode, rS, rA, rB);
	IMLReg regS = _GetRegGPR(ppcImlGenContext, rS);
	IMLReg regB = _GetRegGPR(ppcImlGenContext, rB);
	IMLReg regTmp = _GetRegTemporary(ppcImlGenContext, 0);
	IMLReg regA = _GetRegGPR(ppcImlGenContext, rA);
	ppcImlGenContext->emitInst().make_r_r(PPCREC_IML_OP_NOT, regTmp, regB);
	ppcImlGenContext->emitInst().make_r_r_r(PPCREC_IML_OP_AND, regA, regS, regTmp);
	if (opcode & PPC_OPC_RC)
		PPCImlGen_UpdateCR0(ppcImlGenContext, regA);
	return true;
}

bool PPCRecompilerImlGen_XOR(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode, bool complementResult)
{
	sint32 rS, rA, rB;
	PPC_OPC_TEMPL_X(opcode, rS, rA, rB);
	IMLReg regA = _GetRegGPR(ppcImlGenContext, rA);
	if( rS == rB )
	{
		ppcImlGenContext->emitInst().make_r_s32(PPCREC_IML_OP_ASSIGN, regA, 0);
	}
	else
	{
		IMLReg regS = _GetRegGPR(ppcImlGenContext, rS);
		IMLReg regB = _GetRegGPR(ppcImlGenContext, rB);
		ppcImlGenContext->emitInst().make_r_r_r(PPCREC_IML_OP_XOR, regA, regS, regB);
	}
	if (complementResult)
		ppcImlGenContext->emitInst().make_r_r(PPCREC_IML_OP_NOT, regA, regA);
	if (opcode & PPC_OPC_RC)
		PPCImlGen_UpdateCR0(ppcImlGenContext, regA);
	return true;
}

void PPCRecompilerImlGen_ANDI_ANDIS(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode, bool isShifted)
{
	sint32 rS, rA;
	uint32 imm;
	if (isShifted)
	{
		PPC_OPC_TEMPL_D_Shift16(opcode, rS, rA, imm);
	}
	else
	{
		PPC_OPC_TEMPL_D_UImm(opcode, rS, rA, imm);
	}
	IMLReg regS = _GetRegGPR(ppcImlGenContext, rS);
	IMLReg regA = _GetRegGPR(ppcImlGenContext, rA);
	ppcImlGenContext->emitInst().make_r_r_s32(PPCREC_IML_OP_AND, regA, regS, (sint32)imm);
	PPCImlGen_UpdateCR0(ppcImlGenContext, regA);
}

void PPCRecompilerImlGen_ORI_ORIS(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode, bool isShifted)
{
	sint32 rS, rA;
	uint32 imm;
	if (isShifted)
	{
		PPC_OPC_TEMPL_D_Shift16(opcode, rS, rA, imm);
	}
	else
	{
		PPC_OPC_TEMPL_D_UImm(opcode, rS, rA, imm);
	}
	IMLReg regS = _GetRegGPR(ppcImlGenContext, rS);
	IMLReg regA = _GetRegGPR(ppcImlGenContext, rA);
	ppcImlGenContext->emitInst().make_r_r_s32(PPCREC_IML_OP_OR, regA, regS, (sint32)imm);
}

void PPCRecompilerImlGen_XORI_XORIS(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode, bool isShifted)
{
	sint32 rS, rA;
	uint32 imm;
	if (isShifted)
	{
		PPC_OPC_TEMPL_D_Shift16(opcode, rS, rA, imm);
	}
	else
	{
		PPC_OPC_TEMPL_D_UImm(opcode, rS, rA, imm);
	}
	IMLReg regS = _GetRegGPR(ppcImlGenContext, rS);
	IMLReg regA = _GetRegGPR(ppcImlGenContext, rA);
	ppcImlGenContext->emitInst().make_r_r_s32(PPCREC_IML_OP_XOR, regA, regS, (sint32)imm);
}

bool PPCRecompilerImlGen_CROR(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	int crD, crA, crB;
	PPC_OPC_TEMPL_X(opcode, crD, crA, crB);
	IMLReg regCrA = _GetRegCR(ppcImlGenContext, crA);
	IMLReg regCrB = _GetRegCR(ppcImlGenContext, crB);
	IMLReg regCrR = _GetRegCR(ppcImlGenContext, crD);
	ppcImlGenContext->emitInst().make_r_r_r(PPCREC_IML_OP_OR, regCrR, regCrA, regCrB);
	return true;
}

bool PPCRecompilerImlGen_CRORC(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	int crD, crA, crB;
	PPC_OPC_TEMPL_X(opcode, crD, crA, crB);
	IMLReg regCrA = _GetRegCR(ppcImlGenContext, crA);
	IMLReg regCrB = _GetRegCR(ppcImlGenContext, crB);
	IMLReg regCrR = _GetRegCR(ppcImlGenContext, crD);
	IMLReg regTmp = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_TEMPORARY);
	ppcImlGenContext->emitInst().make_r_r_s32(PPCREC_IML_OP_XOR, regTmp, regCrB, 1);
	ppcImlGenContext->emitInst().make_r_r_r(PPCREC_IML_OP_OR, regCrR, regCrA, regTmp);
	return true;
}

bool PPCRecompilerImlGen_CRAND(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	int crD, crA, crB;
	PPC_OPC_TEMPL_X(opcode, crD, crA, crB);
	IMLReg regCrA = _GetRegCR(ppcImlGenContext, crA);
	IMLReg regCrB = _GetRegCR(ppcImlGenContext, crB);
	IMLReg regCrR = _GetRegCR(ppcImlGenContext, crD);
	ppcImlGenContext->emitInst().make_r_r_r(PPCREC_IML_OP_AND, regCrR, regCrA, regCrB);
	return true;
}

bool PPCRecompilerImlGen_CRANDC(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	int crD, crA, crB;
	PPC_OPC_TEMPL_X(opcode, crD, crA, crB);
	IMLReg regCrA = _GetRegCR(ppcImlGenContext, crA);
	IMLReg regCrB = _GetRegCR(ppcImlGenContext, crB);
	IMLReg regCrR = _GetRegCR(ppcImlGenContext, crD);
	IMLReg regTmp = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_TEMPORARY);
	ppcImlGenContext->emitInst().make_r_r_s32(PPCREC_IML_OP_XOR, regTmp, regCrB, 1);
	ppcImlGenContext->emitInst().make_r_r_r(PPCREC_IML_OP_AND, regCrR, regCrA, regTmp);
	return true;
}

bool PPCRecompilerImlGen_CRXOR(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	int crD, crA, crB;
	PPC_OPC_TEMPL_X(opcode, crD, crA, crB);
	IMLReg regCrA = _GetRegCR(ppcImlGenContext, crA);
	IMLReg regCrB = _GetRegCR(ppcImlGenContext, crB);
	IMLReg regCrR = _GetRegCR(ppcImlGenContext, crD);
	if (regCrA == regCrB)
	{
		ppcImlGenContext->emitInst().make_r_s32(PPCREC_IML_OP_ASSIGN, regCrR, 0);
		return true;
	}
	ppcImlGenContext->emitInst().make_r_r_r(PPCREC_IML_OP_XOR, regCrR, regCrA, regCrB);
	return true;
}

bool PPCRecompilerImlGen_CREQV(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	int crD, crA, crB;
	PPC_OPC_TEMPL_X(opcode, crD, crA, crB);
	IMLReg regCrA = _GetRegCR(ppcImlGenContext, crA);
	IMLReg regCrB = _GetRegCR(ppcImlGenContext, crB);
	IMLReg regCrR = _GetRegCR(ppcImlGenContext, crD);
	if (regCrA == regCrB)
	{
		ppcImlGenContext->emitInst().make_r_s32(PPCREC_IML_OP_ASSIGN, regCrR, 1);
		return true;
	}
	IMLReg regTmp = PPCRecompilerImlGen_loadRegister(ppcImlGenContext, PPCREC_NAME_TEMPORARY);
	ppcImlGenContext->emitInst().make_r_r_s32(PPCREC_IML_OP_XOR, regTmp, regCrB, 1);
	ppcImlGenContext->emitInst().make_r_r_r(PPCREC_IML_OP_XOR, regCrR, regCrA, regTmp);
	return true;
}

bool PPCRecompilerImlGen_HLE(ppcImlGenContext_t* ppcImlGenContext, uint32 opcode)
{
	uint32 hleFuncId = opcode&0xFFFF;
	ppcImlGenContext->emitInst().make_macro(PPCREC_IML_MACRO_HLE, ppcImlGenContext->ppcAddressOfCurrentInstruction, hleFuncId, 0, IMLREG_INVALID);
	return true;
}

uint32 PPCRecompiler_iterateCurrentInstruction(ppcImlGenContext_t* ppcImlGenContext)
{
	uint32 v = CPU_swapEndianU32(*(ppcImlGenContext->currentInstruction));
	ppcImlGenContext->currentInstruction += 1;
	return v;
}

uint32 PPCRecompiler_getCurrentInstruction(ppcImlGenContext_t* ppcImlGenContext)
{
	uint32 v = CPU_swapEndianU32(*(ppcImlGenContext->currentInstruction));
	return v;
}

uint32 PPCRecompiler_getPreviousInstruction(ppcImlGenContext_t* ppcImlGenContext)
{
	uint32 v = CPU_swapEndianU32(*(ppcImlGenContext->currentInstruction-1));
	return v;
}

void PPCRecompilerIml_setSegmentPoint(IMLSegmentPoint* segmentPoint, IMLSegment* imlSegment, sint32 index)
{
	segmentPoint->imlSegment = imlSegment;
	segmentPoint->SetInstructionIndex(index);
	if (imlSegment->segmentPointList)
		imlSegment->segmentPointList->prev = segmentPoint;
	segmentPoint->prev = nullptr;
	segmentPoint->next = imlSegment->segmentPointList;
	imlSegment->segmentPointList = segmentPoint;
}

void PPCRecompilerIml_removeSegmentPoint(IMLSegmentPoint* segmentPoint)
{
	if (segmentPoint->prev)
		segmentPoint->prev->next = segmentPoint->next;
	else
		segmentPoint->imlSegment->segmentPointList = segmentPoint->next;
	if (segmentPoint->next)
		segmentPoint->next->prev = segmentPoint->prev;
}

void PPCRecompiler_pushBackIMLInstructions(IMLSegment* imlSegment, sint32 index, sint32 shiftBackCount)
{
	cemu_assert_debug(index >= 0 && index <= imlSegment->imlList.size());

	imlSegment->imlList.insert(imlSegment->imlList.begin() + index, shiftBackCount, {});

	memset(imlSegment->imlList.data() + index, 0, sizeof(IMLInstruction) * shiftBackCount);

	for (sint32 i = 0; i < shiftBackCount; i++)
	{
		imlSegment->imlList[index + i].type = PPCREC_IML_TYPE_NONE;
	}

	if (imlSegment->segmentPointList)
	{
		IMLSegmentPoint* segmentPoint = imlSegment->segmentPointList;
		while (segmentPoint)
		{
			segmentPoint->ShiftIfAfter(index, shiftBackCount);
			segmentPoint = segmentPoint->next;
		}
	}
}

IMLInstruction* PPCRecompiler_insertInstruction(IMLSegment* imlSegment, sint32 index)
{
	PPCRecompiler_pushBackIMLInstructions(imlSegment, index, 1);
	return imlSegment->imlList.data() + index;
}

IMLInstruction* PPCRecompiler_appendInstruction(IMLSegment* imlSegment)
{
	size_t index = imlSegment->imlList.size();
	imlSegment->imlList.emplace_back();
	memset(imlSegment->imlList.data() + index, 0, sizeof(IMLInstruction));
	return imlSegment->imlList.data() + index;
}

IMLSegment* PPCRecompilerIml_appendSegment(ppcImlGenContext_t* ppcImlGenContext)
{
	IMLSegment* segment = new IMLSegment();
	ppcImlGenContext->segmentList2.emplace_back(segment);
	return segment;
}

void PPCRecompilerIml_insertSegments(ppcImlGenContext_t* ppcImlGenContext, sint32 index, sint32 count)
{
	ppcImlGenContext->segmentList2.insert(ppcImlGenContext->segmentList2.begin() + index, count, nullptr);
	for (sint32 i = 0; i < count; i++)
		ppcImlGenContext->segmentList2[index + i] = new IMLSegment();
}

bool PPCRecompiler_decodePPCInstruction(ppcImlGenContext_t* ppcImlGenContext)
{
	bool unsupportedInstructionFound = false;

	uint32 opcode = PPCRecompiler_iterateCurrentInstruction(ppcImlGenContext);
	switch ((opcode >> 26))
	{
	case 1:
		if (PPCRecompilerImlGen_HLE(ppcImlGenContext, opcode) == false)
			unsupportedInstructionFound = true;
		ppcImlGenContext->hasFPUInstruction = true;
		break;
	case 4:
		switch (PPC_getBits(opcode, 30, 5))
		{
		case 0:
			switch (PPC_getBits(opcode, 25, 5))
			{
			case 0:
				if( !PPCRecompilerImlGen_PS_CMPU0(ppcImlGenContext, opcode) )
					unsupportedInstructionFound = true;
				ppcImlGenContext->hasFPUInstruction = true;
				break;
			case 1:
				if( !PPCRecompilerImlGen_PS_CMPO0(ppcImlGenContext, opcode) )
					unsupportedInstructionFound = true;
				ppcImlGenContext->hasFPUInstruction = true;
				break;
			case 2:
				if( !PPCRecompilerImlGen_PS_CMPU1(ppcImlGenContext, opcode) )
					unsupportedInstructionFound = true;
				ppcImlGenContext->hasFPUInstruction = true;
				break;
			default:
				unsupportedInstructionFound = true;
				break;
			}
			break;
		case 8:
			switch (PPC_getBits(opcode, 25, 5))
			{
			case 1:
				if (PPCRecompilerImlGen_PS_NEG(ppcImlGenContext, opcode) == false)
					unsupportedInstructionFound = true;
				ppcImlGenContext->hasFPUInstruction = true;
				break;
			case 2:
				if (PPCRecompilerImlGen_PS_MR(ppcImlGenContext, opcode) == false)
					unsupportedInstructionFound = true;
				ppcImlGenContext->hasFPUInstruction = true;
				break;
			case 8:
				if (PPCRecompilerImlGen_PS_ABS(ppcImlGenContext, opcode) == false)
					unsupportedInstructionFound = true;
				ppcImlGenContext->hasFPUInstruction = true;
				break;
			default:
				unsupportedInstructionFound = true;
				break;
			}
			break;
		case 10:
			if (PPCRecompilerImlGen_PS_SUM0(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			ppcImlGenContext->hasFPUInstruction = true;
			break;
		case 11:
			if (PPCRecompilerImlGen_PS_SUM1(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			ppcImlGenContext->hasFPUInstruction = true;
			break;
		case 12:
			if (PPCRecompilerImlGen_PS_MULSX(ppcImlGenContext, opcode, false) == false)
				unsupportedInstructionFound = true;
			ppcImlGenContext->hasFPUInstruction = true;
			break;
		case 13:
			if (PPCRecompilerImlGen_PS_MULSX(ppcImlGenContext, opcode, true) == false)
				unsupportedInstructionFound = true;
			ppcImlGenContext->hasFPUInstruction = true;
			break;
		case 14:
			if (PPCRecompilerImlGen_PS_MADDSX(ppcImlGenContext, opcode, false) == false)
				unsupportedInstructionFound = true;
			ppcImlGenContext->hasFPUInstruction = true;
			break;
		case 15:
			if (PPCRecompilerImlGen_PS_MADDSX(ppcImlGenContext, opcode, true) == false)
				unsupportedInstructionFound = true;
			ppcImlGenContext->hasFPUInstruction = true;
			break;
		case 16:
			switch (PPC_getBits(opcode, 25, 5))
			{
			case 16:
				if (PPCRecompilerImlGen_PS_MERGE00(ppcImlGenContext, opcode) == false)
					unsupportedInstructionFound = true;
				ppcImlGenContext->hasFPUInstruction = true;
				break;
			case 17:
				if (PPCRecompilerImlGen_PS_MERGE01(ppcImlGenContext, opcode) == false)
					unsupportedInstructionFound = true;
				ppcImlGenContext->hasFPUInstruction = true;
				break;
			case 18:
				if (PPCRecompilerImlGen_PS_MERGE10(ppcImlGenContext, opcode) == false)
					unsupportedInstructionFound = true;
				ppcImlGenContext->hasFPUInstruction = true;
				break;
			case 19:
				if (PPCRecompilerImlGen_PS_MERGE11(ppcImlGenContext, opcode) == false)
					unsupportedInstructionFound = true;
				ppcImlGenContext->hasFPUInstruction = true;
				break;
			default:
				unsupportedInstructionFound = true;
				break;
			}
			break;
		case 18:
			if (PPCRecompilerImlGen_PS_DIV(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			ppcImlGenContext->hasFPUInstruction = true;
			break;
		case 20:
			if (PPCRecompilerImlGen_PS_SUB(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			ppcImlGenContext->hasFPUInstruction = true;
			break;
		case 21:
			if (PPCRecompilerImlGen_PS_ADD(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			ppcImlGenContext->hasFPUInstruction = true;
			break;
		case 23:
			if (PPCRecompilerImlGen_PS_SEL(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			ppcImlGenContext->hasFPUInstruction = true;
			break;
		case 25:
			if (PPCRecompilerImlGen_PS_MUL(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			ppcImlGenContext->hasFPUInstruction = true;
			break;
		case 24:
			if (PPCRecompilerImlGen_PS_RES(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			ppcImlGenContext->hasFPUInstruction = true;
			break;
		case 26:
			if (PPCRecompilerImlGen_PS_RSQRTE(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			ppcImlGenContext->hasFPUInstruction = true;
			break;
		case 28:
			if (PPCRecompilerImlGen_PS_MSUB(ppcImlGenContext, opcode, false) == false)
				unsupportedInstructionFound = true;
			ppcImlGenContext->hasFPUInstruction = true;
			break;
		case 29:
			if (PPCRecompilerImlGen_PS_MADD(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			ppcImlGenContext->hasFPUInstruction = true;
			break;
		case 30:
			if (PPCRecompilerImlGen_PS_MSUB(ppcImlGenContext, opcode, true) == false)
				unsupportedInstructionFound = true;
			ppcImlGenContext->hasFPUInstruction = true;
			break;
		case 31:
			if (PPCRecompilerImlGen_PS_NMADD(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			ppcImlGenContext->hasFPUInstruction = true;
			break;
		default:
			unsupportedInstructionFound = true;
			break;
		}
		break;
	case 7:
		PPCRecompilerImlGen_MULLI(ppcImlGenContext, opcode);
		break;
	case 8:
		if (!PPCRecompilerImlGen_SUBFIC(ppcImlGenContext, opcode))
			unsupportedInstructionFound = true;
		break;
	case 10:
		if (!PPCRecompilerImlGen_CMPI(ppcImlGenContext, opcode, true))
			unsupportedInstructionFound = true;
		break;
	case 11:
		if (!PPCRecompilerImlGen_CMPI(ppcImlGenContext, opcode, false))
			unsupportedInstructionFound = true;
		break;
	case 12:
		if (PPCRecompilerImlGen_ADDIC_(ppcImlGenContext, opcode, false) == false)
			unsupportedInstructionFound = true;
		break;
	case 13:
		if (PPCRecompilerImlGen_ADDIC_(ppcImlGenContext, opcode, true) == false)
			unsupportedInstructionFound = true;
		break;
	case 14:
		if (PPCRecompilerImlGen_ADDI(ppcImlGenContext, opcode) == false)
			unsupportedInstructionFound = true;
		break;
	case 15:
		if (PPCRecompilerImlGen_ADDIS(ppcImlGenContext, opcode) == false)
			unsupportedInstructionFound = true;
		break;
	case 16:
		if (PPCRecompilerImlGen_BC(ppcImlGenContext, opcode) == false)
			unsupportedInstructionFound = true;
		break;
	case 17:
		if (PPC_getBits(opcode, 30, 1) == 1)
		{
		}
		else
		{
			unsupportedInstructionFound = true;
		}
		break;
	case 18:
		if (PPCRecompilerImlGen_B(ppcImlGenContext, opcode) == false)
			unsupportedInstructionFound = true;
		break;
	case 19:
		switch (PPC_getBits(opcode, 30, 10))
		{
		case 0:
			PPCRecompilerImlGen_MCRF(ppcImlGenContext, opcode);
			break;
		case 16:
			if (PPCRecompilerImlGen_BCSPR(ppcImlGenContext, opcode, SPR_LR) == false)
				unsupportedInstructionFound = true;
			break;
		case 129:
			if (PPCRecompilerImlGen_CRANDC(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			break;
		case 150:
			if (PPCRecompilerImlGen_ISYNC(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			break;
		case 193:
			if (PPCRecompilerImlGen_CRXOR(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			break;
		case 257:
			if (PPCRecompilerImlGen_CRAND(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			break;
		case 289:
			if (PPCRecompilerImlGen_CREQV(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			break;
		case 417:
			if (PPCRecompilerImlGen_CRORC(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			break;
		case 449:
			if (PPCRecompilerImlGen_CROR(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			break;
		case 528:
			if (PPCRecompilerImlGen_BCSPR(ppcImlGenContext, opcode, SPR_CTR) == false)
				unsupportedInstructionFound = true;
			break;
		default:
			unsupportedInstructionFound = true;
			break;
		}
		break;
	case 20:
		if (PPCRecompilerImlGen_RLWIMI(ppcImlGenContext, opcode) == false)
			unsupportedInstructionFound = true;
		break;
	case 21:
		if (PPCRecompilerImlGen_RLWINM(ppcImlGenContext, opcode) == false)
			unsupportedInstructionFound = true;
		break;
	case 23:
		if (PPCRecompilerImlGen_RLWNM(ppcImlGenContext, opcode) == false)
			unsupportedInstructionFound = true;
		break;
	case 24:
		PPCRecompilerImlGen_ORI_ORIS(ppcImlGenContext, opcode, false);
		break;
	case 25:
		PPCRecompilerImlGen_ORI_ORIS(ppcImlGenContext, opcode, true);
		break;
	case 26:
		PPCRecompilerImlGen_XORI_XORIS(ppcImlGenContext, opcode, false);
		break;
	case 27:
		PPCRecompilerImlGen_XORI_XORIS(ppcImlGenContext, opcode, true);
		break;
	case 28:
		PPCRecompilerImlGen_ANDI_ANDIS(ppcImlGenContext, opcode, false);
		break;
	case 29:
		PPCRecompilerImlGen_ANDI_ANDIS(ppcImlGenContext, opcode, true);
		break;
	case 31:
		switch (PPC_getBits(opcode, 30, 10))
		{
		case 0:
			PPCRecompilerImlGen_CMP(ppcImlGenContext, opcode, false);
			break;
		case 4:
			PPCRecompilerImlGen_TW(ppcImlGenContext, opcode);
			break;
		case 8:
		if (PPCRecompilerImlGen_SUBFC(ppcImlGenContext, opcode) == false)
			unsupportedInstructionFound = true;
		break;
		case 10:
			if (PPCRecompilerImlGen_ADDC(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			break;
		case 11:
			if (PPCRecompilerImlGen_MULHWU(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			break;
		case 19:
			if (PPCRecompilerImlGen_MFCR(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			break;
		case 20:
			if (PPCRecompilerImlGen_LWARX(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			break;
		case 23:
			PPCRecompilerImlGen_LOAD_INDEXED(ppcImlGenContext, opcode, 32, false, true, false);
			break;
		case 24:
			if (PPCRecompilerImlGen_SLW(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			break;
		case 26:
			if (PPCRecompilerImlGen_CNTLZW(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			break;
		case 28:
			if (!PPCRecompilerImlGen_AND_NAND(ppcImlGenContext, opcode, false))
				unsupportedInstructionFound = true;
			break;
		case 32:
			PPCRecompilerImlGen_CMP(ppcImlGenContext, opcode, true);
			break;
		case 40:
			if (PPCRecompilerImlGen_SUBF(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			break;
		case 54:
			if (PPCRecompilerImlGen_DCBFlush(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			break;
		case 55:
			PPCRecompilerImlGen_LOAD_INDEXED(ppcImlGenContext, opcode, 32, false, true, true);
			break;
		case 60:
			if (!PPCRecompilerImlGen_ANDC(ppcImlGenContext, opcode))
				unsupportedInstructionFound = true;
			break;
		case 75:
			if (PPCRecompilerImlGen_MULHW(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			break;
		case 86:
			if (PPCRecompilerImlGen_DCBFlush(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			break;
		case 87:
			PPCRecompilerImlGen_LOAD_INDEXED(ppcImlGenContext, opcode, 8, false, true, false);
			break;
		case 104:
			if (PPCRecompilerImlGen_NEG(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			break;
		case 119:
			PPCRecompilerImlGen_LOAD_INDEXED(ppcImlGenContext, opcode, 8, false, true, true);
			break;
		case 124:
			if (!PPCRecompilerImlGen_OR_NOR(ppcImlGenContext, opcode, true))
				unsupportedInstructionFound = true;
			break;
		case 136:
			if (PPCRecompilerImlGen_SUBFE(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			break;
		case 138:
			if (PPCRecompilerImlGen_ADDE(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			break;
		case 144:
			if( !PPCRecompilerImlGen_MTCRF(ppcImlGenContext, opcode))
				unsupportedInstructionFound = true;
			break;
		case 150:
			if (!PPCRecompilerImlGen_STWCX(ppcImlGenContext, opcode))
				unsupportedInstructionFound = true;
			break;
		case 151:
			if (!PPCRecompilerImlGen_STORE_INDEXED(ppcImlGenContext, opcode, 32, true, false))
				unsupportedInstructionFound = true;
			break;
		case 183:
			if (!PPCRecompilerImlGen_STORE_INDEXED(ppcImlGenContext, opcode, 32, true, true))
				unsupportedInstructionFound = true;
			break;
		case 200:
			if (PPCRecompilerImlGen_SUBFZE(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			break;
		case 202:
			if (PPCRecompilerImlGen_ADDZE(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			break;
		case 215:
			if (!PPCRecompilerImlGen_STORE_INDEXED(ppcImlGenContext, opcode, 8, true, false))
				unsupportedInstructionFound = true;
			break;
		case 234:
			if (PPCRecompilerImlGen_ADDME(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			break;
		case 235:
			if (PPCRecompilerImlGen_MULLW(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			break;
		case 247:
			if (!PPCRecompilerImlGen_STORE_INDEXED(ppcImlGenContext, opcode, 8, true, true))
				unsupportedInstructionFound = true;
			break;
		case 266:
			if (PPCRecompilerImlGen_ADD(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			break;
		case 279:
			PPCRecompilerImlGen_LOAD_INDEXED(ppcImlGenContext, opcode, 16, false, true, false);
			break;
		case 284:
			if (!PPCRecompilerImlGen_XOR(ppcImlGenContext, opcode, true))
				unsupportedInstructionFound = true;
			break;
		case 311:
			PPCRecompilerImlGen_LOAD_INDEXED(ppcImlGenContext, opcode, 16, false, true, true);
			break;
		case 316:
			if (!PPCRecompilerImlGen_XOR(ppcImlGenContext, opcode, false))
				unsupportedInstructionFound = true;
			break;
		case 339:
			if (PPCRecompilerImlGen_MFSPR(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			break;
		case 343:
			PPCRecompilerImlGen_LOAD_INDEXED(ppcImlGenContext, opcode, 16, true, true, false);
			break;
		case 371:
			if (PPCRecompilerImlGen_MFTB(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			break;
		case 375:
			PPCRecompilerImlGen_LOAD_INDEXED(ppcImlGenContext, opcode, 16, true, true, true);
			break;
		case 407:
			if (!PPCRecompilerImlGen_STORE_INDEXED(ppcImlGenContext, opcode, 16, true, false))
				unsupportedInstructionFound = true;
			break;
		case 412:
			if (PPCRecompilerImlGen_ORC(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			break;
		case 439:
			if (!PPCRecompilerImlGen_STORE_INDEXED(ppcImlGenContext, opcode, 16, true, true))
				unsupportedInstructionFound = true;
			break;
		case 444:
			if (!PPCRecompilerImlGen_OR_NOR(ppcImlGenContext, opcode, false))
				unsupportedInstructionFound = true;
			break;
		case 459:
			PPCRecompilerImlGen_DIVWU(ppcImlGenContext, opcode);
			break;
		case 467:
			if (PPCRecompilerImlGen_MTSPR(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			break;
		case 476:
			if (!PPCRecompilerImlGen_AND_NAND(ppcImlGenContext, opcode, true))
				unsupportedInstructionFound = true;
			break;
		case 491:
			if (PPCRecompilerImlGen_DIVW(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			break;
		case 534:
			PPCRecompilerImlGen_LOAD_INDEXED(ppcImlGenContext, opcode, 32, false, false, false);
			break;
		case 535:
			if (PPCRecompilerImlGen_LFSX_LFSUX_LFDX_LFDUX(ppcImlGenContext, opcode, false, false) == false)
				unsupportedInstructionFound = true;
			ppcImlGenContext->hasFPUInstruction = true;
			break;
		case 536:
			if (PPCRecompilerImlGen_SRW(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			break;
		case 567:
			if (PPCRecompilerImlGen_LFSX_LFSUX_LFDX_LFDUX(ppcImlGenContext, opcode, true, false) == false)
				unsupportedInstructionFound = true;
			ppcImlGenContext->hasFPUInstruction = true;
			break;
		case 597:
			if (PPCRecompilerImlGen_LSWI(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			break;
		case 598:
			PPCRecompilerImlGen_SYNC(ppcImlGenContext, opcode);
			break;
		case 599:
			if (PPCRecompilerImlGen_LFSX_LFSUX_LFDX_LFDUX(ppcImlGenContext, opcode, false, true) == false)
				unsupportedInstructionFound = true;
			ppcImlGenContext->hasFPUInstruction = true;
			break;
		case 631:
			if (PPCRecompilerImlGen_LFSX_LFSUX_LFDX_LFDUX(ppcImlGenContext, opcode, true, true) == false)
				unsupportedInstructionFound = true;
			ppcImlGenContext->hasFPUInstruction = true;
			break;
		case 662:
			if (!PPCRecompilerImlGen_STORE_INDEXED(ppcImlGenContext, opcode, 32, false, false))
				unsupportedInstructionFound = true;
			break;
		case 663:
			if (PPCRecompilerImlGen_STFSX_STFSUX_STFDX_STFDUX(ppcImlGenContext, opcode, false, false) == false)
				unsupportedInstructionFound = true;
			break;
		case 695:
			if (PPCRecompilerImlGen_STFSX_STFSUX_STFDX_STFDUX(ppcImlGenContext, opcode, true, false) == false)
				unsupportedInstructionFound = true;
			break;
		case 725:
			if (PPCRecompilerImlGen_STSWI(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			break;
		case 727:
			if (PPCRecompilerImlGen_STFSX_STFSUX_STFDX_STFDUX(ppcImlGenContext, opcode, false, true) == false)
				unsupportedInstructionFound = true;
			break;
		case 759:
			if (PPCRecompilerImlGen_STFSX_STFSUX_STFDX_STFDUX(ppcImlGenContext, opcode, true, true) == false)
				unsupportedInstructionFound = true;
			break;
		case 790:
			PPCRecompilerImlGen_LOAD_INDEXED(ppcImlGenContext, opcode, 16, false, false, false);
			break;
		case 792:
			if (PPCRecompilerImlGen_SRAW(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			break;
		case 824:
			if (PPCRecompilerImlGen_SRAWI(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			break;
		case 918:
			if (!PPCRecompilerImlGen_STORE_INDEXED(ppcImlGenContext, opcode, 16, false, false))
				unsupportedInstructionFound = true;
			break;
		case 922:
			if (PPCRecompilerImlGen_EXTSH(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			break;
		case 954:
			if (PPCRecompilerImlGen_EXTSB(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			break;
		case 983:
			if (PPCRecompilerImlGen_STFIWX(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			break;
		case 1014:
			if (PPCRecompilerImlGen_DCBZ(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			break;
		default:
			unsupportedInstructionFound = true;
			break;
		}
		break;
	case 32:
		if(!PPCRecompilerImlGen_LOAD(ppcImlGenContext, opcode, 32, false, true, false))
			unsupportedInstructionFound = true;
		break;
	case 33:
		if (!PPCRecompilerImlGen_LOAD(ppcImlGenContext, opcode, 32, false, true, true))
			unsupportedInstructionFound = true;
		break;
	case 34:
		if (!PPCRecompilerImlGen_LOAD(ppcImlGenContext, opcode, 8, false, true, false))
			unsupportedInstructionFound = true;
		break;
	case 35:
		if (!PPCRecompilerImlGen_LOAD(ppcImlGenContext, opcode, 8, false, true, true))
			unsupportedInstructionFound = true;
		break;
	case 36:
		if(!PPCRecompilerImlGen_STORE(ppcImlGenContext, opcode, 32, true, false))
			unsupportedInstructionFound = true;
		break;
	case 37:
		if (!PPCRecompilerImlGen_STORE(ppcImlGenContext, opcode, 32, true, true))
			unsupportedInstructionFound = true;
		break;
	case 38:
		if (!PPCRecompilerImlGen_STORE(ppcImlGenContext, opcode, 8, true, false))
			unsupportedInstructionFound = true;
		break;
	case 39:
		if (!PPCRecompilerImlGen_STORE(ppcImlGenContext, opcode, 8, true, true))
			unsupportedInstructionFound = true;
		break;
	case 40:
		if (!PPCRecompilerImlGen_LOAD(ppcImlGenContext, opcode, 16, false, true, false))
			unsupportedInstructionFound = true;
		break;
	case 41:
		if (!PPCRecompilerImlGen_LOAD(ppcImlGenContext, opcode, 16, false, true, true))
			unsupportedInstructionFound = true;
		break;
	case 42:
		if (!PPCRecompilerImlGen_LOAD(ppcImlGenContext, opcode, 16, true, true, false))
			unsupportedInstructionFound = true;
		break;
	case 43:
		if (!PPCRecompilerImlGen_LOAD(ppcImlGenContext, opcode, 16, true, true, true))
			unsupportedInstructionFound = true;
		break;
	case 44:
		if (!PPCRecompilerImlGen_STORE(ppcImlGenContext, opcode, 16, true, false))
			unsupportedInstructionFound = true;
		break;
	case 45:
		if (!PPCRecompilerImlGen_STORE(ppcImlGenContext, opcode, 16, true, true))
			unsupportedInstructionFound = true;
		break;
	case 46:
		PPCRecompilerImlGen_LMW(ppcImlGenContext, opcode);
		break;
	case 47:
		PPCRecompilerImlGen_STMW(ppcImlGenContext, opcode);
		break;
	case 48:
		if (PPCRecompilerImlGen_LFS_LFSU_LFD_LFDU(ppcImlGenContext, opcode, false, false) == false)
			unsupportedInstructionFound = true;
		ppcImlGenContext->hasFPUInstruction = true;
		break;
	case 49:
		if (PPCRecompilerImlGen_LFS_LFSU_LFD_LFDU(ppcImlGenContext, opcode, true, false) == false)
			unsupportedInstructionFound = true;
		ppcImlGenContext->hasFPUInstruction = true;
		break;
	case 50:
		if (PPCRecompilerImlGen_LFS_LFSU_LFD_LFDU(ppcImlGenContext, opcode, false, true) == false)
			unsupportedInstructionFound = true;
		ppcImlGenContext->hasFPUInstruction = true;
		break;
	case 51:
		if (PPCRecompilerImlGen_LFS_LFSU_LFD_LFDU(ppcImlGenContext, opcode, true, true) == false)
			unsupportedInstructionFound = true;
		ppcImlGenContext->hasFPUInstruction = true;
		break;
	case 52:
		if (PPCRecompilerImlGen_STFS_STFSU_STFD_STFDU(ppcImlGenContext, opcode, false, false) == false)
			unsupportedInstructionFound = true;
		ppcImlGenContext->hasFPUInstruction = true;
		break;
	case 53:
		if (PPCRecompilerImlGen_STFS_STFSU_STFD_STFDU(ppcImlGenContext, opcode, true, false) == false)
			unsupportedInstructionFound = true;
		ppcImlGenContext->hasFPUInstruction = true;
		break;
	case 54:
		if (PPCRecompilerImlGen_STFS_STFSU_STFD_STFDU(ppcImlGenContext, opcode, false, true) == false)
			unsupportedInstructionFound = true;
		ppcImlGenContext->hasFPUInstruction = true;
		break;
	case 55:
		if (PPCRecompilerImlGen_STFS_STFSU_STFD_STFDU(ppcImlGenContext, opcode, true, true) == false)
			unsupportedInstructionFound = true;
		ppcImlGenContext->hasFPUInstruction = true;
		break;
	case 56:
		if (PPCRecompilerImlGen_PSQ_L(ppcImlGenContext, opcode, false) == false)
			unsupportedInstructionFound = true;
		ppcImlGenContext->hasFPUInstruction = true;
		break;
	case 57:
		if (PPCRecompilerImlGen_PSQ_L(ppcImlGenContext, opcode, true) == false)
			unsupportedInstructionFound = true;
		ppcImlGenContext->hasFPUInstruction = true;
		break;
	case 59:
		switch (PPC_getBits(opcode, 30, 5))
		{
		case 18:
			if (PPCRecompilerImlGen_FDIVS(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			ppcImlGenContext->hasFPUInstruction = true;
			break;
		case 20:
			if (PPCRecompilerImlGen_FSUBS(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			ppcImlGenContext->hasFPUInstruction = true;
			break;
		case 21:
			if (PPCRecompilerImlGen_FADDS(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			ppcImlGenContext->hasFPUInstruction = true;
			break;
		case 24:
			if (PPCRecompilerImlGen_FRES(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			ppcImlGenContext->hasFPUInstruction = true;
			break;
		case 25:
			if (PPCRecompilerImlGen_FMULS(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			ppcImlGenContext->hasFPUInstruction = true;
			break;
		case 28:
			if (PPCRecompilerImlGen_FMSUBS(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			ppcImlGenContext->hasFPUInstruction = true;
			break;
		case 29:
			if (PPCRecompilerImlGen_FMADDS(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			ppcImlGenContext->hasFPUInstruction = true;
			break;
		case 30:
			if (PPCRecompilerImlGen_FNMSUBS(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			ppcImlGenContext->hasFPUInstruction = true;
			break;
		default:
			unsupportedInstructionFound = true;
			break;
		}
		break;
	case 60:
		if (PPCRecompilerImlGen_PSQ_ST(ppcImlGenContext, opcode, false) == false)
			unsupportedInstructionFound = true;
		ppcImlGenContext->hasFPUInstruction = true;
		break;
	case 61:
		if (PPCRecompilerImlGen_PSQ_ST(ppcImlGenContext, opcode, true) == false)
			unsupportedInstructionFound = true;
		ppcImlGenContext->hasFPUInstruction = true;
		break;
	case 63:
		switch (PPC_getBits(opcode, 30, 5))
		{
		case 0:
			switch (PPC_getBits(opcode, 30, 10))
			{
			case 0:
				if (PPCRecompilerImlGen_FCMPU(ppcImlGenContext, opcode) == false)
					unsupportedInstructionFound = true;
				break;
			case 32:
				if (PPCRecompilerImlGen_FCMPO(ppcImlGenContext, opcode) == false)
					unsupportedInstructionFound = true;
				break;
			default:
				unsupportedInstructionFound = true;
				break;
			}
			ppcImlGenContext->hasFPUInstruction = true;
			break;
		case 12:
			if (PPCRecompilerImlGen_FRSP(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			ppcImlGenContext->hasFPUInstruction = true;
			break;
		case 15:
			if (PPCRecompilerImlGen_FCTIWZ(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			ppcImlGenContext->hasFPUInstruction = true;
			break;
		case 18:
			if (PPCRecompilerImlGen_FDIV(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			ppcImlGenContext->hasFPUInstruction = true;
			break;
		case 20:
			if (PPCRecompilerImlGen_FSUB(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			ppcImlGenContext->hasFPUInstruction = true;
			break;
		case 21:
			if (PPCRecompilerImlGen_FADD(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			ppcImlGenContext->hasFPUInstruction = true;
			break;
		case 23:
			if (PPCRecompilerImlGen_FSEL(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			ppcImlGenContext->hasFPUInstruction = true;
			break;
		case 25:
			if (PPCRecompilerImlGen_FMUL(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			ppcImlGenContext->hasFPUInstruction = true;
			break;
		case 26:
			if (PPCRecompilerImlGen_FRSQRTE(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			ppcImlGenContext->hasFPUInstruction = true;
			break;
		case 28:
			if (PPCRecompilerImlGen_FMSUB(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			ppcImlGenContext->hasFPUInstruction = true;
			break;
		case 29:
			if (PPCRecompilerImlGen_FMADD(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			ppcImlGenContext->hasFPUInstruction = true;
			break;
		case 30:
			if (PPCRecompilerImlGen_FNMSUB(ppcImlGenContext, opcode) == false)
				unsupportedInstructionFound = true;
			ppcImlGenContext->hasFPUInstruction = true;
			break;
		default:
			switch (PPC_getBits(opcode, 30, 10))
			{
			case 40:
				if (PPCRecompilerImlGen_FNEG(ppcImlGenContext, opcode) == false)
					unsupportedInstructionFound = true;
				ppcImlGenContext->hasFPUInstruction = true;
				break;
			case 72:
				if (PPCRecompilerImlGen_FMR(ppcImlGenContext, opcode) == false)
					unsupportedInstructionFound = true;
				ppcImlGenContext->hasFPUInstruction = true;
				break;
			case 136:
				if (PPCRecompilerImlGen_FNABS(ppcImlGenContext, opcode) == false)
					unsupportedInstructionFound = true;
				ppcImlGenContext->hasFPUInstruction = true;
				break;
			case 264:
				if (PPCRecompilerImlGen_FABS(ppcImlGenContext, opcode) == false)
					unsupportedInstructionFound = true;
				ppcImlGenContext->hasFPUInstruction = true;
				break;
			default:
				unsupportedInstructionFound = true;
				break;
			}
			break;
		}
		break;
	default:
		unsupportedInstructionFound = true;
		break;
	}
	return unsupportedInstructionFound;
}

bool PPCRecompiler_CheckIfInstructionEndsSegment(PPCFunctionBoundaryTracker& boundaryTracker, uint32 instructionAddress, uint32 opcode, bool& makeNextInstEnterable, bool& continueDefaultPath, bool& hasBranchTarget, uint32& branchTarget)
{
	hasBranchTarget = false;
	branchTarget = 0xFFFFFFFF;
	makeNextInstEnterable = false;
	continueDefaultPath = false;
	switch (Espresso::GetPrimaryOpcode(opcode))
	{
	case Espresso::PrimaryOpcode::VIRTUAL_HLE:
	{
		makeNextInstEnterable = true;
		hasBranchTarget = false;
		continueDefaultPath = false;
		return true;
	}
	case Espresso::PrimaryOpcode::BC:
	{
		uint32 BD, BI;
		Espresso::BOField BO;
		bool AA, LK;
		Espresso::decodeOp_BC(opcode, BD, BO, BI, AA, LK);
		if (!LK)
		{
			hasBranchTarget = true;
			branchTarget = (AA ? BD : BD) + instructionAddress;
			if (!boundaryTracker.ContainsAddress(branchTarget))
				hasBranchTarget = false;
		}
		makeNextInstEnterable = LK;
		continueDefaultPath = true;
		return true;
	}
	case Espresso::PrimaryOpcode::B:
	{
		uint32 LI;
		bool AA, LK;
		Espresso::decodeOp_B(opcode, LI, AA, LK);
		if (!LK)
		{
			hasBranchTarget = true;
			branchTarget = AA ? LI : LI + instructionAddress;
			if (!boundaryTracker.ContainsAddress(branchTarget))
				hasBranchTarget = false;
		}
		makeNextInstEnterable = LK;
		continueDefaultPath = false;
		return true;
	}
	case Espresso::PrimaryOpcode::GROUP_19:
		switch (Espresso::GetGroup19Opcode(opcode))
		{
		case Espresso::Opcode19::BCLR:
		case Espresso::Opcode19::BCCTR:
		{
			Espresso::BOField BO;
			uint32 BI;
			bool LK;
			Espresso::decodeOp_BCSPR(opcode, BO, BI, LK);
			continueDefaultPath = !BO.conditionIgnore() || !BO.decrementerIgnore();
			makeNextInstEnterable = Espresso::DecodeLK(opcode);
			return true;
		}
		default:
			break;
		}
		break;
	case Espresso::PrimaryOpcode::GROUP_31:
		switch (Espresso::GetGroup31Opcode(opcode))
		{
		default:
			break;
		}
		break;
	default:
		break;
	}
	return false;
}

void PPCRecompiler_DetermineBasicBlockRange(std::vector<PPCBasicBlockInfo>& basicBlockList, PPCFunctionBoundaryTracker& boundaryTracker, uint32 ppcStart, uint32 ppcEnd, const std::set<uint32>& combinedBranchTargets, const std::set<uint32>& entryAddresses)
{
	cemu_assert_debug(ppcStart <= ppcEnd);

	uint32 currentAddr = ppcStart;

	PPCBasicBlockInfo* curBlockInfo = &basicBlockList.emplace_back(currentAddr, entryAddresses);

	uint32 basicBlockStart = currentAddr;
	while (currentAddr <= ppcEnd)
	{
		curBlockInfo->lastAddress = currentAddr;
		uint32 opcode = memory_readU32(currentAddr);
		bool nextInstIsEnterable = false;
		bool hasBranchTarget = false;
		bool hasContinuedFlow = false;
		uint32 branchTarget = 0;
		if (PPCRecompiler_CheckIfInstructionEndsSegment(boundaryTracker, currentAddr, opcode, nextInstIsEnterable, hasContinuedFlow, hasBranchTarget, branchTarget))
		{
			curBlockInfo->hasBranchTarget = hasBranchTarget;
			curBlockInfo->branchTarget = branchTarget;
			curBlockInfo->hasContinuedFlow = hasContinuedFlow;
			if (currentAddr >= ppcEnd)
				break;
			curBlockInfo = &basicBlockList.emplace_back(currentAddr + 4, entryAddresses);
			curBlockInfo->isEnterable = curBlockInfo->isEnterable || nextInstIsEnterable;
			currentAddr += 4;
			continue;
		}
		currentAddr += 4;
		if (currentAddr <= ppcEnd)
		{
			if (combinedBranchTargets.find(currentAddr) != combinedBranchTargets.end())
			{
				curBlockInfo = &basicBlockList.emplace_back(currentAddr, entryAddresses);
			}
		}

	}
}

std::vector<PPCBasicBlockInfo> PPCRecompiler_DetermineBasicBlockRange(PPCFunctionBoundaryTracker& boundaryTracker, const std::set<uint32>& entryAddresses)
{
	cemu_assert(!entryAddresses.empty());
	std::vector<PPCBasicBlockInfo> basicBlockList;

	const std::set<uint32> branchTargets = boundaryTracker.GetBranchTargets();
	auto funcRanges = boundaryTracker.GetRanges();

	std::set<uint32> combinedBranchTargets = branchTargets;
	combinedBranchTargets.insert(entryAddresses.begin(), entryAddresses.end());

	for (auto& funcRangeIt : funcRanges)
		PPCRecompiler_DetermineBasicBlockRange(basicBlockList, boundaryTracker, funcRangeIt.startAddress, funcRangeIt.startAddress + funcRangeIt.length - 4, combinedBranchTargets, entryAddresses);

	size_t numMarkedEnterable = 0;
	for (auto& basicBlockIt : basicBlockList)
	{
		if (entryAddresses.find(basicBlockIt.startAddress) != entryAddresses.end())
		{
			cemu_assert_debug(basicBlockIt.isEnterable);
			numMarkedEnterable++;
		}
	}
	cemu_assert_debug(numMarkedEnterable == entryAddresses.size());

	return basicBlockList;
}

bool PPCIMLGen_FillBasicBlock(ppcImlGenContext_t& ppcImlGenContext, PPCBasicBlockInfo& basicBlockInfo)
{
	ppcImlGenContext.currentOutputSegment = basicBlockInfo.GetSegmentForInstructionAppend();
	ppcImlGenContext.currentInstruction = (uint32*)(memory_base + basicBlockInfo.startAddress);

	uint32* firstCurrentInstruction = ppcImlGenContext.currentInstruction;
	uint32* endCurrentInstruction = (uint32*)(memory_base + basicBlockInfo.lastAddress);

	while (ppcImlGenContext.currentInstruction <= endCurrentInstruction)
	{
		uint32 addressOfCurrentInstruction = (uint32)((uint8*)ppcImlGenContext.currentInstruction - memory_base);
		ppcImlGenContext.ppcAddressOfCurrentInstruction = addressOfCurrentInstruction;

		if (PPCRecompiler_decodePPCInstruction(&ppcImlGenContext))
		{
			cemuLog_logDebug(LogType::Force, "PPCRecompiler: Unsupported instruction at 0x{:08x}", addressOfCurrentInstruction);
			ppcImlGenContext.currentOutputSegment = nullptr;
			return false;
		}
	}
	ppcImlGenContext.currentOutputSegment = nullptr;
	return true;
}

IMLSegment* PPCIMLGen_CreateSplitSegmentAtEnd(ppcImlGenContext_t& ppcImlGenContext, PPCBasicBlockInfo& basicBlockInfo)
{
	IMLSegment* writeSegment = basicBlockInfo.GetSegmentForInstructionAppend();

	IMLSegment* continuedSegment = ppcImlGenContext.InsertSegment(ppcImlGenContext.GetSegmentIndex(writeSegment) + 1);

	continuedSegment->SetLinkBranchTaken(writeSegment->GetBranchTaken());
	continuedSegment->SetLinkBranchNotTaken(writeSegment->GetBranchNotTaken());

	writeSegment->SetLinkBranchNotTaken(continuedSegment);
	writeSegment->SetLinkBranchTaken(nullptr);

	if (ppcImlGenContext.currentOutputSegment == writeSegment)
		ppcImlGenContext.currentOutputSegment = continuedSegment;

	cemu_assert_debug(basicBlockInfo.appendSegment == writeSegment);
	basicBlockInfo.appendSegment = continuedSegment;

	return writeSegment;
}

IMLSegment* PPCIMLGen_CreateNewSegmentAsBranchTarget(ppcImlGenContext_t& ppcImlGenContext, PPCBasicBlockInfo& basicBlockInfo)
{
	IMLSegment* writeSegment = basicBlockInfo.GetSegmentForInstructionAppend();
	IMLSegment* branchTargetSegment = ppcImlGenContext.NewSegment();
	cemu_assert_debug(!writeSegment->GetBranchTaken());
	writeSegment->SetLinkBranchTaken(branchTargetSegment);
	return branchTargetSegment;
}

void PPCIMLGen_AssertIfNotLastSegmentInstruction(ppcImlGenContext_t& ppcImlGenContext)
{
	cemu_assert_debug(ppcImlGenContext.currentBasicBlock->lastAddress == ppcImlGenContext.ppcAddressOfCurrentInstruction);
}

bool PPCRecompiler_IsBasicBlockATightFiniteLoop(IMLSegment* imlSegment, PPCBasicBlockInfo& basicBlockInfo)
{

	if (imlSegment->nextSegmentBranchTaken != imlSegment)
		return false;

	uint32 opcode = *(uint32be*)(memory_base + basicBlockInfo.lastAddress);
	if (Espresso::GetPrimaryOpcode(opcode) != Espresso::PrimaryOpcode::BC)
		return false;
	uint32 BO, BI, BD;
	PPC_OPC_TEMPL_B(opcode, BO, BI, BD);
	Espresso::BOField boField(BO);
	if(!boField.conditionIgnore() || boField.branchAlways())
		return false;
	if(boField.decrementerIgnore())
		return false;
	return true;
}

void PPCRecompiler_HandleCycleCheckCount(ppcImlGenContext_t& ppcImlGenContext, PPCBasicBlockInfo& basicBlockInfo)
{
	IMLSegment* imlSegment = basicBlockInfo.GetFirstSegmentInChain();
	if (!basicBlockInfo.hasBranchTarget)
		return;
	if (basicBlockInfo.branchTarget > basicBlockInfo.startAddress)
		return;

	if (PPCRecompiler_IsBasicBlockATightFiniteLoop(imlSegment, basicBlockInfo))
		return;

	basicBlockInfo.GetFirstSegmentInChain()->SetEnterable(basicBlockInfo.startAddress);

	IMLSegment* splitSeg = PPCIMLGen_CreateSplitSegmentAtEnd(ppcImlGenContext, basicBlockInfo);
	splitSeg->AppendInstruction()->make_cjump_cycle_check();

	IMLSegment* exitSegment = ppcImlGenContext.NewSegment();
	splitSeg->SetLinkBranchTaken(exitSegment);

	exitSegment->AppendInstruction()->make_macro(PPCREC_IML_MACRO_LEAVE, basicBlockInfo.startAddress, 0, 0, IMLREG_INVALID);

	cemu_assert_debug(splitSeg->nextSegmentBranchNotTaken);
	exitSegment->SetNextSegmentForOverwriteHints(splitSeg->nextSegmentBranchNotTaken);
}

void PPCRecompiler_SetSegmentsUncertainFlow(ppcImlGenContext_t& ppcImlGenContext)
{
	for (IMLSegment* segIt : ppcImlGenContext.segmentList2)
	{
		if (segIt->imlList.empty())
		{
			cemu_assert_debug(segIt->GetBranchNotTaken());
			continue;
		}
		IMLInstruction* imlInstruction = segIt->GetLastInstruction();
		if (imlInstruction->type == PPCREC_IML_TYPE_MACRO)
		{
			auto macroType = imlInstruction->operation;
			switch (macroType)
			{
				case PPCREC_IML_MACRO_B_TO_REG:
				case PPCREC_IML_MACRO_BL:
				case PPCREC_IML_MACRO_B_FAR:
				case PPCREC_IML_MACRO_HLE:
				case PPCREC_IML_MACRO_LEAVE:
					segIt->nextSegmentIsUncertain = true;
					break;
				case PPCREC_IML_MACRO_DEBUGBREAK:
				case PPCREC_IML_MACRO_COUNT_CYCLES:
					break;
				default:
				cemu_assert_unimplemented();
			}
		}
	}
}

bool PPCRecompiler_GenerateIML(ppcImlGenContext_t& ppcImlGenContext, PPCFunctionBoundaryTracker& boundaryTracker, std::set<uint32>& entryAddresses)
{
	std::vector<PPCBasicBlockInfo> basicBlockList = PPCRecompiler_DetermineBasicBlockRange(boundaryTracker, entryAddresses);

	std::unordered_map<uint32, PPCBasicBlockInfo*> addrToBB;
	ppcImlGenContext.segmentList2.resize(basicBlockList.size());
	for (size_t i = 0; i < basicBlockList.size(); i++)
	{
		PPCBasicBlockInfo& basicBlockInfo = basicBlockList[i];
		IMLSegment* seg = new IMLSegment();
		seg->ppcAddress = basicBlockInfo.startAddress;
		if(basicBlockInfo.isEnterable)
			seg->SetEnterable(basicBlockInfo.startAddress);
		ppcImlGenContext.segmentList2[i] = seg;
		cemu_assert_debug(addrToBB.find(basicBlockInfo.startAddress) == addrToBB.end());
		basicBlockInfo.SetInitialSegment(seg);
		addrToBB.emplace(basicBlockInfo.startAddress, &basicBlockInfo);
	}
	for (size_t i = 0; i < basicBlockList.size(); i++)
	{
		PPCBasicBlockInfo& bbInfo = basicBlockList[i];
		cemu_assert_debug(bbInfo.GetFirstSegmentInChain() == bbInfo.GetSegmentForInstructionAppend());
		IMLSegment* seg = ppcImlGenContext.segmentList2[i];
		if (bbInfo.hasBranchTarget)
		{
			PPCBasicBlockInfo* targetBB = addrToBB[bbInfo.branchTarget];
			cemu_assert_debug(targetBB);
			IMLSegment_SetLinkBranchTaken(seg, targetBB->GetFirstSegmentInChain());
		}
		if (bbInfo.hasContinuedFlow)
		{
			PPCBasicBlockInfo* targetBB = addrToBB[bbInfo.lastAddress + 4];
			if (!targetBB)
			{
				cemuLog_log(LogType::Recompiler, "Recompiler was unable to link segment [0x{:08x}-0x{:08x}] to 0x{:08x}", bbInfo.startAddress, bbInfo.lastAddress, bbInfo.lastAddress + 4);
				return false;
			}
			cemu_assert_debug(targetBB);
			IMLSegment_SetLinkBranchNotTaken(seg, targetBB->GetFirstSegmentInChain());
		}
	}

	for (size_t i = 0; i < basicBlockList.size(); i++)
	{
		PPCBasicBlockInfo& basicBlockInfo = basicBlockList[i];
		IMLSegment* seg = basicBlockInfo.GetSegmentForInstructionAppend();

		uint32 ppcInstructionCount = (basicBlockInfo.lastAddress - basicBlockInfo.startAddress + 4) / 4;
		cemu_assert_debug(ppcInstructionCount > 0);

		PPCRecompiler_pushBackIMLInstructions(seg, 0, 1);
		seg->imlList[0].type = PPCREC_IML_TYPE_MACRO;
		seg->imlList[0].operation = PPCREC_IML_MACRO_COUNT_CYCLES;
		seg->imlList[0].op_macro.param = ppcInstructionCount;
	}

	for (size_t i = 0; i < basicBlockList.size(); i++)
	{
		PPCBasicBlockInfo& basicBlockInfo = basicBlockList[i];
		PPCRecompiler_HandleCycleCheckCount(ppcImlGenContext, basicBlockInfo);
	}

	for (size_t i = 0; i < basicBlockList.size(); i++)
	{
		PPCBasicBlockInfo& basicBlockInfo = basicBlockList[i];
		ppcImlGenContext.currentBasicBlock = &basicBlockInfo;
		if (!PPCIMLGen_FillBasicBlock(ppcImlGenContext, basicBlockInfo))
			return false;
		ppcImlGenContext.currentBasicBlock = nullptr;
	}

	PPCRecompiler_SetSegmentsUncertainFlow(ppcImlGenContext);

#ifdef CEMU_DEBUG_ASSERT
	for (size_t segIndex = 0; segIndex < ppcImlGenContext.segmentList2.size(); segIndex++)
	{
		IMLSegment* seg = ppcImlGenContext.segmentList2[segIndex];
		IMLSegment* nextSeg = (segIndex+1) < ppcImlGenContext.segmentList2.size() ? ppcImlGenContext.segmentList2[segIndex + 1] : nullptr;

		if (seg->imlList.size() > 0)
		{
			for (size_t f = 0; f < seg->imlList.size() - 1; f++)
			{
				if (seg->imlList[f].IsSuffixInstruction())
				{
					debug_printf("---------------- SegmentDump (Suffix instruction at wrong pos in segment 0x%x):\n", (int)segIndex);
					IMLDebug_Dump(&ppcImlGenContext);
					DEBUG_BREAK;
				}
			}
		}
		if (seg->nextSegmentBranchTaken)
		{
			if (!seg->HasSuffixInstruction())
			{
				debug_printf("---------------- SegmentDump (NoSuffixInstruction in segment 0x%x):\n", (int)segIndex);
				IMLDebug_Dump(&ppcImlGenContext);
				DEBUG_BREAK;
			}
		}
		if (seg->nextSegmentBranchNotTaken)
		{
			cemu_assert_debug(seg->nextSegmentBranchNotTaken == nextSeg);
		}
		if (seg->imlList.size() > 0)
		{
			IMLInstruction* inst = seg->GetLastInstruction();
			if (inst->type == PPCREC_IML_TYPE_MACRO && inst->op_macro.param == PPCREC_IML_MACRO_B_FAR)
			{
				cemu_assert_debug(!seg->GetBranchTaken());
				cemu_assert_debug(!seg->GetBranchNotTaken());
			}
			if (inst->type == PPCREC_IML_TYPE_CJUMP_CYCLE_CHECK)
			{
				cemu_assert_debug(seg->GetBranchTaken());
				cemu_assert_debug(seg->GetBranchNotTaken());
			}
			if (inst->type == PPCREC_IML_TYPE_CONDITIONAL_JUMP)
			{
				if (!seg->GetBranchTaken() || !seg->GetBranchNotTaken())
				{
					debug_printf("---------------- SegmentDump (Missing branch for conditional jump in segment 0x%x):\n", (int)segIndex);
					IMLDebug_Dump(&ppcImlGenContext);
					cemu_assert_error();
				}
			}
		}
		segIndex++;
	}
#endif

	return true;
}

bool PPCRecompiler_generateIntermediateCode(ppcImlGenContext_t& ppcImlGenContext, PPCRecFunction_t* ppcRecFunc, std::set<uint32>& entryAddresses, PPCFunctionBoundaryTracker& boundaryTracker)
{
	ppcImlGenContext.boundaryTracker = &boundaryTracker;
	if (!PPCRecompiler_GenerateIML(ppcImlGenContext, boundaryTracker, entryAddresses))
		return false;

	ppcRecRange_t recRange;
	recRange.ppcAddress = ppcRecFunc->ppcAddress;
	recRange.ppcSize = ppcRecFunc->ppcSize;
	ppcRecFunc->list_ranges.push_back(recRange);

	return true;
}

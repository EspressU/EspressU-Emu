#include "Cafe/HW/Espresso/Interpreter/PPCInterpreterInternal.h"
#include "Cafe/HW/Espresso/Recompiler/IML/IML.h"
#include "Cafe/HW/Espresso/Recompiler/IML/IMLInstruction.h"

#include "../PPCRecompiler.h"
#include "../PPCRecompilerIml.h"
#include "../BackendX64/BackendX64.h"

#include "Common/FileStream.h"

#include <boost/container/static_vector.hpp>
#include <boost/container/small_vector.hpp>

IMLReg _FPRRegFromID(IMLRegID regId)
{
	return IMLReg(IMLRegFormat::F64, IMLRegFormat::F64, 0, regId);
}

void PPCRecompiler_optimizeDirectFloatCopiesScanForward(ppcImlGenContext_t* ppcImlGenContext, IMLSegment* imlSegment, sint32 imlIndexLoad, IMLReg fprReg)
{
	IMLRegID fprIndex = fprReg.GetRegID();

	IMLInstruction* imlInstructionLoad = imlSegment->imlList.data() + imlIndexLoad;
	if (imlInstructionLoad->op_storeLoad.flags2.notExpanded)
		return;
	boost::container::static_vector<sint32, 4> trackedMoves;
	IMLUsedRegisters registersUsed;
	sint32 scanRangeEnd = std::min<sint32>(imlIndexLoad + 25, imlSegment->imlList.size());
	bool foundMatch = false;
	sint32 lastStore = -1;
	for (sint32 i = imlIndexLoad + 1; i < scanRangeEnd; i++)
	{
		IMLInstruction* imlInstruction = imlSegment->imlList.data() + i;
		if (imlInstruction->IsSuffixInstruction())
			break;
		if ((imlInstruction->type == PPCREC_IML_TYPE_FPR_STORE && imlInstruction->op_storeLoad.mode == PPCREC_FPR_ST_MODE_SINGLE) ||
			(imlInstruction->type == PPCREC_IML_TYPE_FPR_STORE_INDEXED && imlInstruction->op_storeLoad.mode == PPCREC_FPR_ST_MODE_SINGLE))
		{
			if (imlInstruction->op_storeLoad.registerData.GetRegID() == fprIndex)
			{
				if (foundMatch == false)
				{
					imlInstructionLoad->op_storeLoad.flags2.notExpanded = true;
				}
				IMLInstruction* imlInstructionStore = imlInstruction;
				imlInstructionStore->op_storeLoad.flags2.notExpanded = true;

				foundMatch = true;
				lastStore = i + 1;

				continue;
			}
		}
		if (imlInstruction->type == PPCREC_IML_TYPE_FPR_R_R && imlInstruction->operation == PPCREC_IML_OP_FPR_ASSIGN && imlInstruction->op_fpr_r_r.regA.GetRegID() == fprIndex)
		{
			if (imlInstruction->op_fpr_r_r.regR.GetRegID() == fprIndex)
			{
				break;
			}
			if (trackedMoves.size() >= trackedMoves.capacity())
			{
				lastStore = i;
				break;
			}
			trackedMoves.push_back(i);
			continue;
		}
		imlInstruction->CheckRegisterUsage(&registersUsed);
		if (registersUsed.writtenGPR1.IsValidAndSameRegID(fprIndex) || registersUsed.writtenGPR2.IsValidAndSameRegID(fprIndex))
			break;
		if (registersUsed.readGPR1.IsValidAndSameRegID(fprIndex))
			break;
		if (registersUsed.readGPR2.IsValidAndSameRegID(fprIndex))
			break;
		if (registersUsed.readGPR3.IsValidAndSameRegID(fprIndex))
			break;
		if (registersUsed.readGPR4.IsValidAndSameRegID(fprIndex))
			break;
	}

	if (foundMatch)
	{
		sint32 positionBias = 0;
		for (auto& trackedMove : trackedMoves)
		{
			sint32 realPosition = trackedMove + positionBias;
			IMLInstruction* imlMoveInstruction = imlSegment->imlList.data() + realPosition;
			if (realPosition >= lastStore)
				break;
			else
				lastStore++;

			cemu_assert_debug(imlMoveInstruction->type == PPCREC_IML_TYPE_FPR_R_R && imlMoveInstruction->op_fpr_r_r.regA.GetRegID() == fprIndex);
			cemu_assert_debug(imlMoveInstruction->op_fpr_r_r.regA.GetRegFormat() == IMLRegFormat::F64);
			auto dstReg = imlMoveInstruction->op_fpr_r_r.regR;
			IMLInstruction* newExpand = PPCRecompiler_insertInstruction(imlSegment, realPosition+1);
			newExpand->make_fpr_r(PPCREC_IML_OP_FPR_EXPAND_F32_TO_F64, dstReg);
			positionBias++;
		}
		IMLInstruction* newExpand = PPCRecompiler_insertInstruction(imlSegment, lastStore);
		newExpand->make_fpr_r(PPCREC_IML_OP_FPR_EXPAND_F32_TO_F64, _FPRRegFromID(fprIndex));
	}
}

void IMLOptimizer_OptimizeDirectFloatCopies(ppcImlGenContext_t* ppcImlGenContext)
{
	for (IMLSegment* segIt : ppcImlGenContext->segmentList2)
	{
		for (sint32 i = 0; i < segIt->imlList.size(); i++)
		{
			IMLInstruction* imlInstruction = segIt->imlList.data() + i;
			if (imlInstruction->type == PPCREC_IML_TYPE_FPR_LOAD && imlInstruction->op_storeLoad.mode == PPCREC_FPR_LD_MODE_SINGLE)
			{
				PPCRecompiler_optimizeDirectFloatCopiesScanForward(ppcImlGenContext, segIt, i, imlInstruction->op_storeLoad.registerData);
			}
			else if (imlInstruction->type == PPCREC_IML_TYPE_FPR_LOAD_INDEXED && imlInstruction->op_storeLoad.mode == PPCREC_FPR_LD_MODE_SINGLE)
			{
				PPCRecompiler_optimizeDirectFloatCopiesScanForward(ppcImlGenContext, segIt, i, imlInstruction->op_storeLoad.registerData);
			}
		}
	}
}

void PPCRecompiler_optimizeDirectIntegerCopiesScanForward(ppcImlGenContext_t* ppcImlGenContext, IMLSegment* imlSegment, sint32 imlIndexLoad, IMLReg gprReg)
{
	cemu_assert_debug(gprReg.GetBaseFormat() == IMLRegFormat::I64);
	cemu_assert_debug(gprReg.GetRegFormat() == IMLRegFormat::I32);

	IMLRegID gprIndex = gprReg.GetRegID();
	IMLInstruction* imlInstructionLoad = imlSegment->imlList.data() + imlIndexLoad;
	if ( imlInstructionLoad->op_storeLoad.flags2.swapEndian == false )
		return;
	bool foundMatch = false;
	IMLUsedRegisters registersUsed;
	sint32 scanRangeEnd = std::min<sint32>(imlIndexLoad + 25, imlSegment->imlList.size());
	sint32 i = imlIndexLoad + 1;
	for (; i < scanRangeEnd; i++)
	{
		IMLInstruction* imlInstruction = imlSegment->imlList.data() + i;
		if (imlInstruction->IsSuffixInstruction())
			break;
		if ((imlInstruction->type == PPCREC_IML_TYPE_STORE && imlInstruction->op_storeLoad.copyWidth == 32 ) )
		{
			if (imlInstruction->op_storeLoad.registerMem.GetRegID() == gprIndex)
				break;
			if (imlInstruction->op_storeLoad.registerData.GetRegID() == gprIndex)
			{
				IMLInstruction* imlInstructionStore = imlInstruction;
				if (foundMatch == false)
				{
					imlInstructionLoad->op_storeLoad.flags2.swapEndian = !imlInstructionLoad->op_storeLoad.flags2.swapEndian;
					foundMatch = true;
				}
				imlInstructionStore->op_storeLoad.flags2.swapEndian = !imlInstructionStore->op_storeLoad.flags2.swapEndian;
				continue;
			}
		}
		imlInstruction->CheckRegisterUsage(&registersUsed);
		if (registersUsed.readGPR1.IsValidAndSameRegID(gprIndex) ||
			registersUsed.readGPR2.IsValidAndSameRegID(gprIndex) ||
			registersUsed.readGPR3.IsValidAndSameRegID(gprIndex))
		{
			break;
		}
		if (registersUsed.IsBaseGPRWritten(gprReg))
			return;
	}
	if (foundMatch)
	{
		PPCRecompiler_insertInstruction(imlSegment, i)->make_r_r(PPCREC_IML_OP_ENDIAN_SWAP, gprReg, gprReg);
	}
}

void IMLOptimizer_OptimizeDirectIntegerCopies(ppcImlGenContext_t* ppcImlGenContext)
{
	for (IMLSegment* segIt : ppcImlGenContext->segmentList2)
	{
		for (sint32 i = 0; i < segIt->imlList.size(); i++)
		{
			IMLInstruction* imlInstruction = segIt->imlList.data() + i;
			if (imlInstruction->type == PPCREC_IML_TYPE_LOAD && imlInstruction->op_storeLoad.copyWidth == 32 && imlInstruction->op_storeLoad.flags2.swapEndian )
			{
				PPCRecompiler_optimizeDirectIntegerCopiesScanForward(ppcImlGenContext, segIt, i, imlInstruction->op_storeLoad.registerData);
			}
		}
	}
}

IMLName PPCRecompilerImlGen_GetRegName(ppcImlGenContext_t* ppcImlGenContext, IMLReg reg);

sint32 _getGQRIndexFromRegister(ppcImlGenContext_t* ppcImlGenContext, IMLReg gqrReg)
{
	if (gqrReg.IsInvalid())
		return -1;
	sint32 namedReg = PPCRecompilerImlGen_GetRegName(ppcImlGenContext, gqrReg);
	if (namedReg >= (PPCREC_NAME_SPR0 + SPR_UGQR0) && namedReg <= (PPCREC_NAME_SPR0 + SPR_UGQR7))
	{
		return namedReg - (PPCREC_NAME_SPR0 + SPR_UGQR0);
	}
	else
	{
		cemu_assert_suspicious();
	}
	return -1;
}

bool PPCRecompiler_isUGQRValueKnown(ppcImlGenContext_t* ppcImlGenContext, sint32 gqrIndex, uint32& gqrValue)
{
	if (gqrIndex == 0)
		gqrValue = 0x00000000;
	else
		return false;
	return true;
}

class IMLOptimizerRegIOAnalysis
{
  public:
	IMLOptimizerRegIOAnalysis(std::span<IMLSegment*> segmentList, uint32 maxRegId) : m_segmentList(segmentList), m_maxRegId(maxRegId)
	{
		m_segRegisterInOutList.resize(segmentList.size());
	}

	struct IMLSegmentRegisterInOut
	{
		std::unordered_set<IMLRegID> regWritten;
		std::unordered_set<IMLRegID> regImported;
		std::unordered_set<IMLRegID> regForward;
	};

	void ComputeDepedencies()
	{
		std::vector<IMLSegmentRegisterInOut>& segRegisterInOutList = m_segRegisterInOutList;
		IMLSegmentRegisterInOut* segIO = segRegisterInOutList.data();
		uint32 index = 0;
		for(auto& seg : m_segmentList)
		{
			seg->momentaryIndex = index;
			index++;
			for(auto& instr : seg->imlList)
			{
				IMLUsedRegisters registerUsage;
				instr.CheckRegisterUsage(&registerUsage);
				registerUsage.ForEachReadGPR([&](IMLReg gprReg) {
					IMLRegID gprId = gprReg.GetRegID();
					if (!segIO->regWritten.contains(gprId))
					{
						segIO->regImported.insert(gprId);
					}
				});
				registerUsage.ForEachWrittenGPR([&](IMLReg gprReg) {
					IMLRegID gprId = gprReg.GetRegID();
					segIO->regWritten.insert(gprId);
				});
			}
			segIO++;
		}
		for(auto& seg : m_segmentList)
		{
			if (!seg->nextSegmentIsUncertain)
				continue;
			if(seg->deadCodeEliminationHintSeg)
				continue;
			IMLSegmentRegisterInOut& segIO = segRegisterInOutList[seg->momentaryIndex];
			for(uint32 i=0; i<=m_maxRegId; i++)
			{
				segIO.regImported.insert((IMLRegID)i);
			}
		}
		std::unordered_set<uint32> segIdsWhichNeedUpdate;
		for (uint32 i = 0; i < m_segmentList.size(); i++)
		{
			segIdsWhichNeedUpdate.insert(i);
		}
		while(!segIdsWhichNeedUpdate.empty())
		{
			auto firstIt = segIdsWhichNeedUpdate.begin();
			uint32 segId = *firstIt;
			segIdsWhichNeedUpdate.erase(firstIt);
			auto& curSeg = m_segmentList[segId];
			IMLSegmentRegisterInOut& curSegIO = segRegisterInOutList[segId];
			for(auto& prevSeg : curSeg->list_prevSegments)
			{
				IMLSegmentRegisterInOut& prevSegIO = segRegisterInOutList[prevSeg->momentaryIndex];
				bool prevSegChanged = false;
				for(auto& regId : curSegIO.regImported)
				{
					if (!prevSegIO.regWritten.contains(regId))
						prevSegChanged |= prevSegIO.regForward.insert(regId).second;
				}
				for(auto& regId : curSegIO.regForward)
				{
					if (!prevSegIO.regWritten.contains(regId))
						prevSegChanged |= prevSegIO.regForward.insert(regId).second;
				}
				if(prevSegChanged)
					segIdsWhichNeedUpdate.insert(prevSeg->momentaryIndex);
			}
			for(auto& prevSeg : curSeg->list_deadCodeHintBy)
			{
				IMLSegmentRegisterInOut& prevSegIO = segRegisterInOutList[prevSeg->momentaryIndex];
				bool prevSegChanged = false;
				for(auto& regId : curSegIO.regImported)
				{
					if (!prevSegIO.regWritten.contains(regId))
						prevSegChanged |= prevSegIO.regForward.insert(regId).second;
				}
				for(auto& regId : curSegIO.regForward)
				{
					if (!prevSegIO.regWritten.contains(regId))
						prevSegChanged |= prevSegIO.regForward.insert(regId).second;
				}
				if(prevSegChanged)
					segIdsWhichNeedUpdate.insert(prevSeg->momentaryIndex);
			}
		}
	}

	std::unordered_set<IMLRegID> GetRegistersNeededAtEndOfSegment(IMLSegment& seg)
	{
		std::unordered_set<IMLRegID> regsNeeded;
		if(seg.nextSegmentIsUncertain)
		{
			if(seg.deadCodeEliminationHintSeg)
			{
				auto& nextSegIO = m_segRegisterInOutList[seg.deadCodeEliminationHintSeg->momentaryIndex];
				regsNeeded.insert(nextSegIO.regImported.begin(), nextSegIO.regImported.end());
				regsNeeded.insert(nextSegIO.regForward.begin(), nextSegIO.regForward.end());
			}
			else
			{
				for(uint32 i = 0; i <= m_maxRegId; i++)
					regsNeeded.insert(i);
			}
			return regsNeeded;
		}
		if(seg.nextSegmentBranchTaken)
		{
			auto& nextSegIO = m_segRegisterInOutList[seg.nextSegmentBranchTaken->momentaryIndex];
			regsNeeded.insert(nextSegIO.regImported.begin(), nextSegIO.regImported.end());
			regsNeeded.insert(nextSegIO.regForward.begin(), nextSegIO.regForward.end());
		}
		if(seg.nextSegmentBranchNotTaken)
		{
			auto& nextSegIO = m_segRegisterInOutList[seg.nextSegmentBranchNotTaken->momentaryIndex];
			regsNeeded.insert(nextSegIO.regImported.begin(), nextSegIO.regImported.end());
			regsNeeded.insert(nextSegIO.regForward.begin(), nextSegIO.regForward.end());
		}
		return regsNeeded;
	}

	bool IsRegisterNeededAtEndOfSegment(IMLSegment& seg, IMLRegID regId)
	{
		if(seg.nextSegmentIsUncertain)
		{
			if(!seg.deadCodeEliminationHintSeg)
				return true;
			auto& nextSegIO = m_segRegisterInOutList[seg.deadCodeEliminationHintSeg->momentaryIndex];
			if(nextSegIO.regImported.contains(regId))
				return true;
			if(nextSegIO.regForward.contains(regId))
				return true;
			return false;
		}
		if(seg.nextSegmentBranchTaken)
		{
			auto& nextSegIO = m_segRegisterInOutList[seg.nextSegmentBranchTaken->momentaryIndex];
			if(nextSegIO.regImported.contains(regId))
				return true;
			if(nextSegIO.regForward.contains(regId))
				return true;
		}
		if(seg.nextSegmentBranchNotTaken)
		{
			auto& nextSegIO = m_segRegisterInOutList[seg.nextSegmentBranchNotTaken->momentaryIndex];
			if(nextSegIO.regImported.contains(regId))
				return true;
			if(nextSegIO.regForward.contains(regId))
				return true;
		}
		return false;
	}

  private:
	std::span<IMLSegment*> m_segmentList;
	uint32 m_maxRegId;

	std::vector<IMLSegmentRegisterInOut> m_segRegisterInOutList;

};

sint32 IMLUtil_FindInstructionWhichWritesRegister(IMLSegment& seg, sint32 startIndex, IMLReg reg, sint32 maxScanDistance = -1)
{
	sint32 endIndex = std::max<sint32>(startIndex - maxScanDistance, 0);
	for (sint32 i = startIndex; i >= endIndex; i--)
	{
		IMLInstruction& imlInstruction = seg.imlList[i];
		IMLUsedRegisters registersUsed;
		imlInstruction.CheckRegisterUsage(&registersUsed);
		if (registersUsed.IsBaseGPRWritten(reg))
			return i;
	}
	return -1;
}

bool IMLUtil_CanMoveInstructionTo(IMLSegment& seg, sint32 initialIndex, sint32 targetIndex)
{
	boost::container::static_vector<IMLRegID, 8> regsWritten;
	boost::container::static_vector<IMLRegID, 8> regsRead;
	IMLUsedRegisters registersUsed;
	seg.imlList[initialIndex].CheckRegisterUsage(&registersUsed);
	registersUsed.ForEachAccessedGPR([&](IMLReg reg, bool isWritten) {
		if (isWritten)
			regsWritten.push_back(reg.GetRegID());
		else
			regsRead.push_back(reg.GetRegID());
	});
	if(initialIndex < targetIndex)
	{
		sint32 scanStartIndex = initialIndex+1;
		sint32 scanEndIndex = targetIndex;
		for (sint32 i = scanStartIndex; i < scanEndIndex; i++)
		{
			IMLUsedRegisters registersUsed;
			seg.imlList[i].CheckRegisterUsage(&registersUsed);
			bool canMove = true;
			registersUsed.ForEachAccessedGPR([&](IMLReg reg, bool isWritten) {
				IMLRegID regId = reg.GetRegID();
				if (!isWritten)
					canMove = canMove && std::find(regsWritten.begin(), regsWritten.end(), regId) == regsWritten.end();
				else
					canMove = canMove && std::find(regsRead.begin(), regsRead.end(), regId) == regsRead.end();
			});
			if(!canMove)
				return false;
		}
	}
	else
	{
		cemu_assert_unimplemented();
		return false;
	}
	return true;
}

sint32 IMLUtil_CountRegisterReadsInRange(IMLSegment& seg, sint32 scanStartIndex, sint32 scanEndIndex, IMLRegID regId)
{
	cemu_assert_debug(scanStartIndex <= scanEndIndex);
	cemu_assert_debug(scanEndIndex < seg.imlList.size());
	sint32 count = 0;
	for (sint32 i = scanStartIndex; i <= scanEndIndex; i++)
	{
		IMLUsedRegisters registersUsed;
		seg.imlList[i].CheckRegisterUsage(&registersUsed);
		registersUsed.ForEachReadGPR([&](IMLReg reg) {
			if (reg.GetRegID() == regId)
				count++;
		});
	}
	return count;
}

sint32 IMLUtil_MoveInstructionTo(IMLSegment& seg, sint32 initialIndex, sint32 targetIndex)
{
	cemu_assert_debug(initialIndex != targetIndex);
	IMLInstruction temp = seg.imlList[initialIndex];
	if (initialIndex < targetIndex)
	{
		cemu_assert_debug(targetIndex > 0);
		targetIndex--;
		for(size_t i=initialIndex; i<targetIndex; i++)
			seg.imlList[i] = seg.imlList[i+1];
		seg.imlList[targetIndex] = temp;
		return targetIndex;
	}
	else
	{
		cemu_assert_unimplemented();
		std::copy(seg.imlList.begin() + targetIndex, seg.imlList.begin() + initialIndex, seg.imlList.begin() + targetIndex + 1);
		seg.imlList[targetIndex] = temp;
		return targetIndex;
	}
}

bool IMLOptimizerX86_ModifiesEFlags(IMLInstruction& inst)
{
	if(inst.type == PPCREC_IML_TYPE_NAME_R || inst.type == PPCREC_IML_TYPE_R_NAME)
		return false;
	if((inst.type == PPCREC_IML_TYPE_R_R || inst.type == PPCREC_IML_TYPE_R_S32) && inst.operation == PPCREC_IML_OP_ASSIGN)
		return false;
	return true;
}

void IMLOptimizer_DebugPrintSeg(ppcImlGenContext_t& ppcImlGenContext, IMLSegment& seg)
{
	printf("----------------\n");
	IMLDebug_DumpSegment(&ppcImlGenContext, &seg);
	fflush(stdout);
}

void IMLOptimizer_RemoveDeadCodeFromSegment(IMLOptimizerRegIOAnalysis& regIoAnalysis, IMLSegment& seg)
{

	std::unordered_set<IMLRegID> regsNeeded = regIoAnalysis.GetRegistersNeededAtEndOfSegment(seg);

	if(seg.HasSuffixInstruction())
	{
		IMLInstruction& imlInstruction = seg.imlList[seg.GetSuffixInstructionIndex()];
		IMLUsedRegisters registersUsed;
		imlInstruction.CheckRegisterUsage(&registersUsed);
		registersUsed.ForEachWrittenGPR([&](IMLReg reg) {
			regsNeeded.erase(reg.GetRegID());
		});
		registersUsed.ForEachReadGPR([&](IMLReg reg) {
			regsNeeded.insert(reg.GetRegID());
		});
	}
	for (sint32 i = seg.imlList.size() - (seg.HasSuffixInstruction() ? 2:1); i >= 0; i--)
	{
		IMLInstruction& imlInstruction = seg.imlList[i];
		IMLUsedRegisters registersUsed;
		imlInstruction.CheckRegisterUsage(&registersUsed);

		bool onlyWritesRedundantRegisters = true;
		registersUsed.ForEachWrittenGPR([&](IMLReg reg) {
			if (regsNeeded.contains(reg.GetRegID()))
				onlyWritesRedundantRegisters = false;
		});
		registersUsed.ForEachWrittenGPR([&](IMLReg reg) {
			regsNeeded.erase(reg.GetRegID());
		});
		registersUsed.ForEachReadGPR([&](IMLReg reg) {
			regsNeeded.insert(reg.GetRegID());
		});
		if(!imlInstruction.HasSideEffects() && onlyWritesRedundantRegisters)
		{
			imlInstruction.make_no_op();
		}
	}
}

void IMLOptimizerX86_SubstituteCJumpForEflagsJump(IMLOptimizerRegIOAnalysis& regIoAnalysis, IMLSegment& seg)
{

	if(!seg.HasSuffixInstruction())
		return;
	sint32 cjmpInstIndex = seg.GetSuffixInstructionIndex();
	if(cjmpInstIndex < 0)
		return;
	IMLInstruction& cjumpInstr = seg.imlList[cjmpInstIndex];
	if( cjumpInstr.type != PPCREC_IML_TYPE_CONDITIONAL_JUMP )
		return;
	IMLReg regCondBool = cjumpInstr.op_conditional_jump.registerBool;
	bool invertedCondition = !cjumpInstr.op_conditional_jump.mustBeTrue;
	sint32 cmpInstrIndex = IMLUtil_FindInstructionWhichWritesRegister(seg, cjmpInstIndex-1, regCondBool, 20);
	if(cmpInstrIndex < 0)
		return;
	IMLInstruction& condSetterInstr = seg.imlList[cmpInstrIndex];
	IMLCondition cond;
	if(condSetterInstr.type == PPCREC_IML_TYPE_COMPARE)
		cond = condSetterInstr.op_compare.cond;
	else if(condSetterInstr.type == PPCREC_IML_TYPE_COMPARE_S32)
		cond = condSetterInstr.op_compare_s32.cond;
	else
		return;
	sint32 indexEflagsSafeStart = -1;
	for(sint32 i = cjmpInstIndex-1; i > cmpInstrIndex; i--)
	{
		if(IMLOptimizerX86_ModifiesEFlags(seg.imlList[i]))
		{
			indexEflagsSafeStart = i+1;
			break;
		}
	}
	if(indexEflagsSafeStart >= 0)
	{
		cemu_assert(indexEflagsSafeStart > 0);
		bool canMove = IMLUtil_CanMoveInstructionTo(seg, cmpInstrIndex, indexEflagsSafeStart);
		if(!canMove)
		{
			return;
		}
		else
		{
			cmpInstrIndex = IMLUtil_MoveInstructionTo(seg, cmpInstrIndex, indexEflagsSafeStart);
		}
	}
	cjumpInstr.make_x86_eflags_jcc(cond, invertedCondition);

	if (IMLUtil_CountRegisterReadsInRange(seg, cmpInstrIndex, cjmpInstIndex, regCondBool.GetRegID()) > 0 || regIoAnalysis.IsRegisterNeededAtEndOfSegment(seg, regCondBool.GetRegID()))
		return;

	auto& cmpInstr = seg.imlList[cmpInstrIndex];
	cemu_assert_debug(cmpInstr.type == PPCREC_IML_TYPE_COMPARE || cmpInstr.type == PPCREC_IML_TYPE_COMPARE_S32);
	if(cmpInstr.type == PPCREC_IML_TYPE_COMPARE)
	{
		IMLReg regA = cmpInstr.op_compare.regA;
		IMLReg regB = cmpInstr.op_compare.regB;
		seg.imlList[cmpInstrIndex].make_r_r(PPCREC_IML_OP_X86_CMP, regA, regB);
	}
	else
	{
		IMLReg regA = cmpInstr.op_compare_s32.regA;
		sint32 val = cmpInstr.op_compare_s32.immS32;
		seg.imlList[cmpInstrIndex].make_r_s32(PPCREC_IML_OP_X86_CMP, regA, val);
	}

}

void IMLOptimizer_StandardOptimizationPassForSegment(IMLOptimizerRegIOAnalysis& regIoAnalysis, IMLSegment& seg)
{
	IMLOptimizer_RemoveDeadCodeFromSegment(regIoAnalysis, seg);

#ifdef ARCH_X86_64
	IMLOptimizerX86_SubstituteCJumpForEflagsJump(regIoAnalysis, seg);
#endif
}

void IMLOptimizer_StandardOptimizationPass(ppcImlGenContext_t& ppcImlGenContext)
{
	IMLOptimizerRegIOAnalysis regIoAnalysis(ppcImlGenContext.segmentList2, ppcImlGenContext.GetMaxRegId());
	regIoAnalysis.ComputeDepedencies();
	for (IMLSegment* segIt : ppcImlGenContext.segmentList2)
	{
		IMLOptimizer_StandardOptimizationPassForSegment(regIoAnalysis, *segIt);
	}
}

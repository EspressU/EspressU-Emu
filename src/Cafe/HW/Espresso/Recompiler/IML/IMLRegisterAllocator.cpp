#include "IML.h"

#include "../PPCRecompiler.h"
#include "../PPCRecompilerIml.h"
#include "IMLRegisterAllocator.h"
#include "IMLRegisterAllocatorRanges.h"

#include "../BackendX64/BackendX64.h"
#ifdef __aarch64__
#include "../BackendAArch64/BackendAArch64.h"
#endif

#include <boost/container/static_vector.hpp>
#include <boost/container/small_vector.hpp>

#include "Common/cpu_features.h"

#define DEBUG_RA_EXTRA_VALIDATION 0
#define DEBUG_RA_INSTRUCTION_GEN 0

struct IMLRARegAbstractLiveness
{
	IMLRARegAbstractLiveness(IMLRegFormat regBaseFormat, sint32 usageStart, sint32 usageEnd)
		: regBaseFormat(regBaseFormat), usageStart(usageStart), usageEnd(usageEnd) {};

	void TrackInstruction(sint32 index)
	{
		usageStart = std::min<sint32>(usageStart, index);
		usageEnd = std::max<sint32>(usageEnd, index + 1);
	}

	sint32 usageStart;
	sint32 usageEnd;
	bool isProcessed{false};
	IMLRegFormat regBaseFormat;
};

struct IMLRegisterAllocatorContext
{
	IMLRegisterAllocatorParameters* raParam;
	ppcImlGenContext_t* deprGenContext;

	std::unordered_map<IMLRegID, IMLRegFormat> regIdToBaseFormat;
	std::vector<std::unordered_map<IMLRegID, IMLRARegAbstractLiveness>> perSegmentAbstractRanges;

	inline std::unordered_map<IMLRegID, IMLRARegAbstractLiveness>& GetSegmentAbstractRangeMap(IMLSegment* imlSegment)
	{
		return perSegmentAbstractRanges[imlSegment->momentaryIndex];
	}

	inline IMLRegFormat GetBaseFormatByRegId(IMLRegID regId) const
	{
		auto it = regIdToBaseFormat.find(regId);
		cemu_assert_debug(it != regIdToBaseFormat.cend());
		return it->second;
	}
};

struct IMLFixedRegisters
{
	struct Entry
	{
		Entry(IMLReg reg, IMLPhysRegisterSet physRegSet)
			: reg(reg), physRegSet(physRegSet) {}

		IMLReg reg;
		IMLPhysRegisterSet physRegSet;
	};
	boost::container::small_vector<Entry, 4> listInput;
	boost::container::small_vector<Entry, 4> listOutput;
};

static void SetupCallingConvention(const IMLInstruction* instruction, IMLFixedRegisters& fixedRegs, const IMLPhysReg intParamToPhysReg[3], const IMLPhysReg floatParamToPhysReg[3], const IMLPhysReg intReturnPhysReg, const IMLPhysReg floatReturnPhysReg, IMLPhysRegisterSet volatileRegisters)
{
	sint32 numIntParams = 0, numFloatParams = 0;

	auto AddParameterMapping = [&](IMLReg reg) {
		if (!reg.IsValid())
			return;
		IMLRegFormat regFormat = reg.GetBaseFormat();
		bool isIntegerFormat = regFormat == IMLRegFormat::I64 || regFormat == IMLRegFormat::I32 || regFormat == IMLRegFormat::I16 || regFormat == IMLRegFormat::I8;
		if (isIntegerFormat)
		{
			IMLPhysRegisterSet ps;
			ps.SetAvailable(intParamToPhysReg[numIntParams]);
			fixedRegs.listInput.emplace_back(reg, ps);
			numIntParams++;
		}
		else if (reg.GetBaseFormat() == IMLRegFormat::F64)
		{
			IMLPhysRegisterSet ps;
			ps.SetAvailable(floatParamToPhysReg[numFloatParams]);
			fixedRegs.listInput.emplace_back(reg, ps);
			numFloatParams++;
		}
		else
		{
			cemu_assert_suspicious();
		}
	};
	AddParameterMapping(instruction->op_call_imm.regParam0);
	AddParameterMapping(instruction->op_call_imm.regParam1);
	AddParameterMapping(instruction->op_call_imm.regParam2);
	if (instruction->op_call_imm.regReturn.IsValid())
	{
		IMLRegFormat returnFormat = instruction->op_call_imm.regReturn.GetBaseFormat();
		bool isIntegerFormat = returnFormat == IMLRegFormat::I64 || returnFormat == IMLRegFormat::I32 || returnFormat == IMLRegFormat::I16 || returnFormat == IMLRegFormat::I8;
		IMLPhysRegisterSet ps;
		if (isIntegerFormat)
		{
			ps.SetAvailable(intReturnPhysReg);
			volatileRegisters.SetReserved(intReturnPhysReg);
		}
		else
		{
			ps.SetAvailable(floatReturnPhysReg);
			volatileRegisters.SetReserved(floatReturnPhysReg);
		}
		fixedRegs.listOutput.emplace_back(instruction->op_call_imm.regReturn, ps);
	}
	fixedRegs.listOutput.emplace_back(IMLREG_INVALID, volatileRegisters);
}

#if defined(__aarch64__)
static void GetInstructionFixedRegisters(IMLInstruction* instruction, IMLFixedRegisters& fixedRegs)
{
	fixedRegs.listInput.clear();
	fixedRegs.listOutput.clear();

	if (instruction->type == PPCREC_IML_TYPE_CALL_IMM)
	{
		const IMLPhysReg intParamToPhysReg[3] = {IMLArchAArch64::PHYSREG_GPR_BASE + 0, IMLArchAArch64::PHYSREG_GPR_BASE + 1, IMLArchAArch64::PHYSREG_GPR_BASE + 2};
		const IMLPhysReg floatParamToPhysReg[3] = {IMLArchAArch64::PHYSREG_FPR_BASE + 0, IMLArchAArch64::PHYSREG_FPR_BASE + 1, IMLArchAArch64::PHYSREG_FPR_BASE + 2};
		IMLPhysRegisterSet volatileRegs;
		for (int i = 0; i <= 17; i++)
			volatileRegs.SetAvailable(IMLArchAArch64::PHYSREG_GPR_BASE + i);
		for (int i = 0; i <= 7; i++)
			volatileRegs.SetAvailable(IMLArchAArch64::PHYSREG_FPR_BASE + i);
		for (int i = 16; i <= 31; i++)
			volatileRegs.SetAvailable(IMLArchAArch64::PHYSREG_FPR_BASE + i);
		SetupCallingConvention(instruction, fixedRegs, intParamToPhysReg, floatParamToPhysReg, IMLArchAArch64::PHYSREG_GPR_BASE + 0, IMLArchAArch64::PHYSREG_FPR_BASE + 0, volatileRegs);
	}
}
#else
static void GetInstructionFixedRegisters(IMLInstruction* instruction, IMLFixedRegisters& fixedRegs)
{
	fixedRegs.listInput.clear();
	fixedRegs.listOutput.clear();

	if (instruction->type == PPCREC_IML_TYPE_R_R_R)
	{
		if (instruction->operation == PPCREC_IML_OP_LEFT_SHIFT || instruction->operation == PPCREC_IML_OP_RIGHT_SHIFT_S || instruction->operation == PPCREC_IML_OP_RIGHT_SHIFT_U)
		{
			if(!g_CPUFeatures.x86.bmi2)
			{
				IMLPhysRegisterSet ps;
				ps.SetAvailable(IMLArchX86::PHYSREG_GPR_BASE + X86_REG_ECX);
				fixedRegs.listInput.emplace_back(instruction->op_r_r_r.regB, ps);
			}
		}
	}
	else if (instruction->type == PPCREC_IML_TYPE_ATOMIC_CMP_STORE)
	{
		IMLPhysRegisterSet ps;
		ps.SetAvailable(IMLArchX86::PHYSREG_GPR_BASE + X86_REG_EAX);
		fixedRegs.listInput.emplace_back(IMLREG_INVALID, ps);
		fixedRegs.listOutput.emplace_back(instruction->op_atomic_compare_store.regBoolOut, ps);
	}
	else if (instruction->type == PPCREC_IML_TYPE_CALL_IMM)
	{
		const IMLPhysReg intParamToPhysReg[3] = {IMLArchX86::PHYSREG_GPR_BASE + X86_REG_RCX, IMLArchX86::PHYSREG_GPR_BASE + X86_REG_RDX, IMLArchX86::PHYSREG_GPR_BASE + X86_REG_R8};
		const IMLPhysReg floatParamToPhysReg[3] = {IMLArchX86::PHYSREG_FPR_BASE + 0, IMLArchX86::PHYSREG_FPR_BASE + 1, IMLArchX86::PHYSREG_FPR_BASE + 2};
		IMLPhysRegisterSet volatileRegs;
		volatileRegs.SetAvailable(IMLArchX86::PHYSREG_GPR_BASE + X86_REG_RAX);
		volatileRegs.SetAvailable(IMLArchX86::PHYSREG_GPR_BASE + X86_REG_RCX);
		volatileRegs.SetAvailable(IMLArchX86::PHYSREG_GPR_BASE + X86_REG_RDX);
		volatileRegs.SetAvailable(IMLArchX86::PHYSREG_GPR_BASE + X86_REG_R8);
		volatileRegs.SetAvailable(IMLArchX86::PHYSREG_GPR_BASE + X86_REG_R9);
		volatileRegs.SetAvailable(IMLArchX86::PHYSREG_GPR_BASE + X86_REG_R10);
		volatileRegs.SetAvailable(IMLArchX86::PHYSREG_GPR_BASE + X86_REG_R11);
		for (int i = 0; i <= 5; i++)
			volatileRegs.SetAvailable(IMLArchX86::PHYSREG_FPR_BASE + i);
		SetupCallingConvention(instruction, fixedRegs, intParamToPhysReg, floatParamToPhysReg, IMLArchX86::PHYSREG_GPR_BASE + X86_REG_EAX, IMLArchX86::PHYSREG_FPR_BASE + 0, volatileRegs);
	}
}
#endif

uint32 IMLRA_GetNextIterationIndex()
{
	static thread_local uint32 recRACurrentIterationIndex = 0;
	recRACurrentIterationIndex++;
	return recRACurrentIterationIndex;
}

bool _detectLoop(IMLSegment* currentSegment, sint32 depth, uint32 iterationIndex, IMLSegment* imlSegmentLoopBase)
{
	if (currentSegment == imlSegmentLoopBase)
		return true;
	if (currentSegment->raInfo.lastIterationIndex == iterationIndex)
		return currentSegment->raInfo.isPartOfProcessedLoop;
	if (depth >= 9)
		return false;
	currentSegment->raInfo.lastIterationIndex = iterationIndex;
	currentSegment->raInfo.isPartOfProcessedLoop = false;

	if (currentSegment->nextSegmentIsUncertain)
		return false;
	if (currentSegment->nextSegmentBranchNotTaken)
	{
		if (currentSegment->nextSegmentBranchNotTaken->momentaryIndex > currentSegment->momentaryIndex)
		{
			currentSegment->raInfo.isPartOfProcessedLoop |= _detectLoop(currentSegment->nextSegmentBranchNotTaken, depth + 1, iterationIndex, imlSegmentLoopBase);
		}
	}
	if (currentSegment->nextSegmentBranchTaken)
	{
		if (currentSegment->nextSegmentBranchTaken->momentaryIndex > currentSegment->momentaryIndex)
		{
			currentSegment->raInfo.isPartOfProcessedLoop |= _detectLoop(currentSegment->nextSegmentBranchTaken, depth + 1, iterationIndex, imlSegmentLoopBase);
		}
	}
	if (currentSegment->raInfo.isPartOfProcessedLoop)
		currentSegment->loopDepth++;
	return currentSegment->raInfo.isPartOfProcessedLoop;
}

void IMLRA_DetectLoop(ppcImlGenContext_t* ppcImlGenContext, IMLSegment* imlSegmentLoopBase)
{
	uint32 iterationIndex = IMLRA_GetNextIterationIndex();
	imlSegmentLoopBase->raInfo.lastIterationIndex = iterationIndex;
	if (_detectLoop(imlSegmentLoopBase->nextSegmentBranchTaken, 0, iterationIndex, imlSegmentLoopBase))
	{
		imlSegmentLoopBase->loopDepth++;
	}
}

void IMLRA_IdentifyLoop(ppcImlGenContext_t* ppcImlGenContext, IMLSegment* imlSegment)
{
	if (imlSegment->nextSegmentIsUncertain)
		return;
	if (imlSegment->nextSegmentBranchTaken == imlSegment)
	{
		imlSegment->loopDepth++;
		return;
	}
	if (imlSegment->nextSegmentBranchTaken && imlSegment->nextSegmentBranchTaken->momentaryIndex < imlSegment->momentaryIndex)
	{
		IMLRA_DetectLoop(ppcImlGenContext, imlSegment);
	}
}

#define SUBRANGE_LIST_SIZE (128)

sint32 IMLRA_CountDistanceUntilNextUse(raLivenessRange* subrange, raInstructionEdge startPosition)
{
	for (sint32 i = 0; i < subrange->list_accessLocations.size(); i++)
	{
		if (subrange->list_accessLocations[i].pos >= startPosition)
		{
			auto& it = subrange->list_accessLocations[i];
			cemu_assert_debug(it.IsRead() != it.IsWrite());
			cemu_assert_debug(!startPosition.ConnectsToPreviousSegment() && !startPosition.ConnectsToNextSegment());
			return it.pos.GetRaw() - startPosition.GetRaw();
		}
	}
	cemu_assert_debug(subrange->imlSegment->imlList.size() < 10000);
	return 10001 * 2;
}

sint32 IMLRA_CountDistanceUntilFixedRegUsageInRange(IMLSegment* imlSegment, raLivenessRange* range, raInstructionEdge startPosition, sint32 physRegister, bool& hasFixedAccess)
{
	hasFixedAccess = false;
	cemu_assert_debug(startPosition.IsInstructionIndex());
	for (auto& fixedReqEntry : range->list_fixedRegRequirements)
	{
		if (fixedReqEntry.pos < startPosition)
			continue;
		if (fixedReqEntry.allowedReg.IsAvailable(physRegister))
		{
			hasFixedAccess = true;
			return fixedReqEntry.pos.GetRaw() - startPosition.GetRaw();
		}
	}
	cemu_assert_debug(range->interval.end.IsInstructionIndex());
	return range->interval.end.GetRaw() - startPosition.GetRaw();
}

sint32 IMLRA_CountDistanceUntilFixedRegUsage(IMLSegment* imlSegment, raInstructionEdge startPosition, sint32 maxDistance, IMLRegID ourRegId, sint32 physRegister)
{
	cemu_assert_debug(startPosition.IsInstructionIndex());
	raInstructionEdge lastPos2;
	lastPos2.Set(imlSegment->imlList.size(), false);

	raInstructionEdge endPos;
	endPos = startPosition + maxDistance;
	if (endPos > lastPos2)
		endPos = lastPos2;
	IMLFixedRegisters fixedRegs;
	if (startPosition.IsOnOutputEdge())
		GetInstructionFixedRegisters(imlSegment->imlList.data() + startPosition.GetInstructionIndex(), fixedRegs);
	for (raInstructionEdge currentPos = startPosition; currentPos <= endPos; ++currentPos)
	{
		if (currentPos.IsOnInputEdge())
		{
			GetInstructionFixedRegisters(imlSegment->imlList.data() + currentPos.GetInstructionIndex(), fixedRegs);
		}
		auto& fixedRegAccess = currentPos.IsOnInputEdge() ? fixedRegs.listInput : fixedRegs.listOutput;
		for (auto& fixedRegLoc : fixedRegAccess)
		{
			if (fixedRegLoc.reg.IsInvalid() || fixedRegLoc.reg.GetRegID() != ourRegId)
			{
				cemu_assert_debug(fixedRegLoc.reg.IsInvalid() || fixedRegLoc.physRegSet.HasExactlyOneAvailable());
				if (fixedRegLoc.physRegSet.IsAvailable(physRegister))
					return currentPos.GetRaw() - startPosition.GetRaw();
			}
		}
	}
	return endPos.GetRaw() - startPosition.GetRaw();
}

sint32 PPCRecRA_countDistanceUntilNextLocalPhysRegisterUse(IMLSegment* imlSegment, raInstructionEdge startPosition, sint32 physRegister)
{
	cemu_assert_debug(startPosition.IsInstructionIndex());
	sint32 minDistance = (sint32)imlSegment->imlList.size() * 2 - startPosition.GetRaw();
	raLivenessRange* subrangeItr = imlSegment->raInfo.linkedList_allSubranges;
	while (subrangeItr)
	{
		if (subrangeItr->GetPhysicalRegister() != physRegister)
		{
			subrangeItr = subrangeItr->link_allSegmentRanges.next;
			continue;
		}
		if (subrangeItr->interval.ContainsEdge(startPosition))
			return 0;
		if (subrangeItr->interval.end < startPosition)
		{
			subrangeItr = subrangeItr->link_allSegmentRanges.next;
			continue;
		}
		cemu_assert_debug(startPosition <= subrangeItr->interval.start);
		sint32 currentDist = subrangeItr->interval.start.GetRaw() - startPosition.GetRaw();
		minDistance = std::min(minDistance, currentDist);
		subrangeItr = subrangeItr->link_allSegmentRanges.next;
	}
	return minDistance;
}

struct IMLRALivenessTimeline
{
	IMLRALivenessTimeline()
	{
	}

	void AddActiveRange(raLivenessRange* subrange)
	{
		activeRanges.emplace_back(subrange);
	}

	void ExpireRanges(raInstructionEdge expireUpTo)
	{
		expiredRanges.clear();
		size_t count = activeRanges.size();
		for (size_t f = 0; f < count; f++)
		{
			raLivenessRange* liverange = activeRanges[f];
			if (liverange->interval.end < expireUpTo)
			{
#ifdef CEMU_DEBUG_ASSERT
				if (!expireUpTo.ConnectsToNextSegment() && (liverange->subrangeBranchTaken || liverange->subrangeBranchNotTaken))
					assert_dbg();
#endif
				expiredRanges.emplace_back(liverange);
				activeRanges[f] = activeRanges[count - 1];
				f--;
				count--;
			}
		}
		if (count != activeRanges.size())
			activeRanges.resize(count);
	}

	std::span<raLivenessRange*> GetExpiredRanges()
	{
		return {expiredRanges.data(), expiredRanges.size()};
	}

	std::span<raLivenessRange*> GetActiveRanges()
	{
		return {activeRanges.data(), activeRanges.size()};
	}

	raLivenessRange* GetActiveRangeByVirtualRegId(IMLRegID regId)
	{
		for (auto& it : activeRanges)
			if (it->virtualRegister == regId)
				return it;
		return nullptr;
	}

	raLivenessRange* GetActiveRangeByPhysicalReg(sint32 physReg)
	{
		cemu_assert_debug(physReg >= 0);
		for (auto& it : activeRanges)
			if (it->physicalRegister == physReg)
				return it;
		return nullptr;
	}

	boost::container::small_vector<raLivenessRange*, 64> activeRanges;

  private:
	boost::container::small_vector<raLivenessRange*, 16> expiredRanges;
};

void PPCRecRA_MaskOverlappingPhysRegForGlobalRange(raLivenessRange* range2, IMLPhysRegisterSet& physRegSet)
{
	auto clusterRanges = range2->GetAllSubrangesInCluster();
	for (auto& subrange : clusterRanges)
	{
		IMLSegment* imlSegment = subrange->imlSegment;
		raLivenessRange* subrangeItr = imlSegment->raInfo.linkedList_allSubranges;
		while (subrangeItr)
		{
			if (subrange == subrangeItr)
			{
				subrangeItr = subrangeItr->link_allSegmentRanges.next;
				continue;
			}
			if (subrange->interval.IsOverlapping(subrangeItr->interval))
			{
				if (subrangeItr->GetPhysicalRegister() >= 0)
					physRegSet.SetReserved(subrangeItr->GetPhysicalRegister());
			}
			subrangeItr = subrangeItr->link_allSegmentRanges.next;
		}
	}
}

bool _livenessRangeStartCompare(raLivenessRange* lhs, raLivenessRange* rhs)
{
	return lhs->interval.start < rhs->interval.start;
}

void _sortSegmentAllSubrangesLinkedList(IMLSegment* imlSegment)
{
	raLivenessRange* subrangeList[4096 + 1];
	sint32 count = 0;
	raLivenessRange* subrangeItr = imlSegment->raInfo.linkedList_allSubranges;
	while (subrangeItr)
	{
		cemu_assert(count < 4096);
		subrangeList[count] = subrangeItr;
		count++;
		subrangeItr = subrangeItr->link_allSegmentRanges.next;
	}
	if (count == 0)
	{
		imlSegment->raInfo.linkedList_allSubranges = nullptr;
		return;
	}
	std::sort(subrangeList, subrangeList + count, _livenessRangeStartCompare);
	subrangeList[count] = nullptr;
	imlSegment->raInfo.linkedList_allSubranges = subrangeList[0];
	subrangeList[0]->link_allSegmentRanges.prev = nullptr;
	subrangeList[0]->link_allSegmentRanges.next = subrangeList[1];
	for (sint32 i = 1; i < count; i++)
	{
		subrangeList[i]->link_allSegmentRanges.prev = subrangeList[i - 1];
		subrangeList[i]->link_allSegmentRanges.next = subrangeList[i + 1];
	}
#if DEBUG_RA_EXTRA_VALIDATION
	sint32 count2 = 0;
	subrangeItr = imlSegment->raInfo.linkedList_allSubranges;
	raInstructionEdge currentStartPosition;
	currentStartPosition.SetRaw(RA_INTER_RANGE_START);
	while (subrangeItr)
	{
		count2++;
		if (subrangeItr->interval2.start < currentStartPosition)
			assert_dbg();
		currentStartPosition = subrangeItr->interval2.start;
		subrangeItr = subrangeItr->link_allSegmentRanges.next;
	}
	if (count != count2)
		assert_dbg();
#endif
}

std::unordered_map<IMLRegID, raLivenessRange*>& IMLRA_GetSubrangeMap(IMLSegment* imlSegment)
{
	return imlSegment->raInfo.linkedList_perVirtualRegister;
}

raLivenessRange* IMLRA_GetSubrange(IMLSegment* imlSegment, IMLRegID regId)
{
	auto it = imlSegment->raInfo.linkedList_perVirtualRegister.find(regId);
	if (it == imlSegment->raInfo.linkedList_perVirtualRegister.end())
		return nullptr;
	return it->second;
}

struct raFixedRegRequirementWithVGPR
{
	raFixedRegRequirementWithVGPR(raInstructionEdge pos, IMLPhysRegisterSet allowedReg, IMLRegID regId)
		: pos(pos), allowedReg(allowedReg), regId(regId) {}

	raInstructionEdge pos;
	IMLPhysRegisterSet allowedReg;
	IMLRegID regId;
};

std::vector<raFixedRegRequirementWithVGPR> IMLRA_BuildSegmentInstructionFixedRegList(IMLSegment* imlSegment)
{
	std::vector<raFixedRegRequirementWithVGPR> frrList;
	size_t index = 0;
	while (index < imlSegment->imlList.size())
	{
		IMLFixedRegisters fixedRegs;
		GetInstructionFixedRegisters(&imlSegment->imlList[index], fixedRegs);
		raInstructionEdge pos;
		pos.Set(index, true);
		for (auto& fixedRegAccess : fixedRegs.listInput)
		{
			frrList.emplace_back(pos, fixedRegAccess.physRegSet, fixedRegAccess.reg.IsValid() ? fixedRegAccess.reg.GetRegID() : IMLRegID_INVALID);
		}
		pos = pos + 1;
		for (auto& fixedRegAccess : fixedRegs.listOutput)
		{
			frrList.emplace_back(pos, fixedRegAccess.physRegSet, fixedRegAccess.reg.IsValid() ? fixedRegAccess.reg.GetRegID() : IMLRegID_INVALID);
		}
		index++;
	}
	return frrList;
}

boost::container::small_vector<raLivenessRange*, 8> IMLRA_GetRangeWithFixedRegReservationOverlappingPos(IMLSegment* imlSegment, raInstructionEdge pos, IMLPhysReg physReg)
{
	boost::container::small_vector<raLivenessRange*, 8> rangeList;
	for (raLivenessRange* currentRange = imlSegment->raInfo.linkedList_allSubranges; currentRange; currentRange = currentRange->link_allSegmentRanges.next)
	{
		if (!currentRange->interval.ContainsEdge(pos))
			continue;
		IMLPhysRegisterSet allowedRegs;
		if (!currentRange->GetAllowedRegistersEx(allowedRegs))
			continue;
		if (allowedRegs.IsAvailable(physReg))
			rangeList.emplace_back(currentRange);
	}
	return rangeList;
}

void IMLRA_HandleFixedRegisters(ppcImlGenContext_t* ppcImlGenContext, IMLSegment* imlSegment)
{
	for (raLivenessRange* currentRange = imlSegment->raInfo.linkedList_allSubranges; currentRange;)
	{
		IMLPhysRegisterSet allowedRegs;
		if(currentRange->list_fixedRegRequirements.empty())
		{
			currentRange = currentRange->link_allSegmentRanges.next;
			continue;
		}
		if (!currentRange->GetAllowedRegistersEx(allowedRegs))
		{
			currentRange = currentRange->link_allSegmentRanges.next;
			continue;
		}
		if (currentRange->interval.ExtendsPreviousSegment() || currentRange->interval.ExtendsIntoNextSegment())
		{
			raLivenessRange* nextRange = currentRange->link_allSegmentRanges.next;
			IMLRA_ExplodeRangeCluster(ppcImlGenContext, currentRange);
			currentRange = nextRange;
			continue;
		}
		currentRange = currentRange->link_allSegmentRanges.next;
	}
	for (raLivenessRange* currentRange = imlSegment->raInfo.linkedList_allSubranges; currentRange; currentRange = currentRange->link_allSegmentRanges.next)
	{
		IMLPhysRegisterSet allowedRegs;
		if (currentRange->list_fixedRegRequirements.empty())
			continue;
		if (!currentRange->GetAllowedRegistersEx(allowedRegs))
			continue;
		if (allowedRegs.HasAnyAvailable())
			continue;
		cemu_assert_unimplemented();
	}
	std::vector<raFixedRegRequirementWithVGPR> frr = IMLRA_BuildSegmentInstructionFixedRegList(imlSegment);
	std::unordered_map<IMLPhysReg, IMLRegID> lastVGPR;
	for (size_t i = 0; i < frr.size(); i++)
	{
		raFixedRegRequirementWithVGPR& entry = frr[i];
		cemu_assert_debug(entry.regId == IMLRegID_INVALID || entry.allowedReg.HasExactlyOneAvailable());
		for (IMLPhysReg physReg = entry.allowedReg.GetFirstAvailableReg(); physReg >= 0; physReg = entry.allowedReg.GetNextAvailableReg(physReg + 1))
		{
			bool vgprHasChanged = false;
			auto it = lastVGPR.find(physReg);
			if (it != lastVGPR.end())
				vgprHasChanged = it->second != entry.regId;
			else
				vgprHasChanged = true;
			lastVGPR[physReg] = entry.regId;

			if (!vgprHasChanged)
				continue;

			boost::container::small_vector<raLivenessRange*, 8> overlappingRanges = IMLRA_GetRangeWithFixedRegReservationOverlappingPos(imlSegment, entry.pos, physReg);
			if (entry.regId != IMLRegID_INVALID)
				cemu_assert_debug(!overlappingRanges.empty());

			for (auto& range : overlappingRanges)
			{
				if (range->interval.start < entry.pos)
				{
					IMLRA_SplitRange(ppcImlGenContext, range, entry.pos, true);
				}
			}
		}
	}
	for (raLivenessRange* currentRange = imlSegment->raInfo.linkedList_allSubranges; currentRange; currentRange = currentRange->link_allSegmentRanges.next)
	{
		IMLPhysRegisterSet allowedRegs;
		if (currentRange->list_fixedRegRequirements.empty())
			continue;
		if (!currentRange->GetAllowedRegistersEx(allowedRegs))
		{
			cemu_assert_debug(currentRange->list_fixedRegRequirements.empty());
			continue;
		}
		cemu_assert_debug(allowedRegs.HasExactlyOneAvailable());
		currentRange->SetPhysicalRegister(allowedRegs.GetFirstAvailableReg());
	}
#if DEBUG_RA_EXTRA_VALIDATION
	for (raLivenessRange* currentRange = imlSegment->raInfo.linkedList_allSubranges; currentRange; currentRange = currentRange->link_allSegmentRanges.next)
	{
		IMLPhysRegisterSet allowedRegs;
		if (!currentRange->HasPhysicalRegister())
			continue;
		for (raLivenessRange* currentRange2 = imlSegment->raInfo.linkedList_allSubranges; currentRange2; currentRange2 = currentRange2->link_allSegmentRanges.next)
		{
			if (currentRange == currentRange2)
				continue;
			if (currentRange->interval2.IsOverlapping(currentRange2->interval2))
			{
				cemu_assert_debug(currentRange->GetPhysicalRegister() != currentRange2->GetPhysicalRegister());
			}
		}
	}
#endif
}

void IMLRA_MakeSafeSplitPosition(IMLSegment* imlSegment, raInstructionEdge& pos)
{
	cemu_assert_debug(pos.IsInstructionIndex());
	if (pos.IsOnOutputEdge())
		pos = pos - 1;
}

void IMLRA_MakeSafeSplitDistance(IMLSegment* imlSegment, raInstructionEdge startPos, sint32& distance)
{
	cemu_assert_debug(startPos.IsInstructionIndex());
	cemu_assert_debug(distance >= 0);
	raInstructionEdge endPos = startPos + distance;
	IMLRA_MakeSafeSplitPosition(imlSegment, endPos);
	if (endPos < startPos)
	{
		distance = 0;
		return;
	}
	distance = endPos.GetRaw() - startPos.GetRaw();
}

static void DbgVerifyAllRanges(IMLRegisterAllocatorContext& ctx);

class RASpillStrategy
{
  public:
	virtual void Apply(ppcImlGenContext_t* ctx, IMLSegment* imlSegment, raLivenessRange* currentRange) = 0;

	sint32 GetCost()
	{
		return strategyCost;
	}

  protected:
	void ResetCost()
	{
		strategyCost = INT_MAX;
	}

	sint32 strategyCost;
};

class RASpillStrategy_LocalRangeHoleCutting : public RASpillStrategy
{
  public:
	void Reset()
	{
		localRangeHoleCutting.distance = -1;
		localRangeHoleCutting.largestHoleSubrange = nullptr;
		ResetCost();
	}

	void Evaluate(IMLSegment* imlSegment, raLivenessRange* currentRange, const IMLRALivenessTimeline& timeline, const IMLPhysRegisterSet& allowedRegs)
	{
		raInstructionEdge currentRangeStart = currentRange->interval.start;
		sint32 requiredSize2 = currentRange->interval.GetPreciseDistance();
		cemu_assert_debug(localRangeHoleCutting.distance == -1);
		cemu_assert_debug(strategyCost == INT_MAX);
		if (!currentRangeStart.ConnectsToPreviousSegment())
		{
			cemu_assert_debug(currentRangeStart.GetRaw() >= 0);
			for (auto candidate : timeline.activeRanges)
			{
				if (candidate->interval.ExtendsIntoNextSegment())
					continue;
				if (candidate == currentRange)
					continue;
				if (candidate->GetPhysicalRegister() < 0)
					continue;
				if (!allowedRegs.IsAvailable(candidate->GetPhysicalRegister()))
					continue;

				sint32 distance2 = IMLRA_CountDistanceUntilNextUse(candidate, currentRangeStart);
				IMLRA_MakeSafeSplitDistance(imlSegment, currentRangeStart, distance2);
				if (distance2 < 2)
					continue;
				cemu_assert_debug(currentRangeStart.IsInstructionIndex());
				distance2 = std::min<sint32>(distance2, imlSegment->imlList.size() * 2 - currentRangeStart.GetRaw());
				sint32 cost = IMLRA_CalculateAdditionalCostAfterSplit(candidate, currentRangeStart + distance2);
				if (distance2 < requiredSize2)
				{
					cost += IMLRA_CalculateAdditionalCostAfterSplit(currentRange, currentRangeStart + distance2);
					cost += (requiredSize2 - distance2) / 10;
				}
				if (cost < strategyCost)
				{
					strategyCost = cost;
					localRangeHoleCutting.distance = distance2;
					localRangeHoleCutting.largestHoleSubrange = candidate;
				}
			}
		}
	}

	void Apply(ppcImlGenContext_t* ctx, IMLSegment* imlSegment, raLivenessRange* currentRange) override
	{
		cemu_assert_debug(strategyCost != INT_MAX);
		sint32 requiredSize2 = currentRange->interval.GetPreciseDistance();
		raInstructionEdge currentRangeStart = currentRange->interval.start;

		raInstructionEdge holeStartPosition = currentRangeStart;
		raInstructionEdge holeEndPosition = currentRangeStart + localRangeHoleCutting.distance;
		raLivenessRange* collisionRange = localRangeHoleCutting.largestHoleSubrange;

		if (collisionRange->interval.start < holeStartPosition)
		{
			collisionRange = IMLRA_SplitRange(nullptr, collisionRange, holeStartPosition, true);
			cemu_assert_debug(!collisionRange || collisionRange->interval.start >= holeStartPosition);
			cemu_assert_debug(!collisionRange || collisionRange->interval.start >= holeEndPosition);
		}
		else
		{
			cemu_assert_unimplemented();
		}
		if (requiredSize2 > localRangeHoleCutting.distance)
		{
			raLivenessRange* tailRange = IMLRA_SplitRange(nullptr, currentRange, currentRangeStart + localRangeHoleCutting.distance, true);
			if (tailRange)
			{
				cemu_assert_debug(tailRange->list_fixedRegRequirements.empty());
				tailRange->UnsetPhysicalRegister();
			}
		}
		if (collisionRange)
		{
			cemu_assert_debug(!collisionRange->interval.IsOverlapping(currentRange->interval));
		}
	}

  private:
	struct
	{
		sint32 distance;
		raLivenessRange* largestHoleSubrange;
	} localRangeHoleCutting;
};

class RASpillStrategy_AvailableRegisterHole : public RASpillStrategy
{
  public:
	void Reset()
	{
		ResetCost();
		availableRegisterHole.distance = -1;
		availableRegisterHole.physRegister = -1;
	}

	void Evaluate(IMLSegment* imlSegment, raLivenessRange* currentRange, const IMLRALivenessTimeline& timeline, const IMLPhysRegisterSet& localAvailableRegsMask, const IMLPhysRegisterSet& allowedRegs)
	{
		sint32 requiredSize2 = currentRange->interval.GetPreciseDistance();

		raInstructionEdge currentRangeStart = currentRange->interval.start;
		cemu_assert_debug(strategyCost == INT_MAX);
		availableRegisterHole.distance = -1;
		availableRegisterHole.physRegister = -1;
		if (currentRangeStart.GetRaw() >= 0)
		{
			if (localAvailableRegsMask.HasAnyAvailable())
			{
				sint32 physRegItr = -1;
				while (true)
				{
					physRegItr = localAvailableRegsMask.GetNextAvailableReg(physRegItr + 1);
					if (physRegItr < 0)
						break;
					if (!allowedRegs.IsAvailable(physRegItr))
						continue;
					sint32 distance = PPCRecRA_countDistanceUntilNextLocalPhysRegisterUse(imlSegment, currentRangeStart, physRegItr);

					sint32 distUntilFixedReg = IMLRA_CountDistanceUntilFixedRegUsage(imlSegment, currentRangeStart, distance, currentRange->GetVirtualRegister(), physRegItr);
					if (distUntilFixedReg < distance)
						distance = distUntilFixedReg;

					IMLRA_MakeSafeSplitDistance(imlSegment, currentRangeStart, distance);
					if (distance < 2)
						continue;
					cemu_assert_debug(distance < requiredSize2);
					sint32 cost = IMLRA_CalculateAdditionalCostAfterSplit(currentRange, currentRangeStart + distance);
					cost += ((requiredSize2 - distance) / 2) / 10;
					if (cost < strategyCost)
					{
						strategyCost = cost;
						availableRegisterHole.distance = distance;
						availableRegisterHole.physRegister = physRegItr;
					}
				}
			}
		}
	}

	void Apply(ppcImlGenContext_t* ctx, IMLSegment* imlSegment, raLivenessRange* currentRange) override
	{
		cemu_assert_debug(strategyCost != INT_MAX);
		raInstructionEdge currentRangeStart = currentRange->interval.start;
		raLivenessRange* tailRange = IMLRA_SplitRange(nullptr, currentRange, currentRangeStart + availableRegisterHole.distance, true);
		if (tailRange)
		{
			cemu_assert_debug(tailRange->list_fixedRegRequirements.empty());
			tailRange->UnsetPhysicalRegister();
		}
	}

  private:
	struct
	{
		sint32 physRegister;
		sint32 distance;
	} availableRegisterHole;
};

class RASpillStrategy_ExplodeRange : public RASpillStrategy
{
  public:
	void Reset()
	{
		ResetCost();
		explodeRange.range = nullptr;
		explodeRange.distance = -1;
	}

	void Evaluate(IMLSegment* imlSegment, raLivenessRange* currentRange, const IMLRALivenessTimeline& timeline, const IMLPhysRegisterSet& allowedRegs)
	{
		raInstructionEdge currentRangeStart = currentRange->interval.start;
		if (currentRangeStart.ConnectsToPreviousSegment())
			currentRangeStart.Set(0, true);
		sint32 requiredSize2 = currentRange->interval.GetPreciseDistance();
		cemu_assert_debug(strategyCost == INT_MAX);
		explodeRange.range = nullptr;
		explodeRange.distance = -1;
		for (auto candidate : timeline.activeRanges)
		{
			if (!candidate->interval.ExtendsIntoNextSegment())
				continue;
			if (candidate == currentRange)
				continue;
			if (candidate->GetPhysicalRegister() < 0)
				continue;
			if (!allowedRegs.IsAvailable(candidate->GetPhysicalRegister()))
				continue;

			sint32 distance = IMLRA_CountDistanceUntilNextUse(candidate, currentRangeStart);
			IMLRA_MakeSafeSplitDistance(imlSegment, currentRangeStart, distance);
			if (distance < 2)
				continue;
			sint32 cost = IMLRA_CalculateAdditionalCostOfRangeExplode(candidate);
			if (distance < requiredSize2)
			{
				cost += IMLRA_CalculateAdditionalCostAfterSplit(currentRange, currentRangeStart + distance);
				cost += ((requiredSize2 - distance) / 2) / 10;
			}
			if (cost < strategyCost)
			{
				strategyCost = cost;
				explodeRange.distance = distance;
				explodeRange.range = candidate;
			}
		}
	}

	void Apply(ppcImlGenContext_t* ctx, IMLSegment* imlSegment, raLivenessRange* currentRange) override
	{
		raInstructionEdge currentRangeStart = currentRange->interval.start;
		if (currentRangeStart.ConnectsToPreviousSegment())
			currentRangeStart.Set(0, true);
		sint32 requiredSize2 = currentRange->interval.GetPreciseDistance();
		IMLRA_ExplodeRangeCluster(nullptr, explodeRange.range);
		if (requiredSize2 > explodeRange.distance)
		{
			raLivenessRange* tailRange = IMLRA_SplitRange(nullptr, currentRange, currentRangeStart + explodeRange.distance, true);
			if (tailRange)
			{
				cemu_assert_debug(tailRange->list_fixedRegRequirements.empty());
				tailRange->UnsetPhysicalRegister();
			}
		}
	}

  private:
	struct
	{
		raLivenessRange* range;
		sint32 distance;
	} explodeRange;
};

class RASpillStrategy_ExplodeRangeInter : public RASpillStrategy
{
  public:
	void Reset()
	{
		ResetCost();
		explodeRange.range = nullptr;
		explodeRange.distance = -1;
	}

	void Evaluate(IMLSegment* imlSegment, raLivenessRange* currentRange, const IMLRALivenessTimeline& timeline, const IMLPhysRegisterSet& allowedRegs)
	{
		cemu_assert_debug(strategyCost == INT_MAX);
		cemu_assert_debug(explodeRange.range == nullptr && explodeRange.distance == -1);
		for (auto candidate : timeline.activeRanges)
		{
			if (!candidate->interval.ExtendsIntoNextSegment())
				continue;
			if (candidate->GetPhysicalRegister() < 0 && candidate != currentRange)
				continue;
			if (!allowedRegs.IsAvailable(candidate->GetPhysicalRegister()))
				continue;
			sint32 cost;
			cost = IMLRA_CalculateAdditionalCostOfRangeExplode(candidate);
			if (cost < strategyCost)
			{
				strategyCost = cost;
				explodeRange.distance = INT_MAX;
				explodeRange.range = candidate;
			}
		}
		sint32 ownCost;
		ownCost = IMLRA_CalculateAdditionalCostOfRangeExplode(currentRange);
		if (ownCost < strategyCost)
		{
			strategyCost = ownCost;
			explodeRange.distance = INT_MAX;
			explodeRange.range = currentRange;
		}
	}

	void Apply(ppcImlGenContext_t* ctx, IMLSegment* imlSegment, raLivenessRange* currentRange) override
	{
		cemu_assert_debug(strategyCost != INT_MAX);
		IMLRA_ExplodeRangeCluster(ctx, explodeRange.range);
	}

  private:
	struct
	{
		raLivenessRange* range;
		sint32 distance;
	}explodeRange;
};

void IMLRA_FilterReservedFixedRegisterRequirementsForSegment(IMLRegisterAllocatorContext& ctx, raLivenessRange* currentRange, IMLPhysRegisterSet& candidatePhysRegSet)
{
	IMLSegment* seg = currentRange->imlSegment;
	if (seg->imlList.empty())
		return;

	raInstructionEdge firstPos = currentRange->interval.start;
	if (currentRange->interval.start.ConnectsToPreviousSegment())
		firstPos.SetRaw(0);
	else if (currentRange->interval.start.ConnectsToNextSegment())
		firstPos.Set(seg->imlList.size() - 1, false);

	raInstructionEdge lastPos = currentRange->interval.end;
	if (currentRange->interval.end.ConnectsToPreviousSegment())
		lastPos.SetRaw(0);
	else if (currentRange->interval.end.ConnectsToNextSegment())
		lastPos.Set(seg->imlList.size() - 1, false);
	cemu_assert_debug(firstPos <= lastPos);

	IMLRegID ourRegId = currentRange->GetVirtualRegister();

	IMLFixedRegisters fixedRegs;
	if (firstPos.IsOnOutputEdge())
		GetInstructionFixedRegisters(seg->imlList.data() + firstPos.GetInstructionIndex(), fixedRegs);
	for (raInstructionEdge currentPos = firstPos; currentPos <= lastPos; ++currentPos)
	{
		if (currentPos.IsOnInputEdge())
		{
			GetInstructionFixedRegisters(seg->imlList.data() + currentPos.GetInstructionIndex(), fixedRegs);
		}
		auto& fixedRegAccess = currentPos.IsOnInputEdge() ? fixedRegs.listInput : fixedRegs.listOutput;
		for (auto& fixedRegLoc : fixedRegAccess)
		{
			if (fixedRegLoc.reg.IsInvalid() || fixedRegLoc.reg.GetRegID() != ourRegId)
				candidatePhysRegSet.RemoveRegisters(fixedRegLoc.physRegSet);
		}
	}
}

void IMLRA_FilterReservedFixedRegisterRequirementsForCluster(IMLRegisterAllocatorContext& ctx, IMLSegment* imlSegment, raLivenessRange* currentRange, IMLPhysRegisterSet& candidatePhysRegSet)
{
	cemu_assert_debug(currentRange->imlSegment == imlSegment);
	if (currentRange->interval.ExtendsPreviousSegment() || currentRange->interval.ExtendsIntoNextSegment())
	{
		auto clusterRanges = currentRange->GetAllSubrangesInCluster();
		for (auto& rangeIt : clusterRanges)
		{
			IMLRA_FilterReservedFixedRegisterRequirementsForSegment(ctx, rangeIt, candidatePhysRegSet);
			if (!candidatePhysRegSet.HasAnyAvailable())
				break;
		}
		return;
	}
	IMLRA_FilterReservedFixedRegisterRequirementsForSegment(ctx, currentRange, candidatePhysRegSet);
}

bool IMLRA_AssignSegmentRegisters(IMLRegisterAllocatorContext& ctx, ppcImlGenContext_t* ppcImlGenContext, IMLSegment* imlSegment)
{
	_sortSegmentAllSubrangesLinkedList(imlSegment);

	IMLRALivenessTimeline livenessTimeline;
	raLivenessRange* subrangeItr = imlSegment->raInfo.linkedList_allSubranges;
	raInstructionEdge lastInstructionEdge;
	lastInstructionEdge.SetRaw(RA_INTER_RANGE_END);

	struct
	{
		RASpillStrategy_LocalRangeHoleCutting localRangeHoleCutting;
		RASpillStrategy_AvailableRegisterHole availableRegisterHole;
		RASpillStrategy_ExplodeRange explodeRange;
		RASpillStrategy_ExplodeRangeInter explodeRangeInter;
	} strategy;

	while (subrangeItr)
	{
		raInstructionEdge currentRangeStart = subrangeItr->interval.start;
		PPCRecRA_debugValidateSubrange(subrangeItr);

		livenessTimeline.ExpireRanges((currentRangeStart > lastInstructionEdge) ? lastInstructionEdge : currentRangeStart);

		if (subrangeItr->GetPhysicalRegister() >= 0)
		{
#if DEBUG_RA_EXTRA_VALIDATION
			for (auto& liverangeItr : livenessTimeline.activeRanges)
			{
				cemu_assert_debug(liverangeItr->GetPhysicalRegister() != subrangeItr->GetPhysicalRegister());
			}
#endif
			livenessTimeline.AddActiveRange(subrangeItr);
			subrangeItr = subrangeItr->link_allSegmentRanges.next;
			continue;
		}
		if (!subrangeItr->list_fixedRegRequirements.empty())
		{
			cemu_assert_debug(subrangeItr->HasPhysicalRegister());
		}
		IMLRegFormat regBaseFormat = ctx.GetBaseFormatByRegId(subrangeItr->GetVirtualRegister());
		IMLPhysRegisterSet candidatePhysRegSet = ctx.raParam->GetPhysRegPool(regBaseFormat);
		cemu_assert_debug(candidatePhysRegSet.HasAnyAvailable());

		IMLPhysRegisterSet allowedRegs = subrangeItr->GetAllowedRegisters(candidatePhysRegSet);
		cemu_assert_debug(allowedRegs.HasAnyAvailable());
		candidatePhysRegSet &= allowedRegs;

		for (auto& liverangeItr : livenessTimeline.activeRanges)
		{
			cemu_assert_debug(liverangeItr->GetPhysicalRegister() >= 0);
			candidatePhysRegSet.SetReserved(liverangeItr->GetPhysicalRegister());
		}
		IMLPhysRegisterSet localAvailableRegsMask = candidatePhysRegSet;
		if (candidatePhysRegSet.HasAnyAvailable())
		{
			PPCRecRA_MaskOverlappingPhysRegForGlobalRange(subrangeItr, candidatePhysRegSet);
		}
		if (candidatePhysRegSet.HasAnyAvailable())
		{
			IMLRA_FilterReservedFixedRegisterRequirementsForCluster(ctx, imlSegment, subrangeItr, candidatePhysRegSet);
		}
		if (candidatePhysRegSet.HasAnyAvailable())
		{
			subrangeItr->SetPhysicalRegisterForCluster(candidatePhysRegSet.GetFirstAvailableReg());
			livenessTimeline.AddActiveRange(subrangeItr);
			subrangeItr = subrangeItr->link_allSegmentRanges.next;
			continue;
		}
		strategy.localRangeHoleCutting.Reset();
		strategy.availableRegisterHole.Reset();
		strategy.explodeRange.Reset();
		RASpillStrategy* selectedStrategy = nullptr;
		auto SelectStrategyIfBetter = [&selectedStrategy](RASpillStrategy& newStrategy) {
			if (newStrategy.GetCost() == INT_MAX)
				return;
			if (selectedStrategy == nullptr || newStrategy.GetCost() < selectedStrategy->GetCost())
				selectedStrategy = &newStrategy;
		};

		if (!subrangeItr->interval.ExtendsIntoNextSegment())
		{
			strategy.localRangeHoleCutting.Evaluate(imlSegment, subrangeItr, livenessTimeline, allowedRegs);
			SelectStrategyIfBetter(strategy.localRangeHoleCutting);
			strategy.availableRegisterHole.Evaluate(imlSegment, subrangeItr, livenessTimeline, localAvailableRegsMask, allowedRegs);
			SelectStrategyIfBetter(strategy.availableRegisterHole);
			strategy.explodeRange.Evaluate(imlSegment, subrangeItr, livenessTimeline, allowedRegs);
			SelectStrategyIfBetter(strategy.explodeRange);
		}
		else
		{
			strategy.explodeRangeInter.Reset();
			strategy.explodeRangeInter.Evaluate(imlSegment, subrangeItr, livenessTimeline, allowedRegs);
			SelectStrategyIfBetter(strategy.explodeRangeInter);
		}
		if (selectedStrategy)
		{
			selectedStrategy->Apply(ppcImlGenContext, imlSegment, subrangeItr);
		}
		else
		{
			cemu_assert_debug(subrangeItr->interval.ExtendsPreviousSegment());
			IMLRA_ExplodeRangeCluster(ppcImlGenContext, subrangeItr);
		}
		return false;
	}
	return true;
}

void IMLRA_AssignRegisters(IMLRegisterAllocatorContext& ctx, ppcImlGenContext_t* ppcImlGenContext)
{
	sint32 maxLoopDepth = 0;
	for (IMLSegment* segIt : ppcImlGenContext->segmentList2)
	{
		maxLoopDepth = std::max(maxLoopDepth, segIt->loopDepth);
	}
	for (IMLSegment* segIt : ppcImlGenContext->segmentList2)
		IMLRA_HandleFixedRegisters(ppcImlGenContext, segIt);
#if DEBUG_RA_EXTRA_VALIDATION
	for (IMLSegment* imlSegment : ppcImlGenContext->segmentList2)
	{
		for (raLivenessRange* currentRange = imlSegment->raInfo.linkedList_allSubranges; currentRange; currentRange = currentRange->link_allSegmentRanges.next)
		{
			IMLPhysRegisterSet allowedRegs;
			if (!currentRange->GetAllowedRegistersEx(allowedRegs))
			{
				cemu_assert_debug(currentRange->list_fixedRegRequirements.empty());
				continue;
			}
			cemu_assert_debug(currentRange->HasPhysicalRegister() && allowedRegs.IsAvailable(currentRange->GetPhysicalRegister()));
		}
	}
#endif

	while (true)
	{
		bool done = false;
		for (sint32 d = maxLoopDepth; d >= 0; d--)
		{
			for (IMLSegment* segIt : ppcImlGenContext->segmentList2)
			{
				if (segIt->loopDepth != d)
					continue;
				done = IMLRA_AssignSegmentRegisters(ctx, ppcImlGenContext, segIt);
				if (done == false)
					break;
			}
			if (done == false)
				break;
		}
		if (done)
			break;
	}
}

void IMLRA_ReshapeForRegisterAllocation(ppcImlGenContext_t* ppcImlGenContext)
{
	size_t segmentIndex = 0;
	while (segmentIndex < ppcImlGenContext->segmentList2.size())
	{
		IMLSegment* imlSegment = ppcImlGenContext->segmentList2[segmentIndex];
		if (imlSegment->nextSegmentIsUncertain)
		{
			segmentIndex++;
			continue;
		}
		if (imlSegment->nextSegmentBranchTaken == nullptr || imlSegment->nextSegmentBranchNotTaken == nullptr)
		{
			segmentIndex++;
			continue;
		}
		if (imlSegment->nextSegmentBranchNotTaken->list_prevSegments.size() <= 1)
		{
			segmentIndex++;
			continue;
		}
		if (imlSegment->nextSegmentBranchNotTaken->isEnterable)
		{
			segmentIndex++;
			continue;
		}
		PPCRecompilerIml_insertSegments(ppcImlGenContext, segmentIndex + 1, 1);
		IMLSegment* imlSegmentP0 = ppcImlGenContext->segmentList2[segmentIndex + 0];
		IMLSegment* imlSegmentP1 = ppcImlGenContext->segmentList2[segmentIndex + 1];
		IMLSegment* nextSegment = imlSegment->nextSegmentBranchNotTaken;
		IMLSegment_RemoveLink(imlSegmentP0, nextSegment);
		IMLSegment_SetLinkBranchNotTaken(imlSegmentP1, nextSegment);
		IMLSegment_SetLinkBranchNotTaken(imlSegmentP0, imlSegmentP1);
		segmentIndex++;
	}
	for (size_t s = 0; s < ppcImlGenContext->segmentList2.size(); s++)
	{
		IMLSegment* imlSegment = ppcImlGenContext->segmentList2[s];
		imlSegment->momentaryIndex = s;
	}
	for (size_t s = 0; s < ppcImlGenContext->segmentList2.size(); s++)
	{
		IMLSegment* imlSegment = ppcImlGenContext->segmentList2[s];
		IMLRA_IdentifyLoop(ppcImlGenContext, imlSegment);
	}
}

IMLRARegAbstractLiveness* _GetAbstractRange(IMLRegisterAllocatorContext& ctx, IMLSegment* imlSegment, IMLRegID regId)
{
	auto& segMap = ctx.GetSegmentAbstractRangeMap(imlSegment);
	auto it = segMap.find(regId);
	return it != segMap.end() ? &it->second : nullptr;
}

void IMLRA_CalculateSegmentMinMaxAbstractRanges(IMLRegisterAllocatorContext& ctx, IMLSegment* imlSegment)
{
	size_t instructionIndex = 0;
	IMLUsedRegisters gprTracking;
	auto& segDistMap = ctx.GetSegmentAbstractRangeMap(imlSegment);
	while (instructionIndex < imlSegment->imlList.size())
	{
		imlSegment->imlList[instructionIndex].CheckRegisterUsage(&gprTracking);
		gprTracking.ForEachAccessedGPR([&](IMLReg gprReg, bool isWritten) {
			IMLRegID gprId = gprReg.GetRegID();
			auto it = segDistMap.find(gprId);
			if (it == segDistMap.end())
			{
				segDistMap.try_emplace(gprId, gprReg.GetBaseFormat(), (sint32)instructionIndex, (sint32)instructionIndex + 1);
				ctx.regIdToBaseFormat.try_emplace(gprId, gprReg.GetBaseFormat());
			}
			else
			{
				it->second.TrackInstruction(instructionIndex);
#ifdef CEMU_DEBUG_ASSERT
				cemu_assert_debug(ctx.regIdToBaseFormat[gprId] == gprReg.GetBaseFormat());
#endif
			}
		});
		instructionIndex++;
	}
}

void IMLRA_CalculateLivenessRanges(IMLRegisterAllocatorContext& ctx)
{
	size_t dbgIndex = 0;
	for (IMLSegment* segIt : ctx.deprGenContext->segmentList2)
	{
		cemu_assert_debug(segIt->momentaryIndex == dbgIndex);
		IMLRA_CalculateSegmentMinMaxAbstractRanges(ctx, segIt);
		dbgIndex++;
	}
}

raLivenessRange* PPCRecRA_convertToMappedRanges(IMLRegisterAllocatorContext& ctx, IMLSegment* imlSegment, IMLRegID vGPR, IMLName name)
{
	IMLRARegAbstractLiveness* abstractRange = _GetAbstractRange(ctx, imlSegment, vGPR);
	if (!abstractRange)
		return nullptr;
	if (abstractRange->isProcessed)
	{
		raLivenessRange* existingRange = IMLRA_GetSubrange(imlSegment, vGPR);
		cemu_assert_debug(existingRange);
		return existingRange;
	}
	abstractRange->isProcessed = true;
	cemu_assert_debug(IMLRA_GetSubrange(imlSegment, vGPR) == nullptr);
	cemu_assert_debug(
		(abstractRange->usageStart == abstractRange->usageEnd && (abstractRange->usageStart == RA_INTER_RANGE_START || abstractRange->usageStart == RA_INTER_RANGE_END)) ||
		abstractRange->usageStart < abstractRange->usageEnd);
	sint32 inclusiveEnd = abstractRange->usageEnd;
	if (inclusiveEnd != RA_INTER_RANGE_START && inclusiveEnd != RA_INTER_RANGE_END)
		inclusiveEnd--;
	raInterval interval;
	interval.SetInterval(abstractRange->usageStart, true, inclusiveEnd, true);
	raLivenessRange* subrange = IMLRA_CreateRange(ctx.deprGenContext, imlSegment, vGPR, name, interval.start, interval.end);
	if (abstractRange->usageEnd == RA_INTER_RANGE_END)
	{
		if (imlSegment->nextSegmentBranchTaken)
		{
			IMLRARegAbstractLiveness* branchTakenRange = _GetAbstractRange(ctx, imlSegment->nextSegmentBranchTaken, vGPR);
			if (branchTakenRange && branchTakenRange->usageStart == RA_INTER_RANGE_START)
			{
				subrange->subrangeBranchTaken = PPCRecRA_convertToMappedRanges(ctx, imlSegment->nextSegmentBranchTaken, vGPR, name);
				subrange->subrangeBranchTaken->previousRanges.push_back(subrange);
				cemu_assert_debug(subrange->subrangeBranchTaken->interval.ExtendsPreviousSegment());
			}
		}
		if (imlSegment->nextSegmentBranchNotTaken)
		{
			IMLRARegAbstractLiveness* branchNotTakenRange = _GetAbstractRange(ctx, imlSegment->nextSegmentBranchNotTaken, vGPR);
			if (branchNotTakenRange && branchNotTakenRange->usageStart == RA_INTER_RANGE_START)
			{
				subrange->subrangeBranchNotTaken = PPCRecRA_convertToMappedRanges(ctx, imlSegment->nextSegmentBranchNotTaken, vGPR, name);
				subrange->subrangeBranchNotTaken->previousRanges.push_back(subrange);
				cemu_assert_debug(subrange->subrangeBranchNotTaken->interval.ExtendsPreviousSegment());
			}
		}
	}
	if (abstractRange->usageStart == RA_INTER_RANGE_START)
	{
		for (auto& it : imlSegment->list_prevSegments)
		{
			IMLRARegAbstractLiveness* prevRange = _GetAbstractRange(ctx, it, vGPR);
			if (!prevRange)
				continue;
			if (prevRange->usageEnd == RA_INTER_RANGE_END)
				PPCRecRA_convertToMappedRanges(ctx, it, vGPR, name);
		}
	}
	return subrange;
}

void IMLRA_UpdateOrAddSubrangeLocation(raLivenessRange* subrange, raInstructionEdge pos)
{
	if (subrange->list_accessLocations.empty())
	{
		subrange->list_accessLocations.emplace_back(pos);
		return;
	}
	if(subrange->list_accessLocations.back().pos == pos)
		return;
	cemu_assert_debug(subrange->list_accessLocations.back().pos < pos);
	subrange->list_accessLocations.emplace_back(pos);
}

void IMLRA_ConvertAbstractToLivenessRanges(IMLRegisterAllocatorContext& ctx, IMLSegment* imlSegment)
{
	const std::unordered_map<IMLRegID, raLivenessRange*>& regToSubrange = IMLRA_GetSubrangeMap(imlSegment);

	auto AddOrUpdateFixedRegRequirement = [&](IMLRegID regId, sint32 instructionIndex, bool isInput, const IMLPhysRegisterSet& physRegSet) {
		raLivenessRange* subrange = regToSubrange.find(regId)->second;
		cemu_assert_debug(subrange);
		raFixedRegRequirement tmp;
		tmp.pos.Set(instructionIndex, isInput);
		tmp.allowedReg = physRegSet;
		if (subrange->list_fixedRegRequirements.empty() || subrange->list_fixedRegRequirements.back().pos != tmp.pos)
			subrange->list_fixedRegRequirements.push_back(tmp);
	};

	auto& segMap = ctx.GetSegmentAbstractRangeMap(imlSegment);
	for (auto& it : segMap)
	{
		if (it.second.isProcessed)
			continue;
		IMLRegID regId = it.first;
		PPCRecRA_convertToMappedRanges(ctx, imlSegment, regId, ctx.raParam->regIdToName.find(regId)->second);
	}
	size_t index = 0;
	IMLUsedRegisters gprTracking;
	while (index < imlSegment->imlList.size())
	{
		imlSegment->imlList[index].CheckRegisterUsage(&gprTracking);
		raInstructionEdge pos((sint32)index, true);
		gprTracking.ForEachReadGPR([&](IMLReg gprReg) {
			IMLRegID gprId = gprReg.GetRegID();
			raLivenessRange* subrange = regToSubrange.find(gprId)->second;
			IMLRA_UpdateOrAddSubrangeLocation(subrange, pos);
		});
		pos = {(sint32)index, false};
		gprTracking.ForEachWrittenGPR([&](IMLReg gprReg) {
			IMLRegID gprId = gprReg.GetRegID();
			raLivenessRange* subrange = regToSubrange.find(gprId)->second;
			IMLRA_UpdateOrAddSubrangeLocation(subrange, pos);
		});
		IMLFixedRegisters fixedRegs;
		GetInstructionFixedRegisters(&imlSegment->imlList[index], fixedRegs);
		for (auto& fixedRegAccess : fixedRegs.listInput)
		{
			if (fixedRegAccess.reg != IMLREG_INVALID)
				AddOrUpdateFixedRegRequirement(fixedRegAccess.reg.GetRegID(), index, true, fixedRegAccess.physRegSet);
		}
		for (auto& fixedRegAccess : fixedRegs.listOutput)
		{
			if (fixedRegAccess.reg != IMLREG_INVALID)
				AddOrUpdateFixedRegRequirement(fixedRegAccess.reg.GetRegID(), index, false, fixedRegAccess.physRegSet);
		}
		index++;
	}
}

void IMLRA_extendAbstractRangeToEndOfSegment(IMLRegisterAllocatorContext& ctx, IMLSegment* imlSegment, IMLRegID regId)
{
	auto& segDistMap = ctx.GetSegmentAbstractRangeMap(imlSegment);
	auto it = segDistMap.find(regId);
	if (it == segDistMap.end())
	{
		sint32 startIndex;
		if (imlSegment->HasSuffixInstruction())
			startIndex = imlSegment->GetSuffixInstructionIndex();
		else
			startIndex = RA_INTER_RANGE_END;
		segDistMap.try_emplace((IMLRegID)regId, IMLRegFormat::INVALID_FORMAT, startIndex, RA_INTER_RANGE_END);
	}
	else
	{
		it->second.usageEnd = RA_INTER_RANGE_END;
	}
}

void IMLRA_extendAbstractRangeToBeginningOfSegment(IMLRegisterAllocatorContext& ctx, IMLSegment* imlSegment, IMLRegID regId)
{
	auto& segDistMap = ctx.GetSegmentAbstractRangeMap(imlSegment);
	auto it = segDistMap.find(regId);
	if (it == segDistMap.end())
	{
		segDistMap.try_emplace((IMLRegID)regId, IMLRegFormat::INVALID_FORMAT, RA_INTER_RANGE_START, RA_INTER_RANGE_START);
	}
	else
	{
		it->second.usageStart = RA_INTER_RANGE_START;
	}
	for (auto& it : imlSegment->list_prevSegments)
	{
		IMLRA_extendAbstractRangeToEndOfSegment(ctx, it, regId);
	}
}

void IMLRA_connectAbstractRanges(IMLRegisterAllocatorContext& ctx, IMLRegID regId, IMLSegment** route, sint32 routeDepth)
{
#ifdef CEMU_DEBUG_ASSERT
	if (routeDepth < 2)
		assert_dbg();
#endif
	IMLRA_extendAbstractRangeToEndOfSegment(ctx, route[0], regId);
	for (sint32 i = 1; i < (routeDepth - 1); i++)
	{
		IMLRA_extendAbstractRangeToEndOfSegment(ctx, route[i], regId);
		IMLRA_extendAbstractRangeToBeginningOfSegment(ctx, route[i], regId);
	}
	IMLRA_extendAbstractRangeToBeginningOfSegment(ctx, route[routeDepth - 1], regId);
}

void _IMLRA_checkAndTryExtendRange(IMLRegisterAllocatorContext& ctx, IMLSegment* currentSegment, IMLRegID regID, sint32 distanceLeft, IMLSegment** route, sint32 routeDepth)
{
	if (routeDepth >= 64)
	{
		cemuLog_logDebug(LogType::Force, "Recompiler RA route maximum depth exceeded\n");
		return;
	}
	route[routeDepth] = currentSegment;

	IMLRARegAbstractLiveness* range = _GetAbstractRange(ctx, currentSegment, regID);

	if (!range)
	{
		distanceLeft -= (sint32)currentSegment->imlList.size();
		if (distanceLeft > 0)
		{
			if (currentSegment->nextSegmentBranchNotTaken)
				_IMLRA_checkAndTryExtendRange(ctx, currentSegment->nextSegmentBranchNotTaken, regID, distanceLeft, route, routeDepth + 1);
			if (currentSegment->nextSegmentBranchTaken)
				_IMLRA_checkAndTryExtendRange(ctx, currentSegment->nextSegmentBranchTaken, regID, distanceLeft, route, routeDepth + 1);
		}
		return;
	}
	else
	{
		if (range->usageStart == RA_INTER_RANGE_END)
		{
			if (distanceLeft < (sint32)currentSegment->imlList.size())
				return;
		}
		else if (range->usageStart != RA_INTER_RANGE_START && range->usageStart > distanceLeft)
			return;
		IMLRA_connectAbstractRanges(ctx, regID, route, routeDepth + 1);
	}
}

void PPCRecRA_checkAndTryExtendRange(IMLRegisterAllocatorContext& ctx, IMLSegment* currentSegment, IMLRARegAbstractLiveness* range, IMLRegID regID)
{
	cemu_assert_debug(range->usageEnd >= 0);
	sint32 instructionsUntilEndOfSeg;
	if (range->usageEnd == RA_INTER_RANGE_END)
		instructionsUntilEndOfSeg = 0;
	else
		instructionsUntilEndOfSeg = (sint32)currentSegment->imlList.size() - range->usageEnd;
	cemu_assert_debug(instructionsUntilEndOfSeg >= 0);
	sint32 remainingScanDist = 45 - instructionsUntilEndOfSeg;
	if (remainingScanDist <= 0)
		return;

	IMLSegment* route[64];
	route[0] = currentSegment;
	if (currentSegment->nextSegmentBranchNotTaken)
		_IMLRA_checkAndTryExtendRange(ctx, currentSegment->nextSegmentBranchNotTaken, regID, remainingScanDist, route, 1);
	if (currentSegment->nextSegmentBranchTaken)
		_IMLRA_checkAndTryExtendRange(ctx, currentSegment->nextSegmentBranchTaken, regID, remainingScanDist, route, 1);
}

void PPCRecRA_mergeCloseRangesForSegmentV2(IMLRegisterAllocatorContext& ctx, IMLSegment* imlSegment)
{
	auto& segMap = ctx.GetSegmentAbstractRangeMap(imlSegment);
	for (auto& it : segMap)
	{
		PPCRecRA_checkAndTryExtendRange(ctx, imlSegment, &(it.second), it.first);
	}
#ifdef CEMU_DEBUG_ASSERT
	if (imlSegment->list_prevSegments.empty() == false && imlSegment->isEnterable)
		assert_dbg();
	if ((imlSegment->nextSegmentBranchNotTaken != nullptr || imlSegment->nextSegmentBranchTaken != nullptr) && imlSegment->nextSegmentIsUncertain)
		assert_dbg();
#endif
}

void PPCRecRA_followFlowAndExtendRanges(IMLRegisterAllocatorContext& ctx, IMLSegment* imlSegment)
{
	std::vector<IMLSegment*> list_segments;
	std::vector<bool> list_processedSegment;
	size_t segmentCount = ctx.deprGenContext->segmentList2.size();
	list_segments.reserve(segmentCount + 1);
	list_processedSegment.resize(segmentCount);

	auto markSegProcessed = [&list_processedSegment](IMLSegment* seg) {
		list_processedSegment[seg->momentaryIndex] = true;
	};
	auto isSegProcessed = [&list_processedSegment](IMLSegment* seg) -> bool {
		return list_processedSegment[seg->momentaryIndex];
	};
	markSegProcessed(imlSegment);

	sint32 index = 0;
	list_segments.push_back(imlSegment);
	while (index < list_segments.size())
	{
		IMLSegment* currentSegment = list_segments[index];
		PPCRecRA_mergeCloseRangesForSegmentV2(ctx, currentSegment);
		if (currentSegment->nextSegmentBranchNotTaken && !isSegProcessed(currentSegment->nextSegmentBranchNotTaken))
		{
			markSegProcessed(currentSegment->nextSegmentBranchNotTaken);
			list_segments.push_back(currentSegment->nextSegmentBranchNotTaken);
		}
		if (currentSegment->nextSegmentBranchTaken && !isSegProcessed(currentSegment->nextSegmentBranchTaken))
		{
			markSegProcessed(currentSegment->nextSegmentBranchTaken);
			list_segments.push_back(currentSegment->nextSegmentBranchTaken);
		}
		index++;
	}
}

void IMLRA_MergeCloseAbstractRanges(IMLRegisterAllocatorContext& ctx)
{
	for (size_t s = 0; s < ctx.deprGenContext->segmentList2.size(); s++)
	{
		IMLSegment* imlSegment = ctx.deprGenContext->segmentList2[s];
		if (!imlSegment->list_prevSegments.empty())
			continue;
		PPCRecRA_followFlowAndExtendRanges(ctx, imlSegment);
	}
}

void IMLRA_ExtendAbstractRangesOutOfLoops(IMLRegisterAllocatorContext& ctx)
{
	for (size_t s = 0; s < ctx.deprGenContext->segmentList2.size(); s++)
	{
		IMLSegment* imlSegment = ctx.deprGenContext->segmentList2[s];
		auto localLoopDepth = imlSegment->loopDepth;
		if (localLoopDepth <= 0)
			continue;
		bool hasLoopExit = false;
		if (imlSegment->nextSegmentBranchTaken && imlSegment->nextSegmentBranchTaken->loopDepth < localLoopDepth)
		{
			hasLoopExit = true;
		}
		if (imlSegment->nextSegmentBranchNotTaken && imlSegment->nextSegmentBranchNotTaken->loopDepth < localLoopDepth)
		{
			hasLoopExit = true;
		}
		if (hasLoopExit == false)
			continue;

		auto& segMap = ctx.GetSegmentAbstractRangeMap(imlSegment);
		for (auto& it : segMap)
		{
			if (it.second.usageEnd != RA_INTER_RANGE_END)
				continue;
			if (imlSegment->nextSegmentBranchTaken)
				IMLRA_extendAbstractRangeToBeginningOfSegment(ctx, imlSegment->nextSegmentBranchTaken, it.first);
			if (imlSegment->nextSegmentBranchNotTaken)
				IMLRA_extendAbstractRangeToBeginningOfSegment(ctx, imlSegment->nextSegmentBranchNotTaken, it.first);
		}
	}
}

void IMLRA_ProcessFlowAndCalculateLivenessRanges(IMLRegisterAllocatorContext& ctx)
{
	IMLRA_MergeCloseAbstractRanges(ctx);
	IMLRA_ExtendAbstractRangesOutOfLoops(ctx);
	for (auto& segIt : ctx.deprGenContext->segmentList2)
		IMLRA_ConvertAbstractToLivenessRanges(ctx, segIt);
}

void IMLRA_AnalyzeSubrangeDataDependency(raLivenessRange* subrange)
{
	bool isRead = false;
	bool isWritten = false;
	bool isOverwritten = false;
	for (auto& location : subrange->list_accessLocations)
	{
		if (location.IsRead())
		{
			isRead = true;
		}
		if (location.IsWrite())
		{
			if (isRead == false)
				isOverwritten = true;
			isWritten = true;
		}
	}
	subrange->_noLoad = isOverwritten;
	subrange->hasStore = isWritten;

	if (subrange->interval.ExtendsPreviousSegment())
		subrange->_noLoad = true;
}

struct subrangeEndingInfo_t
{
	raLivenessRange* subrangeList[SUBRANGE_LIST_SIZE];
	sint32 subrangeCount;

	bool hasUndefinedEndings;
};

void _findSubrangeWriteEndings(raLivenessRange* subrange, uint32 iterationIndex, sint32 depth, subrangeEndingInfo_t* info)
{
	if (depth >= 30)
	{
		info->hasUndefinedEndings = true;
		return;
	}
	if (subrange->lastIterationIndex == iterationIndex)
		return;
	subrange->lastIterationIndex = iterationIndex;
	if (subrange->hasStoreDelayed)
		return;
	IMLSegment* imlSegment = subrange->imlSegment;
	if (!subrange->interval.ExtendsIntoNextSegment())
	{
		if (info->subrangeCount >= SUBRANGE_LIST_SIZE)
		{
			info->hasUndefinedEndings = true;
			return;
		}
		else
		{
			info->subrangeList[info->subrangeCount] = subrange;
			info->subrangeCount++;
		}
		return;
	}

	if (imlSegment->nextSegmentBranchNotTaken)
	{
		if (subrange->subrangeBranchNotTaken == nullptr)
		{
			info->hasUndefinedEndings = true;
		}
		else
		{
			_findSubrangeWriteEndings(subrange->subrangeBranchNotTaken, iterationIndex, depth + 1, info);
		}
	}
	if (imlSegment->nextSegmentBranchTaken)
	{
		if (subrange->subrangeBranchTaken == nullptr)
		{
			info->hasUndefinedEndings = true;
		}
		else
		{
			_findSubrangeWriteEndings(subrange->subrangeBranchTaken, iterationIndex, depth + 1, info);
		}
	}
}

static void IMLRA_AnalyzeRangeDataFlow(raLivenessRange* subrange)
{
	if (!subrange->interval.ExtendsIntoNextSegment())
		return;
	if (subrange->hasStore)
	{
		subrangeEndingInfo_t writeEndingInfo;
		writeEndingInfo.subrangeCount = 0;
		writeEndingInfo.hasUndefinedEndings = false;
		_findSubrangeWriteEndings(subrange, IMLRA_GetNextIterationIndex(), 0, &writeEndingInfo);
		if (writeEndingInfo.hasUndefinedEndings == false)
		{
			sint32 delayStoreCost = 0;
			bool alreadyStoredInAllEndings = true;
			for (sint32 i = 0; i < writeEndingInfo.subrangeCount; i++)
			{
				raLivenessRange* subrangeItr = writeEndingInfo.subrangeList[i];
				if (subrangeItr->hasStore)
					continue;
				alreadyStoredInAllEndings = false;
				sint32 storeCost = IMLRA_GetSegmentReadWriteCost(subrangeItr->imlSegment);
				delayStoreCost = std::max(storeCost, delayStoreCost);
			}
			if (alreadyStoredInAllEndings)
			{
				subrange->hasStore = false;
				subrange->hasStoreDelayed = true;
			}
			else if (delayStoreCost <= IMLRA_GetSegmentReadWriteCost(subrange->imlSegment))
			{
				subrange->hasStore = false;
				subrange->hasStoreDelayed = true;
				for (sint32 i = 0; i < writeEndingInfo.subrangeCount; i++)
				{
					raLivenessRange* subrangeItr = writeEndingInfo.subrangeList[i];
					subrangeItr->hasStore = true;
				}
			}
		}
	}
}

void IMLRA_AnalyzeRangeDataFlow(ppcImlGenContext_t* ppcImlGenContext)
{
	for (auto& seg : ppcImlGenContext->segmentList2)
	{
		raLivenessRange* subrange = seg->raInfo.linkedList_allSubranges;
		while (subrange)
		{
			IMLRA_AnalyzeSubrangeDataDependency(subrange);
			subrange = subrange->link_allSegmentRanges.next;
		}
	}
	for (auto& seg : ppcImlGenContext->segmentList2)
	{
		raLivenessRange* subrange = seg->raInfo.linkedList_allSubranges;
		while (subrange)
		{
			IMLRA_AnalyzeRangeDataFlow(subrange);
			subrange = subrange->link_allSegmentRanges.next;
		}
	}
}

inline IMLReg _MakeNativeReg(IMLRegFormat baseFormat, IMLRegID regId)
{
	return IMLReg(baseFormat, baseFormat, 0, regId);
}

void IMLRA_RewriteRegisters(IMLRegisterAllocatorContext& ctx, IMLSegment* imlSegment)
{
	std::unordered_map<IMLRegID, IMLRegID> virtId2PhysReg;
	boost::container::small_vector<raLivenessRange*, 64> activeRanges;
	raLivenessRange* currentRange = imlSegment->raInfo.linkedList_allSubranges;
	raInstructionEdge currentEdge;
	for (size_t i = 0; i < imlSegment->imlList.size(); i++)
	{
		currentEdge.Set(i, false);
		while (currentRange && currentRange->interval.start <= currentEdge)
		{
			cemu_assert_debug(virtId2PhysReg.find(currentRange->GetVirtualRegister()) == virtId2PhysReg.end() || virtId2PhysReg[currentRange->GetVirtualRegister()] == currentRange->GetPhysicalRegister());

			virtId2PhysReg[currentRange->GetVirtualRegister()] = currentRange->GetPhysicalRegister();
			activeRanges.push_back(currentRange);
			currentRange = currentRange->link_allSegmentRanges.next;
		}
		imlSegment->imlList[i].RewriteGPR(virtId2PhysReg);
		auto it = activeRanges.begin();
		while (it != activeRanges.end())
		{
			if ((*it)->interval.end <= currentEdge)
			{
				virtId2PhysReg.erase((*it)->GetVirtualRegister());
				it = activeRanges.erase(it);
			}
			else
				++it;
		}
	}
}

void IMLRA_GenerateSegmentMoveInstructions2(IMLRegisterAllocatorContext& ctx, IMLSegment* imlSegment)
{
	IMLRA_RewriteRegisters(ctx, imlSegment);

#if DEBUG_RA_INSTRUCTION_GEN
	cemuLog_log(LogType::Force, "");
	cemuLog_log(LogType::Force, "[Seg before RA]");
	IMLDebug_DumpSegment(nullptr, imlSegment, true);
#endif

	bool hadSuffixInstruction = imlSegment->HasSuffixInstruction();

	std::vector<IMLInstruction> rebuiltInstructions;
	sint32 numInstructionsWithoutSuffix = (sint32)imlSegment->imlList.size() - (imlSegment->HasSuffixInstruction() ? 1 : 0);

	if (imlSegment->imlList.empty())
	{
		raLivenessRange* currentRange = imlSegment->raInfo.linkedList_allSubranges;
		while (currentRange)
		{
			if (currentRange->hasStore)
				rebuiltInstructions.emplace_back().make_name_r(currentRange->GetName(), _MakeNativeReg(ctx.regIdToBaseFormat[currentRange->GetVirtualRegister()], currentRange->GetPhysicalRegister()));
			currentRange = currentRange->link_allSegmentRanges.next;
		}
		currentRange = imlSegment->raInfo.linkedList_allSubranges;
		while (currentRange)
		{
			if (!currentRange->_noLoad)
			{
				cemu_assert_debug(currentRange->interval.ExtendsIntoNextSegment());
				rebuiltInstructions.emplace_back().make_r_name(_MakeNativeReg(ctx.regIdToBaseFormat[currentRange->GetVirtualRegister()], currentRange->GetPhysicalRegister()), currentRange->GetName());
			}
			currentRange = currentRange->link_allSegmentRanges.next;
		}
		imlSegment->imlList = std::move(rebuiltInstructions);
		return;
	}

	{
		raLivenessRange* currentRange = imlSegment->raInfo.linkedList_allSubranges;
		raInstructionEdge edge;
		if (imlSegment->HasSuffixInstruction())
			edge.Set(numInstructionsWithoutSuffix, true);
		else
			edge.Set(numInstructionsWithoutSuffix - 1, false);

		while (currentRange)
		{
			if (!currentRange->interval.IsNextSegmentOnly() && currentRange->interval.end > edge)
			{
				currentRange->interval.SetEnd(edge);
			}
			currentRange = currentRange->link_allSegmentRanges.next;
		}
	}

#if DEBUG_RA_INSTRUCTION_GEN
	cemuLog_log(LogType::Force, "");
	cemuLog_log(LogType::Force, "--- Intermediate liveness info ---");
	{
		raLivenessRange* dbgRange = imlSegment->raInfo.linkedList_allSubranges;
		while (dbgRange)
		{
			cemuLog_log(LogType::Force, "Range i{}: {}-{}", dbgRange->GetVirtualRegister(), dbgRange->interval2.start.GetDebugString(), dbgRange->interval2.end.GetDebugString());
			dbgRange = dbgRange->link_allSegmentRanges.next;
		}
	}
#endif

	boost::container::small_vector<raLivenessRange*, 64> activeRanges;
	raLivenessRange* currentRange = imlSegment->raInfo.linkedList_allSubranges;
	while (currentRange && currentRange->interval.start.ConnectsToPreviousSegment())
	{
		activeRanges.push_back(currentRange);
		currentRange = currentRange->link_allSegmentRanges.next;
	}
	auto it = activeRanges.begin();
	raInstructionEdge firstOutputEdge;
	firstOutputEdge.Set(0, false);
	while (it != activeRanges.end())
	{
		if ((*it)->interval.end < firstOutputEdge)
		{
			raLivenessRange* storedRange = *it;
			if (storedRange->hasStore)
				rebuiltInstructions.emplace_back().make_name_r(storedRange->GetName(), _MakeNativeReg(ctx.regIdToBaseFormat[storedRange->GetVirtualRegister()], storedRange->GetPhysicalRegister()));
			it = activeRanges.erase(it);
			continue;
		}
		++it;
	}

	sint32 numInstructions = (sint32)imlSegment->imlList.size();
	for (sint32 i = 0; i < numInstructions; i++)
	{
		raInstructionEdge curEdge;
		curEdge.SetRaw(i * 2 + 1);
		while (currentRange && currentRange->interval.start <= curEdge)
		{
			if (!currentRange->_noLoad)
			{
				rebuiltInstructions.emplace_back().make_r_name(_MakeNativeReg(ctx.regIdToBaseFormat[currentRange->GetVirtualRegister()], currentRange->GetPhysicalRegister()), currentRange->GetName());
			}
			activeRanges.push_back(currentRange);
			currentRange = currentRange->link_allSegmentRanges.next;
		}
		rebuiltInstructions.push_back(imlSegment->imlList[i]);
		curEdge.SetRaw(i * 2 + 1 + 1);
		auto it = activeRanges.begin();
		while (it != activeRanges.end())
		{
			if ((*it)->interval.end <= curEdge)
			{
				raLivenessRange* storedRange = *it;
				if (storedRange->hasStore)
				{
					cemu_assert_debug(i != numInstructionsWithoutSuffix);
					rebuiltInstructions.emplace_back().make_name_r(storedRange->GetName(), _MakeNativeReg(ctx.regIdToBaseFormat[storedRange->GetVirtualRegister()], storedRange->GetPhysicalRegister()));
				}
				it = activeRanges.erase(it);
				continue;
			}
			++it;
		}
	}
	cemu_assert_debug(hadSuffixInstruction == imlSegment->HasSuffixInstruction());
	if (imlSegment->HasSuffixInstruction())
	{
		if (currentRange)
		{
			cemuLog_logDebug(LogType::Force, "[DEBUG] GenerateSegmentMoveInstructions() hit suffix path with non-null currentRange. Segment: {:08x}", imlSegment->ppcAddress);
		}
		for (auto& remainingRange : activeRanges)
		{
			cemu_assert_debug(!remainingRange->hasStore);
		}
	}
	else
	{
		for (auto& remainingRange : activeRanges)
		{
			cemu_assert_debug(!remainingRange->hasStore);
		}
		while (currentRange)
		{
			cemu_assert_debug(currentRange->interval.IsNextSegmentOnly());
			cemu_assert_debug(!currentRange->_noLoad);
			rebuiltInstructions.emplace_back().make_r_name(_MakeNativeReg(ctx.regIdToBaseFormat[currentRange->GetVirtualRegister()], currentRange->GetPhysicalRegister()), currentRange->GetName());
			currentRange = currentRange->link_allSegmentRanges.next;
		}
	}

	imlSegment->imlList = std::move(rebuiltInstructions);
	cemu_assert_debug(hadSuffixInstruction == imlSegment->HasSuffixInstruction());

#if DEBUG_RA_INSTRUCTION_GEN
	cemuLog_log(LogType::Force, "");
	cemuLog_log(LogType::Force, "[Seg after RA]");
	IMLDebug_DumpSegment(nullptr, imlSegment, false);
#endif
}

void IMLRA_GenerateMoveInstructions(IMLRegisterAllocatorContext& ctx)
{
	for (size_t s = 0; s < ctx.deprGenContext->segmentList2.size(); s++)
	{
		IMLSegment* imlSegment = ctx.deprGenContext->segmentList2[s];
		IMLRA_GenerateSegmentMoveInstructions2(ctx, imlSegment);
	}
}

static void DbgVerifyFixedRegRequirements(IMLSegment* imlSegment)
{
#if DEBUG_RA_EXTRA_VALIDATION
	std::vector<raFixedRegRequirementWithVGPR> frr = IMLRA_BuildSegmentInstructionFixedRegList(imlSegment);
	for(auto& fixedReq : frr)
	{
		for (raLivenessRange* range = imlSegment->raInfo.linkedList_allSubranges; range; range = range->link_allSegmentRanges.next)
		{
			if (!range->interval2.ContainsEdge(fixedReq.pos))
				continue;
			if(range->GetVirtualRegister() == fixedReq.regId)
			{
				cemu_assert(range->HasPhysicalRegister());
				cemu_assert(fixedReq.allowedReg.IsAvailable(range->GetPhysicalRegister()));
			}
			else
			{
				cemu_assert(!fixedReq.allowedReg.IsAvailable(range->GetPhysicalRegister()));
			}
		}
	}
#endif
}

static void DbgVerifyAllRanges(IMLRegisterAllocatorContext& ctx)
{
#if DEBUG_RA_EXTRA_VALIDATION
	for (size_t s = 0; s < ctx.deprGenContext->segmentList2.size(); s++)
	{
		IMLSegment* imlSegment = ctx.deprGenContext->segmentList2[s];
		raLivenessRange* subrangeItr = imlSegment->raInfo.linkedList_allSubranges;
		while (subrangeItr)
		{
			PPCRecRA_debugValidateSubrange(subrangeItr);
			subrangeItr = subrangeItr->link_allSegmentRanges.next;
		}
	}
	for (size_t s = 0; s < ctx.deprGenContext->segmentList2.size(); s++)
	{
		DbgVerifyFixedRegRequirements(ctx.deprGenContext->segmentList2[s]);
	}
#endif
}

void IMLRegisterAllocator_AllocateRegisters(ppcImlGenContext_t* ppcImlGenContext, IMLRegisterAllocatorParameters& raParam)
{
	IMLRegisterAllocatorContext ctx;
	ctx.raParam = &raParam;
	ctx.deprGenContext = ppcImlGenContext;

	IMLRA_ReshapeForRegisterAllocation(ppcImlGenContext);
	ppcImlGenContext->UpdateSegmentIndices();
	ctx.perSegmentAbstractRanges.resize(ppcImlGenContext->segmentList2.size());
	IMLRA_CalculateLivenessRanges(ctx);
	IMLRA_ProcessFlowAndCalculateLivenessRanges(ctx);
	IMLRA_AssignRegisters(ctx, ppcImlGenContext);
	DbgVerifyAllRanges(ctx);
	IMLRA_AnalyzeRangeDataFlow(ppcImlGenContext);
	IMLRA_GenerateMoveInstructions(ctx);

	IMLRA_DeleteAllRanges(ppcImlGenContext);
}

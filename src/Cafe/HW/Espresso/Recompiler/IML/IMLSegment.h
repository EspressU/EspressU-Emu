#pragma once
#include "IMLInstruction.h"

#include <boost/container/small_vector.hpp>

#define RA_INTER_RANGE_START	(-1)
#define RA_INTER_RANGE_END		(0x70000000)

struct IMLSegmentPoint
{
	friend struct IMLSegmentInterval;

	sint32 index;
	struct IMLSegment* imlSegment;
	IMLSegmentPoint* next;
	IMLSegmentPoint* prev;

	sint32 GetInstructionIndex() const
	{
		return index;
	}

	void SetInstructionIndex(sint32 index)
	{
		this->index = index;
	}

	void ShiftIfAfter(sint32 instructionIndex, sint32 shiftCount)
	{
		if (!IsPreviousSegment() && !IsNextSegment())
		{
			if (GetInstructionIndex() >= instructionIndex)
				index += shiftCount;
		}
	}

	void DecrementByOneInstruction()
	{
		index--;
	}

	bool IsPreviousSegment() const { return index == RA_INTER_RANGE_START; }
	bool IsNextSegment() const { return index == RA_INTER_RANGE_END; }

	bool operator>(const IMLSegmentPoint& other) const { return index > other.index; }
	bool operator<(const IMLSegmentPoint& other) const { return index < other.index; }
	bool operator==(const IMLSegmentPoint& other) const { return index == other.index; }
	bool operator!=(const IMLSegmentPoint& other) const { return index != other.index; }

	bool operator>(const sint32 other) const { return index > other; }
	bool operator<(const sint32 other) const { return index < other; }
	bool operator<=(const sint32 other) const { return index <= other; }
	bool operator>=(const sint32 other) const { return index >= other; }
};

struct IMLSegmentInterval
{
	IMLSegmentPoint start;
	IMLSegmentPoint end;

	bool ContainsInstructionIndex(sint32 offset) const { return start <= offset && end > offset; }

	bool IsRangeOverlapping(const IMLSegmentInterval& other)
	{
		sint32 r1start = this->start.GetInstructionIndex();
		sint32 r1end = this->end.GetInstructionIndex();
		sint32 r2start = other.start.GetInstructionIndex();
		sint32 r2end = other.end.GetInstructionIndex();
		if (r1start < r2end && r1end > r2start)
			return true;
		if (this->start.IsPreviousSegment() && r1start == r2start)
			return true;
		if (this->end.IsNextSegment() && r1end == r2end)
			return true;
		return false;
	}

	bool ExtendsIntoPreviousSegment() const
	{
		return start.IsPreviousSegment();
	}

	bool ExtendsIntoNextSegment() const
	{
		return end.IsNextSegment();
	}

	bool IsNextSegmentOnly() const
	{
		if(!start.IsNextSegment())
			return false;
		cemu_assert_debug(end.IsNextSegment());
		return true;
	}

	bool IsPreviousSegmentOnly() const
	{
		if (!end.IsPreviousSegment())
			return false;
		cemu_assert_debug(start.IsPreviousSegment());
		return true;
	}

	sint32 GetDistance() const
	{
		return end.GetInstructionIndex() - start.GetInstructionIndex();
	}
};

struct PPCSegmentRegisterAllocatorInfo_t
{
	bool isPartOfProcessedLoop{};
	sint32 lastIterationIndex{};
	struct raLivenessRange* linkedList_allSubranges{};
	std::unordered_map<IMLRegID, struct raLivenessRange*> linkedList_perVirtualRegister;
};

struct IMLSegment
{
	sint32 momentaryIndex{};
	sint32 loopDepth{};
	uint32 ppcAddress{};
	uint32 x64Offset{};
	std::vector<IMLInstruction> imlList;
	IMLSegment* nextSegmentBranchNotTaken{};
	IMLSegment* nextSegmentBranchTaken{};
	bool nextSegmentIsUncertain{};
	std::vector<IMLSegment*> list_prevSegments{};
	IMLSegment* deadCodeEliminationHintSeg{};
	std::vector<IMLSegment*> list_deadCodeHintBy{};
	bool isEnterable{};
	uint32 enterPPCAddress{};
	PPCSegmentRegisterAllocatorInfo_t raInfo{};
	void SetEnterable(uint32 enterAddress);
	void SetLinkBranchNotTaken(IMLSegment* imlSegmentDst);
	void SetLinkBranchTaken(IMLSegment* imlSegmentDst);

	IMLSegment* GetBranchTaken()
	{
		return nextSegmentBranchTaken;
	}

	IMLSegment* GetBranchNotTaken()
	{
		return nextSegmentBranchNotTaken;
	}

	void SetNextSegmentForOverwriteHints(IMLSegment* seg)
	{
		cemu_assert_debug(!deadCodeEliminationHintSeg);
		deadCodeEliminationHintSeg = seg;
		if (seg)
			seg->list_deadCodeHintBy.push_back(this);
	}

	IMLInstruction* AppendInstruction();

	bool HasSuffixInstruction() const;
	sint32 GetSuffixInstructionIndex() const;
	IMLInstruction* GetLastInstruction();

	IMLSegmentPoint* segmentPointList{};
};

void IMLSegment_SetLinkBranchNotTaken(IMLSegment* imlSegmentSrc, IMLSegment* imlSegmentDst);
void IMLSegment_SetLinkBranchTaken(IMLSegment* imlSegmentSrc, IMLSegment* imlSegmentDst);
void IMLSegment_RelinkInputSegment(IMLSegment* imlSegmentOrig, IMLSegment* imlSegmentNew);
void IMLSegment_RemoveLink(IMLSegment* imlSegmentSrc, IMLSegment* imlSegmentDst);

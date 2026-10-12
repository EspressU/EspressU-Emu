#pragma once
#include "IMLRegisterAllocator.h"

struct raLivenessSubrangeLink
{
	struct raLivenessRange* prev;
	struct raLivenessRange* next;
};

struct raInstructionEdge
{
	friend struct raInterval;
public:
	raInstructionEdge()
	{
		index = 0;
	}

	raInstructionEdge(sint32 instructionIndex, bool isInputEdge)
	{
		Set(instructionIndex, isInputEdge);
	}

	void Set(sint32 instructionIndex, bool isInputEdge)
	{
		if(instructionIndex == RA_INTER_RANGE_START || instructionIndex == RA_INTER_RANGE_END)
		{
			index = instructionIndex;
			return;
		}
		index = instructionIndex * 2 + (isInputEdge ? 0 : 1);
		cemu_assert_debug(index >= 0 && index < 0x100000*2);
	}

	void SetRaw(sint32 index)
	{
		this->index = index;
		cemu_assert_debug(index == RA_INTER_RANGE_START || index == RA_INTER_RANGE_END || (index >= 0 && index < 0x100000*2));
	}

	std::string GetDebugString()
	{
		if(index == RA_INTER_RANGE_START)
			return "RA_START";
		else if(index == RA_INTER_RANGE_END)
			return "RA_END";
		std::string str = fmt::format("{}", GetInstructionIndex());
		if(IsOnInputEdge())
			str += "i";
		else if(IsOnOutputEdge())
			str += "o";
		return str;
	}

	sint32 GetInstructionIndex() const
	{
		cemu_assert_debug(index != RA_INTER_RANGE_START && index != RA_INTER_RANGE_END);
		return index >> 1;
	}

	sint32 GetInstructionIndexEx() const
	{
		if(index == RA_INTER_RANGE_START || index == RA_INTER_RANGE_END)
			return index;
		return index >> 1;
	}

	sint32 GetRaw() const
	{
		return index;
	}

	bool IsOnInputEdge() const
	{
		cemu_assert_debug(index != RA_INTER_RANGE_START && index != RA_INTER_RANGE_END);
		return (index&1) == 0;
	}

	bool IsOnOutputEdge() const
	{
		cemu_assert_debug(index != RA_INTER_RANGE_START && index != RA_INTER_RANGE_END);
		return (index&1) != 0;
	}

	bool ConnectsToPreviousSegment() const
	{
		return index == RA_INTER_RANGE_START;
	}

	bool ConnectsToNextSegment() const
	{
		return index == RA_INTER_RANGE_END;
	}

	bool IsInstructionIndex() const
	{
		return index != RA_INTER_RANGE_START && index != RA_INTER_RANGE_END;
	}

	bool operator>(const raInstructionEdge& other) const
	{
		return index > other.index;
	}
	bool operator<(const raInstructionEdge& other) const
	{
		return index < other.index;
	}
	bool operator<=(const raInstructionEdge& other) const
	{
		return index <= other.index;
	}
	bool operator>=(const raInstructionEdge& other) const
	{
		return index >= other.index;
	}
	bool operator==(const raInstructionEdge& other) const
	{
		return index == other.index;
	}

	raInstructionEdge operator+(sint32 offset) const
	{
		cemu_assert_debug(IsInstructionIndex());
		cemu_assert_debug(offset >= 0 && offset < RA_INTER_RANGE_END);
		raInstructionEdge edge;
		edge.index = index + offset;
		return edge;
	}

	raInstructionEdge operator-(sint32 offset) const
	{
		cemu_assert_debug(IsInstructionIndex());
		cemu_assert_debug(offset >= 0 && offset < RA_INTER_RANGE_END);
		raInstructionEdge edge;
		edge.index = index - offset;
		return edge;
	}

	raInstructionEdge& operator++()
	{
		cemu_assert_debug(IsInstructionIndex());
		index++;
		return *this;
	}

private:
	sint32 index;

};

struct raAccessLocation
{
	raAccessLocation(raInstructionEdge pos) : pos(pos) {}

	bool IsRead() const
	{
		return pos.IsOnInputEdge();
	}

	bool IsWrite() const
	{
		return pos.IsOnOutputEdge();
	}

	raInstructionEdge pos;
};

struct raInterval
{
	raInterval()
	{

	}

	raInterval(raInstructionEdge start, raInstructionEdge end)
	{
		SetInterval(start, end);
	}

	void SetInterval(sint32 start, bool isStartOnInput, sint32 end, bool isEndOnOutput)
	{
		this->start.Set(start, isStartOnInput);
		this->end.Set(end, !isEndOnOutput);
	}

	void SetInterval(raInstructionEdge start, raInstructionEdge end)
	{
		cemu_assert_debug(start <= end);
		this->start = start;
		this->end = end;
	}

	void SetStart(const raInstructionEdge& edge)
	{
		start = edge;
	}

	void SetEnd(const raInstructionEdge& edge)
	{
		end = edge;
	}

	sint32 GetStartIndex() const
	{
		return start.GetInstructionIndex();
	}

	sint32 GetEndIndex() const
	{
		return end.GetInstructionIndex();
	}

	bool ExtendsPreviousSegment() const
	{
		return start.ConnectsToPreviousSegment();
	}

	bool ExtendsIntoNextSegment() const
	{
		return end.ConnectsToNextSegment();
	}

	bool IsNextSegmentOnly() const
	{
		return start.ConnectsToNextSegment() && end.ConnectsToNextSegment();
	}

	bool IsPreviousSegmentOnly() const
	{
		return start.ConnectsToPreviousSegment() && end.ConnectsToPreviousSegment();
	}

	bool IsLocal() const
	{
		return start.GetRaw() > RA_INTER_RANGE_START && end.GetRaw() < RA_INTER_RANGE_END;
	}

	bool ContainsInstructionIndex(sint32 instructionIndex) const
	{
		cemu_assert_debug(instructionIndex != RA_INTER_RANGE_START && instructionIndex != RA_INTER_RANGE_END);
		return instructionIndex >= start.GetInstructionIndexEx() && instructionIndex <= end.GetInstructionIndexEx();
	}

	bool ContainsInstructionIndexEx(sint32 instructionIndex) const
	{
		if(instructionIndex == RA_INTER_RANGE_START)
			return start.ConnectsToPreviousSegment();
		if(instructionIndex == RA_INTER_RANGE_END)
			return end.ConnectsToNextSegment();
		return instructionIndex >= start.GetInstructionIndexEx() && instructionIndex <= end.GetInstructionIndexEx();
	}

	bool ContainsEdge(const raInstructionEdge& edge) const
	{
		return edge >= start && edge <= end;
	}

	bool ContainsWholeInterval(const raInterval& other) const
	{
		return other.start >= start && other.end <= end;
	}

	bool IsOverlapping(const raInterval& other) const
	{
		return start <= other.end && end >= other.start;
	}

	sint32 GetPreciseDistance()
	{
		cemu_assert_debug(!start.ConnectsToNextSegment());
		if(start == end)
			return 1;
		cemu_assert_debug(!end.ConnectsToPreviousSegment() && !end.ConnectsToNextSegment());
		if(start.ConnectsToPreviousSegment())
			return end.GetRaw() + 1;

		return end.GetRaw() - start.GetRaw() + 1;
	}

	raInstructionEdge start;
	raInstructionEdge end;
};

struct raFixedRegRequirement
{
	raInstructionEdge pos;
	IMLPhysRegisterSet allowedReg;
};

struct raLivenessRange
{
	IMLSegment* imlSegment;
	raInterval interval;

	bool _noLoad;
	bool hasStore;
	bool hasStoreDelayed;
	raLivenessRange* subrangeBranchTaken;
	raLivenessRange* subrangeBranchNotTaken;
	boost::container::small_vector<raLivenessRange*, 4> previousRanges;
	uint32 lastIterationIndex;
	std::vector<raAccessLocation> list_accessLocations;
	std::vector<raFixedRegRequirement> list_fixedRegRequirements;
	raLivenessSubrangeLink link_sameVirtualRegister;
	raLivenessSubrangeLink link_allSegmentRanges;
	IMLRegID virtualRegister;
	IMLName name;
	IMLPhysReg physicalRegister;

	boost::container::small_vector<raLivenessRange*, 128> GetAllSubrangesInCluster();
	bool GetAllowedRegistersEx(IMLPhysRegisterSet& allowedRegisters);
	IMLPhysRegisterSet GetAllowedRegisters(IMLPhysRegisterSet regPool);

	IMLRegID GetVirtualRegister() const;
	sint32 GetPhysicalRegister() const;
	bool HasPhysicalRegister() const { return physicalRegister >= 0; }
	IMLName GetName() const;
	void SetPhysicalRegister(IMLPhysReg physicalRegister);
	void SetPhysicalRegisterForCluster(IMLPhysReg physicalRegister);
	void UnsetPhysicalRegister() { physicalRegister = -1; }

  private:
	void GetAllowedRegistersExRecursive(raLivenessRange* range, uint32 iterationIndex, IMLPhysRegisterSet& allowedRegs);
};

raLivenessRange* IMLRA_CreateRange(ppcImlGenContext_t* ppcImlGenContext, IMLSegment* imlSegment, IMLRegID virtualRegister, IMLName name, raInstructionEdge startPosition, raInstructionEdge endPosition);
void IMLRA_DeleteRange(ppcImlGenContext_t* ppcImlGenContext, raLivenessRange* subrange);
void IMLRA_DeleteAllRanges(ppcImlGenContext_t* ppcImlGenContext);

void IMLRA_ExplodeRangeCluster(ppcImlGenContext_t* ppcImlGenContext, raLivenessRange* originRange);

void IMLRA_MergeSubranges(ppcImlGenContext_t* ppcImlGenContext, raLivenessRange* subrange, raLivenessRange* absorbedSubrange);

raLivenessRange* IMLRA_SplitRange(ppcImlGenContext_t* ppcImlGenContext, raLivenessRange*& subrange, raInstructionEdge splitPosition, bool trimToUsage = false);

void PPCRecRA_debugValidateSubrange(raLivenessRange* subrange);

sint32 IMLRA_GetSegmentReadWriteCost(IMLSegment* imlSegment);
sint32 IMLRA_CalculateAdditionalCostOfRangeExplode(raLivenessRange* subrange);
sint32 IMLRA_CalculateAdditionalCostAfterSplit(raLivenessRange* subrange, raInstructionEdge splitPosition);
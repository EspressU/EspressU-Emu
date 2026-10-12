#include "util/Zir/Core/IR.h"
#include "util/Zir/Core/ZirUtility.h"
#include "util/Zir/Core/ZpIRPasses.h"
#include "util/Zir/Core/ZpIRDebug.h"

namespace ZirPass
{
	using namespace ZpIR;

	RALivenessRange_t::RALivenessRange_t(RABlock_t* block, IRReg irReg, sint32 start, sint32 end, DataType irDataType) : m_block(block), m_irReg(irReg), m_irDataType(irDataType)
	{
		block->livenessRanges.emplace(irReg, this);
		m_startIndex = start;
		m_endIndex = end;
		for (auto& itr : block->livenessRanges)
		{
			RALivenessRange_t* itrRange = itr.second;
			if (start < itrRange->m_endIndex && end >= itrRange->m_startIndex)
			{
				m_overlappingRanges.emplace_back(itrRange);
				itrRange->m_overlappingRanges.emplace_back(this);
			}
		}
		block->unassignedRanges.emplace(this);
	}

	RALivenessRange_t::~RALivenessRange_t()
	{
		for (auto& itr : m_overlappingRanges)
		{
			RALivenessRange_t* overlappedRange = itr;
			overlappedRange->m_overlappingRanges.erase(std::remove(overlappedRange->m_overlappingRanges.begin(), overlappedRange->m_overlappingRanges.end(), overlappedRange), overlappedRange->m_overlappingRanges.end());
		}
		m_overlappingRanges.clear();
		assert_dbg();
	}

	void RALivenessRange_t::setStart(sint32 startIndex)
	{
		m_startIndex = startIndex;
		assert_dbg();
	}

	void RALivenessRange_t::setEnd(sint32 endIndex)
	{
		if (endIndex > m_endIndex)
		{
			for (auto& itr : m_block->livenessRanges)
			{
				RALivenessRange_t* itrRange = itr.second;
				if(itrRange->isOverlapping(this))
					continue;
				if(itrRange == this)
					continue;
				if (itrRange->isOverlapping(m_startIndex, endIndex))
				{
					m_overlappingRanges.emplace_back(itrRange);
					itrRange->m_overlappingRanges.emplace_back(this);
				}
			}
		}
		else if (endIndex < m_endIndex)
		{
			cemu_assert_suspicious();
		}
		m_endIndex = endIndex;
	}

	void RALivenessRange_t::assignPhysicalRegister(ZpIRPhysicalReg physReg)
	{
		if (m_location != LOCATION::UNASSIGNED)
			cemu_assert_suspicious();
		m_location = LOCATION::PHYSICAL_REGISTER;
		m_physicalRegister = physReg;
		auto itr = m_block->unassignedRanges.find(this);
		if (itr == m_block->unassignedRanges.end())
			cemu_assert_suspicious();
		if (*itr != this)
			cemu_assert_suspicious();
		m_block->unassignedRanges.erase(itr);
	}

	void RARegular::prepareRABlocks()
	{
		auto& irBasicBlocks = m_irFunction->m_basicBlocks;

		m_raBlockArray.resize(m_irFunction->m_basicBlocks.size());
	}

	void RARegular::generateLivenessRanges()
	{
		auto& irBasicBlocks = m_irFunction->m_basicBlocks;
		for (uint32 basicBlockIndex = 0; basicBlockIndex < (uint32)irBasicBlocks.size(); basicBlockIndex++)
		{
			auto& blockItr = irBasicBlocks[basicBlockIndex];
			RABlock_t* raBlock = m_raBlockArray.data() + basicBlockIndex;
			std::unordered_map<IRReg, RALivenessRange_t*>& blockRanges = raBlock->livenessRanges;
			for (auto& regImport : blockItr->m_imports)
			{
				new RALivenessRange_t(raBlock, regImport.reg, -1, -1, blockItr->m_regs[(uint16)regImport.reg].type);
			}
			IR::__InsBase* ins = blockItr->m_instructionFirst;
			size_t i = 0;
			while(ins)
			{
				ZpIRCmdUtil::forEachAccessedReg(*blockItr, ins,
					[&blockRanges, i, raBlock](IRReg readReg)
					{
						if (readReg >= 0x8000)
							cemu_assert_suspicious();
						auto livenessRange = blockRanges.find(readReg);
						if (livenessRange == blockRanges.end())
							cemu_assert_suspicious();
						livenessRange->second->setEnd((sint32)i);
					},
					[&blockRanges, i, raBlock, blockItr](IRReg writtenReg)
					{
						if (writtenReg >= 0x8000)
							cemu_assert_suspicious();
						auto livenessRange = blockRanges.find(writtenReg);
						if (livenessRange != blockRanges.end())
							cemu_assert_suspicious();
						new RALivenessRange_t(raBlock, writtenReg, (sint32)i, (sint32)i, blockItr->m_regs[(uint16)writtenReg].type);
					});
				i++;
				ins = ins->next;
			}
			for (auto& regExport : blockItr->m_exports)
			{
				auto livenessRange = blockRanges.find(regExport.reg);
				if (livenessRange == blockRanges.end())
					cemu_assert_suspicious();
				cemu_assert_unimplemented();
			}
		}
		std::unordered_map<LocationSymbolName, RALivenessRange_t*> listExportedRanges;
		for (uint32 basicBlockIndex = 0; basicBlockIndex < (uint32)irBasicBlocks.size(); basicBlockIndex++)
		{
			auto& blockItr = irBasicBlocks[basicBlockIndex];
			std::unordered_map<IRReg, RALivenessRange_t*>& localRanges = m_raBlockArray[basicBlockIndex].livenessRanges;
			listExportedRanges.clear();
			for (auto& regExport : blockItr->m_exports)
			{
				auto livenessRange = localRanges.find(regExport.reg);
				if (livenessRange == localRanges.end())
					assert_dbg();
				listExportedRanges.emplace(regExport.name, livenessRange->second);
			}
			if (blockItr->m_branchTaken)
			{
				ZpIRBasicBlock* successorBlock = blockItr->m_branchTaken;
				std::unordered_map<IRReg, RALivenessRange_t*>& successorRanges = localRanges = m_raBlockArray[basicBlockIndex].livenessRanges;
				for (auto& regImport : successorBlock->m_exports)
				{
					auto livenessRange = successorRanges.find(regImport.reg);
					if (livenessRange == successorRanges.end())
						assert_dbg();
					auto connectedSourceRange = listExportedRanges.find(regImport.name);
					if (connectedSourceRange == listExportedRanges.end())
						assert_dbg();
					livenessRange->second->addSourceFromPreviousBlock(connectedSourceRange->second);
				}
			}
		}
	}

	void RARegular::assignPhysicalRegistersForBlock(RABlock_t& raBlock)
	{
		debugPrint(raBlock);
		std::vector<sint32> physRegCandidates;
		physRegCandidates.reserve(32);
		while (!raBlock.unassignedRanges.empty())
		{
			RALivenessRange_t* range = *raBlock.unassignedRanges.begin();
			std::span<sint32> physReg = extGetSuitablePhysicalRegisters(range->m_irDataType);

			physRegCandidates.clear();
			for (auto& r : physReg)
				physRegCandidates.emplace_back(r);

			filterCandidates(physRegCandidates, range);
			if (!physRegCandidates.empty())
			{
				ZpIRPhysicalReg physRegister = extPickPreferredRegister(physRegCandidates);
				range->assignPhysicalRegister(physRegister);
				continue;
			}

			assert_dbg();

			assert_dbg();

		}

		printf("Assigned:\n");
		debugPrint(raBlock);
	}

	void RARegular::assignPhysicalRegisters()
	{

		for (auto& raBlockInfo : m_raBlockArray)
			assignPhysicalRegistersForBlock(raBlockInfo);
	}

	void RARegular::rewrite()
	{
		for (size_t i = 0; i < m_raBlockArray.size(); i++)
			rewriteBlock(*m_irFunction->m_basicBlocks[i], m_raBlockArray[i]);
	}

	void RARegular::rewriteBlock(ZpIRBasicBlock& basicBlock, RABlock_t& raBlock)
	{
		assert_dbg();

		basicBlock.m_imports.clear();
		basicBlock.m_imports.shrink_to_fit();
		basicBlock.m_exports.clear();
		basicBlock.m_exports.shrink_to_fit();
		basicBlock.m_regs.clear();
		basicBlock.m_regs.shrink_to_fit();
	}

}

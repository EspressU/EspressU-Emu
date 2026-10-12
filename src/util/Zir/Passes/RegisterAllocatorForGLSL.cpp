#include "util/Zir/Core/IR.h"
#include "util/Zir/Core/ZirUtility.h"
#include "util/Zir/Core/ZpIRPasses.h"
#include "util/Zir/Core/ZpIRDebug.h"

namespace ZirPass
{

	void RegisterAllocatorForGLSL::assignPhysicalRegisters()
	{
		if (m_irFunction->m_basicBlocks.size() != 1)
			cemu_assert_unimplemented();

		for (auto& itr : m_irFunction->m_basicBlocks)
			assignPhysicalRegistersForBlock(itr);
	}

	void RegisterAllocatorForGLSL::assignPhysicalRegistersForBlock(ZpIR::ZpIRBasicBlock* basicBlock)
	{
		for (auto& itr : basicBlock->m_imports)
		{
			assert_dbg();

		}
		for (auto& itr : basicBlock->m_regs)
		{
			if (itr.type != ZpIR::DataType::NONE && !itr.hasAssignedPhysicalRegister())
			{
				if (itr.type == ZpIR::DataType::F32)
					itr.assignPhysicalRegister(MakePhysReg(PHYS_REG_TYPE::F32, m_physicalRegisterCounterF32++));
				else if (itr.type == ZpIR::DataType::S32)
					itr.assignPhysicalRegister(MakePhysReg(PHYS_REG_TYPE::S32, m_physicalRegisterCounterS32++));
				else if (itr.type == ZpIR::DataType::U32)
					itr.assignPhysicalRegister(MakePhysReg(PHYS_REG_TYPE::U32, m_physicalRegisterCounterU32++));
				else
				{
					cemu_assert_debug(false);
				}
			}
		}

	}

	std::string RegisterAllocatorForGLSL::DebugPrintHelper_getPhysRegisterName(ZpIR::ZpIRBasicBlock* block, ZpIR::ZpIRPhysicalReg r)
	{
		std::string s;
		uint32 regIndex = GetPhysRegIndex(r);

		if (IsPhysRegTypeF32(r))
			s = fmt::format("r{}f", regIndex);
		else if (IsPhysRegTypeU32(r))
			s = fmt::format("r{}u", regIndex);
		else if (IsPhysRegTypeS32(r))
			s = fmt::format("r{}i", regIndex);
		return s;
	}

}
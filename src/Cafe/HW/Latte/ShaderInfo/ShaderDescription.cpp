#include "Cafe/HW/Latte/ShaderInfo/ShaderInfo.h"
#include "Cafe/HW/Latte/ISA/LatteInstructions.h"

namespace Latte
{
	bool ShaderDescription::analyzeShaderCode(void* shaderProgram, size_t sizeInBytes, LatteConst::ShaderType shaderType)
	{
		assert_dbg();

		const LatteCFInstruction* cfCode = (const LatteCFInstruction*)shaderProgram;
		const size_t cfMaxCount = sizeInBytes / 8;

		size_t cfIndex = 0;
		while (cfIndex < cfMaxCount)
		{
			const LatteCFInstruction* baseInstr = cfCode + cfIndex;
			cfIndex++;
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
				assert_dbg();
				isALU = true;
			}
			else if (const auto cfInstr = baseInstr->getParserIfOpcodeMatch<LatteCFInstruction_EXPORT_IMPORT>())
			{
				assert_dbg();
			}
			else
			{
				cemuLog_log(LogType::Force, "ShaderDescription::analyzeShaderCode(): Missing implementation for CF opcode 0x%02x\n", baseInstr->getField_Opcode());
				cemu_assert_debug(false);
			}
			if (!isALU && baseInstr->getField_END_OF_PROGRAM())
				break;
		}

		return true;
	}
};

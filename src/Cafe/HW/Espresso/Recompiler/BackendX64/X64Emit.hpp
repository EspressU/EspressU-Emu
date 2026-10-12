

template<uint8 op0, bool rex64Bit = false>
class x64_opc_1byte
{
public:
	static void emitBytes(x64GenContext_t* x64GenContext)
	{
		x64Gen_writeU8(x64GenContext, op0);
	}

	static constexpr bool isRevOrder()
	{
		return false;
	}

	static constexpr bool hasRex64BitPrefix()
	{
		return rex64Bit;
	}
};

template<uint8 op0, bool rex64Bit = false>
class x64_opc_1byte_rev
{
public:
	static void emitBytes(x64GenContext_t* x64GenContext)
	{
		x64Gen_writeU8(x64GenContext, op0);
	}

	static constexpr bool isRevOrder()
	{
		return true;
	}

	static constexpr bool hasRex64BitPrefix()
	{
		return rex64Bit;
	}
};

template<uint8 op0, uint8 op1, bool rex64Bit = false>
class x64_opc_2byte
{
public:
	static void emitBytes(x64GenContext_t* x64GenContext)
	{
		x64Gen_writeU8(x64GenContext, op0);
		x64Gen_writeU8(x64GenContext, op1);
	}

	static constexpr bool isRevOrder()
	{
		return false;
	}

	static constexpr bool hasRex64BitPrefix()
	{
		return rex64Bit;
	}
};

enum class MODRM_OPR_TYPE
{
	REG,
	MEM
};

class x64MODRM_opr_reg64
{
public:
	x64MODRM_opr_reg64(uint8 reg)
	{
		this->reg = reg;
	}

	static constexpr MODRM_OPR_TYPE getType()
	{
		return MODRM_OPR_TYPE::REG;
	}

	const uint8 getReg() const
	{
		return reg;
	}

private:
	uint8 reg;
};

class x64MODRM_opr_memReg64
{
public:
	x64MODRM_opr_memReg64(uint8 reg)
	{
		this->reg = reg;
		this->offset = 0;
	}

	x64MODRM_opr_memReg64(uint8 reg, sint32 offset)
	{
		this->reg = reg;
		this->offset = offset;
	}

	static constexpr MODRM_OPR_TYPE getType()
	{
		return MODRM_OPR_TYPE::MEM;
	}

	const uint8 getBaseReg() const
	{
		return reg;
	}

	const uint32 getOffset() const
	{
		return (uint32)offset;
	}

	static constexpr bool hasBaseReg()
	{
		return true;
	}

	static constexpr bool hasIndexReg()
	{
		return false;
	}
private:
	uint8 reg;
	sint32 offset;
};

class x64MODRM_opr_memRegPlusReg
{
public:
	x64MODRM_opr_memRegPlusReg(uint8 regBase, uint8 regIndex)
	{
		if ((regIndex & 7) == 4)
		{
			std::swap(regBase, regIndex);
			cemu_assert((regBase & 7) != 4);
		}
		this->regBase = regBase;
		this->regIndex = regIndex;
		this->offset = 0;
	}

	x64MODRM_opr_memRegPlusReg(uint8 regBase, uint8 regIndex, sint32 offset)
	{
		if ((regIndex & 7) == 4)
		{
			std::swap(regBase, regIndex);
			cemu_assert((regIndex & 7) != 4);
		}
		this->regBase = regBase;
		this->regIndex = regIndex;
		this->offset = offset;
	}

	static constexpr MODRM_OPR_TYPE getType()
	{
return MODRM_OPR_TYPE::MEM;
	}

	const uint8 getBaseReg() const
	{
		return regBase;
	}

	const uint8 getIndexReg()
	{
		return regIndex;
	}

	const uint32 getOffset() const
	{
		return (uint32)offset;
	}

	static constexpr bool hasBaseReg()
	{
		return true;
	}

	static constexpr bool hasIndexReg()
	{
		return true;
	}
private:
	uint8 regBase;
	uint8 regIndex;
	sint32 offset;
};

template<class opcodeBytes, typename TA, typename TB>
void _x64Gen_writeMODRM_internal(x64GenContext_t* x64GenContext, TA opA, TB opB)
{
	static_assert(TA::getType() == MODRM_OPR_TYPE::REG);
	if constexpr (TA::getType() == MODRM_OPR_TYPE::REG && TB::getType() == MODRM_OPR_TYPE::REG)
	{
		if (opA.getReg() & 8 || opB.getReg() & 8 || opcodeBytes::hasRex64BitPrefix())
		{
			x64Gen_writeU8(x64GenContext, 0x40 | ((opA.getReg() & 8) ? (1 << 2) : 0) | ((opB.getReg() & 8) ? (1 << 0) : 0) | (opcodeBytes::hasRex64BitPrefix() ? (1 << 3) : 0));
		}
	}
	else if constexpr (TA::getType() == MODRM_OPR_TYPE::REG && TB::getType() == MODRM_OPR_TYPE::MEM)
	{
		if constexpr (opB.hasBaseReg() && opB.hasIndexReg())
		{
			if (opA.getReg() & 8 || opB.getBaseReg() & 8 || opB.getIndexReg() & 8 || opcodeBytes::hasRex64BitPrefix())
			{
				x64Gen_writeU8(x64GenContext, 0x40 | ((opA.getReg() & 8) ? (1 << 2) : 0) | ((opB.getBaseReg() & 8) ? (1 << 0) : 0) | ((opB.getIndexReg() & 8) ? (1 << 1) : 0) | (opcodeBytes::hasRex64BitPrefix() ? (1 << 3) : 0));
			}
		}
		else if constexpr (opB.hasBaseReg())
		{
			if (opA.getReg() & 8 || opB.getBaseReg() & 8 || opcodeBytes::hasRex64BitPrefix())
			{
				x64Gen_writeU8(x64GenContext, 0x40 | ((opA.getReg() & 8) ? (1 << 2) : 0) | ((opB.getBaseReg() & 8) ? (1 << 0) : 0) | (opcodeBytes::hasRex64BitPrefix() ? (1 << 3) : 0));
			}
		}
		else
		{
			if (opA.getReg() & 8 || opcodeBytes::hasRex64BitPrefix())
			{
				x64Gen_writeU8(x64GenContext, 0x40 | ((opA.getReg() & 8) ? (1 << 2) : 0) | (opcodeBytes::hasRex64BitPrefix() ? (1 << 3) : 0));
			}
		}
	}
	opcodeBytes::emitBytes(x64GenContext);
	if constexpr (TA::getType() == MODRM_OPR_TYPE::REG && TB::getType() == MODRM_OPR_TYPE::REG)
	{
		x64Gen_writeU8(x64GenContext, 0xC0 + (opB.getReg() & 7) + ((opA.getReg() & 7) << 3));
	}
	else if constexpr (TA::getType() == MODRM_OPR_TYPE::REG && TB::getType() == MODRM_OPR_TYPE::MEM)
	{
		if constexpr (TB::hasBaseReg() == false)
		{
			cemu_assert(false);
		}
		else if constexpr (TB::hasIndexReg())
		{
			cemu_assert((opB.getIndexReg() & 7) != 4);
			const uint32 offset = opB.getOffset();
			if (offset == 0 && (opB.getBaseReg() & 7) != 5)
			{
				x64Gen_writeU8(x64GenContext, 0x00 + (4) + ((opA.getReg() & 7) << 3));
				x64Gen_writeU8(x64GenContext, ((opB.getIndexReg()&7) << 3) + (opB.getBaseReg() & 7));
			}
			else if (offset == (uint32)(sint32)(sint8)offset)
			{
				x64Gen_writeU8(x64GenContext, 0x40 + (4) + ((opA.getReg() & 7) << 3));
				x64Gen_writeU8(x64GenContext, ((opB.getIndexReg() & 7) << 3) + (opB.getBaseReg() & 7));
				x64Gen_writeU8(x64GenContext, (uint8)offset);
			}
			else
			{
				x64Gen_writeU8(x64GenContext, 0x80 + (4) + ((opA.getReg() & 7) << 3));
				x64Gen_writeU8(x64GenContext, ((opB.getIndexReg() & 7) << 3) + (opB.getBaseReg() & 7));
				x64Gen_writeU32(x64GenContext, (uint32)offset);
			}
		}
		else
		{
			const uint32 offset = opB.getOffset();
			if (offset == 0 && (opB.getBaseReg() & 7) != 5)
			{
				if ((opB.getBaseReg() & 7) == 4)
				{
					x64Gen_writeU8(x64GenContext, 0x00 + (4) + ((opA.getReg() & 7) << 3));
					x64Gen_writeU8(x64GenContext, (4 << 3) + (opB.getBaseReg() & 7));
				}
				else
				{
					x64Gen_writeU8(x64GenContext, 0x00 + (opB.getBaseReg() & 7) + ((opA.getReg() & 7) << 3));
				}
			}
			else if (offset == (uint32)(sint32)(sint8)offset)
			{
				if ((opB.getBaseReg() & 7) == 4)
				{
					x64Gen_writeU8(x64GenContext, 0x40 + (4) + ((opA.getReg() & 7) << 3));
					x64Gen_writeU8(x64GenContext, (4 << 3) + (opB.getBaseReg() & 7));
				}
				else
				{
					x64Gen_writeU8(x64GenContext, 0x40 + (opB.getBaseReg() & 7) + ((opA.getReg() & 7) << 3));
				}
				x64Gen_writeU8(x64GenContext, (uint8)offset);
			}
			else
			{
				if ((opB.getBaseReg() & 7) == 4)
				{
					x64Gen_writeU8(x64GenContext, 0x80 + (4) + ((opA.getReg() & 7) << 3));
					x64Gen_writeU8(x64GenContext, (4 << 3) + (opB.getBaseReg() & 7));
				}
				else
				{
					x64Gen_writeU8(x64GenContext, 0x80 + (opB.getBaseReg() & 7) + ((opA.getReg() & 7) << 3));
				}
				x64Gen_writeU32(x64GenContext, (uint32)offset);
			}
		}
	}
	else
	{
		assert_dbg();
	}
}

template<class opcodeBytes, typename TA, typename TB>
void x64Gen_writeMODRM_dyn(x64GenContext_t* x64GenContext, TA opLeft, TB opRight)
{
	if constexpr (opcodeBytes::isRevOrder())
		_x64Gen_writeMODRM_internal<opcodeBytes, TB, TA>(x64GenContext, opRight, opLeft);
	else
		_x64Gen_writeMODRM_internal<opcodeBytes, TA, TB>(x64GenContext, opLeft, opRight);
}
#pragma once

using IMLRegID = uint16;
using IMLPhysReg = sint32;

enum class IMLRegFormat : uint8
{
	INVALID_FORMAT,
	I64,
	I32,
	I16,
	I8,
	F64,
	F32,
	TYPE_COUNT,
};

class IMLReg
{
public:
	IMLReg()
	{
		m_raw = 0;
	}

	IMLReg(IMLRegFormat baseRegFormat, IMLRegFormat regFormat, uint8 viewOffset, IMLRegID regId)
	{
		m_raw = 0;
		m_raw |= ((uint8)baseRegFormat << 28);
		m_raw |= ((uint8)regFormat << 24);
		m_raw |= (uint32)regId;
	}

	IMLReg(IMLReg&& baseReg, IMLRegFormat viewFormat, uint8 viewOffset, IMLRegID regId)
	{
		DEBUG_BREAK;
	}

	IMLReg(const IMLReg& other) : m_raw(other.m_raw) {}

	IMLRegFormat GetBaseFormat() const
	{
		return (IMLRegFormat)((m_raw >> 28) & 0xF);
	}

	IMLRegFormat GetRegFormat() const
	{
		return (IMLRegFormat)((m_raw >> 24) & 0xF);
	}

	IMLRegID GetRegID() const
	{
		cemu_assert_debug(GetBaseFormat() != IMLRegFormat::INVALID_FORMAT);
		cemu_assert_debug(GetRegFormat() != IMLRegFormat::INVALID_FORMAT);
		return (IMLRegID)(m_raw & 0xFFFF);
	}

	void SetRegID(IMLRegID regId)
	{
		cemu_assert_debug(regId <= 0xFFFF);
		m_raw &= ~0xFFFF;
		m_raw |= (uint32)regId;
	}

	bool IsInvalid() const
	{
		return GetBaseFormat() == IMLRegFormat::INVALID_FORMAT;
	}

	bool IsValid() const
	{
		return GetBaseFormat() != IMLRegFormat::INVALID_FORMAT;
	}

	bool IsValidAndSameRegID(IMLRegID regId) const
	{
		return IsValid() && GetRegID() == regId;
	}

	bool operator==(const IMLReg& other) const
	{
		return m_raw == other.m_raw;
	}

private:
	uint32 m_raw;
};

static const IMLReg IMLREG_INVALID(IMLRegFormat::INVALID_FORMAT, IMLRegFormat::INVALID_FORMAT, 0, 0);
static const IMLRegID IMLRegID_INVALID(0xFFFF);

using IMLName = uint32;

enum
{
	PPCREC_IML_OP_ASSIGN,
	PPCREC_IML_OP_ENDIAN_SWAP,
	PPCREC_IML_OP_MULTIPLY_SIGNED,
	PPCREC_IML_OP_MULTIPLY_HIGH_UNSIGNED,
	PPCREC_IML_OP_MULTIPLY_HIGH_SIGNED,
	PPCREC_IML_OP_DIVIDE_SIGNED,
	PPCREC_IML_OP_DIVIDE_UNSIGNED,

	PPCREC_IML_OP_OR,
	PPCREC_IML_OP_AND,
	PPCREC_IML_OP_XOR,
	PPCREC_IML_OP_LEFT_ROTATE,
	PPCREC_IML_OP_LEFT_SHIFT,
	PPCREC_IML_OP_RIGHT_SHIFT_U,
	PPCREC_IML_OP_RIGHT_SHIFT_S,
	PPCREC_IML_OP_SLW,
	PPCREC_IML_OP_SRW,
	PPCREC_IML_OP_CNTLZW,
	PPCREC_IML_OP_FPR_ASSIGN,
	PPCREC_IML_OP_FPR_LOAD_ONE,
	PPCREC_IML_OP_FPR_ADD,
	PPCREC_IML_OP_FPR_SUB,
	PPCREC_IML_OP_FPR_MULTIPLY,
	PPCREC_IML_OP_FPR_DIVIDE,
	PPCREC_IML_OP_FPR_EXPAND_F32_TO_F64,
	PPCREC_IML_OP_FPR_NEGATE,
	PPCREC_IML_OP_FPR_ABS,
	PPCREC_IML_OP_FPR_NEGATIVE_ABS,
	PPCREC_IML_OP_FPR_ROUND_TO_SINGLE_PRECISION_BOTTOM,
	PPCREC_IML_OP_FPR_FCTIWZ,
	PPCREC_IML_OP_FPR_SELECT,
	PPCREC_IML_OP_FPR_INT_TO_FLOAT,
	PPCREC_IML_OP_FPR_FLOAT_TO_INT,

	PPCREC_IML_OP_FPR_BITCAST_INT_TO_FLOAT,

	PPCREC_IML_OP_ADD,
	PPCREC_IML_OP_SUB,

	PPCREC_IML_OP_NOT,
	PPCREC_IML_OP_NEG,
	PPCREC_IML_OP_ASSIGN_S16_TO_S32,
	PPCREC_IML_OP_ASSIGN_S8_TO_S32,

	PPCREC_IML_OP_ADD_WITH_CARRY,

	PPCREC_IML_OP_X86_CMP,

	PPCREC_IML_OP_INVALID
};

#define PPCREC_IML_OP_FPR_COPY_PAIR (PPCREC_IML_OP_ASSIGN)

enum
{
	PPCREC_IML_MACRO_B_TO_REG,

	PPCREC_IML_MACRO_BL,
	PPCREC_IML_MACRO_B_FAR,
	PPCREC_IML_MACRO_COUNT_CYCLES,
	PPCREC_IML_MACRO_HLE,
	PPCREC_IML_MACRO_LEAVE,
	PPCREC_IML_MACRO_DEBUGBREAK,
};

enum class IMLCondition : uint8
{
	EQ,
	NEQ,
	SIGNED_GT,
	SIGNED_LT,
	UNSIGNED_GT,
	UNSIGNED_LT,

	UNORDERED_GT,
	UNORDERED_LT,
	UNORDERED_EQ,
	UNORDERED_U,

	ORDERED_GT,
	ORDERED_LT,
	ORDERED_EQ,
	ORDERED_U
};

enum
{
	PPCREC_IML_TYPE_NONE,
	PPCREC_IML_TYPE_NO_OP,
	PPCREC_IML_TYPE_R_R,
	PPCREC_IML_TYPE_R_R_R,
	PPCREC_IML_TYPE_R_R_R_CARRY,
	PPCREC_IML_TYPE_R_R_S32,
	PPCREC_IML_TYPE_R_R_S32_CARRY,
	PPCREC_IML_TYPE_LOAD,
	PPCREC_IML_TYPE_LOAD_INDEXED,
	PPCREC_IML_TYPE_STORE,
	PPCREC_IML_TYPE_STORE_INDEXED,
	PPCREC_IML_TYPE_R_NAME,
	PPCREC_IML_TYPE_NAME_R,
	PPCREC_IML_TYPE_R_S32,
	PPCREC_IML_TYPE_MACRO,
	PPCREC_IML_TYPE_CJUMP_CYCLE_CHECK,

	PPCREC_IML_TYPE_COMPARE,
	PPCREC_IML_TYPE_COMPARE_S32,
	PPCREC_IML_TYPE_JUMP,
	PPCREC_IML_TYPE_CONDITIONAL_JUMP,

	PPCREC_IML_TYPE_ATOMIC_CMP_STORE,

	PPCREC_IML_TYPE_CALL_IMM,

	PPCREC_IML_TYPE_FPR_LOAD,
	PPCREC_IML_TYPE_FPR_LOAD_INDEXED,
	PPCREC_IML_TYPE_FPR_STORE,
	PPCREC_IML_TYPE_FPR_STORE_INDEXED,
	PPCREC_IML_TYPE_FPR_R_R,
	PPCREC_IML_TYPE_FPR_R_R_R,
	PPCREC_IML_TYPE_FPR_R_R_R_R,
	PPCREC_IML_TYPE_FPR_R,

	PPCREC_IML_TYPE_FPR_COMPARE,

	PPCREC_IML_TYPE_X86_EFLAGS_JCC,
};

enum
{
	PPCREC_NAME_NONE,
	PPCREC_NAME_TEMPORARY = 1000,
	PPCREC_NAME_R0 = 2000,
	PPCREC_NAME_SPR0 = 3000,
	PPCREC_NAME_FPR_HALF = 4800,
	PPCREC_NAME_TEMPORARY_FPR0 = 5000,
	PPCREC_NAME_XER_CA = 6000,
	PPCREC_NAME_XER_OV = 6001,
	PPCREC_NAME_XER_SO = 6002,
	PPCREC_NAME_CR = 7000,
	PPCREC_NAME_CR_LAST = PPCREC_NAME_CR+31,
	PPCREC_NAME_CPU_MEMRES_EA = 8000,
	PPCREC_NAME_CPU_MEMRES_VAL = 8001,
    PPCREC_NAME_V0 = 9000
};

#define PPC_REC_INVALID_REGISTER	0xFF

enum
{
	PPCREC_FPR_LD_MODE_SINGLE,
	PPCREC_FPR_LD_MODE_DOUBLE,

	PPCREC_FPR_ST_MODE_SINGLE,
	PPCREC_FPR_ST_MODE_DOUBLE,

	PPCREC_FPR_ST_MODE_UI32_FROM_PS0,
};

struct IMLUsedRegisters
{
	IMLUsedRegisters() {};

	bool IsWrittenByRegId(IMLRegID regId) const
	{
		if (writtenGPR1.IsValid() && writtenGPR1.GetRegID() == regId)
			return true;
		if (writtenGPR2.IsValid() && writtenGPR2.GetRegID() == regId)
			return true;
		return false;
	}

	bool IsBaseGPRWritten(IMLReg imlReg) const
	{
		cemu_assert_debug(imlReg.IsValid());
		auto regId = imlReg.GetRegID();
		return IsWrittenByRegId(regId);
	}

	template<typename Fn>
	void ForEachWrittenGPR(Fn F) const
	{
		if (writtenGPR1.IsValid())
			F(writtenGPR1);
		if (writtenGPR2.IsValid())
			F(writtenGPR2);
	}

	template<typename Fn>
	void ForEachReadGPR(Fn F) const
	{
		if (readGPR1.IsValid())
			F(readGPR1);
		if (readGPR2.IsValid())
			F(readGPR2);
		if (readGPR3.IsValid())
			F(readGPR3);
		if (readGPR4.IsValid())
			F(readGPR4);
	}

	template<typename Fn>
	void ForEachAccessedGPR(Fn F) const
	{
		if (readGPR1.IsValid())
			F(readGPR1, false);
		if (readGPR2.IsValid())
			F(readGPR2, false);
		if (readGPR3.IsValid())
			F(readGPR3, false);
		if (readGPR4.IsValid())
			F(readGPR4, false);
		if (writtenGPR1.IsValid())
			F(writtenGPR1, true);
		if (writtenGPR2.IsValid())
			F(writtenGPR2, true);
	}

	IMLReg readGPR1;
	IMLReg readGPR2;
	IMLReg readGPR3;
	IMLReg readGPR4;
	IMLReg writtenGPR1;
	IMLReg writtenGPR2;
};

struct IMLInstruction
{
	IMLInstruction() {}
	IMLInstruction(const IMLInstruction& other)
	{
		memcpy(this, &other, sizeof(IMLInstruction));
	}

	uint8 type;
	uint8 operation;
	union
	{
		struct
		{
			uint8 _padding[7];
		}padding;
		struct
		{
			IMLReg regR;
			IMLReg regA;
		}op_r_r;
		struct
		{
			IMLReg regR;
			IMLReg regA;
			IMLReg regB;
		}op_r_r_r;
		struct
		{
			IMLReg regR;
			IMLReg regA;
			IMLReg regB;
			IMLReg regCarry;
		}op_r_r_r_carry;
		struct
		{
			IMLReg regR;
			IMLReg regA;
			sint32 immS32;
		}op_r_r_s32;
		struct
		{
			IMLReg regR;
			IMLReg regA;
			IMLReg regCarry;
			sint32 immS32;
		}op_r_r_s32_carry;
		struct
		{
			IMLReg regR;
			IMLName name;
		}op_r_name;
		struct
		{
			IMLReg regR;
			sint32 immS32;
		}op_r_immS32;
		struct
		{
			uint32 param;
			uint32 param2;
			uint16 paramU16;
			IMLReg paramReg;
		}op_macro;
		struct
		{
			IMLReg registerData;
			IMLReg registerMem;
			IMLReg registerMem2;
			uint8 copyWidth;
			struct
			{
				bool swapEndian : 1;
				bool signExtend : 1;
				bool notExpanded : 1;
			}flags2;
			uint8 mode;
			sint32 immS32;
		}op_storeLoad;
		struct
		{
			uintptr_t callAddress;
			IMLReg regParam0;
			IMLReg regParam1;
			IMLReg regParam2;
			IMLReg regReturn;
		}op_call_imm;
		struct
		{
			IMLReg regR;
			IMLReg regA;
		}op_fpr_r_r;
		struct
		{
			IMLReg regR;
			IMLReg regA;
			IMLReg regB;
		}op_fpr_r_r_r;
		struct
		{
			IMLReg regR;
			IMLReg regA;
			IMLReg regB;
			IMLReg regC;
		}op_fpr_r_r_r_r;
		struct
		{
			IMLReg regR;
		}op_fpr_r;
		struct
		{
			IMLReg regR;
			IMLReg regA;
			IMLReg regB;
			IMLCondition cond;
		}op_fpr_compare;
		struct
		{
			IMLReg regR;
			IMLReg regA;
			IMLReg regB;
			IMLCondition cond;
		}op_compare;
		struct
		{
			IMLReg regR;
			IMLReg regA;
			sint32 immS32;
			IMLCondition cond;
		}op_compare_s32;
		struct
		{
			IMLReg registerBool;
			bool mustBeTrue;
		}op_conditional_jump;
		struct
		{
			IMLReg regEA;
			IMLReg regCompareValue;
			IMLReg regWriteValue;
			IMLReg regBoolOut;
		}op_atomic_compare_store;
		struct
		{
			IMLReg regR;
			sint32 immS32;
			uint8 crRegisterIndex;
			uint8 crBitIndex;
			bool  bitMustBeSet;
		}op_conditional_r_s32;
		struct
		{
			IMLCondition cond;
			bool invertedCondition;
		}op_x86_eflags_jcc;
	};

	bool IsSuffixInstruction() const
	{
		if (type == PPCREC_IML_TYPE_MACRO && operation == PPCREC_IML_MACRO_BL ||
			type == PPCREC_IML_TYPE_MACRO && operation == PPCREC_IML_MACRO_B_FAR ||
			type == PPCREC_IML_TYPE_MACRO && operation == PPCREC_IML_MACRO_B_TO_REG ||
			type == PPCREC_IML_TYPE_MACRO && operation == PPCREC_IML_MACRO_LEAVE ||
			type == PPCREC_IML_TYPE_MACRO && operation == PPCREC_IML_MACRO_HLE ||
			type == PPCREC_IML_TYPE_CJUMP_CYCLE_CHECK ||
			type == PPCREC_IML_TYPE_JUMP ||
			type == PPCREC_IML_TYPE_CONDITIONAL_JUMP ||
			type == PPCREC_IML_TYPE_X86_EFLAGS_JCC)
			return true;
		return false;
	}

	void make_no_op()
	{
		type = PPCREC_IML_TYPE_NO_OP;
		operation = 0;
	}

	void make_r_name(IMLReg regR, IMLName name)
	{
		cemu_assert_debug(regR.GetBaseFormat() == regR.GetRegFormat());
		type = PPCREC_IML_TYPE_R_NAME;
		operation = PPCREC_IML_OP_ASSIGN;
		op_r_name.regR = regR;
		op_r_name.name = name;
	}

	void make_name_r(IMLName name, IMLReg regR)
	{
		cemu_assert_debug(regR.GetBaseFormat() == regR.GetRegFormat());
		type = PPCREC_IML_TYPE_NAME_R;
		operation = PPCREC_IML_OP_ASSIGN;
		op_r_name.regR = regR;
		op_r_name.name = name;
	}

	void make_debugbreak(uint32 currentPPCAddress = 0)
	{
		make_macro(PPCREC_IML_MACRO_DEBUGBREAK, 0, currentPPCAddress, 0, IMLREG_INVALID);
	}

	void make_macro(uint32 macroId, uint32 param, uint32 param2, uint16 paramU16, IMLReg regParam)
	{
		this->type = PPCREC_IML_TYPE_MACRO;
		this->operation = macroId;
		this->op_macro.param = param;
		this->op_macro.param2 = param2;
		this->op_macro.paramU16 = paramU16;
		this->op_macro.paramReg = regParam;
	}

	void make_cjump_cycle_check()
	{
		this->type = PPCREC_IML_TYPE_CJUMP_CYCLE_CHECK;
		this->operation = 0;
	}

	void make_r_r(uint32 operation, IMLReg regR, IMLReg regA)
	{
		this->type = PPCREC_IML_TYPE_R_R;
		this->operation = operation;
		this->op_r_r.regR = regR;
		this->op_r_r.regA = regA;
	}

	void make_r_s32(uint32 operation, IMLReg regR, sint32 immS32)
	{
		this->type = PPCREC_IML_TYPE_R_S32;
		this->operation = operation;
		this->op_r_immS32.regR = regR;
		this->op_r_immS32.immS32 = immS32;
	}

	void make_r_r_r(uint32 operation, IMLReg regR, IMLReg regA, IMLReg regB)
	{
		this->type = PPCREC_IML_TYPE_R_R_R;
		this->operation = operation;
		this->op_r_r_r.regR = regR;
		this->op_r_r_r.regA = regA;
		this->op_r_r_r.regB = regB;
	}

	void make_r_r_r_carry(uint32 operation, IMLReg regR, IMLReg regA, IMLReg regB, IMLReg regCarry)
	{
		this->type = PPCREC_IML_TYPE_R_R_R_CARRY;
		this->operation = operation;
		this->op_r_r_r_carry.regR = regR;
		this->op_r_r_r_carry.regA = regA;
		this->op_r_r_r_carry.regB = regB;
		this->op_r_r_r_carry.regCarry = regCarry;
	}

	void make_r_r_s32(uint32 operation, IMLReg regR, IMLReg regA, sint32 immS32)
	{
		this->type = PPCREC_IML_TYPE_R_R_S32;
		this->operation = operation;
		this->op_r_r_s32.regR = regR;
		this->op_r_r_s32.regA = regA;
		this->op_r_r_s32.immS32 = immS32;
	}

	void make_r_r_s32_carry(uint32 operation, IMLReg regR, IMLReg regA, sint32 immS32, IMLReg regCarry)
	{
		this->type = PPCREC_IML_TYPE_R_R_S32_CARRY;
		this->operation = operation;
		this->op_r_r_s32_carry.regR = regR;
		this->op_r_r_s32_carry.regA = regA;
		this->op_r_r_s32_carry.immS32 = immS32;
		this->op_r_r_s32_carry.regCarry = regCarry;
	}

	void make_compare(IMLReg regA, IMLReg regB, IMLReg regR, IMLCondition cond)
	{
		this->type = PPCREC_IML_TYPE_COMPARE;
		this->operation = PPCREC_IML_OP_INVALID;
		this->op_compare.regR = regR;
		this->op_compare.regA = regA;
		this->op_compare.regB = regB;
		this->op_compare.cond = cond;
	}

	void make_compare_s32(IMLReg regA, sint32 immS32, IMLReg regR, IMLCondition cond)
	{
		this->type = PPCREC_IML_TYPE_COMPARE_S32;
		this->operation = PPCREC_IML_OP_INVALID;
		this->op_compare_s32.regR = regR;
		this->op_compare_s32.regA = regA;
		this->op_compare_s32.immS32 = immS32;
		this->op_compare_s32.cond = cond;
	}

	void make_conditional_jump(IMLReg regBool, bool mustBeTrue)
	{
		this->type = PPCREC_IML_TYPE_CONDITIONAL_JUMP;
		this->operation = PPCREC_IML_OP_INVALID;
		this->op_conditional_jump.registerBool = regBool;
		this->op_conditional_jump.mustBeTrue = mustBeTrue;
	}

	void make_jump()
	{
		this->type = PPCREC_IML_TYPE_JUMP;
		this->operation = PPCREC_IML_OP_INVALID;
	}

	void make_r_memory(IMLReg regD, IMLReg regMem, sint32 immS32, uint32 copyWidth, bool signExtend, bool switchEndian)
	{
		this->type = PPCREC_IML_TYPE_LOAD;
		this->operation = 0;
		this->op_storeLoad.registerData = regD;
		this->op_storeLoad.registerMem = regMem;
		this->op_storeLoad.immS32 = immS32;
		this->op_storeLoad.copyWidth = copyWidth;
		this->op_storeLoad.flags2.swapEndian = switchEndian;
		this->op_storeLoad.flags2.signExtend = signExtend;
	}

	void make_memory_r(IMLReg regS, IMLReg regMem, sint32 immS32, uint32 copyWidth, bool switchEndian)
	{
		this->type = PPCREC_IML_TYPE_STORE;
		this->operation = 0;
		this->op_storeLoad.registerData = regS;
		this->op_storeLoad.registerMem = regMem;
		this->op_storeLoad.immS32 = immS32;
		this->op_storeLoad.copyWidth = copyWidth;
		this->op_storeLoad.flags2.swapEndian = switchEndian;
		this->op_storeLoad.flags2.signExtend = false;
	}

	void make_atomic_cmp_store(IMLReg regEA, IMLReg regCompareValue, IMLReg regWriteValue, IMLReg regSuccessOutput)
	{
		this->type = PPCREC_IML_TYPE_ATOMIC_CMP_STORE;
		this->operation = 0;
		this->op_atomic_compare_store.regEA = regEA;
		this->op_atomic_compare_store.regCompareValue = regCompareValue;
		this->op_atomic_compare_store.regWriteValue = regWriteValue;
		this->op_atomic_compare_store.regBoolOut = regSuccessOutput;
	}

	void make_call_imm(uintptr_t callAddress, IMLReg param0, IMLReg param1, IMLReg param2, IMLReg regReturn)
	{
		this->type = PPCREC_IML_TYPE_CALL_IMM;
		this->operation = 0;
		this->op_call_imm.callAddress = callAddress;
		this->op_call_imm.regParam0 = param0;
		this->op_call_imm.regParam1 = param1;
		this->op_call_imm.regParam2 = param2;
		this->op_call_imm.regReturn = regReturn;
	}

	void make_fpr_r_memory(IMLReg registerDestination, IMLReg registerMemory, sint32 immS32, uint32 mode, bool switchEndian)
	{
		this->type = PPCREC_IML_TYPE_FPR_LOAD;
		this->operation = 0;
		this->op_storeLoad.registerData = registerDestination;
		this->op_storeLoad.registerMem = registerMemory;
		this->op_storeLoad.immS32 = immS32;
		this->op_storeLoad.mode = mode;
		this->op_storeLoad.flags2.swapEndian = switchEndian;
	}

	void make_fpr_r_memory_indexed(IMLReg registerDestination, IMLReg registerMemory1, IMLReg registerMemory2, uint32 mode, bool switchEndian)
	{
		this->type = PPCREC_IML_TYPE_FPR_LOAD_INDEXED;
		this->operation = 0;
		this->op_storeLoad.registerData = registerDestination;
		this->op_storeLoad.registerMem = registerMemory1;
		this->op_storeLoad.registerMem2 = registerMemory2;
		this->op_storeLoad.immS32 = 0;
		this->op_storeLoad.mode = mode;
		this->op_storeLoad.flags2.swapEndian = switchEndian;
	}

	void make_fpr_memory_r(IMLReg registerSource, IMLReg registerMemory, sint32 immS32, uint32 mode, bool switchEndian)
	{
		this->type = PPCREC_IML_TYPE_FPR_STORE;
		this->operation = 0;
		this->op_storeLoad.registerData = registerSource;
		this->op_storeLoad.registerMem = registerMemory;
		this->op_storeLoad.immS32 = immS32;
		this->op_storeLoad.mode = mode;
		this->op_storeLoad.flags2.swapEndian = switchEndian;
	}

	void make_fpr_memory_r_indexed(IMLReg registerSource, IMLReg registerMemory1, IMLReg registerMemory2, sint32 immS32, uint32 mode, bool switchEndian)
	{
		this->type = PPCREC_IML_TYPE_FPR_STORE_INDEXED;
		this->operation = 0;
		this->op_storeLoad.registerData = registerSource;
		this->op_storeLoad.registerMem = registerMemory1;
		this->op_storeLoad.registerMem2 = registerMemory2;
		this->op_storeLoad.immS32 = immS32;
		this->op_storeLoad.mode = mode;
		this->op_storeLoad.flags2.swapEndian = switchEndian;
	}

	void make_fpr_compare(IMLReg regA, IMLReg regB, IMLReg regR, IMLCondition cond)
	{
		this->type = PPCREC_IML_TYPE_FPR_COMPARE;
		this->operation = -999;
		this->op_fpr_compare.regR = regR;
		this->op_fpr_compare.regA = regA;
		this->op_fpr_compare.regB = regB;
		this->op_fpr_compare.cond = cond;
	}

	void make_fpr_r(sint32 operation, IMLReg registerResult)
	{
		this->type = PPCREC_IML_TYPE_FPR_R;
		this->operation = operation;
		this->op_fpr_r.regR = registerResult;
	}

	void make_fpr_r_r(sint32 operation, IMLReg registerResult, IMLReg registerOperand, sint32 crRegister=PPC_REC_INVALID_REGISTER)
	{
		this->type = PPCREC_IML_TYPE_FPR_R_R;
		this->operation = operation;
		this->op_fpr_r_r.regR = registerResult;
		this->op_fpr_r_r.regA = registerOperand;
	}

	void make_fpr_r_r_r(sint32 operation, IMLReg registerResult, IMLReg registerOperand1, IMLReg registerOperand2, sint32 crRegister=PPC_REC_INVALID_REGISTER)
	{
		this->type = PPCREC_IML_TYPE_FPR_R_R_R;
		this->operation = operation;
		this->op_fpr_r_r_r.regR = registerResult;
		this->op_fpr_r_r_r.regA = registerOperand1;
		this->op_fpr_r_r_r.regB = registerOperand2;
	}

	void make_fpr_r_r_r_r(sint32 operation, IMLReg registerResult, IMLReg registerOperandA, IMLReg registerOperandB, IMLReg registerOperandC, sint32 crRegister=PPC_REC_INVALID_REGISTER)
	{
		this->type = PPCREC_IML_TYPE_FPR_R_R_R_R;
		this->operation = operation;
		this->op_fpr_r_r_r_r.regR = registerResult;
		this->op_fpr_r_r_r_r.regA = registerOperandA;
		this->op_fpr_r_r_r_r.regB = registerOperandB;
		this->op_fpr_r_r_r_r.regC = registerOperandC;
	}

	void make_x86_eflags_jcc(IMLCondition cond, bool invertedCondition)
	{
		this->type = PPCREC_IML_TYPE_X86_EFLAGS_JCC;
		this->operation = -999;
		this->op_x86_eflags_jcc.cond = cond;
		this->op_x86_eflags_jcc.invertedCondition = invertedCondition;
	}

	void CheckRegisterUsage(IMLUsedRegisters* registersUsed) const;
	bool HasSideEffects() const;

	void RewriteGPR(const std::unordered_map<IMLRegID, IMLRegID>& translationTable);
};

namespace IMLArchX86
{
	static constexpr int PHYSREG_GPR_BASE = 0;
	static constexpr int PHYSREG_FPR_BASE = 16;
};

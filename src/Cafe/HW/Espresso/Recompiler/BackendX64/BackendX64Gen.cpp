#include "BackendX64.h"

void x64Gen_writeU8(x64GenContext_t* x64GenContext, uint8 v)
{
	x64GenContext->emitter->_emitU8(v);
}

void x64Gen_writeU16(x64GenContext_t* x64GenContext, uint32 v)
{
	x64GenContext->emitter->_emitU16(v);
}

void x64Gen_writeU32(x64GenContext_t* x64GenContext, uint32 v)
{
	x64GenContext->emitter->_emitU32(v);
}

void x64Gen_writeU64(x64GenContext_t* x64GenContext, uint64 v)
{
	x64GenContext->emitter->_emitU64(v);
}

#include "X64Emit.hpp"

void _x64Gen_writeMODRMDeprecated(x64GenContext_t* x64GenContext, sint32 dataRegister, sint32 memRegisterA64, sint32 memRegisterB64, sint32 memImmS32)
{
	bool forceUseOffset = false;
	if ((memRegisterA64 & 7) == 5)
	{
		forceUseOffset = true;
	}

	if (memRegisterB64 == X86_REG_NONE)
	{
		uint8 modRM = (dataRegister & 7) * 8 + (memRegisterA64 & 7);
		if (forceUseOffset && memImmS32 == 0)
		{
			modRM |= (1 << 6);
		}
		if (memImmS32 == 0)
		{
			modRM |= (0 << 6);
		}
		else if (memImmS32 >= -128 && memImmS32 <= 127)
		{
			modRM |= (1 << 6);
		}
		else
		{
			modRM |= (2 << 6);
		}
		x64Gen_writeU8(x64GenContext, modRM);
		if ((memRegisterA64 & 7) == 4)
		{
			x64Gen_writeU8(x64GenContext, 0x24);
		}
		if (((modRM >> 6) & 3) == 0)
			;
		else if (((modRM >> 6) & 3) == 1)
			x64Gen_writeU8(x64GenContext, (uint8)memImmS32);
		else if (((modRM >> 6) & 3) == 2)
			x64Gen_writeU32(x64GenContext, (uint32)memImmS32);
		else
			assert_dbg();
		return;
	}
	if( (memRegisterA64&7) == 4 )
	{
		assert_dbg();
	}
	if( (memRegisterA64&7) == 4 )
		assert_dbg();
	uint8 modRM = (0x04<<0)+((dataRegister&7)<<3);
	if( forceUseOffset && memImmS32 == 0 )
	{
		modRM |= (1<<6);
	}
	if( memImmS32 == 0 )
	{
		modRM |= (0<<6);
	}
	else if( memImmS32 >= -128 && memImmS32 <= 127 )
	{
		modRM |= (1<<6);
	}
	else
	{
		modRM |= (2<<6);
	}
	x64Gen_writeU8(x64GenContext, modRM);
	x64Gen_writeU8(x64GenContext, 0x00+(memRegisterA64&7)+(memRegisterB64&7)*8);
	if( ((modRM>>6)&3) == 0 )
		;
	else if( ((modRM>>6)&3) == 1 )
		x64Gen_writeU8(x64GenContext, (uint8)memImmS32);
	else if( ((modRM>>6)&3) == 2 )
		x64Gen_writeU32(x64GenContext, (uint32)memImmS32);
	else
		assert_dbg();
}

void x64Emit_mov_reg32_mem32(x64GenContext_t* x64GenContext, sint32 destReg, sint32 memBaseReg64, sint32 memOffset)
{
	x64Gen_writeMODRM_dyn<x64_opc_1byte<0x8B>>(x64GenContext, x64MODRM_opr_reg64(destReg), x64MODRM_opr_memReg64(memBaseReg64, memOffset));
}

void x64Emit_mov_mem32_reg32(x64GenContext_t* x64GenContext, sint32 memBaseReg64, sint32 memOffset, sint32 srcReg)
{
	x64Gen_writeMODRM_dyn<x64_opc_1byte_rev<0x89>>(x64GenContext, x64MODRM_opr_memReg64(memBaseReg64, memOffset), x64MODRM_opr_reg64(srcReg));
}

void x64Emit_mov_mem64_reg64(x64GenContext_t* x64GenContext, sint32 memBaseReg64, sint32 memOffset, sint32 srcReg)
{
	x64Gen_writeMODRM_dyn<x64_opc_1byte_rev<0x89, true>>(x64GenContext, x64MODRM_opr_memReg64(memBaseReg64, memOffset), x64MODRM_opr_reg64(srcReg));
}

void x64Emit_mov_reg64_mem64(x64GenContext_t* x64GenContext, sint32 destReg, sint32 memBaseReg64, sint32 memOffset)
{
	x64Gen_writeMODRM_dyn<x64_opc_1byte<0x8B, true>>(x64GenContext, x64MODRM_opr_reg64(destReg), x64MODRM_opr_memReg64(memBaseReg64, memOffset));
}

void x64Emit_mov_reg64_mem32(x64GenContext_t* x64GenContext, sint32 destReg, sint32 memBaseReg64, sint32 memOffset)
{
	x64Gen_writeMODRM_dyn<x64_opc_1byte<0x8B>>(x64GenContext, x64MODRM_opr_reg64(destReg), x64MODRM_opr_memReg64(memBaseReg64, memOffset));
}

void x64Emit_mov_mem32_reg64(x64GenContext_t* x64GenContext, sint32 memBaseReg64, sint32 memOffset, sint32 srcReg)
{
	x64Gen_writeMODRM_dyn<x64_opc_1byte_rev<0x89>>(x64GenContext, x64MODRM_opr_memReg64(memBaseReg64, memOffset), x64MODRM_opr_reg64(srcReg));
}

void x64Emit_mov_reg64_mem64(x64GenContext_t* x64GenContext, sint32 destReg, sint32 memBaseReg64, sint32 memIndexReg64, sint32 memOffset)
{
	x64Gen_writeMODRM_dyn<x64_opc_1byte<0x8B, true>>(x64GenContext, x64MODRM_opr_reg64(destReg), x64MODRM_opr_memRegPlusReg(memBaseReg64, memIndexReg64, memOffset));
}

void x64Emit_mov_reg32_mem32(x64GenContext_t* x64GenContext, sint32 destReg, sint32 memBaseReg64, sint32 memIndexReg64, sint32 memOffset)
{
	x64Gen_writeMODRM_dyn<x64_opc_1byte<0x8B>>(x64GenContext, x64MODRM_opr_reg64(destReg), x64MODRM_opr_memRegPlusReg(memBaseReg64, memIndexReg64, memOffset));
}

void x64Emit_mov_reg64b_mem8(x64GenContext_t* x64GenContext, sint32 destReg, sint32 memBaseReg64, sint32 memIndexReg64, sint32 memOffset)
{
	x64Gen_writeMODRM_dyn<x64_opc_1byte<0x8A>>(x64GenContext, x64MODRM_opr_reg64(destReg), x64MODRM_opr_memRegPlusReg(memBaseReg64, memIndexReg64, memOffset));
}

void x64Emit_movZX_reg32_mem8(x64GenContext_t* x64GenContext, sint32 destReg, sint32 memBaseReg64, sint32 memIndexReg64, sint32 memOffset)
{
	x64Gen_writeMODRM_dyn<x64_opc_2byte<0x0F,0xB6>>(x64GenContext, x64MODRM_opr_reg64(destReg), x64MODRM_opr_memRegPlusReg(memBaseReg64, memIndexReg64, memOffset));
}

void x64Emit_movZX_reg64_mem8(x64GenContext_t* x64GenContext, sint32 destReg, sint32 memBaseReg64, sint32 memOffset)
{
	x64Gen_writeMODRM_dyn<x64_opc_2byte<0x0F, 0xB6>>(x64GenContext, x64MODRM_opr_reg64(destReg), x64MODRM_opr_memReg64(memBaseReg64, memOffset));
}

void x64Gen_movSignExtend_reg64Low32_mem8Reg64PlusReg64(x64GenContext_t* x64GenContext, sint32 dstRegister, sint32 memRegisterA64, sint32 memRegisterB64, sint32 memImmS32)
{
	if (dstRegister >= 8 && memRegisterA64 >= 8 && memRegisterB64 >= 8)
		x64Gen_writeU8(x64GenContext, 0x47);
	else if (memRegisterA64 >= 8 && memRegisterB64 >= 8)
		x64Gen_writeU8(x64GenContext, 0x43);
	else if (dstRegister >= 8 && memRegisterB64 >= 8)
		x64Gen_writeU8(x64GenContext, 0x42);
	else if (dstRegister >= 8 && memRegisterA64 >= 8)
		x64Gen_writeU8(x64GenContext, 0x45);
	else if (dstRegister >= 8)
		x64Gen_writeU8(x64GenContext, 0x44);
	else if (memRegisterA64 >= 8)
		x64Gen_writeU8(x64GenContext, 0x41);
	else if (memRegisterB64 >= 8)
		x64Gen_writeU8(x64GenContext, 0x42);
	else if (dstRegister >= 4)
		x64Gen_writeU8(x64GenContext, 0x40);

	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0xBE);
	_x64Gen_writeMODRMDeprecated(x64GenContext, dstRegister, memRegisterA64, memRegisterB64, memImmS32);
}

void x64Gen_mov_mem64Reg64PlusReg64_reg64(x64GenContext_t* x64GenContext, sint32 srcRegister, sint32 memRegisterA64, sint32 memRegisterB64, sint32 memImmS32)
{
	if( srcRegister >= 8 && memRegisterA64 >= 8 && memRegisterB64 >= 8 )
		x64Gen_writeU8(x64GenContext, 0x47|8);
	else if( memRegisterA64 >= 8 && memRegisterB64 >= 8 )
		x64Gen_writeU8(x64GenContext, 0x43|8);
	else if( srcRegister >= 8 && memRegisterB64 >= 8 )
		x64Gen_writeU8(x64GenContext, 0x42|8);
	else if( srcRegister >= 8 && memRegisterA64 >= 8 )
		x64Gen_writeU8(x64GenContext, 0x45|8);
	else if( srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x44|8);
	else if( memRegisterA64 >= 8 )
		x64Gen_writeU8(x64GenContext, 0x41|8);
	else if( memRegisterB64 >= 8 )
		x64Gen_writeU8(x64GenContext, 0x42|8);
	else
		x64Gen_writeU8(x64GenContext, 0x48);
	x64Gen_writeU8(x64GenContext, 0x89);
	_x64Gen_writeMODRMDeprecated(x64GenContext, srcRegister, memRegisterA64, memRegisterB64, memImmS32);
}

void x64Gen_movZeroExtend_reg64Low16_mem16Reg64PlusReg64(x64GenContext_t* x64GenContext, sint32 dstRegister, sint32 memRegisterA64, sint32 memRegisterB64, sint32 memImmS32)
{
	x64Gen_writeU8(x64GenContext, 0x66);
	x64Emit_mov_reg32_mem32(x64GenContext, dstRegister, memRegisterA64, memRegisterB64, memImmS32);
}

void x64Gen_movTruncate_mem32Reg64PlusReg64_reg64(x64GenContext_t* x64GenContext, sint32 memRegisterA64, sint32 memRegisterB64, sint32 memImmS32, sint32 srcRegister)
{
	if( srcRegister >= 8 && memRegisterA64 >= 8 && memRegisterB64 >= 8 )
		x64Gen_writeU8(x64GenContext, 0x47);
	else if( memRegisterA64 >= 8 && memRegisterB64 >= 8 )
		x64Gen_writeU8(x64GenContext, 0x43);
	else if( srcRegister >= 8 && memRegisterB64 >= 8 )
		x64Gen_writeU8(x64GenContext, 0x42);
	else if( srcRegister >= 8 && memRegisterA64 >= 8 )
		x64Gen_writeU8(x64GenContext, 0x45);
	else if( srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x44);
	else if( memRegisterA64 >= 8 )
		x64Gen_writeU8(x64GenContext, 0x41);
	else if( memRegisterB64 >= 8 )
		x64Gen_writeU8(x64GenContext, 0x42);

	x64Gen_writeU8(x64GenContext, 0x89);
	_x64Gen_writeMODRMDeprecated(x64GenContext, srcRegister, memRegisterA64, memRegisterB64, memImmS32);
}

void x64Gen_movTruncate_mem16Reg64PlusReg64_reg64(x64GenContext_t* x64GenContext, sint32 memRegisterA64, sint32 memRegisterB64, sint32 memImmS32, sint32 srcRegister)
{
	x64Gen_writeU8(x64GenContext, 0x66);
	x64Gen_movTruncate_mem32Reg64PlusReg64_reg64(x64GenContext, memRegisterA64, memRegisterB64, memImmS32, srcRegister);
}

void x64Gen_movTruncate_mem8Reg64PlusReg64_reg64(x64GenContext_t* x64GenContext, sint32 memRegisterA64, sint32 memRegisterB64, sint32 memImmS32, sint32 srcRegister)
{

	uint8 rexByte = 0x40;
	if( srcRegister >= 8 )
		rexByte |= (1<<2);
	if( memRegisterA64 >= 8 )
		rexByte |= (1<<0);
	if( memRegisterB64 >= 8 )
		rexByte |= (1<<1);
	x64Gen_writeU8(x64GenContext, rexByte);

	x64Gen_writeU8(x64GenContext, 0x88);
	_x64Gen_writeMODRMDeprecated(x64GenContext, srcRegister, memRegisterA64, memRegisterB64, memImmS32);
}

void x64Gen_mov_mem32Reg64_imm32(x64GenContext_t* x64GenContext, sint32 memRegister, uint32 memImmU32, uint32 dataImmU32)
{
	if( (memRegister&7) == 4 )
	{
		if( memRegister >= 8 )
			x64Gen_writeU8(x64GenContext, 0x41);
		sint32 memImmS32 = (sint32)memImmU32;
		if( memImmS32 >= -128 && memImmS32 <= 127 )
		{
			x64Gen_writeU8(x64GenContext, 0xC7);
			x64Gen_writeU8(x64GenContext, 0x44);
			x64Gen_writeU8(x64GenContext, 0x24);
			x64Gen_writeU8(x64GenContext, (uint8)memImmU32);
		}
		else
		{
			x64Gen_writeU8(x64GenContext, 0xC7);
			x64Gen_writeU8(x64GenContext, 0x84);
			x64Gen_writeU8(x64GenContext, 0x24);
			x64Gen_writeU32(x64GenContext, memImmU32);
		}
		x64Gen_writeU32(x64GenContext, dataImmU32);
	}
	else
	{
		assert_dbg();
	}
}

void x64Gen_mov_mem64Reg64_imm32(x64GenContext_t* x64GenContext, sint32 memRegister, uint32 memImmU32, uint32 dataImmU32)
{
	if( memRegister == X86_REG_R14 )
	{
		sint32 memImmS32 = (sint32)memImmU32;
		if( memImmS32 == 0 )
		{
			x64Gen_writeU8(x64GenContext, 0x49);
			x64Gen_writeU8(x64GenContext, 0xC7);
			x64Gen_writeU8(x64GenContext, 0x06);
			x64Gen_writeU32(x64GenContext, dataImmU32);
		}
		else if( memImmS32 >= -128 && memImmS32 <= 127 )
		{
			x64Gen_writeU8(x64GenContext, 0x49);
			x64Gen_writeU8(x64GenContext, 0xC7);
			x64Gen_writeU8(x64GenContext, 0x46);
			x64Gen_writeU8(x64GenContext, (uint8)memImmS32);
			x64Gen_writeU32(x64GenContext, dataImmU32);
		}
		else
		{
			assert_dbg();
		}
	}
	else
	{
		assert_dbg();
	}
}

void x64Gen_mov_mem8Reg64_imm8(x64GenContext_t* x64GenContext, sint32 memRegister, uint32 memImmU32, uint8 dataImmU8)
{
	if( memRegister == X86_REG_RSP )
	{
		sint32 memImmS32 = (sint32)memImmU32;
		if( memImmS32 >= -128 && memImmS32 <= 127 )
		{
			x64Gen_writeU8(x64GenContext, 0xC6);
			x64Gen_writeU8(x64GenContext, 0x44);
			x64Gen_writeU8(x64GenContext, 0x24);
			x64Gen_writeU8(x64GenContext, (uint8)memImmU32);
		}
		else
		{
			x64Gen_writeU8(x64GenContext, 0xC6);
			x64Gen_writeU8(x64GenContext, 0x84);
			x64Gen_writeU8(x64GenContext, 0x24);
			x64Gen_writeU32(x64GenContext, memImmU32);
		}
		x64Gen_writeU8(x64GenContext, dataImmU8);
	}
	else
	{
		assert_dbg();
	}
}

void x64Gen_mov_reg64_imm64(x64GenContext_t* x64GenContext, sint32 destRegister, uint64 immU64)
{
	x64Gen_writeU8(x64GenContext, 0x48+(destRegister/8));
	x64Gen_writeU8(x64GenContext, 0xB8+(destRegister%8));
	x64Gen_writeU64(x64GenContext, immU64);
}

void x64Gen_mov_reg64Low32_imm32(x64GenContext_t* x64GenContext, sint32 destRegister, uint64 immU32)
{
	if( destRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x41);
	x64Gen_writeU8(x64GenContext, 0xB8+(destRegister&7));
	x64Gen_writeU32(x64GenContext, (uint32)immU32);
}

void x64Gen_mov_reg64_reg64(x64GenContext_t* x64GenContext, sint32 destRegister, sint32 srcRegister)
{
	if( destRegister >= 8 && srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x4D);
	else if( destRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x49);
	else if( srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x4C);
	else
		x64Gen_writeU8(x64GenContext, 0x48);
	x64Gen_writeU8(x64GenContext, 0x89);
	x64Gen_writeU8(x64GenContext, 0xC0+(destRegister&7)+(srcRegister&7)*8);
}

void x64Gen_xchg_reg64_reg64(x64GenContext_t* x64GenContext, sint32 destRegister, sint32 srcRegister)
{
	if( destRegister >= 8 && srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x4D);
	else if( destRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x49);
	else if( srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x4C);
	else
		x64Gen_writeU8(x64GenContext, 0x48);
	x64Gen_writeU8(x64GenContext, 0x87);
	x64Gen_writeU8(x64GenContext, 0xC0+(destRegister&7)+(srcRegister&7)*8);
}

void x64Gen_mov_reg64Low32_reg64Low32(x64GenContext_t* x64GenContext, sint32 destRegister, sint32 srcRegister)
{
	if (destRegister >= 8 && srcRegister >= 8)
		x64Gen_writeU8(x64GenContext, 0x45);
	else if (destRegister >= 8)
		x64Gen_writeU8(x64GenContext, 0x41);
	else if (srcRegister >= 8)
		x64Gen_writeU8(x64GenContext, 0x44);
	x64Gen_writeU8(x64GenContext, 0x89);
	x64Gen_writeU8(x64GenContext, 0xC0 + (destRegister & 7) + (srcRegister & 7) * 8);
}

void x64Gen_cmovcc_reg64Low32_reg64Low32(x64GenContext_t* x64GenContext, uint32 conditionType, sint32 destRegister, sint32 srcRegister)
{
	if (destRegister >= 8 && srcRegister >= 8)
		x64Gen_writeU8(x64GenContext, 0x45);
	else if (srcRegister >= 8)
		x64Gen_writeU8(x64GenContext, 0x41);
	else if (destRegister >= 8)
		x64Gen_writeU8(x64GenContext, 0x44);
	x64Gen_writeU8(x64GenContext, 0x0F);
	if (conditionType == X86_CONDITION_CARRY || conditionType == X86_CONDITION_UNSIGNED_BELOW)
		x64Gen_writeU8(x64GenContext, 0x42);
	else if (conditionType == X86_CONDITION_NOT_CARRY || conditionType == X86_CONDITION_UNSIGNED_ABOVE_EQUAL)
		x64Gen_writeU8(x64GenContext, 0x43);
	else if (conditionType == X86_CONDITION_EQUAL)
		x64Gen_writeU8(x64GenContext, 0x44);
	else if (conditionType == X86_CONDITION_NOT_EQUAL)
		x64Gen_writeU8(x64GenContext, 0x45);
	else if (conditionType == X86_CONDITION_UNSIGNED_BELOW_EQUAL)
		x64Gen_writeU8(x64GenContext, 0x46);
	else if (conditionType == X86_CONDITION_UNSIGNED_ABOVE)
		x64Gen_writeU8(x64GenContext, 0x47);
	else if (conditionType == X86_CONDITION_SIGN)
		x64Gen_writeU8(x64GenContext, 0x48);
	else if (conditionType == X86_CONDITION_NOT_SIGN)
		x64Gen_writeU8(x64GenContext, 0x49);
	else if (conditionType == X86_CONDITION_PARITY)
		x64Gen_writeU8(x64GenContext, 0x4A);
	else if (conditionType == X86_CONDITION_SIGNED_LESS)
		x64Gen_writeU8(x64GenContext, 0x4C);
	else if (conditionType == X86_CONDITION_SIGNED_GREATER_EQUAL)
		x64Gen_writeU8(x64GenContext, 0x4D);
	else if (conditionType == X86_CONDITION_SIGNED_LESS_EQUAL)
		x64Gen_writeU8(x64GenContext, 0x4E);
	else if (conditionType == X86_CONDITION_SIGNED_GREATER)
		x64Gen_writeU8(x64GenContext, 0x4F);
	else
	{
		assert_dbg();
	}
	x64Gen_writeU8(x64GenContext, 0xC0 + (destRegister & 7) * 8 + (srcRegister & 7));
}

void x64Gen_movSignExtend_reg64Low32_reg64Low16(x64GenContext_t* x64GenContext, sint32 destRegister, sint32 srcRegister)
{
	if( destRegister >= 8 && srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x4D);
	else if( srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x41);
	else if( destRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x4C);
	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0xBF);
	x64Gen_writeU8(x64GenContext, 0xC0+(srcRegister&7)+(destRegister&7)*8);
}

void x64Gen_movZeroExtend_reg64Low32_reg64Low16(x64GenContext_t* x64GenContext, sint32 destRegister, sint32 srcRegister)
{
	if( destRegister >= 8 && srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x4D);
	else if( srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x41);
	else if( destRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x4C);
	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0xB7);
	x64Gen_writeU8(x64GenContext, 0xC0+(srcRegister&7)+(destRegister&7)*8);
}

void x64Gen_movSignExtend_reg64Low32_reg64Low8(x64GenContext_t* x64GenContext, sint32 destRegister, sint32 srcRegister)
{
	if( destRegister >= 8 && srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x4D);
	else if( srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x41);
	else if( destRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x4C);
	else if( srcRegister >= 4 )
		x64Gen_writeU8(x64GenContext, 0x40);
	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0xBE);
	x64Gen_writeU8(x64GenContext, 0xC0+(srcRegister&7)+(destRegister&7)*8);
}

void x64Gen_movZeroExtend_reg64Low32_reg64Low8(x64GenContext_t* x64GenContext, sint32 destRegister, sint32 srcRegister)
{
	if( destRegister >= 8 && srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x4D);
	else if( srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x41);
	else if( destRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x4C);
	else if( srcRegister >= 4 )
		x64Gen_writeU8(x64GenContext, 0x40);
	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0xB6);
	x64Gen_writeU8(x64GenContext, 0xC0+(srcRegister&7)+(destRegister&7)*8);
}

void x64Gen_lea_reg64Low32_reg64Low32PlusReg64Low32(x64GenContext_t* x64GenContext, sint32 dstRegister, sint32 memRegisterA64, sint32 memRegisterB64)
{
	if ((memRegisterA64 & 0x7) == 5)
	{
		sint32 temp = memRegisterA64;
		memRegisterA64 = memRegisterB64;
		memRegisterB64 = temp;
	}
	if ((memRegisterA64 & 0x7) == 4)
	{
		sint32 temp = memRegisterA64;
		memRegisterA64 = memRegisterB64;
		memRegisterB64 = temp;
		if ((memRegisterA64 & 0x7) == 4)
			assert_dbg();
	}

	x64Gen_writeU8(x64GenContext, 0x67);
	if (dstRegister >= 8 && memRegisterA64 >= 8 && memRegisterB64 >= 8)
		x64Gen_writeU8(x64GenContext, 0x47);
	else if (dstRegister >= 8 && memRegisterA64 >= 8)
		x64Gen_writeU8(x64GenContext, 0x45);
	else if (dstRegister >= 8 && memRegisterB64 >= 8)
		x64Gen_writeU8(x64GenContext, 0x46);
	else if (dstRegister >= 8)
		x64Gen_writeU8(x64GenContext, 0x44);
	else if (memRegisterA64 >= 8 && memRegisterB64 >= 8)
		x64Gen_writeU8(x64GenContext, 0x43);
	else if (memRegisterB64 >= 8)
		x64Gen_writeU8(x64GenContext, 0x42);
	else if (memRegisterA64 >= 8)
		x64Gen_writeU8(x64GenContext, 0x41);

	x64Gen_writeU8(x64GenContext, 0x8D);
	_x64Gen_writeMODRMDeprecated(x64GenContext, dstRegister&0x7, memRegisterA64 & 0x7, memRegisterB64 & 0x7, 0);
}

void _x64_op_reg64Low_mem8Reg64(x64GenContext_t* x64GenContext, sint32 dstRegister, sint32 memRegister64, sint32 memImmS32, uint8 opByte)
{
	if (dstRegister >= 8 && memRegister64 >= 8)
		x64Gen_writeU8(x64GenContext, 0x45);
	if (dstRegister >= 8)
		x64Gen_writeU8(x64GenContext, 0x44);
	if (memRegister64 >= 8)
		x64Gen_writeU8(x64GenContext, 0x41);
	x64Gen_writeU8(x64GenContext, opByte);
	_x64Gen_writeMODRMDeprecated(x64GenContext, dstRegister, memRegister64, X86_REG_NONE, memImmS32);
}

void x64Gen_or_reg64Low8_mem8Reg64(x64GenContext_t* x64GenContext, sint32 dstRegister, sint32 memRegister64, sint32 memImmS32)
{
	_x64_op_reg64Low_mem8Reg64(x64GenContext, dstRegister, memRegister64, memImmS32, 0x0A);
}

void x64Gen_and_reg64Low8_mem8Reg64(x64GenContext_t* x64GenContext, sint32 dstRegister, sint32 memRegister64, sint32 memImmS32)
{
	_x64_op_reg64Low_mem8Reg64(x64GenContext, dstRegister, memRegister64, memImmS32, 0x22);
}

void x64Gen_mov_mem8Reg64_reg64Low8(x64GenContext_t* x64GenContext, sint32 dstRegister, sint32 memRegister64, sint32 memImmS32)
{
	_x64_op_reg64Low_mem8Reg64(x64GenContext, dstRegister, memRegister64, memImmS32, 0x88);
}

void x64Gen_add_reg64_reg64(x64GenContext_t* x64GenContext, sint32 destRegister, sint32 srcRegister)
{
	x64Gen_writeU8(x64GenContext, 0x48+(destRegister/8)+(srcRegister/8)*4);
	x64Gen_writeU8(x64GenContext, 0x01);
	x64Gen_writeU8(x64GenContext, 0xC0+(srcRegister&7)*8+(destRegister&7));
}

void x64Gen_add_reg64_imm32(x64GenContext_t* x64GenContext, sint32 srcRegister, uint32 immU32)
{
	sint32 immS32 = (sint32)immU32;
	if (srcRegister >= 8)
		x64Gen_writeU8(x64GenContext, 0x49);
	else
		x64Gen_writeU8(x64GenContext, 0x48);
	if (immS32 >= -128 && immS32 <= 127)
	{
		x64Gen_writeU8(x64GenContext, 0x83);
		x64Gen_writeU8(x64GenContext, 0xC0 + (srcRegister & 7));
		x64Gen_writeU8(x64GenContext, (uint8)immS32);
	}
	else
	{
		x64Gen_writeU8(x64GenContext, 0x81);
		x64Gen_writeU8(x64GenContext, 0xC0 + (srcRegister & 7));
		x64Gen_writeU32(x64GenContext, immU32);
	}
}

void x64Gen_add_reg64Low32_reg64Low32(x64GenContext_t* x64GenContext, sint32 destRegister, sint32 srcRegister)
{
	if( destRegister >= 8 && srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x45);
	else if( srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x44);
	else if( destRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x41);
	x64Gen_writeU8(x64GenContext, 0x01);
	x64Gen_writeU8(x64GenContext, 0xC0+(srcRegister&7)*8+(destRegister&7));
}

void x64Gen_add_reg64Low32_imm32(x64GenContext_t* x64GenContext, sint32 srcRegister, uint32 immU32)
{
	sint32 immS32 = (sint32)immU32;
	if( srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x41);
	if( immS32 >= -128 && immS32 <= 127 )
	{
		x64Gen_writeU8(x64GenContext, 0x83);
		x64Gen_writeU8(x64GenContext, 0xC0+(srcRegister&7));
		x64Gen_writeU8(x64GenContext, (uint8)immS32);
	}
	else
	{
		if( srcRegister == X86_REG_RAX )
		{
			x64Gen_writeU8(x64GenContext, 0x05);
		}
		else
		{
			x64Gen_writeU8(x64GenContext, 0x81);
			x64Gen_writeU8(x64GenContext, 0xC0+(srcRegister&7));
		}
		x64Gen_writeU32(x64GenContext, immU32);
	}
}

void x64Gen_sub_reg64Low32_reg64Low32(x64GenContext_t* x64GenContext, sint32 destRegister, sint32 srcRegister)
{
	if( destRegister >= 8 && srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x45);
	else if( srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x44);
	else if( destRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x41);
	x64Gen_writeU8(x64GenContext, 0x29);
	x64Gen_writeU8(x64GenContext, 0xC0+(srcRegister&7)*8+(destRegister&7));
}

void x64Gen_sub_reg64Low32_imm32(x64GenContext_t* x64GenContext, sint32 srcRegister, uint32 immU32)
{
	sint32 immS32 = (sint32)immU32;
	if( srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x41);
	if( immS32 >= -128 && immS32 <= 127 )
	{
		x64Gen_writeU8(x64GenContext, 0x83);
		x64Gen_writeU8(x64GenContext, 0xE8+(srcRegister&7));
		x64Gen_writeU8(x64GenContext, (uint8)immS32);
	}
	else
	{
		if( srcRegister == X86_REG_RAX )
		{
			x64Gen_writeU8(x64GenContext, 0x2D);
		}
		else
		{
			x64Gen_writeU8(x64GenContext, 0x81);
			x64Gen_writeU8(x64GenContext, 0xE8+(srcRegister&7));
		}
		x64Gen_writeU32(x64GenContext, immU32);
	}
}

void x64Gen_sub_reg64_imm32(x64GenContext_t* x64GenContext, sint32 srcRegister, uint32 immU32)
{
	sint32 immS32 = (sint32)immU32;
	if( srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x49);
	else
		x64Gen_writeU8(x64GenContext, 0x48);
	if( immS32 >= -128 && immS32 <= 127 )
	{
		x64Gen_writeU8(x64GenContext, 0x83);
		x64Gen_writeU8(x64GenContext, 0xE8+(srcRegister&7));
		x64Gen_writeU8(x64GenContext, (uint8)immS32);
	}
	else
	{
		x64Gen_writeU8(x64GenContext, 0x81);
		x64Gen_writeU8(x64GenContext, 0xE8+(srcRegister&7));
		x64Gen_writeU32(x64GenContext, immU32);
	}
}

void x64Gen_sub_mem32reg64_imm32(x64GenContext_t* x64GenContext, sint32 memRegister, sint32 memImmS32, uint64 immU32)
{
	sint32 immS32 = (sint32)immU32;
	if( memRegister == X86_REG_RSP )
	{
		if( memImmS32 >= 128 )
		{
			if( immS32 >= -128 && immS32 <= 127 )
			{
				x64Gen_writeU8(x64GenContext, 0x83);
				x64Gen_writeU8(x64GenContext, 0xAC);
				x64Gen_writeU8(x64GenContext, 0x24);
				x64Gen_writeU32(x64GenContext, (uint32)memImmS32);
				x64Gen_writeU8(x64GenContext, (uint8)immU32);
			}
			else
			{
				x64Gen_writeU8(x64GenContext, 0x81);
				x64Gen_writeU8(x64GenContext, 0xAC);
				x64Gen_writeU8(x64GenContext, 0x24);
				x64Gen_writeU32(x64GenContext, (uint32)memImmS32);
				x64Gen_writeU32(x64GenContext, (uint32)immU32);
			}
		}
		else
			assert_dbg();
	}
	else
	{
		assert_dbg();
	}
}

void x64Gen_dec_mem32(x64GenContext_t* x64GenContext, sint32 memoryRegister, uint32 memoryImmU32)
{
	sint32 memoryImmS32 = (sint32)memoryImmU32;
	if (memoryRegister != X86_REG_RSP)
		assert_dbg();
	if (memoryImmS32 >= -128 && memoryImmS32 <= 127)
	{
		x64Gen_writeU8(x64GenContext, 0xFF);
		x64Gen_writeU8(x64GenContext, 0x4C);
		x64Gen_writeU8(x64GenContext, 0x24);
		x64Gen_writeU8(x64GenContext, (uint8)memoryImmU32);
	}
	else
	{
		x64Gen_writeU8(x64GenContext, 0xFF);
		x64Gen_writeU8(x64GenContext, 0x8C);
		x64Gen_writeU8(x64GenContext, 0x24);
		x64Gen_writeU32(x64GenContext, memoryImmU32);
	}
}

void x64Gen_imul_reg64Low32_reg64Low32(x64GenContext_t* x64GenContext, sint32 destRegister, sint32 operandRegister)
{
	if( destRegister >= 8 && operandRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x45);
	else if( operandRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x41);
	else if( destRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x44);
	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0xAF);
	x64Gen_writeU8(x64GenContext, 0xC0+(operandRegister&7)+(destRegister&7)*8);
}

void x64Gen_idiv_reg64Low32(x64GenContext_t* x64GenContext, sint32 operandRegister)
{
	if( operandRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x41);
	x64Gen_writeU8(x64GenContext, 0xF7);
	x64Gen_writeU8(x64GenContext, 0xF8+(operandRegister&7));
}

void x64Gen_div_reg64Low32(x64GenContext_t* x64GenContext, sint32 operandRegister)
{
	if( operandRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x41);
	x64Gen_writeU8(x64GenContext, 0xF7);
	x64Gen_writeU8(x64GenContext, 0xF0+(operandRegister&7));
}

void x64Gen_imul_reg64Low32(x64GenContext_t* x64GenContext, sint32 operandRegister)
{
	if( operandRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x41);
	x64Gen_writeU8(x64GenContext, 0xF7);
	x64Gen_writeU8(x64GenContext, 0xE8+(operandRegister&7));
}

void x64Gen_mul_reg64Low32(x64GenContext_t* x64GenContext, sint32 operandRegister)
{
	if( operandRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x41);
	x64Gen_writeU8(x64GenContext, 0xF7);
	x64Gen_writeU8(x64GenContext, 0xE0+(operandRegister&7));
}

void x64Gen_and_reg64Low32_imm32(x64GenContext_t* x64GenContext, sint32 srcRegister, uint32 immU32)
{
	sint32 immS32 = (sint32)immU32;
	if( srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x41);
	if( immS32 >= -128 && immS32 <= 127 )
	{
		x64Gen_writeU8(x64GenContext, 0x83);
		x64Gen_writeU8(x64GenContext, 0xE0+(srcRegister&7));
		x64Gen_writeU8(x64GenContext, (uint8)immS32);
	}
	else
	{
		if( srcRegister == X86_REG_RAX )
		{
			x64Gen_writeU8(x64GenContext, 0x25);
		}
		else
		{
			x64Gen_writeU8(x64GenContext, 0x81);
			x64Gen_writeU8(x64GenContext, 0xE0+(srcRegister&7));
		}
		x64Gen_writeU32(x64GenContext, immU32);
	}
}

void x64Gen_and_reg64Low32_reg64Low32(x64GenContext_t* x64GenContext, sint32 destRegister, sint32 srcRegister)
{
	if( destRegister >= 8 && srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x45);
	else if( srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x44);
	else if( destRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x41);
	x64Gen_writeU8(x64GenContext, 0x21);
	x64Gen_writeU8(x64GenContext, 0xC0+(destRegister&7)+(srcRegister&7)*8);
}

void x64Gen_test_reg64Low32_reg64Low32(x64GenContext_t* x64GenContext, sint32 destRegister, sint32 srcRegister)
{
	if( destRegister >= 8 && srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x45);
	else if( srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x41);
	else if( destRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x44);
	x64Gen_writeU8(x64GenContext, 0x85);
	x64Gen_writeU8(x64GenContext, 0xC0+(destRegister&7)*8+(srcRegister&7));
}

void x64Gen_test_reg64Low32_imm32(x64GenContext_t* x64GenContext, sint32 srcRegister, uint32 immU32)
{
	sint32 immS32 = (sint32)immU32;
	if( srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x41);
	if( srcRegister == X86_REG_RAX )
	{
		x64Gen_writeU8(x64GenContext, 0xA9);
	}
	else
	{
		x64Gen_writeU8(x64GenContext, 0xF7);
		x64Gen_writeU8(x64GenContext, 0xC0+(srcRegister&7));
	}
	x64Gen_writeU32(x64GenContext, immU32);
}

void x64Gen_cmp_reg64Low32_imm32(x64GenContext_t* x64GenContext, sint32 srcRegister, sint32 immS32)
{
	if( srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x41);
	if( immS32 >= -128 && immS32 <= 127 )
	{
		x64Gen_writeU8(x64GenContext, 0x83);
		x64Gen_writeU8(x64GenContext, 0xF8+(srcRegister&7));
		x64Gen_writeU8(x64GenContext, (uint8)immS32);
	}
	else
	{
		if( srcRegister == X86_REG_RAX )
		{
			x64Gen_writeU8(x64GenContext, 0x3D);
		}
		else
		{
			x64Gen_writeU8(x64GenContext, 0x81);
			x64Gen_writeU8(x64GenContext, 0xF8+(srcRegister&7));
		}
		x64Gen_writeU32(x64GenContext, (uint32)immS32);
	}
}

void x64Gen_cmp_reg64Low32_reg64Low32(x64GenContext_t* x64GenContext, sint32 destRegister, sint32 srcRegister)
{
	if( destRegister >= 8 && srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x45);
	else if( srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x44);
	else if( destRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x41);
	x64Gen_writeU8(x64GenContext, 0x39);
	x64Gen_writeU8(x64GenContext, 0xC0+(destRegister&7)+(srcRegister&7)*8);
}

void x64Gen_cmp_reg64Low32_mem32reg64(x64GenContext_t* x64GenContext, sint32 destRegister, sint32 memRegister, sint32 memImmS32)
{
	if( memRegister == X86_REG_RSP )
	{
		if( memImmS32 >= -128 && memImmS32 <= 127 )
			assert_dbg();
		if( destRegister >= 8 )
			x64Gen_writeU8(x64GenContext, 0x44);
		x64Gen_writeU8(x64GenContext, 0x3B);
		x64Gen_writeU8(x64GenContext, 0x84+(destRegister&7)*8);
		x64Gen_writeU8(x64GenContext, 0x24);
		x64Gen_writeU32(x64GenContext, (uint32)memImmS32);
	}
	else
	{
		assert_dbg();
	}
}

void x64Gen_or_reg64Low32_imm32(x64GenContext_t* x64GenContext, sint32 srcRegister, uint32 immU32)
{
	sint32 immS32 = (sint32)immU32;
	if( srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x41);
	if( immS32 >= -128 && immS32 <= 127 )
	{
		x64Gen_writeU8(x64GenContext, 0x83);
		x64Gen_writeU8(x64GenContext, 0xC8+(srcRegister&7));
		x64Gen_writeU8(x64GenContext, (uint8)immS32);
	}
	else
	{
		if( srcRegister == X86_REG_RAX )
		{
			x64Gen_writeU8(x64GenContext, 0x0D);
		}
		else
		{
			x64Gen_writeU8(x64GenContext, 0x81);
			x64Gen_writeU8(x64GenContext, 0xC8+(srcRegister&7));
		}
		x64Gen_writeU32(x64GenContext, immU32);
	}
}

void x64Gen_or_reg64Low32_reg64Low32(x64GenContext_t* x64GenContext, sint32 destRegister, sint32 srcRegister)
{
	if( destRegister >= 8 && srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x45);
	else if( srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x44);
	else if( destRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x41);
	x64Gen_writeU8(x64GenContext, 0x09);
	x64Gen_writeU8(x64GenContext, 0xC0+(destRegister&7)+(srcRegister&7)*8);
}

void x64Gen_xor_reg32_reg32(x64GenContext_t* x64GenContext, sint32 destRegister, sint32 srcRegister)
{
	x64Gen_writeU8(x64GenContext, 0x33);
	x64Gen_writeU8(x64GenContext, 0xC0+srcRegister+destRegister*8);
}

void x64Gen_xor_reg64Low32_reg64Low32(x64GenContext_t* x64GenContext, sint32 destRegister, sint32 srcRegister)
{
	if( destRegister >= 8 && srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x45);
	else if( srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x44);
	else if( destRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x41);
	x64Gen_writeU8(x64GenContext, 0x31);
	x64Gen_writeU8(x64GenContext, 0xC0+(destRegister&7)+(srcRegister&7)*8);
}

void x64Gen_xor_reg64Low32_imm32(x64GenContext_t* x64GenContext, sint32 srcRegister, uint32 immU32)
{
	sint32 immS32 = (sint32)immU32;
	if( srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x41);
	if( immS32 >= -128 && immS32 <= 127 )
	{
		x64Gen_writeU8(x64GenContext, 0x83);
		x64Gen_writeU8(x64GenContext, 0xF0+(srcRegister&7));
		x64Gen_writeU8(x64GenContext, (uint8)immS32);
	}
	else
	{
		if( srcRegister == X86_REG_RAX )
		{
			x64Gen_writeU8(x64GenContext, 0x35);
		}
		else
		{
			x64Gen_writeU8(x64GenContext, 0x81);
			x64Gen_writeU8(x64GenContext, 0xF0+(srcRegister&7));
		}
		x64Gen_writeU32(x64GenContext, immU32);
	}
}

void x64Gen_rol_reg64Low32_imm8(x64GenContext_t* x64GenContext, sint32 srcRegister, sint8 immS8)
{
	if( srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x41);
	if( immS8 == 1 )
	{
		x64Gen_writeU8(x64GenContext, 0xD1);
		x64Gen_writeU8(x64GenContext, 0xC0+(srcRegister&7));
	}
	else
	{
		x64Gen_writeU8(x64GenContext, 0xC1);
		x64Gen_writeU8(x64GenContext, 0xC0+(srcRegister&7));
		x64Gen_writeU8(x64GenContext, (uint8)immS8);
	}
}

void x64Gen_rol_reg64Low32_cl(x64GenContext_t* x64GenContext, sint32 srcRegister)
{
	if( srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x41);
	x64Gen_writeU8(x64GenContext, 0xD3);
	x64Gen_writeU8(x64GenContext, 0xC0+(srcRegister&7));
}

void x64Gen_rol_reg64Low16_imm8(x64GenContext_t* x64GenContext, sint32 srcRegister, sint8 immS8)
{
	x64Gen_writeU8(x64GenContext, 0x66);
	if( srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x41);
	if( immS8 == 1 )
	{
		x64Gen_writeU8(x64GenContext, 0xD1);
		x64Gen_writeU8(x64GenContext, 0xC0+(srcRegister&7));
	}
	else
	{
		x64Gen_writeU8(x64GenContext, 0xC1);
		x64Gen_writeU8(x64GenContext, 0xC0+(srcRegister&7));
		x64Gen_writeU8(x64GenContext, (uint8)immS8);
	}
}

void x64Gen_rol_reg64_imm8(x64GenContext_t* x64GenContext, sint32 srcRegister, sint8 immS8)
{
	if( srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x49);
	else
		x64Gen_writeU8(x64GenContext, 0x48);
	if( immS8 == 1 )
	{
		x64Gen_writeU8(x64GenContext, 0xD1);
		x64Gen_writeU8(x64GenContext, 0xC0+(srcRegister&7));
	}
	else
	{
		x64Gen_writeU8(x64GenContext, 0xC1);
		x64Gen_writeU8(x64GenContext, 0xC0+(srcRegister&7));
		x64Gen_writeU8(x64GenContext, (uint8)immS8);
	}
}

void x64Gen_shl_reg64Low32_imm8(x64GenContext_t* x64GenContext, sint32 srcRegister, sint8 immS8)
{
	if( srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x41);
	if( immS8 == 1 )
	{
		x64Gen_writeU8(x64GenContext, 0xD1);
		x64Gen_writeU8(x64GenContext, 0xF0+(srcRegister&7));
	}
	else
	{
		x64Gen_writeU8(x64GenContext, 0xC1);
		x64Gen_writeU8(x64GenContext, 0xF0+(srcRegister&7));
		x64Gen_writeU8(x64GenContext, (uint8)immS8);
	}
}

void x64Gen_shr_reg64Low32_imm8(x64GenContext_t* x64GenContext, sint32 srcRegister, sint8 immS8)
{
	if( srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x41);
	if( immS8 == 1 )
	{
		x64Gen_writeU8(x64GenContext, 0xD1);
		x64Gen_writeU8(x64GenContext, 0xE8+(srcRegister&7));
	}
	else
	{
		x64Gen_writeU8(x64GenContext, 0xC1);
		x64Gen_writeU8(x64GenContext, 0xE8+(srcRegister&7));
		x64Gen_writeU8(x64GenContext, (uint8)immS8);
	}
}

void x64Gen_sar_reg64Low32_imm8(x64GenContext_t* x64GenContext, sint32 srcRegister, sint8 immS8)
{
	if( srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x41);
	if( immS8 == 1 )
	{
		x64Gen_writeU8(x64GenContext, 0xD1);
		x64Gen_writeU8(x64GenContext, 0xF8+(srcRegister&7));
	}
	else
	{
		x64Gen_writeU8(x64GenContext, 0xC1);
		x64Gen_writeU8(x64GenContext, 0xF8+(srcRegister&7));
		x64Gen_writeU8(x64GenContext, (uint8)immS8);
	}
}

void x64Gen_not_reg64Low32(x64GenContext_t* x64GenContext, sint32 destRegister)
{
	if( destRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x41);
	x64Gen_writeU8(x64GenContext, 0xF7);
	x64Gen_writeU8(x64GenContext, 0xD0+(destRegister&7));
}

void x64Gen_neg_reg64Low32(x64GenContext_t* x64GenContext, sint32 destRegister)
{
	if( destRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x41);
	x64Gen_writeU8(x64GenContext, 0xF7);
	x64Gen_writeU8(x64GenContext, 0xD8+(destRegister&7));
}

void x64Gen_cdq(x64GenContext_t* x64GenContext)
{
	x64Gen_writeU8(x64GenContext, 0x99);
}

void x64Gen_bswap_reg64Lower32bit(x64GenContext_t* x64GenContext, sint32 destRegister)
{
	if( destRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x41);
	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0xC8+(destRegister&7));
}

void x64Gen_lzcnt_reg64Low32_reg64Low32(x64GenContext_t* x64GenContext, sint32 destRegister, sint32 srcRegister)
{
	x64Gen_writeU8(x64GenContext, 0xF3);
	if( destRegister >= 8 && srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x45);
	else if( destRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x44);
	else if( srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x41);
	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0xBD);
	x64Gen_writeU8(x64GenContext, 0xC0+(destRegister&7)*8+(srcRegister&7));
}

void x64Gen_bsr_reg64Low32_reg64Low32(x64GenContext_t* x64GenContext, sint32 destRegister, sint32 srcRegister)
{
	if( destRegister >= 8 && srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x45);
	else if( destRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x44);
	else if( srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x41);
	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0xBD);
	x64Gen_writeU8(x64GenContext, 0xC0+(destRegister&7)*8+(srcRegister&7));
}

void x64Gen_setcc_mem8(x64GenContext_t* x64GenContext, sint32 conditionType, sint32 memoryRegister, uint32 memoryImmU32)
{
	sint32 memoryImmS32 = (sint32)memoryImmU32;
	if( memoryRegister != X86_REG_RSP )
		assert_dbg();
	if( memoryRegister >= 8 )
		assert_dbg();
	if( memoryImmS32 >= -128 && memoryImmS32 <= 127 )
	{
		if( conditionType == X86_CONDITION_EQUAL )
		{
			x64Gen_writeU8(x64GenContext, 0x0F);
			x64Gen_writeU8(x64GenContext, 0x94);
			x64Gen_writeU8(x64GenContext, 0x44);
			x64Gen_writeU8(x64GenContext, 0x24);
			x64Gen_writeU8(x64GenContext, (uint32)memoryImmU32);
		}
		else if( conditionType == X86_CONDITION_NOT_EQUAL )
		{
			x64Gen_writeU8(x64GenContext, 0x0F);
			x64Gen_writeU8(x64GenContext, 0x95);
			x64Gen_writeU8(x64GenContext, 0x44);
			x64Gen_writeU8(x64GenContext, 0x24);
			x64Gen_writeU8(x64GenContext, (uint32)memoryImmU32);
		}
		else if( conditionType == X86_CONDITION_UNSIGNED_ABOVE )
		{
			x64Gen_writeU8(x64GenContext, 0x0F);
			x64Gen_writeU8(x64GenContext, 0x97);
			x64Gen_writeU8(x64GenContext, 0x44);
			x64Gen_writeU8(x64GenContext, 0x24);
			x64Gen_writeU8(x64GenContext, (uint32)memoryImmU32);
		}
		else if( conditionType == X86_CONDITION_UNSIGNED_ABOVE_EQUAL )
		{
			x64Gen_writeU8(x64GenContext, 0x0F);
			x64Gen_writeU8(x64GenContext, 0x93);
			x64Gen_writeU8(x64GenContext, 0x44);
			x64Gen_writeU8(x64GenContext, 0x24);
			x64Gen_writeU8(x64GenContext, (uint32)memoryImmU32);
		}
		else if( conditionType == X86_CONDITION_UNSIGNED_BELOW )
		{
			x64Gen_writeU8(x64GenContext, 0x0F);
			x64Gen_writeU8(x64GenContext, 0x92);
			x64Gen_writeU8(x64GenContext, 0x44);
			x64Gen_writeU8(x64GenContext, 0x24);
			x64Gen_writeU8(x64GenContext, (uint32)memoryImmU32);
		}
		else if( conditionType == X86_CONDITION_UNSIGNED_BELOW_EQUAL )
		{
			x64Gen_writeU8(x64GenContext, 0x0F);
			x64Gen_writeU8(x64GenContext, 0x96);
			x64Gen_writeU8(x64GenContext, 0x44);
			x64Gen_writeU8(x64GenContext, 0x24);
			x64Gen_writeU8(x64GenContext, (uint32)memoryImmU32);
		}
		else if( conditionType == X86_CONDITION_SIGNED_GREATER )
		{
			x64Gen_writeU8(x64GenContext, 0x0F);
			x64Gen_writeU8(x64GenContext, 0x9F);
			x64Gen_writeU8(x64GenContext, 0x44);
			x64Gen_writeU8(x64GenContext, 0x24);
			x64Gen_writeU8(x64GenContext, (uint32)memoryImmU32);
		}
		else if( conditionType == X86_CONDITION_SIGNED_GREATER_EQUAL )
		{
			x64Gen_writeU8(x64GenContext, 0x0F);
			x64Gen_writeU8(x64GenContext, 0x9D);
			x64Gen_writeU8(x64GenContext, 0x44);
			x64Gen_writeU8(x64GenContext, 0x24);
			x64Gen_writeU8(x64GenContext, (uint32)memoryImmU32);
		}
		else if( conditionType == X86_CONDITION_SIGNED_LESS )
		{
			x64Gen_writeU8(x64GenContext, 0x0F);
			x64Gen_writeU8(x64GenContext, 0x9C);
			x64Gen_writeU8(x64GenContext, 0x44);
			x64Gen_writeU8(x64GenContext, 0x24);
			x64Gen_writeU8(x64GenContext, (uint32)memoryImmU32);
		}
		else if( conditionType == X86_CONDITION_SIGNED_LESS_EQUAL )
		{
			x64Gen_writeU8(x64GenContext, 0x0F);
			x64Gen_writeU8(x64GenContext, 0x9E);
			x64Gen_writeU8(x64GenContext, 0x44);
			x64Gen_writeU8(x64GenContext, 0x24);
			x64Gen_writeU8(x64GenContext, (uint32)memoryImmU32);
		}
		else if( conditionType == X86_CONDITION_PARITY )
		{
			x64Gen_writeU8(x64GenContext, 0x0F);
			x64Gen_writeU8(x64GenContext, 0x9A);
			x64Gen_writeU8(x64GenContext, 0x44);
			x64Gen_writeU8(x64GenContext, 0x24);
			x64Gen_writeU8(x64GenContext, (uint32)memoryImmU32);
		}
		else
			assert_dbg();
	}
	else
	{
		if( conditionType == X86_CONDITION_EQUAL )
		{
			x64Gen_writeU8(x64GenContext, 0x0F);
			x64Gen_writeU8(x64GenContext, 0x94);
			x64Gen_writeU8(x64GenContext, 0x84);
			x64Gen_writeU8(x64GenContext, 0x24);
			x64Gen_writeU32(x64GenContext, (uint32)memoryImmU32);
		}
		else if( conditionType == X86_CONDITION_NOT_EQUAL )
		{
			x64Gen_writeU8(x64GenContext, 0x0F);
			x64Gen_writeU8(x64GenContext, 0x95);
			x64Gen_writeU8(x64GenContext, 0x84);
			x64Gen_writeU8(x64GenContext, 0x24);
			x64Gen_writeU32(x64GenContext, (uint32)memoryImmU32);
		}
		else if( conditionType == X86_CONDITION_UNSIGNED_ABOVE )
		{
			x64Gen_writeU8(x64GenContext, 0x0F);
			x64Gen_writeU8(x64GenContext, 0x97);
			x64Gen_writeU8(x64GenContext, 0x84);
			x64Gen_writeU8(x64GenContext, 0x24);
			x64Gen_writeU32(x64GenContext, (uint32)memoryImmU32);
		}
		else if( conditionType == X86_CONDITION_UNSIGNED_ABOVE_EQUAL )
		{
			x64Gen_writeU8(x64GenContext, 0x0F);
			x64Gen_writeU8(x64GenContext, 0x93);
			x64Gen_writeU8(x64GenContext, 0x84);
			x64Gen_writeU8(x64GenContext, 0x24);
			x64Gen_writeU32(x64GenContext, (uint32)memoryImmU32);
		}
		else if( conditionType == X86_CONDITION_UNSIGNED_BELOW || conditionType == X86_CONDITION_CARRY )
		{
			x64Gen_writeU8(x64GenContext, 0x0F);
			x64Gen_writeU8(x64GenContext, 0x92);
			x64Gen_writeU8(x64GenContext, 0x84);
			x64Gen_writeU8(x64GenContext, 0x24);
			x64Gen_writeU32(x64GenContext, (uint32)memoryImmU32);
		}
		else if( conditionType == X86_CONDITION_NOT_CARRY )
		{
			x64Gen_writeU8(x64GenContext, 0x0F);
			x64Gen_writeU8(x64GenContext, 0x93);
			x64Gen_writeU8(x64GenContext, 0x84);
			x64Gen_writeU8(x64GenContext, 0x24);
			x64Gen_writeU32(x64GenContext, (uint32)memoryImmU32);
		}
		else if( conditionType == X86_CONDITION_UNSIGNED_BELOW_EQUAL )
		{
			x64Gen_writeU8(x64GenContext, 0x0F);
			x64Gen_writeU8(x64GenContext, 0x96);
			x64Gen_writeU8(x64GenContext, 0x84);
			x64Gen_writeU8(x64GenContext, 0x24);
			x64Gen_writeU32(x64GenContext, (uint32)memoryImmU32);
		}
		else if( conditionType == X86_CONDITION_SIGNED_GREATER )
		{
			x64Gen_writeU8(x64GenContext, 0x0F);
			x64Gen_writeU8(x64GenContext, 0x9F);
			x64Gen_writeU8(x64GenContext, 0x84);
			x64Gen_writeU8(x64GenContext, 0x24);
			x64Gen_writeU32(x64GenContext, (uint32)memoryImmU32);
		}
		else if( conditionType == X86_CONDITION_SIGNED_GREATER_EQUAL )
		{
			x64Gen_writeU8(x64GenContext, 0x0F);
			x64Gen_writeU8(x64GenContext, 0x9D);
			x64Gen_writeU8(x64GenContext, 0x84);
			x64Gen_writeU8(x64GenContext, 0x24);
			x64Gen_writeU32(x64GenContext, (uint32)memoryImmU32);
		}
		else if( conditionType == X86_CONDITION_SIGNED_LESS )
		{
			x64Gen_writeU8(x64GenContext, 0x0F);
			x64Gen_writeU8(x64GenContext, 0x9C);
			x64Gen_writeU8(x64GenContext, 0x84);
			x64Gen_writeU8(x64GenContext, 0x24);
			x64Gen_writeU32(x64GenContext, (uint32)memoryImmU32);
		}
		else if( conditionType == X86_CONDITION_SIGNED_LESS_EQUAL )
		{
			x64Gen_writeU8(x64GenContext, 0x0F);
			x64Gen_writeU8(x64GenContext, 0x9E);
			x64Gen_writeU8(x64GenContext, 0x84);
			x64Gen_writeU8(x64GenContext, 0x24);
			x64Gen_writeU32(x64GenContext, (uint32)memoryImmU32);
		}
		else if( conditionType == X86_CONDITION_SIGN )
		{
			x64Gen_writeU8(x64GenContext, 0x0F);
			x64Gen_writeU8(x64GenContext, 0x98);
			x64Gen_writeU8(x64GenContext, 0x84);
			x64Gen_writeU8(x64GenContext, 0x24);
			x64Gen_writeU32(x64GenContext, (uint32)memoryImmU32);
		}
		else if( conditionType == X86_CONDITION_PARITY )
		{
			x64Gen_writeU8(x64GenContext, 0x0F);
			x64Gen_writeU8(x64GenContext, 0x9A);
			x64Gen_writeU8(x64GenContext, 0x84);
			x64Gen_writeU8(x64GenContext, 0x24);
			x64Gen_writeU32(x64GenContext, (uint32)memoryImmU32);
		}
		else
			assert_dbg();
	}
}

void x64Gen_setcc_reg64b(x64GenContext_t* x64GenContext, sint32 conditionType, sint32 dataRegister)
{
	if (conditionType == X86_CONDITION_NOT_EQUAL)
	{
		if (dataRegister >= 8)
			x64Gen_writeU8(x64GenContext, 0x41);
		else if (dataRegister >= 4)
			x64Gen_writeU8(x64GenContext, 0x40);
		x64Gen_writeU8(x64GenContext, 0x0F);
		x64Gen_writeU8(x64GenContext, 0x95);
		x64Gen_writeU8(x64GenContext, 0xC0 + (dataRegister & 7));
	}
	else if (conditionType == X86_CONDITION_EQUAL)
	{
		if (dataRegister >= 8)
			x64Gen_writeU8(x64GenContext, 0x41);
		else if (dataRegister >= 4)
			x64Gen_writeU8(x64GenContext, 0x40);
		x64Gen_writeU8(x64GenContext, 0x0F);
		x64Gen_writeU8(x64GenContext, 0x94);
		x64Gen_writeU8(x64GenContext, 0xC0 + (dataRegister & 7));
	}
	else
		assert_dbg();
}

void x64Gen_bt_mem8(x64GenContext_t* x64GenContext, sint32 memoryRegister, uint32 memoryImmU32, uint8 bitIndex)
{
	sint32 memoryImmS32 = (sint32)memoryImmU32;
	if( memoryRegister != X86_REG_RSP )
		assert_dbg();
	if( memoryImmS32 >= -128 && memoryImmS32 <= 127 )
	{
		x64Gen_writeU8(x64GenContext, 0x0F);
		x64Gen_writeU8(x64GenContext, 0xBA);
		x64Gen_writeU8(x64GenContext, 0x64);
		x64Gen_writeU8(x64GenContext, 0x24);
		x64Gen_writeU8(x64GenContext, (uint8)memoryImmU32);
		x64Gen_writeU8(x64GenContext, bitIndex);
	}
	else
	{
		x64Gen_writeU8(x64GenContext, 0x0F);
		x64Gen_writeU8(x64GenContext, 0xBA);
		x64Gen_writeU8(x64GenContext, 0xA4);
		x64Gen_writeU8(x64GenContext, 0x24);
		x64Gen_writeU32(x64GenContext, memoryImmU32);
		x64Gen_writeU8(x64GenContext, bitIndex);
	}
}

void x64Gen_cmc(x64GenContext_t* x64GenContext)
{
	x64Gen_writeU8(x64GenContext, 0xF5);
}

void x64Gen_jmp_imm32(x64GenContext_t* x64GenContext, uint32 destImm32)
{
	x64Gen_writeU8(x64GenContext, 0xE9);
	x64Gen_writeU32(x64GenContext, destImm32);
}

void x64Gen_jmp_memReg64(x64GenContext_t* x64GenContext, sint32 memRegister, uint32 immU32)
{
	if( memRegister == X86_REG_NONE )
	{
		assert_dbg();
	}
	if( memRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x41);
	sint32 immS32 = (sint32)immU32;
	if( immS32 == 0 )
	{
		x64Gen_writeU8(x64GenContext, 0xFF);
		x64Gen_writeU8(x64GenContext, 0x20+(memRegister&7));
	}
	else if( immS32 >= -128 && immS32 <= 127 )
	{
		x64Gen_writeU8(x64GenContext, 0xFF);
		x64Gen_writeU8(x64GenContext, 0x60+(memRegister&7));
		x64Gen_writeU8(x64GenContext, (uint8)immU32);
	}
	else
	{
		x64Gen_writeU8(x64GenContext, 0xFF);
		x64Gen_writeU8(x64GenContext, 0xA0+(memRegister&7));
		x64Gen_writeU32(x64GenContext, immU32);
	}
}

void x64Gen_jmpc_far(x64GenContext_t* x64GenContext, sint32 conditionType, sint32 relativeDest)
{
	if( conditionType == X86_CONDITION_NONE )
	{
		x64Gen_writeU8(x64GenContext, 0xE9);
	}
	else if( conditionType == X86_CONDITION_UNSIGNED_BELOW || conditionType == X86_CONDITION_CARRY )
	{
		x64Gen_writeU8(x64GenContext, 0x0F);
		x64Gen_writeU8(x64GenContext, 0x82);
	}
	else if( conditionType == X86_CONDITION_NOT_CARRY || conditionType == X86_CONDITION_UNSIGNED_ABOVE_EQUAL )
	{
		x64Gen_writeU8(x64GenContext, 0x0F);
		x64Gen_writeU8(x64GenContext, 0x83);
	}
	else if( conditionType == X86_CONDITION_EQUAL )
	{
		x64Gen_writeU8(x64GenContext, 0x0F);
		x64Gen_writeU8(x64GenContext, 0x84);
	}
	else if( conditionType == X86_CONDITION_NOT_EQUAL )
	{
		x64Gen_writeU8(x64GenContext, 0x0F);
		x64Gen_writeU8(x64GenContext, 0x85);
	}
	else if( conditionType == X86_CONDITION_UNSIGNED_BELOW_EQUAL )
	{
		x64Gen_writeU8(x64GenContext, 0x0F);
		x64Gen_writeU8(x64GenContext, 0x86);
	}
	else if( conditionType == X86_CONDITION_UNSIGNED_ABOVE )
	{
		x64Gen_writeU8(x64GenContext, 0x0F);
		x64Gen_writeU8(x64GenContext, 0x87);
	}
	else if( conditionType == X86_CONDITION_SIGN )
	{
		x64Gen_writeU8(x64GenContext, 0x0F);
		x64Gen_writeU8(x64GenContext, 0x88);
	}
	else if( conditionType == X86_CONDITION_NOT_SIGN )
	{
		x64Gen_writeU8(x64GenContext, 0x0F);
		x64Gen_writeU8(x64GenContext, 0x89);
	}
	else if( conditionType == X86_CONDITION_PARITY )
	{
		x64Gen_writeU8(x64GenContext, 0x0F);
		x64Gen_writeU8(x64GenContext, 0x8A);
	}
	else if( conditionType == X86_CONDITION_SIGNED_LESS )
	{
		x64Gen_writeU8(x64GenContext, 0x0F);
		x64Gen_writeU8(x64GenContext, 0x8C);
	}
	else if( conditionType == X86_CONDITION_SIGNED_GREATER_EQUAL )
	{
		x64Gen_writeU8(x64GenContext, 0x0F);
		x64Gen_writeU8(x64GenContext, 0x8D);
	}
	else if( conditionType == X86_CONDITION_SIGNED_LESS_EQUAL )
	{
		x64Gen_writeU8(x64GenContext, 0x0F);
		x64Gen_writeU8(x64GenContext, 0x8E);
	}
	else if( conditionType == X86_CONDITION_SIGNED_GREATER )
	{
		x64Gen_writeU8(x64GenContext, 0x0F);
		x64Gen_writeU8(x64GenContext, 0x8F);
	}
	else
		assert_dbg();
	x64Gen_writeU32(x64GenContext, (uint32)relativeDest);
}

void x64Gen_jmpc_near(x64GenContext_t* x64GenContext, sint32 conditionType, sint32 relativeDest)
{
	if (conditionType == X86_CONDITION_NONE)
	{
		x64Gen_writeU8(x64GenContext, 0xEB);
	}
	else if (conditionType == X86_CONDITION_UNSIGNED_BELOW || conditionType == X86_CONDITION_CARRY)
	{
		x64Gen_writeU8(x64GenContext, 0x72);
	}
	else if (conditionType == X86_CONDITION_NOT_CARRY || conditionType == X86_CONDITION_UNSIGNED_ABOVE_EQUAL)
	{
		x64Gen_writeU8(x64GenContext, 0x73);
	}
	else if (conditionType == X86_CONDITION_EQUAL)
	{
		x64Gen_writeU8(x64GenContext, 0x74);
	}
	else if (conditionType == X86_CONDITION_NOT_EQUAL)
	{
		x64Gen_writeU8(x64GenContext, 0x75);
	}
	else if (conditionType == X86_CONDITION_UNSIGNED_BELOW_EQUAL)
	{
		x64Gen_writeU8(x64GenContext, 0x76);
	}
	else if (conditionType == X86_CONDITION_UNSIGNED_ABOVE)
	{
		x64Gen_writeU8(x64GenContext, 0x77);
	}
	else if (conditionType == X86_CONDITION_SIGN)
	{
		x64Gen_writeU8(x64GenContext, 0x78);
	}
	else if (conditionType == X86_CONDITION_NOT_SIGN)
	{
		x64Gen_writeU8(x64GenContext, 0x79);
	}
	else if (conditionType == X86_CONDITION_PARITY)
	{
		x64Gen_writeU8(x64GenContext, 0x7A);
	}
	else if (conditionType == X86_CONDITION_SIGNED_LESS)
	{
		x64Gen_writeU8(x64GenContext, 0x7C);
	}
	else if (conditionType == X86_CONDITION_SIGNED_GREATER_EQUAL)
	{
		x64Gen_writeU8(x64GenContext, 0x7D);
	}
	else if (conditionType == X86_CONDITION_SIGNED_LESS_EQUAL)
	{
		x64Gen_writeU8(x64GenContext, 0x7E);
	}
	else if (conditionType == X86_CONDITION_SIGNED_GREATER)
	{
		x64Gen_writeU8(x64GenContext, 0x7F);
	}
	else
		assert_dbg();
	x64Gen_writeU8(x64GenContext, (uint8)relativeDest);
}

void x64Gen_push_reg64(x64GenContext_t* x64GenContext, sint32 srcRegister)
{
	if( srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x41);
	x64Gen_writeU8(x64GenContext, 0x50+(srcRegister&7));
}

void x64Gen_pop_reg64(x64GenContext_t* x64GenContext, sint32 destRegister)
{
	if( destRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x41);
	x64Gen_writeU8(x64GenContext, 0x58+(destRegister&7));
}

void x64Gen_jmp_reg64(x64GenContext_t* x64GenContext, sint32 srcRegister)
{
	if( srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x41);
	x64Gen_writeU8(x64GenContext, 0xFF);
	x64Gen_writeU8(x64GenContext, 0xE0+(srcRegister&7));
}

void x64Gen_call_reg64(x64GenContext_t* x64GenContext, sint32 srcRegister)
{
	if( srcRegister >= 8 )
		x64Gen_writeU8(x64GenContext, 0x41);
	x64Gen_writeU8(x64GenContext, 0xFF);
	x64Gen_writeU8(x64GenContext, 0xD0+(srcRegister&7));
}

void x64Gen_ret(x64GenContext_t* x64GenContext)
{
	x64Gen_writeU8(x64GenContext, 0xC3);
}

void x64Gen_int3(x64GenContext_t* x64GenContext)
{
	x64Gen_writeU8(x64GenContext, 0xCC);
}

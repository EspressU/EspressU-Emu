#include "BackendX64.h"

void x64Gen_genSSEVEXPrefix2(x64GenContext_t* x64GenContext, sint32 xmmRegister1, sint32 xmmRegister2, bool use64BitMode)
{
	if( xmmRegister1 < 8 && xmmRegister2 < 8 && use64BitMode == false )
		return;
	uint8 v = 0x40;
	if( xmmRegister1 >= 8 )
		v |= 0x01;
	if( xmmRegister2 >= 8 )
		v |= 0x04;
	if( use64BitMode )
		v |= 0x08;
	x64Gen_writeU8(x64GenContext, v);
}

void x64Gen_genSSEVEXPrefix1(x64GenContext_t* x64GenContext, sint32 xmmRegister, bool use64BitMode)
{
	if( xmmRegister < 8 && use64BitMode == false )
		return;
	uint8 v = 0x40;
	if( use64BitMode )
		v |= 0x01;
	if( xmmRegister >= 8 )
		v |= 0x04;
	x64Gen_writeU8(x64GenContext, v);
}

void x64Gen_movaps_xmmReg_xmmReg(x64GenContext_t* x64GenContext, sint32 xmmRegisterDest, sint32 xmmRegisterSource)
{
	x64Gen_genSSEVEXPrefix2(x64GenContext, xmmRegisterSource, xmmRegisterDest, false);
	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0x28);
	x64Gen_writeU8(x64GenContext, 0xC0+(xmmRegisterDest&7)*8+(xmmRegisterSource&7));
}

void x64Gen_movupd_xmmReg_memReg128(x64GenContext_t* x64GenContext, sint32 xmmRegister, sint32 memRegister, uint32 memImmU32)
{
	if( memRegister == X86_REG_ESP )
	{
		x64Gen_writeU8(x64GenContext, 0x66);
		x64Gen_genSSEVEXPrefix1(x64GenContext, xmmRegister, false);
		x64Gen_writeU8(x64GenContext, 0x0F);
		x64Gen_writeU8(x64GenContext, 0x10);
		x64Gen_writeU8(x64GenContext, 0x84+(xmmRegister&7)*8);
		x64Gen_writeU8(x64GenContext, 0xE4);
		x64Gen_writeU32(x64GenContext, memImmU32);
	}
	else if( memRegister == X86_REG_NONE )
	{
		assert_dbg();
	}
	else
	{
		assert_dbg();
	}
}

void x64Gen_movupd_memReg128_xmmReg(x64GenContext_t* x64GenContext, sint32 xmmRegister, sint32 memRegister, uint32 memImmU32)
{
	if( memRegister == X86_REG_ESP )
	{
		x64Gen_writeU8(x64GenContext, 0x66);
		x64Gen_genSSEVEXPrefix1(x64GenContext, xmmRegister, false);
		x64Gen_writeU8(x64GenContext, 0x0F);
		x64Gen_writeU8(x64GenContext, 0x11);
		x64Gen_writeU8(x64GenContext, 0x84+(xmmRegister&7)*8);
		x64Gen_writeU8(x64GenContext, 0xE4);
		x64Gen_writeU32(x64GenContext, memImmU32);
	}
	else if( memRegister == X86_REG_NONE )
	{
		assert_dbg();
	}
	else
	{
		assert_dbg();
	}
}

void x64Gen_movddup_xmmReg_memReg64(x64GenContext_t* x64GenContext, sint32 xmmRegister, sint32 memRegister, uint32 memImmU32)
{
	if( memRegister == X86_REG_RSP )
	{
		x64Gen_writeU8(x64GenContext, 0xF2);
		if( xmmRegister >= 8 )
			x64Gen_writeU8(x64GenContext, 0x44);
		x64Gen_writeU8(x64GenContext, 0x0F);
		x64Gen_writeU8(x64GenContext, 0x12);
		x64Gen_writeU8(x64GenContext, 0x84+(xmmRegister&7)*8);
		x64Gen_writeU8(x64GenContext, 0xE4);
		x64Gen_writeU32(x64GenContext, memImmU32);
	}
	else if( memRegister == X86_REG_R15 )
	{
		x64Gen_writeU8(x64GenContext, 0xF2);
		x64Gen_genSSEVEXPrefix1(x64GenContext, xmmRegister, true);
		x64Gen_writeU8(x64GenContext, 0x0F);
		x64Gen_writeU8(x64GenContext, 0x12);
		x64Gen_writeU8(x64GenContext, 0x87+(xmmRegister&7)*8);
		x64Gen_writeU32(x64GenContext, memImmU32);
	}
	else if( memRegister == X86_REG_NONE )
	{
		assert_dbg();
	}
	else
	{
		assert_dbg();
	}
}

void x64Gen_movddup_xmmReg_xmmReg(x64GenContext_t* x64GenContext, sint32 xmmRegisterDest, sint32 xmmRegisterSrc)
{
	x64Gen_writeU8(x64GenContext, 0xF2);
	x64Gen_genSSEVEXPrefix2(x64GenContext, xmmRegisterSrc, xmmRegisterDest, false);
	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0x12);
	x64Gen_writeU8(x64GenContext, 0xC0+(xmmRegisterDest&7)*8+(xmmRegisterSrc&7));
}

void x64Gen_movhlps_xmmReg_xmmReg(x64GenContext_t* x64GenContext, sint32 xmmRegisterDest, sint32 xmmRegisterSrc)
{
	x64Gen_genSSEVEXPrefix2(x64GenContext, xmmRegisterSrc, xmmRegisterDest, false);
	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0x12);
	x64Gen_writeU8(x64GenContext, 0xC0+(xmmRegisterDest&7)*8+(xmmRegisterSrc&7));
}

void x64Gen_movsd_xmmReg_xmmReg(x64GenContext_t* x64GenContext, sint32 xmmRegisterDest, sint32 xmmRegisterSrc)
{
	x64Gen_writeU8(x64GenContext, 0xF2);
	x64Gen_genSSEVEXPrefix2(x64GenContext, xmmRegisterSrc, xmmRegisterDest, false);
	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0x10);
	x64Gen_writeU8(x64GenContext, 0xC0+(xmmRegisterDest&7)*8+(xmmRegisterSrc&7));
}

void x64Gen_movsd_memReg64_xmmReg(x64GenContext_t* x64GenContext, sint32 xmmRegister, sint32 memRegister, uint32 memImmU32)
{
	if( memRegister == X86_REG_NONE )
	{
		assert_dbg();
	}
	else if( memRegister == X86_REG_RSP )
	{
		x64Gen_writeU8(x64GenContext, 0xF2);
		x64Gen_genSSEVEXPrefix2(x64GenContext, 0, xmmRegister, false);
		x64Gen_writeU8(x64GenContext, 0x0F);
		x64Gen_writeU8(x64GenContext, 0x11);
		x64Gen_writeU8(x64GenContext, 0x84+(xmmRegister&7)*8);
		x64Gen_writeU8(x64GenContext, 0x24);
		x64Gen_writeU32(x64GenContext, memImmU32);
	}
	else
	{
		assert_dbg();
	}
}

void x64Gen_movsd_xmmReg_memReg64(x64GenContext_t* x64GenContext, sint32 xmmRegister, sint32 memRegister, uint32 memImmU32)
{
	if( memRegister == X86_REG_RSP )
	{
		x64Gen_writeU8(x64GenContext, 0xF2);
		x64Gen_genSSEVEXPrefix2(x64GenContext, 0, xmmRegister, false);
		x64Gen_writeU8(x64GenContext, 0x0F);
		x64Gen_writeU8(x64GenContext, 0x10);
		x64Gen_writeU8(x64GenContext, 0x84+(xmmRegister&7)*8);
		x64Gen_writeU8(x64GenContext, 0x24);
		x64Gen_writeU32(x64GenContext, memImmU32);
	}
	else if( memRegister == 15 )
	{
		x64Gen_writeU8(x64GenContext, 0x36);
		x64Gen_writeU8(x64GenContext, 0xF2);
		x64Gen_genSSEVEXPrefix2(x64GenContext, memRegister, xmmRegister, false);
		x64Gen_writeU8(x64GenContext, 0x0F);
		x64Gen_writeU8(x64GenContext, 0x10);
		x64Gen_writeU8(x64GenContext, 0x87+(xmmRegister&7)*8);
		x64Gen_writeU32(x64GenContext, memImmU32);
	}
	else
	{
		assert_dbg();
	}
}

void x64Gen_movlpd_xmmReg_memReg64(x64GenContext_t* x64GenContext, sint32 xmmRegister, sint32 memRegister, uint32 memImmU32)
{
	if( memRegister == X86_REG_NONE )
	{
		assert_dbg();
	}
	else if( memRegister == X86_REG_RSP )
	{
		x64Gen_writeU8(x64GenContext, 0x66);
		x64Gen_genSSEVEXPrefix2(x64GenContext, 0, xmmRegister, false);
		x64Gen_writeU8(x64GenContext, 0x0F);
		x64Gen_writeU8(x64GenContext, 0x12);
		x64Gen_writeU8(x64GenContext, 0x84+(xmmRegister&7)*8);
		x64Gen_writeU8(x64GenContext, 0x24);
		x64Gen_writeU32(x64GenContext, memImmU32);
	}
	else
	{
		assert_dbg();
	}
}

void x64Gen_unpcklpd_xmmReg_xmmReg(x64GenContext_t* x64GenContext, sint32 xmmRegisterDest, sint32 xmmRegisterSrc)
{
	x64Gen_writeU8(x64GenContext, 0x66);
	x64Gen_genSSEVEXPrefix2(x64GenContext, xmmRegisterSrc, xmmRegisterDest, false);
	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0x14);
	x64Gen_writeU8(x64GenContext, 0xC0+(xmmRegisterDest&7)*8+(xmmRegisterSrc&7));
}

void x64Gen_unpckhpd_xmmReg_xmmReg(x64GenContext_t* x64GenContext, sint32 xmmRegisterDest, sint32 xmmRegisterSrc)
{
	x64Gen_writeU8(x64GenContext, 0x66);
	x64Gen_genSSEVEXPrefix2(x64GenContext, xmmRegisterSrc, xmmRegisterDest, false);
	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0x15);
	x64Gen_writeU8(x64GenContext, 0xC0+(xmmRegisterDest&7)*8+(xmmRegisterSrc&7));
}

void x64Gen_shufpd_xmmReg_xmmReg_imm8(x64GenContext_t* x64GenContext, sint32 xmmRegisterDest, sint32 xmmRegisterSrc, uint8 imm8)
{
	x64Gen_writeU8(x64GenContext, 0x66);
	x64Gen_genSSEVEXPrefix2(x64GenContext, xmmRegisterSrc, xmmRegisterDest, false);
	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0xC6);
	x64Gen_writeU8(x64GenContext, 0xC0+(xmmRegisterDest&7)*8+(xmmRegisterSrc&7));
	x64Gen_writeU8(x64GenContext, imm8);
}

void x64Gen_addsd_xmmReg_xmmReg(x64GenContext_t* x64GenContext, sint32 xmmRegisterDest, sint32 xmmRegisterSrc)
{
	x64Gen_writeU8(x64GenContext, 0xF2);
	x64Gen_genSSEVEXPrefix2(x64GenContext, xmmRegisterSrc, xmmRegisterDest, false);
	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0x58);
	x64Gen_writeU8(x64GenContext, 0xC0+(xmmRegisterDest&7)*8+(xmmRegisterSrc&7));
}

void x64Gen_addpd_xmmReg_xmmReg(x64GenContext_t* x64GenContext, sint32 xmmRegisterDest, sint32 xmmRegisterSrc)
{
	x64Gen_writeU8(x64GenContext, 0x66);
	x64Gen_genSSEVEXPrefix2(x64GenContext, xmmRegisterSrc, xmmRegisterDest, false);
	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0x58);
	x64Gen_writeU8(x64GenContext, 0xC0+(xmmRegisterDest&7)*8+(xmmRegisterSrc&7));
}

void x64Gen_subsd_xmmReg_xmmReg(x64GenContext_t* x64GenContext, sint32 xmmRegisterDest, sint32 xmmRegisterSrc)
{
	x64Gen_writeU8(x64GenContext, 0xF2);
	x64Gen_genSSEVEXPrefix2(x64GenContext, xmmRegisterSrc, xmmRegisterDest, false);
	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0x5C);
	x64Gen_writeU8(x64GenContext, 0xC0+(xmmRegisterDest&7)*8+(xmmRegisterSrc&7));
}

void x64Gen_subpd_xmmReg_xmmReg(x64GenContext_t* x64GenContext, sint32 xmmRegisterDest, sint32 xmmRegisterSrc)
{
	x64Gen_writeU8(x64GenContext, 0x66);
	x64Gen_genSSEVEXPrefix2(x64GenContext, xmmRegisterSrc, xmmRegisterDest, false);
	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0x5C);
	x64Gen_writeU8(x64GenContext, 0xC0+(xmmRegisterDest&7)*8+(xmmRegisterSrc&7));
}

void x64Gen_mulsd_xmmReg_xmmReg(x64GenContext_t* x64GenContext, sint32 xmmRegisterDest, sint32 xmmRegisterSrc)
{
	x64Gen_writeU8(x64GenContext, 0xF2);
	x64Gen_genSSEVEXPrefix2(x64GenContext, xmmRegisterSrc, xmmRegisterDest, false);
	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0x59);
	x64Gen_writeU8(x64GenContext, 0xC0+(xmmRegisterDest&7)*8+(xmmRegisterSrc&7));
}

void x64Gen_mulpd_xmmReg_xmmReg(x64GenContext_t* x64GenContext, sint32 xmmRegisterDest, sint32 xmmRegisterSrc)
{
	x64Gen_writeU8(x64GenContext, 0x66);
	x64Gen_genSSEVEXPrefix2(x64GenContext, xmmRegisterSrc, xmmRegisterDest, false);
	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0x59);
	x64Gen_writeU8(x64GenContext, 0xC0+(xmmRegisterDest&7)*8+(xmmRegisterSrc&7));
}

void x64Gen_mulpd_xmmReg_memReg128(x64GenContext_t* x64GenContext, sint32 xmmRegister, sint32 memRegister, uint32 memImmU32)
{
	if (memRegister == X86_REG_NONE)
	{
		assert_dbg();
	}
	else if (memRegister == X86_REG_R14)
	{
		x64Gen_writeU8(x64GenContext, 0x66);
		x64Gen_writeU8(x64GenContext, (xmmRegister < 8) ? 0x41 : 0x45);
		x64Gen_writeU8(x64GenContext, 0x0F);
		x64Gen_writeU8(x64GenContext, 0x59);
		x64Gen_writeU8(x64GenContext, 0x86 + (xmmRegister & 7) * 8);
		x64Gen_writeU32(x64GenContext, memImmU32);
	}
	else
	{
		assert_dbg();
	}
}

void x64Gen_divsd_xmmReg_xmmReg(x64GenContext_t* x64GenContext, sint32 xmmRegisterDest, sint32 xmmRegisterSrc)
{
	x64Gen_writeU8(x64GenContext, 0xF2);
	x64Gen_genSSEVEXPrefix2(x64GenContext, xmmRegisterSrc, xmmRegisterDest, false);
	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0x5E);
	x64Gen_writeU8(x64GenContext, 0xC0+(xmmRegisterDest&7)*8+(xmmRegisterSrc&7));
}

void x64Gen_divpd_xmmReg_xmmReg(x64GenContext_t* x64GenContext, sint32 xmmRegisterDest, sint32 xmmRegisterSrc)
{
	x64Gen_writeU8(x64GenContext, 0x66);
	x64Gen_genSSEVEXPrefix2(x64GenContext, xmmRegisterSrc, xmmRegisterDest, false);
	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0x5E);
	x64Gen_writeU8(x64GenContext, 0xC0+(xmmRegisterDest&7)*8+(xmmRegisterSrc&7));
}

void x64Gen_comisd_xmmReg_xmmReg(x64GenContext_t* x64GenContext, sint32 xmmRegisterDest, sint32 xmmRegisterSrc)
{
	x64Gen_writeU8(x64GenContext, 0x66);
	x64Gen_genSSEVEXPrefix2(x64GenContext, xmmRegisterSrc, xmmRegisterDest, false);
	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0x2F);
	x64Gen_writeU8(x64GenContext, 0xC0+(xmmRegisterDest&7)*8+(xmmRegisterSrc&7));
}

void x64Gen_comisd_xmmReg_mem64Reg64(x64GenContext_t* x64GenContext, sint32 xmmRegisterDest, sint32 memoryReg, sint32 memImmS32)
{
	if( memoryReg == X86_REG_R15 )
	{
		x64Gen_writeU8(x64GenContext, 0x66);
		x64Gen_genSSEVEXPrefix1(x64GenContext, xmmRegisterDest, true);
		x64Gen_writeU8(x64GenContext, 0x0F);
		x64Gen_writeU8(x64GenContext, 0x2F);
		x64Gen_writeU8(x64GenContext, 0x87+(xmmRegisterDest&7)*8);
		x64Gen_writeU32(x64GenContext, (uint32)memImmS32);
	}
	else
		assert_dbg();
}

void x64Gen_ucomisd_xmmReg_xmmReg(x64GenContext_t* x64GenContext, sint32 xmmRegisterDest, sint32 xmmRegisterSrc)
{
	x64Gen_writeU8(x64GenContext, 0x66);
	x64Gen_genSSEVEXPrefix2(x64GenContext, xmmRegisterSrc, xmmRegisterDest, false);
	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0x2E);
	x64Gen_writeU8(x64GenContext, 0xC0+(xmmRegisterDest&7)*8+(xmmRegisterSrc&7));
}

void x64Gen_comiss_xmmReg_mem64Reg64(x64GenContext_t* x64GenContext, sint32 xmmRegisterDest, sint32 memoryReg, sint32 memImmS32)
{
	if (memoryReg == X86_REG_R15)
	{
		x64Gen_genSSEVEXPrefix1(x64GenContext, xmmRegisterDest, true);
		x64Gen_writeU8(x64GenContext, 0x0F);
		x64Gen_writeU8(x64GenContext, 0x2F);
		x64Gen_writeU8(x64GenContext, 0x87 + (xmmRegisterDest & 7) * 8);
		x64Gen_writeU32(x64GenContext, (uint32)memImmS32);
	}
	else
		assert_dbg();
}

void x64Gen_orps_xmmReg_mem128Reg64(x64GenContext_t* x64GenContext, sint32 xmmRegisterDest, uint32 memReg, uint32 memImmS32)
{
	if( memReg == X86_REG_R15 )
	{
		x64Gen_genSSEVEXPrefix2(x64GenContext, memReg, xmmRegisterDest, false);
		x64Gen_writeU8(x64GenContext, 0x0F);
		x64Gen_writeU8(x64GenContext, 0x56);
		x64Gen_writeU8(x64GenContext, 0x87+(xmmRegisterDest&7)*8);
		x64Gen_writeU32(x64GenContext, (uint32)memImmS32);
	}
	else
		assert_dbg();
}

void x64Gen_xorps_xmmReg_mem128Reg64(x64GenContext_t* x64GenContext, sint32 xmmRegisterDest, uint32 memReg, uint32 memImmS32)
{
	if( memReg == X86_REG_R15 )
	{
		x64Gen_genSSEVEXPrefix1(x64GenContext, xmmRegisterDest, true);
		x64Gen_writeU8(x64GenContext, 0x0F);
		x64Gen_writeU8(x64GenContext, 0x57);
		x64Gen_writeU8(x64GenContext, 0x87+(xmmRegisterDest&7)*8);
		x64Gen_writeU32(x64GenContext, (uint32)memImmS32);
	}
	else
		assert_dbg();
}

void x64Gen_andpd_xmmReg_memReg128(x64GenContext_t* x64GenContext, sint32 xmmRegister, sint32 memRegister, uint32 memImmU32)
{
	if (memRegister == X86_REG_NONE)
	{
		assert_dbg();
	}
	else if (memRegister == X86_REG_R14)
	{
		x64Gen_writeU8(x64GenContext, 0x66);
		x64Gen_writeU8(x64GenContext, (xmmRegister < 8) ? 0x41 : 0x45);
		x64Gen_writeU8(x64GenContext, 0x0F);
		x64Gen_writeU8(x64GenContext, 0x54);
		x64Gen_writeU8(x64GenContext, 0x86 + (xmmRegister & 7) * 8);
		x64Gen_writeU32(x64GenContext, memImmU32);
	}
	else
	{
		assert_dbg();
	}
}

void x64Gen_andps_xmmReg_mem128Reg64(x64GenContext_t* x64GenContext, sint32 xmmRegisterDest, uint32 memReg, uint32 memImmS32)
{
	if( memReg == X86_REG_R15 )
	{
		x64Gen_genSSEVEXPrefix1(x64GenContext, xmmRegisterDest, true);
		x64Gen_writeU8(x64GenContext, 0x0F);
		x64Gen_writeU8(x64GenContext, 0x54);
		x64Gen_writeU8(x64GenContext, 0x87+(xmmRegisterDest&7)*8);
		x64Gen_writeU32(x64GenContext, (uint32)memImmS32);
	}
	else
		assert_dbg();
}

void x64Gen_andps_xmmReg_xmmReg(x64GenContext_t* x64GenContext, sint32 xmmRegisterDest, sint32 xmmRegisterSrc)
{
	x64Gen_genSSEVEXPrefix2(x64GenContext, xmmRegisterSrc, xmmRegisterDest, false);
	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0x54);
	x64Gen_writeU8(x64GenContext, 0xC0+(xmmRegisterDest&7)*8+(xmmRegisterSrc&7));
}

void x64Gen_pcmpeqd_xmmReg_mem128Reg64(x64GenContext_t* x64GenContext, sint32 xmmRegisterDest, uint32 memReg, uint32 memImmS32)
{
	if( memReg == X86_REG_R15 )
	{
		x64Gen_writeU8(x64GenContext, 0x66);
		x64Gen_genSSEVEXPrefix1(x64GenContext, xmmRegisterDest, true);
		x64Gen_writeU8(x64GenContext, 0x0F);
		x64Gen_writeU8(x64GenContext, 0x76);
		x64Gen_writeU8(x64GenContext, 0x87+(xmmRegisterDest&7)*8);
		x64Gen_writeU32(x64GenContext, (uint32)memImmS32);
	}
	else
		assert_dbg();
}

void x64Gen_cvttpd2dq_xmmReg_xmmReg(x64GenContext_t* x64GenContext, sint32 xmmRegisterDest, sint32 xmmRegisterSrc)
{
	x64Gen_writeU8(x64GenContext, 0x66);
	x64Gen_genSSEVEXPrefix2(x64GenContext, xmmRegisterSrc, xmmRegisterDest, false);
	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0xE6);
	x64Gen_writeU8(x64GenContext, 0xC0+(xmmRegisterDest&7)*8+(xmmRegisterSrc&7));
}

void x64Gen_cvttsd2si_xmmReg_xmmReg(x64GenContext_t* x64GenContext, sint32 registerDest, sint32 xmmRegisterSrc)
{
	x64Gen_writeU8(x64GenContext, 0xF2);
	x64Gen_genSSEVEXPrefix2(x64GenContext, xmmRegisterSrc, registerDest, false);
	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0x2C);
	x64Gen_writeU8(x64GenContext, 0xC0+(registerDest&7)*8+(xmmRegisterSrc&7));
}

void x64Gen_cvtsi2sd_xmmReg_xmmReg(x64GenContext_t* x64GenContext, sint32 xmmRegisterDest, sint32 registerSrc)
{
	x64Gen_writeU8(x64GenContext, 0xF2);
	x64Gen_genSSEVEXPrefix2(x64GenContext, registerSrc, xmmRegisterDest, false);
	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0x2A);
	x64Gen_writeU8(x64GenContext, 0xC0+(xmmRegisterDest&7)*8+(registerSrc&7));
}

void x64Gen_cvtsd2ss_xmmReg_xmmReg(x64GenContext_t* x64GenContext, sint32 xmmRegisterDest, sint32 xmmRegisterSrc)
{
	x64Gen_writeU8(x64GenContext, 0xF2);
	x64Gen_genSSEVEXPrefix2(x64GenContext, xmmRegisterSrc, xmmRegisterDest, false);
	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0x5A);
	x64Gen_writeU8(x64GenContext, 0xC0+(xmmRegisterDest&7)*8+(xmmRegisterSrc&7));
}

void x64Gen_cvtpd2ps_xmmReg_xmmReg(x64GenContext_t* x64GenContext, sint32 xmmRegisterDest, sint32 xmmRegisterSrc)
{
	x64Gen_writeU8(x64GenContext, 0x66);
	x64Gen_genSSEVEXPrefix2(x64GenContext, xmmRegisterSrc, xmmRegisterDest, false);
	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0x5A);
	x64Gen_writeU8(x64GenContext, 0xC0+(xmmRegisterDest&7)*8+(xmmRegisterSrc&7));
}

void x64Gen_cvtps2pd_xmmReg_xmmReg(x64GenContext_t* x64GenContext, sint32 xmmRegisterDest, sint32 xmmRegisterSrc)
{
	x64Gen_genSSEVEXPrefix2(x64GenContext, xmmRegisterSrc, xmmRegisterDest, false);
	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0x5A);
	x64Gen_writeU8(x64GenContext, 0xC0+(xmmRegisterDest&7)*8+(xmmRegisterSrc&7));
}

void x64Gen_cvtss2sd_xmmReg_xmmReg(x64GenContext_t* x64GenContext, sint32 xmmRegisterDest, sint32 xmmRegisterSrc)
{
	x64Gen_writeU8(x64GenContext, 0xF3);
	x64Gen_genSSEVEXPrefix2(x64GenContext, xmmRegisterSrc, xmmRegisterDest, false);
	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0x5A);
	x64Gen_writeU8(x64GenContext, 0xC0+(xmmRegisterDest&7)*8+(xmmRegisterSrc&7));
}

void x64Gen_cvtpi2pd_xmmReg_mem64Reg64(x64GenContext_t* x64GenContext, sint32 xmmRegisterDest, sint32 memReg, sint32 memImmS32)
{
	if( memReg == X86_REG_RSP )
	{
		x64Gen_writeU8(x64GenContext, 0x66);
		x64Gen_genSSEVEXPrefix1(x64GenContext, xmmRegisterDest, false);
		x64Gen_writeU8(x64GenContext, 0x0F);
		x64Gen_writeU8(x64GenContext, 0x2A);
		x64Gen_writeU8(x64GenContext, 0x84+(xmmRegisterDest&7)*8);
		x64Gen_writeU8(x64GenContext, 0x24);
		x64Gen_writeU32(x64GenContext, (uint32)memImmS32);
	}
	else
	{
		assert_dbg();
	}
}

void x64Gen_cvtsd2si_reg64Low_xmmReg(x64GenContext_t* x64GenContext, sint32 registerDest, sint32 xmmRegisterSrc)
{
	x64Gen_writeU8(x64GenContext, 0xF2);
	x64Gen_genSSEVEXPrefix2(x64GenContext, xmmRegisterSrc, registerDest, false);
	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0x2D);
	x64Gen_writeU8(x64GenContext, 0xC0+(registerDest&7)*8+(xmmRegisterSrc&7));
}

void x64Gen_cvttsd2si_reg64Low_xmmReg(x64GenContext_t* x64GenContext, sint32 registerDest, sint32 xmmRegisterSrc)
{
	x64Gen_writeU8(x64GenContext, 0xF2);
	x64Gen_genSSEVEXPrefix2(x64GenContext, xmmRegisterSrc, registerDest, false);
	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0x2C);
	x64Gen_writeU8(x64GenContext, 0xC0+(registerDest&7)*8+(xmmRegisterSrc&7));
}

void x64Gen_sqrtsd_xmmReg_xmmReg(x64GenContext_t* x64GenContext, sint32 xmmRegisterDest, sint32 xmmRegisterSrc)
{
	x64Gen_writeU8(x64GenContext, 0xF2);
	x64Gen_genSSEVEXPrefix2(x64GenContext, xmmRegisterSrc, xmmRegisterDest, false);
	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0x51);
	x64Gen_writeU8(x64GenContext, 0xC0+(xmmRegisterDest&7)*8+(xmmRegisterSrc&7));
}

void x64Gen_sqrtpd_xmmReg_xmmReg(x64GenContext_t* x64GenContext, sint32 xmmRegisterDest, sint32 xmmRegisterSrc)
{
	x64Gen_writeU8(x64GenContext, 0x66);
	x64Gen_genSSEVEXPrefix2(x64GenContext, xmmRegisterSrc, xmmRegisterDest, false);
	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0x51);
	x64Gen_writeU8(x64GenContext, 0xC0+(xmmRegisterDest&7)*8+(xmmRegisterSrc&7));
}

void x64Gen_rcpss_xmmReg_xmmReg(x64GenContext_t* x64GenContext, sint32 xmmRegisterDest, sint32 xmmRegisterSrc)
{
	x64Gen_writeU8(x64GenContext, 0xF3);
	x64Gen_genSSEVEXPrefix2(x64GenContext, xmmRegisterSrc, xmmRegisterDest, false);
	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0x53);
	x64Gen_writeU8(x64GenContext, 0xC0+(xmmRegisterDest&7)*8+(xmmRegisterSrc&7));
}

void x64Gen_mulss_xmmReg_memReg64(x64GenContext_t* x64GenContext, sint32 xmmRegister, sint32 memRegister, uint32 memImmU32)
{
	if( memRegister == X86_REG_NONE )
	{
		assert_dbg();
	}
	else if( memRegister == 15 )
	{
		x64Gen_writeU8(x64GenContext, 0xF3);
		x64Gen_writeU8(x64GenContext, (xmmRegister<8)?0x41:0x45);
		x64Gen_writeU8(x64GenContext, 0x0F);
		x64Gen_writeU8(x64GenContext, 0x59);
		x64Gen_writeU8(x64GenContext, 0x87+(xmmRegister&7)*8);
		x64Gen_writeU32(x64GenContext, memImmU32);
	}
	else
	{
		assert_dbg();
	}
}

void x64Gen_movd_xmmReg_reg64Low32(x64GenContext_t* x64GenContext, sint32 xmmRegisterDest, sint32 registerSrc)
{
	x64Gen_writeU8(x64GenContext, 0x66);
	x64Gen_genSSEVEXPrefix2(x64GenContext, registerSrc, xmmRegisterDest, false);
	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0x6E);
	x64Gen_writeU8(x64GenContext, 0xC0+(xmmRegisterDest&7)*8+(registerSrc&7));
}

void x64Gen_movd_reg64Low32_xmmReg(x64GenContext_t* x64GenContext, sint32 registerDest, sint32 xmmRegisterSrc)
{
	x64Gen_writeU8(x64GenContext, 0x66);
	x64Gen_genSSEVEXPrefix2(x64GenContext, registerDest, xmmRegisterSrc, false);
	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0x7E);
	x64Gen_writeU8(x64GenContext, 0xC0+(xmmRegisterSrc&7)*8+(registerDest&7));
}

void x64Gen_movq_xmmReg_reg64(x64GenContext_t* x64GenContext, sint32 xmmRegisterDest, sint32 registerSrc)
{
	x64Gen_writeU8(x64GenContext, 0x66);
	x64Gen_genSSEVEXPrefix2(x64GenContext, registerSrc, xmmRegisterDest, true);
	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0x6E);
	x64Gen_writeU8(x64GenContext, 0xC0+(xmmRegisterDest&7)*8+(registerSrc&7));
}

void x64Gen_movq_reg64_xmmReg(x64GenContext_t* x64GenContext, sint32 registerDst, sint32 xmmRegisterSrc)
{
	x64Gen_writeU8(x64GenContext, 0x66);
	x64Gen_genSSEVEXPrefix2(x64GenContext, registerDst, xmmRegisterSrc, true);
	x64Gen_writeU8(x64GenContext, 0x0F);
	x64Gen_writeU8(x64GenContext, 0x7E);
	x64Gen_writeU8(x64GenContext, 0xC0+(xmmRegisterSrc&7)*8+(registerDst&7));
}
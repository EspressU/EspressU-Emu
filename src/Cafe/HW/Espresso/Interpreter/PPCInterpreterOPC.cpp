#include "../PPCState.h"
#include "PPCInterpreterInternal.h"
#include "PPCInterpreterHelper.h"

#include "Cafe/OS/libs/coreinit/coreinit_CodeGen.h"
#include <float.h>
#include "Cafe/HW/Latte/Core/LatteBufferCache.h"

void PPCInterpreter_MFMSR(PPCInterpreter_t* hCPU, uint32 Opcode)
{
	cemuLog_logDebug(LogType::Force, "Rare instruction: MFMSR");
	if (hCPU->sprExtended.msr & MSR_PR)
	{
		PPC_ASSERT(true);
		return;
	}
	int rD, rA, rB;
	PPC_OPC_TEMPL_X(Opcode, rD, rA, rB);
	hCPU->gpr[rD] = hCPU->sprExtended.msr;

	PPCInterpreter_nextInstruction(hCPU);

}

void PPCInterpreter_MTMSR(PPCInterpreter_t* hCPU, uint32 Opcode)
{
	cemuLog_logDebug(LogType::Force, "Rare instruction: MTMSR");
	if (hCPU->sprExtended.msr & MSR_PR)
	{
		PPC_ASSERT(true);
		return;
	}
	int rS, rA, rB;
	PPC_OPC_TEMPL_X(Opcode, rS, rA, rB);

	hCPU->sprExtended.msr = hCPU->gpr[rS];
	PPCInterpreter_nextInstruction(hCPU);
}

void PPCInterpreter_MTFSB1X(PPCInterpreter_t* hCPU, uint32 Opcode)
{
	cemuLog_logDebug(LogType::Force, "Rare instruction: MTFSB1X");
	int crbD, n1, n2;
	PPC_OPC_TEMPL_X(Opcode, crbD, n1, n2);
	if (crbD != 1 && crbD != 2)
	{
		hCPU->fpscr |= 1 << (31 - crbD);
	}
	if (Opcode & PPC_OPC_RC)
	{
		PPC_ASSERT(true);
	}

	PPCInterpreter_nextInstruction(hCPU);
}

void PPCInterpreter_MCRF(PPCInterpreter_t* hCPU, uint32 Opcode)
{
	uint32 crD, crS, b;
	PPC_OPC_TEMPL_X(Opcode, crD, crS, b);
	crD >>= 2;
	crS >>= 2;
	ppc_setCRField(hCPU, crD, ppc_getCRField(hCPU, crS));

	PPCInterpreter_nextInstruction(hCPU);
}

void PPCInterpreter_MFCR(PPCInterpreter_t* hCPU, uint32 Opcode)
{
	int rD, rA, rB;
	PPC_OPC_TEMPL_X(Opcode, rD, rA, rB);

	hCPU->gpr[rD] = hCPU->cr;
	PPCInterpreter_nextInstruction(hCPU);
}

void PPCInterpreter_MTCRF(PPCInterpreter_t* hCPU, uint32 Opcode)
{
	uint32 rS;
	uint32 crfMask;
	PPC_OPC_TEMPL_XFX(Opcode, rS, crfMask);

	const uint32 mask = ppc_MTCRFMaskToPackedMask(crfMask);
	hCPU->cr = (hCPU->cr & ~mask) | (hCPU->gpr[rS] & mask);

	PPCInterpreter_nextInstruction(hCPU);
}

void PPCInterpreter_MCRXR(PPCInterpreter_t* hCPU, uint32 Opcode)
{
	uint32 cr;
	cr = (Opcode >> (31 - 8)) & 7;
	cr >>= 2;

	uint32 xer = PPCInterpreter_getXER(hCPU);
	uint32 xerBits = (xer >> 28) & 0xF;

	const uint32 crField = ((xerBits & 1) << 3) | ((xerBits & 2) << 1) |
		((xerBits & 4) >> 1) | ((xerBits & 8) >> 3);
	ppc_setCRField(hCPU, cr, crField);

	PPCInterpreter_setXER(hCPU, xer&~0xF0000000);

	PPCInterpreter_nextInstruction(hCPU);
}

void PPCInterpreter_TLBIE(PPCInterpreter_t* hCPU, uint32 Opcode)
{
	int rS, rA, rB;
	PPC_OPC_TEMPL_X(Opcode, rS, rA, rB);

	PPCInterpreter_nextInstruction(hCPU);
}

void PPCInterpreter_TLBSYNC(PPCInterpreter_t* hCPU, uint32 Opcode)
{
	cemu_assert_unimplemented();
	PPCInterpreter_nextInstruction(hCPU);
}

void PPCInterpreter_BX(PPCInterpreter_t* hCPU, uint32 Opcode)
{
	uint32 li;
	PPC_OPC_TEMPL_I(Opcode, li);
	if ((Opcode & PPC_OPC_AA) == 0)
		li += (unsigned int)hCPU->instructionPointer;
	if (Opcode & PPC_OPC_LK)
	{
		hCPU->spr.LR = (unsigned int)hCPU->instructionPointer + 4;
		hCPU->instructionPointer = li;
        PPCCore_attemptToEnterAddr(hCPU, li);
		return;
	}
	PPCInterpreter_jumpToInstruction(hCPU, li);
}

void PPCInterpreter_BCX(PPCInterpreter_t* hCPU, uint32 Opcode)
{
	uint32 BO, BI, BD;
	PPC_OPC_TEMPL_B(Opcode, BO, BI, BD);
	if (!(BO & 4))
		hCPU->spr.CTR--;
	bool bo2 = (BO & 2) != 0;
	bool bo8 = (BO & 8) != 0;
	bool cr = ppc_getCRBit(hCPU, BI) != 0;
	if (((BO & 4) || ((hCPU->spr.CTR != 0) ^ bo2))
		&& ((BO & 16) || (!(cr ^ bo8))))
	{
		if (!(Opcode & PPC_OPC_AA))
		{
			BD += (unsigned int)hCPU->instructionPointer;
		}
		else
		{
			cemu_assert_unimplemented();
		}
		if (Opcode & PPC_OPC_LK)
			hCPU->spr.LR = ((unsigned int)hCPU->instructionPointer) + 4;
		PPCInterpreter_jumpToInstruction(hCPU, BD);
	}
	else
		PPCInterpreter_nextInstruction(hCPU);
}

void PPCInterpreter_BCLRX(PPCInterpreter_t* hCPU, uint32 Opcode)
{
	uint32 BO, BI, BD;
	PPC_OPC_TEMPL_XL(Opcode, BO, BI, BD);
	PPC_ASSERT(BD == 0);
	if (!(BO & 4))
	{
		if (hCPU->spr.CTR == 0)
		{
			PPC_ASSERT(true);
			cemuLog_logDebug(LogType::Force, "Decrementer underflow!");
		}
		hCPU->spr.CTR--;
	}
	bool bo2 = (BO & 2) ? true : false;
	bool bo8 = (BO & 8) ? true : false;
	bool cr = ppc_getCRBit(hCPU, BI) != 0;
	if (((BO & 4) || ((hCPU->spr.CTR != 0) ^ bo2))
		&& ((BO & 16) || (!(cr ^ bo8))))
	{
		BD = hCPU->spr.LR & 0xfffffffc;
		if (Opcode & PPC_OPC_LK)
		{
			hCPU->spr.LR = (unsigned int)hCPU->instructionPointer + 4;
		}
        PPCCore_attemptToEnterAddr(hCPU, BD);
		return;
	}
	else
	{
		BD = (unsigned int)hCPU->instructionPointer + 4;
		PPCInterpreter_nextInstruction(hCPU);
	}
}

void PPCInterpreter_BCCTR(PPCInterpreter_t* hCPU, uint32 Opcode)
{
	uint32 x = (unsigned int)hCPU->instructionPointer;
	uint32 BO, BI, BD;
	PPC_OPC_TEMPL_XL(Opcode, BO, BI, BD);
	PPC_ASSERT(BD == 0);
	PPC_ASSERT(!(BO & 2));
	bool bo8 = (BO & 8) ? true : false;
	bool cr = ppc_getCRBit(hCPU, BI) != 0;
	if ((BO & 16) || (!(cr ^ bo8)))
	{
		if (Opcode & PPC_OPC_LK)
		{
			hCPU->spr.LR = (unsigned int)hCPU->instructionPointer + 4;
			hCPU->instructionPointer = (unsigned int)(hCPU->spr.CTR & 0xfffffffc);
		}
		else
		{
			hCPU->instructionPointer = (unsigned int)(hCPU->spr.CTR & 0xfffffffc);
		}
	}
	else
	{
		hCPU->instructionPointer += 4;
	}
}

void PPCInterpreter_DCBT(PPCInterpreter_t* hCPU, uint32 Opcode)
{
	sint32 rA, rB;
	rA = (Opcode >> (31 - 15)) & 0x1F;
	rB = (Opcode >> (31 - 20)) & 0x1F;
	PPCInterpreter_nextInstruction(hCPU);
}

void PPCInterpreter_DCBST(PPCInterpreter_t* hCPU, uint32 Opcode)
{
	sint32 rA, rB;
	rA = (Opcode >> (31 - 15)) & 0x1F;
	rB = (Opcode >> (31 - 20)) & 0x1F;

	uint32 ea = (rA ? hCPU->gpr[rA] : 0) + hCPU->gpr[rB];

	LatteBufferCache_notifyDCFlush(ea, 32);

	PPCInterpreter_nextInstruction(hCPU);
}

void PPCInterpreter_DCBF(PPCInterpreter_t* hCPU, uint32 Opcode)
{
	sint32 rA, rB;
	rA = (Opcode >> (31 - 15)) & 0x1F;
	rB = (Opcode >> (31 - 20)) & 0x1F;

	uint32 ea = (rA ? hCPU->gpr[rA] : 0) + hCPU->gpr[rB];

	LatteBufferCache_notifyDCFlush(ea, 32);

	PPCInterpreter_nextInstruction(hCPU);
}

void PPCInterpreter_DCBZL(PPCInterpreter_t* hCPU, uint32 Opcode)
{
	PPCInterpreter_nextInstruction(hCPU);
}

void PPCInterpreter_DCBI(PPCInterpreter_t* hCPU, uint32 Opcode)
{
	PPCInterpreter_nextInstruction(hCPU);
}

void PPCInterpreter_ICBI(PPCInterpreter_t* hCPU, uint32 Opcode)
{
	sint32 rD, rA, rB;
	PPC_OPC_TEMPL_X(Opcode, rD, rA, rB);
	uint32 ea = (rA ? hCPU->gpr[rA] : 0) + hCPU->gpr[rB];
	coreinit::codeGenHandleICBI(ea);
	PPCInterpreter_nextInstruction(hCPU);
}

void PPCInterpreter_EIEIO(PPCInterpreter_t* hCPU, uint32 Opcode)
{
	PPCInterpreter_nextInstruction(hCPU);
}

void PPCInterpreter_SC(PPCInterpreter_t* hCPU, uint32 Opcode)
{
	cemuLog_logDebug(LogType::Force, "SC executed at 0x{:08x}", hCPU->instructionPointer);
	PPCInterpreter_nextInstruction(hCPU);
}

void PPCInterpreter_SYNC(PPCInterpreter_t* hCPU, uint32 Opcode)
{
	PPCInterpreter_nextInstruction(hCPU);
}

void PPCInterpreter_ISYNC(PPCInterpreter_t* hCPU, uint32 Opcode)
{
	PPCInterpreter_nextInstruction(hCPU);
}

void PPCInterpreter_RFI(PPCInterpreter_t* hCPU, uint32 Opcode)
{
	cemuLog_logDebug(LogType::Force, "RFI");
	hCPU->sprExtended.msr &= ~(0x87C0FF73 | 0x00040000);
	hCPU->sprExtended.msr |= hCPU->sprExtended.srr1 & 0x87c0ff73;
	hCPU->sprExtended.msr |= MSR_RI;
	hCPU->instructionPointer = (unsigned int)(hCPU->sprExtended.srr0);
}

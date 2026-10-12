#include "PPCInterpreterInternal.h"
#include "Cafe/OS/RPL/rpl.h"
#include "Cafe/GameProfile/GameProfile.h"
#include "Cafe/HW/Espresso/Debugger/Debugger.h"

thread_local PPCInterpreter_t* ppcInterpreterCurrentInstance;

uint64 ppcMainThreadDECCycleValue = 0;
uint64 ppcMainThreadDECCycleStart = 0;
uint64 ppcCyclesSince2000 = 0;
uint64 ppcCyclesSince2000TimerClock = 0;
uint64 ppcCyclesSince2000_UTC = 0;

PPCInterpreter_t* PPCInterpreter_createInstance(unsigned int Entrypoint)
{
	PPCInterpreter_t* pData;
	uint32 prefixAreaSize = 0x6000;
	pData = (PPCInterpreter_t*)((uint8*)malloc(sizeof(PPCInterpreter_t)+prefixAreaSize)+prefixAreaSize);
	memset((void*)pData, 0x00, sizeof(PPCInterpreter_t));
	pData->instructionPointer = (uint32)Entrypoint;
	pData->gpr[GPR_SP] = 0x00000000;
	pData->spr.LR = 0;
	return pData;
}

TLS_WORKAROUND_NOINLINE PPCInterpreter_t* PPCInterpreter_getCurrentInstance()
{
	return ppcInterpreterCurrentInstance;
}

TLS_WORKAROUND_NOINLINE void PPCInterpreter_setCurrentInstance(PPCInterpreter_t* hCPU)
{
	ppcInterpreterCurrentInstance = hCPU;
}

uint64 PPCInterpreter_getMainCoreCycleCounter()
{
	return PPCTimer_getFromRDTSC();
}

void PPCInterpreter_jumpToInstruction(PPCInterpreter_t* cpuInterpreter, uint32 newIP)
{
	cpuInterpreter->instructionPointer = (uint32)newIP;
}

void PPCInterpreter_setDEC(PPCInterpreter_t* hCPU, uint32 newValue)
{
	hCPU->sprExtended.DEC = newValue;
	ppcMainThreadDECCycleStart = PPCInterpreter_getMainCoreCycleCounter();
	ppcMainThreadDECCycleValue = newValue;
}

uint32 PPCInterpreter_getXER(PPCInterpreter_t* hCPU)
{
	uint32 xerValue = hCPU->spr.XER;
	xerValue &= ~(1 << XER_BIT_CA);
	xerValue &= ~(1 << XER_BIT_SO);
	xerValue &= ~(1 << XER_BIT_OV);
	if (hCPU->xer_ca)
		xerValue |= (1 << XER_BIT_CA);
	if (hCPU->xer_so)
		xerValue |= (1 << XER_BIT_SO);
	if (hCPU->xer_ov)
		xerValue |= (1 << XER_BIT_OV);
	return xerValue;
}

void PPCInterpreter_setXER(PPCInterpreter_t* hCPU, uint32 v)
{
	const uint32 XER_MASK = 0xE0FFFFFF;
	hCPU->spr.XER = v & XER_MASK;
	hCPU->xer_ca = (v >> XER_BIT_CA) & 1;
	hCPU->xer_so = (v >> XER_BIT_SO) & 1;
	hCPU->xer_ov = (v >> XER_BIT_OV) & 1;
}

uint32 PPCInterpreter_getCoreIndex(PPCInterpreter_t* hCPU)
{
	return hCPU->spr.UPIR;
};

uint32 PPCInterpreter_getCurrentCoreIndex()
{
	return PPCInterpreter_getCurrentInstance()->spr.UPIR;
};

uint8* PPCInterpreterGetStackPointer()
{
	return memory_getPointerFromVirtualOffset(PPCInterpreter_getCurrentInstance()->gpr[1]);
}

uint8* PPCInterpreter_PushAndReturnStackPointer(sint32 offset)
{
	PPCInterpreter_t* hCPU = PPCInterpreter_getCurrentInstance();
	uint8* result = memory_getPointerFromVirtualOffset(hCPU->gpr[1] - offset);
	hCPU->gpr[1] -= offset;
	return result;
}

void PPCInterpreterModifyStackPointer(sint32 offset)
{
	PPCInterpreter_getCurrentInstance()->gpr[1] -= offset;
}

uint32 RPLLoader_MakePPCCallable(void(*ppcCallableExport)(PPCInterpreter_t* hCPU));

uint32 PPCInterpreter_makeCallableExportDepr(void (*ppcCallableExport)(PPCInterpreter_t* hCPU))
{
	return RPLLoader_MakePPCCallable(ppcCallableExport);
}

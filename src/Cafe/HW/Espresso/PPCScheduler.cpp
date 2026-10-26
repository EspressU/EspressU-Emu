#include "Cafe/OS/libs/gx2/GX2.h"
#include "Cafe/HW/Latte/Core/Latte.h"
#include "Cafe/OS/libs/coreinit/coreinit_Alarm.h"
#include "Cafe/OS/libs/coreinit/coreinit_Thread.h"
#include "Cafe/HW/Latte/Core/LattePerformanceMonitor.h"

#include "Cafe/HW/Espresso/Recompiler/PPCRecompiler.h"
#include "Cafe/CafeSystem.h"
#include "Cafe/HW/Latte/Core/HostTuning.h"

uint32 ppcThreadQuantum = 45000;

void PPCInterpreter_relinquishTimeslice()
{
	PPCInterpreter_t* hCPU = PPCInterpreter_getCurrentInstance();
	if( hCPU->remainingCycles >= 0 )
	{
		hCPU->skippedCycles = hCPU->remainingCycles + 1;
		hCPU->remainingCycles = -1;
	}
}

void PPCCore_boostQuantum(sint32 numCycles)
{
	PPCInterpreter_t* hCPU = PPCInterpreter_getCurrentInstance();
	hCPU->remainingCycles += numCycles;
}

void PPCCore_deboostQuantum(sint32 numCycles)
{
	PPCInterpreter_t* hCPU = PPCInterpreter_getCurrentInstance();
	hCPU->remainingCycles -= numCycles;
}

namespace coreinit
{
	void __OSThreadSwitchToNext();
}

void PPCCore_switchToScheduler()
{
	cemu_assert_debug(__OSHasSchedulerLock() == false);
	cemu_assert_debug(PPCInterpreter_getCurrentInstance()->coreInterruptMask != 0 || CafeSystem::GetForegroundTitleId() == 0x000500001019e600);
	__OSLockScheduler();
	coreinit::__OSThreadSwitchToNext();
	__OSUnlockScheduler();
}

void PPCCore_switchToSchedulerWithLock()
{
	cemu_assert_debug(__OSHasSchedulerLock() == true);
	cemu_assert_debug(PPCInterpreter_getCurrentInstance()->coreInterruptMask != 0 || CafeSystem::GetForegroundTitleId() == 0x000500001019e600);
	coreinit::__OSThreadSwitchToNext();
}

void _PPCCore_callbackExit(PPCInterpreter_t* hCPU)
{
    PPCInterpreter_relinquishTimeslice();
    hCPU->instructionPointer = 0;
}

static void PPCCore_executeWithJIT(PPCInterpreter_t* hCPU)
{
    PPCRecompiler_attemptEnter(hCPU, hCPU->instructionPointer);
    while ((--hCPU->remainingCycles) >= 0)
    {
        PPCRecompiler_attemptEnter(hCPU, hCPU->instructionPointer);
        if (hCPU->remainingCycles >= 0)
            PPCInterpreterSlim_executeInstruction(hCPU);
    }
}

static void PPCCore_executeWithJITThread(PPCInterpreter_t* hCPU)
{
    PPCRecompiler_attemptEnterWithoutRecompile(hCPU, hCPU->instructionPointer);
    while ((--hCPU->remainingCycles) >= 0)
    {
        PPCRecompiler_attemptEnterWithoutRecompile(hCPU, hCPU->instructionPointer);
        if (hCPU->remainingCycles >= 0)
            PPCInterpreterSlim_executeInstruction(hCPU);
    }
}

static void PPCCore_executeInterpreter(PPCInterpreter_t* hCPU)
{
    PPCInterpreterSlim_executeTimeslice(hCPU);
}

void (*attemptEnterAddr)(PPCInterpreter_t* hCPU, uint32 enterAddress) = PPCInterpreter_jumpToInstruction;
void (*attemptEnter)(PPCInterpreter_t*) = PPCCore_executeInterpreter;
void (*attemptEnterThread)(PPCInterpreter_t*) = PPCCore_executeInterpreter;

void PPCCore_attemptToEnterAddr(PPCInterpreter_t* hCPU, uint32 enterAddress)
{
    hCPU->instructionPointer = enterAddress;
    attemptEnterAddr(hCPU, enterAddress);
}

void PPCCore_InitializePointer(bool recompilerEnabled)
{
    if (recompilerEnabled)
    {
        attemptEnterAddr = PPCRecompiler_attemptEnter;
        attemptEnter = PPCCore_executeWithJIT;
        attemptEnterThread = PPCCore_executeWithJITThread;
    }
    else
    {
        attemptEnterAddr = PPCInterpreter_jumpToInstruction;
        attemptEnter = PPCCore_executeInterpreter;
        attemptEnterThread = PPCCore_executeInterpreter;
        cemuLog_log(LogType::Force, "No JIT: running the block-cache interpreter, block linking {}",
            HostTuning::Get().interpreterBlockLinking.load() ? "on" : "off");
    }
}

PPCInterpreter_t* PPCCore_executeCallbackInternal(uint32 functionMPTR)
{
    cemu_assert_debug(functionMPTR != 0);
    PPCInterpreter_t* hCPU = PPCInterpreter_getCurrentInstance();
    uint32 lr = hCPU->spr.LR;
    uint32 ip = hCPU->instructionPointer;
    hCPU->gpr[1] -= 16 * 4;
    hCPU->spr.LR = PPCInterpreter_makeCallableExportDepr(_PPCCore_callbackExit);
    hCPU->instructionPointer = functionMPTR;
    while (true)
    {
        hCPU->remainingCycles = ppcThreadQuantum;
        hCPU->skippedCycles = 0;
        if (hCPU->remainingCycles > 0)
            attemptEnter(hCPU);

        if (hCPU->instructionPointer == 0)
        {
            hCPU->remainingCycles += hCPU->skippedCycles;
            hCPU->skippedCycles = 0;
            break;
        }
        coreinit::OSYieldThread();
    }
    hCPU->gpr[1] += 16 * 4;
    hCPU->spr.LR = lr;
    hCPU->instructionPointer = ip;
    return hCPU;
}

void PPCCore_init()
{
}

#include "Cafe/OS/common/OSCommon.h"
#include "Cafe/HW/Latte/ISA/RegDefines.h"
#include "GX2.h"
#include "GX2_Resource.h"
#include "Cafe/HW/Latte/Core/Latte.h"
#include "Cafe/HW/Latte/Core/LatteDraw.h"

MPTR gx2Mem_defaultAlloc = MPTR_NULL;
MPTR gx2Mem_defaultFree = MPTR_NULL;

void gx2Memory_GX2SetDefaultAllocator(MPTR defaultAllocFunc, MPTR defaulFreeFunc)
{
	gx2Mem_defaultAlloc = defaultAllocFunc;
	gx2Mem_defaultFree = defaulFreeFunc;
}

void _GX2DefaultAlloc_Alloc(PPCInterpreter_t* hCPU)
{
	hCPU->gpr[3] = hCPU->gpr[4];
	hCPU->gpr[4] = hCPU->gpr[5];
	hCPU->instructionPointer = gCoreinitData->MEMAllocFromDefaultHeapEx.GetMPTR();
}

void _GX2DefaultAlloc_Free(PPCInterpreter_t* hCPU)
{
	hCPU->gpr[3] = hCPU->gpr[4];
	hCPU->instructionPointer = gCoreinitData->MEMFreeToDefaultHeap.GetMPTR();
}

void gx2Export_GX2SetDefaultAllocator(PPCInterpreter_t* hCPU)
{
	cemuLog_log(LogType::GX2, "GX2SetDefaultAllocator(0x{:08x}, 0x{:08x})", hCPU->gpr[3], hCPU->gpr[4]);
	gx2Mem_defaultAlloc = hCPU->gpr[3];
	gx2Mem_defaultFree = hCPU->gpr[4];
	osLib_returnFromFunction(hCPU, 0);
}

void _GX2DefaultAllocR_Alloc(PPCInterpreter_t* hCPU)
{
	cemuLog_log(LogType::GX2, "GX2DefaultAllocate(0x{:08x}, 0x{:08x}, 0x{:08x})", hCPU->gpr[3], hCPU->gpr[4], hCPU->gpr[5]);
	hCPU->instructionPointer = gx2Mem_defaultAlloc;
}

void _GX2DefaultAllocR_Free(PPCInterpreter_t* hCPU)
{
	cemuLog_log(LogType::GX2, "GX2DefaultFree(0x{:08x}, 0x{:08x})", hCPU->gpr[3], hCPU->gpr[4]);
	hCPU->instructionPointer = gx2Mem_defaultFree;
}

namespace GX2
{
	void GX2MEMAllocatorsInit()
	{
		gx2Mem_defaultAlloc = PPCInterpreter_makeCallableExportDepr(_GX2DefaultAlloc_Alloc);
		gx2Mem_defaultFree = PPCInterpreter_makeCallableExportDepr(_GX2DefaultAlloc_Free);
		GX2::GX2RSetAllocator(PPCInterpreter_makeCallableExportDepr(_GX2DefaultAllocR_Alloc), PPCInterpreter_makeCallableExportDepr(_GX2DefaultAllocR_Free));
	}

	void GX2MemInit()
	{
		osLib_addFunction("gx2", "GX2SetDefaultAllocator", gx2Export_GX2SetDefaultAllocator);
	}
};

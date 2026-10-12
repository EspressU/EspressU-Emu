#include "Cafe/OS/common/OSCommon.h"
#include "Cafe/OS/libs/coreinit/coreinit.h"
#include "Cafe/HW/Latte/Core/LatteBufferCache.h"

#define LC_LOCKED_CACHE_GRANULARITY		(0x200)
#define LC_LOCKED_CACHE_SIZE			(0x4000)

#define LC_MASK_FREE			(0)
#define LC_MASK_RESERVED		(1)
#define LC_MASK_RESERVED_END	(2)

namespace coreinit
{
	uint8   lcCacheMask[PPC_CORE_COUNT][(LC_LOCKED_CACHE_SIZE + LC_LOCKED_CACHE_GRANULARITY - 1) / LC_LOCKED_CACHE_GRANULARITY] = { 0 };
	uint32  lcAllocatedBlocks[PPC_CORE_COUNT] = { 0 };

	MPTR lcAddr[] =
	{
		0xFFC00000,
		0xFFC40000,
		0xFFC80000
	};

	void coreinitExport_LCGetMaxSize(PPCInterpreter_t* hCPU)
	{
		osLib_returnFromFunction(hCPU, LC_LOCKED_CACHE_SIZE);
	}

	void coreinitExport_LCAlloc(PPCInterpreter_t* hCPU)
	{
		uint32 size = hCPU->gpr[3];
		if (size == 0 || size > LC_LOCKED_CACHE_SIZE)
		{
			debug_printf("LCAlloc: Invalid alloc size %d\n", size);
			osLib_returnFromFunction(hCPU, MPTR_NULL);
			return;
		}
		if ((size % LC_LOCKED_CACHE_GRANULARITY) != 0)
		{
			debug_printf("LCAlloc: Unaligned alloc size 0x%04x\n", size);
			size += (LC_LOCKED_CACHE_GRANULARITY - 1);
			size &= ~(LC_LOCKED_CACHE_GRANULARITY - 1);
		}
		uint32 coreIndex = PPCInterpreter_getCoreIndex(hCPU);
		uint8* cacheMask = lcCacheMask[coreIndex];
		uint32 entryMaskLength = size / LC_LOCKED_CACHE_GRANULARITY;
		for (uint32 i = 0; i <= (LC_LOCKED_CACHE_SIZE / LC_LOCKED_CACHE_GRANULARITY) - entryMaskLength; i++)
		{
			bool rangeIsFree = true;
			for (uint32 f = 0; f < entryMaskLength; f++)
			{
				if (cacheMask[i + f] != LC_MASK_FREE)
				{
					rangeIsFree = false;
					break;
				}
			}
			if (rangeIsFree)
			{
				MPTR allocAddr = lcAddr[coreIndex] + i * LC_LOCKED_CACHE_GRANULARITY;
				for (uint32 f = 0; f < entryMaskLength - 1; f++)
				{
					cacheMask[i + f] = LC_MASK_RESERVED;
				}
				cacheMask[i + entryMaskLength - 1] = LC_MASK_RESERVED_END;
				lcAllocatedBlocks[coreIndex] += entryMaskLength;
				osLib_returnFromFunction(hCPU, allocAddr);
				return;
			}
		}
		osLib_returnFromFunction(hCPU, MPTR_NULL);
	}

	void coreinitExport_LCDealloc(PPCInterpreter_t* hCPU)
	{
		uint32 coreIndex = PPCInterpreter_getCoreIndex(hCPU);
		uint8* cacheMask = lcCacheMask[coreIndex];

		MPTR deallocAddr = hCPU->gpr[3];
		if (deallocAddr < lcAddr[coreIndex] || deallocAddr >= (lcAddr[coreIndex] + LC_LOCKED_CACHE_SIZE))
		{
#ifdef CEMU_DEBUG_ASSERT
			cemuLog_log(LogType::Force, "LCDealloc(): Out of bounds");
#endif
			osLib_returnFromFunction(hCPU, 0);
			return;
		}
		uint32 relativeOffset = (uint32)deallocAddr - lcAddr[coreIndex];
		if ((relativeOffset % LC_LOCKED_CACHE_GRANULARITY) != 0)
		{
			debug_printf("LCDealloc: Offset alignment is invalid (0x%08x / 0x%08x)\n", deallocAddr, relativeOffset);
			osLib_returnFromFunction(hCPU, 0);
			return;
		}
		uint32 startIndex = relativeOffset / LC_LOCKED_CACHE_GRANULARITY;
		if (startIndex > 0 && (cacheMask[startIndex - 1] != LC_MASK_RESERVED_END && cacheMask[startIndex - 1] != LC_MASK_FREE))
		{
			debug_printf("LCDealloc: Offset is invalid (0x%08x / 0x%08x)\n", deallocAddr, relativeOffset);
			osLib_returnFromFunction(hCPU, 0);
			return;
		}
		for (uint32 i = startIndex; i < (LC_LOCKED_CACHE_SIZE / LC_LOCKED_CACHE_GRANULARITY); i++)
		{
			if (cacheMask[i] == LC_MASK_RESERVED_END)
			{
				cacheMask[i] = LC_MASK_FREE;
				lcAllocatedBlocks[coreIndex]--;
				break;
			}
			else if (cacheMask[i] == LC_MASK_FREE)
			{
				debug_printf("LCDealloc: Allocation mask error detected\n");
				break;
			}
			cacheMask[i] = LC_MASK_FREE;
			lcAllocatedBlocks[coreIndex]--;
		}
		osLib_returnFromFunction(hCPU, 0);
	}

	void coreinitExport_LCGetUnallocated(PPCInterpreter_t* hCPU)
	{
		uint32 unallocatedBytes = 0;
		uint32 coreIndex = PPCInterpreter_getCoreIndex(hCPU);
		unallocatedBytes = LC_LOCKED_CACHE_SIZE - (lcAllocatedBlocks[coreIndex] * LC_LOCKED_CACHE_GRANULARITY);
		osLib_returnFromFunction(hCPU, unallocatedBytes);
	}

	void coreinitExport_LCGetAllocatableSize(PPCInterpreter_t* hCPU)
	{
		debug_printf("LCGetAllocatableSize()\n");
		uint32 coreIndex = PPCInterpreter_getCoreIndex(hCPU);
		uint8* cacheMask = lcCacheMask[coreIndex];
		uint32 largestFreeRange = 0;
		uint32 currentRangeSize = 0;
		for (uint32 i = 0; i < LC_LOCKED_CACHE_SIZE / LC_LOCKED_CACHE_GRANULARITY; i++)
		{
			if (cacheMask[i] == LC_MASK_FREE)
			{
				currentRangeSize++;
			}
			else
			{
				largestFreeRange = std::max(largestFreeRange, currentRangeSize);
				currentRangeSize = 0;
			}
		}
		largestFreeRange = std::max(largestFreeRange, currentRangeSize);
		osLib_returnFromFunction(hCPU, largestFreeRange * LC_LOCKED_CACHE_GRANULARITY);
	}

	uint32 LCIsEnabled[PPC_CORE_COUNT] = { 0 };

	void coreinitExport_LCEnableDMA(PPCInterpreter_t* hCPU)
	{

		LCIsEnabled[PPCInterpreter_getCoreIndex(hCPU)]++;
		osLib_returnFromFunction(hCPU, 1);
	}

	sint32 _lcDisableErrorCounter = 0;

	void coreinitExport_LCDisableDMA(PPCInterpreter_t* hCPU)
	{
#ifndef PUBLIC_RELASE
		if (LCIsEnabled[PPCInterpreter_getCoreIndex(hCPU)] == 0)
			assert_dbg();
#endif
		LCIsEnabled[PPCInterpreter_getCoreIndex(hCPU)]--;
#ifdef CEMU_DEBUG_ASSERT
		if (LCIsEnabled[PPCInterpreter_getCoreIndex(hCPU)] == 0)
		{
			uint32 coreIndex = PPCInterpreter_getCoreIndex(hCPU);
			uint8* cacheMask = lcCacheMask[coreIndex];
			for (uint32 i = 0; i <= (LC_LOCKED_CACHE_SIZE / LC_LOCKED_CACHE_GRANULARITY); i++)
			{
				if (cacheMask[i] != LC_MASK_FREE)
				{
					if (_lcDisableErrorCounter < 15)
					{
						cemuLog_log(LogType::Force, "LC disabled but there is still memory allocated");
						_lcDisableErrorCounter++;
					}
				}
			}
		}
#endif

		osLib_returnFromFunction(hCPU, 1);
	}

	void coreinitExport_LCIsDMAEnabled(PPCInterpreter_t* hCPU)
	{
		osLib_returnFromFunction(hCPU, LCIsEnabled[PPCInterpreter_getCoreIndex(hCPU)] ? 1 : 0);
	}

	void coreinitExport_LCHardwareIsAvailable(PPCInterpreter_t* hCPU)
	{
		osLib_returnFromFunction(hCPU, 1);
	}

	void coreinitExport_LCLoadDMABlocks(PPCInterpreter_t* hCPU)
	{
		uint32 numBlocks = hCPU->gpr[5];
		if (numBlocks == 0)
			numBlocks = 128;
		uint32 transferSize = numBlocks * 32;
		uint8* destPtr = memory_getPointerFromVirtualOffset(hCPU->gpr[3]);
		uint8* srcPtr = memory_getPointerFromVirtualOffset(hCPU->gpr[4]);
		memcpy(destPtr, srcPtr, transferSize);

		osLib_returnFromFunction(hCPU, 0);
	}

	void coreinitExport_LCStoreDMABlocks(PPCInterpreter_t* hCPU)
	{
		uint32 numBlocks = hCPU->gpr[5];
		if (numBlocks == 0)
			numBlocks = 128;
		uint8* destPtr = memory_getPointerFromVirtualOffset(hCPU->gpr[3]);
		uint8* srcPtr = memory_getPointerFromVirtualOffset(hCPU->gpr[4]);
		memcpy_qwords(destPtr, srcPtr, numBlocks * (32 / sizeof(uint64)));

		LatteBufferCache_notifyDCFlush(hCPU->gpr[3], numBlocks * 32);

		osLib_returnFromFunction(hCPU, 0);
	}

	void coreinitExport_LCWaitDMAQueue(PPCInterpreter_t* hCPU)
	{
		uint32 len = hCPU->gpr[3];
		osLib_returnFromFunction(hCPU, 0);
	}

	void InitializeLC()
	{
		for (sint32 f = 0; f < PPC_CORE_COUNT; f++)
		{
			memset(lcCacheMask[f], LC_MASK_FREE, (LC_LOCKED_CACHE_SIZE + LC_LOCKED_CACHE_GRANULARITY - 1) / LC_LOCKED_CACHE_GRANULARITY);
		}

		osLib_addFunction("coreinit", "LCGetMaxSize", coreinitExport_LCGetMaxSize);

		osLib_addFunction("coreinit", "LCAlloc", coreinitExport_LCAlloc);
		osLib_addFunction("coreinit", "LCDealloc", coreinitExport_LCDealloc);
		osLib_addFunction("coreinit", "LCGetUnallocated", coreinitExport_LCGetUnallocated);
		osLib_addFunction("coreinit", "LCGetAllocatableSize", coreinitExport_LCGetAllocatableSize);

		osLib_addFunction("coreinit", "LCEnableDMA", coreinitExport_LCEnableDMA);
		osLib_addFunction("coreinit", "LCDisableDMA", coreinitExport_LCDisableDMA);
		osLib_addFunction("coreinit", "LCIsDMAEnabled", coreinitExport_LCIsDMAEnabled);
		osLib_addFunction("coreinit", "LCHardwareIsAvailable", coreinitExport_LCHardwareIsAvailable);

		osLib_addFunction("coreinit", "LCLoadDMABlocks", coreinitExport_LCLoadDMABlocks);
		osLib_addFunction("coreinit", "LCStoreDMABlocks", coreinitExport_LCStoreDMABlocks);
		osLib_addFunction("coreinit", "LCWaitDMAQueue", coreinitExport_LCWaitDMAQueue);

	}

}

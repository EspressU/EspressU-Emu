#pragma once
#include "Cafe/OS/libs/coreinit/coreinit_MEM.h"

namespace coreinit
{
	struct MEMUnitHeapBlock
	{
		MEMPTR<MEMUnitHeapBlock> nextBlock;
	};
	static_assert(sizeof(MEMUnitHeapBlock) == 4);

	struct MEMUnitHeap : public MEMHeapBase
	{
		 uint32 padding034;
		 uint32 padding038;
		 uint32 padding03C;
		 MEMPTR<MEMUnitHeapBlock> firstFreeBlock;
		 uint32be blockSize;
	};
	static_assert(sizeof(MEMUnitHeap) == 0x48);

	MEMHeapBase* MEMCreateUnitHeapEx(void* memStart, uint32 heapSize, uint32 memBlockSize, uint32 alignment, uint32 createFlags);
	void* MEMDestroyUnitHeap(MEMHeapHandle hHeap);
}

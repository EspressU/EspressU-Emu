#pragma once
#include "Cafe/OS/libs/coreinit/coreinit_MEM.h"

namespace coreinit
{
	struct MEMBlockHeapTrack2_t
	{
		 MEMPTR<void> addrStart;
		 MEMPTR<void> addrEnd;
		 uint32be isFree;
		 MEMPTR<void> previousBlock;
		 MEMPTR<void> nextBlock;
	};
	static_assert(sizeof(MEMBlockHeapTrack2_t) == 0x14);

	struct MEMBlockHeap2_t : MEMHeapBase
	{
		 uint8 ukn[0x50 - 0x34];
		 MEMBlockHeapTrack2_t track;
		 MEMPTR<void> headBlock;
		 MEMPTR<void> tailBlock;
		 MEMPTR<void> nextFreeBlock;
		 uint32be freeBlocksLeft;
		 uint8 padding[0x80 - 0x74];
	};
	static_assert(sizeof(MEMBlockHeap2_t) == 0x80);

	MEMHeapHandle MEMInitBlockHeap(MEMBlockHeap2_t* memStart, void* startAddr, void* endAddr, void* initTrackMem, uint32 initTrackMemSize, uint32 createFlags);
	void* MEMDestroyBlockHeap(MEMHeapHandle hHeap);

	sint32 MEMAddBlockHeapTracking(MPTR heap, MPTR trackMem, uint32 trackMemSize);

	void InitializeMEMBlockHeap();
}

#include "Cafe/OS/common/OSCommon.h"
#include "Cafe/OS/libs/coreinit/coreinit_MEM.h"
#include "Cafe/OS/libs/coreinit/coreinit_MEM_BlockHeap.h"

namespace coreinit
{
	MEMHeapHandle MEMInitBlockHeap(MEMBlockHeap2_t* memStart, void* startAddr, void* endAddr, void* initTrackMem, uint32 initTrackMemSize, uint32 createFlags)
	{
		if (memStart == nullptr || startAddr == nullptr || endAddr == nullptr || (uintptr_t)startAddr >= (uintptr_t)endAddr)
			return nullptr;

		if (initTrackMemSize == 0)
			initTrackMem = nullptr;
		else if (initTrackMem == nullptr)
			initTrackMemSize = 0;

		MEMInitHeapBase(memStart, MEMHeapMagic::BLOCK_HEAP, startAddr, endAddr, createFlags);

		memStart->track.addrStart = startAddr;
		memStart->track.addrEnd = (void*)((uintptr_t)endAddr - 1);
		memStart->track.isFree = 1;
		memStart->track.previousBlock = nullptr;
		memStart->track.nextBlock = nullptr;

		memStart->headBlock = &memStart->track;
		memStart->tailBlock = &memStart->track;
		memStart->nextFreeBlock = nullptr;
		memStart->freeBlocksLeft = 0;

		uint8 flags = memStart->flags;
		if(HAS_FLAG(flags, MEM_HEAP_OPTION_FILL))
		{
			cemu_assert_unimplemented();
		}

		if(initTrackMemSize != 0)
		{
			sint32 result = MEMAddBlockHeapTracking(MEMPTR<void>(memStart).GetMPTR(), MEMPTR<void>(initTrackMem).GetMPTR(), initTrackMemSize);
			if(result != 0)
			{
				MEMBaseDestroyHeap(memStart);
				return nullptr;
			}
		}

		MEMHeapTable_Add(memStart);
		return memStart;
	}

	void* MEMDestroyBlockHeap(MEMHeapHandle hHeap)
	{
		if (!hHeap)
			return nullptr;
		cemu_assert_debug(hHeap->magic == MEMHeapMagic::BLOCK_HEAP);
		MEMBaseDestroyHeap(hHeap);
		MEMHeapTable_Remove(hHeap);
		memset(hHeap, 0x00, sizeof(MEMBlockHeap2_t));
		return hHeap;
	}

	sint32 MEMGetAllocatableSizeForBlockHeapEx(MEMBlockHeap2_t* blockHeap, sint32 alignment)
	{
		if (!blockHeap || blockHeap->magic != MEMHeapMagic::BLOCK_HEAP)
		{
			cemuLog_log(LogType::Force, "MEMGetAllocatableSizeForBlockHeapEx(): Not a valid block heap");
			return 0;
		}
		if (alignment < 0)
			alignment = -alignment;
		else if (alignment == 0)
			alignment = 4;

		if (HAS_FLAG(blockHeap->flags, MEM_HEAP_OPTION_THREADSAFE))
			OSUninterruptibleSpinLock_Acquire(&blockHeap->spinlock);

		MEMBlockHeapTrack2_t* track = (MEMBlockHeapTrack2_t*)blockHeap->headBlock.GetPtr();
		uint32 allocatableSize = 0;
		while (track)
		{
			if (track->isFree != 0)
			{
				uint32 addrStart = track->addrStart.GetMPTR();
				uint32 addrEnd = track->addrEnd.GetMPTR();
				uint32 alignmentMinusOne = alignment - 1;
				uint32 alignedAddrStart = ((addrStart + alignmentMinusOne) / alignment) * alignment;
				if (alignedAddrStart <= addrEnd)
				{
					uint32 size = (addrEnd + 1) - alignedAddrStart;
					allocatableSize = std::max(allocatableSize, size);
				}
			}
			track = (MEMBlockHeapTrack2_t*)track->nextBlock.GetPtr();
		}

		if (HAS_FLAG(blockHeap->flags, MEM_HEAP_OPTION_THREADSAFE))
			OSUninterruptibleSpinLock_Release(&blockHeap->spinlock);

		return allocatableSize;
	}

	void MEMDumpBlockHeap(MPTR heap);

	typedef struct
	{
		 uint32 addrStart;
		 uint32 addrEnd;
		 uint32 isFree;
		 MPTR previousBlock;
		 MPTR nextBlock;
	}MEMBlockHeapTrackDEPR;

	typedef struct
	{
		 uint32 ukn00;
		 uint32 ukn04;
		 uint32 trackArray;
		 uint32 trackCount;
	}MEMBlockHeapGroupDEPR;

	typedef struct
	{
		 betype<MEMHeapMagic> magic;
		 MEMLink_t		link;
		 MEMList_t		childList;
		 MPTR			heapStart;
		 MPTR			heapEnd;
		 coreinit::OSSpinLock	spinlock;
		union
		{
			uint32       val;
			uint32		flags;
		}
		attribute;
		 uint8			ukn034[0x50 - 0x34];
		 MEMBlockHeapTrackDEPR nullBlockTrack;
		 MPTR			headBlock;
		 MPTR			tailBlock;
		 MPTR			nextFreeBlock;
		 uint32			freeBlocksLeft;
	}MEMBlockHeapDEPR;

	void _blockHeapDebugVerifyIfBlockIsLinked(MEMBlockHeapDEPR* blockHeapHead, MEMBlockHeapTrackDEPR* track)
	{
	}

	void _blockHeapDebugVerifyLinkOrder(MEMBlockHeapDEPR* blockHeapHead)
	{
	}

	sint32 MEMAddBlockHeapTracking(MPTR heap, MPTR trackMem, uint32 trackMemSize)
	{
		MEMBlockHeapDEPR* blockHeapHead = (MEMBlockHeapDEPR*)memory_getPointerFromVirtualOffset(heap);
		if (blockHeapHead->magic != MEMHeapMagic::BLOCK_HEAP)
		{
			return 0;
		}
		if (trackMem == MPTR_NULL || trackMemSize <= (sizeof(MEMBlockHeapGroupDEPR) + sizeof(MEMBlockHeapTrackDEPR)))
		{
			return -4;
		}
		uint32 trackCount = (trackMemSize - sizeof(MEMBlockHeapGroupDEPR)) / sizeof(MEMBlockHeapTrackDEPR);
		MEMBlockHeapGroupDEPR* group = (MEMBlockHeapGroupDEPR*)memory_getPointerFromVirtualOffset(trackMem);
		group->ukn00 = _swapEndianU32(0);
		group->ukn04 = _swapEndianU32(0);
		group->trackArray = _swapEndianU32(trackMem + sizeof(MEMBlockHeapGroupDEPR));
		group->trackCount = _swapEndianU32(trackCount);
		MEMBlockHeapTrackDEPR* track = (MEMBlockHeapTrackDEPR*)(group + 1);
		track[0].previousBlock = _swapEndianU32(0);
		if (trackCount > 1)
		{
			for (uint32 i = 0; i < trackCount - 1; i++)
			{
				track[i].nextBlock = _swapEndianU32(memory_getVirtualOffsetFromPointer(track + i + 1));
				track[i + 1].previousBlock = _swapEndianU32(0);
			}
		}

		__OSLockScheduler();
		track[trackCount - 1].nextBlock = blockHeapHead->nextFreeBlock;
		blockHeapHead->nextFreeBlock = _swapEndianU32(memory_getVirtualOffsetFromPointer(track));
		blockHeapHead->freeBlocksLeft = _swapEndianU32(_swapEndianU32(blockHeapHead->freeBlocksLeft) + trackCount);
		__OSUnlockScheduler();
		return 0;
	}

	uint32 MEMGetTrackingLeftInBlockHeap(MPTR heap)
	{
		MEMBlockHeapDEPR* blockHeapHead = (MEMBlockHeapDEPR*)memory_getPointerFromVirtualOffset(heap);
		if (blockHeapHead->magic != MEMHeapMagic::BLOCK_HEAP)
		{
			return 0;
		}
		return _swapEndianU32(blockHeapHead->freeBlocksLeft);
	}

	MPTR _MEMBlockHeap_GetFreeBlockTrack(MEMBlockHeapDEPR* blockHeapHead)
	{
		MPTR trackMPTR = _swapEndianU32(blockHeapHead->nextFreeBlock);
		MEMBlockHeapTrackDEPR* track = (MEMBlockHeapTrackDEPR*)memory_getPointerFromVirtualOffset(trackMPTR);
		MPTR nextFreeBlockMPTR = _swapEndianU32(track->nextBlock);
		track->nextBlock = _swapEndianU32(MPTR_NULL);
		blockHeapHead->nextFreeBlock = _swapEndianU32(nextFreeBlockMPTR);
		if (_swapEndianU32(blockHeapHead->freeBlocksLeft) == 0)
		{
			cemuLog_log(LogType::Force, "BlockHeap: No free blocks left");
			assert_dbg();
		}
		blockHeapHead->freeBlocksLeft = _swapEndianU32(_swapEndianU32(blockHeapHead->freeBlocksLeft) - 1);
		return trackMPTR;
	}

	void _MEMBlockHeap_ReleaseBlockTrack(MEMBlockHeapDEPR* blockHeapHead, MPTR trackMPTR)
	{
		MEMBlockHeapTrackDEPR* track = (MEMBlockHeapTrackDEPR*)memory_getPointerFromVirtualOffset(trackMPTR);
		track->nextBlock = blockHeapHead->nextFreeBlock;
		blockHeapHead->nextFreeBlock = _swapEndianU32(trackMPTR);
		blockHeapHead->freeBlocksLeft = _swapEndianU32(_swapEndianU32(blockHeapHead->freeBlocksLeft) + 1);
	}

	sint32 _MEMBlockHeap_AllocAtBlock(MEMBlockHeapDEPR* blockHeapHead, MEMBlockHeapTrackDEPR* track, MPTR allocationAddress, uint32 size)
	{
		MPTR trackMPTR = memory_getVirtualOffsetFromPointer(track);
		uint32 trackEndAddr = _swapEndianU32(track->addrEnd);
		uint32 prefixBlockSize = allocationAddress - _swapEndianU32(track->addrStart);
		uint32 suffixBlockSize = (_swapEndianU32(track->addrEnd) + 1) - (allocationAddress + size);
		if (prefixBlockSize > 0 && suffixBlockSize > 0)
		{
			if (_swapEndianU32(blockHeapHead->freeBlocksLeft) < 2)
				return -1;
		}
		else if (prefixBlockSize > 0 || suffixBlockSize > 0)
		{
			if (_swapEndianU32(blockHeapHead->freeBlocksLeft) < 1)
				return -2;
		}
		MPTR currentPreviousTrack = _swapEndianU32(track->previousBlock);
		if (track->isFree != _swapEndianU32(0))
		{
			MEMBlockHeapTrackDEPR* trackItr = (MEMBlockHeapTrackDEPR*)memory_getPointerFromVirtualOffsetAllowNull(_swapEndianU32(blockHeapHead->nextFreeBlock));
			while (trackItr)
			{
				if (trackItr == track)
					assert_dbg();
				trackItr = (MEMBlockHeapTrackDEPR*)memory_getPointerFromVirtualOffsetAllowNull(_swapEndianU32(trackItr->nextBlock));
			}
		}
		_blockHeapDebugVerifyLinkOrder(blockHeapHead);
		if (prefixBlockSize > 0)
		{
			uint32 blockRangeStart = _swapEndianU32(track->addrStart);
			uint32 blockRangeEnd = allocationAddress - 1;
			MPTR prefixTrackMPTR = _MEMBlockHeap_GetFreeBlockTrack(blockHeapHead);
			MEMBlockHeapTrackDEPR* prefixTrack = (MEMBlockHeapTrackDEPR*)memory_getPointerFromVirtualOffset(prefixTrackMPTR);
			prefixTrack->isFree = _swapEndianU32(1);
			prefixTrack->addrStart = _swapEndianU32(blockRangeStart);
			prefixTrack->addrEnd = _swapEndianU32(blockRangeEnd);
			if (blockHeapHead->headBlock == _swapEndianU32(trackMPTR))
				blockHeapHead->headBlock = _swapEndianU32(prefixTrackMPTR);
			if (_swapEndianU32(track->previousBlock) != MPTR_NULL)
			{
				MEMBlockHeapTrackDEPR* tempTrack = (MEMBlockHeapTrackDEPR*)memory_getPointerFromVirtualOffset(_swapEndianU32(track->previousBlock));
				tempTrack->nextBlock = _swapEndianU32(prefixTrackMPTR);
			}
			prefixTrack->nextBlock = _swapEndianU32(trackMPTR);
			prefixTrack->previousBlock = _swapEndianU32(currentPreviousTrack);
			currentPreviousTrack = prefixTrackMPTR;
		}
		track->isFree = _swapEndianU32(0);
		track->addrStart = _swapEndianU32(allocationAddress);
		track->addrEnd = _swapEndianU32(allocationAddress + size - 1);
		track->previousBlock = _swapEndianU32(currentPreviousTrack);
		currentPreviousTrack = trackMPTR;
		if (suffixBlockSize > 0)
		{
			uint32 blockRangeStart = allocationAddress + size;
			uint32 blockRangeEnd = trackEndAddr;
			MPTR suffixTrackMPTR = _MEMBlockHeap_GetFreeBlockTrack(blockHeapHead);
			MEMBlockHeapTrackDEPR* suffixTrack = (MEMBlockHeapTrackDEPR*)memory_getPointerFromVirtualOffset(suffixTrackMPTR);
			suffixTrack->isFree = _swapEndianU32(1);
			suffixTrack->addrStart = _swapEndianU32(blockRangeStart);
			suffixTrack->addrEnd = _swapEndianU32(blockRangeEnd);
			suffixTrack->previousBlock = _swapEndianU32(currentPreviousTrack);
			suffixTrack->nextBlock = track->nextBlock;
			if (_swapEndianU32(blockHeapHead->tailBlock) == trackMPTR)
				blockHeapHead->tailBlock = _swapEndianU32(suffixTrackMPTR);
			if (_swapEndianU32(track->nextBlock) != MPTR_NULL)
			{
				MEMBlockHeapTrackDEPR* tempTrack = (MEMBlockHeapTrackDEPR*)memory_getPointerFromVirtualOffset(_swapEndianU32(track->nextBlock));
				tempTrack->previousBlock = _swapEndianU32(suffixTrackMPTR);
			}
			track->nextBlock = _swapEndianU32(suffixTrackMPTR);
		}
		_blockHeapDebugVerifyLinkOrder(blockHeapHead);
		memset(memory_getPointerFromVirtualOffset(allocationAddress), 0x00, size);
		return 0;
	}

	MEMBlockHeapTrackDEPR* _MEMBlockHeap_FindBlockContaining(MEMBlockHeapDEPR* blockHeapHead, MPTR memAddr)
	{
		MPTR heapStart = _swapEndianU32(blockHeapHead->heapStart);
		MPTR heapEnd = _swapEndianU32(blockHeapHead->heapEnd);
		if (memAddr < heapStart)
			return NULL;
		if (memAddr > heapEnd)
			return NULL;
		uint32 distanceToStart = memAddr - heapStart;
		uint32 distanceToEnd = heapEnd - memAddr;
		MEMBlockHeapTrackDEPR* track = (MEMBlockHeapTrackDEPR*)memory_getPointerFromVirtualOffsetAllowNull(_swapEndianU32(blockHeapHead->headBlock));
		if (track == NULL)
			return NULL;
		while (track)
		{
			if (_swapEndianU32(track->addrStart) == memAddr)
				return track;
			track = (MEMBlockHeapTrackDEPR*)memory_getPointerFromVirtualOffsetAllowNull(_swapEndianU32(track->nextBlock));
		}
		return NULL;
	}

	MPTR MEMAllocFromBlockHeapEx(MPTR heap, uint32 size, sint32 alignment)
	{
		MEMBlockHeapDEPR* blockHeapHead = (MEMBlockHeapDEPR*)memory_getPointerFromVirtualOffset(heap);
		if (blockHeapHead->magic != MEMHeapMagic::BLOCK_HEAP)
		{
			return MPTR_NULL;
		}
		__OSLockScheduler();
		if (alignment == 0)
			alignment = 4;
		bool allocateAtEndOfBlock = false;
		if (alignment < 0)
		{
			allocateAtEndOfBlock = true;
			alignment = -alignment;
		}
		MEMBlockHeapTrackDEPR* track;
		if (allocateAtEndOfBlock)
			track = (MEMBlockHeapTrackDEPR*)memory_getPointerFromVirtualOffsetAllowNull(_swapEndianU32(blockHeapHead->tailBlock));
		else
			track = (MEMBlockHeapTrackDEPR*)memory_getPointerFromVirtualOffsetAllowNull(_swapEndianU32(blockHeapHead->headBlock));

		cemu_assert_debug(std::popcount<unsigned int>(alignment) == 1);
		while (track)
		{
			if (track->isFree != _swapEndianU32(0))
			{
				uint32 blockRangeStart = _swapEndianU32(track->addrStart);
				uint32 blockRangeEnd = _swapEndianU32(track->addrEnd) + 1;
				if (allocateAtEndOfBlock == false)
				{
					uint32 alignedBlockRangeStart = (blockRangeStart + alignment - 1) & ~(alignment - 1);
					if (alignedBlockRangeStart < blockRangeEnd)
					{
						uint32 allocRange = blockRangeEnd - alignedBlockRangeStart;
						if (allocRange >= size)
						{
							sint32 allocError = _MEMBlockHeap_AllocAtBlock(blockHeapHead, track, alignedBlockRangeStart, size);
							_blockHeapDebugVerifyLinkOrder(blockHeapHead);
							__OSUnlockScheduler();
							if (allocError == 0)
								return alignedBlockRangeStart;
							return MPTR_NULL;
						}
					}
				}
				else
				{
					uint32 alignedBlockRangeStart = (blockRangeEnd + 1 - size) & ~(alignment - 1);
					if (alignedBlockRangeStart >= blockRangeStart)
					{
						sint32 allocError = _MEMBlockHeap_AllocAtBlock(blockHeapHead, track, alignedBlockRangeStart, size);
						_blockHeapDebugVerifyLinkOrder(blockHeapHead);
						__OSUnlockScheduler();
						if (allocError == 0)
							return alignedBlockRangeStart;
						return MPTR_NULL;
					}
				}
			}
			if (allocateAtEndOfBlock)
				track = (MEMBlockHeapTrackDEPR*)memory_getPointerFromVirtualOffsetAllowNull(_swapEndianU32(track->previousBlock));
			else
				track = (MEMBlockHeapTrackDEPR*)memory_getPointerFromVirtualOffsetAllowNull(_swapEndianU32(track->nextBlock));
			if (track == nullptr)
				break;
		}
		__OSUnlockScheduler();
		return MPTR_NULL;
	}

	void MEMFreeToBlockHeap(MPTR heap, MPTR memAddr)
	{
		MEMBlockHeapDEPR* blockHeapHead = (MEMBlockHeapDEPR*)memory_getPointerFromVirtualOffset(heap);
		if (blockHeapHead->magic != MEMHeapMagic::BLOCK_HEAP)
		{
			return;
		}
		__OSLockScheduler();
		_blockHeapDebugVerifyLinkOrder(blockHeapHead);
		MEMBlockHeapTrackDEPR* block = _MEMBlockHeap_FindBlockContaining(blockHeapHead, memAddr);
		if (block != NULL)
		{
			MPTR blockMPTR = memory_getVirtualOffsetFromPointer(block);
#ifdef CEMU_DEBUG_ASSERT
			if (block->isFree != 0)
				assert_dbg();
			_blockHeapDebugVerifyLinkOrder(blockHeapHead);
#endif
			block->isFree = _swapEndianU32(1);
			if (_swapEndianU32(block->previousBlock) != MPTR_NULL)
			{
				MPTR previousBlockMPTR = _swapEndianU32(block->previousBlock);
				MEMBlockHeapTrackDEPR* previousBlock = (MEMBlockHeapTrackDEPR*)memory_getPointerFromVirtualOffset(previousBlockMPTR);
				if (_swapEndianU32(previousBlock->isFree) != 0)
				{
					previousBlock->addrEnd = block->addrEnd;
					previousBlock->nextBlock = block->nextBlock;
					if (_swapEndianU32(block->nextBlock) != MPTR_NULL)
					{
						MEMBlockHeapTrackDEPR* tempNextBlock = (MEMBlockHeapTrackDEPR*)memory_getPointerFromVirtualOffset(_swapEndianU32(block->nextBlock));
						tempNextBlock->previousBlock = _swapEndianU32(previousBlockMPTR);
					}
					_MEMBlockHeap_ReleaseBlockTrack(blockHeapHead, blockMPTR);
					_blockHeapDebugVerifyIfBlockIsLinked(blockHeapHead, (MEMBlockHeapTrackDEPR*)memory_getPointerFromVirtualOffset(blockMPTR));
					blockMPTR = previousBlockMPTR;
					block = previousBlock;
				}
			}
			if (_swapEndianU32(block->nextBlock) != MPTR_NULL)
			{
				MPTR nextBlockMPTR = _swapEndianU32(block->nextBlock);
				MEMBlockHeapTrackDEPR* nextBlock = (MEMBlockHeapTrackDEPR*)memory_getPointerFromVirtualOffset(nextBlockMPTR);
				if (_swapEndianU32(nextBlock->isFree) != 0)
				{
					block->addrEnd = nextBlock->addrEnd;
					block->nextBlock = nextBlock->nextBlock;
					if (_swapEndianU32(nextBlock->nextBlock) != MPTR_NULL)
					{
						MEMBlockHeapTrackDEPR* tempNextBlock = (MEMBlockHeapTrackDEPR*)memory_getPointerFromVirtualOffset(_swapEndianU32(nextBlock->nextBlock));
						tempNextBlock->previousBlock = _swapEndianU32(blockMPTR);
					}
					if (_swapEndianU32(blockHeapHead->tailBlock) == nextBlockMPTR)
					{
						blockHeapHead->tailBlock = _swapEndianU32(blockMPTR);
					}
					_MEMBlockHeap_ReleaseBlockTrack(blockHeapHead, nextBlockMPTR);
					_blockHeapDebugVerifyIfBlockIsLinked(blockHeapHead, (MEMBlockHeapTrackDEPR*)memory_getPointerFromVirtualOffset(nextBlockMPTR));
				}
			}
		}
		_blockHeapDebugVerifyLinkOrder(blockHeapHead);
		__OSUnlockScheduler();
	}

	uint32 MEMGetTotalFreeSizeForBlockHeap(MEMBlockHeapDEPR* blockHeap)
	{
		if (!blockHeap || blockHeap->magic != MEMHeapMagic::BLOCK_HEAP)
		{
			cemu_assert_suspicious();
			return 0;
		}

		__OSLockScheduler();
		uint32 totalSize = 0;
		MPTR blockMPTR = _swapEndianU32(blockHeap->headBlock);
		while (blockMPTR)
		{
			MEMBlockHeapTrackDEPR* track = (MEMBlockHeapTrackDEPR*)memory_getPointerFromVirtualOffset(blockMPTR);
			if (track->isFree != _swapEndianU32(0))
			{
				uint32 blockSize = _swapEndianU32(track->addrEnd) - _swapEndianU32(track->addrStart) + 1;
				totalSize += blockSize;
			}
			blockMPTR = _swapEndianU32(track->nextBlock);
		}
		__OSUnlockScheduler();

		return totalSize;
	}

	void MEMDumpBlockHeap(MPTR heap)
	{
		MEMBlockHeapDEPR* blockHeapHead = (MEMBlockHeapDEPR*)memory_getPointerFromVirtualOffset(heap);
		if (blockHeapHead->magic != MEMHeapMagic::BLOCK_HEAP)
		{
			cemu_assert_suspicious();
			return;
		}
		debug_printf("### MEMDumpBlockHeap ###\n");
		__OSLockScheduler();
		MEMBlockHeapTrackDEPR* track = (MEMBlockHeapTrackDEPR*)memory_getPointerFromVirtualOffsetAllowNull(_swapEndianU32(blockHeapHead->headBlock));
		while (track)
		{
			uint32 blockRangeStart = _swapEndianU32(track->addrStart);
			uint32 blockRangeEnd = _swapEndianU32(track->addrEnd) + 1;
			debug_printf(" %08x %08x - %08x prev/next %08x %08x isFree: %d\n", memory_getVirtualOffsetFromPointer(track), blockRangeStart, blockRangeEnd, _swapEndianU32(track->previousBlock), _swapEndianU32(track->nextBlock), _swapEndianU32(track->isFree));
			track = (MEMBlockHeapTrackDEPR*)memory_getPointerFromVirtualOffsetAllowNull(_swapEndianU32(track->nextBlock));
		}
		debug_printf("\n");
		__OSUnlockScheduler();
	}

	void InitializeMEMBlockHeap()
	{
		cafeExportRegister("coreinit", MEMInitBlockHeap, LogType::CoreinitMem);
		cafeExportRegister("coreinit", MEMDestroyBlockHeap, LogType::CoreinitMem);
		cafeExportRegister("coreinit", MEMGetAllocatableSizeForBlockHeapEx, LogType::CoreinitMem);

		cafeExportRegister("coreinit", MEMAddBlockHeapTracking, LogType::CoreinitMem);
		cafeExportRegister("coreinit", MEMGetTrackingLeftInBlockHeap, LogType::CoreinitMem);
		cafeExportRegister("coreinit", MEMAllocFromBlockHeapEx, LogType::CoreinitMem);
		cafeExportRegister("coreinit", MEMFreeToBlockHeap, LogType::CoreinitMem);
		cafeExportRegister("coreinit", MEMGetTotalFreeSizeForBlockHeap, LogType::CoreinitMem);
	}
}

#pragma once

namespace coreinit
{
	struct FIFOEntry_t
	{
		MEMPTR<uint8> p;
	};

	struct IPCFifo_t
	{
		uint32be writeIndex;
		uint32be readIndex;
		uint32be availableEntries;
		uint32be entryCount;
		MEMPTR<FIFOEntry_t> entryArray;
	};

	struct IPCBufPool_t
	{
		 uint32be magic;
		 MEMPTR<void> fullBufferPtr;
		 uint32be fullBufferSize;
		 uint32be uknFromParamR7;
		 uint32be ukn10;
		 uint32be entrySize1;
		 uint32be entrySize2;
		 uint32be entryCount;
		 MEMPTR<uint8> entryStartPtr;
		 uint32be entryCountMul4;
		 IPCFifo_t fifo;
		 coreinit::OSMutex mutex;
		 uint32 ukn68;
	};

	static_assert(sizeof(IPCBufPool_t) == 0x6C);

	uint8* IPCBufPoolAllocate(IPCBufPool_t* ipcBufPool, uint32 size);
	IPCBufPool_t* IPCBufPoolCreate(uint8* bufferArea, uint32 bufferSize, uint32 entrySize, uint32be* entryCountOutput, uint32 uknR7);
	sint32 IPCBufPoolFree(IPCBufPool_t* ipcBufPool, uint8* entry);

	void InitializeIPCBuf();
}

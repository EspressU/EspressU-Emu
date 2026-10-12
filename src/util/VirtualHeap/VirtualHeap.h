#pragma once

struct VirtualBufferHeapEntry_t
{
	uint32 startOffset;
	uint32 endOffset;
	VirtualBufferHeapEntry_t* next;
	VirtualBufferHeapEntry_t* previous;
};

struct VirtualBufferHeap_t
{
	uint32 virtualSize;
	void* baseAddress;
	VirtualBufferHeapEntry_t* firstEntry;
	VirtualBufferHeapEntry_t* firstUnusedEntry;
	uint32 updateTrackIndex;
	struct
	{
		uint32 numActiveAllocs;
		uint32 allocatedMemory;
	}stats;
};

VirtualBufferHeap_t* virtualBufferHeap_create(uint32 virtualHeapSize, void* baseAddr = nullptr);
VirtualBufferHeapEntry_t* virtualBufferHeap_allocate(VirtualBufferHeap_t* bufferHeap, uint32 size);
void virtualBufferHeap_free(VirtualBufferHeap_t* bufferHeap, VirtualBufferHeapEntry_t* entry);

void* virtualBufferHeap_allocateAddr(VirtualBufferHeap_t* bufferHeap, uint32 size);
void virtualBufferHeap_freeAddr(VirtualBufferHeap_t* bufferHeap, void* addr);
#pragma once
#include "GX2_Resource.h"

#define GX2_MAX_STREAMOUT_BUFFERS 4

namespace GX2
{
	struct GX2StreamOutBuffer
	{
		 uint32be size;
		 MEMPTR<void> dataPtr;
		 uint32be vertexStride;
		 GX2RBuffer rBuffer;
		 MEMPTR<void> ctxPtr;
	};

	static_assert(sizeof(GX2StreamOutBuffer) == 0x20, "GX2StreamOutBuffer_t has invalid size");

	void GX2SetStreamOutBuffer(uint32 bufferIndex, GX2StreamOutBuffer* streamOutBuffer);
	void GX2StreamoutInit();
}
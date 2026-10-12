#include "Cafe/HW/Latte/Core/LatteConst.h"
#include "Cafe/HW/Latte/ISA/RegDefines.h"
#include "Cafe/HW/Latte/Core/Latte.h"
#include "Cafe/HW/Latte/Core/LatteDraw.h"
#include "Cafe/HW/Latte/Core/LatteShader.h"
#include "Cafe/GameProfile/GameProfile.h"
#include "Cafe/HW/Latte/LegacyShaderDecompiler/LatteDecompiler.h"

#include "util/containers/IntervalBucketContainer.h"
#include "Cafe/HW/Latte/Renderer/Renderer.h"
#include "Cafe/HW/Latte/Core/LatteRingBuffer.h"
#include "Cafe/HW/Latte/Core/LatteBufferCache.h"

struct
{
	sint32 currentRingbufferOffset;
	VirtualBufferHeap_t* mainBufferHeap;
}streamoutManager;

sint32 LatteStreamout_GetRingBufferSize()
{
	return 8 * 1024 * 1024;
}

static sint32 LatteStreamout_GetEffectiveRingBufferSize()
{
	if (g_renderer && g_renderer->GetType() == RendererAPI::Metal)
		return LatteStreamout_GetRingBufferSize() * LATTE_NUM_STREAMOUT_BUFFER;
	return LatteStreamout_GetRingBufferSize();
}

sint32 LatteStreamout_allocateGPURingbufferMem(sint32 size)
{
	size = (size + 255)&~255;
	if ((streamoutManager.currentRingbufferOffset + size) > LatteStreamout_GetEffectiveRingBufferSize())
	{
		streamoutManager.currentRingbufferOffset = 0;
	}
	sint32 allocOffset = streamoutManager.currentRingbufferOffset;
	streamoutManager.currentRingbufferOffset += size;
	return allocOffset;
}

void LatteStreamout_InitCache()
{
	streamoutManager.currentRingbufferOffset = 0;
	streamoutManager.mainBufferHeap = nullptr;
}

bool _transformFeedbackIsActive = false;
struct
{
	uint64 vertexCount;
	uint32 instanceCount;
	uint32 streamoutWriteMask;
	struct
	{
		bool isActive;
		sint32 ringBufferOffset;
		uint32 rangeAddr;
		uint32 rangeSize;
	}streamoutBufferWrite[LATTE_NUM_STREAMOUT_BUFFER];
}activeStreamoutOperation;

void LatteStreamout_Reset()
{
	LatteStreamout_InitCache();
	_transformFeedbackIsActive = false;
	memset(&activeStreamoutOperation, 0, sizeof(activeStreamoutOperation));
}

uint64 LatteStreamout_getNumberOfWrittenVertices()
{
	if (activeStreamoutOperation.instanceCount != 0 &&
		activeStreamoutOperation.vertexCount > std::numeric_limits<uint64>::max() / activeStreamoutOperation.instanceCount)
	{
		return std::numeric_limits<uint64>::max();
	}
	return activeStreamoutOperation.vertexCount * activeStreamoutOperation.instanceCount;
}

uint32 LatteStreamout_getBufferWriteRangeSize(uint32 streamoutBufferIndex)
{
	uint32 bufferStride = LatteGPUState.contextRegister[mmVGT_STRMOUT_VTX_STRIDE_0 + streamoutBufferIndex * 4] << 2;
	uint32 bufferSize = LatteGPUState.contextRegister[mmVGT_STRMOUT_BUFFER_SIZE_0 + streamoutBufferIndex * 4] << 2;
	const uint64 writtenVertices = LatteStreamout_getNumberOfWrittenVertices();
	if (bufferStride == 0 || writtenVertices == 0)
		return 0;
	if (writtenVertices > bufferSize / bufferStride)
		return bufferSize;
	return static_cast<uint32>(writtenVertices * bufferStride);
}

void LatteStreamout_PrepareDrawcall(uint32 count, uint32 instanceCount)
{
	if (LatteGPUState.contextRegister[mmVGT_STRMOUT_EN] == 0)
	{
		_transformFeedbackIsActive = false;
		return;
	}
	LatteDecompilerShader* vertexShader = LatteSHRC_GetActiveVertexShader();
	LatteDecompilerShader* geometryShader = LatteSHRC_GetActiveGeometryShader();
	sint32 maxVerticesInGS = 1;
	if (geometryShader)
	{
		uint32 gsOutPrimType = LatteGPUState.contextRegister[mmVGT_GS_OUT_PRIM_TYPE];
		uint32 bytesPerVertex = std::max<uint32>(LatteGPUState.contextRegister[mmSQ_GS_VERT_ITEMSIZE] * 4, 1);
		maxVerticesInGS = ((LatteGPUState.contextRegister[mmSQ_GSVS_RING_ITEMSIZE] & 0x7FFF) * 4) / bytesPerVertex;
		maxVerticesInGS = std::max(maxVerticesInGS, 1);
	}
	activeStreamoutOperation.vertexCount = static_cast<uint64>(count) * static_cast<uint32>(maxVerticesInGS);
	activeStreamoutOperation.instanceCount = instanceCount;
	uint32 streamoutWriteMask = 0;
	if (geometryShader)
	{
#ifdef CEMU_DEBUG_ASSERT
		cemu_assert_debug(vertexShader->streamoutBufferWriteMask.any() == false);
#endif
		for (sint32 i = 0; i < LATTE_NUM_STREAMOUT_BUFFER; i++)
			if (geometryShader->streamoutBufferWriteMask[i])
				streamoutWriteMask |= (1 << i);
	}
	else
	{
		for (sint32 i = 0; i < LATTE_NUM_STREAMOUT_BUFFER; i++)
			if (vertexShader->streamoutBufferWriteMask[i])
				streamoutWriteMask |= (1 << i);
	}
	activeStreamoutOperation.streamoutWriteMask = streamoutWriteMask;
	if (g_renderer->GetType() == RendererAPI::Metal)
	{
		streamoutManager.currentRingbufferOffset = 0;
	}
	uint32 remainingRingCapacity = static_cast<uint32>(LatteStreamout_GetEffectiveRingBufferSize());
	for (uint32 i = 0; i < LATTE_NUM_STREAMOUT_BUFFER; i++)
	{
		if ((streamoutWriteMask&(1 << i)) == 0)
		{
			activeStreamoutOperation.streamoutBufferWrite[i].isActive = false;
			continue;
		}
		uint32 bufferBaseMPTR = LatteGPUState.contextRegister[mmVGT_STRMOUT_BUFFER_BASE_0 + i * 4] << 8;
		uint32 bufferSize = LatteGPUState.contextRegister[mmVGT_STRMOUT_BUFFER_SIZE_0 + i * 4] << 2;
		uint32 bufferOffset = LatteGPUState.contextRegister[mmVGT_STRMOUT_BUFFER_OFFSET_0 + i * 4];
		uint32 streamoutWriteSize = LatteStreamout_getBufferWriteRangeSize(i);
		uint32 remainingBytesToWrite = bufferOffset >= bufferSize ? 0 : (bufferSize - bufferOffset);
		uint32 rangeSize = std::min(streamoutWriteSize, remainingBytesToWrite);
		rangeSize = std::min(rangeSize, remainingRingCapacity);
		uint32 allocationSize = (rangeSize + 255u) & ~255u;
		if (allocationSize > remainingRingCapacity)
		{
			rangeSize = remainingRingCapacity & ~255u;
			allocationSize = rangeSize;
		}
		remainingRingCapacity -= allocationSize;
		uint32 rangeAddr = bufferBaseMPTR + bufferOffset;
		sint32 ringBufferOffset = LatteStreamout_allocateGPURingbufferMem(static_cast<sint32>(rangeSize));

		activeStreamoutOperation.streamoutBufferWrite[i].isActive = true;
		activeStreamoutOperation.streamoutBufferWrite[i].ringBufferOffset = ringBufferOffset;
		activeStreamoutOperation.streamoutBufferWrite[i].rangeAddr = rangeAddr;
		activeStreamoutOperation.streamoutBufferWrite[i].rangeSize = rangeSize;

		g_renderer->streamout_setupXfbBuffer(i, ringBufferOffset, rangeAddr, rangeSize);
	}
	g_renderer->streamout_begin();
	_transformFeedbackIsActive = true;
}

void LatteStreamout_FinishDrawcall(bool useDirectMemoryMode)
{
	if (_transformFeedbackIsActive)
	{
		_transformFeedbackIsActive = false;
		for (uint32 i = 0; i < LATTE_NUM_STREAMOUT_BUFFER; i++)
		{
			if ((activeStreamoutOperation.streamoutWriteMask&(1 << i)) == 0)
				continue;
			if (activeStreamoutOperation.streamoutBufferWrite[i].rangeSize > 0)
			{
				if(useDirectMemoryMode)
					g_renderer->bufferCache_copyStreamoutToMainBuffer(activeStreamoutOperation.streamoutBufferWrite[i].ringBufferOffset, activeStreamoutOperation.streamoutBufferWrite[i].rangeAddr, activeStreamoutOperation.streamoutBufferWrite[i].rangeSize);
				else
					LatteBufferCache_copyStreamoutDataToCache(activeStreamoutOperation.streamoutBufferWrite[i].rangeAddr, activeStreamoutOperation.streamoutBufferWrite[i].rangeSize, activeStreamoutOperation.streamoutBufferWrite[i].ringBufferOffset);
			}
			uint32 newOffset = LatteGPUState.contextRegister[mmVGT_STRMOUT_BUFFER_OFFSET_0 + i * 4] + activeStreamoutOperation.streamoutBufferWrite[i].rangeSize;
			LatteGPUState.contextRegister[mmVGT_STRMOUT_BUFFER_OFFSET_0 + i * 4] = newOffset;
		}
		g_renderer->streamout_rendererFinishDrawcall();
	}
}

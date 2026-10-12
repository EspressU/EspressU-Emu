#pragma once

#include "Cafe/HW/Latte/Core/LatteQueryObject.h"
#include <vector>

#include "Cafe/HW/Latte/Renderer/Metal/MetalCommon.h"

class LatteQueryObjectMtl : public LatteQueryObject
{
public:
	LatteQueryObjectMtl(class MetalRenderer* mtlRenderer) : m_mtlr{mtlRenderer} {}
	~LatteQueryObjectMtl();

	bool getResult(uint64& numSamplesPassed) override;
	void begin() override;
	void end() override;

	void SealCurrentRange(uint32 bufferIndex);
	void AccumulateBuffer(uint32 bufferIndex);

private:
	class MetalRenderer* m_mtlr;

	MetalQueryRange m_range = {INVALID_UINT32, INVALID_UINT32};
    struct PendingRange
    {
        uint32 bufferIndex;
        MetalQueryRange range;
    };
    std::vector<PendingRange> m_pendingRanges;
	uint64 m_accumulatedSamples = 0;
	bool m_ended = false;
	MTL::CommandBuffer* m_commandBuffer = nullptr;
};

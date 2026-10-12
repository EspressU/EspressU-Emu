#include "Cafe/HW/Latte/Renderer/Metal/MetalQuery.h"
#include "Cafe/HW/Latte/Renderer/Metal/MetalRenderer.h"

bool LatteQueryObjectMtl::getResult(uint64& numSamplesPassed)
{
    if (!m_ended || (m_commandBuffer && !CommandBufferCompleted(m_commandBuffer)))
        return false;

    uint64* resultPtr = m_mtlr->GetOcclusionQueryResultsPtr();

    numSamplesPassed = m_accumulatedSamples;
    for (uint32 i = m_range.begin; i < m_range.end; ++i)
        numSamplesPassed += resultPtr[i];

    for (const auto& pending : m_pendingRanges)
    {
        const uint64* pendingResults = m_mtlr->GetOcclusionQueryResultsPtr(pending.bufferIndex);
        for (uint32 i = pending.range.begin; i < pending.range.end; ++i)
            numSamplesPassed += pendingResults[i];
    }
    return true;
}

void LatteQueryObjectMtl::SealCurrentRange(uint32 bufferIndex)
{
    if (m_range.begin == INVALID_UINT32)
        return;

    const uint32 end = m_ended ? m_range.end : m_mtlr->GetOcclusionQueryIndex();

    if (m_range.begin < end)
        m_pendingRanges.push_back({bufferIndex, {m_range.begin, end}});

    m_range = m_ended ? MetalQueryRange{INVALID_UINT32, INVALID_UINT32} : MetalQueryRange{0, 0};
}

void LatteQueryObjectMtl::AccumulateBuffer(uint32 bufferIndex)
{
    const uint64* results = m_mtlr->GetOcclusionQueryResultsPtr(bufferIndex);
    for (auto it = m_pendingRanges.begin(); it != m_pendingRanges.end();)
    {
        if (it->bufferIndex != bufferIndex)
        {
            ++it;
            continue;
        }
        for (uint32 i = it->range.begin; i < it->range.end; ++i)
            m_accumulatedSamples += results[i];
        it = m_pendingRanges.erase(it);
    }
}

LatteQueryObjectMtl::~LatteQueryObjectMtl()
{
    if (m_commandBuffer)
        m_commandBuffer->release();
}

void LatteQueryObjectMtl::begin()
{
    m_range.begin = m_mtlr->GetOcclusionQueryIndex();
    m_mtlr->BeginOcclusionQuery();
}

void LatteQueryObjectMtl::end()
{
    m_range.end = m_mtlr->GetOcclusionQueryIndex();
    m_ended = true;
    m_mtlr->EndOcclusionQuery();

    m_commandBuffer = m_mtlr->GetAndRetainCurrentCommandBufferIfNotCompleted();
    if (m_commandBuffer)
        m_mtlr->RequestSoonCommit();
}

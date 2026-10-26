#include "Cafe/HW/Latte/Core/PerfTelemetry.h"
#include "Cafe/HW/Latte/Renderer/Metal/MetalCommon.h"
#include "Cafe/HW/Latte/Renderer/Metal/MetalMemoryManager.h"
#include "Cafe/HW/Latte/Renderer/Metal/MetalVoidVertexPipeline.h"
#include "Cafe/HW/Latte/Renderer/Metal/MetalFaultHistory.h"

#include "CafeSystem.h"
#include "Cemu/Logging/CemuLogging.h"
#include "Common/precompiled.h"
#include "HW/MMU/MMU.h"
#include "config/CemuConfig.h"

#include "Cafe/HW/Latte/Core/LatteBufferCache.h"

#include <cstring>
#if BOOST_OS_IOS
#include <os/proc.h>
#endif

MetalMemoryManager::~MetalMemoryManager()
{
    for (auto& cache : m_argumentCaches)
    {
        for (auto& entry : cache.entries)
        {
            for (const auto& binding : entry.bindings)
                if (binding.resource)
                    static_cast<NS::Object*>(binding.resource)->release();
            if (entry.encoder)
                entry.encoder->release();
        }
    }
    if (m_bufferCache)
    {
        m_bufferCache->release();
    }
    if (m_importedMemoryBuffer)
    {
        NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
        const NS::UInteger retainCount = m_importedMemoryBuffer->retainCount();
        m_importedMemoryBuffer->release();
        m_importedMemoryBuffer = nullptr;
        pool->release();
        if (retainCount == 1 && m_importedRegistered)
        {
            memory_unregisterGpuMapping(m_importedMemBaseAddress, (uint32)m_hostAllocationSize);
            m_importedRegistered = false;
        }
        else
            cemuLog_log(LogType::Force, "Metal: imported guest memory buffer had a retain count of {}, its pages stay protected but are not replaced", (uint64)retainCount);
    }
}

MetalSynchronizedHeapAllocator::AllocatorReservation* MetalMemoryManager::GetCachedSnapshot(uint32 slot, const void* data, uint32 size, uint32 firstByte)
{
    cemu_assert_debug(slot < SnapshotCount && firstByte <= size);
    m_mtlr->GetCommandBuffer();
    auto& snapshot = m_snapshots[slot];
    const auto* source = static_cast<const uint8*>(data);
    const uint32 copySize = size - firstByte;
    const bool canCompare = snapshot.allocation && snapshot.firstByte <= firstByte && snapshot.endByte >= size;
    if (canCompare)
        PerfTelemetry::Get().gpuSnapshotCompareBytes.fetch_add(copySize, std::memory_order_relaxed);
    if (canCompare && std::memcmp(snapshot.allocation->memPtr + firstByte, source + firstByte, copySize) == 0)
    {
        m_mtlr->GetPerformanceMonitor().m_snapshotReuses++;
        return snapshot.allocation;
    }

    if (snapshot.allocation)
        m_snapshotAllocator.FreeReservation(snapshot.allocation);
    snapshot.allocation = m_snapshotAllocator.AllocateBufferMemory(std::max(size, 1u), 256);
    snapshot.firstByte = firstByte;
    snapshot.endByte = size;
    if (!snapshot.allocation)
    {
        return nullptr;
    }
    std::memcpy(snapshot.allocation->memPtr + firstByte, source + firstByte, copySize);
    m_snapshotAllocator.FlushReservation(snapshot.allocation);
    m_mtlr->GetPerformanceMonitor().m_snapshotBytes += copySize;
    PerfTelemetry::Get().gpuSnapshotCopyBytes.fetch_add(copySize, std::memory_order_relaxed);
    return snapshot.allocation;
}

void MetalMemoryManager::ReleaseArgumentCacheEntry(ArgumentCacheEntry& entry)
{
    if (entry.allocation)
        RetireArgumentBlock(entry.allocation);
    entry.allocation = nullptr;
    for (const auto& binding : entry.bindings)
        if (binding.resource)
            static_cast<NS::Object*>(binding.resource)->release();
    entry.bindings = {};
    if (entry.encoder)
        entry.encoder->release();
    entry.encoder = nullptr;
}

void MetalMemoryManager::ReleaseArgumentCache()
{
    for (auto& cache : m_argumentCaches)
    {
        for (auto& entry : cache.entries)
            ReleaseArgumentCacheEntry(entry);
        cache.entries.clear();
    }
}

void MetalMemoryManager::AgeArgumentCache()
{
    const uint64 generation = ++m_argumentGeneration;
    for (auto& cache : m_argumentCaches)
    {
        auto& entries = cache.entries;
        for (size_t i = 0; i < entries.size();)
        {
            if (generation - entries[i].lastGeneration > ARGUMENT_CACHE_MAX_AGE)
            {
                ReleaseArgumentCacheEntry(entries[i]);
                entries[i] = std::move(entries.back());
                entries.pop_back();
            }
            else
                ++i;
        }
    }
}

MetalMemoryManager::ArgumentBlock* MetalMemoryManager::AllocateArgumentBlock(uint32 size, uint32 alignment)
{
    auto it = m_freeArgumentBlocks.find(size);
    if (it != m_freeArgumentBlocks.end())
    {
        auto& blocks = it->second;
        for (size_t i = blocks.size(); i-- > 0;)
        {
            if (blocks[i]->bufferOffset % alignment == 0)
            {
                ArgumentBlock* block = blocks[i];
                blocks[i] = blocks.back();
                blocks.pop_back();
                m_pooledArgumentBlocks--;
                return block;
            }
        }
    }
    return m_snapshotAllocator.AllocateBufferMemory(size, alignment);
}

void MetalMemoryManager::RetireArgumentBlock(ArgumentBlock* block)
{
    if (m_pooledArgumentBlocks >= ARGUMENT_BLOCK_POOL_MAX)
    {
        m_snapshotAllocator.FreeReservation(block);
        return;
    }
    MTL::CommandBuffer* retireOn = m_mtlr->GetCommandBufferToRetireOn();
    m_pooledArgumentBlocks++;
    if (retireOn)
        m_retiringArgumentBlocks[retireOn].push_back(block);
    else
        m_freeArgumentBlocks[block->size].push_back(block);
}

void MetalMemoryManager::ReclaimArgumentBlocks(MTL::CommandBuffer* finishedCommandBuffer)
{
    auto it = m_retiringArgumentBlocks.find(finishedCommandBuffer);
    if (it == m_retiringArgumentBlocks.end())
        return;
    for (ArgumentBlock* block : it->second)
        m_freeArgumentBlocks[block->size].push_back(block);
    m_retiringArgumentBlocks.erase(it);
}

namespace
{
    enum class ArgumentSlotKind : uint32 { Support, Uniform, Texture, Sampler, VertexIndex, Constant };

    ArgumentSlotKind GetArgumentSlotKind(uint32 index)
    {
        using namespace MetalArgumentBuffer;
        if (index == SupportBuffer)
            return ArgumentSlotKind::Support;
        if (index >= UniformBufferBase && index < StreamoutBuffer)
            return ArgumentSlotKind::Uniform;
        if (index >= TextureBase && index < SamplerBase)
            return ArgumentSlotKind::Texture;
        if (index >= SamplerBase && index < VertexBufferBase)
            return ArgumentSlotKind::Sampler;
        if (index == StreamoutBuffer || (index >= VertexBufferBase && index < VertexBufferSizeBase) || index == IndexBuffer)
            return ArgumentSlotKind::VertexIndex;
        return ArgumentSlotKind::Constant;
    }
}

MetalSynchronizedHeapAllocator::AllocatorReservation* MetalMemoryManager::GetCachedArgumentBuffer(uint32 stage, MTL::ArgumentEncoder* encoder, const MetalArgumentBindings& bindings)
{
    cemu_assert_debug(stage < METAL_SHADER_TYPE_TOTAL);
    auto& telemetry = PerfTelemetry::Get();
    m_mtlr->GetCommandBuffer();
    const uint64 lookupStart = PerfTelemetry::SampleStart();
    auto& cache = m_argumentCaches[stage];
    uint64 hash = reinterpret_cast<uintptr_t>(encoder) * 0x9E3779B97F4A7C15ull;
    for (const auto& binding : bindings)
    {
        hash ^= reinterpret_cast<uintptr_t>(binding.resource) + binding.value + static_cast<uint64>(binding.type) + 0x9E3779B97F4A7C15ull + (hash << 6) + (hash >> 2);
        hash *= 0xFF51AFD7ED558CCDull;
    }
    for (auto& entry : cache.entries)
    {
        if (entry.hash == hash && entry.encoder == encoder && entry.bindings == bindings)
        {
            entry.lastUse = ++cache.useCounter;
            entry.lastGeneration = m_argumentGeneration;
            m_mtlr->GetPerformanceMonitor().m_argumentBufferReuses++;
            PerfTelemetry::SampleCount(telemetry.gpuArgReuses);
            PerfTelemetry::SampleEnd(telemetry.gpuArgReuseNs, lookupStart);
            return entry.allocation;
        }
    }

    const uint64 encodeStart = PerfTelemetry::SampleStart();
    const uint32 alignment = std::max<uint32>(256, static_cast<uint32>(encoder->alignment()));
    const uint32 encodedLength = static_cast<uint32>(encoder->encodedLength());
    auto* allocation = AllocateArgumentBlock(encodedLength, alignment);
    PerfTelemetry::SampleEnd(telemetry.gpuArgEncAllocNs, encodeStart);
    if (!allocation)
    {
        return nullptr;
    }

    const ArgumentCacheEntry* base = nullptr;
    for (const auto& entry : cache.entries)
        if (entry.encoder == encoder && entry.allocation && (!base || entry.lastUse > base->lastUse))
            base = &entry;

    uint16 deltaSlots[std::tuple_size_v<MetalArgumentBindings>];
    uint32 deltaCount = 0;
    uint32 differingKinds = 0;
    bool canDelta = base != nullptr;
    if (base)
    {
        for (uint32 index = 0; index < bindings.size(); ++index)
        {
            if (bindings[index] == base->bindings[index])
                continue;
            if (PerfTelemetry::IsSampling())
                differingKinds |= 1u << static_cast<uint32>(GetArgumentSlotKind(index));
            if (bindings[index].type == MetalArgumentBinding::Type::Unused)
                canDelta = false;
            else
                deltaSlots[deltaCount++] = static_cast<uint16>(index);
        }
        if (PerfTelemetry::IsSampling())
        {
            auto countKind = [&](std::atomic<uint32_t>& counter, ArgumentSlotKind kind)
            {
                if ((differingKinds >> static_cast<uint32>(kind)) & 1u)
                    PerfTelemetry::SampleCount(counter);
            };
            countKind(telemetry.gpuArgMissSupport, ArgumentSlotKind::Support);
            if (differingKinds == (1u << static_cast<uint32>(ArgumentSlotKind::Support)))
                PerfTelemetry::SampleCount(telemetry.gpuArgMissSupportOnly);
            countKind(telemetry.gpuArgMissUniform, ArgumentSlotKind::Uniform);
            countKind(telemetry.gpuArgMissTexture, ArgumentSlotKind::Texture);
            countKind(telemetry.gpuArgMissSampler, ArgumentSlotKind::Sampler);
            countKind(telemetry.gpuArgMissVertexIndex, ArgumentSlotKind::VertexIndex);
            countKind(telemetry.gpuArgMissConstant, ArgumentSlotKind::Constant);
        }
    }
    else
        PerfTelemetry::SampleCount(telemetry.gpuArgMissNoBase);

    const uint64 copyStart = PerfTelemetry::SampleStart();
    if (canDelta)
    {
        std::memcpy(allocation->memPtr, base->allocation->memPtr, encodedLength);
    }
    else
    {
        std::memset(allocation->memPtr, 0, allocation->size);
    }

    for (const auto& binding : bindings)
        if (binding.resource)
            static_cast<NS::Object*>(binding.resource)->retain();
    encoder->retain();

    ArgumentCacheEntry* slot;
    if (cache.entries.size() < ARGUMENT_CACHE_ENTRIES)
    {
        slot = &cache.entries.emplace_back();
    }
    else
    {
        slot = &cache.entries[0];
        for (auto& entry : cache.entries)
            if (entry.lastUse < slot->lastUse)
                slot = &entry;
        ReleaseArgumentCacheEntry(*slot);
    }
    slot->hash = hash;
    slot->lastUse = ++cache.useCounter;
    slot->lastGeneration = m_argumentGeneration;
    slot->encoder = encoder;
    slot->bindings = bindings;
    slot->allocation = allocation;
    PerfTelemetry::SampleEnd(telemetry.gpuArgEncCopyNs, copyStart);
    const uint64 setStart = PerfTelemetry::SampleStart();
    encoder->setArgumentBuffer(allocation->mtlBuffer, allocation->bufferOffset);
    auto encodeSlot = [&](uint32 index)
    {
        const auto& binding = bindings[index];
        switch (binding.type)
        {
            case MetalArgumentBinding::Type::Unused:
                break;
            case MetalArgumentBinding::Type::Buffer:
                encoder->setBuffer(static_cast<MTL::Buffer*>(binding.resource), binding.value, index);
                break;
            case MetalArgumentBinding::Type::Texture:
                encoder->setTexture(static_cast<MTL::Texture*>(binding.resource), index);
                break;
            case MetalArgumentBinding::Type::Sampler:
                encoder->setSamplerState(static_cast<MTL::SamplerState*>(binding.resource), index);
                break;
            case MetalArgumentBinding::Type::Constant:
                if (void* constant = encoder->constantData(index))
                    *static_cast<uint32*>(constant) = static_cast<uint32>(binding.value);
                break;
        }
    };
    if (canDelta)
    {
        for (uint32 i = 0; i < deltaCount; ++i)
            encodeSlot(deltaSlots[i]);
        PerfTelemetry::SampleCount(telemetry.gpuArgDeltaEncodes);
        PerfTelemetry::SampleCount(telemetry.gpuArgDeltaSlots, deltaCount);
    }
    else
    {
        for (uint32 index = 0; index < bindings.size(); ++index)
            encodeSlot(index);
    }

    PerfTelemetry::SampleEnd(telemetry.gpuArgEncSetNs, setStart);
    const uint64 flushStart = PerfTelemetry::SampleStart();
    m_snapshotAllocator.FlushReservation(allocation);
    PerfTelemetry::SampleEnd(telemetry.gpuArgEncFlushNs, flushStart);
    m_mtlr->GetPerformanceMonitor().m_argumentBufferEncodes++;
    PerfTelemetry::SampleCount(telemetry.gpuArgEncodes);
    PerfTelemetry::SampleEnd(telemetry.gpuArgEncodeNs, encodeStart);
    return allocation;
}

void* MetalMemoryManager::AcquireTextureUploadBuffer(size_t size)
{
    if (m_textureUploadBuffer.size() < size)
    {
        try
        {
            m_textureUploadBuffer.resize(size);
        }
        catch (const std::bad_alloc&)
        {
            std::vector<uint8>().swap(m_textureUploadBuffer);
            try
            {
                m_textureUploadBuffer.resize(size);
            }
            catch (const std::bad_alloc&)
            {
                std::vector<uint8>().swap(m_textureUploadBuffer);
                MetalFaultHistory::Record(MetalFaultHistory::Kind::UploadBuffer, size, "CPU-side texture upload buffer");
                cemuLog_logOnce(LogType::Force, "Metal: could not allocate a {} byte texture upload buffer, skipping the upload", size);
                LatteWait::Get().evictionRequested.store(true);
                return nullptr;
            }
        }
    }

    return m_textureUploadBuffer.data();
}

void MetalMemoryManager::ReleaseTextureUploadBuffer(uint8* mem)
{
    cemu_assert_debug(m_textureUploadBuffer.data() == mem);
    m_textureUploadBuffer.clear();
}

void MetalMemoryManager::InitBufferCache(size_t size)
{
    cemu_assert_debug(!m_bufferCache);

    m_metalBufferCacheMode = g_current_game_profile->GetBufferCacheMode();

    if (m_metalBufferCacheMode == MetalBufferCacheMode::Auto)
    {
        if (m_mtlr->IsAppleGPU())
        {
            switch (CafeSystem::GetForegroundTitleId())
            {
            case 0x0005000010143600:
            case 0x0005000010143500:
            case 0x0005000010143400:
                m_metalBufferCacheMode = MetalBufferCacheMode::Host;
                break;
            default:
                m_metalBufferCacheMode = MetalBufferCacheMode::Host;
                break;
            }
        }
        else
        {
            m_metalBufferCacheMode = MetalBufferCacheMode::DevicePrivate;
        }
    }

    if (m_metalBufferCacheMode == MetalBufferCacheMode::Host)
    {
        if (m_mtlr->HasUnifiedMemory())
        {
            m_importedMemBaseAddress = mmuRange_MEM2.getBase();
               m_hostAllocationSize = mmuRange_MEM2.getSize();
            m_importedMemoryBuffer = m_mtlr->GetDevice()->newBuffer(memory_getPointerFromVirtualOffset(m_importedMemBaseAddress), m_hostAllocationSize, MTL::ResourceStorageModeShared, nullptr);
            if (m_importedMemoryBuffer)
            {
                memory_registerGpuMapping(m_importedMemBaseAddress, (uint32)m_hostAllocationSize);
                m_importedRegistered = true;
            }
            if (!m_importedMemoryBuffer)
            {
                cemuLog_log(LogType::Force, "Failed to import host memory as a buffer, using device shared mode instead");
                m_metalBufferCacheMode = MetalBufferCacheMode::DeviceShared;
                m_importedMemBaseAddress = 0;
                m_hostAllocationSize = 0;
            }
        }
        else
        {
            cemuLog_log(LogType::Force, "Host buffer cache mode is only available on unified memory systems, using device shared mode instead");
            m_metalBufferCacheMode = MetalBufferCacheMode::DeviceShared;
        }
    }

    constexpr size_t MB = 1024 * 1024;
    constexpr size_t MIN_BUFFER_CACHE_SIZE = 48 * MB;
    const size_t requestedSize = size;
    size_t attemptSize = size;
    const size_t deviceMaxLength = static_cast<size_t>(m_mtlr->GetDevice()->maxBufferLength());
    if (deviceMaxLength != 0 && attemptSize > deviceMaxLength)
        attemptSize = deviceMaxLength & ~(MB - 1);
#if BOOST_OS_IOS
    const size_t availableMemory = static_cast<size_t>(os_proc_available_memory());
    if (availableMemory != 0)
        attemptSize = std::min(attemptSize, std::max(availableMemory / 3, MIN_BUFFER_CACHE_SIZE) & ~(MB - 1));
#endif
    const MTL::ResourceOptions cacheOptions = (m_metalBufferCacheMode == MetalBufferCacheMode::DevicePrivate ? MTL::ResourceStorageModePrivate : MTL::ResourceStorageModeShared);
    while (!m_bufferCache && attemptSize >= MIN_BUFFER_CACHE_SIZE)
    {
        m_bufferCache = m_mtlr->GetDevice()->newBuffer(attemptSize, cacheOptions);
        if (m_bufferCache)
            break;
        cemuLog_log(LogType::Force, "Metal: a {} MB GPU buffer cache could not be allocated (device max buffer {} MB, allocated {} MB, recommended working set {} MB), trying smaller",
            attemptSize / MB, deviceMaxLength / MB, static_cast<size_t>(m_mtlr->GetDevice()->currentAllocatedSize()) / MB, static_cast<size_t>(m_mtlr->GetDevice()->recommendedMaxWorkingSetSize()) / MB);
        attemptSize = (attemptSize - attemptSize / 4) & ~(MB - 1);
    }
    if (m_bufferCache)
    {
        m_bufferCacheSize = m_bufferCache->length();
        if (m_bufferCacheSize < requestedSize)
            cemuLog_log(LogType::Force, "Metal: GPU buffer cache is {} MB instead of the {} MB requested; heavy scenes may stream geometry more often", m_bufferCacheSize / MB, requestedSize / MB);
    }
    else
    {
        m_bufferCacheSize = requestedSize;
        cemuLog_log(LogType::Force,
            "Metal: failed to allocate the GPU buffer cache (asked for {} MB, nothing down to {} MB fit). Nothing will render; this is an out-of-memory condition, not a shader or pipeline fault",
            requestedSize / MB, MIN_BUFFER_CACHE_SIZE / MB);
    }

    if (m_metalBufferCacheMode == MetalBufferCacheMode::DeviceShared)
        m_sharedTracker.Initialize(m_bufferCacheSize);

    LatteBufferCache_hostSetVolatilityTracking(m_metalBufferCacheMode == MetalBufferCacheMode::Host);

#ifdef CEMU_DEBUG_ASSERT
    if (m_bufferCache)
        m_bufferCache->setLabel(GetLabel("Buffer cache", m_bufferCache));
    if (m_importedMemoryBuffer)
        m_importedMemoryBuffer->setLabel(GetLabel("Imported memory buffer", m_importedMemoryBuffer));
#endif
}

void MetalMemoryManager::UploadToBufferCache(const void* data, size_t offset, size_t size)
{
    if (size == 0)
        return;
    if (!m_bufferCache)
        return;
    cemu_assert_debug((offset + size) <= m_bufferCache->length());

    if (m_metalBufferCacheMode == MetalBufferCacheMode::DevicePrivate || SharedCacheBusy(offset, size))
    {
        auto blitCommandEncoder = m_mtlr->GetBlitCommandEncoder(PerfTelemetry::kBreakBufferUpload);

        auto allocation = m_stagingAllocator.AllocateBufferMemory(size, 1);
        if (!allocation.mtlBuffer)
            return;
        memcpy(allocation.memPtr, data, size);
        m_stagingAllocator.FlushReservation(allocation);

        blitCommandEncoder->copyFromBuffer(allocation.mtlBuffer, allocation.bufferOffset, m_bufferCache, offset, size);
        TrackSharedCache(m_bufferCache, offset, size, true);

    }
    else
    {
        memcpy((uint8*)m_bufferCache->contents() + offset, data, size);
        NotifyBufferCacheRangeModified(offset, size);
    }

}

void MetalMemoryManager::CopyBufferCache(size_t srcOffset, size_t dstOffset, size_t size)
{
    if (size == 0 || srcOffset == dstOffset)
        return;
    if (!m_bufferCache)
        return;
    if (m_metalBufferCacheMode == MetalBufferCacheMode::DevicePrivate ||
        SharedCacheBusy(srcOffset, size, true) || SharedCacheBusy(dstOffset, size))
    {
        m_mtlr->m_nextBufferCopyBreak = PerfTelemetry::kBreakCacheCopy;
        m_mtlr->CopyBufferToBuffer(m_bufferCache, srcOffset, m_bufferCache, dstOffset, size, ALL_MTL_RENDER_STAGES, ALL_MTL_RENDER_STAGES);
        TrackSharedCache(m_bufferCache, srcOffset, size);
        TrackSharedCache(m_bufferCache, dstOffset, size, true);
    }
    else
    {
        memcpy((uint8*)m_bufferCache->contents() + dstOffset, (uint8*)m_bufferCache->contents() + srcOffset, size);
        NotifyBufferCacheRangeModified(dstOffset, size);
    }

}

bool MetalMemoryManager::SharedCacheBusy(size_t offset, size_t size, bool writesOnly) const
{
    return m_sharedTracker.Busy(offset, size, writesOnly);
}

void MetalMemoryManager::TrackSharedCache(MTL::Buffer* buffer, size_t offset, size_t size, bool write)
{
    if (m_metalBufferCacheMode == MetalBufferCacheMode::DeviceShared && buffer == m_bufferCache && size)
        m_sharedTracker.Mark(m_mtlr->GetCommandBuffer(), offset, size, write);
}

void MetalMemoryManager::NotifyBufferCacheRangeModified(size_t offset, size_t size)
{
    NotifyBufferRangeModified(m_bufferCache, offset, size);
}

void MetalMemoryManager::NotifyImportedMemoryRangeModified(size_t offset, size_t size)
{
    NotifyBufferRangeModified(m_importedMemoryBuffer, offset, size);
}

void MetalMemoryManager::NotifyBufferRangeModified(MTL::Buffer* buffer, size_t offset, size_t size)
{
    if (!buffer || size == 0 || buffer->storageMode() != MTL::StorageModeManaged)
        return;

    size_t bufferLength = buffer->length();
    if (offset >= bufferLength)
        return;

    size = std::min(size, bufferLength - offset);
    buffer->didModifyRange(NS::Range(offset, size));
}

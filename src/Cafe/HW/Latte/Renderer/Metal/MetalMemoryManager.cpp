#include "Cafe/HW/Latte/Core/PerfTelemetry.h"
#include "Cafe/HW/Latte/Renderer/Metal/MetalCommon.h"
#include "Cafe/HW/Latte/Renderer/Metal/MetalMemoryManager.h"
#include "Cafe/HW/Latte/Renderer/Metal/MetalVoidVertexPipeline.h"

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
    for (auto& snapshot : m_argumentSnapshots)
    {
        for (const auto& binding : snapshot.bindings)
            if (binding.resource)
                static_cast<NS::Object*>(binding.resource)->release();
        if (snapshot.encoder)
            snapshot.encoder->release();
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
    if (snapshot.allocation && snapshot.firstByte <= firstByte && snapshot.endByte >= size &&
        std::memcmp(snapshot.allocation->memPtr + firstByte, source + firstByte, copySize) == 0)
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
    return snapshot.allocation;
}

MetalSynchronizedHeapAllocator::AllocatorReservation* MetalMemoryManager::GetCachedArgumentBuffer(uint32 stage, MTL::ArgumentEncoder* encoder, const MetalArgumentBindings& bindings)
{
    cemu_assert_debug(stage < METAL_SHADER_TYPE_TOTAL);
    m_mtlr->GetCommandBuffer();
    auto& snapshot = m_argumentSnapshots[stage];
    if (snapshot.encoder == encoder && std::memcmp(snapshot.bindings.data(), bindings.data(), sizeof(MetalArgumentBindings)) == 0)
    {
        m_mtlr->GetPerformanceMonitor().m_argumentBufferReuses++;
        return snapshot.allocation;
    }

    if (snapshot.allocation)
        m_snapshotAllocator.FreeReservation(snapshot.allocation);
    if (snapshot.encoder != encoder)
    {
        if (snapshot.encoder)
            snapshot.encoder->release();
        snapshot.encoder = encoder->retain();
    }

    for (size_t i = 0; i < bindings.size(); i++)
        if (bindings[i].resource != snapshot.bindings[i].resource && bindings[i].resource)
            static_cast<NS::Object*>(bindings[i].resource)->retain();
    for (size_t i = 0; i < bindings.size(); i++)
        if (bindings[i].resource != snapshot.bindings[i].resource && snapshot.bindings[i].resource)
            static_cast<NS::Object*>(snapshot.bindings[i].resource)->release();
    snapshot.bindings = bindings;
    const uint32 alignment = std::max<uint32>(256, static_cast<uint32>(encoder->alignment()));
    snapshot.allocation = m_snapshotAllocator.AllocateBufferMemory(static_cast<uint32>(encoder->encodedLength()), alignment);
    auto* allocation = snapshot.allocation;
    if (!allocation)
    {
        snapshot.encoder->release();
        snapshot.encoder = nullptr;
        return nullptr;
    }
    std::memset(allocation->memPtr, 0, allocation->size);
    encoder->setArgumentBuffer(allocation->mtlBuffer, allocation->bufferOffset);
    for (uint32 index = 0; index < bindings.size(); ++index)
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
    }

    m_snapshotAllocator.FlushReservation(allocation);
    m_mtlr->GetPerformanceMonitor().m_argumentBufferEncodes++;
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

#pragma once

#include "Cafe/HW/Latte/Renderer/Metal/MetalBufferAllocator.h"
#include "Cafe/HW/Latte/Renderer/Metal/MetalSharedBufferTracker.h"

#include "GameProfile/GameProfile.h"
#include "Common/DeviceCapabilities.h"

#include <array>
#include <unordered_map>
#include <vector>

struct MetalArgumentBinding
{
    enum class Type : uint64 { Unused, Buffer, Texture, Sampler, Constant };
    Type type = Type::Unused;
    void* resource = nullptr;
    size_t value = 0;

    bool operator==(const MetalArgumentBinding&) const = default;
};

static_assert(sizeof(MetalArgumentBinding) == 24, "MetalArgumentBinding must have no padding");
using MetalArgumentBindings = std::array<MetalArgumentBinding, MetalArgumentBuffer::IndexType + 1>;

class MetalMemoryManager
{
public:
    MetalMemoryManager(class MetalRenderer* metalRenderer) : m_mtlr{metalRenderer}, m_stagingAllocator(m_mtlr, m_mtlr->GetOptimalBufferStorageMode(), (size_t)DeviceCaps::GetBudgets().stagingChunkBytes), m_indexAllocator(m_mtlr, m_mtlr->GetOptimalBufferStorageMode(), 4u * 1024 * 1024), m_snapshotAllocator(m_mtlr, m_mtlr->GetOptimalBufferStorageMode(), 4u * 1024 * 1024) {}
    ~MetalMemoryManager();

    static constexpr uint32 VertexSnapshotBase = 0;
    static constexpr uint32 UniformSnapshotBase = VertexSnapshotBase + MAX_MTL_VERTEX_BUFFERS;
    static constexpr uint32 SupportSnapshotBase = UniformSnapshotBase + METAL_GENERAL_SHADER_TYPE_TOTAL * MAX_MTL_BUFFERS;
    static constexpr uint32 SnapshotCount = SupportSnapshotBase + METAL_SHADER_TYPE_TOTAL;

    MetalSynchronizedHeapAllocator::AllocatorReservation* GetCachedSnapshot(uint32 slot, const void* data, uint32 size, uint32 firstByte = 0);
    MetalSynchronizedHeapAllocator::AllocatorReservation* GetCachedArgumentBuffer(uint32 stage, MTL::ArgumentEncoder* encoder, const MetalArgumentBindings& bindings);
    void ReleaseArgumentCache();
    void AgeArgumentCache();
    void GetSnapshotStats(uint32& numBuffers, size_t& totalSize, size_t& freeSize) const
    {
        m_snapshotAllocator.GetStats(numBuffers, totalSize, freeSize);
    }

    MetalSynchronizedRingAllocator& GetStagingAllocator()
    {
        return m_stagingAllocator;
    }

    MetalSynchronizedHeapAllocator& GetIndexAllocator()
    {
        return m_indexAllocator;
    }

    MTL::Buffer* GetBufferCache()
    {
        return m_bufferCache;
    }

    MTL::Buffer* GetImportedMemoryBuffer()
    {
        return m_importedMemoryBuffer;
    }

    void CleanupBuffers(MTL::CommandBuffer* latestFinishedCommandBuffer)
    {
        m_stagingAllocator.CleanupBuffer(latestFinishedCommandBuffer);
        m_indexAllocator.CleanupBuffer(latestFinishedCommandBuffer);
        m_snapshotAllocator.CleanupBuffer(latestFinishedCommandBuffer);
        ReclaimArgumentBlocks(latestFinishedCommandBuffer);
        m_sharedTracker.Complete(latestFinishedCommandBuffer);
    }

    void* AcquireTextureUploadBuffer(size_t size);
    void ReleaseTextureUploadBuffer(uint8* mem);

    void InitBufferCache(size_t size);
    size_t GetBufferCacheSize() const
    {
        return m_bufferCacheSize;
    }
    void UploadToBufferCache(const void* data, size_t offset, size_t size);
    void CopyBufferCache(size_t srcOffset, size_t dstOffset, size_t size);
    void TrackSharedCache(MTL::Buffer* buffer, size_t offset, size_t size, bool write = false);
    bool SharedCacheBusy(size_t offset, size_t size, bool writesOnly = false) const;
    void NotifyBufferCacheRangeModified(size_t offset, size_t size);
    void NotifyImportedMemoryRangeModified(size_t offset, size_t size);

    bool UseHostMemoryForCache() const
    {
        return (m_metalBufferCacheMode == MetalBufferCacheMode::Host);
    }

    bool NeedsReducedLatency() const
    {
        return (m_metalBufferCacheMode == MetalBufferCacheMode::DeviceShared || m_metalBufferCacheMode == MetalBufferCacheMode::Host);
    }

    MPTR GetImportedMemBaseAddress() const
    {
        return m_importedMemBaseAddress;
    }

    size_t GetHostAllocationSize() const
    {
        return m_hostAllocationSize;
    }

    bool IsRangeImported(MPTR address, size_t size) const
    {
        if (!UseHostMemoryForCache() || !m_importedMemoryBuffer)
            return false;
        if (address < m_importedMemBaseAddress)
            return false;

        uint64 offset = (uint64)address - m_importedMemBaseAddress;
        return offset <= m_hostAllocationSize && size <= (m_hostAllocationSize - offset);
    }

    size_t GetImportedMemoryOffset(MPTR address) const
    {
        cemu_assert_debug(address >= m_importedMemBaseAddress);
        return (size_t)((uint64)address - m_importedMemBaseAddress);
    }

private:
    void NotifyBufferRangeModified(MTL::Buffer* buffer, size_t offset, size_t size);

    class MetalRenderer* m_mtlr;

    std::vector<uint8> m_textureUploadBuffer;

    MetalSynchronizedRingAllocator m_stagingAllocator;
    MetalSynchronizedHeapAllocator m_indexAllocator;
    MetalSynchronizedHeapAllocator m_snapshotAllocator;

    struct BufferSnapshot
    {
        MetalSynchronizedHeapAllocator::AllocatorReservation* allocation = nullptr;
        uint32 firstByte = 0;
        uint32 endByte = 0;
    } m_snapshots[SnapshotCount]{};

    static constexpr uint32 ARGUMENT_CACHE_ENTRIES = 48;
    static constexpr uint32 ARGUMENT_CACHE_MAX_AGE = 6;
    struct ArgumentCacheEntry
    {
        uint64 hash = 0;
        uint64 lastUse = 0;
        uint64 lastGeneration = 0;
        MTL::ArgumentEncoder* encoder = nullptr;
        MetalArgumentBindings bindings{};
        MetalSynchronizedHeapAllocator::AllocatorReservation* allocation = nullptr;
    };
    struct ArgumentCache
    {
        std::vector<ArgumentCacheEntry> entries;
        uint64 useCounter = 0;
    } m_argumentCaches[METAL_SHADER_TYPE_TOTAL]{};
    uint64 m_argumentGeneration = 0;

    static constexpr uint32 ARGUMENT_BLOCK_POOL_MAX = 1536;
    using ArgumentBlock = MetalSynchronizedHeapAllocator::AllocatorReservation;
    ArgumentBlock* AllocateArgumentBlock(uint32 size, uint32 alignment);
    void RetireArgumentBlock(ArgumentBlock* block);
    void ReclaimArgumentBlocks(MTL::CommandBuffer* finishedCommandBuffer);
    std::unordered_map<uint32, std::vector<ArgumentBlock*>> m_freeArgumentBlocks;
    std::unordered_map<MTL::CommandBuffer*, std::vector<ArgumentBlock*>> m_retiringArgumentBlocks;
    uint32 m_pooledArgumentBlocks = 0;
    void ReleaseArgumentCacheEntry(ArgumentCacheEntry& entry);

    MTL::Buffer* m_bufferCache = nullptr;
    size_t m_bufferCacheSize = 0;
    MTL::Buffer* m_importedMemoryBuffer = nullptr;
    bool m_importedRegistered = false;
    MetalBufferCacheMode m_metalBufferCacheMode;
    MPTR m_importedMemBaseAddress;
    size_t m_hostAllocationSize = 0;
    MetalSharedBufferTracker m_sharedTracker;
};

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "Cafe/HW/Latte/Renderer/Metal/MetalPipelineCompiler.h"
#include "util/helpers/ConcurrentQueue.h"
#include "util/helpers/fspinlock.h"
#include "util/math/vector2.h"

void MetalPipelineCache_DrainAsyncCompiles();
size_t MetalPipelineCache_GetAsyncCompileCount();
bool MetalPipelineCache_LoaderAbandoned();

NS::Array* MetalPipelineCache_GetBinaryArchives();
void MetalPipelineCache_NoteArchiveLookup(bool hit);
void MetalPipelineCache_QueueArchiveAdd(MTL::RenderPipelineDescriptor* desc);
bool MetalPipelineCache_FlushArchive(uint32 timeoutMs);

class MetalPipelineCache
{
public:
	static MetalPipelineCache& GetInstance();

    MetalPipelineCache(class MetalRenderer* metalRenderer);
    ~MetalPipelineCache();

    PipelineObject* GetRenderPipelineState(const LatteFetchShader* fetchShader, const LatteDecompilerShader* vertexShader, const LatteDecompilerShader* geometryShader, const LatteDecompilerShader* pixelShader, const class MetalAttachmentsInfo& lastUsedAttachmentsInfo, const class MetalAttachmentsInfo& activeAttachmentsInfo, Vector2i extend, uint32 indexCount, const LatteContextRegister& lcr);

	uint32 BeginLoading(uint64 cacheTitleId);
	bool UpdateLoading(uint32& pipelinesLoadedTotal, uint32& pipelinesMissingShaders);
	void EndLoading();
	void LoadPipelineFromCache(std::span<uint8> fileData);
       void Close();
	bool StopLoading(uint32 timeoutMs);

    NS::Array* GetBinaryArchives() const { return m_binaryArchives; }
    void NoteArchiveLookup(bool hit) { (hit ? m_archiveHits : m_archiveMisses).fetch_add(1, std::memory_order_relaxed); }
    void QueueArchiveAdd(MTL::RenderPipelineDescriptor* desc);
    bool FlushBinaryArchive(uint32 timeoutMs);

    size_t GetPipelineCacheSize() const { return m_pipelineCache.size(); }

private:
    class MetalRenderer* m_mtlr;

    std::unordered_map<uint64, PipelineObject*> m_pipelineCache;
    FSpinlock m_pipelineCacheLock;

	std::thread* m_pipelineCacheStoreThread{nullptr};

	class FileCache* s_cache{nullptr};

	std::atomic_uint32_t m_numCompilationThreads{ 0 };
	std::atomic_uint32_t m_loaderThreadsRunning{ 0 };
	ConcurrentQueue<std::vector<uint8>> m_compilationQueue;
	std::atomic_uint32_t m_compilationCount;

    static uint64 CalculatePipelineHash(const LatteFetchShader* fetchShader, const LatteDecompilerShader* vertexShader, const LatteDecompilerShader* geometryShader, const LatteDecompilerShader* pixelShader, const class MetalAttachmentsInfo& lastUsedAttachmentsInfo, const class MetalAttachmentsInfo& activeAttachmentsInfo, const LatteContextRegister& lcr);

    void AddCurrentStateToCache(uint64 pipelineStateHash, const class MetalAttachmentsInfo& lastUsedAttachmentsInfo);

	bool SerializePipeline(class MemStreamWriter& memWriter, struct CachedPipeline& cachedPipeline);
	bool DeserializePipeline(class MemStreamReader& memReader, struct CachedPipeline& cachedPipeline);

    NS::Array* m_binaryArchives{nullptr};
    std::atomic_uint32_t m_archiveHits{0};
    std::atomic_uint32_t m_archiveMisses{0};
    std::atomic_uint32_t m_pipelinesDeduped{0};
    std::chrono::steady_clock::time_point m_loadStart;
    uint32_t m_loadShaderCompilesAtStart{0};
    uint64_t m_loadShaderCompileNsAtStart{0};
    uint32_t m_loadPipelineCompilesAtStart{0};
    uint64_t m_loadPipelineCompileNsAtStart{0};
    uint32_t m_archiveFilesLoaded{0};

    std::mutex m_archiveMutex;
    std::condition_variable m_archiveCv;
    std::deque<MTL::RenderPipelineDescriptor*> m_archiveQueue;
    std::thread* m_archiveThread{nullptr};
    bool m_archiveStop{false};
    bool m_archiveThreadAlive{false};
    bool m_archiveFlushRequested{false};
    uint64_t m_archiveFlushGen{0};
    uint64_t m_archiveFlushDoneGen{0};
    std::condition_variable m_archiveFlushCv;
    std::atomic<bool> m_archiveWriteEnabled{false};
    std::string m_archiveWritePath;
    uint64_t m_archiveBaseBytes{0};

    void OpenBinaryArchives(uint64 cacheTitleId);
    void CloseBinaryArchives();
    void BinaryArchiveWriterThread();

    int CompilerThread();
	void WorkerThread();
};

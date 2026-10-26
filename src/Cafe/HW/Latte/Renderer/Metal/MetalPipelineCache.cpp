#include "Cafe/HW/Latte/Renderer/Metal/MetalPipelineCache.h"
#include "Cafe/HW/Latte/Renderer/Metal/MetalRenderer.h"
#include "Cafe/HW/Latte/Renderer/Metal/LatteToMtl.h"
#include "Cafe/HW/Latte/Renderer/Metal/MetalPipelineCompiler.h"

#include "Cafe/HW/Latte/Core/FetchShader.h"
#include "Cafe/HW/Latte/ISA/RegDefines.h"
#include "Cafe/HW/Latte/Core/LatteConst.h"
#include "Cafe/HW/Latte/Common/RegisterSerializer.h"
#include "Cafe/HW/Latte/Core/LatteShaderCache.h"
#include "Cafe/HW/Latte/Core/LatteShader.h"
#include "Cafe/HW/Latte/Core/PerfTelemetry.h"
#include "Cafe/HW/Latte/ISA/LatteReg.h"
#include "Cemu/FileCache/FileCache.h"
#include "Common/precompiled.h"
#include "util/helpers/helpers.h"
#include "config/ActiveSettings.h"

#include <openssl/sha.h>
#include <sys/sysctl.h>
#include <mach/mach.h>
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#include <os/proc.h>
#endif
#include <condition_variable>
#include <chrono>
#include <deque>
#include <fstream>

static bool g_compilePipelineThreadInit{false};
static std::mutex g_compilePipelineMutex;
static std::condition_variable g_compilePipelineCondVar;
static std::queue<MetalPipelineCompiler*> g_compilePipelineRequests;
static std::atomic<uint32_t> g_compilePipelineInFlight{0};

namespace
{
	// Pipeline compiles hold a lot of memory while they run. Loading a big cache on all cores at once took a
	// 6 GB iPad from 1.3 GB to 2 MB free in seconds, before a single texture existed, and iOS killed the app.
	// With little memory left only a few compiles run at a time; one always may, so loading never stops.
	constexpr uint64 kCompileMemoryLow = 1024ull << 20;
	constexpr uint64 kCompileMemoryCritical = 512ull << 20;
	std::mutex s_compileGateMutex;
	std::condition_variable s_compileGateCv;
	uint32 s_compilesRunning = 0;
	std::atomic<uint32> s_compileGateLogs{0};

	struct CompileMemoryGate
	{
		CompileMemoryGate()
		{
			std::unique_lock lock(s_compileGateMutex);
			bool waited = false;
			for (;;)
			{
				const uint64 available = (uint64)os_proc_available_memory();
				const uint32 allowed = available == 0 ? UINT32_MAX : (available < kCompileMemoryCritical ? 1 : (available < kCompileMemoryLow ? 2 : UINT32_MAX));
				if (s_compilesRunning < allowed)
					break;
				if (!waited && s_compileGateLogs++ < 8)
					cemuLog_log(LogType::Force, "Metal: only {} MB free, pipeline compiles wait so at most {} run at once", available >> 20, allowed);
				waited = true;
				s_compileGateCv.wait_for(lock, std::chrono::milliseconds(20));
			}
			++s_compilesRunning;
		}
		~CompileMemoryGate()
		{
			{
				std::lock_guard lock(s_compileGateMutex);
				--s_compilesRunning;
			}
			s_compileGateCv.notify_all();
		}
	};
}

static void compileThreadFunc(sint32 threadIndex)
{
	SetThreadName("compilePl");

	if (threadIndex != 0)
		;

	while (true)
	{
		std::unique_lock lock(g_compilePipelineMutex);
		while (g_compilePipelineRequests.empty())
			g_compilePipelineCondVar.wait(lock);

		MetalPipelineCompiler* request = g_compilePipelineRequests.front();

		g_compilePipelineRequests.pop();

		lock.unlock();

		{
			CompileMemoryGate gate;
			request->Compile(true, false, true);
		}
		delete request;
		g_compilePipelineInFlight.fetch_sub(1);
	}
}

void MetalPipelineCache_DrainAsyncCompiles()
{
	{
		std::unique_lock lock(g_compilePipelineMutex);
		while (!g_compilePipelineRequests.empty())
		{
			delete g_compilePipelineRequests.front();
			g_compilePipelineRequests.pop();
			g_compilePipelineInFlight.fetch_sub(1);
		}
	}
	for (int i = 0; i < 1500 && g_compilePipelineInFlight.load() != 0; i++)
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	if (g_compilePipelineInFlight.load() != 0)
		cemuLog_log(LogType::Force, "Metal: {} pipeline compile(s) were still running after 3 s", g_compilePipelineInFlight.load());
}

size_t MetalPipelineCache_GetAsyncCompileCount()
{
	return g_compilePipelineInFlight.load();
}

static void initCompileThread()
{
	uint32 numCompileThreads;

	uint32 cpuCoreCount = GetPhysicalCoreCount();
	if (cpuCoreCount <= 2)
		numCompileThreads = 1;
	else
		numCompileThreads = 2 + (cpuCoreCount - 3);

	numCompileThreads = std::min(numCompileThreads, 8u);

	for (uint32 i = 0; i < numCompileThreads; i++)
	{
		std::thread compileThread(compileThreadFunc, i);
		compileThread.detach();
	}
}

static void queuePipeline(MetalPipelineCompiler* v)
{
	g_compilePipelineInFlight.fetch_add(1);
	std::unique_lock lock(g_compilePipelineMutex);
	g_compilePipelineRequests.push(std::move(v));
	lock.unlock();
	g_compilePipelineCondVar.notify_one();
}

bool IsAsyncPipelineAllowed(const MetalAttachmentsInfo& attachmentsInfo, Vector2i extend, uint32 indexCount)
{
	if (extend.x == 1600 && extend.y == 1600)
		return false;

	if (attachmentsInfo.depthFormat != Latte::E_GX2SURFFMT::INVALID_FORMAT)
		return true;

	if (indexCount <= 6)
		return false;

	return true;
}

MetalPipelineCache* g_mtlPipelineCache = nullptr;
static std::mutex g_mtlPipelineCacheLifeMutex;

MetalPipelineCache& MetalPipelineCache::GetInstance()
{
    return *g_mtlPipelineCache;
}

MetalPipelineCache::MetalPipelineCache(class MetalRenderer* metalRenderer) : m_mtlr{metalRenderer}
{
    g_mtlPipelineCache = this;
}

MetalPipelineCache::~MetalPipelineCache()
{
    std::lock_guard<std::mutex> lifeLock(g_mtlPipelineCacheLifeMutex);
    EndLoading();
    Close();
    for (auto& [key, pipelineObj] : m_pipelineCache)
    {
        pipelineObj->m_pipeline->release();
        delete pipelineObj;
    }
    if (g_mtlPipelineCache == this)
        g_mtlPipelineCache = nullptr;
}

PipelineObject* MetalPipelineCache::GetRenderPipelineState(const LatteFetchShader* fetchShader, const LatteDecompilerShader* vertexShader, const LatteDecompilerShader* geometryShader, const LatteDecompilerShader* pixelShader, const MetalAttachmentsInfo& lastUsedAttachmentsInfo, const MetalAttachmentsInfo& activeAttachmentsInfo, Vector2i extend, uint32 indexCount, const LatteContextRegister& lcr)
{
    uint64 hash = CalculatePipelineHash(fetchShader, vertexShader, geometryShader, pixelShader, lastUsedAttachmentsInfo, activeAttachmentsInfo, lcr);
    PipelineObject*& pipelineObj = m_pipelineCache[hash];
    if (pipelineObj)
        return pipelineObj;

    pipelineObj = new PipelineObject();

    MetalPipelineCompiler* compiler = new MetalPipelineCompiler(m_mtlr, *pipelineObj);
    compiler->InitFromState(fetchShader, vertexShader, geometryShader, pixelShader, lastUsedAttachmentsInfo, activeAttachmentsInfo, lcr);

    bool allowAsyncCompile = false;
    if (GetConfig().async_compile)
		allowAsyncCompile = IsAsyncPipelineAllowed(activeAttachmentsInfo, extend, indexCount);

	if (allowAsyncCompile)
	{
	    if (!g_compilePipelineThreadInit)
		{
			initCompileThread();
			g_compilePipelineThreadInit = true;
		}

		queuePipeline(compiler);
	}
	else
	{
        PerfTelemetry::Get().pipelineSyncCompiles.fetch_add(1, std::memory_order_relaxed);
        cemu_assert_debug(compiler->Compile(true, true, true));
        delete compiler;
	}

    AddCurrentStateToCache(hash, lastUsedAttachmentsInfo);

    return pipelineObj;
}

uint64 MetalPipelineCache::CalculatePipelineHash(const LatteFetchShader* fetchShader, const LatteDecompilerShader* vertexShader, const LatteDecompilerShader* geometryShader, const LatteDecompilerShader* pixelShader, const MetalAttachmentsInfo& lastUsedAttachmentsInfo, const MetalAttachmentsInfo& activeAttachmentsInfo, const LatteContextRegister& lcr)
{
    uint64 stateHash = 0;
    for (int i = 0; i < Latte::GPU_LIMITS::NUM_COLOR_ATTACHMENTS; ++i)
	{
	    Latte::E_GX2SURFFMT format = lastUsedAttachmentsInfo.colorFormats[i];
		if (format == Latte::E_GX2SURFFMT::INVALID_FORMAT)
            continue;

		stateHash += (uint64)format + i * 31;
		stateHash = std::rotl<uint64>(stateHash, 7);

		if (activeAttachmentsInfo.colorFormats[i] == Latte::E_GX2SURFFMT::INVALID_FORMAT)
		{
            stateHash += 1;
		    stateHash = std::rotl<uint64>(stateHash, 1);
		}
	}

	if (lastUsedAttachmentsInfo.depthFormat != Latte::E_GX2SURFFMT::INVALID_FORMAT)
	{
		stateHash += (uint64)lastUsedAttachmentsInfo.depthFormat;
		stateHash = std::rotl<uint64>(stateHash, 7);
		stateHash += lastUsedAttachmentsInfo.hasStencil ? 1 : 0;
		stateHash = std::rotl<uint64>(stateHash, 1);

		if (activeAttachmentsInfo.depthFormat == Latte::E_GX2SURFFMT::INVALID_FORMAT)
		{
            stateHash += 1;
		    stateHash = std::rotl<uint64>(stateHash, 1);
		}
	}

	for (auto& group : fetchShader->bufferGroups)
	{
		uint32 bufferStride = group.getCurrentBufferStride(lcr.GetRawView());
		stateHash = std::rotl<uint64>(stateHash, 7);
		stateHash += bufferStride * 3;
	}

	stateHash += fetchShader->getVkPipelineHashFragment();
	stateHash = std::rotl<uint64>(stateHash, 7);

	stateHash += lcr.GetRawView()[mmVGT_STRMOUT_EN];
	stateHash = std::rotl<uint64>(stateHash, 7);

	if(lcr.PA_CL_CLIP_CNTL.get_DX_RASTERIZATION_KILL())
		stateHash += 0x333333;

	stateHash = (stateHash >> 8) + (stateHash * 0x370531ull) % 0x7F980D3BF9B4639Dull;

	uint32* ctxRegister = lcr.GetRawView();

	if (vertexShader)
		stateHash += vertexShader->baseHash + std::rotl<uint64>(vertexShader->auxHash, 17);

	stateHash = std::rotl<uint64>(stateHash, 13);

	if (geometryShader)
		stateHash += geometryShader->baseHash + std::rotl<uint64>(geometryShader->auxHash, 29);

	stateHash = std::rotl<uint64>(stateHash, 13);

	if (pixelShader)
		stateHash += pixelShader->baseHash + std::rotl<uint64>(pixelShader->auxHash, 41);

	stateHash = std::rotl<uint64>(stateHash, 13);

	uint32 polygonCtrl = lcr.PA_SU_SC_MODE_CNTL.getRawValue();
	stateHash += polygonCtrl;
	stateHash = std::rotl<uint64>(stateHash, 7);

	stateHash += ctxRegister[Latte::REGADDR::PA_CL_CLIP_CNTL];
	stateHash = std::rotl<uint64>(stateHash, 7);

	const auto colorControlReg = ctxRegister[Latte::REGADDR::CB_COLOR_CONTROL];
	stateHash += colorControlReg;

	stateHash += ctxRegister[Latte::REGADDR::CB_TARGET_MASK];

	const uint32 blendEnableMask = (colorControlReg >> 8) & 0xFF;
	if (blendEnableMask)
	{
		for (auto i = 0; i < 8; ++i)
		{
			if (((blendEnableMask & (1 << i))) == 0)
				continue;
			stateHash = std::rotl<uint64>(stateHash, 7);
			stateHash += ctxRegister[Latte::REGADDR::CB_BLEND0_CONTROL + i];
		}
	}

	const LattePrimitiveMode primitiveMode = static_cast<LattePrimitiveMode>(lcr.GetRawView()[mmVGT_PRIMITIVE_TYPE]);
    bool isPrimitiveRect = (primitiveMode == Latte::LATTE_VGT_PRIMITIVE_TYPE::E_PRIMITIVE_TYPE::RECTS);

    bool usesGeometryShader = (geometryShader != nullptr || isPrimitiveRect);

    if (usesGeometryShader)
    {
        stateHash += lcr.GetRawView()[mmVGT_PRIMITIVE_TYPE];
        stateHash = std::rotl<uint64>(stateHash, 7);
    }

	return stateHash;
}

struct
{
	uint32 pipelineLoadIndex;
	uint32 pipelineMaxFileIndex;

	std::atomic_uint32_t pipelinesQueued;
	std::atomic_uint32_t pipelinesLoaded;
} g_mtlCacheState;

static std::vector<LatteShaderCachePipelineRekey> s_pendingRekeys;

uint32 MetalPipelineCache::BeginLoading(uint64 cacheTitleId)
{
	std::error_code ec;
	fs::create_directories(ActiveSettings::GetCachePath("shaderCache/transferable"), ec);
	const auto pathCacheFile = ActiveSettings::GetCachePath("shaderCache/transferable/{:016x}_mtlpipeline.bin", cacheTitleId);

	g_mtlCacheState.pipelineLoadIndex = 0;
	g_mtlCacheState.pipelineMaxFileIndex = 0;
	g_mtlCacheState.pipelinesLoaded = 0;
	g_mtlCacheState.pipelinesQueued = 0;
	s_pendingRekeys.clear();

	m_loadStart = std::chrono::steady_clock::now();
	m_loadShaderCompilesAtStart = PerfTelemetry::Get().shaderCompiles.load(std::memory_order_relaxed);
	m_loadShaderCompileNsAtStart = PerfTelemetry::Get().shaderCompileNs.load(std::memory_order_relaxed);
	m_loadPipelineCompilesAtStart = PerfTelemetry::Get().pipelineCompiles.load(std::memory_order_relaxed);
	m_loadPipelineCompileNsAtStart = PerfTelemetry::Get().pipelineCompileNs.load(std::memory_order_relaxed);
	m_archiveHits = 0;
	m_archiveMisses = 0;
	m_pipelinesDeduped = 0;
	OpenBinaryArchives(cacheTitleId);

	m_compilationCount.store(0);
	m_compilationQueue.clear();

	uint32 cpuCoreCount = GetPhysicalCoreCount();
	m_numCompilationThreads = std::clamp(cpuCoreCount, 1u, 8u);

	for (uint32 i = 0; i < m_numCompilationThreads; i++)
	{
		m_loaderThreadsRunning.fetch_add(1);
		std::thread compileThread(&MetalPipelineCache::CompilerThread, this);
		compileThread.detach();
	}

	cemu_assert_debug(s_cache == nullptr);
	s_cache = FileCache::Open(pathCacheFile, true, LatteShaderCache_getPipelineCacheExtraVersion(cacheTitleId));
	if (!s_cache)
	{
		cemuLog_log(LogType::Force, "Failed to open or create Metal pipeline cache file: {}", _pathToUtf8(pathCacheFile));
		return 0;
	}
	else
	{
		s_cache->UseCompression(false);
		g_mtlCacheState.pipelineMaxFileIndex = s_cache->GetMaximumFileIndex();
	}
	return s_cache->GetFileCount();
}

bool MetalPipelineCache::UpdateLoading(uint32& pipelinesLoadedTotal, uint32& pipelinesMissingShaders)
{
	pipelinesLoadedTotal = g_mtlCacheState.pipelinesLoaded;
	pipelinesMissingShaders = 0;
	while (g_mtlCacheState.pipelineLoadIndex <= g_mtlCacheState.pipelineMaxFileIndex)
	{
		if (m_compilationQueue.size() >= 50)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			return true;
		}

		uint64 fileNameA, fileNameB;
		std::vector<uint8> fileData;
		if (s_cache->GetFileByIndex(g_mtlCacheState.pipelineLoadIndex, &fileNameA, &fileNameB, fileData))
		{
			LatteShaderCache_TranslatePipelineEntry(fileNameA, fileNameB, fileData, s_pendingRekeys);
			g_mtlCacheState.pipelinesQueued++;
			m_compilationQueue.push(std::move(fileData));
			g_mtlCacheState.pipelineLoadIndex++;
			return true;
		}
		g_mtlCacheState.pipelineLoadIndex++;
	}
	if (g_mtlCacheState.pipelinesLoaded != g_mtlCacheState.pipelinesQueued)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		return true;
	}
	LatteShaderCache_ApplyPipelineRekeys(s_cache, s_pendingRekeys);
	{
		const auto wallMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_loadStart).count();
		const uint32 pipelineCompiles = PerfTelemetry::Get().pipelineCompiles.load(std::memory_order_relaxed) - m_loadPipelineCompilesAtStart;
		const uint64 pipelineMs = (PerfTelemetry::Get().pipelineCompileNs.load(std::memory_order_relaxed) - m_loadPipelineCompileNsAtStart) / 1000000;
		const uint32 shaderCompiles = PerfTelemetry::Get().shaderCompiles.load(std::memory_order_relaxed) - m_loadShaderCompilesAtStart;
		cemuLog_log(LogType::Force, "Metal pipeline load: {} cache entries, {} pipelines built in {} ms summed ({} ms avg), {} duplicate entries skipped, binary archive {} hits / {} misses from {} file(s), {} shaders compiled on demand, {} ms wall for the pipeline stage",
			g_mtlCacheState.pipelinesQueued.load(), pipelineCompiles, pipelineMs, pipelineCompiles ? pipelineMs / pipelineCompiles : 0, m_pipelinesDeduped.load(), m_archiveHits.load(), m_archiveMisses.load(), m_archiveFilesLoaded, shaderCompiles, wallMs);
	}
	return false;
}

void MetalPipelineCache::EndLoading()
{
	uint32 threadCount = m_numCompilationThreads;
	m_numCompilationThreads = 0;
	for (uint32 i = 0; i < threadCount; i++)
	{
		m_compilationQueue.push({});
	}
}

struct CachedPipeline
{
	struct ShaderHash
	{
		uint64 baseHash;
		uint64 auxHash;
		bool isPresent{};

		void set(uint64 baseHash, uint64 auxHash)
		{
			this->baseHash = baseHash;
			this->auxHash = auxHash;
			this->isPresent = true;
		}
	};

	ShaderHash vsHash;
	ShaderHash gsHash;
	ShaderHash psHash;

	MetalAttachmentsInfo lastUsedAttachmentsInfo;

	Latte::GPUCompactedRegisterState gpuState;
};

void MetalPipelineCache::LoadPipelineFromCache(std::span<uint8> fileData)
{
	static FSpinlock s_spinlockSharedInternal;

	LatteContextRegister* lcr = new LatteContextRegister();
	s_spinlockSharedInternal.lock();
	CachedPipeline* cachedPipeline = new CachedPipeline();
	s_spinlockSharedInternal.unlock();

	MemStreamReader streamReader(fileData.data(), fileData.size());
	if (!DeserializePipeline(streamReader, *cachedPipeline))
	{
		s_spinlockSharedInternal.lock();
		delete lcr;
		delete cachedPipeline;
		s_spinlockSharedInternal.unlock();
		return;
	}
	Latte::LoadGPURegisterState(*lcr, cachedPipeline->gpuState);

	LatteDecompilerShader* vertexShader = nullptr;
	LatteDecompilerShader* geometryShader = nullptr;
	LatteDecompilerShader* pixelShader = nullptr;
	if (cachedPipeline->vsHash.isPresent)
	{
		vertexShader = LatteSHRC_FindVertexShader(cachedPipeline->vsHash.baseHash, cachedPipeline->vsHash.auxHash);
		if (!vertexShader)
		{
			cemuLog_log(LogType::Force, "Vertex shader not found in cache");
			return;
		}
	}
	if (cachedPipeline->gsHash.isPresent)
	{
		geometryShader = LatteSHRC_FindGeometryShader(cachedPipeline->gsHash.baseHash, cachedPipeline->gsHash.auxHash);
		if (!geometryShader)
		{
			cemuLog_log(LogType::Force, "Geometry shader not found in cache");
			return;
		}
	}
	if (cachedPipeline->psHash.isPresent)
	{
		pixelShader = LatteSHRC_FindPixelShader(cachedPipeline->psHash.baseHash, cachedPipeline->psHash.auxHash);
		if (!pixelShader)
		{
			cemuLog_log(LogType::Force, "Pixel shader not found in cache");
			return;
		}
	}

	if (!pixelShader)
	{
		cemu_assert_debug(false);
		return;
	}

	MetalAttachmentsInfo attachmentsInfo(*lcr, pixelShader);

	uint64 pipelineStateHash = CalculatePipelineHash(vertexShader->compatibleFetchShader, vertexShader, geometryShader, pixelShader, cachedPipeline->lastUsedAttachmentsInfo, attachmentsInfo, *lcr);
	PipelineObject* pipelineObject = new PipelineObject();
	m_pipelineCacheLock.lock();
	const bool claimed = m_pipelineCache.try_emplace(pipelineStateHash, pipelineObject).second;
	m_pipelineCacheLock.unlock();
	if (!claimed)
	{
		delete pipelineObject;
		m_pipelinesDeduped.fetch_add(1, std::memory_order_relaxed);
		s_spinlockSharedInternal.lock();
		delete lcr;
		delete cachedPipeline;
		s_spinlockSharedInternal.unlock();
		return;
	}

	{
		MetalPipelineCompiler pp(m_mtlr, *pipelineObject);
		pp.InitFromState(vertexShader->compatibleFetchShader, vertexShader, geometryShader, pixelShader, cachedPipeline->lastUsedAttachmentsInfo, attachmentsInfo, *lcr);
		pp.Compile(true, true, false);
	}

	s_spinlockSharedInternal.lock();
	delete lcr;
	delete cachedPipeline;
	s_spinlockSharedInternal.unlock();
}

ConcurrentQueue<CachedPipeline*> g_mtlPipelineCachingQueue;

void MetalPipelineCache::Close()
{
    if (m_pipelineCacheStoreThread)
    {
        CachedPipeline* stopJob = nullptr;
        g_mtlPipelineCachingQueue.push(stopJob);
        m_pipelineCacheStoreThread->join();
        delete m_pipelineCacheStoreThread;
        m_pipelineCacheStoreThread = nullptr;
    }
    CachedPipeline* leftover = nullptr;
    while (g_mtlPipelineCachingQueue.peek2(leftover))
        delete leftover;
    if(s_cache)
    {
        delete s_cache;
        s_cache = nullptr;
    }
    CloseBinaryArchives();
}

void MetalPipelineCache::AddCurrentStateToCache(uint64 pipelineStateHash, const MetalAttachmentsInfo& lastUsedAttachmentsInfo)
{
	if (!m_pipelineCacheStoreThread)
	{
		m_pipelineCacheStoreThread = new std::thread(&MetalPipelineCache::WorkerThread, this);
	}
	CachedPipeline* job = new CachedPipeline();
	auto vs = LatteSHRC_GetActiveVertexShader();
	auto gs = LatteSHRC_GetActiveGeometryShader();
	auto ps = LatteSHRC_GetActivePixelShader();
	if (vs)
		job->vsHash.set(vs->baseHash, vs->auxHash);
	if (gs)
		job->gsHash.set(gs->baseHash, gs->auxHash);
	if (ps)
		job->psHash.set(ps->baseHash, ps->auxHash);
	job->lastUsedAttachmentsInfo = lastUsedAttachmentsInfo;
	Latte::StoreGPURegisterState(LatteGPUState.contextNew, job->gpuState);
	g_mtlPipelineCachingQueue.push(job);
}

bool MetalPipelineCache::SerializePipeline(MemStreamWriter& memWriter, CachedPipeline& cachedPipeline)
{
	memWriter.writeBE<uint8>(0x01);
	uint8 presentMask = 0;
	if (cachedPipeline.vsHash.isPresent)
		presentMask |= 1;
	if (cachedPipeline.gsHash.isPresent)
		presentMask |= 2;
	if (cachedPipeline.psHash.isPresent)
		presentMask |= 4;
	memWriter.writeBE<uint8>(presentMask);
	if (cachedPipeline.vsHash.isPresent)
	{
		memWriter.writeBE<uint64>(cachedPipeline.vsHash.baseHash);
		memWriter.writeBE<uint64>(cachedPipeline.vsHash.auxHash);
	}
	if (cachedPipeline.gsHash.isPresent)
	{
		memWriter.writeBE<uint64>(cachedPipeline.gsHash.baseHash);
		memWriter.writeBE<uint64>(cachedPipeline.gsHash.auxHash);
	}
	if (cachedPipeline.psHash.isPresent)
	{
		memWriter.writeBE<uint64>(cachedPipeline.psHash.baseHash);
		memWriter.writeBE<uint64>(cachedPipeline.psHash.auxHash);
	}

	for (uint8 i = 0; i < LATTE_NUM_COLOR_TARGET; i++)
	    memWriter.writeBE<uint16>((uint16)cachedPipeline.lastUsedAttachmentsInfo.colorFormats[i]);
	memWriter.writeBE<uint16>((uint16)cachedPipeline.lastUsedAttachmentsInfo.depthFormat);
    memWriter.writeBE<uint8>(cachedPipeline.lastUsedAttachmentsInfo.hasStencil ? 1 : 0);

	Latte::SerializeRegisterState(cachedPipeline.gpuState, memWriter);

	return true;
}

bool MetalPipelineCache::DeserializePipeline(MemStreamReader& memReader, CachedPipeline& cachedPipeline)
{
	if (memReader.readBE<uint8>() != 1)
	{
		cemuLog_log(LogType::Force, "Cached Metal pipeline corrupted or has unknown version");
		return false;
	}
	uint8 presentMask = memReader.readBE<uint8>();
	if (presentMask & 1)
	{
		uint64 baseHash = memReader.readBE<uint64>();
		uint64 auxHash = memReader.readBE<uint64>();
		cachedPipeline.vsHash.set(baseHash, auxHash);
	}
	if (presentMask & 2)
	{
		uint64 baseHash = memReader.readBE<uint64>();
		uint64 auxHash = memReader.readBE<uint64>();
		cachedPipeline.gsHash.set(baseHash, auxHash);
	}
	if (presentMask & 4)
	{
		uint64 baseHash = memReader.readBE<uint64>();
		uint64 auxHash = memReader.readBE<uint64>();
		cachedPipeline.psHash.set(baseHash, auxHash);
	}

	for (uint8 i = 0; i < LATTE_NUM_COLOR_TARGET; i++)
	    cachedPipeline.lastUsedAttachmentsInfo.colorFormats[i] = (Latte::E_GX2SURFFMT)memReader.readBE<uint16>();
	cachedPipeline.lastUsedAttachmentsInfo.depthFormat = (Latte::E_GX2SURFFMT)memReader.readBE<uint16>();
    cachedPipeline.lastUsedAttachmentsInfo.hasStencil = memReader.readBE<uint8>() != 0;

	if (!Latte::DeserializeRegisterState(cachedPipeline.gpuState, memReader))
	{
		return false;
	}
	cemu_assert_debug(!memReader.hasError());

	return true;
}

int MetalPipelineCache::CompilerThread()
{
	SetThreadName("plCacheCompiler");
	while (m_numCompilationThreads != 0)
	{
		std::vector<uint8> pipelineData = m_compilationQueue.pop();
		if(pipelineData.empty())
			continue;
		{
			CompileMemoryGate gate;
			LoadPipelineFromCache(pipelineData);
		}
		++g_mtlCacheState.pipelinesLoaded;
	}
	m_loaderThreadsRunning.fetch_sub(1);
	return 0;
}

static std::atomic<bool> g_mtlLoaderAbandoned{false};

bool MetalPipelineCache_LoaderAbandoned()
{
	return g_mtlLoaderAbandoned.load();
}

bool MetalPipelineCache::StopLoading(uint32 timeoutMs)
{
	EndLoading();
	for (uint32 waited = 0; m_loaderThreadsRunning.load() != 0 && waited < timeoutMs; waited += 2)
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	if (m_loaderThreadsRunning.load() == 0)
		return true;
	cemuLog_log(LogType::Force, "Metal: {} pipeline cache loader thread(s) did not stop within {} ms", m_loaderThreadsRunning.load(), timeoutMs);
	g_mtlLoaderAbandoned.store(true);
	return false;
}

void MetalPipelineCache::WorkerThread()
{
	SetThreadName("plCacheWriter");
	while (true)
	{
		CachedPipeline* job;
		g_mtlPipelineCachingQueue.pop(job);
		if (!job)
			return;
		if (!s_cache)
		{
			delete job;
			continue;
		}
		MemStreamWriter memWriter(1024 * 4);
		SerializePipeline(memWriter, *job);
		auto blob = memWriter.getResult();
		uint8 hash[SHA256_DIGEST_LENGTH];
		SHA256(blob.data(), blob.size(), hash);
		uint64 nameA = *(uint64be*)(hash + 0);
		uint64 nameB = *(uint64be*)(hash + 8);
		s_cache->AddFileAsync({ nameA, nameB }, blob.data(), blob.size());
		delete job;
	}
}

bool MetalPipelineCache_FlushArchive(uint32 timeoutMs)
{
	std::lock_guard<std::mutex> lifeLock(g_mtlPipelineCacheLifeMutex);
	return g_mtlPipelineCache ? g_mtlPipelineCache->FlushBinaryArchive(timeoutMs) : true;
}

NS::Array* MetalPipelineCache_GetBinaryArchives()
{
	return g_mtlPipelineCache ? g_mtlPipelineCache->GetBinaryArchives() : nullptr;
}

void MetalPipelineCache_NoteArchiveLookup(bool hit)
{
	if (g_mtlPipelineCache)
		g_mtlPipelineCache->NoteArchiveLookup(hit);
}

void MetalPipelineCache_QueueArchiveAdd(MTL::RenderPipelineDescriptor* desc)
{
	if (g_mtlPipelineCache)
		g_mtlPipelineCache->QueueArchiveAdd(desc);
}

static constexpr bool ARCHIVES_ENABLED = true;
// Metal throws std::bad_alloc on one of its own worker threads (not catchable) when newBinaryArchive finds no big enough
// free hole in the address space, so archives are only used when a large hole exists, for loading and for new archives
static constexpr uint64 ARCHIVE_MIN_FREE_HOLE_BYTES = 256ull * 1024 * 1024;
static constexpr uint32 ARCHIVE_MAX_FILES = 8;
static constexpr uint64 ARCHIVE_MAX_BYTES = 384ull * 1024 * 1024;
static constexpr uint64 ARCHIVE_LOAD_BYTES = 256ull * 1024 * 1024;
static constexpr uint64 ARCHIVE_ALL_TITLES_BYTES = 1024ull * 1024 * 1024;
static constexpr int ARCHIVE_MAX_AGE_DAYS = 45;
static constexpr size_t ARCHIVE_MAX_QUEUE = 20000;
static constexpr uint32 ARCHIVE_SAVE_EVERY = 2000;
// set while the archive is being closed, so the writer thread stops probing and does not start another save
static std::atomic<bool> s_archiveAbandon{false};

static std::string GetBinaryArchiveKey(MTL::Device* device)
{
	char osBuild[64] = {};
	size_t osBuildLen = sizeof(osBuild) - 1;
	sysctlbyname("kern.osversion", osBuild, &osBuildLen, nullptr, 0);
	const std::string id = fmt::format("{}|{}|v1", osBuild, device->name()->utf8String());
	uint64 h = 1469598103934665603ull;
	for (unsigned char c : id)
	{
		h ^= c;
		h *= 1099511628211ull;
	}
	return fmt::format("{:016x}", h);
}

// largest free hole in the address space below the highest mapped region, in bytes
static uint64 GetLargestAddressSpaceHole()
{
	uint64 largest = 0;
	vm_address_t addr = 0, prevEnd = 0;
	for (;;)
	{
		vm_size_t size = 0;
		vm_region_basic_info_data_64_t info;
		mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
		mach_port_t objectName = MACH_PORT_NULL;
		if (vm_region_64(mach_task_self(), &addr, &size, VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info, &count, &objectName) != KERN_SUCCESS)
			break;
		if (prevEnd != 0 && addr > prevEnd)
			largest = std::max<uint64>(largest, addr - prevEnd);
		prevEnd = addr + size;
		addr = prevEnd;
	}
	return largest;
}

void MetalPipelineCache::OpenBinaryArchives(uint64 cacheTitleId)
{
	NS_STACK_SCOPED NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
	CloseBinaryArchives();

	std::error_code ec;
	const fs::path dir = ActiveSettings::GetCachePath("shaderCache/precompiled");
	fs::create_directories(dir, ec);
	// A second title in the same process finds the address space fragmented by the first one and Metal
	// throws bad_alloc inside newBinaryArchive, so archives are only used for the first title per process
	static std::atomic<bool> s_archivesUsedInProcess{false};
	if (!ARCHIVES_ENABLED || s_archivesUsedInProcess.exchange(true))
	{
		m_archiveWriteEnabled = false;
		m_archiveFilesLoaded = 0;
		m_archiveBaseBytes = 0;
		cemuLog_log(LogType::Force, "Metal binary archive: not used for this launch (restart the app to use it again)");
		return;
	}
	// Opening archives crashed the app on this title before (bad_alloc inside Metal). It stays off for this title:
	// the system's own pipeline cache already makes later launches fast, and a crash on every second launch costs more.
	const fs::path disabledPath = dir / fmt::format("{:016x}_mtlopen.disabled", cacheTitleId);
	{
		std::error_code dEc;
		if (fs::exists(disabledPath, dEc))
		{
			m_archiveWriteEnabled = false;
			m_archiveFilesLoaded = 0;
			m_archiveBaseBytes = 0;
			cemuLog_log(LogType::Force, "Metal binary archive: switched off for this title because opening it crashed the app before (delete the compiled shaders in Settings to try again)");
			return;
		}
	}
	{
		const uint64 hole = GetLargestAddressSpaceHole();
		if (hole < ARCHIVE_MIN_FREE_HOLE_BYTES)
		{
			cemuLog_log(LogType::Force, "Metal binary archive: address space too fragmented (largest free hole {} MB, need {} MB), running without archives this time", hole / (1024 * 1024), ARCHIVE_MIN_FREE_HOLE_BYTES / (1024 * 1024));
			m_archiveWriteEnabled = false;
			m_archiveFilesLoaded = 0;
			m_archiveBaseBytes = 0;
			return;
		}
	}
	const std::string titlePrefix = fmt::format("{:016x}_mtlbin_", cacheTitleId);
	const std::string keyPrefix = titlePrefix + GetBinaryArchiveKey(m_mtlr->GetDevice()) + "_";

	struct ArchiveFile { fs::path path; uint64 size; fs::file_time_type time; bool currentTitle; };
	std::vector<ArchiveFile> all;
	const std::string keyOnly = "_mtlbin_" + GetBinaryArchiveKey(m_mtlr->GetDevice()) + "_";
	const auto ageLimit = fs::file_time_type::clock::now() - std::chrono::hours(24 * ARCHIVE_MAX_AGE_DAYS);
	uint32 prunedFiles = 0;
	uint64 prunedBytes = 0;
	auto prune = [&](const fs::path& path, uint64 size) {
		std::error_code rmEc;
		if (fs::remove(path, rmEc))
		{
			prunedFiles++;
			prunedBytes += size;
		}
	};
	for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
	{
		const std::string name = it->path().filename().string();
		if (name.find("_mtlbin_") == std::string::npos)
			continue;
		std::error_code sizeEc, timeEc;
		const uint64 size = it->is_regular_file(sizeEc) ? (uint64)it->file_size(sizeEc) : 0;
		const auto time = fs::last_write_time(it->path(), timeEc);
		const bool isBin = name.size() > 4 && name.compare(name.size() - 4, 4, ".bin") == 0;
		const bool isCurrentTitle = name.compare(0, titlePrefix.size(), titlePrefix) == 0;
		if (!isBin || name.find(keyOnly) == std::string::npos || (!isCurrentTitle && !timeEc && time < ageLimit))
		{
			prune(it->path(), size);
			continue;
		}
		all.push_back({it->path(), size, timeEc ? fs::file_time_type::min() : time, isCurrentTitle});
	}
	{
		uint64 total = 0;
		for (auto& f : all)
			total += f.size;
		std::sort(all.begin(), all.end(), [](const ArchiveFile& a, const ArchiveFile& b) { return a.time < b.time; });
		for (auto it = all.begin(); it != all.end() && total > ARCHIVE_ALL_TITLES_BYTES;)
		{
			if (it->currentTitle)
			{
				++it;
				continue;
			}
			total -= it->size;
			prune(it->path, it->size);
			it = all.erase(it);
		}
	}
	std::vector<fs::path> files;
	std::vector<bool> used(ARCHIVE_MAX_FILES, false);
	{
		std::vector<ArchiveFile> mine;
		for (auto& f : all)
			if (f.currentTitle)
				mine.push_back(f);
		std::sort(mine.begin(), mine.end(), [](const ArchiveFile& a, const ArchiveFile& b) { return a.path.filename().string() < b.path.filename().string(); });
		uint64 budget = 0;
		for (auto& f : mine)
		{
			std::error_code touchEc;
			fs::last_write_time(f.path, fs::file_time_type::clock::now(), touchEc);
			if (!files.empty() && budget + f.size > ARCHIVE_LOAD_BYTES)
			{
				const size_t skippedIdx = (size_t)strtoul(f.path.filename().string().c_str() + keyPrefix.size(), nullptr, 10);
				if (skippedIdx < used.size())
					used[skippedIdx] = true;
				continue;
			}
			budget += f.size;
			files.push_back(f.path);
		}
	}

	// Metal needs contiguous address space on top of the mapped file, skip (keeping the files) if there is too little
	{
		uint64 total = 0;
		for (const fs::path& f : files)
		{
			std::error_code sizeEc;
			total += fs::file_size(f, sizeEc);
		}
		const uint64 hole = files.empty() ? 0 : GetLargestAddressSpaceHole();
		if (!files.empty() && hole < total * 2)
		{
			cemuLog_log(LogType::Force, "Metal binary archive: address space too fragmented (largest free hole {} MB, archives {} MB), running without them this time", hole / (1024 * 1024), total / (1024 * 1024));
			m_archiveWriteEnabled = false;
			m_archiveFilesLoaded = 0;
			m_archiveBaseBytes = 0;
			return;
		}
	}

	// crash marker: if the previous attempt to open the archives never finished, they are
	// the likely cause (e.g. std::bad_alloc inside Metal), so drop them and run without
	const fs::path markerPath = dir / fmt::format("{:016x}_mtlopen.marker", cacheTitleId);
	{
		std::error_code mEc;
		if (fs::exists(markerPath, mEc))
		{
			cemuLog_log(LogType::Force, "Metal binary archive: previous open did not finish, deleting {} archive file(s) and continuing without", files.size());
			for (const fs::path& f : files)
			{
				std::error_code rmEc;
				fs::remove(f, rmEc);
			}
			files.clear();
			std::fill(used.begin(), used.end(), false);
			fs::remove(markerPath, mEc);
			{
				std::ofstream disabled(disabledPath, std::ios::binary);
				disabled << "1";
			}
			m_archiveWriteEnabled = false;
			m_archiveFilesLoaded = 0;
			m_archiveBaseBytes = 0;
			cemuLog_log(LogType::Force, "Metal binary archive: switched off for this title from now on");
			return;
		}
		else if (!files.empty())
		{
			std::ofstream marker(markerPath, std::ios::binary);
			marker << "1";
		}
	}

	std::vector<NS::Object*> loaded;
	uint64 bytes = 0;
	for (const fs::path& file : files)
	{
		std::error_code sizeEc;
		const uint64 size = fs::file_size(file, sizeEc);
		bool skip = sizeEc || size == 0 || size > ARCHIVE_MAX_BYTES;
#if TARGET_OS_IPHONE
		// opening maps the file and Metal may allocate more on top, keep a safe margin
		if (!skip && (uint64)os_proc_available_memory() < size * 3 + 256ull * 1024 * 1024)
			skip = true;
#endif
		if (skip)
		{
			cemuLog_log(LogType::Force, "Metal binary archive {} skipped (size {} MB, not safe to open), deleting it", _pathToUtf8(file.filename()), size / (1024 * 1024));
			std::error_code rmEc;
			fs::remove(file, rmEc);
			continue;
		}
		NS_STACK_SCOPED MTL::BinaryArchiveDescriptor* desc = MTL::BinaryArchiveDescriptor::alloc()->init();
		desc->setUrl(ToNSURL(_pathToUtf8(file)));
		NS::Error* error = nullptr;
		MTL::BinaryArchive* archive = nullptr;
		try
		{
			archive = m_mtlr->GetDevice()->newBinaryArchive(desc, &error);
		}
		catch (...)
		{
			archive = nullptr;
		}
		if (!archive)
		{
			cemuLog_log(LogType::Force, "Metal binary archive {} could not be loaded ({}), deleting it", _pathToUtf8(file.filename()), error ? error->localizedDescription()->utf8String() : "unknown error");
			std::error_code rmEc;
			fs::remove(file, rmEc);
			continue;
		}
		loaded.push_back(archive);
		if (!sizeEc)
			bytes += size;
		const std::string name = file.filename().string();
		const size_t idxPos = keyPrefix.size();
		const size_t idx = (size_t)strtoul(name.c_str() + idxPos, nullptr, 10);
		if (idx < used.size())
			used[idx] = true;
	}
	if (!loaded.empty())
	{
		m_binaryArchives = NS::Array::alloc()->init(loaded.data(), loaded.size());
		for (NS::Object* o : loaded)
			o->release();
	}
	{
		std::error_code mEc;
		fs::remove(markerPath, mEc);
	}
	m_archiveFilesLoaded = (uint32)loaded.size();
	m_archiveBaseBytes = bytes;
	cemuLog_log(LogType::Force, "Metal binary archive: opened {} file(s), {} MB for this title; removed {} file(s), {} MB (another OS build or GPU, not used for {} days, or over the size caps)",
		loaded.size(), bytes / (1024 * 1024), prunedFiles, prunedBytes / (1024 * 1024), ARCHIVE_MAX_AGE_DAYS);

	uint32 nextIndex = 0;
	while (nextIndex < ARCHIVE_MAX_FILES && used[nextIndex])
		nextIndex++;
	m_archiveWriteEnabled = nextIndex < ARCHIVE_MAX_FILES && bytes < ARCHIVE_MAX_BYTES;
	if (m_archiveWriteEnabled)
		m_archiveWritePath = _pathToUtf8(dir / fmt::format("{}{}.bin", keyPrefix, nextIndex));
	else
		cemuLog_log(LogType::Force, "Metal binary archive: not adding more pipelines ({} file(s), {} MB)", loaded.size(), bytes / (1024 * 1024));
}

bool MetalPipelineCache::FlushBinaryArchive(uint32 timeoutMs)
{
	std::unique_lock<std::mutex> lock(m_archiveMutex);
	if (!m_archiveThreadAlive || m_archiveStop)
		return true;
	const uint64_t generation = ++m_archiveFlushGen;
	m_archiveFlushRequested = true;
	m_archiveCv.notify_all();
	return m_archiveFlushCv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return m_archiveFlushDoneGen >= generation || !m_archiveThreadAlive; });
}

void MetalPipelineCache::CloseBinaryArchives()
{
	FlushBinaryArchive(3000);
	if (m_archiveThread)
	{
		s_archiveAbandon.store(true);
		{
			std::lock_guard<std::mutex> lock(m_archiveMutex);
			m_archiveStop = true;
		}
		m_archiveCv.notify_all();
		m_archiveThread->join();
		delete m_archiveThread;
		m_archiveThread = nullptr;
		s_archiveAbandon.store(false);
		std::lock_guard<std::mutex> lock(m_archiveMutex);
		m_archiveThreadAlive = false;
		m_archiveFlushRequested = false;
		m_archiveFlushCv.notify_all();
	}
	{
		std::lock_guard<std::mutex> lock(m_archiveMutex);
		for (auto* desc : m_archiveQueue)
			desc->release();
		m_archiveQueue.clear();
		m_archiveStop = false;
	}
	m_archiveWriteEnabled = false;
	m_archiveFilesLoaded = 0;
	if (m_binaryArchives)
	{
		m_binaryArchives->release();
		m_binaryArchives = nullptr;
	}
}

static const char* GetArchiveSkipReason(MTL::RenderPipelineDescriptor* desc);

void MetalPipelineCache::QueueArchiveAdd(MTL::RenderPipelineDescriptor* desc)
{
	if (!m_archiveWriteEnabled)
		return;
	if (const char* reason = GetArchiveSkipReason(desc))
	{
		static std::atomic<uint32> s_skipped{0};
		if (s_skipped.fetch_add(1) == 0)
			cemuLog_log(LogType::Force, "Metal binary archive: pipelines with {} are not archived (they would make saving fail)", reason);
		return;
	}
	std::lock_guard<std::mutex> lock(m_archiveMutex);
	if (m_archiveStop || m_archiveQueue.size() >= ARCHIVE_MAX_QUEUE)
		return;
	MTL::RenderPipelineDescriptor* copy = desc->copy();
	copy->setBinaryArchives(nullptr);
	m_archiveQueue.push_back(copy);
	if (!m_archiveThread)
	{
		m_archiveThread = new std::thread(&MetalPipelineCache::BinaryArchiveWriterThread, this);
		m_archiveThreadAlive = true;
	}
	m_archiveCv.notify_one();
}

static const char* GetArchiveSkipReason(MTL::RenderPipelineDescriptor* desc)
{
	if (desc->fragmentFunction() == nullptr)
		return "no fragment function";
	if (!desc->rasterizationEnabled())
		return "rasterization disabled";
	return nullptr;
}

static MTL::BinaryArchive* BuildArchiveRange(MTL::Device* device, const std::vector<MTL::RenderPipelineDescriptor*>& v, size_t b, size_t e)
{
	NS_STACK_SCOPED MTL::BinaryArchiveDescriptor* archiveDesc = MTL::BinaryArchiveDescriptor::alloc()->init();
	NS::Error* error = nullptr;
	MTL::BinaryArchive* archive = device->newBinaryArchive(archiveDesc, &error);
	if (!archive)
		return nullptr;
	for (size_t i = b; i < e; i++)
	{
		NS::Error* addError = nullptr;
		archive->addRenderPipelineFunctions(v[i], &addError);
	}
	return archive;
}

static void FindUnserializableDescriptors(MTL::Device* device, const std::vector<MTL::RenderPipelineDescriptor*>& v, size_t b, size_t e, const std::string& scratchPath, std::vector<size_t>& bad)
{
	if (b >= e || s_archiveAbandon.load())
		return;
	NS_STACK_SCOPED NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
	MTL::BinaryArchive* archive = BuildArchiveRange(device, v, b, e);
	bool ok = false;
	if (archive)
	{
		NS::Error* error = nullptr;
		ok = archive->serializeToURL(ToNSURL(scratchPath), &error);
		archive->release();
		std::error_code ec;
		fs::remove(fs::path(scratchPath), ec);
	}
	if (ok)
		return;
	if (e - b == 1)
	{
		bad.push_back(b);
		return;
	}
	const size_t mid = b + (e - b) / 2;
	FindUnserializableDescriptors(device, v, b, mid, scratchPath, bad);
	FindUnserializableDescriptors(device, v, mid, e, scratchPath, bad);
}

void MetalPipelineCache::BinaryArchiveWriterThread()
{
	SetThreadName("mtlArchive");
	MTL::BinaryArchive* archive = nullptr;
	uint32 pending = 0;
	uint32 added = 0;
	std::vector<MTL::RenderPipelineDescriptor*> kept;
	auto lastSave = std::chrono::steady_clock::now();
	for (;;)
	{
		MTL::RenderPipelineDescriptor* desc = nullptr;
		bool stopping;
		bool flushing;
		uint64_t flushGeneration;
		bool queueEmpty;
		{
			std::unique_lock<std::mutex> lock(m_archiveMutex);
			if (m_archiveQueue.empty() && !m_archiveStop && !m_archiveFlushRequested)
			{
				if (pending == 0)
					m_archiveCv.wait(lock, [&] { return !m_archiveQueue.empty() || m_archiveStop || m_archiveFlushRequested; });
				else
					m_archiveCv.wait_for(lock, std::chrono::seconds(20), [&] { return !m_archiveQueue.empty() || m_archiveStop || m_archiveFlushRequested; });
			}
			stopping = m_archiveStop;
			flushing = m_archiveFlushRequested;
			flushGeneration = m_archiveFlushGen;
			if (stopping)
			{
				for (auto* d : m_archiveQueue)
					d->release();
				m_archiveQueue.clear();
			}
			else if (!m_archiveQueue.empty())
			{
				desc = m_archiveQueue.front();
				m_archiveQueue.pop_front();
			}
			queueEmpty = m_archiveQueue.empty();
		}
		NS_STACK_SCOPED NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
		if (desc)
		{
			if (!archive)
			{
				NS_STACK_SCOPED MTL::BinaryArchiveDescriptor* archiveDesc = MTL::BinaryArchiveDescriptor::alloc()->init();
				NS::Error* error = nullptr;
				archive = m_mtlr->GetDevice()->newBinaryArchive(archiveDesc, &error);
				if (!archive)
					cemuLog_log(LogType::Force, "Metal binary archive could not be created: {}", error ? error->localizedDescription()->utf8String() : "unknown error");
			}
			if (archive)
			{
				NS::Error* error = nullptr;
				if (archive->addRenderPipelineFunctions(desc, &error))
				{
					pending++;
					added++;
					desc->retain();
					try { kept.push_back(desc); } catch (const std::bad_alloc&) { desc->release(); }
				}
			}
			desc->release();
		}
		if (archive && pending > 0 && !stopping && !s_archiveAbandon.load() && (pending >= ARCHIVE_SAVE_EVERY || (flushing && queueEmpty) || (queueEmpty && std::chrono::steady_clock::now() - lastSave >= std::chrono::seconds(20))))
		{
			const auto start = std::chrono::steady_clock::now();
			const std::string tmpPath = m_archiveWritePath + ".tmp";
			NS::Error* error = nullptr;
			bool saved = archive->serializeToURL(ToNSURL(tmpPath), &error);
			std::error_code ec;
			if (saved)
			{
				fs::rename(fs::path(m_archiveWritePath + ".tmp"), fs::path(m_archiveWritePath), ec);
				saved = !ec;
			}
			if (saved)
			{
				const uint64 size = fs::file_size(fs::path(m_archiveWritePath), ec);
				cemuLog_log(LogType::Force, "Metal binary archive: saved {} pipelines, {} MB, in {} ms", added, ec ? 0 : size / (1024 * 1024), std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
				if (!ec && m_archiveBaseBytes + size >= ARCHIVE_MAX_BYTES)
				{
					std::lock_guard<std::mutex> lock(m_archiveMutex);
					m_archiveWriteEnabled = false;
				}
			}
			else
			{
				cemuLog_log(LogType::Force, "Metal binary archive could not be saved: {}", error ? error->localizedDescription()->utf8String() : "rename failed");
				fs::remove(fs::path(tmpPath), ec);
				if (error && !kept.empty())
				{
					std::vector<size_t> bad;
					const size_t chunk = 128;
					for (size_t b = 0; b < kept.size(); b += chunk)
						FindUnserializableDescriptors(m_mtlr->GetDevice(), kept, b, std::min(kept.size(), b + chunk), tmpPath + ".probe", bad);
					std::sort(bad.begin(), bad.end());
					for (size_t k = 0; k < bad.size(); k++)
					{
						MTL::RenderPipelineDescriptor* d = kept[bad[k]];
						const char* reason = GetArchiveSkipReason(d);
						cemuLog_log(LogType::Force, "Metal binary archive: skipped a pipeline the archive cannot serialize ({})", reason ? reason : "unknown reason");
					}
					for (size_t k = bad.size(); k-- > 0;)
					{
						kept[bad[k]]->release();
						kept.erase(kept.begin() + bad[k]);
					}
					if (archive)
						archive->release();
					archive = BuildArchiveRange(m_mtlr->GetDevice(), kept, 0, kept.size());
					added = (uint32)kept.size();
					if (archive && !kept.empty())
					{
						NS::Error* retryError = nullptr;
						bool retried = archive->serializeToURL(ToNSURL(tmpPath), &retryError);
						if (retried)
						{
							fs::rename(fs::path(tmpPath), fs::path(m_archiveWritePath), ec);
							retried = !ec;
						}
						if (retried)
						{
							const uint64 size = fs::file_size(fs::path(m_archiveWritePath), ec);
							cemuLog_log(LogType::Force, "Metal binary archive: saved {} pipelines after skipping {}, {} MB", added, bad.size(), ec ? 0 : size / (1024 * 1024));
							if (!ec && m_archiveBaseBytes + size >= ARCHIVE_MAX_BYTES)
							{
								std::lock_guard<std::mutex> lock(m_archiveMutex);
								m_archiveWriteEnabled = false;
							}
						}
						else
						{
							cemuLog_log(LogType::Force, "Metal binary archive could not be saved after skipping {} pipelines: {}", bad.size(), retryError ? retryError->localizedDescription()->utf8String() : "rename failed");
							fs::remove(fs::path(tmpPath), ec);
						}
					}
				}
			}
			pending = 0;
			lastSave = std::chrono::steady_clock::now();
		}
		if (flushing && queueEmpty)
		{
			std::lock_guard<std::mutex> lock(m_archiveMutex);
			if (m_archiveQueue.empty())
			{
				m_archiveFlushDoneGen = flushGeneration;
				m_archiveFlushRequested = m_archiveFlushGen != flushGeneration;
				m_archiveFlushCv.notify_all();
			}
		}
		if (stopping)
			break;
	}
	for (auto* d : kept)
		d->release();
	if (archive)
		archive->release();
}

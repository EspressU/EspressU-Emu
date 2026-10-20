#include "Cafe/HW/Latte/Renderer/Metal/RendererShaderMtl.h"
#include "Cafe/HW/Latte/Renderer/Metal/MetalRenderer.h"
#include "Cafe/HW/Latte/Renderer/Metal/MetalCommon.h"

#include "Cemu/Logging/CemuLogging.h"
#include "Common/precompiled.h"
#include "GameProfile/GameProfile.h"
#include "util/helpers/helpers.h"
#include "Cafe/HW/Latte/Core/PerfTelemetry.h"

#include <chrono>
#include <unordered_set>

#if BOOST_OS_MACOS
#define METAL_AIR_CACHE_NAME "Cemu_AIR_cache"
#define METAL_AIR_CACHE_PATH "/Volumes/" METAL_AIR_CACHE_NAME
#define METAL_AIR_CACHE_SIZE (16 * 1024 * 1024)
#define METAL_AIR_CACHE_BLOCK_COUNT (METAL_AIR_CACHE_SIZE / 512)
#endif

static bool s_isLoadingShadersMtl{false};
static std::chrono::steady_clock::time_point s_shaderLoadStart;
static uint32 s_shaderLoadCompilesAtStart;
static uint64 s_shaderLoadCompileNsAtStart;
static std::mutex s_shaderLoadSourceMutex;
static std::unordered_set<size_t> s_shaderLoadSourceHashes;
static uint32 s_shaderLoadSourceDuplicates;
static uint32 s_shaderLoadExtraThreads;
static std::atomic<uint32> s_shaderLoadDeferred{0};

extern std::atomic_int g_compiled_shaders_total;
extern std::atomic_int g_compiled_shaders_async;

class ShaderMtlThreadPool
{
public:
	void StartThreads()
	{
		if (m_threadsActive.exchange(true))
			return;

		const uint32 threadCount = 2;
		for (uint32 i = 0; i < threadCount; ++i)
			s_threads.emplace_back(&ShaderMtlThreadPool::CompilerThreadFunc, this);

	}

	void StartExtraThreads(uint32 count)
	{
		if (count == 0 || m_extraActive.exchange(true))
			return;
		for (uint32 i = 0; i < count; ++i)
			m_extraThreads.emplace_back(&ShaderMtlThreadPool::ExtraThreadFunc, this);
	}
	void StopExtraThreads()
	{
		if (!m_extraActive.exchange(false))
			return;
		for (auto& it : m_extraThreads)
			it.join();
		m_extraThreads.clear();
	}
	void ExtraThreadFunc()
	{
		SetThreadName("mtlShaderLoad");
		while (m_extraActive.load(std::memory_order::relaxed))
		{
			RendererShaderMtl* job = nullptr;
			{
				std::lock_guard<std::mutex> lock(s_compilationQueueMutex);
				if (!s_compilationQueue.empty())
				{
					job = s_compilationQueue.front();
					s_compilationQueue.pop_front();
					job->m_compilationState.setValue(RendererShaderMtl::COMPILATION_STATE::COMPILING);
				}
			}
			if (!job)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
				continue;
			}
			job->CompileInternal();
			if (job->ShouldCountCompilation())
				++g_compiled_shaders_async;
			job->m_compilationState.setValue(RendererShaderMtl::COMPILATION_STATE::DONE);
		}
	}
	void StopThreads()
	{
		StopExtraThreads();
		if (!m_threadsActive.exchange(false))
			return;
		for (uint32 i = 0; i < s_threads.size(); ++i)
			s_compilationQueueCount.increment();
		for (auto& it : s_threads)
			it.join();
		s_threads.clear();
		s_compilationQueueCount.reset();
	}

	~ShaderMtlThreadPool()
	{
		StopThreads();
	}

	void CompilerThreadFunc()
	{
		SetThreadName("mtlShaderComp");
		while (m_threadsActive.load(std::memory_order::relaxed))
		{
			s_compilationQueueCount.decrementWithWait();
			s_compilationQueueMutex.lock();
			if (s_compilationQueue.empty())
			{
				s_compilationQueueMutex.unlock();
				continue;
			}
			RendererShaderMtl* job = s_compilationQueue.front();
			s_compilationQueue.pop_front();
			cemu_assert_debug(job->m_compilationState.getValue() == RendererShaderMtl::COMPILATION_STATE::QUEUED);
			job->m_compilationState.setValue(RendererShaderMtl::COMPILATION_STATE::COMPILING);
			s_compilationQueueMutex.unlock();
			job->CompileInternal();
			if (job->ShouldCountCompilation())
			    ++g_compiled_shaders_async;
			cemu_assert_debug(job->m_compilationState.getValue() == RendererShaderMtl::COMPILATION_STATE::COMPILING);
			job->m_compilationState.setValue(RendererShaderMtl::COMPILATION_STATE::DONE);
		}
	}

	bool HasThreadsRunning() const { return m_threadsActive; }

public:
	std::vector<std::thread> s_threads;
	std::vector<std::thread> m_extraThreads;
	std::atomic<bool> m_extraActive{false};
	std::deque<RendererShaderMtl*> s_compilationQueue;
	CounterSemaphore s_compilationQueueCount;
	std::mutex s_compilationQueueMutex;

private:
	std::atomic<bool> m_threadsActive;
} shaderMtlThreadPool;

void RendererShaderMtl::ShaderCacheLoading_begin(uint64 cacheTitleId)
{
    s_isLoadingShadersMtl = true;

    static_cast<MetalRenderer*>(g_renderer.get())->SetShouldMaximizeConcurrentCompilation(true);
    {
        std::lock_guard<std::mutex> lock(s_shaderLoadSourceMutex);
        s_shaderLoadSourceHashes.clear();
        s_shaderLoadSourceDuplicates = 0;
    }
    s_shaderLoadDeferred = 0;
    s_shaderLoadStart = std::chrono::steady_clock::now();
    s_shaderLoadCompilesAtStart = PerfTelemetry::Get().shaderCompiles.load(std::memory_order_relaxed);
    s_shaderLoadCompileNsAtStart = PerfTelemetry::Get().shaderCompileNs.load(std::memory_order_relaxed);
    const uint32 targetThreads = std::clamp(GetPhysicalCoreCount(), 2u, 6u);
    s_shaderLoadExtraThreads = targetThreads - 2;
    shaderMtlThreadPool.StartExtraThreads(s_shaderLoadExtraThreads);
}

void RendererShaderMtl::ShaderCacheLoading_end()
{
    if (s_isLoadingShadersMtl)
    {
        const auto wallMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - s_shaderLoadStart).count();
        const uint32 compiles = PerfTelemetry::Get().shaderCompiles.load(std::memory_order_relaxed) - s_shaderLoadCompilesAtStart;
        const uint64 compileMs = (PerfTelemetry::Get().shaderCompileNs.load(std::memory_order_relaxed) - s_shaderLoadCompileNsAtStart) / 1000000;
        uint32 unique, duplicates;
        {
            std::lock_guard<std::mutex> lock(s_shaderLoadSourceMutex);
            unique = (uint32)s_shaderLoadSourceHashes.size();
            duplicates = s_shaderLoadSourceDuplicates;
            s_shaderLoadSourceHashes.clear();
        }
        cemuLog_log(LogType::Force, "Metal shader load: {} shaders read in {} ms wall, {} of them left for the pipelines to compile on demand, {} compiled now ({} ms summed, {} ms avg, {} compile threads), {} distinct sources, {} repeated sources", unique + duplicates, wallMs, s_shaderLoadDeferred.load(), compiles, compileMs, compiles ? compileMs / compiles : 0, 2 + s_shaderLoadExtraThreads, unique, duplicates);
    }
    shaderMtlThreadPool.StopExtraThreads();
    s_isLoadingShadersMtl = false;
    static_cast<MetalRenderer*>(g_renderer.get())->SetShouldMaximizeConcurrentCompilation(false);
}

void RendererShaderMtl::ShaderCacheLoading_Close()
{
}

void RendererShaderMtl::Initialize()
{
    shaderMtlThreadPool.StartThreads();
}

void RendererShaderMtl::Shutdown()
{
    shaderMtlThreadPool.StopExtraThreads();
    shaderMtlThreadPool.StopThreads();
    s_isLoadingShadersMtl = false;
}
RendererShaderMtl::RendererShaderMtl(MetalRenderer* mtlRenderer, ShaderType type, uint64 baseHash, uint64 auxHash, bool isGameShader, bool isGfxPackShader, const std::string& mslCode)
	: RendererShader(type, baseHash, auxHash, isGameShader, isGfxPackShader), m_mtlr{mtlRenderer}, m_mslCode{mslCode}
{
	if (s_isLoadingShadersMtl && isGameShader)
	{
		const size_t sourceHash = std::hash<std::string>{}(m_mslCode);
		std::lock_guard<std::mutex> lock(s_shaderLoadSourceMutex);
		if (!s_shaderLoadSourceHashes.insert(sourceHash).second)
			s_shaderLoadSourceDuplicates++;
	}
	if (s_isLoadingShadersMtl && isGameShader)
	{
		m_compilationState.setValue(COMPILATION_STATE::DEFERRED);
		s_shaderLoadDeferred.fetch_add(1, std::memory_order_relaxed);
		return;
	}
	shaderMtlThreadPool.s_compilationQueueMutex.lock();
	m_compilationState.setValue(COMPILATION_STATE::QUEUED);
	shaderMtlThreadPool.s_compilationQueue.push_back(this);
	shaderMtlThreadPool.s_compilationQueueCount.increment();
	shaderMtlThreadPool.s_compilationQueueMutex.unlock();
	cemu_assert_debug(shaderMtlThreadPool.HasThreadsRunning());
}

RendererShaderMtl::~RendererShaderMtl()
{
	{
		std::lock_guard<std::mutex> lock(shaderMtlThreadPool.s_compilationQueueMutex);
		auto& queue = shaderMtlThreadPool.s_compilationQueue;
		queue.erase(std::remove(queue.begin(), queue.end(), this), queue.end());
	}
	if (m_argumentEncoder)
		m_argumentEncoder->release();
    if (m_function)
        m_function->release();
}

void RendererShaderMtl::PreponeCompilation(bool isRenderThread)
{
	shaderMtlThreadPool.s_compilationQueueMutex.lock();
	bool isStillQueued = m_compilationState.hasState(COMPILATION_STATE::QUEUED);
	const bool isDeferred = m_compilationState.hasState(COMPILATION_STATE::DEFERRED);
	if (isStillQueued)
	{
		shaderMtlThreadPool.s_compilationQueue.erase(std::remove(shaderMtlThreadPool.s_compilationQueue.begin(), shaderMtlThreadPool.s_compilationQueue.end(), this), shaderMtlThreadPool.s_compilationQueue.end());
		m_compilationState.setValue(COMPILATION_STATE::COMPILING);
	}
	else if (isDeferred)
	{
		m_compilationState.setValue(COMPILATION_STATE::COMPILING);
		isStillQueued = true;
	}
	shaderMtlThreadPool.s_compilationQueueMutex.unlock();
	if (!isStillQueued)
	{
		m_compilationState.waitUntilValue(COMPILATION_STATE::DONE);
		if (ShouldCountCompilation())
		    --g_compiled_shaders_async;
		return;
	}
	else
	{
		CompileInternal();
		m_compilationState.setValue(COMPILATION_STATE::DONE);
	}
}

bool RendererShaderMtl::IsCompiled()
{
	return m_compilationState.hasState(COMPILATION_STATE::DONE);
};

bool RendererShaderMtl::WaitForCompiled()
{
	if (m_compilationState.hasState(COMPILATION_STATE::DEFERRED))
		return true;
	m_compilationState.waitUntilValue(COMPILATION_STATE::DONE);
	return m_function != nullptr;
}

bool RendererShaderMtl::ShouldCountCompilation() const
{
    return !s_isLoadingShadersMtl && m_isGameShader;
}

MTL::Library* RendererShaderMtl::LibraryFromSource()
{
    NS_STACK_SCOPED MTL::CompileOptions* options = MTL::CompileOptions::alloc()->init();
    if (g_current_game_profile->GetShaderFastMath())
        options->setFastMathEnabled(true);

    if (m_mtlr->GetPositionInvariance())
    {
        options->setPreserveInvariance(true);
    }

    NS::Error* error = nullptr;
	MTL::Library* library = m_mtlr->GetDevice()->newLibrary(ToNSString(m_mslCode), options, &error);
	if (!library)
    {
        cemuLog_log(LogType::Force, "failed to create library from source: {} -> {}", error ? error->localizedDescription()->utf8String() : "unknown error", m_mslCode.c_str());
        return nullptr;
    }

    return library;
}

void RendererShaderMtl::CompileInternal()
{
	PerfTelemetry::Get().shaderCompiles.fetch_add(1, std::memory_order_relaxed);
	PerfTelemetry::ScopedTimer compileTimer(PerfTelemetry::Get().shaderCompileNs);
	NS_STACK_SCOPED NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
    MTL::Library* library = nullptr;

    if (!library)
    {
        library = LibraryFromSource();
        FinishCompilation();
        if (!library)
            return;

    }

    m_function = library->newFunction(ToNSString("main0"));
    library->release();

	if (m_function && m_isGameShader)
	{
		m_argumentEncoder = m_function->newArgumentEncoder(MetalArgumentBuffer::BindingIndex);
		if (!m_argumentEncoder)
		{
			cemuLog_log(LogType::Force, "failed to create Metal argument encoder for shader {:016x}", m_baseHash);
			m_function->release();
			m_function = nullptr;
			return;
		}
	}

	if (ShouldCountCompilation())
	    g_compiled_shaders_total++;
}

void RendererShaderMtl::FinishCompilation()
{
    m_mslCode.clear();
    m_mslCode.shrink_to_fit();
}

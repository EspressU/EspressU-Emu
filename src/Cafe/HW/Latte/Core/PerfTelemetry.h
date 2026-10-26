
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <type_traits>

namespace PerfTelemetry
{
	struct Counters
	{
		std::atomic<uint64_t> ppcIdleNs{0};
		std::atomic<uint32_t> ppcHostThreads{1};

		std::atomic<uint64_t> gpuIdleNs{0};
		std::atomic<uint64_t> gpuSyncNs{0};
		std::atomic<uint64_t> drawableWaitNs{0};

		std::atomic<uint64_t> mtlGpuNs{0};
		std::atomic<uint32_t> mtlCommandBuffers{0};
		std::atomic<uint32_t> tvPresents{0};
		std::atomic<uint32_t> padPresents{0};
		std::atomic<uint32_t> tvAndPadSameFramePresents{0};
		std::atomic<uint32_t> padPresentsHeldBack{0};

		std::atomic<uint32_t> shaderCompiles{0};
		std::atomic<uint64_t> shaderCompileNs{0};
		std::atomic<uint32_t> pipelineCompiles{0};
		std::atomic<uint64_t> pipelineCompileNs{0};
		std::atomic<uint32_t> pipelineSyncCompiles{0};

		std::atomic<uint32_t> textureDecodes{0};
		std::atomic<uint64_t> textureDecodeNs{0};
		std::atomic<uint32_t> textureDecodesByFormat[64]{};
		std::atomic<uint64_t> textureDecodeNsByFormat[64]{};

		std::atomic<uint32_t> renderPassBreaks[16]{};
		std::atomic<uint32_t> renderPassesStarted{0};
		std::atomic<uint32_t> imageCopyNotTouchingPass{0};
		std::atomic<uint32_t> passReturnsToPrevious{0};
		std::atomic<uint64_t> guestWaitTimestampNs{0};
		std::atomic<uint64_t> guestWaitVsyncNs{0};
		std::atomic<uint64_t> guestWaitFlipNs{0};
		std::atomic<uint32_t> coreWakeups{0};
		std::atomic<uint64_t> coreWakeLatencyNs{0};
		std::atomic<uint64_t> coreWakeLatencyMaxNs{0};
		std::atomic<uint32_t> textureTranscodes{0};
		std::atomic<uint64_t> textureTranscodeNs{0};
		std::atomic<uint32_t> textureTranscodeCacheHits{0};
		std::atomic<uint32_t> textureTranscodeDirect{0};

		std::atomic<uint32_t> gpuDraws{0};
		std::atomic<uint64_t> gpuBeginNs{0};
		std::atomic<uint64_t> gpuTexUpdateNs{0};
		std::atomic<uint64_t> gpuTexHashNs{0};
		std::atomic<uint32_t> gpuTexHashes{0};
		std::atomic<uint64_t> gpuTexHashBytes{0};
		std::atomic<uint64_t> gpuExecNs{0};
		std::atomic<uint64_t> gpuIndexNs{0};
		std::atomic<uint64_t> gpuFlushNs{0};
		std::atomic<uint64_t> gpuBufferNs{0};
		std::atomic<uint64_t> gpuSnapshotCompareBytes{0};
		std::atomic<uint64_t> gpuSnapshotCopyBytes{0};
		std::atomic<uint64_t> gpuPassNs{0};
		std::atomic<uint64_t> gpuPipelineNs{0};
		std::atomic<uint64_t> gpuBindNs{0};
		std::atomic<uint32_t> gpuUseResourceCalls{0};
		std::atomic<uint32_t> gpuUseResourceSkips{0};
		std::atomic<uint64_t> gpuUseResourceNs{0};
		std::atomic<uint32_t> gpuArgEncodes{0};
		std::atomic<uint64_t> gpuArgEncodeNs{0};
		std::atomic<uint32_t> gpuArgReuses{0};
		std::atomic<uint64_t> gpuArgReuseNs{0};
		std::atomic<uint32_t> gpuArgMissNoBase{0};
		std::atomic<uint32_t> gpuArgMissSupport{0};
		std::atomic<uint32_t> gpuArgMissSupportOnly{0};
		std::atomic<uint32_t> gpuArgMissUniform{0};
		std::atomic<uint32_t> gpuArgMissTexture{0};
		std::atomic<uint32_t> gpuArgMissSampler{0};
		std::atomic<uint32_t> gpuArgMissVertexIndex{0};
		std::atomic<uint32_t> gpuArgMissConstant{0};
		std::atomic<uint32_t> gpuArgDeltaEncodes{0};
		std::atomic<uint64_t> gpuArgDeltaSlots{0};
		std::atomic<uint64_t> gpuArgEncAllocNs{0};
		std::atomic<uint64_t> gpuArgEncCopyNs{0};
		std::atomic<uint64_t> gpuArgEncSetNs{0};
		std::atomic<uint64_t> gpuArgEncFlushNs{0};
		std::atomic<uint64_t> gpuBindTextureLoopNs{0};
		std::atomic<uint64_t> gpuBindResidencyNs{0};
		std::atomic<uint64_t> gpuBindSupportPrepNs{0};
		std::atomic<uint64_t> gpuBindRemappedNs{0};
		std::atomic<uint64_t> gpuBindSnapshotNs{0};
		std::atomic<uint64_t> gpuBindUniformLoopNs{0};
		std::atomic<uint64_t> gpuBindTrackNs{0};
		std::atomic<uint64_t> gpuBindCrumbNs{0};
		std::atomic<uint64_t> gpuBindVertexNs{0};
		std::atomic<uint64_t> gpuBindFinalNs{0};
		std::atomic<uint32_t> gpuSamplerLookups{0};
		std::atomic<uint64_t> gpuSamplerLookupNs{0};
		std::atomic<uint32_t> gpuTexViewLookups{0};
		std::atomic<uint64_t> gpuTexViewLookupNs{0};
		std::atomic<uint64_t> gpuEndNs{0};

		std::atomic<uint32_t> jitBlocks{0};
		std::atomic<uint64_t> jitCompileNs{0};
		std::atomic<uint32_t> jitInvalidations{0};
		std::atomic<uint32_t> jitArenaReleases{0};
		std::atomic<uint32_t> jitArenaAllocFails{0};
	};

	enum RenderPassBreak : int
	{
		kBreakFboChange = 0,
		kBreakTextureUpload = 1,
		kBreakBufferUpload = 2,
		kBreakBlitOther = 3,
		kBreakCompute = 4,
		kBreakTemporaryPass = 5,
		kBreakForced = 6,
		kBreakCommit = 7,
		kBreakImageCopy = 8,
		kBreakUniformResize = 9,
		kBreakBufferCopy = 10,
		kBreakCacheCopy = 11,
		kBreakStreamout = 12,
		kBreakReadback = 13,
		kBreakTranscodeCopy = 14,
	};

	inline Counters& Get()
	{
		static Counters s_counters;
		return s_counters;
	}

	struct Summary
	{
		std::atomic<bool> valid{false};
		std::atomic<float> hostFps{0};
		std::atomic<float> guestFps{0};
		std::atomic<float> vsyncRate{0};
		std::atomic<float> ppcExecPct{0};
		std::atomic<float> gpuThreadBusyPct{0};
		std::atomic<float> mtlGpuMsPerFrame{0};
		std::atomic<float> mtlGpuBusyPct{0};
		std::atomic<int> bottleneck{0};
	};

	inline Summary& GetSummary()
	{
		static Summary s_summary;
		return s_summary;
	}

	inline std::atomic<bool>& AppInBackground()
	{
		static std::atomic<bool> s_background{false};
		return s_background;
	}

	inline std::atomic<bool>& DrawBreadcrumbsEnabled()
	{
		static std::atomic<bool> s_enabled{true};
		return s_enabled;
	}

	inline const char* BottleneckName(int code)
	{
		switch (code)
		{
		case 1: return "at frame cap";
		case 2: return "GPU";
		case 3: return "CPU (PPC)";
		case 4: return "CPU (GPU thread)";
		case 5: return "sync";
		case 6: return "compiling";
		case 7: return "no frames";
		default: return "unknown";
		}
	}

	inline uint64_t NowNs()
	{
		return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	inline constexpr uint32_t kSampleEvery = 8;
	inline bool g_sampleFrame = false;
	inline bool IsSampling() { return g_sampleFrame; }
	inline void BeginFrameSampling()
	{
		static uint32_t s_frame = 0;
		g_sampleFrame = (s_frame++ % kSampleEvery) == 0;
	}
	inline uint64_t SampleStart() { return g_sampleFrame ? NowNs() : 0; }
	inline void SampleEnd(std::atomic<uint64_t>& sink, uint64_t start)
	{
		if (start)
			sink.fetch_add((NowNs() - start) * kSampleEvery, std::memory_order_relaxed);
	}
	template <typename T>
	inline void SampleCount(std::atomic<T>& sink, typename std::common_type<T>::type n = 1)
	{
		if (g_sampleFrame)
			sink.fetch_add(n * kSampleEvery, std::memory_order_relaxed);
	}
	class SampledTimer
	{
	public:
		explicit SampledTimer(std::atomic<uint64_t>& sink) : m_sink(sink), m_start(SampleStart()) {}
		~SampledTimer() { SampleEnd(m_sink, m_start); }
		SampledTimer(const SampledTimer&) = delete;
		SampledTimer& operator=(const SampledTimer&) = delete;
	private:
		std::atomic<uint64_t>& m_sink;
		uint64_t m_start;
	};

	class ScopedTimer
	{
	public:
		explicit ScopedTimer(std::atomic<uint64_t>& sink) : m_sink(sink), m_start(NowNs()) {}
		~ScopedTimer() { m_sink.fetch_add(NowNs() - m_start, std::memory_order_relaxed); }
		ScopedTimer(const ScopedTimer&) = delete;
		ScopedTimer& operator=(const ScopedTimer&) = delete;
	private:
		std::atomic<uint64_t>& m_sink;
		uint64_t m_start;
	};
}

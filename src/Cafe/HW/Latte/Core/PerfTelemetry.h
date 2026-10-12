#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

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

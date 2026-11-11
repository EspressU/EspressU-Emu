// Copyright (c) 2026 EspressU (https://espressu.dev). EspressU Licence: non-commercial use only.
// Anything that contains or is derived from this file must be fully open source, keep this file
// under the EspressU Licence, keep this notice in every copy, and credit EspressU (https://espressu.dev).
// Full text: LICENSES/EspressU-Licence.txt. Not covered by MPL-2.0.

#include "Cafe/HW/Latte/Renderer/Metal/MetalFrameStats.h"
#include "Cafe/HW/Latte/Renderer/Metal/MetalFrameGen.h"
#include "Cafe/HW/Latte/Core/HostTuning.h"
#include "Cafe/HW/Latte/Core/PerfTelemetry.h"
#include "Cemu/Logging/CemuLogging.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <string>

namespace
{
	constexpr double kLongFrameMs = 50.0;
	constexpr double kVeryLongFrameMs = 100.0;
	// A frame this long is a pause, a trip to the home screen or a debugger stop, not a hitch.
	constexpr double kGapMs = 1500.0;
	constexpr double kWindowSeconds = 5.0;
	constexpr double kSummarySeconds = 60.0;
	constexpr uint32 kLongFrameLinesPerWindow = 8;

	// Phase heuristics. In-game loading either streams files fast or shows a screen with few draws.
	constexpr double kLoadingReadMBps = 12.0;
	constexpr double kLoadingDraws = 20.0;
	// Startup ends with the first 3D scene: this many draws a frame for this many frames in a row.
	constexpr uint32 kSceneDraws = 100;
	constexpr uint32 kSceneFrames = 30;

	const char* PhaseName(int phase)
	{
		switch (phase)
		{
		case PerfTelemetry::kPhaseStartup: return "startup";
		case PerfTelemetry::kPhaseLoading: return "loading";
		default: return "gameplay";
		}
	}

	const char* ThermalName(int state)
	{
		switch (state)
		{
		case 0: return "nominal";
		case 1: return "fair";
		case 2: return "serious";
		case 3: return "critical";
		default: return "unknown";
		}
	}

	// Frame times: 0.1 ms steps up to 100 ms, then 1 ms steps up to 2 s.
	class Histogram
	{
	public:
		static constexpr uint32 kFine = 1000;
		static constexpr uint32 kCoarse = 1900;

		void Add(double ms)
		{
			uint32 bucket;
			if (ms < 100.0)
				bucket = (uint32)std::max(0.0, ms * 10.0);
			else
				bucket = kFine + std::min<uint32>(kCoarse - 1, (uint32)(ms - 100.0));
			m_buckets[bucket]++;
			m_count++;
			m_sumMs += ms;
			m_maxMs = std::max(m_maxMs, ms);
			if (ms > kLongFrameMs)
				m_over50++;
			if (ms > kVeryLongFrameMs)
				m_over100++;
		}

		void Reset() { *this = Histogram(); }

		uint64 Count() const { return m_count; }
		double SumMs() const { return m_sumMs; }
		double MaxMs() const { return m_maxMs; }
		uint64 Over50() const { return m_over50; }
		uint64 Over100() const { return m_over100; }

		// Frame time that p percent of frames stay at or under.
		double Percentile(double p) const
		{
			if (!m_count)
				return 0.0;
			const uint64 rank = std::max<uint64>(1, (uint64)std::ceil(m_count * p / 100.0));
			uint64 seen = 0;
			for (uint32 i = 0; i < kFine + kCoarse; i++)
			{
				seen += m_buckets[i];
				if (seen >= rank)
					return std::min(BucketMs(i), m_maxMs);
			}
			return m_maxMs;
		}

		// "1% low": the average frame rate of the slowest 1% of frames.
		double OnePercentLowFps() const
		{
			if (!m_count)
				return 0.0;
			uint64 want = std::max<uint64>(1, m_count / 100);
			double sum = 0.0;
			uint64 taken = 0;
			for (int i = (int)(kFine + kCoarse) - 1; i >= 0 && taken < want; i--)
			{
				const uint64 n = std::min<uint64>(m_buckets[i], want - taken);
				sum += n * std::min(BucketMs((uint32)i), m_maxMs);
				taken += n;
			}
			return sum > 0.0 ? 1000.0 * taken / sum : 0.0;
		}

	private:
		// Upper edge, so percentiles never look better than they were.
		static double BucketMs(uint32 i) { return i < kFine ? (i + 1) / 10.0 : 100.0 + (i - kFine) + 1.0; }

		std::array<uint32, kFine + kCoarse> m_buckets{};
		uint64 m_count = 0;
		double m_sumMs = 0.0;
		double m_maxMs = 0.0;
		uint64 m_over50 = 0;
		uint64 m_over100 = 0;
	};

	// Cumulative counters, read once per frame.
	struct Snapshot
	{
		uint64 ns = 0;
		uint64 decompile = 0, shaderCompile = 0, compileWait = 0, pipeline = 0;
		uint64 textureDecode = 0, textureUpload = 0, readback = 0, gpuSync = 0, drawable = 0, gpuIdle = 0, metalGpu = 0;
		uint64 fileBytes = 0, dedupeWait = 0, throttle = 0, astcEncode = 0;
		uint32 astcDirect = 0, astcEncoded = 0;
		uint64 asyncWorker = 0, asyncEncode = 0, asyncWait = 0;
		uint32 asyncQueued = 0, asyncApplied = 0, asyncPlaceholders = 0, asyncFallbacks = 0, asyncWaits = 0;
		uint32 draws = 0, shaderCompiles = 0, pipelines = 0, waits = 0, readbacks = 0, queued = 0, skipped = 0, dedupes = 0;
		uint32 presents = 0;

		static Snapshot Take(uint64 now)
		{
			auto& c = PerfTelemetry::Get();
			constexpr auto r = std::memory_order_relaxed;
			Snapshot s;
			s.ns = now;
			s.decompile = c.rtShaderDecompileNs.load(r);
			s.shaderCompile = c.rtShaderCompileNs.load(r);
			s.compileWait = c.rtCompileWaitNs.load(r);
			s.pipeline = c.rtPipelineCompileNs.load(r);
			s.textureDecode = c.textureDecodeNs.load(r);
			s.astcEncode = c.textureTranscodeNs.load(r);
			s.astcDirect = c.textureTranscodeDirect.load(r);
			s.astcEncoded = c.textureTranscodes.load(r);
			s.textureUpload = c.rtTextureUploadNs.load(r);
			s.asyncWorker = c.asyncTexWorkerNs.load(r);
			s.asyncEncode = c.asyncTexEncodeNs.load(r);
			s.asyncWait = c.rtAsyncTexWaitNs.load(r);
			s.asyncQueued = c.asyncTexQueued.load(r);
			s.asyncApplied = c.asyncTexApplied.load(r);
			s.asyncPlaceholders = c.asyncTexPlaceholders.load(r);
			s.asyncFallbacks = c.asyncTexFallbacks.load(r);
			s.asyncWaits = c.asyncTexWaits.load(r);
			s.readback = c.rtReadbackWaitNs.load(r);
			s.gpuSync = c.gpuSyncNs.load(r);
			s.drawable = c.drawableWaitNs.load(r);
			s.gpuIdle = c.gpuIdleNs.load(r);
			s.metalGpu = c.mtlGpuNs.load(r);
			s.fileBytes = c.fileReadBytes.load(r);
			s.dedupeWait = c.shaderSourceDedupeWaitNs.load(r);
			s.throttle = c.compileThrottleWaitNs.load(r);
			s.draws = c.gpuDraws.load(r);
			s.shaderCompiles = c.rtShaderCompiles.load(r);
			s.pipelines = c.rtPipelineCompiles.load(r);
			s.waits = c.rtCompileWaits.load(r);
			s.readbacks = c.rtReadbackWaits.load(r);
			s.queued = c.pipelinesQueued.load(r);
			s.skipped = c.pipelinesSkippedDraws.load(r);
			s.dedupes = c.shaderSourceDedupes.load(r);
			s.presents = c.tvPresents.load(r);
			return s;
		}
	};

	double Ms(uint64 a, uint64 b) { return a > b ? (double)(a - b) / 1e6 : 0.0; }

	// Render-thread time of one frame, split by cause.
	struct Breakdown
	{
		double translate = 0, shaders = 0, wait = 0, pipelines = 0, textures = 0, uploads = 0, readback = 0, gpuWait = 0, present = 0, gameCpu = 0, other = 0, metalGpu = 0;

		double CompileTotal() const { return translate + shaders + wait + pipelines; }

		static Breakdown From(const Snapshot& a, const Snapshot& b, double frameMs)
		{
			Breakdown d;
			d.translate = Ms(b.decompile, a.decompile);
			d.shaders = Ms(b.shaderCompile, a.shaderCompile);
			d.wait = Ms(b.compileWait, a.compileWait);
			d.pipelines = Ms(b.pipeline, a.pipeline);
			// Converting on the render thread plus waiting there for background conversions (copies, readbacks).
			d.textures = Ms(b.textureDecode, a.textureDecode) + Ms(b.asyncWait, a.asyncWait);
			d.uploads = Ms(b.textureUpload, a.textureUpload);
			d.readback = Ms(b.readback, a.readback);
			d.gpuWait = std::max(0.0, Ms(b.gpuSync, a.gpuSync) - d.readback);
			d.present = Ms(b.drawable, a.drawable);
			d.gameCpu = Ms(b.gpuIdle, a.gpuIdle);
			d.metalGpu = Ms(b.metalGpu, a.metalGpu);
			d.other = std::max(0.0, frameMs - d.translate - d.shaders - d.wait - d.pipelines - d.textures - d.uploads - d.readback - d.gpuWait - d.present - d.gameCpu);
			return d;
		}

		const char* Cause() const
		{
			struct Part { double ms; const char* name; } parts[] = {
				{ CompileTotal(), "shader/pipeline compile" }, { textures + uploads, "textures" }, { readback, "readback" },
				{ gpuWait, "GPU busy" }, { present, "present" }, { gameCpu, "emulated CPU" }, { other, "other render work" },
			};
			const Part* best = &parts[0];
			for (const auto& p : parts)
				if (p.ms > best->ms)
					best = &p;
			return best->name;
		}
	};

	struct PhaseTotals
	{
		Histogram frames;
		double compileMs = 0, waitMs = 0, readbackMs = 0, seconds = 0;
		uint64 newShaders = 0, syncPipelines = 0, queuedPipelines = 0, skippedDraws = 0;
		int worstThermal = 0;
	};

	struct State
	{
		std::mutex mutex;
		uint64 titleStartNs = 0;
		bool haveLast = false;
		Snapshot last;

		// Phase detection.
		int phase = PerfTelemetry::kPhaseStartup;
		uint32 sceneRun = 0;
		uint64 sceneRunStartNs = 0;
		double drawsAvg = 0.0;
		uint64 readSampleNs = 0, readSampleBytes = 0;
		double readMBps = 0.0;
		double firstGameFrameS = -1.0, firstSceneS = -1.0;

		// 5 second window.
		Histogram window;
		Snapshot windowStart;
		uint32 windowPhaseFrames[3] = {};
		uint32 windowLongLines = 0, windowLongSuppressed = 0;
		uint32 gaps = 0;

		PhaseTotals phases[3];
		uint64 lastSummaryNs = 0;
	};

	State& S()
	{
		static State s;
		return s;
	}

	void ResetForTitle(State& s, uint64 titleStart)
	{
		s.titleStartNs = titleStart;
		s.haveLast = false;
		s.phase = PerfTelemetry::kPhaseStartup;
		s.sceneRun = 0;
		s.drawsAvg = 0.0;
		s.readSampleNs = 0;
		s.readMBps = 0.0;
		s.firstGameFrameS = s.firstSceneS = -1.0;
		s.window.Reset();
		for (auto& n : s.windowPhaseFrames)
			n = 0;
		s.windowLongLines = s.windowLongSuppressed = 0;
		s.gaps = 0;
		for (auto& p : s.phases)
			p = PhaseTotals();
		s.lastSummaryNs = 0;
		PerfTelemetry::Get().framePhase.store(PerfTelemetry::kPhaseStartup, std::memory_order_relaxed);
	}

	std::string SummaryText(State& s, const char* reason)
	{
		std::string text = fmt::format("[Frames] summary ({}) | first game frame {}, first 3D scene {} | thermal now {}",
			reason,
			s.firstGameFrameS < 0 ? std::string("not yet") : fmt::format("{:.1f} s", s.firstGameFrameS),
			s.firstSceneS < 0 ? std::string("not yet") : fmt::format("{:.1f} s", s.firstSceneS),
			ThermalName(PerfTelemetry::Get().thermalState.load(std::memory_order_relaxed)));
		for (int p = 0; p < 3; p++)
		{
			const auto& t = s.phases[p];
			if (!t.frames.Count())
				continue;
			const double avgMs = t.frames.SumMs() / t.frames.Count();
			const double minutes = std::max(1.0 / 60.0, t.seconds / 60.0);
			text += fmt::format("\n[Frames]   {}: {:.0f} s, {} frames, avg {:.1f} fps, 1% low {:.1f} fps | frame time p95 {:.1f} ms, p99 {:.1f} ms, max {:.0f} ms | over 50 ms {}, over 100 ms {} | "
				"render thread compile {:.0f} ms ({:.0f} ms/min, of that waiting for workers {:.0f} ms), new shaders {}, pipelines built on the render thread {}, queued {}, draws skipped while compiling {} | readback waits {:.0f} ms | worst thermal {}",
				PhaseName(p), t.seconds, t.frames.Count(), avgMs > 0 ? 1000.0 / avgMs : 0.0, t.frames.OnePercentLowFps(),
				t.frames.Percentile(95.0), t.frames.Percentile(99.0), t.frames.MaxMs(), t.frames.Over50(), t.frames.Over100(),
				t.compileMs, t.compileMs / minutes, t.waitMs, t.newShaders, t.syncPipelines, t.queuedPipelines, t.skippedDraws, t.readbackMs,
				ThermalName(t.worstThermal));
		}
		return text;
	}

	void LogWindow(State& s, const Snapshot& now)
	{
		const double seconds = (double)(now.ns - s.windowStart.ns) / 1e9;
		const auto& h = s.window;
		int phase = 0;
		for (int p = 1; p < 3; p++)
			if (s.windowPhaseFrames[p] > s.windowPhaseFrames[phase])
				phase = p;
		const double gameFps = h.Count() / std::max(0.001, seconds);
		double displayFps = (double)(uint32)(now.presents - s.windowStart.presents) / std::max(0.001, seconds);
		double generatedFps = 0.0;
		const bool frameGen = HostTuning::Get().frameGeneration.load(std::memory_order_relaxed);
		if (frameGen)
		{
			const auto rates = MetalFrameGen::DisplayRates();
			if (rates.output > 0.0)
			{
				displayFps = rates.output;
				generatedFps = rates.generated;
			}
		}
		const Breakdown d = Breakdown::From(s.windowStart, now, 0.0);
		cemuLog_log(LogType::Force,
			"[Frames] {:.1f} s {} | game {:.1f} fps, display {:.1f} fps{} | frame time avg {:.1f} p95 {:.1f} p99 {:.1f} max {:.0f} ms, 1% low {:.1f} fps | over 50 ms {}, over 100 ms {}{} | "
			"render thread compile {:.0f} ms (translate {:.0f}, shaders {:.0f}, waiting for workers {:.0f}, pipelines {:.0f}), new shaders {}, sync pipelines {}, queued {}, draws skipped {}, same-source shaders reused {} | "
			"textures {:.0f} ms (ASTC encoder {:.0f} ms, BC blocks converted directly {:.0f}% of {}), uploads {:.0f} ms, readback waits {:.0f} ms (x{}) | async textures {} queued, {} ready, {} shown as average colour first, {} loaded directly (queue full), workers {:.0f} ms (ASTC encoder {:.0f}), render thread waited {:.0f} ms (x{}) | throttled background compiles {:.0f} ms | file reads {:.1f} MB/s | thermal {}",
			seconds, PhaseName(phase), gameFps, displayFps,
			frameGen ? fmt::format(" (framegen on, {:.1f} generated)", generatedFps) : std::string(" (framegen off)"),
			h.Count() ? h.SumMs() / h.Count() : 0.0, h.Percentile(95.0), h.Percentile(99.0), h.MaxMs(), h.OnePercentLowFps(), h.Over50(), h.Over100(),
			s.windowLongSuppressed ? fmt::format(" ({} long frames not listed)", s.windowLongSuppressed) : std::string(),
			d.CompileTotal(), d.translate, d.shaders, d.wait, d.pipelines,
			now.shaderCompiles - s.windowStart.shaderCompiles, now.pipelines - s.windowStart.pipelines, now.queued - s.windowStart.queued,
			now.skipped - s.windowStart.skipped, now.dedupes - s.windowStart.dedupes,
			d.textures, Ms(now.astcEncode, s.windowStart.astcEncode),
			100.0 * (now.astcDirect - s.windowStart.astcDirect) / std::max<uint32>(1, (now.astcDirect - s.windowStart.astcDirect) + (now.astcEncoded - s.windowStart.astcEncoded)),
			(now.astcDirect - s.windowStart.astcDirect) + (now.astcEncoded - s.windowStart.astcEncoded),
			d.uploads, d.readback, now.readbacks - s.windowStart.readbacks,
			now.asyncQueued - s.windowStart.asyncQueued, now.asyncApplied - s.windowStart.asyncApplied, now.asyncPlaceholders - s.windowStart.asyncPlaceholders,
			now.asyncFallbacks - s.windowStart.asyncFallbacks, Ms(now.asyncWorker, s.windowStart.asyncWorker), Ms(now.asyncEncode, s.windowStart.asyncEncode),
			Ms(now.asyncWait, s.windowStart.asyncWait), now.asyncWaits - s.windowStart.asyncWaits,
			Ms(now.throttle, s.windowStart.throttle),
			s.readMBps, ThermalName(PerfTelemetry::Get().thermalState.load(std::memory_order_relaxed)));
		s.window.Reset();
		for (auto& n : s.windowPhaseFrames)
			n = 0;
		s.windowLongLines = s.windowLongSuppressed = 0;
		s.windowStart = now;
	}
}

namespace MetalFrameStats
{
	int CurrentPhase()
	{
		return PerfTelemetry::Get().framePhase.load(std::memory_order_relaxed);
	}

	void OnTvFrame()
	{
		auto& c = PerfTelemetry::Get();
		const uint64 titleStart = c.titleStartNs.load(std::memory_order_relaxed);
		if (titleStart == 0)
			return;
		State& s = S();
		std::lock_guard lock(s.mutex);
		const uint64 nowNs = PerfTelemetry::NowNs();
		if (titleStart != s.titleStartNs)
			ResetForTitle(s, titleStart);
		const Snapshot now = Snapshot::Take(nowNs);
		if (!s.haveLast)
		{
			s.last = now;
			s.windowStart = now;
			s.readSampleNs = nowNs;
			s.readSampleBytes = now.fileBytes;
			s.lastSummaryNs = nowNs;
			s.haveLast = true;
			return;
		}
		const Snapshot prev = s.last;
		s.last = now;
		const double frameMs = (double)(nowNs - prev.ns) / 1e6;
		if (frameMs >= kGapMs || c.titlePaused.load(std::memory_order_relaxed) || PerfTelemetry::AppInBackground().load(std::memory_order_relaxed))
		{
			// Do not count paused or backgrounded time; start the window over.
			s.gaps++;
			s.windowStart = now;
			s.window.Reset();
			s.readSampleNs = nowNs;
			s.readSampleBytes = now.fileBytes;
			return;
		}

		// Phase.
		const uint32 draws = now.draws - prev.draws;
		s.drawsAvg = s.drawsAvg * 0.8 + draws * 0.2;
		if (nowNs - s.readSampleNs >= 1000000000ull)
		{
			s.readMBps = (double)(now.fileBytes - s.readSampleBytes) / (1024.0 * 1024.0) / ((double)(nowNs - s.readSampleNs) / 1e9);
			s.readSampleNs = nowNs;
			s.readSampleBytes = now.fileBytes;
		}
		const double sinceStart = (double)(nowNs - titleStart) / 1e9;
		if (draws > 0 && s.firstGameFrameS < 0)
		{
			s.firstGameFrameS = sinceStart;
			cemuLog_log(LogType::Force, "[Frames] first game frame {:.1f} s after the start", sinceStart);
		}
		// The shader cache loading screen runs at a fixed slow rate; it is not a hitch, only time to first frame.
		if (s.firstGameFrameS < 0)
			return;
		if (s.phase == PerfTelemetry::kPhaseStartup)
		{
			if (draws >= kSceneDraws)
			{
				if (s.sceneRun++ == 0)
					s.sceneRunStartNs = prev.ns;
				if (s.sceneRun >= kSceneFrames)
				{
					s.firstSceneS = (double)(s.sceneRunStartNs - titleStart) / 1e9;
					s.phase = PerfTelemetry::kPhaseGameplay;
					cemuLog_log(LogType::Force, "[Frames] first 3D scene {:.1f} s after the start (startup ends, {} draws a frame)", s.firstSceneS, draws);
				}
			}
			else
				s.sceneRun = 0;
		}
		else
		{
			const bool loading = s.readMBps >= kLoadingReadMBps || s.drawsAvg < kLoadingDraws;
			s.phase = loading ? PerfTelemetry::kPhaseLoading : PerfTelemetry::kPhaseGameplay;
		}
		c.framePhase.store(s.phase, std::memory_order_relaxed);

		// Totals.
		const Breakdown d = Breakdown::From(prev, now, frameMs);
		const int thermal = c.thermalState.load(std::memory_order_relaxed);
		auto& t = s.phases[s.phase];
		t.frames.Add(frameMs);
		t.seconds += frameMs / 1000.0;
		t.compileMs += d.CompileTotal();
		t.waitMs += d.wait;
		t.readbackMs += d.readback;
		t.newShaders += now.shaderCompiles - prev.shaderCompiles;
		t.syncPipelines += now.pipelines - prev.pipelines;
		t.queuedPipelines += now.queued - prev.queued;
		t.skippedDraws += now.skipped - prev.skipped;
		t.worstThermal = std::max(t.worstThermal, thermal);
		s.window.Add(frameMs);
		s.windowPhaseFrames[s.phase]++;

		if (frameMs > kLongFrameMs)
		{
			if (s.windowLongLines < kLongFrameLinesPerWindow)
			{
				s.windowLongLines++;
				cemuLog_log(LogType::Force,
					"[Frames] long frame {:.0f} ms ({}, {:.1f} s) -> {} | translate {:.1f}, shader compile {:.1f} (x{}), waiting for workers {:.1f} (x{}), pipeline build {:.1f} (x{}) | "
					"textures {:.1f} (ASTC encoder {:.1f}), uploads {:.1f}, readback {:.1f} (x{}), GPU busy {:.1f}, present {:.1f}, emulated CPU {:.1f}, other {:.1f} | GPU time done {:.1f} | draws {}, skipped {}, queued {} | thermal {}",
					frameMs, PhaseName(s.phase), sinceStart, d.Cause(),
					d.translate, d.shaders, now.shaderCompiles - prev.shaderCompiles, d.wait, now.waits - prev.waits, d.pipelines, now.pipelines - prev.pipelines,
					d.textures, Ms(now.astcEncode, prev.astcEncode), d.uploads, d.readback, now.readbacks - prev.readbacks, d.gpuWait, d.present, d.gameCpu, d.other, d.metalGpu,
					draws, now.skipped - prev.skipped, now.queued - prev.queued, ThermalName(thermal));
			}
			else
				s.windowLongSuppressed++;
		}

		if ((double)(nowNs - s.windowStart.ns) / 1e9 >= kWindowSeconds)
			LogWindow(s, now);
		if ((double)(nowNs - s.lastSummaryNs) / 1e9 >= kSummarySeconds)
		{
			s.lastSummaryNs = nowNs;
			cemuLog_log(LogType::Force, "{}", SummaryText(s, "so far"));
		}
	}

	void LogSummary(const char* reason)
	{
		State& s = S();
		std::lock_guard lock(s.mutex);
		if (!s.haveLast)
			return;
		cemuLog_log(LogType::Force, "{}", SummaryText(s, reason));
	}
}

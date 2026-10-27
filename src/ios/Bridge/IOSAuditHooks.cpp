#include "IOSAuditHooks.h"

#if defined(ESPRESSU_AUDIT_HOOKS)

#include "IOSAuditFrameStats.h"

#include "Cafe/CafeSystem.h"
#include "Cafe/HW/Latte/Core/Latte.h"
#include "Cafe/HW/Latte/Core/LatteWaitInfo.h"
#include "Cafe/HW/Latte/Core/PerfTelemetry.h"
#include "Cafe/HW/Latte/Renderer/Renderer.h"
#include "Cafe/HW/MMU/MMU.h"
#include "Cemu/Logging/CemuLogging.h"

#include <mach/mach.h>
#include <os/proc.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace
{

	struct QueuedFrame
	{
		CemuAuditFrame header;
		std::vector<uint8_t> thumb;
	};

	struct CaptureState
	{
		std::mutex mutex;
		std::deque<QueuedFrame> queue;
		std::atomic<uint32_t> armedViews{0};
		std::atomic<uint32_t> remaining{0};
		uint32_t thumbW = 128;
		uint32_t thumbH = 72;
		uint32_t nextSeq = 1;
	};

	CaptureState& Capture()
	{
		static CaptureState s;
		return s;
	}

	constexpr uint32_t kTimingRing = 1u << 14;
	struct TimingState
	{
		std::atomic<bool> enabled{false};
		std::atomic<uint64_t> written{0};
		uint64_t readPos = 0;
		uint64_t ring[kTimingRing];
	};

	TimingState& Timing()
	{
		static TimingState s;
		return s;
	}

	struct AudioDeviceState
	{
		const void* device = nullptr;
		int16_t prev[8] = {0};
	};

	struct AudioState
	{
		std::mutex mutex;
		AudioDeviceState devices[4];
		CemuAuditAudioStats stats{};
		double sumSquares = 0.0;
		uint64_t sampleCount = 0;
	};

	AudioState& Audio()
	{
		static AudioState s;
		return s;
	}

	uint64_t NowNs()
	{
		return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	void AppendEscaped(std::string& out, const char* s)
	{
		out.push_back('"');
		if (s)
		{
			for (const unsigned char* p = (const unsigned char*)s; *p; ++p)
			{
				if (*p == '"' || *p == '\\') { out.push_back('\\'); out.push_back((char)*p); }
				else if (*p < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", *p); out += b; }
				else out.push_back((char)*p);
			}
		}
		out.push_back('"');
	}

	struct Json
	{
		std::string text;
		bool first = true;
		void Key(const char* k)
		{
			if (!first) text.push_back(',');
			first = false;
			AppendEscaped(text, k);
			text.push_back(':');
		}
		void U(const char* k, uint64_t v) { Key(k); text += std::to_string((unsigned long long)v); }
		void I(const char* k, int64_t v) { Key(k); text += std::to_string((long long)v); }
		void F(const char* k, double v)
		{
			Key(k);
			char b[48];
			if (!std::isfinite(v)) v = 0.0;
			std::snprintf(b, sizeof(b), "%.6g", v);
			text += b;
		}
		void B(const char* k, bool v) { Key(k); text += v ? "true" : "false"; }
		void S(const char* k, const char* v) { Key(k); AppendEscaped(text, v); }
		void Open(const char* k) { Key(k); text.push_back('{'); first = true; }
		void Close() { text.push_back('}'); first = false; }
	};

	bool PhysFootprint(uint64_t& out)
	{
		task_vm_info_data_t info;
		mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
		if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &count) != KERN_SUCCESS)
			return false;
		out = (uint64_t)info.phys_footprint;
		return true;
	}

	bool GuestRangeOk(uint32_t address, uint32_t length)
	{
		if (length == 0 || length > 65536)
			return false;
		if (address < 0x10000000u || (uint64_t)address + length > 0x50000000ull)
			return false;
		return memory_isAddressRangeAccessible(address, length);
	}
}

extern "C" int cemu_audit_api_version(void)
{
	return CEMU_AUDIT_API_VERSION;
}

extern "C" const char* cemu_audit_build_info(void)
{
	return "{\"hooks\":" "1" ",\"capture\":\"metal\",\"snapshot\":1,\"audio\":true,\"frameTiming\":true,\"guestMemory\":true}";
}

extern "C" uint64_t cemu_audit_now_ns(void)
{
	return NowNs();
}

extern "C" void cemu_audit_set_log_profile(int profile)
{
	uint64 mask = cemuLog_getFlag(LogType::CoreinitLogging);
	if (profile >= 1)
	{
		mask |= cemuLog_getFlag(LogType::APIErrors);
		mask |= cemuLog_getFlag(LogType::GX2);
		mask |= cemuLog_getFlag(LogType::UnsupportedAPI);
		mask |= cemuLog_getFlag(LogType::TextureCache);
		mask |= cemuLog_getFlag(LogType::SoundAPI);
		mask |= cemuLog_getFlag(LogType::InputAPI);
		mask |= cemuLog_getFlag(LogType::TextureReadback);
	}
	if (profile >= 2)
	{
		mask |= cemuLog_getFlag(LogType::CoreinitMem);
		mask |= cemuLog_getFlag(LogType::CoreinitThreadSync);
		mask |= cemuLog_getFlag(LogType::CoreinitMP);
		mask |= cemuLog_getFlag(LogType::CoreinitMemoryMapping);
	}
	cemuLog_setActiveLoggingFlags(mask);
}

extern "C" bool cemu_audit_guest_read(uint32_t guestAddress, void* out, uint32_t length)
{
	if (!out || !CafeSystem::IsTitleRunning() || !GuestRangeOk(guestAddress, length))
		return false;
	std::memcpy(out, memory_getPointerFromVirtualOffset(guestAddress), length);
	std::atomic_thread_fence(std::memory_order_acquire);
	return true;
}

extern "C" bool cemu_audit_guest_write(uint32_t guestAddress, const void* data, uint32_t length)
{
	if (!data || !CafeSystem::IsTitleRunning() || !GuestRangeOk(guestAddress, length))
		return false;
	std::atomic_thread_fence(std::memory_order_release);
	std::memcpy(memory_getPointerFromVirtualOffset(guestAddress), data, length);
	return true;
}

extern "C" const char* cemu_audit_snapshot_json(void)
{
	static thread_local std::string result;
	Json j;
	j.text = "{";

	j.U("tNs", NowNs());
	j.B("titleRunning", CafeSystem::IsTitleRunning());

	j.Open("latte");
	j.U("frameCounter", LatteGPUState.frameCounter);
	j.U("flipCounter", LatteGPUState.flipCounter);
	j.U("drawCallCounter", LatteGPUState.drawCallCounter);
	j.U("textureBindCounter", LatteGPUState.textureBindCounter);
	j.U("flipRequestCount", (uint64_t)LatteGPUState.flipRequestCount.load());
	j.U("gx2InitCalled", LatteGPUState.gx2InitCalled);
	j.B("tvBufferUsesSRGB", LatteGPUState.tvBufferUsesSRGB);
	j.B("drcBufferUsesSRGB", LatteGPUState.drcBufferUsesSRGB);
	j.B("activeShaderHasError", LatteGPUState.activeShaderHasError);
	j.Close();

	{
		auto& w = LatteWait::Get();
		const auto r = std::memory_order_relaxed;
		j.Open("gpuThread");
		const char* reason = w.reason.load(r);
		j.S("waitReason", reason ? reason : "");
		j.U("waitKind", w.reasonKind.load(r));
		j.U("timeouts", w.timeouts.load(r));
		j.B("gpuPresumedLost", w.gpuPresumedLost.load(r));
		j.B("gpuError", w.gpuError.load(r));
		j.I("gpuErrorCode", w.gpuErrorCode.load(r));
		j.U("pm4Count", w.pm4Count.load(r));
		j.U("lastPM4Opcode", w.lastPM4Opcode.load(r));
		j.U("queriesInFlight", w.queriesInFlight.load(r));
		j.U("readbacksPending", w.readbacksPending.load(r));
		j.U("executingCommandBuffers", w.executingCommandBuffers.load(r));
		j.U("erroredCommandBuffers", w.erroredCommandBuffers.load(r));
		j.U("cbSubmitted", w.cbSubmitted.load(r));
		j.U("cbRetired", w.cbRetired.load(r));
		j.U("cbErrorStreak", w.cbErrorStreak.load(r));
		j.I("cbLastErrorCode", w.cbLastErrorCode.load(r));
		j.U("presentedFrames", w.presentedFrames.load(r));
		j.U("drawableFailures", w.drawableFailures.load(r));
		j.U("drawableFailuresInARow", w.drawableFailuresInARow.load(r));
		j.U("tvDrawableWidth", w.tvDrawableWidth.load(r));
		j.U("tvDrawableHeight", w.tvDrawableHeight.load(r));
		j.B("tvLayerAttached", w.tvLayerAttached.load(r));
		j.B("tvLayerHasDevice", w.tvLayerHasDevice.load(r));
		j.Close();

		j.Open("gpuMemory");
		j.B("valid", w.memStatsValid.load(r));
		j.U("deviceMB", w.memDeviceMB.load(r));
		j.U("hostMappedMB", w.memHostMappedMB.load(r));
		j.U("textureCount", w.memTextureCount.load(r));
		j.U("textureMB", w.memTextureMB.load(r));
		j.U("stagingMB", w.memStagingMB.load(r));
		j.U("indexMB", w.memIndexMB.load(r));
		j.U("snapshotMB", w.memSnapshotMB.load(r));
		j.U("bufferCacheMB", w.memBufferCacheMB.load(r));
		j.U("xfbMB", w.memXfbMB.load(r));
		j.U("readbackMB", w.memReadbackMB.load(r));
		j.U("texturesEvicted", w.texturesEvicted.load(r));
		j.U("evictionPasses", w.evictionPasses.load(r));
		j.Close();
	}

	{
		auto& c = PerfTelemetry::Get();
		const auto r = std::memory_order_relaxed;
		j.Open("perf");
		j.U("ppcIdleNs", c.ppcIdleNs.load(r));
		j.U("ppcHostThreads", c.ppcHostThreads.load(r));
		j.U("gpuIdleNs", c.gpuIdleNs.load(r));
		j.U("gpuSyncNs", c.gpuSyncNs.load(r));
		j.U("drawableWaitNs", c.drawableWaitNs.load(r));
		j.U("mtlGpuNs", c.mtlGpuNs.load(r));
		j.U("mtlCommandBuffers", c.mtlCommandBuffers.load(r));
		j.U("tvPresents", c.tvPresents.load(r));
		j.U("padPresents", c.padPresents.load(r));
		j.U("shaderCompiles", c.shaderCompiles.load(r));
		j.U("shaderCompileNs", c.shaderCompileNs.load(r));
		j.U("pipelineCompiles", c.pipelineCompiles.load(r));
		j.U("pipelineCompileNs", c.pipelineCompileNs.load(r));
		j.U("pipelineSyncCompiles", c.pipelineSyncCompiles.load(r));
		auto& s = PerfTelemetry::GetSummary();
		j.B("summaryValid", s.valid.load(r));
		j.F("hostFps", s.hostFps.load(r));
		j.F("guestFps", s.guestFps.load(r));
		j.F("vsyncRate", s.vsyncRate.load(r));
		j.F("ppcExecPct", s.ppcExecPct.load(r));
		j.F("gpuThreadBusyPct", s.gpuThreadBusyPct.load(r));
		j.F("mtlGpuMsPerFrame", s.mtlGpuMsPerFrame.load(r));
		j.F("mtlGpuBusyPct", s.mtlGpuBusyPct.load(r));
		j.S("bottleneck", PerfTelemetry::BottleneckName(s.bottleneck.load(r)));
		j.Close();
	}

	j.Open("renderer");
	if (g_renderer)
	{
		const RendererAPI api = g_renderer->GetType();
		j.S("api", api == RendererAPI::Metal ? "metal" : "opengl");
		int usage = 0, total = 0;
		const bool vram = g_renderer->GetVRAMInfo(usage, total);
		j.B("vramKnown", vram);
		j.I("vramUsedMB", vram ? usage : 0);
		j.I("vramTotalMB", vram ? total : 0);
		j.B("padWindowActive", g_renderer->IsPadWindowActive());
	}
	else
	{
		j.S("api", "none");
	}
	j.Close();

	{
		uint64_t footprint = 0;
		const bool haveFootprint = PhysFootprint(footprint);
		j.Open("memory");
		j.U("availableBytes", (uint64_t)os_proc_available_memory());
		j.B("footprintKnown", haveFootprint);
		j.U("footprintBytes", footprint);
		j.Close();
	}

	{
		CemuAuditAudioStats a;
		cemu_audit_audio_get(&a);
		j.Open("audio");
		j.U("callbacks", a.callbacks);
		j.U("frames", a.frames);
		j.U("validFrames", a.validFrames);
		j.U("underrunCallbacks", a.underrunCallbacks);
		j.U("underrunFrames", a.underrunFrames);
		j.U("feedRejects", a.feedRejects);
		j.U("silentCallbacks", a.silentCallbacks);
		j.U("discontinuities", a.discontinuities);
		j.U("maxStep", a.maxStep);
		j.U("peak", a.peak);
		j.U("longestZeroRun", a.longestZeroRun);
		j.U("channels", a.channels);
		j.F("rms", a.rms);
		j.Close();
	}

	j.text.push_back('}');
	result = std::move(j.text);
	return result.c_str();
}

extern "C" void cemu_audit_capture_arm(uint32_t viewMask, uint32_t count, uint32_t thumbWidth, uint32_t thumbHeight)
{
	auto& c = Capture();
	std::lock_guard<std::mutex> lock(c.mutex);
	c.armedViews.store(0);
	c.remaining.store(0);
	c.queue.clear();
	c.thumbW = std::clamp(thumbWidth, 8u, 512u);
	c.thumbH = std::clamp(thumbHeight, 8u, 288u);
	c.nextSeq = 1;
	c.remaining.store(std::min(count, 4096u));
	c.armedViews.store(viewMask & (CEMU_AUDIT_VIEW_TV | CEMU_AUDIT_VIEW_PAD));
}

extern "C" void cemu_audit_capture_cancel(void)
{
	auto& c = Capture();
	std::lock_guard<std::mutex> lock(c.mutex);
	c.armedViews.store(0);
	c.remaining.store(0);
	c.queue.clear();
}

extern "C" uint32_t cemu_audit_capture_pending(void)
{
	auto& c = Capture();
	std::lock_guard<std::mutex> lock(c.mutex);
	return (uint32_t)c.queue.size();
}

extern "C" bool cemu_audit_capture_pop(CemuAuditFrame* header, uint8_t* thumbRGB, uint32_t thumbCapacity)
{
	auto& c = Capture();
	std::lock_guard<std::mutex> lock(c.mutex);
	if (c.queue.empty() || !header)
		return false;
	QueuedFrame& f = c.queue.front();
	*header = f.header;
	if (thumbRGB && thumbCapacity >= f.thumb.size() && !f.thumb.empty())
		std::memcpy(thumbRGB, f.thumb.data(), f.thumb.size());
	c.queue.pop_front();
	return true;
}

bool cemu_audit_capture_wants(bool tv)
{
	auto& t = Timing();
	if (tv && t.enabled.load(std::memory_order_relaxed))
	{
		const uint64_t n = t.written.load(std::memory_order_relaxed);
		t.ring[n & (kTimingRing - 1)] = NowNs();
		t.written.store(n + 1, std::memory_order_release);
	}
	auto& c = Capture();
	return c.remaining.load(std::memory_order_relaxed) > 0 &&
	       (c.armedViews.load(std::memory_order_relaxed) & (tv ? CEMU_AUDIT_VIEW_TV : CEMU_AUDIT_VIEW_PAD)) != 0;
}

void cemu_audit_capture_deliver(bool tv, uint32_t latteFrame, uint32_t srcWidth, uint32_t srcHeight,
                                uint32_t rawPixelFormat, int layout, const uint8_t* pixels, uint32_t rowBytes,
                                bool readbackFailed)
{
	auto& c = Capture();
	uint32_t thumbW, thumbH;
	{
		std::lock_guard<std::mutex> lock(c.mutex);
		thumbW = c.thumbW;
		thumbH = c.thumbH;
	}

	QueuedFrame f{};
	CemuAuditFrame& h = f.header;
	h.view = tv ? CEMU_AUDIT_VIEW_TV : CEMU_AUDIT_VIEW_PAD;
	h.latteFrame = latteFrame;
	h.timestampNs = NowNs();
	h.srcWidth = srcWidth;
	h.srcHeight = srcHeight;
	h.pixelFormat = rawPixelFormat;
	h.thumbWidth = thumbW;
	h.thumbHeight = thumbH;
	h.status = CEMU_AUDIT_FRAME_OK;

	if (readbackFailed || !pixels)
	{
		h.status = CEMU_AUDIT_FRAME_READBACK_FAILED;
	}
	else if (layout < 0)
	{
		h.status = CEMU_AUDIT_FRAME_UNSUPPORTED_FORMAT;
	}
	else
	{
		f.thumb.assign((size_t)thumbW * thumbH * 3, 0);
		ios_audit::FrameStats stats;
		ios_audit::ComputeStatsAndThumb(pixels, srcWidth, srcHeight, rowBytes, (ios_audit::PixelLayout)layout,
		                                thumbW, thumbH, f.thumb.data(), stats);
		h.meanR = stats.meanR;
		h.meanG = stats.meanG;
		h.meanB = stats.meanB;
		h.blackFraction = stats.blackFraction;
		h.whiteFraction = stats.whiteFraction;
		h.minLuma = stats.minLuma;
		h.maxLuma = stats.maxLuma;
		h.hash = stats.hash;
	}

	std::lock_guard<std::mutex> lock(c.mutex);
	if (c.remaining.load() == 0)
		return;
	h.seq = c.nextSeq++;
	if (c.queue.size() >= 512)
		c.queue.pop_front();
	c.queue.push_back(std::move(f));
	const uint32_t left = c.remaining.load() - 1;
	c.remaining.store(left);
	if (left == 0)
		c.armedViews.store(0);
}

extern "C" void cemu_audit_frame_timing_enable(bool enabled)
{
	auto& t = Timing();
	if (enabled)
		t.readPos = t.written.load(std::memory_order_acquire);
	t.enabled.store(enabled, std::memory_order_relaxed);
}

extern "C" uint32_t cemu_audit_frame_timing_drain(uint64_t* outNs, uint32_t capacity, uint32_t* dropped)
{
	auto& t = Timing();
	if (dropped)
		*dropped = 0;
	if (!outNs || capacity == 0)
		return 0;
	const uint64_t written = t.written.load(std::memory_order_acquire);
	uint64_t pos = t.readPos;
	if (written - pos > kTimingRing)
	{
		if (dropped)
			*dropped = (uint32_t)std::min<uint64_t>(written - pos - kTimingRing, 0xFFFFFFFFull);
		pos = written - kTimingRing;
	}
	uint32_t n = 0;
	while (pos < written && n < capacity)
		outNs[n++] = t.ring[pos++ & (kTimingRing - 1)];
	t.readPos = pos;
	return n;
}

extern "C" void cemu_audit_audio_reset(void)
{
	auto& a = Audio();
	std::lock_guard<std::mutex> lock(a.mutex);
	a.stats = CemuAuditAudioStats{};
	a.sumSquares = 0.0;
	a.sampleCount = 0;
}

extern "C" void cemu_audit_audio_get(CemuAuditAudioStats* out)
{
	if (!out)
		return;
	auto& a = Audio();
	std::lock_guard<std::mutex> lock(a.mutex);
	*out = a.stats;
	out->rms = a.sampleCount ? std::sqrt(a.sumSquares / (double)a.sampleCount) / 32768.0 : 0.0;
}

extern "C" void cemu_audit_audio_note_feed_reject(void)
{
	auto& a = Audio();
	std::lock_guard<std::mutex> lock(a.mutex);
	a.stats.feedRejects++;
}

extern "C" void cemu_audit_audio_note_render(const void* device, const int16_t* samples, uint32_t bytesValid,
                                             uint32_t bytesRequested, uint32_t channels, uint32_t bitsPerSample)
{
	if (channels == 0 || channels > 8)
		return;
	auto& a = Audio();

	uint32_t peak = 0, maxStep = 0, longestRun = 0;
	uint64_t jumps = 0;
	double sumSq = 0.0;
	uint64_t nSamples = 0;
	const uint32_t frameBytes = channels * 2;
	const uint32_t requestedFrames = bytesRequested / frameBytes;
	uint32_t validFrames = 0;

	AudioDeviceState* dev = nullptr;
	{
		std::lock_guard<std::mutex> lock(a.mutex);
		for (auto& d : a.devices)
		{
			if (d.device == device) { dev = &d; break; }
			if (!dev && d.device == nullptr) dev = &d;
		}
		if (dev && dev->device == nullptr)
			dev->device = device;
	}

	bool silentCallback = false;
	if (bitsPerSample == 16 && samples)
	{
		validFrames = std::min(bytesValid / frameBytes, requestedFrames);
		uint32_t run = 0;
		bool allZeroCallback = validFrames > 0;
		for (uint32_t f = 0; f < validFrames; ++f)
		{
			bool zeroFrame = true;
			for (uint32_t ch = 0; ch < channels; ++ch)
			{
				const int32_t s = samples[(size_t)f * channels + ch];
				const uint32_t mag = (uint32_t)(s < 0 ? -s : s);
				if (mag > peak) peak = mag;
				if (s != 0) zeroFrame = false;
				sumSq += (double)s * (double)s;
				++nSamples;
				if (dev && ch < 8)
				{
					const int32_t d = s - (int32_t)dev->prev[ch];
					const uint32_t step = (uint32_t)(d < 0 ? -d : d);
					if (step > maxStep) maxStep = step;
					if (step > 12000u) ++jumps;
					dev->prev[ch] = (int16_t)s;
				}
			}
			if (zeroFrame) { ++run; if (run > longestRun) longestRun = run; }
			else { run = 0; allZeroCallback = false; }
		}
		silentCallback = allZeroCallback;
	}
	else
	{
		validFrames = std::min(bytesValid / frameBytes, requestedFrames);
	}

	std::lock_guard<std::mutex> lock(a.mutex);
	if (silentCallback)
		a.stats.silentCallbacks++;
	a.stats.callbacks++;
	a.stats.frames += requestedFrames;
	a.stats.validFrames += validFrames;
	if (validFrames < requestedFrames)
	{
		a.stats.underrunCallbacks++;
		a.stats.underrunFrames += requestedFrames - validFrames;
	}
	a.stats.discontinuities += jumps;
	if (maxStep > a.stats.maxStep) a.stats.maxStep = maxStep;
	if (peak > a.stats.peak) a.stats.peak = peak;
	if (longestRun > a.stats.longestZeroRun) a.stats.longestZeroRun = longestRun;
	a.stats.channels = channels;
	a.sumSquares += sumSq;
	a.sampleCount += nSamples;
}

#endif

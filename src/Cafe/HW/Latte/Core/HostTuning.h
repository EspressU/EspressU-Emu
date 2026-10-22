#pragma once

#include <atomic>
#include <cstdint>

// Host-side performance options that the iOS front end switches at runtime.
// Every option defaults to the behaviour EspressU had before it existed.
namespace HostTuning
{
	struct Options
	{
		// Free textures and idle GPU buffers on the render thread when iOS sends a memory warning.
		std::atomic<bool> freeMemoryOnWarning{false};
		// Write failed Metal allocations and GPU faults to a small file that survives a crash.
		std::atomic<bool> keepGpuFaultHistory{false};
		// Enlarge the finished TV and GamePad images with the MetalFX spatial scaler.
		std::atomic<bool> metalFxUpscale{false};
		// Present TV and GamePad frames through a Metal 4 command queue (iOS 26, Metal 4 GPUs).
		std::atomic<bool> metal4Output{false};
		// Generated in-between frames on the Metal 4 output: 0 off, 1 EspressU's own, 2 MetalFX.
		std::atomic<uint8_t> frameGeneration{0};
		// When TV and GamePad are both on screen, draw the GamePad every second frame and hold it back while its
		// drawables are slow, so it never delays the TV image.
		std::atomic<bool> padPresentPacing{false};
		// Let the no-JIT block-cache interpreter jump from one cached block to the next through stored links
		// instead of a hash lookup per block.
		std::atomic<bool> interpreterBlockLinking{false};
		// Only reported: the front end sizes the GamePad surface itself.
		std::atomic<bool> padLowResolution{false};
		// Write a summary of what the options above actually did to the log every few seconds.
		std::atomic<bool> diagnosticsLog{false};
	};

	inline Options& Get()
	{
		static Options s_options;
		return s_options;
	}

	// Counters the options above update, for the diagnostics log. Totals since launch; the report prints the change.
	struct Stats
	{
		// Memory warnings
		std::atomic<uint64_t> memoryWarnings{0};
		std::atomic<uint64_t> memoryCleanups{0};
		std::atomic<uint64_t> memoryTexturesFreed{0};
		std::atomic<uint64_t> memoryFreedMB{0};
		// Low-memory fallbacks
		std::atomic<uint64_t> substituteTextures{0};
		std::atomic<uint64_t> viewFallbacks{0};
		std::atomic<uint64_t> faultsRecorded{0};
		// MetalFX spatial
		std::atomic<uint64_t> fxUpscales{0};
		std::atomic<uint64_t> fxRebuilds{0};
		std::atomic<uint64_t> fxFailures{0};
		std::atomic<uint64_t> fxNotEnlarged{0};
		// Metal 4 output
		std::atomic<uint64_t> m4Frames{0};
		std::atomic<uint64_t> m4Dropped{0};
		std::atomic<uint64_t> m4RegularPath{0};
		std::atomic<uint64_t> m4SlotTimeouts{0};
		std::atomic<uint64_t> m4GpuMicros{0};
		// Frame generation
		std::atomic<uint64_t> fgGenerated{0};
		std::atomic<uint64_t> fgSkippedFastEnough{0};
		std::atomic<uint64_t> fgSkippedHistory{0};
		std::atomic<uint64_t> fgSkippedQueue{0};
		std::atomic<uint64_t> fgSkippedGpu{0};
		std::atomic<uint64_t> fgSkippedCooldown{0};
		std::atomic<uint64_t> fgCooldowns{0};
		std::atomic<uint64_t> fgEncodeFailed{0};
		std::atomic<uint64_t> fgGpuMicros{0};
		std::atomic<uint64_t> fgIntervalMicros{0};
		std::atomic<uint8_t> fgBackend{0}; // 0 none, 1 EspressU, 2 MetalFX
		std::atomic<bool> fgDisabled{false};
		// GamePad pacing
		std::atomic<uint64_t> padSkippedFrames{0};
		std::atomic<uint64_t> padSlowDrawables{0};
		// Interpreter
		std::atomic<uint64_t> itpBlocks{0};
		std::atomic<uint64_t> itpLinkHits{0};
		std::atomic<uint64_t> itpDecodes{0};
	};

	inline Stats& GetStats()
	{
		static Stats s_stats;
		return s_stats;
	}

	inline void Count(std::atomic<uint64_t>& counter, uint64_t amount = 1)
	{
		if (Get().diagnosticsLog.load(std::memory_order_relaxed))
			counter.fetch_add(amount, std::memory_order_relaxed);
	}
}

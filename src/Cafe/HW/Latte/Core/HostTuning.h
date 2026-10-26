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
		// When TV and GamePad are both on screen, draw the GamePad every second frame and hold it back while its
		// drawables are slow, so it never delays the TV image.
		std::atomic<bool> padPresentPacing{false};
		// Let the no-JIT block-cache interpreter jump from one cached block to the next through stored links
		// instead of a hash lookup per block.
		std::atomic<bool> interpreterBlockLinking{false};
		// Let a JIT worker wait for another one that is compiling the same function instead of compiling it twice.
		std::atomic<bool> jitSkipOverlappingCompiles{false};
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
		// GamePad pacing
		std::atomic<uint64_t> padSkippedFrames{0};
		std::atomic<uint64_t> padSlowDrawables{0};
		// Interpreter
		std::atomic<uint64_t> itpBlocks{0};
		std::atomic<uint64_t> itpLinkHits{0};
		std::atomic<uint64_t> itpDecodes{0};
		// JIT
		std::atomic<uint64_t> jitCompiled{0};
		std::atomic<uint64_t> jitDiscarded{0};
		std::atomic<uint64_t> jitWaited{0};
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

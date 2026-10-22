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
	};

	inline Options& Get()
	{
		static Options s_options;
		return s_options;
	}
}

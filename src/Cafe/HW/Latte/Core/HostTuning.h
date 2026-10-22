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
	};

	inline Options& Get()
	{
		static Options s_options;
		return s_options;
	}
}

#pragma once

#include <cstdint>
#include <string>

// Keeps a short, persistent record of Metal allocations that failed and GPU faults, so a bug
// report after a crash still shows what ran out first. Recording is off unless
// HostTuning::Options::keepGpuFaultHistory is set.
namespace MetalFaultHistory
{
	enum class Kind : uint8_t
	{
		Texture,
		TextureView,
		StagingBuffer,
		HeapBuffer,
		UploadBuffer,
		StreamoutBuffer,
		ReadbackBuffer,
		GpuFault,
		Count
	};

	void Record(Kind kind, uint64_t wantedBytes, const std::string& detail);

	// The newest entries first, one per line; an empty string when there are none.
	std::string Describe(size_t maxEntries);

	void Clear();
}

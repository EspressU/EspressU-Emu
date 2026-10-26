#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <vector>

// Time between consecutive game frames (scan buffer swaps), kept per running title for the session report:
// average, lows and hitches need every frame, not just a counter.
namespace FrameTimeStats
{
	struct Recorder
	{
		static constexpr size_t kMaxFrames = 2'000'000; // about 9 hours at 60 fps
		// Longer gaps are pauses or loading screens, counted apart instead of as frames.
		static constexpr double kPauseMs = 1000.0;

		std::mutex mutex;
		std::vector<float> frameMs;
		uint64_t pauses = 0;
		std::chrono::steady_clock::time_point last{};
		bool haveLast = false;

		void NoteFrame()
		{
			const auto now = std::chrono::steady_clock::now();
			std::lock_guard lock(mutex);
			if (haveLast)
			{
				const double ms = std::chrono::duration<double, std::milli>(now - last).count();
				if (ms >= kPauseMs)
					pauses++;
				else if (frameMs.size() < kMaxFrames)
					frameMs.push_back((float)ms);
			}
			last = now;
			haveLast = true;
		}

		// Hands over everything recorded so far and starts over.
		void Take(std::vector<float>& outFrames, uint64_t& outPauses)
		{
			std::lock_guard lock(mutex);
			outFrames.swap(frameMs);
			frameMs.clear();
			outPauses = pauses;
			pauses = 0;
			haveLast = false;
		}
	};

	inline Recorder& Get()
	{
		static Recorder s_recorder;
		return s_recorder;
	}
}

// Copyright (c) 2026 EspressU (https://espressu.dev). EspressU Licence: non-commercial use only.
// Anything that contains or is derived from this file must be fully open source, keep this file
// under the EspressU Licence, keep this notice in every copy, and credit EspressU (https://espressu.dev).
// Full text: LICENSES/EspressU-Licence.txt. Not covered by MPL-2.0.

#pragma once

// Frame times and long-frame causes for EspressU-Report.txt, lines start with [Frames].
//
// Every presented game frame is timed on the render thread and filed under one phase: startup (until the
// first 3D scene), loading (the game reads files fast or draws almost nothing) or gameplay. Each phase keeps
// a histogram, so the report can give 1% lows, p95/p99 and frames over 50/100 ms. A frame over 50 ms gets
// its own line that splits the time the render thread spent into shader translation, shader and pipeline
// compiles, waiting for compile workers, texture work, readbacks, GPU waits, waiting for the emulated CPU
// and presenting.
namespace MetalFrameStats
{
	// Render thread, once per presented TV frame (end of SwapBuffers).
	void OnTvFrame();
	// Any thread. Logs the per-phase totals so far.
	void LogSummary(const char* reason);
	// The current phase as PerfTelemetry::FramePhase, for the compile throttle.
	int CurrentPhase();
}

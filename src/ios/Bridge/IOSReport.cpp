#include "IOSReport.h"
#include "Cafe/HW/Latte/Core/FrameTimeStats.h"
#include "Cafe/HW/Latte/Core/PerfTelemetry.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <functional>
#include <mutex>
#include <numeric>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace
{
	// Ordinary log lines stop going into the report past this size; summaries and crash reports still do.
	constexpr off_t kLogTeeLimit = 48ll << 20;

	int s_fd = -1;
	// Where this session starts in the file; the log size limit counts from here.
	off_t s_sessionStart = 0;
	std::string s_path;
	std::string s_previousPath;
	std::atomic<bool> s_logTeeFull{false};

	std::mutex s_stateMutex;
	std::string s_settings;
	std::string s_device;
	uint64_t s_titleId = 0;
	std::string s_titleName;
	std::chrono::steady_clock::time_point s_titleStart;
	bool s_titleRunning = false;

	struct PerfTotals
	{
		uint32_t windows = 0;
		double seconds = 0, guestFps = 0, speed = 0, ppcRun = 0, gpuThread = 0, metalMs = 0, metalBusy = 0;
		double minGuestFps = 1e9, minSpeed = 1e9;
		uint32_t bottleneck[16]{};
		uint32_t thermal[4]{};
		uint64_t minAvailableMB = UINT64_MAX;
		uint64_t maxFootprintMB = 0;
	} s_perf;

	void WriteAll(const char* text, size_t length)
	{
		if (s_fd < 0 || !text)
			return;
		while (length > 0)
		{
			const ssize_t written = write(s_fd, text, length);
			if (written <= 0)
				return;
			text += written;
			length -= (size_t)written;
		}
	}

	void WriteString(const std::string& text)
	{
		WriteAll(text.data(), text.size());
	}

	std::string Timestamp()
	{
		const auto now = std::chrono::system_clock::now();
		const std::time_t t = std::chrono::system_clock::to_time_t(now);
		std::tm local{};
		localtime_r(&t, &local);
		char buf[40];
		const int ms = (int)(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
		snprintf(buf, sizeof(buf), "[%02d:%02d:%02d.%03d] ", local.tm_hour, local.tm_min, local.tm_sec, ms);
		return buf;
	}

	std::string DateTime()
	{
		const std::time_t t = std::time(nullptr);
		std::tm local{};
		localtime_r(&t, &local);
		char buf[40];
		std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &local);
		return buf;
	}

	std::string ReadTail(const std::string& path, size_t maxBytes)
	{
		FILE* f = fopen(path.c_str(), "rb");
		if (!f)
			return {};
		fseek(f, 0, SEEK_END);
		const long size = ftell(f);
		const long start = size > (long)maxBytes ? size - (long)maxBytes : 0;
		fseek(f, start, SEEK_SET);
		std::string text((size_t)(size - start), '\0');
		const size_t got = fread(text.data(), 1, text.size(), f);
		fclose(f);
		text.resize(got);
		return text;
	}

	// What the session before this one, further up in the same file, left behind: a crash report, a kill while a
	// game ran, or a normal end. Only its verdict is written; the session itself is right above.
	std::string DescribePreviousSession(const std::string& path)
	{
		std::string tail = ReadTail(path, 1024 * 1024);
		const size_t sessionAt = tail.rfind("EspressU report, session started");
		if (tail.empty() || sessionAt == std::string::npos)
			return "This is the first session in this report.\n";
		tail = tail.substr(sessionAt);

		const char* crashMarkers[] = {"=== CEMU CRASH", "=== CEMU TERMINATE", "=== GPU THREAD EXCEPTION"};
		bool crashed = false;
		for (const char* marker : crashMarkers)
			crashed = crashed || tail.find(marker) != std::string::npos;
		const size_t lastStart = tail.rfind("[report] TITLE RUNNING");
		const size_t lastStop = tail.rfind("[report] TITLE STOPPED");
		if (crashed)
			return "The previous session (above) CRASHED; its crash report is at the end of it.\n";
		if (lastStart != std::string::npos && (lastStop == std::string::npos || lastStop < lastStart))
			return "The previous session (above) ENDED WHILE A GAME WAS RUNNING without a crash report: iOS killed the app "
			       "(out of memory, watchdog, overheating) or it was closed from the app switcher.\n";
		return "The previous session (above) ended normally.\n";
	}

	const char* ThermalName(int state)
	{
		switch (state)
		{
		case 0: return "nominal";
		case 1: return "fair";
		case 2: return "serious";
		case 3: return "critical";
		default: return "?";
		}
	}

	std::string Percent(uint32_t part, uint32_t whole)
	{
		char buf[16];
		snprintf(buf, sizeof(buf), "%.0f%%", whole ? 100.0 * part / whole : 0.0);
		return buf;
	}

	std::string FormatSummary(const std::vector<float>& frames, uint64_t pauses, const PerfTotals& perf, double runSeconds, const char* reason)
	{
		std::string out;
		char line[512];
		snprintf(line, sizeof(line), "Title %016llx %s, ran %.0f s, ended: %s\n", (unsigned long long)s_titleId, s_titleName.c_str(), runSeconds, reason ? reason : "?");
		out += line;

		double avgFps = 0, low1 = 0, low01 = 0, medianMs = 0, worstMs = 0, stddevMs = 0;
		uint32_t over50 = 0, over100 = 0, over250 = 0, spikes = 0, slower33 = 0, slower17 = 0;
		if (!frames.empty())
		{
			std::vector<float> sorted(frames);
			std::sort(sorted.begin(), sorted.end());
			const double total = std::accumulate(frames.begin(), frames.end(), 0.0);
			const double mean = total / frames.size();
			avgFps = 1000.0 / mean;
			medianMs = sorted[sorted.size() / 2];
			worstMs = sorted.back();
			// Lows: the average frame rate of the slowest 1% and 0.1% of frames.
			auto lowFps = [&](double fraction) {
				const size_t count = std::max<size_t>(1, (size_t)(sorted.size() * fraction));
				const double sum = std::accumulate(sorted.end() - count, sorted.end(), 0.0);
				return 1000.0 / (sum / count);
			};
			low1 = lowFps(0.01);
			low01 = lowFps(0.001);
			double var = 0;
			for (float ms : frames)
			{
				var += (ms - mean) * (ms - mean);
				if (ms > 50) over50++;
				if (ms > 100) over100++;
				if (ms > 250) over250++;
				if (ms > medianMs * 2.0) spikes++;
				if (ms > 33.4) slower33++;
				if (ms > 16.8) slower17++;
			}
			stddevMs = std::sqrt(var / frames.size());
		}
		snprintf(line, sizeof(line),
			"Frames: %zu (+%llu pauses over 1 s, not counted) | avg %.1f fps | 1%% low %.1f fps | 0.1%% low %.1f fps\n"
			"Frame time: median %.1f ms, worst %.0f ms, spread (std dev) %.1f ms\n"
			"Stutter: %u frames over 2x the median, %u over 50 ms, %u over 100 ms, %u over 250 ms | slower than 30 fps: %s, slower than 60 fps: %s\n",
			frames.size(), (unsigned long long)pauses, avgFps, low1, low01, medianMs, worstMs, stddevMs,
			spikes, over50, over100, over250, Percent(slower33, (uint32_t)frames.size()).c_str(), Percent(slower17, (uint32_t)frames.size()).c_str());
		out += line;

		if (perf.windows)
		{
			const double n = perf.windows;
			snprintf(line, sizeof(line),
				"Per 5 s window (%u windows): game fps avg %.1f, min %.1f | game speed avg %.0f%%, min %.0f%% | emulated CPU busy %.0f%% | GPU thread busy %.0f%% | Metal GPU %.1f ms/frame, %.0f%% busy\n",
				perf.windows, perf.guestFps / n, perf.minGuestFps, perf.speed / n, perf.minSpeed, perf.ppcRun / n, perf.gpuThread / n, perf.metalMs / n, perf.metalBusy / n);
			out += line;
			out += "Limited by:";
			for (int i = 0; i < 16; i++)
			{
				if (perf.bottleneck[i])
					out += std::string(" ") + PerfTelemetry::BottleneckName(i) + " " + Percent(perf.bottleneck[i], perf.windows);
			}
			out += "\nThermal:";
			for (int i = 0; i < 4; i++)
			{
				if (perf.thermal[i])
					out += std::string(" ") + ThermalName(i) + " " + Percent(perf.thermal[i], perf.windows);
			}
			snprintf(line, sizeof(line), "\nMemory: lowest free %llu MB, highest in use %llu MB\n",
				(unsigned long long)(perf.minAvailableMB == UINT64_MAX ? 0 : perf.minAvailableMB), (unsigned long long)perf.maxFootprintMB);
			out += line;
		}

		// One line to grep for and paste next to another run.
		const size_t settingsHash = std::hash<std::string>{}(s_settings) & 0xFFFFFF;
		uint32_t topBottleneck = 0;
		for (uint32_t i = 1; i < 16; i++)
			if (perf.bottleneck[i] > perf.bottleneck[topBottleneck])
				topBottleneck = i;
		snprintf(line, sizeof(line),
			"COMPARE | %s | title %016llx | %.0f s | avg %.1f fps | 1%% low %.1f | 0.1%% low %.1f | >100 ms %u | speed %.0f%% | metal %.1f ms | limit %s | settings %06zx\n",
			DateTime().c_str(), (unsigned long long)s_titleId, runSeconds, avgFps, low1, low01, over100,
			perf.windows ? perf.speed / perf.windows : 0.0, perf.windows ? perf.metalMs / perf.windows : 0.0,
			perf.windows ? PerfTelemetry::BottleneckName((int)topBottleneck) : "?", settingsHash);
		out += line;
		return out;
	}
}

void IOSReport_OpenEarly()
{
	if (s_fd >= 0)
		return;
	const char* home = getenv("HOME");
	if (!home)
		return;
	// All logs live in Documents/Logs, next to each other and visible in the Files app.
	const std::string dir = std::string(home) + "/Documents/Logs/";
	mkdir(dir.c_str(), 0755);
	// One file for every session, appended to until it is deleted in the app or the Files app.
	s_path = dir + "EspressU-Report.txt";
	const std::string previous = DescribePreviousSession(s_path);
	s_fd = open(s_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (s_fd < 0)
		return;
	struct stat info{};
	s_sessionStart = fstat(s_fd, &info) == 0 ? info.st_size : 0;
	if (s_sessionStart > 0)
		WriteString("\n\n");

	WriteString("################################################################\n");
	WriteString("EspressU report, session started " + DateTime() + "\n");
	WriteString("Sections: PREVIOUS SESSION, DEVICE, SETTINGS (at each game start), LOG (everything the app and the core log),\n"
	            "CRASH (when one happens), PERFORMANCE SUMMARY + COMPARE line (each time a game stops).\n");
	WriteString("################################################################\n");
	IOSReport_Section("PREVIOUS SESSION");
	WriteString(previous);
	IOSReport_Section("LOG");
}

int IOSReport_Fd()
{
	return s_fd;
}

void IOSReport_Write(const char* text, size_t length)
{
	if (s_fd < 0 || s_logTeeFull.load(std::memory_order_relaxed))
		return;
	struct stat info{};
	if (fstat(s_fd, &info) == 0 && info.st_size - s_sessionStart > kLogTeeLimit)
	{
		if (!s_logTeeFull.exchange(true))
			WriteString("\n[report] this session reached 48 MB in the report; further ordinary log lines are only in log.txt\n");
		return;
	}
	WriteAll(text, length);
}

void IOSReport_Line(const std::string& line)
{
	WriteString(Timestamp() + line + "\n");
}

void IOSReport_Section(const std::string& title)
{
	WriteString("\n===== " + title + " =====\n");
}

void IOSReport_SetDevice(const std::string& text)
{
	{
		std::lock_guard lock(s_stateMutex);
		if (s_device == text)
			return;
		s_device = text;
	}
	IOSReport_Section("DEVICE");
	WriteString(text);
	if (!text.empty() && text.back() != '\n')
		WriteString("\n");
	IOSReport_Section("LOG (continued)");
}

void IOSReport_SetSettings(const std::string& text)
{
	{
		std::lock_guard lock(s_stateMutex);
		s_settings = text;
	}
	IOSReport_Section("SETTINGS");
	WriteString(text);
	if (!text.empty() && text.back() != '\n')
		WriteString("\n");
	char hash[64];
	snprintf(hash, sizeof(hash), "settings hash %06zx (same hash = same settings)\n", std::hash<std::string>{}(text) & 0xFFFFFF);
	WriteString(hash);
	IOSReport_Section("LOG (continued)");
}

void IOSReport_TitleStarted(uint64_t titleId, const std::string& name)
{
	std::vector<float> discard;
	uint64_t pauses = 0;
	FrameTimeStats::Get().Take(discard, pauses);
	{
		std::lock_guard lock(s_stateMutex);
		s_titleId = titleId;
		s_titleName = name;
		s_titleStart = std::chrono::steady_clock::now();
		s_titleRunning = true;
		s_perf = PerfTotals{};
	}
	char line[256];
	snprintf(line, sizeof(line), "[report] TITLE RUNNING %016llx %s", (unsigned long long)titleId, name.c_str());
	IOSReport_Line(line);
}

void IOSReport_NotePerfWindow(const IOSReportPerfWindow& w)
{
	std::lock_guard lock(s_stateMutex);
	if (!s_titleRunning)
		return;
	auto& p = s_perf;
	p.windows++;
	p.seconds += w.seconds;
	p.guestFps += w.guestFps;
	p.speed += w.speedPct;
	p.ppcRun += w.ppcRunPct;
	p.gpuThread += w.gpuThreadBusyPct;
	p.metalMs += w.metalGpuMs;
	p.metalBusy += w.metalGpuBusyPct;
	p.minGuestFps = std::min(p.minGuestFps, w.guestFps);
	p.minSpeed = std::min(p.minSpeed, w.speedPct);
	p.bottleneck[std::clamp(w.bottleneck, 0, 15)]++;
	p.thermal[std::clamp(w.thermal, 0, 3)]++;
	if (w.availableMB)
		p.minAvailableMB = std::min(p.minAvailableMB, w.availableMB);
	p.maxFootprintMB = std::max(p.maxFootprintMB, w.footprintMB);
}

void IOSReport_TitleStopped(const char* reason)
{
	std::vector<float> frames;
	uint64_t pauses = 0;
	FrameTimeStats::Get().Take(frames, pauses);
	std::string summary;
	{
		std::lock_guard lock(s_stateMutex);
		if (!s_titleRunning)
			return;
		s_titleRunning = false;
		const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - s_titleStart).count();
		summary = FormatSummary(frames, pauses, s_perf, seconds, reason);
	}
	IOSReport_Section("PERFORMANCE SUMMARY");
	WriteString(summary);
	IOSReport_Line("[report] TITLE STOPPED");
	IOSReport_Section("LOG (continued)");
}

std::string IOSReport_Path()
{
	return s_path;
}

std::string IOSReport_PreviousPath()
{
	return s_previousPath;
}

void IOSReport_Clear()
{
	if (s_fd < 0)
		return;
	ftruncate(s_fd, 0);
	s_sessionStart = 0;
	s_logTeeFull.store(false);
	WriteString("################################################################\n");
	WriteString("EspressU report, cleared " + DateTime() + "; the session continues below\n");
	WriteString("################################################################\n");
	IOSReport_Section("LOG (continued)");
}

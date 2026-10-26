#include "Cafe/HW/Latte/Renderer/Metal/MetalFaultHistory.h"
#include "Cafe/HW/Latte/Core/HostTuning.h"
#include "Cafe/CafeSystem.h"
#include "config/ActiveSettings.h"

#include <array>
#include <ctime>
#include <deque>
#include <fstream>
#include <mutex>
#if BOOST_OS_IOS
#include <os/proc.h>
#endif

namespace MetalFaultHistory
{
	namespace
	{
		constexpr size_t kKeptOnDisk = 200;
		// A failing allocation tends to fail every frame; past this many per kind a session only counts them.
		constexpr uint32_t kWrittenPerKindPerSession = 16;

		std::mutex s_mutex;
		bool s_loaded = false;
		std::deque<std::string> s_lines;
		std::array<uint32_t, (size_t)Kind::Count> s_sessionCounts{};

		const char* KindName(Kind kind)
		{
			switch (kind)
			{
			case Kind::Texture: return "texture";
			case Kind::TextureView: return "texture view";
			case Kind::StagingBuffer: return "staging buffer";
			case Kind::HeapBuffer: return "heap buffer";
			case Kind::UploadBuffer: return "upload buffer";
			case Kind::StreamoutBuffer: return "streamout buffer";
			case Kind::ReadbackBuffer: return "readback buffer";
			case Kind::GpuFault: return "GPU fault";
			default: return "other";
			}
		}

		fs::path FilePath()
		{
			return ActiveSettings::GetUserDataPath("metal_fault_history.txt");
		}

		void LoadLocked()
		{
			if (s_loaded)
				return;
			s_loaded = true;
			std::ifstream in(FilePath());
			std::string line;
			while (std::getline(in, line))
			{
				if (line.empty())
					continue;
				s_lines.push_back(line);
				if (s_lines.size() > kKeptOnDisk)
					s_lines.pop_front();
			}
		}

		void SaveLocked()
		{
			std::error_code ec;
			const fs::path path = FilePath();
			const fs::path temp = path.string() + ".tmp";
			{
				std::ofstream out(temp, std::ios::trunc);
				if (!out)
					return;
				for (const auto& line : s_lines)
					out << line << '\n';
			}
			fs::rename(temp, path, ec);
		}
	}

	void Record(Kind kind, uint64_t wantedBytes, const std::string& detail)
	{
		HostTuning::Count(HostTuning::GetStats().faultsRecorded);
		if (!HostTuning::Get().keepGpuFaultHistory.load(std::memory_order_relaxed))
			return;
		std::lock_guard lock(s_mutex);
		uint32_t& count = s_sessionCounts[(size_t)kind];
		if (++count > kWrittenPerKindPerSession)
			return;

		char when[32] = "?";
		const std::time_t now = std::time(nullptr);
		if (std::tm local{}; localtime_r(&now, &local))
			std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &local);
		uint64_t availableMB = 0;
#if BOOST_OS_IOS
		availableMB = (uint64_t)os_proc_available_memory() / (1024 * 1024);
#endif
		std::string line = fmt::format("{} | title {:016x} | {} | {}", when, CafeSystem::GetForegroundTitleId(), KindName(kind), detail);
		if (wantedBytes)
			line += fmt::format(" | wanted {} KB", (wantedBytes + 1023) / 1024);
		line += fmt::format(" | {} MB free", availableMB);
		if (count == kWrittenPerKindPerSession)
			line += " | further ones this session are not written";

		LoadLocked();
		s_lines.push_back(std::move(line));
		while (s_lines.size() > kKeptOnDisk)
			s_lines.pop_front();
		// Written right away: the next thing after a failed allocation is often the app being killed.
		SaveLocked();
	}

	std::string Describe(size_t maxEntries)
	{
		std::lock_guard lock(s_mutex);
		LoadLocked();
		std::string text;
		size_t written = 0;
		for (auto it = s_lines.rbegin(); it != s_lines.rend() && written < maxEntries; ++it, ++written)
		{
			text += *it;
			text += '\n';
		}
		return text;
	}

	void Clear()
	{
		std::lock_guard lock(s_mutex);
		s_loaded = true;
		s_lines.clear();
		s_sessionCounts = {};
		std::error_code ec;
		fs::remove(FilePath(), ec);
	}
}

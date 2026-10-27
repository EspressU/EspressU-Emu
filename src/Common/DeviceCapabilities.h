#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#include <sys/sysctl.h>
#endif

#if defined(__APPLE__) && TARGET_OS_IPHONE
#define CEMU_DEVICECAPS_IOS 1
#else
#define CEMU_DEVICECAPS_IOS 0
#endif

namespace DeviceCaps
{
	enum class Tier : uint8_t { Low = 0, Standard = 1, High = 2 };

	enum class ScreenClass : uint8_t
	{
		Unknown = 0,
		PhoneCompact = 1,
		Phone = 2,
		Pad = 3,
		PadLarge = 4,
	};

	struct Info
	{
		char machine[32] = {};
		char chipFamily[24] = {};
		char chipSeries = 0;
		int chipNumber = 0;

		bool gpuKnown = false;
		int appleGpuFamily = 0;
		bool metal3 = false;
		bool meshShaders = false;
		bool bcTextures = false;
		bool astcHdr = false;
		uint64_t maxBufferLength = 0;
		uint64_t recommendedMaxWorkingSet = 0;

		uint64_t physicalMemory = 0;
		uint64_t availableAtLaunch = 0;
		uint32_t logicalCores = 0;
		uint32_t perfCores = 0;
		uint32_t effCores = 0;

		ScreenClass screen = ScreenClass::Unknown;
		uint32_t screenShortPoints = 0;
		uint32_t screenLongPoints = 0;
		uint32_t screenScale = 0;
		bool isPad = false;

		Tier tier() const
		{
			constexpr uint64_t GiB = 1024ull * 1024ull * 1024ull;
			if (physicalMemory == 0)
				return Tier::Standard;
			if (physicalMemory < (9 * GiB) / 2)
				return Tier::Low;
			if (physicalMemory < 7 * GiB)
				return Tier::Standard;
			return Tier::High;
		}

		bool isHighEndSoc() const
		{
			if (chipSeries == 'M')
				return true;
			return chipSeries == 'A' && chipNumber >= 17;
		}
	};

	inline const char* TierName(Tier t)
	{
		return t == Tier::Low ? "low" : (t == Tier::High ? "high" : "standard");
	}

	inline const char* ScreenClassName(ScreenClass s)
	{
		switch (s)
		{
		case ScreenClass::PhoneCompact: return "phone-compact";
		case ScreenClass::Phone: return "phone";
		case ScreenClass::Pad: return "pad";
		case ScreenClass::PadLarge: return "pad-large";
		default: return "unknown";
		}
	}

	struct Budgets
	{
		uint64_t bufferCacheBytes = 164ull * 1024 * 1024;
		uint64_t stagingChunkBytes = 32ull * 1024 * 1024;
		uint64_t textureReadbackBytes = 32ull * 1024 * 1024;
		uint64_t evictLowFloorBytes = 600ull * 1024 * 1024;
		uint64_t evictCriticalFloorBytes = 400ull * 1024 * 1024;
		uint64_t minBootHeadroomBytes = 0;
		bool multicoreViable = true;
	};

	inline uint32_t BudgetScalePercent(Tier tier, uint64_t availableAtLaunch)
	{
		constexpr uint64_t MB = 1024ull * 1024ull;
		uint64_t referenceMB = 0;
		uint32_t maxPercent = 100;
		switch (tier)
		{
		case Tier::Low: return 100;
		case Tier::Standard: referenceMB = 4608; maxPercent = 150; break;
		case Tier::High: referenceMB = 4096; maxPercent = 200; break;
		}
		if (availableAtLaunch == 0)
			return 100;
		const uint64_t percent = availableAtLaunch / MB * 100 / referenceMB;
		return (uint32_t)std::min<uint64_t>(maxPercent, std::max<uint64_t>(100, percent));
	}

	inline Budgets ComputeBudgets(const Info& info)
	{
		Budgets b;
#if CEMU_DEVICECAPS_IOS
		constexpr uint64_t MB = 1024ull * 1024ull;
		switch (info.tier())
		{
		case Tier::Low:
			b.bufferCacheBytes = 128 * MB;
			b.stagingChunkBytes = 16 * MB;
			b.multicoreViable = false;
			break;
		case Tier::Standard:
			break;
		case Tier::High:
			b.bufferCacheBytes = 256 * MB;
			b.stagingChunkBytes = 64 * MB;
			b.textureReadbackBytes = 64 * MB;
			break;
		}
		const uint64_t floorCache = b.bufferCacheBytes;
		const uint64_t floorStaging = b.stagingChunkBytes;
		const uint32_t pct = BudgetScalePercent(info.tier(), info.availableAtLaunch);
		if (pct > 100)
		{
			auto scaled = [&](uint64_t v, uint64_t quantum) { return std::max<uint64_t>(v, v * pct / 100 / quantum * quantum); };
			uint64_t cache = scaled(b.bufferCacheBytes, 16 * MB);
			if (info.maxBufferLength != 0)
				cache = std::min<uint64_t>(cache, std::max<uint64_t>(floorCache, info.maxBufferLength / 8));
			b.bufferCacheBytes = cache;
			b.stagingChunkBytes = scaled(b.stagingChunkBytes, 8 * MB);
			b.textureReadbackBytes = scaled(b.textureReadbackBytes, 8 * MB);
			b.evictLowFloorBytes = scaled(b.evictLowFloorBytes, 50 * MB);
			b.evictCriticalFloorBytes = scaled(b.evictCriticalFloorBytes, 50 * MB);
		}
		if (info.maxBufferLength != 0)
			b.bufferCacheBytes = std::min<uint64_t>(b.bufferCacheBytes, std::max<uint64_t>(64 * MB, info.maxBufferLength));
		if (info.logicalCores != 0 && info.logicalCores < 3)
			b.multicoreViable = false;
		b.minBootHeadroomBytes = std::min<uint64_t>(b.bufferCacheBytes, floorCache) + floorStaging + 192 * MB;
#endif
		return b;
	}

	inline bool EvictionMarks(const Budgets& b, uint64_t startAvailable, uint64_t& lowMark, uint64_t& criticalMark)
	{
		constexpr uint64_t MB = 1024ull * 1024ull;
		const bool small = startAvailable < 1536 * MB;
		const bool large = startAvailable >= 3072 * MB;
		const uint64_t lowPercent = small ? 45 : (large ? 20 : 35);
		const uint64_t criticalPercent = small ? 25 : (large ? 12 : 20);
		lowMark = std::max<uint64_t>(startAvailable * lowPercent / 100, std::min<uint64_t>(b.evictLowFloorBytes, startAvailable / 2));
		criticalMark = std::max<uint64_t>(startAvailable * criticalPercent / 100, std::min<uint64_t>(b.evictCriticalFloorBytes, startAvailable * 30 / 100));
		return small;
	}

	namespace detail
	{
		inline Info& Storage()
		{
			static Info s_info;
			return s_info;
		}
		inline bool& Published()
		{
			static bool s_published = false;
			return s_published;
		}

#if defined(__APPLE__)
		inline uint64_t SysctlU64(const char* name)
		{
			uint64_t v = 0;
			size_t len = sizeof(v);
			if (sysctlbyname(name, &v, &len, nullptr, 0) == 0 && len == sizeof(v))
				return v;
			uint32_t v32 = 0;
			len = sizeof(v32);
			if (sysctlbyname(name, &v32, &len, nullptr, 0) == 0)
				return v32;
			return 0;
		}
		inline void SysctlString(const char* name, char* out, size_t outSize)
		{
			size_t len = outSize;
			out[0] = 0;
			if (sysctlbyname(name, out, &len, nullptr, 0) != 0)
				out[0] = 0;
			out[outSize - 1] = 0;
		}
#endif
	}

	inline Info FromSysctl()
	{
		Info info;
#if defined(__APPLE__)
		detail::SysctlString("hw.machine", info.machine, sizeof(info.machine));
		info.physicalMemory = detail::SysctlU64("hw.memsize");
		info.logicalCores = (uint32_t)detail::SysctlU64("hw.ncpu");
		info.perfCores = (uint32_t)detail::SysctlU64("hw.perflevel0.logicalcpu");
		info.effCores = (uint32_t)detail::SysctlU64("hw.perflevel1.logicalcpu");
		info.isPad = std::strncmp(info.machine, "iPad", 4) == 0;
#endif
		return info;
	}

	inline void Publish(const Info& info)
	{
		detail::Storage() = info;
		detail::Published() = true;
	}

	inline bool IsPublished() { return detail::Published(); }

	inline const Info& Get()
	{
		if (!detail::Published())
		{
			static const Info s_fallback = FromSysctl();
			return s_fallback;
		}
		return detail::Storage();
	}

	inline const Budgets& GetBudgets()
	{
		static const Budgets s_budgets = ComputeBudgets(Get());
		return s_budgets;
	}

	inline std::string Line(const Info& info)
	{
		char buf[640];
		std::snprintf(buf, sizeof(buf),
			"DEVICE %s | chip %s | tier %s | RAM %llu MB, %llu MB free at launch | cores %u (%u perf + %u eff) | "
			"GPU apple%d%s, max buffer %llu MB, working set %llu MB, BC %s, ASTC-HDR %s, mesh shaders %s | screen %s %ux%u pt @%ux",
			info.machine[0] ? info.machine : "unknown",
			info.chipFamily[0] ? info.chipFamily : "unknown",
			TierName(info.tier()),
			(unsigned long long)(info.physicalMemory / (1024 * 1024)),
			(unsigned long long)(info.availableAtLaunch / (1024 * 1024)),
			info.logicalCores, info.perfCores, info.effCores,
			info.appleGpuFamily, info.metal3 ? "+metal3" : "",
			(unsigned long long)(info.maxBufferLength / (1024 * 1024)),
			(unsigned long long)(info.recommendedMaxWorkingSet / (1024 * 1024)),
			info.gpuKnown ? (info.bcTextures ? "native" : "via ASTC") : "unknown",
			info.gpuKnown ? (info.astcHdr ? "yes" : "no") : "unknown",
			info.gpuKnown ? (info.meshShaders ? "yes" : "no") : "unknown",
			ScreenClassName(info.screen), info.screenLongPoints, info.screenShortPoints, info.screenScale);
		return std::string(buf);
	}
}

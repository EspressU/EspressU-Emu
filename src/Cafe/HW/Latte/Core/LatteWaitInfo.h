#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

namespace LatteWait
{
	enum class Kind : uint8_t
	{
		None = 0,
		GuestIdle = 1,
		GuestWait = 2,
		Gpu = 3,
		Display = 4,
	};

	struct State
	{
		std::atomic<const char*> reason{nullptr};
		std::atomic<int64_t> reasonSinceMs{0};
		std::atomic<uint8_t> reasonKind{0};

		std::atomic<uint32_t> timeouts{0};
		std::atomic<const char*> lastTimeoutReason{nullptr};
		std::atomic<bool> gpuPresumedLost{false};

		std::atomic<uint32_t> lastPM4Opcode{0};
		std::atomic<uint32_t> pm4Count{0};

		std::atomic<uint32_t> queriesInFlight{0};
		std::atomic<uint32_t> readbacksPending{0};
		std::atomic<uint32_t> executingCommandBuffers{0};
		std::atomic<uint32_t> erroredCommandBuffers{0};
		std::atomic<uint32_t> cbSubmitted{0};
		std::atomic<uint32_t> cbRetired{0};
		std::atomic<uint32_t> cbErrorStreak{0};
		std::atomic<int32_t> cbLastErrorCode{0};

		std::atomic<bool> gpuError{false};
		std::atomic<int32_t> gpuErrorCode{0};

		std::atomic<uint32_t> memDeviceMB{0};
		std::atomic<uint32_t> memHostMappedMB{0};
		std::atomic<uint32_t> memTextureCount{0};
		std::atomic<uint32_t> memTextureMB{0};
		std::atomic<uint32_t> memStagingMB{0};
		std::atomic<uint32_t> memIndexMB{0};
		std::atomic<uint32_t> memSnapshotMB{0};
		std::atomic<uint32_t> memBufferCacheMB{0};
		std::atomic<uint32_t> memXfbMB{0};
		std::atomic<uint32_t> memReadbackMB{0};
		std::atomic<uint32_t> texturesEvicted{0};
		std::atomic<bool> evictionRequested{false};
		std::atomic<uint32_t> evictionPasses{0};
		std::atomic<bool> memStatsValid{false};

		std::atomic<uint32_t> presentedFrames{0};
		std::atomic<uint32_t> drawableFailures{0};
		std::atomic<uint32_t> drawableFailuresInARow{0};
		std::atomic<bool> tvDrawableHeld{false};
		std::atomic<uint32_t> tvDrawableWidth{0};
		std::atomic<uint32_t> tvDrawableHeight{0};
		std::atomic<bool> tvLayerAttached{false};
		std::atomic<bool> tvLayerHasDevice{false};
	};

	inline State& Get()
	{
		static State s_state;
		return s_state;
	}

	inline int64_t NowMs()
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	inline void Set(const char* reason, Kind kind = Kind::Gpu)
	{
		auto& s = Get();
		if (s.reason.load(std::memory_order_relaxed) != reason)
		{
			s.reasonSinceMs.store(NowMs(), std::memory_order_relaxed);
			s.reasonKind.store((uint8_t)kind, std::memory_order_relaxed);
			s.reason.store(reason, std::memory_order_relaxed);
		}
	}

	inline void Clear()
	{
		auto& s = Get();
		if (s.reason.load(std::memory_order_relaxed) != nullptr)
		{
			s.reason.store(nullptr, std::memory_order_relaxed);
			s.reasonKind.store((uint8_t)Kind::None, std::memory_order_relaxed);
		}
	}

	inline void ResetAll()
	{
		auto& s = Get();
		s.reason.store(nullptr);
		s.reasonSinceMs.store(0);
		s.reasonKind.store(0);
		s.timeouts.store(0);
		s.lastTimeoutReason.store(nullptr);
		s.gpuPresumedLost.store(false);
		s.lastPM4Opcode.store(0);
		s.pm4Count.store(0);
		s.queriesInFlight.store(0);
		s.readbacksPending.store(0);
		s.executingCommandBuffers.store(0);
		s.erroredCommandBuffers.store(0);
		s.cbSubmitted.store(0);
		s.cbRetired.store(0);
		s.cbErrorStreak.store(0);
		s.cbLastErrorCode.store(0);
		s.gpuError.store(false);
		s.gpuErrorCode.store(0);
		s.memDeviceMB.store(0);
		s.memHostMappedMB.store(0);
		s.memTextureCount.store(0);
		s.memTextureMB.store(0);
		s.memStagingMB.store(0);
		s.memIndexMB.store(0);
		s.memSnapshotMB.store(0);
		s.memBufferCacheMB.store(0);
		s.memXfbMB.store(0);
		s.memReadbackMB.store(0);
		s.texturesEvicted.store(0);
		s.evictionRequested.store(false);
		s.evictionPasses.store(0);
		s.memStatsValid.store(false);
		s.presentedFrames.store(0);
		s.drawableFailures.store(0);
		s.drawableFailuresInARow.store(0);
		s.tvDrawableHeld.store(false);
		s.tvDrawableWidth.store(0);
		s.tvDrawableHeight.store(0);
		s.tvLayerAttached.store(false);
		s.tvLayerHasDevice.store(false);
	}

	inline void NotePM4(uint32_t opcode)
	{
		auto& s = Get();
		s.lastPM4Opcode.store(opcode, std::memory_order_relaxed);
		s.pm4Count.store(s.pm4Count.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
	}

	struct WaitTotal
	{
		std::atomic<const char*> reason{nullptr};
		std::atomic<uint64_t> ns{0};
		std::atomic<uint32_t> count{0};
	};
	inline WaitTotal* WaitTotals()
	{
		static WaitTotal s_totals[32];
		return s_totals;
	}
	inline void AddWaitTime(const char* reason, uint64_t ns)
	{
		if (!reason)
			return;
		WaitTotal* totals = WaitTotals();
		for (int i = 0; i < 32; i++)
		{
			const char* current = totals[i].reason.load(std::memory_order_relaxed);
			if (current == nullptr)
			{
				const char* expected = nullptr;
				if (!totals[i].reason.compare_exchange_strong(expected, reason) && expected != reason)
					continue;
				current = reason;
			}
			if (current == reason)
			{
				totals[i].ns.fetch_add(ns, std::memory_order_relaxed);
				totals[i].count.fetch_add(1, std::memory_order_relaxed);
				return;
			}
		}
	}

	struct Scope
	{
		explicit Scope(const char* reason, Kind kind = Kind::Gpu)
			: m_previous(Get().reason.load(std::memory_order_relaxed)), m_previousKind((Kind)Get().reasonKind.load(std::memory_order_relaxed)),
			  m_reason(reason), m_start(std::chrono::steady_clock::now())
		{
			Set(reason, kind);
		}
		~Scope()
		{
			AddWaitTime(m_reason, (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - m_start).count());
			if (m_previous)
				Set(m_previous, m_previousKind);
			else
				Clear();
		}
		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;
	private:
		const char* m_previous;
		Kind m_previousKind;
		const char* m_reason;
		std::chrono::steady_clock::time_point m_start;
	};
}

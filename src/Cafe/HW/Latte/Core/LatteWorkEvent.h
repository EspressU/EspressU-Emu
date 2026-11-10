#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>

// Lets the GPU command thread sleep while it has nothing to do and wakes it as soon as work arrives
class LatteWorkEvent
{
public:
	std::size_t Snapshot()
	{
		return m_state.load(std::memory_order_acquire) & ~s_waiting;
	}

	void Notify()
	{
		if (m_state.fetch_add(2, std::memory_order_acq_rel) & s_waiting)
		{
			std::lock_guard lock(m_mutex);
			m_condition.notify_one();
		}
	}

	// sleeps until Notify() was called after Snapshot() returned generation, or until the deadline
	void WaitUntil(std::size_t generation, std::chrono::steady_clock::time_point deadline)
	{
		std::unique_lock lock(m_mutex);
		m_state.fetch_or(s_waiting, std::memory_order_acq_rel);
		m_condition.wait_until(lock, deadline, [&]() { return Snapshot() != generation; });
		m_state.fetch_and(~s_waiting, std::memory_order_release);
	}

private:
	std::mutex m_mutex;
	std::condition_variable m_condition;
	static constexpr std::size_t s_waiting = 1;
	std::atomic<std::size_t> m_state{};
};

inline LatteWorkEvent& Latte_GetWorkEvent()
{
	static LatteWorkEvent event;
	return event;
}

#include "IOSLiveLog.h"

#if defined(CEMU_PLATFORM_IOS)

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>

namespace
{
	constexpr size_t kMaxLines = 1024;
	constexpr size_t kMaxLineLength = 400;

	struct LiveLog
	{
		std::mutex mutex;
		std::deque<std::string> lines;
		uint64_t nextSequence = 0;
		uint64_t firstSequence = 0;
		std::chrono::steady_clock::time_point origin = std::chrono::steady_clock::now();
	};

	LiveLog& Get()
	{
		static LiveLog s_log;
		return s_log;
	}

	std::atomic<bool> s_enabled{true};
}

void ios_live_log_set_enabled(bool enabled)
{
	s_enabled.store(enabled, std::memory_order_relaxed);
}

bool ios_live_log_is_enabled(void)
{
	return s_enabled.load(std::memory_order_relaxed);
}

void ios_live_log_push(const char* line)
{
	if (!line || !s_enabled.load(std::memory_order_relaxed))
		return;

	LiveLog& log = Get();

	const auto now = std::chrono::steady_clock::now();
	const auto wall = std::chrono::system_clock::now();

	double elapsed;
	{
		std::lock_guard<std::mutex> lock(log.mutex);
		elapsed = std::chrono::duration<double>(now - log.origin).count();
	}

	const auto wallTime = std::chrono::system_clock::to_time_t(wall);
	std::tm wallParts{};
	localtime_r(&wallTime, &wallParts);
	const int millis = (int)std::chrono::duration_cast<std::chrono::milliseconds>(
		wall - std::chrono::time_point_cast<std::chrono::seconds>(wall)).count();

	char prefix[64];
	snprintf(prefix, sizeof(prefix), "%02d:%02d:%02d.%03d +%7.3fs ",
		wallParts.tm_hour, wallParts.tm_min, wallParts.tm_sec, millis, elapsed);

	std::string entry(prefix);

	const size_t inputLength = strlen(line);
	const size_t copyLength = inputLength > kMaxLineLength ? kMaxLineLength : inputLength;
	entry.reserve(entry.size() + copyLength + 16);
	for (size_t i = 0; i < copyLength; i++)
		entry.push_back((line[i] == '\n' || line[i] == '\r') ? ' ' : line[i]);
	if (inputLength > kMaxLineLength)
		entry.append(" [...]");

	std::lock_guard<std::mutex> lock(log.mutex);
	log.lines.emplace_back(std::move(entry));
	log.nextSequence++;
	while (log.lines.size() > kMaxLines)
	{
		log.lines.pop_front();
		log.firstSequence++;
	}
}

void ios_live_log_begin_run(void)
{
	LiveLog& log = Get();
	std::lock_guard<std::mutex> lock(log.mutex);
	log.lines.clear();
	log.origin = std::chrono::steady_clock::now();
	log.firstSequence = log.nextSequence;
}

const char* ios_live_log_drain(uint64_t* inoutCursor, uint64_t* outDropped)
{
	static thread_local std::string s_out;
	s_out.clear();
	if (outDropped)
		*outDropped = 0;
	if (!inoutCursor)
		return s_out.c_str();

	LiveLog& log = Get();
	std::lock_guard<std::mutex> lock(log.mutex);

	uint64_t cursor = *inoutCursor;
	if (cursor < log.firstSequence)
	{
		if (outDropped)
			*outDropped = log.firstSequence - cursor;
		cursor = log.firstSequence;
	}
	else if (cursor > log.nextSequence)
	{
		cursor = log.firstSequence;
	}

	for (uint64_t seq = cursor; seq < log.nextSequence; seq++)
	{
		if (!s_out.empty())
			s_out.push_back('\n');
		s_out.append(log.lines[(size_t)(seq - log.firstSequence)]);
	}

	*inoutCursor = log.nextSequence;
	return s_out.c_str();
}

#else

void ios_live_log_set_enabled(bool) {}
bool ios_live_log_is_enabled(void) { return false; }
void ios_live_log_push(const char*) {}
void ios_live_log_begin_run(void) {}
const char* ios_live_log_drain(uint64_t*, uint64_t* outDropped)
{
	if (outDropped)
		*outDropped = 0;
	return "";
}

#endif

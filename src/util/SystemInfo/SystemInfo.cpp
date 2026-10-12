#include "util/SystemInfo/SystemInfo.h"

uint64 ProcessorTime::work() const
{
	return user + kernel;
}

uint64 ProcessorTime::total() const
{
	return idle + user + kernel;
}

double ProcessorTime::Compare(const ProcessorTime& last, const ProcessorTime& now)
{
	if (now.work() < last.work() || now.total() <= last.total())
		return 0.0;

	auto dwork = now.work() - last.work();
	auto dtotal = now.total() - last.total();

	return (double)dwork / dtotal;
}

uint32 GetProcessorCount()
{
	return std::max(1u, std::thread::hardware_concurrency());
}

void QueryProcTime(ProcessorTime &out)
{
	uint64 now, user, kernel;
	QueryProcTime(now, user, kernel);

	out.idle = now - (user + kernel);
	out.kernel = kernel;
	out.user = user;
}

#pragma once

struct ProcessorTime
{
	uint64 idle{}, kernel{}, user{};

	uint64 work() const;
	uint64 total() const;

	static double Compare(const ProcessorTime& last, const ProcessorTime& now);
};

uint32 GetProcessorCount();
uint64 QueryRamUsage();
void QueryProcTime(uint64 &out_now, uint64 &out_user, uint64 &out_kernel);
void QueryProcTime(ProcessorTime &out);
void QueryCoreTimes(std::vector<ProcessorTime>& out);

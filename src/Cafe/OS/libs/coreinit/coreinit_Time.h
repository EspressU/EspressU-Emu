#pragma once
#include "Cafe/HW/Espresso/Const.h"

namespace coreinit
{
	typedef struct
	{
		 sint32be second;
		 sint32be minute;
		 sint32be hour;
		 sint32be dayOfMonth;
		 sint32be month;
		 sint32be year;
		 sint32be dayOfWeek;
		 sint32be dayOfYear;
		 sint32be millisecond;
		 sint32be microsecond;
	}OSCalendarTime_t;

	static_assert(sizeof(OSCalendarTime_t) == 0x28);

	namespace EspressoTime
	{
		typedef sint64 TimerTicks;

		constexpr sint64 GetCoreClock()
		{
			return Espresso::CORE_CLOCK;
		}

		constexpr sint64 GetBusClock()
		{
			return Espresso::BUS_CLOCK;
		}

		constexpr sint64 GetTimerClock()
		{
			return Espresso::TIMER_CLOCK;
		}

		inline TimerTicks ConvertNsToTimerTicks(uint64 ns)
		{
			return static_cast<TimerTicks>((static_cast<uint64>(GetTimerClock()) / 31250ULL) * (ns) / 32000ULL);
		}

		inline TimerTicks ConvertMsToTimerTicks(uint64 ms)
		{
			return static_cast<TimerTicks>(ms * static_cast<uint64>(GetTimerClock()) / 1000ULL);
		}
	};

	void OSTicksToCalendarTime(uint64 ticks, OSCalendarTime_t* calenderStruct);

	uint64 OSGetSystemTime();
	uint64 OSGetTime();
	uint32 OSGetSystemTick();
	uint32 OSGetTick();

	void InitializeTimeAndCalendar();
};

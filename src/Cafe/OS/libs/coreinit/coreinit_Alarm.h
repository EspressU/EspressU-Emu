#pragma once
#include "Cafe/OS/libs/coreinit/coreinit_Thread.h"

namespace coreinit
{
	class OSHostAlarm;
	OSHostAlarm* OSHostAlarmCreate(uint64 nextFire, uint64 period, void(*callbackFunc)(uint64 currentTick, void* context), void* context);
	void OSHostAlarmDestroy(OSHostAlarm* hostAlarm);

	struct OSAlarm_t
	{
		 betype<uint32>  magic;
		 MEMPTR<const char> name;
		 uint32	        ukn08;
		 MPTR			handler;
		 uint32          ukn10;
		 uint32			padding14;
		 uint64	        nextTime;
		 MPTR			prev;
		 MPTR			next;
		 uint64          period;
		 uint64          startTime;
		 uint32be        userData;
		 uint32		    ukn3C;
		 OSThreadQueue   uknThreadQueue;
		 MPTR			alarmQueue;
		 MPTR			ukn54;

		void setMagic()
		{
			magic = (uint32)'aLrM';
		}

		bool checkMagic()
		{
			return magic == (uint32)'aLrM';
		}
	};

	static_assert(sizeof(OSAlarm_t) == 0x58);

	void OSCreateAlarm(OSAlarm_t* alarm);
	bool OSCancelAlarm(OSAlarm_t* alarm);
	void OSSetAlarm(OSAlarm_t* alarm, uint64 time, MPTR handlerFunc);
	void OSSetAlarmUserData(OSAlarm_t* alarm, uint32 userData);
	void OSSetPeriodicAlarm(OSAlarm_t* OSAlarm, uint64 startTick, uint64 periodTick, MPTR OSAlarmHandler);

	void OSAlarm_Shutdown();

	void alarm_update();

	void MapAlarmExports();
	void InitializeAlarm();
}
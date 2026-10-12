#include "Cafe/CafeSystem.h"
#include "Cafe/OS/libs/coreinit/coreinit_Thread.h"
#include "Cafe/OS/libs/coreinit/coreinit_Scheduler.h"
#include "Cemu/Logging/CemuLogging.h"

#include <atomic>
#include <mutex>

static std::atomic_bool sTitlePaused{false};
static std::mutex sPauseMutex;

bool IOSTitlePause_Pause()
{
	sint32 suspended;
	{
		std::lock_guard<std::mutex> lock(sPauseMutex);
		if (!CafeSystem::IsTitleRunning() || sTitlePaused.load())
			return false;
		__OSLockScheduler();
		for (sint32 i = 0; i < activeThreadCount; i++)
		{
			auto thread = reinterpret_cast<OSThread_t*>(memory_getPointerFromVirtualOffset(activeThread[i]));
			coreinit::__OSSuspendThreadNolock(thread);
		}
		suspended = activeThreadCount;
		__OSUnlockScheduler();
		sTitlePaused.store(true);
	}
	cemuLog_log(LogType::Force, "iOS: title paused ({} guest threads suspended)", suspended);
	return true;
}

bool IOSTitlePause_Resume()
{
	{
		std::lock_guard<std::mutex> lock(sPauseMutex);
		if (!sTitlePaused.exchange(false))
			return false;
		if (!CafeSystem::IsTitleRunning())
			return false;
		__OSLockScheduler();
		for (sint32 i = 0; i < activeThreadCount; i++)
		{
			auto thread = reinterpret_cast<OSThread_t*>(memory_getPointerFromVirtualOffset(activeThread[i]));
			coreinit::__OSResumeThreadInternal(thread, 1);
		}
		__OSUnlockScheduler();
	}
	cemuLog_log(LogType::Force, "iOS: title resumed");
	return true;
}

bool IOSTitlePause_IsPaused()
{
	return sTitlePaused.load();
}

void IOSTitlePause_Forget()
{
	sTitlePaused.store(false);
}

#include "Cafe/OS/common/OSCommon.h"
#include "Cafe/OS/libs/coreinit/coreinit_MPQueue.h"
#include "Cafe/OS/libs/coreinit/coreinit_Time.h"
#include "Cafe/HW/Espresso/PPCCallback.h"
#include "util/helpers/fspinlock.h"

#define AcquireMPQLock() s_workaroundSpinlock.lock()
#define ReleaseMPQLock() s_workaroundSpinlock.unlock()

namespace coreinit
{

	FSpinlock s_workaroundSpinlock;

	void MPInitTask(MPTask* task, void* func, void* data, uint32 size)
	{
		s_workaroundSpinlock.lock();
		task->thisptr = task;

		task->coreIndex = PPC_CORE_COUNT;

		task->taskFunc.func = func;
		task->taskFunc.data = data;
		task->taskFunc.size = size;

		task->taskState = MP_TASK_STATE_INIT;

		task->userdata = nullptr;
		task->runtime = 0;
		s_workaroundSpinlock.unlock();
	}

	bool MPTermTask(MPTask* task)
	{
		return true;
	}

	bool MPRunTask(MPTask* task)
	{
		if (task->taskState != MP_TASK_STATE_READY)
			return false;

		auto* taskQ = task->taskQ.GetPtr();

		if(taskQ->state != MP_TASKQ_STATE_STOPPING && taskQ->state != MP_TASKQ_STATE_STOP)
		{
			AcquireMPQLock();
			if (taskQ->state == MP_TASKQ_STATE_STOPPING || taskQ->state == MP_TASKQ_STATE_STOP)
			{
				ReleaseMPQLock();
				return false;
			}

			taskQ->taskReadyCount = taskQ->taskReadyCount - 1;
			taskQ->taskRunCount = taskQ->taskRunCount + 1;
			ReleaseMPQLock();

			const auto startTime = OSGetSystemTime();

			task->coreIndex = OSGetCoreId();
			task->taskState = MP_TASK_STATE_RUN;

			task->taskFunc.result = PPCCoreCallback(task->taskFunc.func, task->taskFunc.data, task->taskFunc.size);

			task->taskState = MP_TASK_STATE_DONE;

			const auto endTime = OSGetSystemTime();
			task->runtime = endTime - startTime;

			cemu_assert_debug(taskQ->state != MP_TASKQ_STATE_DONE);

			AcquireMPQLock();
			taskQ->taskRunCount = taskQ->taskRunCount - 1;
			taskQ->taskDoneCount[1] = taskQ->taskDoneCount[1] + 1;
			if (taskQ->state == MP_TASKQ_STATE_STOPPING && taskQ->taskRunCount == 0)
				taskQ->state = MP_TASKQ_STATE_STOP;

			if (taskQ->taskCount == taskQ->taskDoneCount[1])
				taskQ->state = MP_TASKQ_STATE_DONE;

			ReleaseMPQLock();

			return true;
		}

		return false;
	}

	bool MPGetTaskInfo(MPTask* task, MPTaskInfo* info)
	{
		info->state = task->taskState;
		info->coreIndex = task->coreIndex;
		info->runtime = task->runtime;
		return true;
	}

	void* MPGetTaskUserData(MPTask* task)
	{
		return task->userdata.GetPtr();
	}

	void MPSetTaskUserData(MPTask* task, void* userdata)
	{
		task->userdata = userdata;
	}

	void MPInitTaskQ(MPTaskQ* taskQ, MPTask** tasks, uint32 taskCount)
	{
		AcquireMPQLock();
		taskQ->thisptr = taskQ;
		OSInitSpinLock(&taskQ->spinlock);
		taskQ->state = MP_TASKQ_STATE_INIT;
		taskQ->taskReadyCount = 0;
		taskQ->taskCount = 0;
		taskQ->taskRunCount = 0;
		for(uint32 i = 0; i < OSGetCoreCount(); ++i)
		{
			taskQ->taskDoneCount[i] = 0;
			taskQ->nextIndex[i] = 0;
			taskQ->endIndex[i] = 0;
		}

		taskQ->taskQueue = (MEMPTR<MPTask>*)tasks;
		taskQ->taskQueueSize = taskCount;

		ReleaseMPQLock();
	}

	bool MPEnqueTask(MPTaskQ* taskq, MPTask* task)
	{
		bool result = false;
		if(task->taskState == MP_TASK_STATE_INIT)
		{
			AcquireMPQLock();

			const uint32 taskQState = taskq->state;
			if((uint32)taskq->endIndex[1] < taskq->taskQueueSize
				&& (taskQState == MP_TASKQ_STATE_INIT || taskQState == MP_TASKQ_STATE_RUN || taskQState == MP_TASKQ_STATE_STOPPING || taskQState == MP_TASKQ_STATE_STOP || taskQState == MP_TASKQ_STATE_DONE))
			{
				task->taskQ = taskq;
				task->taskState = MP_TASK_STATE_READY;

				taskq->thisptr = taskq;

				const uint32 endIndex = taskq->endIndex[1];
				taskq->endIndex[1] = endIndex + 1;

				taskq->taskCount = taskq->taskCount + 1;
				taskq->taskReadyCount = taskq->taskReadyCount + 1;
				taskq->taskQueue[endIndex] = task;

				if (taskQState == MP_TASKQ_STATE_DONE)
					taskq->state = MP_TASKQ_STATE_RUN;

				result = true;
			}

			ReleaseMPQLock();
		}

		return result;
	}

	bool MPTermTaskQ(MPTaskQ* taskq)
	{
		AcquireMPQLock();
		if (taskq->state == MP_TASKQ_STATE_RUN)
		{
			taskq->state = MP_TASKQ_STATE_STOP;
		}

		while (taskq->taskRunCount != 0)
		{
			ReleaseMPQLock();
			OSYieldThread();
			AcquireMPQLock();
		}
		ReleaseMPQLock();
		return true;
	}

	bool MPGetTaskQInfo(MPTaskQ* taskq, MPTaskQInfo* info)
	{
		AcquireMPQLock();
		info->state = taskq->state;
		info->taskCount = taskq->taskCount;
		info->taskReadyCount = taskq->taskReadyCount;
		info->taskRunCount = taskq->taskRunCount;
		info->taskDoneCount = taskq->taskDoneCount[1];
		ReleaseMPQLock();
		return true;
	}

	bool MPStartTaskQ(MPTaskQ* taskq)
	{
		bool result = false;
		AcquireMPQLock();
		if (taskq->state == MP_TASKQ_STATE_INIT || taskq->state == MP_TASKQ_STATE_STOP)
		{
			taskq->state = MP_TASKQ_STATE_RUN;
			result = true;
		}

		ReleaseMPQLock();
		return result;
	}

	bool MPRunTasksFromTaskQ(MPTaskQ* taskq, int granularity)
	{
		uint32 result = 0;
		while (true)
		{
			if (taskq->state != MP_TASKQ_STATE_RUN)
				return (taskq->state & MP_TASKQ_STATE_DONE) != 0;

			AcquireMPQLock();
			const auto nextIndex = taskq->nextIndex[1];
			const auto endIndex = taskq->endIndex[1];
			if (nextIndex == endIndex)
				break;

			auto newNextIndex = nextIndex + granularity;
			if (endIndex < nextIndex + granularity)
				newNextIndex = endIndex;

			const auto workCount = (newNextIndex - nextIndex);

			taskq->nextIndex[1] = newNextIndex;
			taskq->taskReadyCount = taskq->taskReadyCount - workCount;
			taskq->taskRunCount = taskq->taskRunCount + workCount;
			ReleaseMPQLock();

			if(result != 0)
				PPCCore_switchToScheduler();

			for (int i = nextIndex; i < newNextIndex; ++i)
			{
				const auto startTime = OSGetSystemTime();

				const auto& task = taskq->taskQueue[i];
				result = task->thisptr.GetMPTR();

				task->taskState = MP_TASK_STATE_RUN;
				task->coreIndex = OSGetCoreId();

				task->taskFunc.result = PPCCoreCallback(task->taskFunc.func, task->taskFunc.data, task->taskFunc.size);

				task->taskState = MP_TASK_STATE_DONE;

				const auto endTime = OSGetSystemTime();
				task->runtime = endTime - startTime;
			}

			AcquireMPQLock();
			const auto runRemaining = taskq->taskRunCount - workCount;
			taskq->taskRunCount = runRemaining;

			const auto doneCount = taskq->taskDoneCount[1] + workCount;
			taskq->taskDoneCount[1] = doneCount;

			if (taskq->state == 4 && runRemaining == 0)
				taskq->state = MP_TASKQ_STATE_STOP;

			if (taskq->taskCount == doneCount)
				taskq->state = MP_TASKQ_STATE_DONE;

			ReleaseMPQLock();
		}

		ReleaseMPQLock();
		return result != 0;
	}

	bool MPStopTaskQ(MPTaskQ* taskq)
	{
		bool result = false;
		AcquireMPQLock();
		if (taskq->state == MP_TASKQ_STATE_RUN)
		{
			taskq->state = MP_TASKQ_STATE_STOPPING;
			if (taskq->taskRunCount == 0)
				taskq->state = MP_TASKQ_STATE_STOP;

			result = true;
		}
		ReleaseMPQLock();
		return result;
	}

	bool MPWaitTaskQ(MPTaskQ* taskQ, uint32 waitState)
	{
		bool waitRun = (waitState & MP_TASKQ_STATE_RUN) != 0;
		bool waitStop = (waitState & MP_TASKQ_STATE_STOP) != 0;
		bool waitDone = (waitState & MP_TASKQ_STATE_DONE) != 0;

		size_t loopCounter = 0;
		while (waitStop || waitDone || waitRun)
		{
			const uint32 state = taskQ->state;
			if (waitRun && HAS_FLAG(state, MP_TASKQ_STATE_RUN))
			{
				waitRun = false;
				waitDone = false;
				continue;
			}

			if (waitStop && HAS_FLAG(state, MP_TASKQ_STATE_STOP))
			{
				waitStop = false;
				waitDone = false;
				continue;
			}

			if (waitDone && HAS_FLAG(state, MP_TASKQ_STATE_DONE))
			{
				waitDone = false;
				waitRun = false;
				waitStop = false;
				continue;
			}
			if (loopCounter > 0)
				coreinit::OSSleepTicks(EspressoTime::ConvertNsToTimerTicks(50000));
			else
				PPCCore_switchToScheduler();
			loopCounter++;
		}
		return true;
	}

	bool MPWaitTaskQWithTimeout(MPTaskQ* taskQ, uint32 waitState, sint64 timeout)
	{
		bool waitRun = (waitState & MP_TASKQ_STATE_RUN) != 0;
		bool waitStop = (waitState & MP_TASKQ_STATE_STOP) != 0;
		bool waitDone = (waitState & MP_TASKQ_STATE_DONE) != 0;

		const auto startTime = OSGetSystemTime();
		const auto timerTicks = EspressoTime::ConvertNsToTimerTicks(timeout);
		const auto endTime = startTime + timerTicks;

		while (waitStop || waitDone || waitRun)
		{
			const uint32 state = taskQ->state;
			if (waitRun && HAS_FLAG(state, MP_TASKQ_STATE_RUN))
			{
				waitRun = false;
				waitDone = false;
				continue;
			}

			if (waitStop && HAS_FLAG(state, MP_TASKQ_STATE_STOP))
			{
				waitStop = false;
				waitDone = false;
				continue;
			}

			if (waitDone && HAS_FLAG(state, MP_TASKQ_STATE_DONE))
			{
				waitDone = false;
				waitRun = false;
				waitStop = false;
				continue;
			}

			if (OSGetSystemTime() >= endTime)
			{
				if (waitState == MP_TASKQ_STATE_DONE)
					cemuLog_log(LogType::Force, "MPWaitTaskQWithTimeout(): Timeout occurred while waiting for done-only state");
				return false;
			}

			PPCCore_switchToScheduler();
		}
		return true;
	}

	MPTask* MPDequeTask(MPTaskQ* taskq)
	{
		MPTask* result = nullptr;
		if (taskq->state == MP_TASKQ_STATE_RUN)
		{
			AcquireMPQLock();
			if (taskq->state == MP_TASKQ_STATE_RUN && taskq->nextIndex[1] != taskq->endIndex[1])
			{
				result = taskq->taskQueue[taskq->nextIndex[1]].GetPtr();
				taskq->nextIndex[1] = taskq->nextIndex[1] + 1;
			}
			ReleaseMPQLock();
		}
		return result;
	}

	uint32 MPDequeTasks(MPTaskQ* taskq, MPTask** tasks, sint32 maxTasks)
	{
		uint32 dequeCount = 0;
		if (taskq->state == MP_TASKQ_STATE_RUN)
		{
			AcquireMPQLock();
			if (taskq->state == MP_TASKQ_STATE_RUN)
			{
				auto nextIndex = (sint32)taskq->nextIndex[1];
				auto newEndIndex = nextIndex + maxTasks;
				if (taskq->endIndex[1] < nextIndex + maxTasks)
					newEndIndex = taskq->endIndex[1];

				dequeCount = newEndIndex - nextIndex;
				taskq->nextIndex[1] = newEndIndex;

				for(int i = 0; nextIndex < newEndIndex; ++nextIndex, ++i)
				{
					tasks[i] = taskq->taskQueue[nextIndex].GetPtr();
				}

				auto idx = 0;
				while (nextIndex < newEndIndex)
				{
					tasks[idx] = taskq->taskQueue[nextIndex].GetPtr();
					nextIndex = nextIndex + 1;
					idx = idx + 1;
				}
			}

			ReleaseMPQLock();
		}
		return dequeCount;
	}

	bool MPResetTaskQ(MPTaskQ* taskq)
	{
		debug_printf("MPResetTaskQ called\n");
		bool result = false;
		AcquireMPQLock();
		if (taskq->state == MP_TASKQ_STATE_DONE || taskq->state == MP_TASKQ_STATE_STOP)
		{
			taskq->state = MP_TASKQ_STATE_INIT;
			taskq->taskRunCount = 0;
			taskq->taskCount = taskq->endIndex[1];
			taskq->taskReadyCount = taskq->endIndex[1];

			for(uint32 i = 0; i < OSGetCoreCount(); ++i)
			{
				taskq->taskDoneCount[i] = 0;
				taskq->nextIndex[i] = 0;
			}

			for(uint32 i = 0; i < taskq->taskCount; ++i)
			{
				const auto& task = taskq->taskQueue[i];
				task->taskFunc.result = 0;

				task->coreIndex = PPC_CORE_COUNT;
				task->runtime = 0;
				task->taskState = MP_TASK_STATE_READY;
			}

			result = true;
		}

		ReleaseMPQLock();
		return result;
	}

	void InitializeMP()
	{
		cafeExportRegister("coreinit", MPInitTask, LogType::CoreinitMP);
		cafeExportRegister("coreinit", MPTermTask, LogType::CoreinitMP);
		cafeExportRegister("coreinit", MPRunTask, LogType::CoreinitMP);
		cafeExportRegister("coreinit", MPGetTaskInfo, LogType::CoreinitMP);
		cafeExportRegister("coreinit", MPGetTaskUserData, LogType::CoreinitMP);
		cafeExportRegister("coreinit", MPSetTaskUserData, LogType::CoreinitMP);

		cafeExportRegister("coreinit", MPInitTaskQ, LogType::CoreinitMP);
		cafeExportRegister("coreinit", MPResetTaskQ, LogType::CoreinitMP);

		cafeExportRegister("coreinit", MPEnqueTask, LogType::CoreinitMP);
		cafeExportRegister("coreinit", MPDequeTask, LogType::CoreinitMP);
		cafeExportRegister("coreinit", MPDequeTasks, LogType::CoreinitMP);

		cafeExportRegister("coreinit", MPRunTasksFromTaskQ, LogType::CoreinitMP);
		cafeExportRegister("coreinit", MPStartTaskQ, LogType::CoreinitMP);
		cafeExportRegister("coreinit", MPWaitTaskQ, LogType::CoreinitMP);
		cafeExportRegister("coreinit", MPWaitTaskQWithTimeout, LogType::CoreinitMP);
		cafeExportRegister("coreinit", MPStopTaskQ, LogType::CoreinitMP);

		cafeExportRegister("coreinit", MPTermTaskQ, LogType::CoreinitMP);
		cafeExportRegister("coreinit", MPGetTaskQInfo, LogType::CoreinitMP);
	}
}

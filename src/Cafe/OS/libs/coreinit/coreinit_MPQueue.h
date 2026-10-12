#pragma once

#include "Cafe/OS/libs/coreinit/coreinit.h"
#include "Cafe/OS/libs/coreinit/coreinit_Spinlock.h"

namespace coreinit
{
	enum MPTaskState
	{
		MP_TASK_STATE_INIT = (1 << 0),
		MP_TASK_STATE_READY = (1 << 1),
		MP_TASK_STATE_RUN = (1 << 2),
		MP_TASK_STATE_DONE = (1 << 3)
	};

	enum MPTaskQState
	{
		MP_TASKQ_STATE_INIT = (1 << 0),
		MP_TASKQ_STATE_RUN = (1 << 1),
		MP_TASKQ_STATE_STOPPING = (1 << 2),
		MP_TASKQ_STATE_STOP = (1 << 3),
		MP_TASKQ_STATE_DONE = (1 << 4)
	};

	struct MPTaskFunction
	{
		 MEMPTR<void> func;
		 MEMPTR<void> data;
		 uint32be size;
		 uint32be result;
	};
	static_assert(sizeof(MPTaskFunction) == 0x10);

#pragma pack(1)

	struct MPTask
	{
		 MEMPTR<void> thisptr;
		 MEMPTR<struct MPTaskQ> taskQ;
		 uint32be taskState;
		 MPTaskFunction taskFunc;
		 uint32be coreIndex;
		 sint64be runtime;
		 MEMPTR<void> userdata;
	};
	static_assert(sizeof(MPTask) == 0x2C);

#pragma pack()

	struct MPTaskQ
	{
		 MEMPTR<void> thisptr;
		 uint32be state;
		 uint32be taskCount;
		 uint32be taskReadyCount;
		 uint32be taskRunCount;
		 uint32be taskDoneCount[PPC_CORE_COUNT];
		 sint32be nextIndex[PPC_CORE_COUNT];
		 sint32be endIndex[PPC_CORE_COUNT];
		 MEMPTR<MEMPTR<MPTask>> taskQueue;
		 uint32be taskQueueSize;
			OSSpinLock spinlock;
	};
	static_assert(sizeof(MPTaskQ) == 0x50);

	struct MPTaskQInfo
	{
		 uint32be state;
		 uint32be taskCount;
		 uint32be taskReadyCount;
		 uint32be taskRunCount;
		 uint32be taskDoneCount;
	};
	static_assert(sizeof(MPTaskQInfo) == 0x14);

	struct MPTaskInfo
	{
		 uint32be state;
		 uint32be funcResult;
		 uint32be coreIndex;
		 sint64be runtime;
	};
	static_assert(sizeof(MPTaskQInfo) == 0x14);

	void MPInitTask(MPTask* task, void* func, void* data, uint32 size);
	bool MPTermTask(MPTask* task);
	bool MPRunTask(MPTask* task);
	bool MPGetTaskInfo(MPTask* task, MPTaskInfo* info);
	void* MPGetTaskUserData(MPTask* task);
	void MPSetTaskUserData(MPTask* task, void* userdata);

	void MPInitTaskQ(MPTaskQ* taskq, MPTask** tasks, uint32 taskCount);
	bool MPEnqueTask(MPTaskQ* taskq, MPTask* task);
	bool MPTermTaskQ(MPTaskQ* taskq);
	bool MPGetTaskQInfo(MPTaskQ* taskq, MPTaskQInfo* info);
	bool MPStartTaskQ(MPTaskQ* taskq);
	bool MPRunTasksFromTaskQ(MPTaskQ* taskq, int granularity);
	bool MPStopTaskQ(MPTaskQ* taskq);
	bool MPWaitTaskQ(MPTaskQ* taskq, uint32 waitState);
	bool MPWaitTaskQWithTimeout(MPTaskQ* taskq, uint32 waitState, sint64 timeout);
	MPTask* MPDequeTask(MPTaskQ* taskq);
	uint32 MPDequeTasks(MPTaskQ* taskq, MPTask** tasks, sint32 maxTasks);
	bool MPResetTaskQ(MPTaskQ* taskq);

	void InitializeMP();
}

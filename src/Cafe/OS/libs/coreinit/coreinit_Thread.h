#pragma once
#include "Cafe/HW/Espresso/Const.h"
#include "Cafe/OS/libs/coreinit/coreinit_Scheduler.h"

struct OSThread_t;

struct OSContextRegFPSCR_t
{
	 uint32be padding;
	 uint32be fpscr;
};

struct OSContext_t
{
	static constexpr uint32 OS_CONTEXT_MAGIC_0 = 0x4f53436f;
	static constexpr uint32 OS_CONTEXT_MAGIC_1 = 0x6e747874;

	 betype<uint32> magic0;
	 betype<uint32> magic1;
	 uint32 gpr[32];
	 uint32 cr;
	 uint32 lr;
	 uint32 ctr;
	 uint32 xer;
	 uint32 srr0;
	 uint32 srr1;
	 uint32 dsi_dsisr;
	 uint32 dsi_dar;
	 uint32 ukn0A8;
	 uint32 ukn0AC;
	 OSContextRegFPSCR_t fpscr;
	 uint64be fp_ps0[32];
	 uint16be boostCount;
	 uint16 state;
	 uint32 gqr[8];
	 uint32be upir;
	 uint64be fp_ps1[32];
	 uint64be coretime[3];
	 uint64be starttime;
	 sint32be ghs_errno;
	 uint32be affinity;
	 uint32be upmc1;
	 uint32be upmc2;
	 uint32be upmc3;
	 uint32be upmc4;
	 uint32be ummcr0;
	 uint32be ummcr1;

	bool checkMagic()
	{
		return magic0 == (uint32)OS_CONTEXT_MAGIC_0 && magic1 == (uint32)OS_CONTEXT_MAGIC_1;
	}

	void SetContextMagic()
	{
		magic0 = OS_CONTEXT_MAGIC_0;
		magic1 = OS_CONTEXT_MAGIC_1;
	}

	bool hasCoreAffinitySet(uint32 coreIndex) const
	{
		return (((uint32)affinity >> coreIndex) & 1) != 0;
	}

	void setAffinity(uint32 mask)
	{
		affinity = mask & 7;
	}

	uint32 getAffinity() const
	{
		return affinity;
	}
};

static_assert(sizeof(OSContext_t) == 0x320);

typedef struct
{
	 uint32 ukn000[0x68 / 4];
	 MEMPTR<void> eh_globals;
	 uint32 eh_mem_manage[9];
	 MPTR eh_store_globals[6];
	 MPTR eh_store_globals_tdeh[76];
}crt_t;

static_assert(sizeof(crt_t) == 0x1D8, "");

#pragma pack(1)

namespace coreinit
{

	struct OSThreadLink
	{
		MEMPTR<struct OSThread_t> next;
		MEMPTR<struct OSThread_t> prev;
	};

	static_assert(sizeof(OSThreadLink) == 8);

	struct OSThreadQueueInternal
	{
		MEMPTR<OSThread_t> head;
		MEMPTR<OSThread_t> tail;

		bool isEmpty() const
		{
			cemu_assert_debug((!head.IsNull() == !tail.IsNull()) || (head.IsNull() == tail.IsNull()));
			return head.IsNull();
		}

		void addThread(OSThread_t* thread, OSThreadLink* threadLink);
		void addThreadByPriority(OSThread_t* thread, OSThreadLink* threadLink);
		void removeThread(OSThread_t* thread, OSThreadLink* threadLink);

		void queueAndWait(OSThread_t* thread);
		void queueOnly(OSThread_t* thread);

		void cancelWait(OSThread_t* thread);
		void wakeupEntireWaitQueue(bool reschedule, bool sharedPriorityAndAffinityWorkaround = false);
		void wakeupSingleThreadWaitQueue(bool reschedule, bool sharedPriorityAndAffinityWorkaround = false);

	private:
		OSThread_t* takeFirstFromQueue(size_t linkOffset)
		{
			cemu_assert_debug(__OSHasSchedulerLock());
			if (head == nullptr)
				return nullptr;
			OSThread_t* thread = head.GetPtr();
			OSThreadLink* link = _getThreadLink(thread, linkOffset);
			removeThread(thread, link);
			return thread;
		}

		static size_t getLinkOffset(OSThread_t* thread, OSThreadLink* threadLink)
		{
			cemu_assert_debug((void*)threadLink >= (void*)thread && (void*)threadLink < (void*)((uint8*)thread + 0x680));
			return (uint8*)threadLink - (uint8*)thread;
		}

		static OSThreadLink* _getThreadLink(OSThread_t* thread, size_t linkOffset)
		{
			return (OSThreadLink*)((uint8*)thread + linkOffset);
		}

		void _debugCheckChain(OSThread_t* thread, OSThreadLink* threadLink)
		{
#ifdef CEMU_DEBUG_ASSERT
			cemu_assert_debug(tail.IsNull() == head.IsNull());
			size_t linkOffset = getLinkOffset(thread, threadLink);
			OSThread_t* threadItr = head.GetPtr();
			while (threadItr)
			{
				if (threadItr == thread)
					return;
				threadItr = _getThreadLink(threadItr, linkOffset)->next.GetPtr();
			}
			cemu_assert_debug(false);
#endif
		}
	};

	static_assert(sizeof(OSThreadQueueInternal) == 0x8);

	struct OSThreadQueueSmall : public OSThreadQueueInternal
	{
	};

	static_assert(sizeof(OSThreadQueueSmall) == 8);
	static_assert(offsetof(OSThreadQueueSmall, head) == 0x0);
	static_assert(offsetof(OSThreadQueueSmall, tail) == 0x4);

	struct OSThreadQueue : public OSThreadQueueInternal
	{
		MEMPTR<void> userData;
		uint32be ukn0C;
	};

	static_assert(sizeof(OSThreadQueue) == 0x10);
	static_assert(offsetof(OSThreadQueue, head) == 0x0);
	static_assert(offsetof(OSThreadQueue, tail) == 0x4);
	static_assert(offsetof(OSThreadQueue, userData) == 0x8);
	static_assert(offsetof(OSThreadQueue, ukn0C) == 0xC);

	struct OSMutex
	{
		 uint32				magic;
		 MEMPTR<void>		userData;
		 uint32be			ukn08;
		 OSThreadQueue		threadQueue;
		 MEMPTR<OSThread_t>	owner;
		 sint32be			lockCount;
		 MEMPTR<OSMutex>		next;
		 MEMPTR<OSMutex>		prev;

	};

	static_assert(sizeof(OSMutex) == 0x2C);

	struct OSMutexQueue
	{
		MEMPTR<OSMutex> head;
		MEMPTR<OSMutex> tail;
		MEMPTR<void> ukn08;
		uint32be ukn0C;

		bool isEmpty() const
		{
			cemu_assert_debug((!head.IsNull() == !tail.IsNull()) || (head.IsNull() == tail.IsNull()));
			return head.IsNull();
		}

		void addMutex(OSMutex* mutex)
		{
			cemu_assert_debug(__OSHasSchedulerLock());
			if (tail.IsNull())
			{
				mutex->next = nullptr;
				mutex->prev = nullptr;
				head = mutex;
				tail = mutex;
			}
			else
			{
				tail->next = mutex;
				mutex->prev = tail;
				mutex->next = nullptr;
				tail = mutex;
			}
		}

		void removeMutex(OSMutex* mutex)
		{
			cemu_assert_debug(__OSHasSchedulerLock());
			cemu_assert_debug(!head.IsNull() && !tail.IsNull());
			if (mutex->prev)
				mutex->prev->next = mutex->next;
			else
				head = mutex->next;
			if (mutex->next)
				mutex->next->prev = mutex->prev;
			else
				tail = mutex->prev;
			mutex->next = nullptr;
			mutex->prev = nullptr;
		}

		OSMutex* getFirst()
		{
			return head.GetPtr();
		}
	};

	static_assert(sizeof(OSMutexQueue) == 0x10);

	struct OSFastMutexLink
	{
		 MEMPTR<struct OSMutex> next;
		 MEMPTR<struct OSMutex> prev;
	};

	struct OSFastMutex
	{
		 uint32be magic;
		 MEMPTR<void> userData;
		 uint32be contendedState;
		 OSThreadQueueSmall threadQueueSmall;
		 OSFastMutexLink ownedLink;
		 MEMPTR<OSThread_t> owner;
		 uint32be lockCount;
		 OSFastMutexLink contendedLink;
	};

	static_assert(sizeof(OSFastMutex) == 0x2C);

	struct OSEvent
	{
		enum class EVENT_MODE : uint32
		{
			MODE_MANUAL = 0,
			MODE_AUTO = 1,
		};

		enum class EVENT_STATE : uint32
		{
			STATE_NOT_SIGNALED = 0,
			STATE_SIGNALED = 1
		};

		 uint32be magic;
		 MEMPTR<void> userData;
		 uint32be ukn08;
		 betype<EVENT_STATE> state;
		 OSThreadQueue threadQueue;
		 betype<EVENT_MODE> mode;
	};

	static_assert(sizeof(OSEvent) == 0x24);

	struct OSRendezvous
	{
		 uint32be coreHit[3];
		 MEMPTR<void> userData;
	};

	static_assert(sizeof(OSRendezvous) == 0x10);

	struct OSCond
	{
		uint32be magic;
		MEMPTR<void> userData;
		uint32be ukn08;
		OSThreadQueue threadQueue;
	};

	static_assert(sizeof(OSCond) == 0x1C);

	struct OSSemaphore
	{
		uint32be magic;
		MEMPTR<void> userData;
		uint32be ukn08;
		sint32be count;
		OSThreadQueue threadQueue;
	};

	static_assert(sizeof(OSSemaphore) == 0x20);

	struct OSFastCond
	{
		uint32be magic;
		MEMPTR<void> userData;
		uint32be ukn08;
		OSThreadQueue threadQueue;
	};

	static_assert(sizeof(OSFastCond) == 0x1C);

};

struct OSThread_t
{
	static constexpr uint32 MAGIC_THREAD = 0x74487244;

	enum class THREAD_TYPE : uint32
	{
		TYPE_DRIVER = 0,
		TYPE_IO = 1,
		TYPE_APP = 2
	};

	enum class THREAD_STATE : uint8
	{
		STATE_NONE = 0,
		STATE_READY = 1,
		STATE_RUNNING = 2,
		STATE_WAITING = 4,
		STATE_MORIBUND = 8,
	};

	enum ATTR_BIT : uint32
	{
		ATTR_AFFINITY_CORE0 = 0x1,
		ATTR_AFFINITY_CORE1 = 0x2,
		ATTR_AFFINITY_CORE2 = 0x4,
		ATTR_DETACHED		= 0x8,
		ATTR_UKN_010		= 0x10,
	};

	enum REQUEST_FLAG_BIT : uint32
	{
		REQUEST_FLAG_NONE = 0,
		REQUEST_FLAG_SUSPEND = 1,
		REQUEST_FLAG_CANCEL = 2,
	};

	static sint32 GetTypePriorityBase(THREAD_TYPE threadType)
	{
		if (threadType == OSThread_t::THREAD_TYPE::TYPE_DRIVER)
			return 0;
		else if (threadType == OSThread_t::THREAD_TYPE::TYPE_IO)
			return 32;
		else if (threadType == OSThread_t::THREAD_TYPE::TYPE_APP)
			return 64;
		return 0;
	}

    void SetThreadMagic()
    {
        magic = MAGIC_THREAD;
    }

    bool IsValidMagic() const
    {
        return magic == MAGIC_THREAD && context.magic0 == OSContext_t::OS_CONTEXT_MAGIC_0 && context.magic1 == OSContext_t::OS_CONTEXT_MAGIC_1;
    }

	 OSContext_t						context;
	 uint32be							magic;
	 betype<THREAD_STATE>				state;
	 uint8								attr;
	 uint16be							id;
	 betype<sint32>						suspendCounter;
	 sint32be							effectivePriority;
	 sint32be							basePriority;
	 uint32be							exitValue;

	 MEMPTR<coreinit::OSThreadQueue>	currentRunQueue[Espresso::CORE_COUNT];
	 coreinit::OSThreadLink				linkRun[Espresso::CORE_COUNT];

	 MEMPTR<coreinit::OSThreadQueueInternal> currentWaitQueue;
	 coreinit::OSThreadLink				waitQueueLink;

	 coreinit::OSThreadQueue			joinQueue;

	 MEMPTR<coreinit::OSMutex>			waitingForMutex;
	 coreinit::OSMutexQueue				mutexQueue;

	 coreinit::OSThreadLink				activeThreadChain;

	 MEMPTR<void>						stackBase;
	 MEMPTR<void>						stackEnd;

	 MEMPTR<void>						entrypoint;
	 crt_t								crt;

	 sint32								alarmRelatedUkn;
	 std::array<MEMPTR<void>, 16>		specificArray;
	 betype<THREAD_TYPE>				type;
	 MEMPTR<const char>					threadName;
	 MEMPTR<void>						waitAlarm;

	 uint32								userStackPointer;

	 MEMPTR<void>						cleanupCallback;
	 MEMPTR<void>						deallocatorFunc;

	 uint32								stateFlags;
	 betype<REQUEST_FLAG_BIT>			requestFlags;
	 sint32								pendingSuspend;
	 sint32								suspendResult;
	 coreinit::OSThreadQueue			suspendQueue;
	 uint32								_padding5F4;
	 uint64be							quantumTicks;
	 uint64								coretimeSumQuantumStart;

	 uint64be							wakeUpCount;
	 uint64be							totalCycles;
	 uint64be							wakeUpTime;
	 uint64								wakeTimeRelatedUkn1;
	 uint64								wakeTimeRelatedUkn2;

	 MPTR								dsiCallback[Espresso::CORE_COUNT];
	 MPTR								isiCallback[Espresso::CORE_COUNT];
	 MPTR								programCallback[Espresso::CORE_COUNT];
	 MPTR								perfMonCallback[Espresso::CORE_COUNT];

	 uint32								ukn660;

	 uint16								numAllocatedTLSBlocks;
	 sint16								tlsStatus;
	 MPTR								tlsBlocksMPTR;

	 MEMPTR<coreinit::OSFastMutex>		waitingForFastMutex;
	 coreinit::OSFastMutexLink			contendedFastMutex;
	 coreinit::OSFastMutexLink			ownedFastMutex;
	 MEMPTR<void>						alignmentExceptionCallback[Espresso::CORE_COUNT];

	 uint32								padding68C[20 / 4];
};
static_assert(sizeof(OSThread_t) == 0x6A0);

namespace coreinit
{
	void MapThreadExports();
	void InitializeThread();

	void InitializeConcurrency();

	bool __CemuIsMulticoreMode();

	OSThread_t* OSGetDefaultThread(sint32 coreIndex);
	void* OSGetDefaultThreadStack(sint32 coreIndex, uint32& size);

	bool OSCreateThreadType(OSThread_t* thread, MPTR entryPoint, sint32 numParam, void* ptrParam, void* stackTop2, sint32 stackSize, sint32 priority, uint32 attr, OSThread_t::THREAD_TYPE threadType);
	bool __OSCreateThreadType(OSThread_t* thread, MPTR entryPoint, sint32 numParam, void* ptrParam, void* stackTop, sint32 stackSize, sint32 priority, uint32 attr, OSThread_t::THREAD_TYPE threadType);
	void OSCreateThreadInternal(OSThread_t* thread, uint32 entryPoint, MPTR stackLowerBaseAddr, uint32 stackSize, uint8 affinityMask, OSThread_t::THREAD_TYPE threadType);
	bool OSRunThread(OSThread_t* thread, MPTR funcAddress, sint32 numParam, void* ptrParam);
	void OSExitThread(sint32 exitValue);
	void OSDetachThread(OSThread_t* thread);

	OSThread_t* OSGetCurrentThread();
	void OSSetCurrentThread(uint32 coreIndex, OSThread_t* thread);

	void __OSSetThreadBasePriority(OSThread_t* thread, sint32 newPriority);
	void __OSUpdateThreadEffectivePriority(OSThread_t* thread);

	bool OSSetThreadPriority(OSThread_t* thread, sint32 newPriority);
	uint32 OSGetThreadAffinity(OSThread_t* thread);

	void OSSetThreadName(OSThread_t* thread, const char* name);
	const char* OSGetThreadName(OSThread_t* thread);

	sint32 __OSResumeThreadInternal(OSThread_t* thread, sint32 resumeCount);
	sint32 OSResumeThread(OSThread_t* thread);
	void OSContinueThread(OSThread_t* thread);
	void __OSSuspendThreadInternal(OSThread_t* thread);
	void __OSSuspendThreadNolock(OSThread_t* thread);
	void OSSuspendThread(OSThread_t* thread);
	void OSSleepThread(OSThreadQueue* threadQueue);
	void OSWakeupThread(OSThreadQueue* threadQueue);
	bool OSJoinThread(OSThread_t* thread, uint32be* exitValue);

	void OSTestThreadCancelInternal();

	void OSYieldThread();
	void OSSleepTicks(uint64 ticks);

	bool OSIsThreadTerminated(OSThread_t* thread);
	bool OSIsThreadSuspended(OSThread_t* thread);
    bool OSIsThreadRunningNoLock(OSThread_t* thread);
	bool OSIsThreadRunning(OSThread_t* thread);

	void OSInitThreadQueue(OSThreadQueue* threadQueue);
	void OSInitThreadQueueEx(OSThreadQueue* threadQueue, void* userData);

	void OSInitEvent(OSEvent* event, OSEvent::EVENT_STATE initialState, OSEvent::EVENT_MODE mode);
	void OSInitEventEx(OSEvent* event, OSEvent::EVENT_STATE initialState, OSEvent::EVENT_MODE mode, void* userData);
	void OSResetEvent(OSEvent* event);
	void OSWaitEventInternal(OSEvent* event);
	void OSWaitEvent(OSEvent* event);
	bool OSWaitEventWithTimeout(OSEvent* event, uint64 timeout);
	void OSSignalEventInternal(OSEvent* event);
	void OSSignalEvent(OSEvent* event);
	void OSSignalEventAllInternal(OSEvent* event);
	void OSSignalEventAll(OSEvent* event);

	void OSInitRendezvous(OSRendezvous* rendezvous);
	bool OSWaitRendezvous(OSRendezvous* rendezvous, uint32 coreMask);

	void OSInitMutex(OSMutex* mutex);
	void OSInitMutexEx(OSMutex* mutex, void* userData);
	void OSLockMutex(OSMutex* mutex);
	bool OSTryLockMutex(OSMutex* mutex);
	void OSUnlockMutex(OSMutex* mutex);
	void OSUnlockMutexInternal(OSMutex* mutex);

	void OSInitCond(OSCond* cond);
	void OSInitCondEx(OSCond* cond, void* userData);
	void OSSignalCond(OSCond* cond);
	void OSWaitCond(OSCond* cond, OSMutex* mutex);

	void OSInitSemaphore(OSSemaphore* semaphore, sint32 initialCount);
	void OSInitSemaphoreEx(OSSemaphore* semaphore, sint32 initialCount, void* userData);
	sint32 OSWaitSemaphoreInternal(OSSemaphore* semaphore);
	sint32 OSWaitSemaphore(OSSemaphore* semaphore);
	sint32 OSTryWaitSemaphore(OSSemaphore* semaphore);
	sint32 OSSignalSemaphore(OSSemaphore* semaphore);
	sint32 OSSignalSemaphoreInternal(OSSemaphore* semaphore, bool reschedule);
	sint32 OSGetSemaphoreCount(OSSemaphore* semaphore);

	void OSFastMutex_Init(OSFastMutex* fastMutex, void* userData);
	void OSFastMutex_Lock(OSFastMutex* fastMutex);
	bool OSFastMutex_TryLock(OSFastMutex* fastMutex);
	void OSFastMutex_Unlock(OSFastMutex* fastMutex);

	void OSFastCond_Init(OSFastCond* fastCond, void* userData);
	void OSFastCond_Wait(OSFastCond* fastCond, OSFastMutex* fastMutex);
	void OSFastCond_Signal(OSFastCond* fastCond);

	void OSSchedulerBegin(sint32 numCPUEmulationThreads);
	void OSSchedulerEnd();
	bool OSIsSchedulerActive();

	void __OSAddReadyThreadToRunQueue(OSThread_t* thread);
	bool __OSCoreShouldSwitchToThread(OSThread_t* currentThread, OSThread_t* newThread, bool sharedPriorityAndAffinityWorkaround);
	void __OSQueueThreadDeallocation(OSThread_t* thread);

    bool __OSIsThreadActive(OSThread_t* thread);
	void __OSDeleteAllActivePPCThreads();

	bool __OSAllCoresIdle();

	void OSSetThermalThrottleMicros(uint32 micros);
}

#pragma pack()

extern MPTR activeThread[256];
extern sint32 activeThreadCount;

extern SlimRWLock srwlock_activeThreadList;
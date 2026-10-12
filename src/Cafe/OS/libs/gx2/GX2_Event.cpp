#include "Cafe/HW/Latte/Core/PerfTelemetry.h"
#include "Cafe/OS/common/OSCommon.h"
#include "GX2_Command.h"
#include "GX2_Event.h"
#include "Cafe/HW/Latte/Core/LattePM4.h"
#include "Cafe/HW/MMU/MMU.h"
#include "Cafe/OS/libs/coreinit/coreinit_Thread.h"
#include "Cafe/HW/Latte/Core/Latte.h"
#include "GX2.h"
#include "Cafe/HW/Latte/Renderer/Renderer.h"
#include "config/ActiveSettings.h"
#include "util/helpers/ConcurrentQueue.h"

namespace GX2
{

	SysAllocator<coreinit::OSThreadQueue> g_vsyncThreadQueue;
	SysAllocator<coreinit::OSThreadQueue> g_flipThreadQueue;

	void GX2SetGPUFence(uint32be* fencePtr, uint32 mask, uint32 compareOp, uint32 compareValue)
	{
		GX2ReserveCmdSpace(7);
		uint8 compareOpTable[] = { 0x7,0x1,0x3,0x2,0x6,0x4,0x5,0x0 };
		gx2WriteGather_submitU32AsBE(pm4HeaderType3(IT_WAIT_REG_MEM, 6));
		gx2WriteGather_submitU32AsBE((uint32)(compareOpTable[compareOp & 7]) | 0x10);
		gx2WriteGather_submitU32AsBE(memory_virtualToPhysical(memory_getVirtualOffsetFromPointer(fencePtr)) | 2);
		gx2WriteGather_submitU32AsBE(0);
		gx2WriteGather_submitU32AsBE(compareValue);
		gx2WriteGather_submitU32AsBE(mask);
		gx2WriteGather_submitU32AsBE(0xA);
	}

	enum class GX2PipeEventType : uint32
	{
		TOP = 0,
		BOTTOM = 1,
		BOTTOM_AFTER_FLUSH = 2
	};

	void GX2SubmitUserTimeStamp(uint64* timestampOut, uint64 value, GX2PipeEventType eventType, uint32 triggerInterrupt)
	{
		GX2ReserveCmdSpace(11);

		MPTR physTimestampAddr = memory_virtualToPhysical(memory_getVirtualOffsetFromPointer(timestampOut));
		uint32 valHigh = (uint32)(value >> 32);
		uint32 valLow = (uint32)(value & 0xffffffff);

		if (eventType == GX2PipeEventType::TOP)
		{
			gx2WriteGather_submitU32AsBE(pm4HeaderType3(IT_MEM_WRITE, 4));
			gx2WriteGather_submitU32AsBE(physTimestampAddr | 0x2);
			gx2WriteGather_submitU32AsBE(0);
			gx2WriteGather_submitU32AsBE(valLow);
			gx2WriteGather_submitU32AsBE(valHigh);
			if (triggerInterrupt != 0)
			{
				gx2WriteGather_submitU32AsBE(0x0000304A);
				gx2WriteGather_submitU32AsBE(0x40000000);
			}
		}
		else if (eventType == GX2PipeEventType::BOTTOM_AFTER_FLUSH)
		{
			gx2WriteGather_submitU32AsBE(pm4HeaderType3(IT_MEM_WRITE, 4));
			gx2WriteGather_submitU32AsBE(physTimestampAddr | 0x2);
			gx2WriteGather_submitU32AsBE(0);
			gx2WriteGather_submitU32AsBE(valLow);
			gx2WriteGather_submitU32AsBE(valHigh);
			if (triggerInterrupt != 0)
			{
				gx2WriteGather_submitU32AsBE(0x0000304B);
				gx2WriteGather_submitU32AsBE(0x40000000);
				gx2WriteGather_submitU32AsBE(pm4HeaderType3(IT_HLE_BOTTOM_OF_PIPE_CB, 3));
				gx2WriteGather_submitU32AsBE(physTimestampAddr);
				gx2WriteGather_submitU32AsBE(valLow);
				gx2WriteGather_submitU32AsBE(valHigh);
			}
		}
		else if (eventType == GX2PipeEventType::BOTTOM)
		{
			if (triggerInterrupt != 0)
			{
				gx2WriteGather_submitU32AsBE(pm4HeaderType3(IT_HLE_BOTTOM_OF_PIPE_CB, 3));
				gx2WriteGather_submitU32AsBE(physTimestampAddr);
				gx2WriteGather_submitU32AsBE(valLow);
				gx2WriteGather_submitU32AsBE(valHigh);
			}
			else
			{
				gx2WriteGather_submitU32AsBE(pm4HeaderType3(IT_MEM_WRITE, 4));
				gx2WriteGather_submitU32AsBE(physTimestampAddr | 0x2);
				gx2WriteGather_submitU32AsBE(0);
				gx2WriteGather_submitU32AsBE(valLow);
				gx2WriteGather_submitU32AsBE(valHigh);
			}
		}
		else
		{
			cemu_assert_debug(false);
		}
	}

	struct GX2EventFunc
	{
		MEMPTR<void> callbackFuncPtr;
		MEMPTR<void> userData;
	}s_eventCallback[GX2CallbackEventTypeCount]{};

	void GX2SetEventCallback(GX2CallbackEventType eventType, void* callbackFuncPtr, void* userData)
	{
		if ((size_t)eventType >= GX2CallbackEventTypeCount)
		{
			cemuLog_log(LogType::Force, "GX2SetEventCallback(): Unknown eventType");
			return;
		}
		s_eventCallback[(size_t)eventType].callbackFuncPtr = callbackFuncPtr;
		s_eventCallback[(size_t)eventType].userData = userData;
	}

	void GX2GetEventCallback(GX2CallbackEventType eventType, MEMPTR<void>* callbackFuncPtrOut, MEMPTR<void>* userDataOut)
	{
		if ((size_t)eventType >= GX2CallbackEventTypeCount)
		{
			cemuLog_log(LogType::Force, "GX2GetEventCallback(): Unknown eventType");
			return;
		}
		if (callbackFuncPtrOut)
			*callbackFuncPtrOut = s_eventCallback[(size_t)eventType].callbackFuncPtr;
		if (userDataOut)
			*userDataOut = s_eventCallback[(size_t)eventType].userData;
	}

	bool s_callbackThreadLaunched{};
	SysAllocator<OSThread_t> s_eventCallbackThread;
	SysAllocator<uint8, 0x2000> s_eventCallbackThreadStack;
	SysAllocator<char, 64> s_eventCallbackThreadName;
	struct GX2EventQueueEntry
	{
		GX2EventQueueEntry() {};
		GX2EventQueueEntry(GX2CallbackEventType eventType) : eventType(eventType) {};
		GX2CallbackEventType eventType{(GX2CallbackEventType)-1};
	};

	SysAllocator<coreinit::OSSemaphore> s_eventCbQueueSemaphore;
	ConcurrentQueue<GX2EventQueueEntry> s_eventCbQueue;

	void __GX2NotifyEvent(GX2CallbackEventType eventType)
	{
		if ((size_t)eventType >= GX2CallbackEventTypeCount)
		{
			cemu_assert_debug(false);
			return;
		}
		if (s_eventCallback[(size_t)eventType].callbackFuncPtr)
		{
			s_eventCbQueue.push(eventType);
			coreinit::OSSignalSemaphore(s_eventCbQueueSemaphore);
		}
		if (eventType == GX2CallbackEventType::VSYNC)
		{
			__OSLockScheduler();
			g_vsyncThreadQueue->wakeupEntireWaitQueue(false);
			__OSUnlockScheduler();
		}
		else if (eventType == GX2CallbackEventType::FLIP)
		{
			__OSLockScheduler();
			g_flipThreadQueue->wakeupEntireWaitQueue(false);
			__OSUnlockScheduler();
		}
	}

	void __GX2CallbackThread(PPCInterpreter_t* hCPU)
	{
		while (coreinit::OSWaitSemaphore(s_eventCbQueueSemaphore))
		{
			GX2EventQueueEntry entry;
			if (!s_eventCbQueue.peek2(entry))
				continue;
			if(!s_eventCallback[(size_t)entry.eventType].callbackFuncPtr)
				continue;
			PPCCoreCallback(s_eventCallback[(size_t)entry.eventType].callbackFuncPtr, (sint32)entry.eventType, s_eventCallback[(size_t)entry.eventType].userData);
		}
		osLib_returnFromFunction(hCPU, 0);
	}

	void GX2WaitForVsync()
	{
		PerfTelemetry::ScopedTimer waitTimer(PerfTelemetry::Get().guestWaitVsyncNs);
		__OSLockScheduler();
		g_vsyncThreadQueue.GetPtr()->queueAndWait(coreinit::OSGetCurrentThread());
		__OSUnlockScheduler();
	}

	void GX2WaitForFlip()
	{
		if ((sint32)(_swapEndianU32(LatteGPUState.sharedArea->flipRequestCountBE) == _swapEndianU32(LatteGPUState.sharedArea->flipExecuteCountBE)))
			return;
		PerfTelemetry::ScopedTimer waitTimer(PerfTelemetry::Get().guestWaitFlipNs);
		__OSLockScheduler();
		g_flipThreadQueue.GetPtr()->queueAndWait(coreinit::OSGetCurrentThread());
		__OSUnlockScheduler();
	}

	bool GX2DrawDone()
	{
		bool forceFullSync = false;
		if (g_renderer && g_renderer->GetType() == RendererAPI::Vulkan)
			forceFullSync = true;
		if (forceFullSync || ActiveSettings::WaitForGX2DrawDoneEnabled())
		{
			GX2ReserveCmdSpace(2);
			gx2WriteGather_submitU32AsBE(pm4HeaderType3(IT_HLE_SYNC_ASYNC_OPERATIONS, 1));
			gx2WriteGather_submitU32AsBE(0x00000000);
		}
		GX2Command_Flush(0x100, true);

		uint64 ts = GX2GetLastSubmittedTimeStamp();
		return GX2WaitTimeStamp(ts);
	}

	void GX2Init_event()
	{

		if (s_callbackThreadLaunched)
			return;
		s_callbackThreadLaunched = true;
		strcpy(s_eventCallbackThreadName.GetPtr(), "GX2 event callback");
		coreinit::OSCreateThreadType(s_eventCallbackThread, PPCInterpreter_makeCallableExportDepr(__GX2CallbackThread), 0, nullptr, (uint8*)s_eventCallbackThreadStack.GetPtr() + s_eventCallbackThreadStack.GetByteSize(), (sint32)s_eventCallbackThreadStack.GetByteSize(), 16, OSThread_t::ATTR_DETACHED, OSThread_t::THREAD_TYPE::TYPE_IO);
		coreinit::OSSetThreadName(s_eventCallbackThread, s_eventCallbackThreadName);
		coreinit::OSResumeThread(s_eventCallbackThread);
	}

	void GX2EventInit()
	{
		cafeExportRegister("gx2", GX2SetGPUFence, LogType::GX2);
		cafeExportRegister("gx2", GX2SubmitUserTimeStamp, LogType::GX2);

		cafeExportRegister("gx2", GX2SetEventCallback, LogType::GX2);
		cafeExportRegister("gx2", GX2GetEventCallback, LogType::GX2);

		cafeExportRegister("gx2", GX2WaitForVsync, LogType::GX2);
		cafeExportRegister("gx2", GX2WaitForFlip, LogType::GX2);
		cafeExportRegister("gx2", GX2DrawDone, LogType::GX2);

		coreinit::OSInitThreadQueue(g_vsyncThreadQueue.GetPtr());
		coreinit::OSInitThreadQueue(g_flipThreadQueue.GetPtr());

		coreinit::OSInitSemaphore(s_eventCbQueueSemaphore, 0);
	}

    void GX2EventResetToDefaultState()
    {
        s_eventCbQueue.clear();
        s_callbackThreadLaunched = false;
        for(auto& it : s_eventCallback)
        {
            it.callbackFuncPtr = nullptr;
            it.userData = nullptr;
        }
    }
}

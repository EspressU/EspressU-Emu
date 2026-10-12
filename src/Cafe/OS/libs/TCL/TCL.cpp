#include "Cafe/OS/common/OSCommon.h"
#include "Cafe/OS/libs/TCL/TCL.h"

#include "HW/Latte/Core/LattePM4.h"
#include "OS/libs/coreinit/coreinit_Time.h"

namespace TCL
{
	SysAllocator<coreinit::OSEvent> s_updateRetirementEvent;
	uint64 s_currentRetireMarker = 0;

	struct TCLStatePPC
	{
		uint64be gpuRetireMarker;
	};

	SysAllocator<TCLStatePPC> s_tclStatePPC;

	void TCLGPUNotifyNewRetirementTimestamp()
	{
		__OSLockScheduler();
		coreinit::OSSignalEventAllInternal(s_updateRetirementEvent.GetPtr());
		__OSUnlockScheduler();
	}

	int TCLTimestamp(TCLTimestampId id, uint64be* timestampOut)
	{
		if (id == TCLTimestampId::TIMESTAMP_LAST_BUFFER_RETIRED)
		{
			MEMPTR<uint32> b;
			stdx::atomic_ref<uint64be> retireTimestamp(s_tclStatePPC->gpuRetireMarker);
			*timestampOut = retireTimestamp.load();
			return 0;
		}
		else
		{
			cemuLog_log(LogType::Force, "TCLTimestamp(): Unsupported timestamp ID {}", (uint32)id);
			*timestampOut = 0;
			return 0;
		}
	}

	int TCLWaitTimestamp(TCLTimestampId id, uint64 waitTs, uint64 timeout)
	{
		if (id == TCLTimestampId::TIMESTAMP_LAST_BUFFER_RETIRED)
		{
			while ( true )
			{
				stdx::atomic_ref<uint64be> retireTimestamp(s_tclStatePPC->gpuRetireMarker);
				uint64 currentTimestamp = retireTimestamp.load();
				if (currentTimestamp >= waitTs)
					return 0;
				coreinit::OSWaitEvent(s_updateRetirementEvent.GetPtr());
			}
		}
		else
		{
			cemuLog_log(LogType::Force, "TCLWaitTimestamp(): Unsupported timestamp ID {}", (uint32)id);
		}
		return 0;
	}

	static constexpr uint32 TCL_RING_BUFFER_SIZE = 4096;

	std::atomic<uint32> tclRingBufferA[TCL_RING_BUFFER_SIZE];
	std::atomic<uint32> tclRingBufferA_readIndex{0};
	std::atomic<uint32> tclRingBufferA_writeIndex{0};

	bool TCLGPUReadRBWord(uint32& cmdWord)
	{
		uint32 readIndex = tclRingBufferA_readIndex.load(std::memory_order::relaxed);
		uint32 writeIndex = tclRingBufferA_writeIndex.load(std::memory_order::acquire);
		if (readIndex == writeIndex)
			return false;
		cmdWord = tclRingBufferA[readIndex].load(std::memory_order::relaxed);
		tclRingBufferA_readIndex.store((readIndex + 1) % TCL_RING_BUFFER_SIZE, std::memory_order::release);
		return true;
	}

	void TCLResetRing()
	{
		tclRingBufferA_readIndex.store(0);
		tclRingBufferA_writeIndex.store(0);
	}

	void TCLWaitForRBSpace(uint32be numU32s)
	{
		uint32 writeIndex = tclRingBufferA_writeIndex.load(std::memory_order::relaxed);
		while (true)
		{
			uint32 readIndex = tclRingBufferA_readIndex.load(std::memory_order::acquire);
			uint32 distance = (readIndex + TCL_RING_BUFFER_SIZE - writeIndex) & (TCL_RING_BUFFER_SIZE - 1);
			if (writeIndex == readIndex)
				distance = TCL_RING_BUFFER_SIZE;
			if (distance >= numU32s + 1)
				break;
			coreinit::OSSleepTicks(coreinit::EspressoTime::ConvertNsToTimerTicks(500000));
		}
	}

	void TCLWriteCmd(uint32be* cmd, uint32 cmdLen)
	{
		uint32 writeIndex = tclRingBufferA_writeIndex.load(std::memory_order::relaxed);

		while (cmdLen > 0)
		{
			tclRingBufferA[writeIndex].store(*cmd, std::memory_order::relaxed);
			writeIndex++;
			writeIndex &= (TCL_RING_BUFFER_SIZE - 1);
			cmd++;
			cmdLen--;
		}

		tclRingBufferA_writeIndex.store(writeIndex, std::memory_order::release);
	}

	#define EVENT_TYPE_TS		5

	void TCLSubmitRetireMarker(bool triggerEventInterrupt)
	{
		s_currentRetireMarker++;
		uint32be cmd[6];
		cmd[0] = pm4HeaderType3(IT_EVENT_WRITE_EOP, 5);
		cmd[1] = (4 | (EVENT_TYPE_TS << 8));
		cmd[2] = MEMPTR<void>(&s_tclStatePPC->gpuRetireMarker).GetMPTR();
		cmd[3] = 0x40000000;
		if (triggerEventInterrupt)
			cmd[3] |= 0x2000000;
		cmd[4] = (uint32)s_currentRetireMarker;
		cmd[5] = (uint32)(s_currentRetireMarker>>32);
		TCLWriteCmd(cmd, 6);
	}

	int TCLSubmitToRing(uint32be* cmd, uint32 cmdLen, betype<TCLSubmissionFlag>* controlFlags, uint64be* timestampValueOut)
	{
		TCLSubmissionFlag flags = *controlFlags;
		cemu_assert_debug(timestampValueOut);

		uint32 totalCommandLength = cmdLen;
		totalCommandLength += 6;

		TCLWaitForRBSpace(totalCommandLength);

		TCLWriteCmd(cmd, cmdLen);

		if ((HAS_FLAG(flags, TCLSubmissionFlag::USE_RETIRED_MARKER)))
		{
			TCLSubmitRetireMarker(!HAS_FLAG(flags, TCLSubmissionFlag::NO_MARKER_INTERRUPT));
			*timestampValueOut = s_currentRetireMarker;
		}
		else
		{
			cemu_assert_unimplemented();
		}
		return 0;
	}

	class : public COSModule
	{
		public:
		std::string_view GetName() override
		{
			return "tcl";
		}

		void RPLMapped() override
		{
			cafeExportRegister("TCL", TCLSubmitToRing, LogType::Placeholder);
			cafeExportRegister("TCL", TCLTimestamp, LogType::Placeholder);
			cafeExportRegister("TCL", TCLWaitTimestamp, LogType::Placeholder);
		};

		void rpl_entry(uint32 moduleHandle, coreinit::RplEntryReason reason) override
		{
			if (reason == coreinit::RplEntryReason::Loaded)
			{
				s_currentRetireMarker = 0;
				s_tclStatePPC->gpuRetireMarker = 0;
				coreinit::OSInitEvent(s_updateRetirementEvent.GetPtr(), coreinit::OSEvent::EVENT_STATE::STATE_NOT_SIGNALED, coreinit::OSEvent::EVENT_MODE::MODE_AUTO);
			}
			else if (reason == coreinit::RplEntryReason::Unloaded)
			{
				s_currentRetireMarker = 0;
				s_tclStatePPC->gpuRetireMarker = 0;
			}
		}
	}s_COStclModule;

	COSModule* GetModule()
	{
		return &s_COStclModule;
	}
}

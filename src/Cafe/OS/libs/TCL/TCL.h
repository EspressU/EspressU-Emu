#include "Cafe/OS/RPL/COSModule.h"

namespace TCL
{
	enum class TCLTimestampId
	{
		TIMESTAMP_LAST_BUFFER_RETIRED = 1,
	};

	enum class TCLSubmissionFlag : uint32
	{
		SURFACE_SYNC = 0x400000,
		NO_MARKER_INTERRUPT = 0x200000,
		USE_RETIRED_MARKER = 0x20000000,
	};

	int TCLTimestamp(TCLTimestampId id, uint64be* timestampOut);
	int TCLWaitTimestamp(TCLTimestampId id, uint64 waitTs, uint64 timeout);
	int TCLSubmitToRing(uint32be* cmd, uint32 cmdLen, betype<TCLSubmissionFlag>* controlFlags, uint64be* timestampValueOut);

	bool TCLGPUReadRBWord(uint32& cmdWord);
	void TCLResetRing();
	void TCLGPUNotifyNewRetirementTimestamp();

	COSModule* GetModule();
}
ENABLE_BITMASK_OPERATORS(TCL::TCLSubmissionFlag);

#include "Cafe/OS/common/OSCommon.h"
#include "coreinit_BSP.h"

namespace coreinit
{
	bool bspGetHardwareVersion(uint32be* version)
	{
		uint8 highVersion = 0x11;
		uint32 tempVers = highVersion << 24;
		*version = tempVers;
		return true;
	}

	void InitializeBSP()
	{
		cafeExportRegister("coreinit", bspGetHardwareVersion, LogType::Placeholder);
	}
}
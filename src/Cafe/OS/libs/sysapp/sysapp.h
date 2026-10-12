#include "Cafe/OS/RPL/COSModule.h"

namespace sysapp
{
	COSModule* GetModule();
}

uint64 _SYSGetSystemApplicationTitleId(sint32 index);
bool _SYSIsSystemApplicationTitleId(uint64 titleId);
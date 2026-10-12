#include "Cafe/OS/RPL/COSModule.h"

bool mic_isActive(uint32 drcIndex);
void mic_updateOnAXFrame();

void mic_setBlow(bool isBlowing);
bool mic_isBlowing();

namespace mic
{
	COSModule* GetModule();
};
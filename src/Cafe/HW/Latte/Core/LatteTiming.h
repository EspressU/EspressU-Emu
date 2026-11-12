#pragma once

#include <chrono>

void LatteTiming_setCustomVsyncFrequency(sint32 frequency);
void LatteTiming_disableCustomVsyncFrequency();
bool LatteTiming_getCustomVsyncFrequency(sint32& customFrequency);

void LatteTiming_EnableHostDrivenVSync();
bool LatteTiming_IsUsingHostDrivenVSync();

// host time of the next emulated vsync (now if it is already due)
std::chrono::steady_clock::time_point LatteTiming_GetNextVsyncDeadline();

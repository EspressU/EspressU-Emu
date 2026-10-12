#pragma once

#include <atomic>
#include <string>
#include <vector>

void LatteOverlay_init();
void LatteOverlay_render(bool pad_view);
void LatteOverlay_updateStats(double fps, sint32 drawcalls, sint32 fastDrawcalls);

void LatteOverlay_pushNotification(const std::string& text, sint32 duration);

#if BOOST_OS_IOS
extern std::atomic_bool g_overlayNativeOnIOS;

void LatteOverlay_CollectNativeText(std::vector<std::string>& stats, std::vector<std::string>& notifications);
#endif

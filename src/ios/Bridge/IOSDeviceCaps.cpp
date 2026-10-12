
#include "CemuDeviceCaps.h"
#include "Common/DeviceCapabilities.h"

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <os/proc.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

namespace
{
    std::mutex g_mutex;
    double g_screenShort = 0, g_screenLong = 0, g_screenScale = 0;
    bool g_screenIsPad = false;
    bool g_screenSet = false;
    bool g_taken = false;
    DeviceCaps::Info g_info;
    std::string g_line;

    bool supportsAppleFamily(MTL::Device* device, int n)
    {
        return device->supportsFamily(static_cast<MTL::GPUFamily>(1000 + n));
    }

    void parseChip(const std::string& gpuName, DeviceCaps::Info& info)
    {
        std::string name = gpuName;
        if (name.rfind("Apple ", 0) == 0)
            name.erase(0, 6);
        const size_t gpuPos = name.rfind(" GPU");
        if (gpuPos != std::string::npos && gpuPos + 4 == name.size())
            name.erase(gpuPos);
        if (name.size() >= 2 && (name[0] == 'A' || name[0] == 'M') && std::isdigit((unsigned char)name[1]))
        {
            info.chipSeries = name[0];
            info.chipNumber = std::atoi(name.c_str() + 1);
            std::snprintf(info.chipFamily, sizeof(info.chipFamily), "%s", name.c_str());
        }
    }

    void classifyScreen(DeviceCaps::Info& info)
    {
        if (!g_screenSet)
            return;
        info.isPad = g_screenIsPad;
        info.screenShortPoints = (uint32_t)(g_screenShort + 0.5);
        info.screenLongPoints = (uint32_t)(g_screenLong + 0.5);
        info.screenScale = (uint32_t)(g_screenScale + 0.5);
        if (g_screenIsPad)
            info.screen = info.screenShortPoints >= 1024 ? DeviceCaps::ScreenClass::PadLarge : DeviceCaps::ScreenClass::Pad;
        else
            info.screen = info.screenShortPoints <= 375 ? DeviceCaps::ScreenClass::PhoneCompact : DeviceCaps::ScreenClass::Phone;
    }

    void takeSnapshotLocked()
    {
        if (g_taken)
            return;
        DeviceCaps::Info info = DeviceCaps::FromSysctl();
        info.availableAtLaunch = (uint64_t)os_proc_available_memory();

        if (MTL::Device* device = MTL::CreateSystemDefaultDevice())
        {
            info.gpuKnown = true;
            if (NS::String* name = device->name())
                parseChip(name->utf8String() ? name->utf8String() : "", info);
            for (int n = 9; n >= 1; --n)
            {
                if (supportsAppleFamily(device, n))
                {
                    info.appleGpuFamily = n;
                    break;
                }
            }
            info.metal3 = device->supportsFamily(MTL::GPUFamilyMetal3);
            info.meshShaders = info.metal3 && info.appleGpuFamily >= 7;
            info.bcTextures = device->supportsBCTextureCompression();
            info.astcHdr = info.appleGpuFamily >= 6;
            info.maxBufferLength = (uint64_t)device->maxBufferLength();
            info.recommendedMaxWorkingSet = (uint64_t)device->recommendedMaxWorkingSetSize();
            device->release();
        }
        classifyScreen(info);

        g_info = info;
        g_line = DeviceCaps::Line(info);
        g_taken = true;
        DeviceCaps::Publish(info);
    }

    void ensureTaken()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        takeSnapshotLocked();
    }
}

extern "C" void cemu_device_caps_set_screen(double shortSidePoints, double longSidePoints, double nativeScale, bool isPad)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_screenShort = shortSidePoints;
    g_screenLong = longSidePoints;
    g_screenScale = nativeScale;
    g_screenIsPad = isPad;
    g_screenSet = true;
    if (g_taken && g_info.screen == DeviceCaps::ScreenClass::Unknown)
    {
        classifyScreen(g_info);
        g_line = DeviceCaps::Line(g_info);
        DeviceCaps::Publish(g_info);
    }
}

extern "C" void cemu_device_caps_initialize(void)
{
    ensureTaken();
}

extern "C" void cemu_device_caps_get(CemuDeviceCaps* out)
{
    if (!out)
        return;
    ensureTaken();
    std::lock_guard<std::mutex> lock(g_mutex);
    const DeviceCaps::Info& i = g_info;
    const DeviceCaps::Budgets b = DeviceCaps::ComputeBudgets(i);
    *out = CemuDeviceCaps{};
    out->chipNumber = i.chipNumber;
    out->chipSeries = i.chipSeries;
    out->tier = (int32_t)i.tier();
    out->appleGpuFamily = i.appleGpuFamily;
    out->gpuKnown = i.gpuKnown;
    out->metal3 = i.metal3;
    out->meshShaders = i.meshShaders;
    out->bcTextures = i.bcTextures;
    out->astcHdr = i.astcHdr;
    out->isHighEndSoc = i.isHighEndSoc();
    out->multicoreViable = b.multicoreViable;
    out->isPad = i.isPad;
    out->maxBufferLength = i.maxBufferLength;
    out->recommendedMaxWorkingSet = i.recommendedMaxWorkingSet;
    out->physicalMemory = i.physicalMemory;
    out->availableAtLaunch = i.availableAtLaunch;
    out->bufferCacheBytes = b.bufferCacheBytes;
    out->stagingChunkBytes = b.stagingChunkBytes;
    out->minBootHeadroomBytes = b.minBootHeadroomBytes;
    out->jitArenaStartMB = b.jitArenaStartMB;
    out->logicalCores = i.logicalCores;
    out->perfCores = i.perfCores;
    out->effCores = i.effCores;
    out->screenClass = (int32_t)i.screen;
    out->screenShortPoints = i.screenShortPoints;
    out->screenLongPoints = i.screenLongPoints;
    out->screenScale = i.screenScale;
}

extern "C" const char* cemu_device_caps_line(void)
{
    ensureTaken();
    static thread_local std::string copy;
    std::lock_guard<std::mutex> lock(g_mutex);
    copy = g_line;
    return copy.c_str();
}

extern "C" const char* cemu_device_caps_machine(void)
{
    ensureTaken();
    static thread_local std::string copy;
    std::lock_guard<std::mutex> lock(g_mutex);
    copy = g_info.machine;
    return copy.c_str();
}

extern "C" const char* cemu_device_caps_chip(void)
{
    ensureTaken();
    static thread_local std::string copy;
    std::lock_guard<std::mutex> lock(g_mutex);
    copy = g_info.chipFamily;
    return copy.c_str();
}

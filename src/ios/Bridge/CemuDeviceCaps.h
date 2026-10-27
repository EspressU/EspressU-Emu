#ifndef CEMU_DEVICE_CAPS_H
#define CEMU_DEVICE_CAPS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CemuDeviceCaps {
    int32_t  chipNumber;
    int32_t  chipSeries;
    int32_t  tier;
    int32_t  appleGpuFamily;
    bool     gpuKnown;
    bool     metal3;
    bool     meshShaders;
    bool     bcTextures;
    bool     astcHdr;
    bool     isHighEndSoc;
    bool     multicoreViable;
    bool     isPad;
    uint64_t maxBufferLength;
    uint64_t recommendedMaxWorkingSet;
    uint64_t physicalMemory;
    uint64_t availableAtLaunch;
    uint64_t bufferCacheBytes;
    uint64_t stagingChunkBytes;
    uint64_t minBootHeadroomBytes;
    uint32_t logicalCores;
    uint32_t perfCores;
    uint32_t effCores;
    int32_t  screenClass;
    uint32_t screenShortPoints;
    uint32_t screenLongPoints;
    uint32_t screenScale;
} CemuDeviceCaps;

void cemu_device_caps_set_screen(double shortSidePoints, double longSidePoints, double nativeScale, bool isPad);

void cemu_device_caps_initialize(void);

void cemu_device_caps_get(struct CemuDeviceCaps* out);

const char* cemu_device_caps_line(void);

const char* cemu_device_caps_machine(void);
const char* cemu_device_caps_chip(void);

#ifdef __cplusplus
}
#endif

#endif

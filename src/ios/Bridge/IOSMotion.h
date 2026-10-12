#ifndef IOS_MOTION_H
#define IOS_MOTION_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CEMU_BRIDGE_MOTION_SOURCE_DEVICE     = 0,
    CEMU_BRIDGE_MOTION_SOURCE_CONTROLLER = 1,
} CemuBridgeMotionSource;

typedef enum {
    CEMU_BRIDGE_MOTION_STATUS_OFF         = 0,
    CEMU_BRIDGE_MOTION_STATUS_DEVICE      = 1,
    CEMU_BRIDGE_MOTION_STATUS_CONTROLLER  = 2,
    CEMU_BRIDGE_MOTION_STATUS_UNAVAILABLE = 3,
} CemuBridgeMotionStatus;

void cemu_bridge_set_motion_enabled(bool enabled);

void cemu_bridge_set_motion_sensitivity(float sensitivity);

void cemu_bridge_set_motion_source(int source);

void cemu_bridge_set_motion_invert(bool horizontal, bool vertical);

void cemu_bridge_set_motion_diagnostic(bool enabled);

void cemu_bridge_motion_recenter(void);

int cemu_bridge_motion_status(void);

bool cemu_bridge_motion_controller_available(void);

typedef struct {
    float accelerometer[3];
    float gyroscope[3];
    double timestamp;
    uint32_t recenterCount;
} IOSMotionSample;

void IOSMotion_Start(void);

void IOSMotion_Poll(IOSMotionSample* out);

#ifdef __cplusplus
}
#endif

#endif

#pragma once

#if BOOST_OS_IOS

#include "input/api/ControllerProvider.h"
#include "input/api/InputAPI.h"

#include <vector>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <algorithm>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float x, y;
} GCBridgeVec2;

typedef struct {
    float x, y, z;
} GCBridgeVec3;

typedef struct {
    float w, x, y, z;
} GCBridgeQuat;

typedef struct {
    uint32_t buttons;
    GCBridgeVec2 leftStick;
    GCBridgeVec2 rightStick;
    float leftTrigger;
    float rightTrigger;
} GCBridgeControllerState;

typedef struct {
    GCBridgeVec3 accelerometer;
    GCBridgeVec3 gyroscope;
    GCBridgeVec3 orientation;
    GCBridgeQuat quaternion;
    double timestamp;
    uint32_t recenterCount;
} GCBridgeMotionState;

typedef GCBridgeControllerState (*GCBridgePollStateFn)(void* context);

typedef GCBridgeMotionState     (*GCBridgePollMotionFn)(void* context);

typedef void (*GCBridgeRumbleFn)(void* context, bool start);

typedef void (*GCBridgeReleaseFn)(void* context);

typedef struct {
    void* context;
    const char* display_name;
    uint8 controllerType;
    GCBridgePollStateFn  poll_state;
    GCBridgePollMotionFn poll_motion;
    GCBridgeRumbleFn     rumble;
    GCBridgeReleaseFn    release;
} GCBridgeControllerDesc;

#ifdef __cplusplus
}
#endif

class GCControllerDevice;

class GCControllerProvider : public ControllerProviderBase
{
public:
    inline static InputAPI::Type kAPIType = InputAPI::GCController;

    GCControllerProvider();
    ~GCControllerProvider() override;

    InputAPI::Type api() const override { return InputAPI::GCController; }

    std::vector<std::shared_ptr<ControllerBase>> get_controllers() override;

    void* add_controller(const GCBridgeControllerDesc& desc);
    void  remove_controller(void* handle);
    void configure_controller(void* handle, uint8 type);
    void set_order(void* const* handles, size_t count);
    bool consume_changes();

private:
    mutable std::mutex m_mutex;

    struct Entry {
        std::shared_ptr<GCControllerDevice> device;
    };

    std::unordered_map<void*, Entry> m_controllers;
    std::vector<void*> m_controller_order;
    size_t m_next_player_index = 0;
    bool m_dirty = false;
};

#endif

#pragma once

#if BOOST_OS_IOS

#include "input/api/Controller.h"
#include "GCControllerProvider.h"
#include "input/motion/MotionHandler.h"

#include <string>
#include <mutex>
#include <atomic>

class GCControllerProvider;

class GCControllerDevice : public Controller<GCControllerProvider>
{
    using base_type = Controller<GCControllerProvider>;

public:
    GCControllerDevice(size_t player_index,
                       std::string_view display_name,
                       GCBridgeControllerDesc desc);

    ~GCControllerDevice() override;

    std::string_view api_name() const override
    {
        return to_string(InputAPI::GCController);
    }

    InputAPI::Type  api()               const override { return InputAPI::GCController; }
    EmulatedController::Type type();
    bool set_type(EmulatedController::Type type);
    bool            is_connected()            override;
    bool            connect()                 override;
    void            start_rumble()            override;
    void            stop_rumble()             override;
    bool            has_motion()              override;
    bool            has_rumble()              override;
    MotionSample    get_motion_sample()        override;
    std::string     get_button_name(uint64 button) const override;
    ControllerState raw_state()               override;

    void            disconnect();
private:
    size_t                  m_player_index;
    GCBridgeControllerDesc  m_desc;
    std::atomic<bool>       m_connected{true};
    mutable std::mutex      m_mutex;
    MotionSample            m_last_motion_cache{};
    WiiUMotionHandler       m_motion_handler;
    double                  m_last_motion_ts = 0.0;
    uint32                  m_recenter_seen = 0;
    bool                    m_motion_settled = false;
};

#endif

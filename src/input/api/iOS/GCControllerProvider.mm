#if BOOST_OS_IOS

#include "GCControllerProvider.h"
#include "GCController.h"
#include "InputManager.h"

GCControllerProvider::GCControllerProvider() = default;

GCControllerProvider::~GCControllerProvider()
{
    decltype(m_controllers) controllers;
    {
        std::scoped_lock lock(m_mutex);
        controllers.swap(m_controllers);
        m_controller_order.clear();
    }
    for (auto& [handle, entry] : controllers)
    {
        entry.device->disconnect();
        delete static_cast<void**>(handle);
    }
}

std::vector<std::shared_ptr<ControllerBase>> GCControllerProvider::get_controllers()
{
    std::scoped_lock lock(m_mutex);
    std::vector<std::shared_ptr<ControllerBase>> result;
    result.reserve(m_controllers.size());
    for (auto* handle : m_controller_order)
    {
        auto it = m_controllers.find(handle);
        if (it != m_controllers.end())
            result.push_back(it->second.device);
    }
    return result;
}

void* GCControllerProvider::add_controller(const GCBridgeControllerDesc& desc)
{
    std::scoped_lock lock(m_mutex);

    size_t idx = m_next_player_index++;
    std::string name = desc.display_name ? desc.display_name : "MFi Controller";

    auto device = std::make_shared<GCControllerDevice>(idx, name, desc);
    device->connect();

    auto* key = new void*(nullptr);
    m_controllers[key] = Entry{ device };
    m_controller_order.push_back(key);
    m_dirty = true;
    return key;
}

void GCControllerProvider::remove_controller(void* handle)
{
    std::shared_ptr<GCControllerDevice> device;
    {
        std::scoped_lock lock(m_mutex);
        auto it = m_controllers.find(handle);
        if (it == m_controllers.end())
            return;
        device = std::move(it->second.device);
        m_controllers.erase(it);
        m_dirty = true;
        m_controller_order.erase(std::remove(m_controller_order.begin(), m_controller_order.end(), handle),
                                 m_controller_order.end());
        delete static_cast<void**>(handle);
    }
    device->disconnect();
}

void GCControllerProvider::configure_controller(void* handle, uint8 type)
{
    if (type >= EmulatedController::Type::MAX) return;
    std::scoped_lock lock(m_mutex);
    auto it = m_controllers.find(handle);
    if (it != m_controllers.end())
        m_dirty |= it->second.device->set_type(static_cast<EmulatedController::Type>(type));
}

void GCControllerProvider::set_order(void* const* handles, size_t count)
{
    std::scoped_lock lock(m_mutex);
    std::vector<void*> order;
    for (size_t i = 0; i < count; ++i)
        if (m_controllers.count(handles[i]) &&
            std::find(order.begin(), order.end(), handles[i]) == order.end())
            order.push_back(handles[i]);

    if (order.size() == m_controllers.size() && order != m_controller_order)
    {
        m_controller_order = std::move(order);
        m_dirty = true;
    }
}

bool GCControllerProvider::consume_changes()
{
    std::scoped_lock lock(m_mutex);
    bool dirty = m_dirty;
    m_dirty = false;
    return dirty;
}

static std::shared_ptr<GCControllerProvider> get_provider()
{
    auto ptr = InputManager::instance().get_api_provider(InputAPI::GCController);
    return std::static_pointer_cast<GCControllerProvider>(ptr);
}

extern "C" {

void* GCControllerBridge_add(const GCBridgeControllerDesc* desc)
{
    if (!desc || desc->controllerType >= EmulatedController::Type::MAX) return nullptr;
    auto p = get_provider();
    if (!p) return nullptr;
    void* handle = p->add_controller(*desc);
    return handle;
}

void GCControllerBridge_remove(void* handle)
{
    if (!handle) return;
    auto p = get_provider();
    if (p) p->remove_controller(handle);
}

void GCControllerBridge_configure(void* handle, uint8 type)
{
    if (auto p = get_provider()) p->configure_controller(handle, type);
}

void GCControllerBridge_setOrder(void* const* handles, size_t count)
{
    if (count && !handles) return;
    if (auto p = get_provider()) p->set_order(handles, count);
}

void GCControllerBridge_notifyChanged(void)
{
    if (auto p = get_provider(); p && p->consume_changes())
        InputManager::instance().on_device_changed();
}

}

#endif

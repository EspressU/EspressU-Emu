#include "Cafe/HW/Latte/Renderer/Metal/MetalLayerHandle.h"
#include "Cafe/HW/Latte/Core/Latte.h"
#include "Cafe/HW/Latte/Renderer/Metal/MetalLayer.h"
#include "Cafe/HW/Latte/Core/HostTuning.h"

#include "Cafe/HW/Latte/Core/PerfTelemetry.h"
#include "gui/interface/WindowSystem.h"

MetalLayerHandle::MetalLayerHandle(MTL::Device* device, const Vector2i& size, bool mainWindow)
{
    const auto& windowInfo = (mainWindow ? WindowSystem::GetWindowInfo().window_main : WindowSystem::GetWindowInfo().window_pad);

    m_device = device;
    m_isMainWindow = mainWindow;
    m_layer = (CA::MetalLayer*)CreateMetalLayer(windowInfo.surface, m_layerScaleX, m_layerScaleY);
    m_layer->setDevice(device);

    const auto& windowSystemInfo = WindowSystem::GetWindowInfo();
    const double authoritativeScale = mainWindow ? windowSystemInfo.dpi_scale.load()
                                                 : windowSystemInfo.pad_dpi_scale.load();
    if (authoritativeScale > 0.0)
    {
        m_layerScaleX = (float)authoritativeScale;
        m_layerScaleY = (float)authoritativeScale;
    }

    m_layer->setDrawableSize(CGSize{(float)size.x * m_layerScaleX, (float)size.y * m_layerScaleY});
    m_layer->setFramebufferOnly(true);
    m_lastGoodWidth = (double)size.x * m_layerScaleX;
    m_lastGoodHeight = (double)size.y * m_layerScaleY;
}

MetalLayerHandle::~MetalLayerHandle()
{
    if (m_drawable)
        m_drawable->release();
}

MetalLayerHandle::MetalLayerHandle(MetalLayerHandle&& other) noexcept
    : m_layer(other.m_layer), m_layerScaleX(other.m_layerScaleX), m_layerScaleY(other.m_layerScaleY),
      m_drawable(other.m_drawable), m_outputStage(other.m_outputStage), m_stageTarget(other.m_stageTarget), m_device(other.m_device), m_isMainWindow(other.m_isMainWindow),
      m_lastGoodWidth(other.m_lastGoodWidth), m_lastGoodHeight(other.m_lastGoodHeight), m_acquireCount(other.m_acquireCount),
      m_skipAcquires(other.m_skipAcquires), m_starvedCount(other.m_starvedCount)
{
    other.m_layer = nullptr;
    other.m_drawable = nullptr;
    other.m_stageTarget = nullptr;
}

MetalLayerHandle& MetalLayerHandle::operator=(MetalLayerHandle&& other) noexcept
{
    if (this == &other)
        return *this;
    if (m_drawable)
        m_drawable->release();
    m_layer = other.m_layer;
    m_layerScaleX = other.m_layerScaleX;
    m_layerScaleY = other.m_layerScaleY;
    m_drawable = other.m_drawable;
    m_outputStage = other.m_outputStage;
    m_stageTarget = other.m_stageTarget;
    m_device = other.m_device;
    m_isMainWindow = other.m_isMainWindow;
    m_lastGoodWidth = other.m_lastGoodWidth;
    m_lastGoodHeight = other.m_lastGoodHeight;
    m_acquireCount = other.m_acquireCount;
    m_skipAcquires = other.m_skipAcquires;
    m_starvedCount = other.m_starvedCount;
    other.m_layer = nullptr;
    other.m_drawable = nullptr;
    other.m_stageTarget = nullptr;
    return *this;
}

void MetalLayerHandle::Resize(const Vector2i& size, double scale)
{
    if (!m_layer)
        return;
    if (scale >= 0.0)
    {
        m_layerScaleX = (float)scale;
        m_layerScaleY = (float)scale;
    }
    const double width = (double)size.x * m_layerScaleX;
    const double height = (double)size.y * m_layerScaleY;
    if (width < 1.0 || height < 1.0)
    {
        cemuLog_log(LogType::Force, "layer {} ignoring a degenerate resize to {}x{}", (void*)this, width, height);
        return;
    }
    m_layer->setDrawableSize(CGSize{width, height});
    m_lastGoodWidth = width;
    m_lastGoodHeight = height;
}

void MetalLayerHandle::RecordLayerState() const
{
    if (!m_isMainWindow || !m_layer)
        return;
    auto& state = LatteWait::Get();
    const CGSize drawableSize = m_layer->drawableSize();
    state.tvDrawableWidth.store((uint32_t)drawableSize.width, std::memory_order_relaxed);
    state.tvDrawableHeight.store((uint32_t)drawableSize.height, std::memory_order_relaxed);
    state.tvLayerHasDevice.store(m_layer->device() != nullptr, std::memory_order_relaxed);
}

void MetalLayerHandle::RepairLayer()
{
    if (m_device && m_layer->device() != m_device)
    {
        cemuLog_log(LogType::Force, "layer {} lost its Metal device, setting it again", (void*)this);
        m_layer->setDevice(m_device);
    }
    const CGSize drawableSize = m_layer->drawableSize();
    if ((drawableSize.width < 1.0 || drawableSize.height < 1.0) && m_lastGoodWidth >= 1.0 && m_lastGoodHeight >= 1.0)
    {
        cemuLog_log(LogType::Force, "layer {} has a {}x{} drawable, restoring {}x{}", (void*)this, drawableSize.width, drawableSize.height, m_lastGoodWidth, m_lastGoodHeight);
        m_layer->setDrawableSize(CGSize{m_lastGoodWidth, m_lastGoodHeight});
    }
}

bool MetalLayerHandle::AcquireDrawable()
{
    if (m_drawable || m_stageTarget)
        return true;
    if (m_skippingFrame)
        return false;

    if (m_outputStage && m_layer)
    {
        const CGSize drawableSize = m_layer->drawableSize();
        bool busy = false;
        m_stageTarget = m_outputStage->BeginFrame(m_isMainWindow ? Metal4OutputStage::TV : Metal4OutputStage::GamePad,
            (uint32_t)drawableSize.width, (uint32_t)drawableSize.height, (uint32_t)m_layer->pixelFormat(), busy);
        if (m_stageTarget)
        {
            if (m_isMainWindow)
            {
                LatteWait::Get().drawableFailuresInARow.store(0, std::memory_order_relaxed);
                LatteWait::Get().tvDrawableHeld.store(true, std::memory_order_relaxed);
            }
            return true;
        }
        // All slots are still on their way to the screen: this frame is not shown, and the GPU thread carries on
        // instead of waiting for the display.
        if (busy)
        {
            m_skippingFrame = true;
            return false;
        }
        // The stage could not take this frame; it goes through the regular drawable instead.
        HostTuning::Count(HostTuning::GetStats().m4RegularPath);
    }

    if (m_skipAcquires > 0)
    {
        --m_skipAcquires;
        return false;
    }

    const uint64_t acquireStart = PerfTelemetry::NowNs();
    {
        LatteWait::Scope waitScope("waiting for a screen drawable", LatteWait::Kind::Display);
        m_drawable = m_layer->nextDrawable();
    }
    const uint64_t acquireNs = PerfTelemetry::NowNs() - acquireStart;
    if (!m_drawable || acquireNs >= 250ull * 1000000ull)
    {
        m_skipAcquires = 30;
        if (m_starvedCount++ % 20 == 0)
            cemuLog_log(LogType::Force, "layer {} ({}) is not returning drawables ({} ms for {}); skipping its next {} presents ({} times so far)",
                (void*)this, m_isMainWindow ? "TV" : "GamePad", acquireNs / 1000000ull, m_drawable ? "a drawable" : "nothing", m_skipAcquires, m_starvedCount);
    }
    if (!m_drawable)
    {
        auto& state = LatteWait::Get();
        if (m_isMainWindow)
        {
            state.drawableFailures.fetch_add(1, std::memory_order_relaxed);
            const uint32_t inARow = state.drawableFailuresInARow.fetch_add(1, std::memory_order_relaxed) + 1;
            RecordLayerState();
            if (inARow == 1 || inARow % 120 == 0)
            {
                const CGSize drawableSize = m_layer->drawableSize();
                cemuLog_log(LogType::Force, "layer {} failed to acquire next drawable ({} in a row), drawable size {}x{}, device {}", (void*)this, inARow, drawableSize.width, drawableSize.height, m_layer->device() ? "set" : "MISSING");
            }
            if (inARow == 3)
                RepairLayer();
        }
        else
        {
            cemuLog_log(LogType::Force, "layer {} failed to acquire next drawable", (void*)this);
        }
        return false;
    }
    m_drawable->retain();
    if (m_isMainWindow)
    {
        LatteWait::Get().drawableFailuresInARow.store(0, std::memory_order_relaxed);
        LatteWait::Get().tvDrawableHeld.store(true, std::memory_order_relaxed);
        if ((m_acquireCount++ % 120) == 0)
            RecordLayerState();
    }

    return true;
}

void MetalLayerHandle::PresentDrawable(MTL::CommandBuffer* commandBuffer)
{
    const uint32 swapInterval = LatteGPUState.sharedArea ? LatteGPUState.sharedArea->swapInterval : 0;
    const bool paced = g_latteFullSpeedRenders.load(std::memory_order_relaxed) && swapInterval > 0 && swapInterval <= 4;
    if (m_stageTarget)
    {
        m_stageTarget = nullptr;
        m_outputStage->PresentFrame(m_isMainWindow ? Metal4OutputStage::TV : Metal4OutputStage::GamePad, commandBuffer, m_layer,
            paced ? (double)swapInterval / 60.0 - 0.0015 : 0.0);
    }
    else
    {
        if (paced)
            commandBuffer->presentDrawableAfterMinimumDuration(m_drawable, (double)swapInterval / 60.0 - 0.0015);
        else
            commandBuffer->presentDrawable(m_drawable);
        m_drawable->release();
        m_drawable = nullptr;
    }
    if (m_isMainWindow)
    {
        LatteWait::Get().tvDrawableHeld.store(false, std::memory_order_relaxed);
        LatteWait::Get().presentedFrames.fetch_add(1, std::memory_order_relaxed);
    }
}

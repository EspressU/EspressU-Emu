#pragma once

#include <QuartzCore/QuartzCore.hpp>

#include "Cafe/HW/Latte/Renderer/Metal/MetalCommon.h"
#include "util/math/vector2.h"

class MetalLayerHandle
{
public:
    MetalLayerHandle() = default;
    MetalLayerHandle(MTL::Device* device, const Vector2i& size, bool mainWindow);

    ~MetalLayerHandle();

    MetalLayerHandle(const MetalLayerHandle&) = delete;
    MetalLayerHandle& operator=(const MetalLayerHandle&) = delete;
    MetalLayerHandle(MetalLayerHandle&& other) noexcept;
    MetalLayerHandle& operator=(MetalLayerHandle&& other) noexcept;

    void Resize(const Vector2i& size, double scale = -1.0);

    bool AcquireDrawable();

    void PresentDrawable(MTL::CommandBuffer* commandBuffer);

    CA::MetalLayer* GetLayer() const { return m_layer; }

    CA::MetalDrawable* GetDrawable() const { return m_drawable; }

    MTL::Texture* GetDrawableTexture() const { return m_drawable ? m_drawable->texture() : nullptr; }

private:
    CA::MetalLayer* m_layer = nullptr;
    float m_layerScaleX = 1.0f;
    float m_layerScaleY = 1.0f;

    CA::MetalDrawable* m_drawable = nullptr;

    MTL::Device* m_device = nullptr;
    bool m_isMainWindow = false;
    double m_lastGoodWidth = 0.0;
    double m_lastGoodHeight = 0.0;
    uint32_t m_acquireCount = 0;
    uint32_t m_skipAcquires = 0;
    uint32_t m_starvedCount = 0;

    void RecordLayerState() const;
    void RepairLayer();
};

#include "Cafe/HW/Latte/Renderer/Metal/MetalLayer.h"

#include "Cafe/HW/Latte/Renderer/MetalView.h"

void* CreateMetalLayer(void* handle, float& scaleX, float& scaleY)
{
    CGFloat screenScale = [UIScreen mainScreen].scale;
    scaleX = (float)screenScale;
    scaleY = (float)screenScale;

    return (__bridge void*)handle;
}

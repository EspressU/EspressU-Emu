#include "Cafe/HW/Latte/Renderer/Vulkan/CocoaSurface.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"

#include "Cafe/HW/Latte/Renderer/MetalView.h"

#import <Metal/Metal.h>

VkSurfaceKHR CreateCocoaSurface(VkInstance instance, void* handle)
{
    VkMetalSurfaceCreateInfoEXT surface;
    surface.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
    surface.pNext = NULL;
    surface.flags = 0;
    surface.pLayer = (CAMetalLayer*)handle;

    VkSurfaceKHR result;
    VkResult err;
    if ((err = vkCreateMetalSurfaceEXT(instance, &surface, nullptr, &result)) != VK_SUCCESS)
    {
        cemuLog_log(LogType::Force, "Cannot create a Metal Vulkan surface: {}", (sint32)err);
        throw std::runtime_error(fmt::format("Cannot create a Metal Vulkan surface: {}", err));
    }

    return result;
}

std::string GetAppleGpuFamilyDescription()
{
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device)
        return "no Metal device";

    int highest = 0;
    for (int fam = 1; fam <= 9; fam++)
    {
        if ([device supportsFamily:(MTLGPUFamily)(1000 + fam)])
            highest = fam;
    }

    std::string result = std::string([[device name] UTF8String] ? [[device name] UTF8String] : "?");
    result += highest ? (" / Apple GPU family " + std::to_string(highest)) : std::string(" / Apple GPU family unknown");
    bool bc = false;
    if (@available(iOS 16.4, macOS 11.0, *))
        bc = [device supportsBCTextureCompression];
    result += std::string(" / Metal BC textures: ") + (bc ? "yes" : "no");
    return result;
}

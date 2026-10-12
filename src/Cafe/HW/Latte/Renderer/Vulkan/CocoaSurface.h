#pragma once

#if BOOST_OS_MACOS || BOOST_OS_IOS

#include <vulkan/vulkan.h>
#include <string>

VkSurfaceKHR CreateCocoaSurface(VkInstance instance, void* handle);

std::string GetAppleGpuFamilyDescription();

#endif

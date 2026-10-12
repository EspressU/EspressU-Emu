#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanRenderer.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/LatteTextureVk.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/RendererShaderVk.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanTextureReadback.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/CocoaSurface.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanPipelineCompiler.h"

#include "Cafe/HW/Latte/Core/LatteBufferCache.h"
#include "Cafe/HW/Latte/Core/LattePerformanceMonitor.h"
#include "Cafe/HW/Latte/Core/LatteOverlay.h"
#include "Cafe/HW/Latte/Core/LatteTextureLoaderASTC.h"

#include "Cafe/HW/Latte/LegacyShaderDecompiler/LatteDecompiler.h"

#include "Cafe/CafeSystem.h"

#include "util/helpers/helpers.h"
#include "util/helpers/StringHelpers.h"

#include "config/ActiveSettings.h"
#include "config/CemuConfig.h"
#include "WindowSystem.h"

#include "imgui/imgui_extension.h"
#include "imgui/imgui_impl_vulkan.h"

#include "Cafe/TitleList/GameInfo.h"

#include "Cafe/HW/Latte/Core/LatteTiming.h"
#include "Cafe/HW/Latte/Core/PerfTelemetry.h"
#include "Cafe/HW/Latte/Core/LatteWaitInfo.h"

#include <cstdint>
#include <cstdlib>
#if BOOST_OS_IOS
#include <os/proc.h>
#endif
#include <thread>
#include <chrono>
#include <array>
#include <algorithm>
#include <glslang/Public/ShaderLang.h>

#ifndef VK_API_VERSION_MAJOR
#define VK_API_VERSION_MAJOR(version) (((uint32_t)(version) >> 22) & 0x7FU)
#define VK_API_VERSION_MINOR(version) (((uint32_t)(version) >> 12) & 0x3FFU)
#endif

extern std::atomic_int g_compiling_pipelines;
extern std::atomic_int g_compiling_pipelines_async;

const  std::vector<const char*> kOptionalDeviceExtensions =
{
	VK_EXT_DEPTH_RANGE_UNRESTRICTED_EXTENSION_NAME,
	VK_NV_FILL_RECTANGLE_EXTENSION_NAME,
	VK_EXT_PIPELINE_CREATION_FEEDBACK_EXTENSION_NAME,
	VK_EXT_FILTER_CUBIC_EXTENSION_NAME,
	VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME,
	VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
	VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME,
	VK_KHR_PRESENT_WAIT_EXTENSION_NAME,
	VK_KHR_PRESENT_ID_EXTENSION_NAME,
	VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME,
	VK_EXT_PIPELINE_ROBUSTNESS_EXTENSION_NAME
};

const std::vector<const char*> kRequiredDeviceExtensions =
{
	VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

VKAPI_ATTR VkBool32 VKAPI_CALL DebugUtilsCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageTypes, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData)
{
#ifdef CEMU_DEBUG_ASSERT

	if (strstr(pCallbackData->pMessage, "consumes input location"))
		return VK_FALSE;
	if (strstr(pCallbackData->pMessage, "blend"))
		return VK_FALSE;

	if (pCallbackData->messageIdNumber == 0x6c3b517c || pCallbackData->messageIdNumber == 0xffffffffa6b17cdf || pCallbackData->messageIdNumber == 0xffffffffc406fcb7)
		return VK_FALSE;
	if (pCallbackData->messageIdNumber == 0x6e633069)
		return VK_FALSE;
	if (pCallbackData->messageIdNumber == 0xffffffffb408bc0b)
		return VK_FALSE;

	if (pCallbackData->messageIdNumber == 0x6bbb14)
		return VK_FALSE;

	if (strstr(pCallbackData->pMessage, "Number of currently valid sampler objects is not less than the maximum allowed"))
		return VK_FALSE;

#endif

	cemuLog_log(LogType::Force, (char*)pCallbackData->pMessage);

	return VK_FALSE;
}

static constexpr const char* kPortabilityEnumerationExtName = "VK_KHR_portability_enumeration";
static constexpr const char* kPortabilitySubsetExtName = "VK_KHR_portability_subset";
static constexpr VkInstanceCreateFlags kInstanceEnumeratePortabilityBit = 0x00000001;

static bool IsInstanceExtensionAvailable(const char* name)
{
	uint32_t count = 0;
	if (vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS || count == 0)
		return false;
	std::vector<VkExtensionProperties> props(count);
	if (vkEnumerateInstanceExtensionProperties(nullptr, &count, props.data()) != VK_SUCCESS)
		return false;
	for (const auto& p : props)
		if (strcmp(p.extensionName, name) == 0)
			return true;
	return false;
}

std::vector<VulkanRenderer::DeviceInfo> VulkanRenderer::GetDevices()
{
    if(!vkEnumerateInstanceVersion)
    {
        cemuLog_log(LogType::Force, "Vulkan cant list devices because Vulkan loader failed");
        return {};
    }
	uint32 apiVersion = VK_API_VERSION_1_1;
	if (vkEnumerateInstanceVersion(&apiVersion) != VK_SUCCESS)
	{
		if (VK_API_VERSION_MAJOR(apiVersion) < 1 || VK_API_VERSION_MINOR(apiVersion) < 2)
			apiVersion = VK_API_VERSION_1_1;
	}

	std::vector<DeviceInfo> result;

	std::vector<const char*> requiredExtensions;
	requiredExtensions.clear();
	requiredExtensions.emplace_back(VK_KHR_SURFACE_EXTENSION_NAME);
	#if BOOST_OS_WINDOWS
	requiredExtensions.emplace_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
	#elif BOOST_OS_LINUX || BOOST_OS_BSD
	auto backend = WindowSystem::GetWindowInfo().window_main.backend;
	if(backend == WindowSystem::WindowHandleInfo::Backend::X11)
		requiredExtensions.emplace_back(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
	#ifdef HAS_WAYLAND
	else if (backend == WindowSystem::WindowHandleInfo::Backend::Wayland)
		requiredExtensions.emplace_back(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
	#endif
	#elif BOOST_OS_MACOS || BOOST_OS_IOS
	requiredExtensions.emplace_back(VK_EXT_METAL_SURFACE_EXTENSION_NAME);
	#endif
	const bool portabilityEnumeration = IsInstanceExtensionAvailable(kPortabilityEnumerationExtName);
	if (portabilityEnumeration)
		requiredExtensions.emplace_back(kPortabilityEnumerationExtName);

	VkApplicationInfo app_info{};
	app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app_info.pApplicationName = EMULATOR_NAME;
	app_info.applicationVersion = VK_MAKE_VERSION(EMULATOR_VERSION_MAJOR, EMULATOR_VERSION_MINOR, EMULATOR_VERSION_PATCH);
	app_info.pEngineName = EMULATOR_NAME;
	app_info.engineVersion = app_info.applicationVersion;
	app_info.apiVersion = apiVersion;

	VkInstanceCreateInfo create_info{};
	create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	create_info.pApplicationInfo = &app_info;
	create_info.ppEnabledExtensionNames = requiredExtensions.data();
	create_info.enabledExtensionCount = requiredExtensions.size();
	if (portabilityEnumeration)
		create_info.flags |= kInstanceEnumeratePortabilityBit;
	create_info.ppEnabledLayerNames = nullptr;
	create_info.enabledLayerCount = 0;

	VkInstance instance = nullptr;
	try
	{
		VkResult err;
		if ((err = vkCreateInstance(&create_info, nullptr, &instance)) != VK_SUCCESS)
			throw std::runtime_error(fmt::format("Unable to create a Vulkan instance: {}", err));

		if (!InitializeInstanceVulkan(instance))
			throw std::runtime_error("can't initialize instanced vulkan functions");

		uint32_t device_count = 0;
		vkEnumeratePhysicalDevices(instance, &device_count, nullptr);
		if (device_count == 0)
			throw std::runtime_error("Failed to find a GPU with Vulkan support.");

		auto surface = CreateFramebufferSurface(instance, WindowSystem::GetWindowInfo().window_main);
		std::vector<VkPhysicalDevice> devices(device_count);
		vkEnumeratePhysicalDevices(instance, &device_count, devices.data());
		for (const auto& device : devices)
		{
			if (IsDeviceSuitable(surface, device))
			{
				VkPhysicalDeviceIDProperties physDeviceIDProps = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES };
				VkPhysicalDeviceProperties2 physDeviceProps = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
				physDeviceProps.pNext = &physDeviceIDProps;
				vkGetPhysicalDeviceProperties2(device, &physDeviceProps);

				result.emplace_back(physDeviceProps.properties.deviceName, physDeviceIDProps.deviceUUID);
			}
		}
		vkDestroySurfaceKHR(instance, surface, nullptr);
	}
	catch (...)
	{
	}

	if (instance)
		vkDestroyInstance(instance, nullptr);

	return result;

}

void VulkanRenderer::DetermineVendor()
{
	VkPhysicalDeviceProperties2 properties{};
	VkPhysicalDeviceDriverProperties driverProperties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES };
	properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	if (m_featureControl.deviceExtensions.driver_properties)
		properties.pNext = &driverProperties;

	vkGetPhysicalDeviceProperties2(m_physicalDevice, &properties);
	switch (properties.properties.vendorID)
	{
	case 0x10DE:
		m_vendor = GfxVendor::Nvidia;
		break;
	case 0x8086:
		m_vendor = GfxVendor::Intel;
		break;
	case 0x1002:
		m_vendor = GfxVendor::AMD;
		break;
	case 0x106B:
		m_vendor = GfxVendor::Apple;
		break;
	}

	VkDriverId driverId = driverProperties.driverID;

	if(driverId == VK_DRIVER_ID_MESA_RADV || driverId == VK_DRIVER_ID_INTEL_OPEN_SOURCE_MESA)
		m_vendor = GfxVendor::Mesa;

	cemuLog_log(LogType::Force, "Using GPU: {}", properties.properties.deviceName);

	if (m_featureControl.deviceExtensions.driver_properties)
	{
		cemuLog_log(LogType::Force, "Driver version: {}", driverProperties.driverInfo);

		if(m_vendor == GfxVendor::Nvidia)
		{
			m_featureControl.disableMultithreadedCompilation = (StringHelpers::ToInt(std::string(driverProperties.driverInfo)) < 515);
		}
	}

	else
	{
		cemuLog_log(LogType::Force, "Driver version (as stored in device info): {:08}", properties.properties.driverVersion);

		if(m_vendor == GfxVendor::Nvidia)
		{
			m_featureControl.disableMultithreadedCompilation = true;
		}
	}
}

void VulkanRenderer::GetDeviceFeatures()
{
	void* prevStruct = nullptr;
	VkPhysicalDeviceCustomBorderColorFeaturesEXT bcf{};
	bcf.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT;
	bcf.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT;
	prevStruct = &bcf;

	VkPhysicalDevicePipelineCreationCacheControlFeaturesEXT pcc{};
	pcc.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_CREATION_CACHE_CONTROL_FEATURES_EXT;
	pcc.pNext = prevStruct;
	prevStruct = &pcc;

	VkPhysicalDevicePresentIdFeaturesKHR pidf{};
	pidf.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR;
	pidf.pNext = prevStruct;
	prevStruct = &pidf;

	VkPhysicalDevicePresentWaitFeaturesKHR pwf{};
	pwf.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR;
	pwf.pNext = prevStruct;
	prevStruct = &pwf;

	VkPhysicalDevicePipelineRobustnessFeaturesEXT pprf{};
	if (m_featureControl.deviceExtensions.pipeline_robustness)
	{
		pprf.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_ROBUSTNESS_FEATURES_EXT;
		pprf.pNext = prevStruct;
		prevStruct = &pprf;
	}

	VkPhysicalDeviceFeatures2 physicalDeviceFeatures2{};
	physicalDeviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	physicalDeviceFeatures2.pNext = prevStruct;

	vkGetPhysicalDeviceFeatures2(m_physicalDevice, &physicalDeviceFeatures2);

	cemuLog_log(LogType::Force, "Vulkan: present_wait extension: {}", (pwf.presentWait && pidf.presentId) ? "supported" : "unsupported");

	VkPhysicalDeviceFloatControlsPropertiesKHR pfcp{};
	prevStruct = nullptr;
	if (m_featureControl.deviceExtensions.shader_float_controls)
	{
		pfcp.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES_KHR;
		pfcp.pNext = prevStruct;
		prevStruct = &pfcp;
	}

	VkPhysicalDeviceProperties2 prop2{};
	prop2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	prop2.pNext = prevStruct;

	vkGetPhysicalDeviceProperties2(m_physicalDevice, &prop2);

	m_featureControl.deviceExtensions.pipeline_creation_cache_control = pcc.pipelineCreationCacheControl;
	m_featureControl.deviceExtensions.custom_border_color_without_format = m_featureControl.deviceExtensions.custom_border_color && bcf.customBorderColors && bcf.customBorderColorWithoutFormat;
	m_featureControl.deviceExtensions.present_wait = m_featureControl.deviceExtensions.present_wait && pwf.presentWait && pidf.presentId;
	m_featureControl.shaderFloatControls.shaderRoundingModeRTEFloat32 = m_featureControl.deviceExtensions.shader_float_controls && pfcp.shaderRoundingModeRTEFloat32;
	if(!m_featureControl.shaderFloatControls.shaderRoundingModeRTEFloat32)
		cemuLog_log(LogType::Force, "Shader round mode control not available on this device or driver. Some rendering issues might occur.");

	if (!m_featureControl.deviceExtensions.pipeline_creation_cache_control)
	{
		cemuLog_log(LogType::Force, "VK_EXT_pipeline_creation_cache_control not supported. Cannot use asynchronous shader and pipeline compilation");
		if (GetConfig().async_compile)
			LatteOverlay_pushNotification(_tr("Async shader compile is enabled but not supported by the graphics driver\nCemu will use synchronous compilation which can cause additional stutter"), 10000);
	}
	if (!m_featureControl.deviceExtensions.custom_border_color_without_format)
	{
		if (m_featureControl.deviceExtensions.custom_border_color)
		{
			cemuLog_log(LogType::Force, "VK_EXT_custom_border_color is present but only with limited support. Cannot emulate arbitrary border color");
		}
		else
		{
			cemuLog_log(LogType::Force, "VK_EXT_custom_border_color not supported. Cannot emulate arbitrary border color");
		}
	}
	if (!m_featureControl.deviceExtensions.depth_clip_enable)
	{
		cemuLog_log(LogType::Force, "VK_EXT_depth_clip_enable not supported");
	}
	if (m_featureControl.deviceExtensions.pipeline_robustness)
	{
		if ( pprf.pipelineRobustness != VK_TRUE )
			m_featureControl.deviceExtensions.pipeline_robustness = false;
	}
	m_featureControl.limits.minUniformBufferOffsetAlignment = std::max(prop2.properties.limits.minUniformBufferOffsetAlignment, (VkDeviceSize)4);
	m_featureControl.limits.nonCoherentAtomSize = std::max(prop2.properties.limits.nonCoherentAtomSize, (VkDeviceSize)4);
	cemuLog_log(LogType::Force, fmt::format("VulkanLimits: UBAlignment {0} nonCoherentAtomSize {1}", prop2.properties.limits.minUniformBufferOffsetAlignment, prop2.properties.limits.nonCoherentAtomSize));
}

#if BOOST_OS_LINUX
#include <sys/wait.h>
#include "resource/IconsFontAwesome5.h"

int BreathOfTheWildChildProcessMain()
{
	InitializeGlobalVulkan();
	struct sigaction sa{};
	sa.sa_handler = [](int unused) { _exit(1); };

	int ret = sigaction(SIGABRT, &sa, nullptr);

	freopen("/dev/null", "w", stderr);

	setenv("RADV_DEBUG", "llvm", 1);

	VkInstanceCreateInfo create_info{};
	create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	VkInstance instance = VK_NULL_HANDLE;
	if (vkCreateInstance(&create_info, nullptr, &instance) != VK_SUCCESS)
		return 1;
	InitializeInstanceVulkan(instance);

	uint32_t count = 0;
	vkEnumeratePhysicalDevices(instance, &count, nullptr);

	vkDestroyInstance(instance, nullptr);
	return 0;
}

static void LinuxBreathOfTheWildWorkaround(VkInstance& instance, const VkInstanceCreateInfo* create_info)
{

	const char* debugEnvC = getenv("RADV_DEBUG");
	std::string_view debugEnv = debugEnvC != nullptr ? debugEnvC : "";
	if (debugEnv.find("aco") != std::string_view::npos || debugEnv.find("llvm") != std::string_view::npos)
		return;

	uint32_t count = 0;
	vkEnumeratePhysicalDevices(instance, &count, nullptr);

	std::vector<VkPhysicalDevice> physicalDevices{count};
	vkEnumeratePhysicalDevices(instance, &count, physicalDevices.data());

	int version = 0;
	for (auto& i : physicalDevices)
	{
		VkPhysicalDeviceDriverProperties driverProps{};
		driverProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
		VkPhysicalDeviceProperties2 prop{};
		prop.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		prop.pNext = &driverProps;
		vkGetPhysicalDeviceProperties2(i, &prop);
		if (prop.properties.vendorID != 0x1002 || driverProps.driverID != VK_DRIVER_ID_MESA_RADV)
			continue;

		version = prop.properties.driverVersion;
		break;
	}

	if (version == 0)
		return;

	int major = VK_API_VERSION_MAJOR(version);
	int minor = VK_API_VERSION_MINOR(version);
	int patch = VK_API_VERSION_PATCH(version);

	if ((major <= 25 && minor < 3) || (major == 26 && (minor > 0 || patch >= 5)) || major > 26)
		return;

	int childID = fork();
	if (childID == 0)
	{
		setenv("CEMU_DETECT_RADV","1", 1);
		execl("/proc/self/exe", "/proc/self/exe", nullptr);
		_exit(2);
	}

	int childStatus = 0;
	waitpid(childID,  &childStatus, 0);

	if (!WIFEXITED(childStatus) || WEXITSTATUS(childStatus) == 2)
	{
		cemuLog_log(LogType::Force, "BOTW/RADV workaround not applied because detecting LLVM presence failed unexpectedly");
		return;
	}

	if (WEXITSTATUS(childStatus) == 1)
		cemuLog_log(LogType::Force, "BOTW/RADV workaround not applied because mesa was built without LLVM");

	if (WEXITSTATUS(childStatus) != 0)
		return;

	cemuLog_log(LogType::Force, "BOTW/RADV workaround active. Adding \"llvm\" to RADV_DEBUG environment variable");
	if (debugEnv.empty())
	{
		setenv("RADV_DEBUG", "llvm", 1);
	}
	else
	{
		std::string appendedDebugEnv{debugEnv};
		appendedDebugEnv.append(",llvm");
		setenv("RADV_DEBUG", appendedDebugEnv.c_str(), 1);
	}

	vkDestroyInstance(instance, nullptr);
	VkResult err = vkCreateInstance(create_info, nullptr, &instance);
	if (err != VK_SUCCESS)
		throw std::runtime_error(fmt::format("Unable to re-create a Vulkan instance after RADV/LLVM workaround: {}", err));
	InitializeInstanceVulkan(instance);

	LatteOverlay_pushNotification(std::string{(const char*)ICON_FA_EXCLAMATION_TRIANGLE} + "RADV_DEBUG=llvm set automatically to avoid crashing due to a driver bug. If possible update mesa to 26.0.5 or newer", 10'000);

}

#endif

static const std::array<const char*, 55> kVkFeatureNames = {
	"robustBufferAccess", "fullDrawIndexUint32", "imageCubeArray", "independentBlend", "geometryShader", "tessellationShader",
	"sampleRateShading", "dualSrcBlend", "logicOp", "multiDrawIndirect", "drawIndirectFirstInstance", "depthClamp", "depthBiasClamp",
	"fillModeNonSolid", "depthBounds", "wideLines", "largePoints", "alphaToOne", "multiViewport", "samplerAnisotropy",
	"textureCompressionETC2", "textureCompressionASTC_LDR", "textureCompressionBC", "occlusionQueryPrecise", "pipelineStatisticsQuery",
	"vertexPipelineStoresAndAtomics", "fragmentStoresAndAtomics", "shaderTessellationAndGeometryPointSize", "shaderImageGatherExtended",
	"shaderStorageImageExtendedFormats", "shaderStorageImageMultisample", "shaderStorageImageReadWithoutFormat",
	"shaderStorageImageWriteWithoutFormat", "shaderUniformBufferArrayDynamicIndexing", "shaderSampledImageArrayDynamicIndexing",
	"shaderStorageBufferArrayDynamicIndexing", "shaderStorageImageArrayDynamicIndexing", "shaderClipDistance", "shaderCullDistance",
	"shaderFloat64", "shaderInt64", "shaderInt16", "shaderResourceResidency", "shaderResourceMinLod", "sparseBinding",
	"sparseResidencyBuffer", "sparseResidencyImage2D", "sparseResidencyImage3D", "sparseResidency2Samples", "sparseResidency4Samples",
	"sparseResidency8Samples", "sparseResidency16Samples", "sparseResidencyAliased", "variableMultisampleRate", "inheritedQueries"
};
static_assert(sizeof(VkPhysicalDeviceFeatures) == 55 * sizeof(VkBool32), "VkPhysicalDeviceFeatures layout changed, update kVkFeatureNames");

void VulkanRenderer::LogVulkanStartupDiagnostics(const VkPhysicalDeviceFeatures& supported, const VkPhysicalDeviceFeatures& requested, const VkPhysicalDeviceFeatures& enabled, const std::vector<const char*>& enabledDeviceExtensions)
{
	cemuLog_log(LogType::Force, "---- Vulkan startup diagnostic ----");

	VkPhysicalDeviceDriverProperties driverProps{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES };
	VkPhysicalDeviceProperties2 props2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
	if (m_featureControl.deviceExtensions.driver_properties)
		props2.pNext = &driverProps;
	vkGetPhysicalDeviceProperties2(m_physicalDevice, &props2);
	const auto& p = props2.properties;
	cemuLog_log(LogType::Force, "Vulkan diagnostic: device '{}' type {} vendor 0x{:x} device 0x{:x} apiVersion {}.{}.{}", p.deviceName, (sint32)p.deviceType, p.vendorID, p.deviceID,
		VK_API_VERSION_MAJOR(p.apiVersion), VK_API_VERSION_MINOR(p.apiVersion), p.apiVersion & 0xFFF);
	{
		const char* chosenPath = getenv("ESPRESSU_MOLTENVK_PATH");
		const uint32_t mvkMajor = p.driverVersion / 10000, mvkMinor = (p.driverVersion / 100) % 100;
		if (chosenPath && p.vendorID == 0x106B)
		{
			const bool wants128 = strstr(chosenPath, "MoltenVK128") != nullptr;
			const bool reports128 = (mvkMajor == 1 && mvkMinor == 2);
			cemuLog_log(LogType::Force, "Vulkan diagnostic: MoltenVK build check: bridge selected {} ({}), the loaded library reports {}.{}.{} - {}", wants128 ? "1.2.8" : "1.4.3", chosenPath, mvkMajor, mvkMinor, p.driverVersion % 100,
				(wants128 == reports128) ? "match" : "MISMATCH (driverVersion decoding or the selector is wrong)");
		}
		else
			cemuLog_log(LogType::Force, "Vulkan diagnostic: MoltenVK build check: no ESPRESSU_MOLTENVK_PATH (system search was used)");
	}
	cemuLog_log(LogType::Force, "Vulkan diagnostic: geometryShader {} - {}", m_featureControl.geometryShader ? "enabled" : "NOT enabled",
		m_featureControl.geometryShader ? "game geometry shaders and RECT emulation are possible" : "pipelines that need a geometry shader (game GS, RECTS primitives without VK_NV_fill_rectangle) are skipped");
	cemuLog_log(LogType::Force, "Vulkan diagnostic: driverVersion raw {} (as MoltenVK version: {}.{}.{}) driverName '{}' driverInfo '{}'", p.driverVersion,
		p.driverVersion / 10000, (p.driverVersion / 100) % 100, p.driverVersion % 100,
		m_featureControl.deviceExtensions.driver_properties ? driverProps.driverName : "n/a",
		m_featureControl.deviceExtensions.driver_properties ? driverProps.driverInfo : "n/a");
#if BOOST_OS_MACOS || BOOST_OS_IOS
	cemuLog_log(LogType::Force, "Vulkan diagnostic: Metal GPU: {}", GetAppleGpuFamilyDescription());
	const char* mvkSwizzle = getenv("MVK_CONFIG_FULL_IMAGE_VIEW_SWIZZLE");
	const char* mvkArgBuf = getenv("MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS");
	cemuLog_log(LogType::Force, "Vulkan diagnostic: MVK_CONFIG_FULL_IMAGE_VIEW_SWIZZLE={} MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS={} ESPRESSU_MOLTENVK_PATH={}", mvkSwizzle ? mvkSwizzle : "(unset)", mvkArgBuf ? mvkArgBuf : "(unset)",
		getenv("ESPRESSU_MOLTENVK_PATH") ? getenv("ESPRESSU_MOLTENVK_PATH") : "(unset)");
#endif

	std::vector<VkExtensionProperties> available;
	uint32_t extCount = 0;
	if (vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extCount, nullptr) == VK_SUCCESS && extCount > 0)
	{
		available.resize(extCount);
		vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extCount, available.data());
	}
	auto isAvailable = [&](const char* name) { return std::any_of(available.begin(), available.end(), [&](const VkExtensionProperties& e) { return strcmp(e.extensionName, name) == 0; }); };
	auto isEnabled = [&](const char* name) { return std::any_of(enabledDeviceExtensions.begin(), enabledDeviceExtensions.end(), [&](const char* e) { return strcmp(e, name) == 0; }); };
	{
		std::string list;
		for (const auto& e : available)
			list += (list.empty() ? "" : ", ") + std::string(e.extensionName);
		cemuLog_log(LogType::Force, "Vulkan diagnostic: {} device extension(s) available: {}", available.size(), list);
	}
	{
		std::vector<const char*> interesting = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, kPortabilitySubsetExtName, VK_KHR_SAMPLER_MIRROR_CLAMP_TO_EDGE_EXTENSION_NAME,
			VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME, VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
			VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME, VK_KHR_DRAW_INDIRECT_COUNT_EXTENSION_NAME, VK_EXT_DEPTH_RANGE_UNRESTRICTED_EXTENSION_NAME };
		for (const char* opt : kOptionalDeviceExtensions)
			if (std::find_if(interesting.begin(), interesting.end(), [&](const char* e) { return strcmp(e, opt) == 0; }) == interesting.end())
				interesting.push_back(opt);
		std::string line;
		for (const char* name : interesting)
			line += fmt::format("{}{}={}", line.empty() ? "" : ", ", name, isEnabled(name) ? "ENABLED" : (isAvailable(name) ? "available-not-enabled" : "absent"));
		cemuLog_log(LogType::Force, "Vulkan diagnostic: extension status: {}", line);
		std::string enabledList;
		for (const char* e : enabledDeviceExtensions)
			enabledList += (enabledList.empty() ? "" : ", ") + std::string(e);
		cemuLog_log(LogType::Force, "Vulkan diagnostic: device extensions requested from vkCreateDevice: {}", enabledList);
	}

	{
		const VkBool32* sup = reinterpret_cast<const VkBool32*>(&supported);
		const VkBool32* req = reinterpret_cast<const VkBool32*>(&requested);
		const VkBool32* en = reinterpret_cast<const VkBool32*>(&enabled);
		std::string supportedList, requestedList, droppedList, absentList;
		for (size_t i = 0; i < kVkFeatureNames.size(); i++)
		{
			const std::string name = kVkFeatureNames[i];
			if (sup[i])
				supportedList += (supportedList.empty() ? "" : ", ") + name;
			else
				absentList += (absentList.empty() ? "" : ", ") + name;
			if (req[i])
				requestedList += (requestedList.empty() ? "" : ", ") + name + (en[i] ? "" : "(dropped)");
			if (req[i] && !en[i])
				droppedList += (droppedList.empty() ? "" : ", ") + name;
		}
		cemuLog_log(LogType::Force, "Vulkan diagnostic: core features supported: {}", supportedList);
		cemuLog_log(LogType::Force, "Vulkan diagnostic: core features NOT supported: {}", absentList);
		cemuLog_log(LogType::Force, "Vulkan diagnostic: core features requested by Cemu: {}", requestedList);
		cemuLog_log(LogType::Force, "Vulkan diagnostic: requested but unsupported (dropped, running without): {}", droppedList.empty() ? "none" : droppedList);
	}
	cemuLog_log(LogType::Force, "Vulkan diagnostic: texture formats: BC={} ASTC_LDR={} mirrorClamp(ext)={} mirrorClamp(core1.2)={} presentWait={} customBorderColorWithoutFormat={} pipelineRobustness={}",
		m_supportedFormatInfo.fmt_bc, m_supportedFormatInfo.fmt_astc, m_featureControl.deviceExtensions.sampler_mirror_clamp_to_edge, m_featureControl.samplerMirrorClampToEdgeCore,
		m_featureControl.deviceExtensions.present_wait, m_featureControl.deviceExtensions.custom_border_color_without_format, m_featureControl.deviceExtensions.pipeline_robustness);
	cemuLog_log(LogType::Force, "---- end Vulkan startup diagnostic ----");
}

VulkanRenderer::VulkanRenderer()
{
	LatteWait::ResetAll();
	glslang::InitializeProcess();
    InitializeGlobalVulkan();

	cemuLog_log(LogType::Force, "------- Init Vulkan graphics backend -------");

	const bool useValidationLayer = cemuLog_isLoggingEnabled(LogType::VulkanValidation);
	if (useValidationLayer)
		cemuLog_log(LogType::Force, "Validation layer is enabled");

	VkResult err;

	m_layerNames.clear();
	if (useValidationLayer)
		m_layerNames.emplace_back("VK_LAYER_KHRONOS_validation");

	std::vector<const char*> enabledInstanceExtensions = CheckInstanceExtensionSupport(m_featureControl);

	uint32 apiVersion = VK_API_VERSION_1_1;
	if (vkEnumerateInstanceVersion(&apiVersion) != VK_SUCCESS)
	{
		if (VK_API_VERSION_MAJOR(apiVersion) < 1 || VK_API_VERSION_MINOR(apiVersion) < 2)
			apiVersion = VK_API_VERSION_1_1;
	}

	cemuLog_log(LogType::Force, fmt::format("Vulkan instance version: {}.{}", VK_API_VERSION_MAJOR(apiVersion), VK_API_VERSION_MINOR(apiVersion)));

	VkApplicationInfo app_info{};
	app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app_info.pApplicationName = EMULATOR_NAME;
	app_info.applicationVersion = VK_MAKE_VERSION(EMULATOR_VERSION_MAJOR, EMULATOR_VERSION_MINOR, EMULATOR_VERSION_PATCH);
	app_info.pEngineName = EMULATOR_NAME;
	app_info.engineVersion = app_info.applicationVersion;
	app_info.apiVersion = apiVersion;

	VkInstanceCreateInfo create_info{};
	create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	create_info.pApplicationInfo = &app_info;
	create_info.ppEnabledExtensionNames = enabledInstanceExtensions.data();
	create_info.enabledExtensionCount = enabledInstanceExtensions.size();
	create_info.ppEnabledLayerNames = m_layerNames.data();
	create_info.enabledLayerCount = m_layerNames.size();
	if (m_featureControl.instanceExtensions.portability_enumeration)
		create_info.flags |= kInstanceEnumeratePortabilityBit;

	err = vkCreateInstance(&create_info, nullptr, &m_instance);

	if (err == VK_ERROR_LAYER_NOT_PRESENT) {
		cemuLog_log(LogType::Force, "Failed to enable vulkan validation (VK_LAYER_KHRONOS_validation)");
		create_info.enabledLayerCount = 0;
		err = vkCreateInstance(&create_info, nullptr, &m_instance);
	}

	if (err != VK_SUCCESS)
		throw std::runtime_error(fmt::format("Unable to create a Vulkan instance: {}", err));

	if (!InitializeInstanceVulkan(m_instance))
		throw std::runtime_error("Unable to load instanced Vulkan functions");

#if BOOST_OS_LINUX
	uint64 currentTitleId = CafeSystem::GetForegroundTitleId();
	if (currentTitleId == 0x00050000101c9500 || currentTitleId == 0x00050000101c9400 || currentTitleId == 0x00050000101c9300)
	{
		LinuxBreathOfTheWildWorkaround(m_instance, &create_info);
	}
#endif

	uint32_t device_count = 0;
	vkEnumeratePhysicalDevices(m_instance, &device_count, nullptr);
	if (device_count == 0)
		throw std::runtime_error("Failed to find a GPU with Vulkan support.");

	auto surface = CreateFramebufferSurface(m_instance, WindowSystem::GetWindowInfo().window_main);

	auto& config = GetConfig();
	decltype(config.vk_graphic_device_uuid) zero{};
	const bool has_device_set = config.vk_graphic_device_uuid != zero;

	VkPhysicalDevice fallbackDevice = VK_NULL_HANDLE;

	std::vector<VkPhysicalDevice> devices(device_count);
	vkEnumeratePhysicalDevices(m_instance, &device_count, devices.data());
	for (const auto& device : devices)
	{
		if (IsDeviceSuitable(surface, device))
		{
			if (fallbackDevice == VK_NULL_HANDLE)
				fallbackDevice = device;

			if (has_device_set)
			{
				VkPhysicalDeviceIDProperties physDeviceIDProps = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES };
				VkPhysicalDeviceProperties2 physDeviceProps = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
				physDeviceProps.pNext = &physDeviceIDProps;
				vkGetPhysicalDeviceProperties2(device, &physDeviceProps);

				if (memcmp(config.vk_graphic_device_uuid.data(), physDeviceIDProps.deviceUUID, VK_UUID_SIZE) != 0)
					continue;
			}

			m_physicalDevice = device;
			break;
		}
	}

	if (m_physicalDevice == VK_NULL_HANDLE && fallbackDevice != VK_NULL_HANDLE)
	{
		cemuLog_log(LogType::Force, "The selected GPU could not be found or is not suitable. Falling back to first available device instead");
		m_physicalDevice = fallbackDevice;
		config.vk_graphic_device_uuid = {};
	}
	else if (m_physicalDevice == VK_NULL_HANDLE)
	{
		cemuLog_log(LogType::Force, "No physical GPU could be found with the required extensions and swap chain support.");
		throw std::runtime_error("No physical GPU could be found with the required extensions and swap chain support.");
	}

	CheckDeviceExtensionSupport(m_physicalDevice, m_featureControl);

	DetermineVendor();
	GetDeviceFeatures();

	memoryManager.reset(new VKRMemoryManager(this));

	try
	{
		VkPhysicalDeviceIDProperties physDeviceIDProps = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES };
		VkPhysicalDeviceProperties2 physDeviceProps = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
		physDeviceProps.pNext = &physDeviceIDProps;
		vkGetPhysicalDeviceProperties2(m_physicalDevice, &physDeviceProps);

		#if BOOST_OS_WINDOWS
		m_dxgi_wrapper = std::make_unique<DXGIWrapper>(physDeviceIDProps.deviceLUID);
		#endif
	}
	catch (const std::exception& ex)
	{
		cemuLog_log(LogType::Force, "can't create dxgi wrapper: {}", ex.what());
	}

	m_indices = FindQueueFamilies(surface, m_physicalDevice);
	std::set<int> uniqueQueueFamilies = { m_indices.graphicsFamily, m_indices.presentFamily };
	std::vector<VkDeviceQueueCreateInfo> queueCreateInfos = CreateQueueCreateInfos(uniqueQueueFamilies);
	VkPhysicalDeviceFeatures deviceFeatures = {};
	VkPhysicalDeviceFeatures2 deviceFeatures2 = {};
	deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	vkGetPhysicalDeviceFeatures2(m_physicalDevice, &deviceFeatures2);
	m_supportedFormatInfo.fmt_bc = deviceFeatures2.features.textureCompressionBC;
	m_supportedFormatInfo.fmt_astc = deviceFeatures2.features.textureCompressionASTC_LDR;

	deviceFeatures.independentBlend = VK_TRUE;
	deviceFeatures.samplerAnisotropy = VK_TRUE;
	deviceFeatures.imageCubeArray = VK_TRUE;
	deviceFeatures.logicOp = deviceFeatures2.features.logicOp;
	if (!deviceFeatures.logicOp) {
		cemuLog_log(LogType::Force, "LogicOp not supported by the driver, some rendering issues might occur");
#if BOOST_OS_MACOS || BOOST_OS_IOS
		cemuLog_log(LogType::Force, "Install the privateapi variant of MoltenVK to get logicOp support on macOS");
#endif
	}
	deviceFeatures.geometryShader = VK_TRUE;
	deviceFeatures.occlusionQueryPrecise = VK_TRUE;
	deviceFeatures.depthClamp = VK_TRUE;
	deviceFeatures.depthBiasClamp = VK_TRUE;
	deviceFeatures.textureCompressionBC = m_supportedFormatInfo.fmt_bc;
	deviceFeatures.textureCompressionASTC_LDR = m_supportedFormatInfo.fmt_astc;

#if BOOST_OS_MACOS || BOOST_OS_IOS
	deviceFeatures.robustBufferAccess = VK_TRUE;
	if (!m_featureControl.deviceExtensions.pipeline_robustness)
		cemuLog_log(LogType::Force, "VK_EXT_pipeline_robustness not supported. Falling back to robustBufferAccess");
#else
	if (m_featureControl.deviceExtensions.pipeline_robustness)
	{
		deviceFeatures.robustBufferAccess = VK_FALSE;
	}
	else
	{
		cemuLog_log(LogType::Force, "VK_EXT_pipeline_robustness not supported. Falling back to robustBufferAccess");
		deviceFeatures.robustBufferAccess = VK_TRUE;
	}
#endif

	deviceFeatures.vertexPipelineStoresAndAtomics = true;

	const VkPhysicalDeviceFeatures requestedFeatures = deviceFeatures;
	{
		const VkBool32* supportedBits = reinterpret_cast<const VkBool32*>(&deviceFeatures2.features);
		VkBool32* enabledBits = reinterpret_cast<VkBool32*>(&deviceFeatures);
		for (size_t i = 0; i < kVkFeatureNames.size(); i++)
		{
			if (enabledBits[i] && !supportedBits[i])
			{
				enabledBits[i] = VK_FALSE;
				cemuLog_log(LogType::Force, "Vulkan: requested feature '{}' is not supported by this device, continuing without it", kVkFeatureNames[i]);
			}
		}
	}

	void* deviceExtensionFeatures = nullptr;

	VkPhysicalDevicePipelineCreationCacheControlFeaturesEXT cacheControlFeature{};
	if (m_featureControl.deviceExtensions.pipeline_creation_cache_control)
	{
		cacheControlFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_CREATION_CACHE_CONTROL_FEATURES_EXT;
		cacheControlFeature.pNext = deviceExtensionFeatures;
		deviceExtensionFeatures = &cacheControlFeature;
		cacheControlFeature.pipelineCreationCacheControl = VK_TRUE;
	}
	VkPhysicalDeviceCustomBorderColorFeaturesEXT customBorderColorFeature{};
	if (m_featureControl.deviceExtensions.custom_border_color_without_format)
	{
		customBorderColorFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT;
		customBorderColorFeature.pNext = deviceExtensionFeatures;
		deviceExtensionFeatures = &customBorderColorFeature;
		customBorderColorFeature.customBorderColors = VK_TRUE;
		customBorderColorFeature.customBorderColorWithoutFormat = VK_TRUE;
	}
	VkPhysicalDevicePresentIdFeaturesKHR presentIdFeature{};
	if(m_featureControl.deviceExtensions.present_wait)
	{
		presentIdFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR;
		presentIdFeature.pNext = deviceExtensionFeatures;
		deviceExtensionFeatures = &presentIdFeature;
		presentIdFeature.presentId = VK_TRUE;
	}
	VkPhysicalDevicePresentWaitFeaturesKHR presentWaitFeature{};
	if(m_featureControl.deviceExtensions.present_wait)
	{
		presentWaitFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR;
		presentWaitFeature.pNext = deviceExtensionFeatures;
		deviceExtensionFeatures = &presentWaitFeature;
		presentWaitFeature.presentWait = VK_TRUE;
	}
	VkPhysicalDevicePipelineRobustnessFeaturesEXT pipelineRobustnessFeature{};
	if (m_featureControl.deviceExtensions.pipeline_robustness)
	{
		pipelineRobustnessFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_ROBUSTNESS_FEATURES_EXT;
		pipelineRobustnessFeature.pNext = deviceExtensionFeatures;
		deviceExtensionFeatures = &pipelineRobustnessFeature;
		pipelineRobustnessFeature.pipelineRobustness = VK_TRUE;
	}

	VkPhysicalDeviceVulkan12Features vulkan12Features{};
	if (m_featureControl.samplerMirrorClampToEdgeCore)
	{
		vulkan12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
		vulkan12Features.pNext = deviceExtensionFeatures;
		deviceExtensionFeatures = &vulkan12Features;
		vulkan12Features.samplerMirrorClampToEdge = VK_TRUE;
	}

	std::vector<const char*> used_extensions;
	VkDeviceCreateInfo createInfo = CreateDeviceCreateInfo(queueCreateInfos, deviceFeatures, deviceExtensionFeatures, used_extensions);

	LogVulkanStartupDiagnostics(deviceFeatures2.features, requestedFeatures, deviceFeatures, used_extensions);

	m_featureControl.geometryShader = deviceFeatures.geometryShader == VK_TRUE;
	VkResult result = vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_logicalDevice);
	if (result == VK_ERROR_FEATURE_NOT_PRESENT || result == VK_ERROR_EXTENSION_NOT_PRESENT)
	{
		m_featureControl.geometryShader = false;
		cemuLog_log(LogType::Force, "Vulkan: vkCreateDevice returned {} ({}) even though the request was filtered against the device's reported features and extensions. Retrying with every optional feature and extension off", (sint32)result, result == VK_ERROR_FEATURE_NOT_PRESENT ? "VK_ERROR_FEATURE_NOT_PRESENT" : "VK_ERROR_EXTENSION_NOT_PRESENT");
		const bool portabilitySubset = m_featureControl.deviceExtensions.portability_subset;
		m_featureControl.deviceExtensions = {};
		m_featureControl.deviceExtensions.portability_subset = portabilitySubset;
		m_featureControl.samplerMirrorClampToEdgeCore = false;
		m_supportedFormatInfo.fmt_bc = false;
		m_supportedFormatInfo.fmt_astc = false;
		VkPhysicalDeviceFeatures minimalFeatures = {};
		std::vector<const char*> minimalExtensions;
		createInfo = CreateDeviceCreateInfo(queueCreateInfos, minimalFeatures, nullptr, minimalExtensions);
		result = vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_logicalDevice);
		if (result == VK_SUCCESS)
			cemuLog_log(LogType::Force, "Vulkan: minimal logical device created, running without optional features and extensions");
	}
	if (result != VK_SUCCESS)
	{
		cemuLog_log(LogType::Force, "Vulkan: Unable to create a logical device. Error {}", (sint32)result);
		throw std::runtime_error(fmt::format("Unable to create a logical device: {}", result));
	}

	InitializeDeviceVulkan(m_logicalDevice);

	vkGetDeviceQueue(m_logicalDevice, m_indices.graphicsFamily, 0, &m_graphicsQueue);
	vkGetDeviceQueue(m_logicalDevice, m_indices.graphicsFamily, 0, &m_presentQueue);

	vkDestroySurfaceKHR(m_instance, surface, nullptr);

	if (useValidationLayer && m_featureControl.instanceExtensions.debug_utils)
	{
		PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT"));

		VkDebugUtilsMessengerCreateInfoEXT debugCallback{};
		debugCallback.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		debugCallback.pNext = nullptr;
		debugCallback.flags = 0;
		debugCallback.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT;
		debugCallback.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		debugCallback.pfnUserCallback = &DebugUtilsCallback;

		vkCreateDebugUtilsMessengerEXT(m_instance, &debugCallback, nullptr, &m_debugCallback);

		cemuLog_log(LogType::Force, "Debug: Vulkan validation layer enabled, vkCreateDebugUtilsMessengerEXT will be used to log validation errors");
	}
#if BOOST_OS_MACOS || BOOST_OS_IOS
	else if (m_featureControl.instanceExtensions.debug_utils)
	{
		auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT"));
		if (createMessenger)
		{
			VkDebugUtilsMessengerCreateInfoEXT debugCallback{};
			debugCallback.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
			debugCallback.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
			debugCallback.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
			debugCallback.pfnUserCallback = +[](VkDebugUtilsMessageSeverityFlagBitsEXT, VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void*) -> VkBool32
			{
				static std::atomic<int> s_count{0};
				if (s_count.fetch_add(1) < 100 && data && data->pMessage)
					cemuLog_log(LogType::Force, "MoltenVK error: {}", data->pMessage);
				return VK_FALSE;
			};
			if (createMessenger(m_instance, &debugCallback, nullptr, &m_debugCallback) == VK_SUCCESS)
				cemuLog_log(LogType::Force, "Vulkan: MoltenVK error messages are logged (first 100)");
			else
				m_debugCallback = nullptr;
		}
	}
#endif

	if (this->IsTracingToolEnabled())
		cemuLog_log(LogType::Force, "Debug: Tracing tool detected, will recompile all shaders with debug info enabled. This disables the SPIR-V cache.");
	if (this->IsDebugMarkersEnabled())
		cemuLog_log(LogType::Force, "Debug: Detected tool capable of using debug markers, will use vkDebugMarkerSetObjectNameEXT to identify Vulkan objects");

	m_state.currentViewport.width = 4;
	m_state.currentViewport.height = 4;
	m_state.currentScissorRect.extent.width = 4;
	m_state.currentScissorRect.extent.height = 4;

	QueryMemoryInfo();
	QueryAvailableFormats();
	CreateCommandPool();
	CreateCommandBuffers();
	CreateDescriptorPool();
	swapchain_createDescriptorSetLayout();

	void* bufferPtr;
	m_uniformVarBufferMemoryIsCoherent = false;
	if (memoryManager->CreateBuffer(UNIFORMVAR_RINGBUFFER_SIZE, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, m_uniformVarBuffer, m_uniformVarBufferMemory))
		m_uniformVarBufferMemoryIsCoherent = true;
	else if (memoryManager->CreateBuffer(UNIFORMVAR_RINGBUFFER_SIZE, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_uniformVarBuffer, m_uniformVarBufferMemory))
		m_uniformVarBufferMemoryIsCoherent = true;
	else if (memoryManager->CreateBuffer(UNIFORMVAR_RINGBUFFER_SIZE, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_uniformVarBuffer, m_uniformVarBufferMemory))
		m_uniformVarBufferMemoryIsCoherent = true;
	else if (memoryManager->CreateBuffer(UNIFORMVAR_RINGBUFFER_SIZE, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_uniformVarBuffer, m_uniformVarBufferMemory))
		m_uniformVarBufferMemoryIsCoherent = true;
	else
	{
		memoryManager->CreateBuffer(UNIFORMVAR_RINGBUFFER_SIZE, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, m_uniformVarBuffer, m_uniformVarBufferMemory);
	}

	if (!m_uniformVarBufferMemoryIsCoherent)
		cemuLog_log(LogType::Force, "[Vulkan-Info] Using non-coherent memory for uniform data");
	bufferPtr = nullptr;
	vkMapMemory(m_logicalDevice, m_uniformVarBufferMemory, 0, VK_WHOLE_SIZE, 0, &bufferPtr);
	m_uniformVarBufferPtr = (uint8*)bufferPtr;

	if (!memoryManager->CreateBuffer(TEXTURE_READBACK_SIZE, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, m_textureReadbackBuffer, m_textureReadbackBufferMemory))
	{
		memoryManager->CreateBuffer(TEXTURE_READBACK_SIZE, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, m_textureReadbackBuffer, m_textureReadbackBufferMemory);
	}
	bufferPtr = nullptr;
	vkMapMemory(m_logicalDevice, m_textureReadbackBufferMemory, 0, VK_WHOLE_SIZE, 0, &bufferPtr);
	m_textureReadbackBufferPtr = (uint8*)bufferPtr;

	VkBufferUsageFlags xfbRingBufferUsage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	memoryManager->CreateBuffer(LatteStreamout_GetRingBufferSize(), xfbRingBufferUsage, 0, m_xfbRingBuffer, m_xfbRingBufferMemory);

	if (!memoryManager->CreateBuffer(OCCLUSION_QUERY_POOL_SIZE * sizeof(uint64), VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, m_occlusionQueries.bufferQueryResults, m_occlusionQueries.memoryQueryResults))
	{
		memoryManager->CreateBuffer(OCCLUSION_QUERY_POOL_SIZE * sizeof(uint64), VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, m_occlusionQueries.bufferQueryResults, m_occlusionQueries.memoryQueryResults);
	}
	bufferPtr = nullptr;
	vkMapMemory(m_logicalDevice, m_occlusionQueries.memoryQueryResults, 0, VK_WHOLE_SIZE, 0, &bufferPtr);
	m_occlusionQueries.ptrQueryResults = (uint64*)bufferPtr;

	for (sint32 i = 0; i < OCCLUSION_QUERY_POOL_SIZE; i++)
		m_occlusionQueries.list_availableQueryIndices.emplace_back(i);

	RendererShaderVk::Init();
	PipelineCompiler::CompileThreadPool_Start();
}

VulkanRenderer::~VulkanRenderer()
{
	if (!m_initializeCalled.load())
	{
		m_padSwapchainInfo = nullptr;
		m_mainSwapchainInfo = nullptr;
		memoryManager.reset();
		if (m_logicalDevice)
		{
			vkDeviceWaitIdle(m_logicalDevice);
			for (auto& it : m_cmd_buffer_fences)
				if (it != VK_NULL_HANDLE)
					vkDestroyFence(m_logicalDevice, it, nullptr);
			for (auto& sem : m_commandBufferSemaphores)
				if (sem != VK_NULL_HANDLE)
					vkDestroySemaphore(m_logicalDevice, sem, nullptr);
			if (m_commandPool != VK_NULL_HANDLE)
				vkDestroyCommandPool(m_logicalDevice, m_commandPool, nullptr);
			vkDestroyDevice(m_logicalDevice, nullptr);
		}
		if (m_instance)
		{
			if (m_debugCallback)
			{
				auto destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT"));
				if (destroyMessenger)
					destroyMessenger(m_instance, m_debugCallback, nullptr);
			}
			vkDestroyInstance(m_instance, nullptr);
		}
		return;
	}
	SubmitCommandBuffer();
	WaitDeviceIdle();
	WaitCommandBufferFinished(GetCurrentCommandBufferId());
	m_destructionRequested = true;
	m_pipeline_cache_semaphore.notify();
	m_pipeline_cache_save_thread.join();

	vkDestroyPipelineCache(m_logicalDevice, m_pipeline_cache, nullptr);

	if(!m_backbufferBlitDescriptorSetCache.empty())
	{
		std::vector<VkDescriptorSet> freeVector;
		freeVector.reserve(m_backbufferBlitDescriptorSetCache.size());
		std::transform(m_backbufferBlitDescriptorSetCache.begin(), m_backbufferBlitDescriptorSetCache.end(), std::back_inserter(freeVector), [](auto& i) {
		  return i.second;
		});
		vkFreeDescriptorSets(m_logicalDevice, m_descriptorPool, freeVector.size(), freeVector.data());
	}

	vkDestroyDescriptorPool(m_logicalDevice, m_descriptorPool, nullptr);

	for(auto& i : m_backbufferBlitPipelineCache)
	{
		vkDestroyPipeline(m_logicalDevice, i.second, nullptr);
	}
	m_backbufferBlitPipelineCache = {};

	if(m_occlusionQueries.queryPool != VK_NULL_HANDLE)
		vkDestroyQueryPool(m_logicalDevice, m_occlusionQueries.queryPool, nullptr);

	vkDestroyDescriptorSetLayout(m_logicalDevice, m_swapchainDescriptorSetLayout, nullptr);

	if (m_imguiBackendInitialized)
		ImGui_ImplVulkan_Shutdown();
	m_imguiBackendInitialized = false;

	DeleteNullObjects();

	memoryManager->DeleteBuffer(m_uniformVarBuffer, m_uniformVarBufferMemory);
	memoryManager->DeleteBuffer(m_textureReadbackBuffer, m_textureReadbackBufferMemory);
	memoryManager->DeleteBuffer(m_xfbRingBuffer, m_xfbRingBufferMemory);
	memoryManager->DeleteBuffer(m_occlusionQueries.bufferQueryResults, m_occlusionQueries.memoryQueryResults);
	memoryManager->DeleteBuffer(m_bufferCache, m_bufferCacheMemory);

	m_padSwapchainInfo = nullptr;
	m_mainSwapchainInfo = nullptr;

	surfaceCopy_cleanup();

	delete defaultShaders.copySurface_vs;
	defaultShaders.copySurface_vs = nullptr;
	delete defaultShaders.copySurface_psColor2Depth;
	defaultShaders.copySurface_psColor2Depth = nullptr;
	delete defaultShaders.copySurface_psDepth2Color;
	defaultShaders.copySurface_psDepth2Color = nullptr;

	for (auto& it : m_cmd_buffer_fences)
	{
		vkDestroyFence(m_logicalDevice, it, nullptr);
		it = VK_NULL_HANDLE;
	}

	for(auto& sem : m_commandBufferSemaphores)
	{
		vkDestroySemaphore(m_logicalDevice, sem, nullptr);
		sem = VK_NULL_HANDLE;
	}

	if (m_pipelineLayout != VK_NULL_HANDLE)
		vkDestroyPipelineLayout(m_logicalDevice, m_pipelineLayout, nullptr);

	if (m_commandPool != VK_NULL_HANDLE)
		vkDestroyCommandPool(m_logicalDevice, m_commandPool, nullptr);

	VKRObjectSampler::DestroyCache();

	if (m_debugCallback)
	{
		PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT"));
		vkDestroyDebugUtilsMessengerEXT(m_instance, m_debugCallback, nullptr);
	}

	while(!m_destructionQueue.empty())
		ProcessDestructionQueue();

	memoryManager.reset();

	if (m_instance != VK_NULL_HANDLE)
	{
		if (m_logicalDevice != VK_NULL_HANDLE)
		{
			vkDestroyDevice(m_logicalDevice, nullptr);
		}

		vkDestroyInstance(m_instance, nullptr);
	}

}

VulkanRenderer* VulkanRenderer::GetInstance()
{
#ifdef CEMU_DEBUG_ASSERT
	cemu_assert_debug(g_renderer && dynamic_cast<VulkanRenderer*>(g_renderer.get()));
#endif
	return (VulkanRenderer*)g_renderer.get();
}

void VulkanRenderer::InitializeSurface(const Vector2i& size, bool mainWindow)
{
	if (mainWindow)
	{
		m_mainSwapchainInfo = std::make_unique<SwapchainInfoVk>(mainWindow, size);
		m_mainSwapchainInfo->Create();
	}
	else if (!m_initializeCalled.load())
	{
		m_padSwapchainInfo = std::make_unique<SwapchainInfoVk>(mainWindow, size);
		m_padSwapchainInfo->Create();
	}
	else
	{
		m_padSurfaceWidth.store(size.x, std::memory_order_relaxed);
		m_padSurfaceHeight.store(size.y, std::memory_order_relaxed);
		m_padSurfaceChanged.store(true, std::memory_order_release);
	}
}

const std::unique_ptr<SwapchainInfoVk>& VulkanRenderer::GetChainInfoPtr(bool mainWindow) const
{
	return mainWindow ? m_mainSwapchainInfo : m_padSwapchainInfo;
}

SwapchainInfoVk& VulkanRenderer::GetChainInfo(bool mainWindow) const
{
	return *GetChainInfoPtr(mainWindow);
}

void VulkanRenderer::StopUsingPadAndWait()
{
	m_destroyPadSwapchainNextAcquire.test_and_set();
	m_destroyPadSwapchainNextAcquire.wait(true);
}

bool VulkanRenderer::IsPadWindowActive()
{
	return IsSwapchainInfoValid(false);
}

bool VulkanRenderer::GetVRAMInfo(int& usageInMB, int& totalInMB) const
{
	usageInMB = totalInMB = -1;
	if (!m_featureControl.deviceExtensions.memory_budget || !vkGetPhysicalDeviceMemoryProperties2)
		return false;
	VkPhysicalDeviceMemoryBudgetPropertiesEXT budget{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT };
	VkPhysicalDeviceMemoryProperties2 props2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2 };
	props2.pNext = &budget;
	vkGetPhysicalDeviceMemoryProperties2(m_physicalDevice, &props2);
	uint64 usage = 0, total = 0;
	for (uint32 i = 0; i < props2.memoryProperties.memoryHeapCount; i++)
	{
		if (!(props2.memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT))
			continue;
		usage += budget.heapUsage[i];
		total += budget.heapBudget[i];
	}
	if (total == 0)
		return false;
	usageInMB = (int)(usage / (1024 * 1024));
	totalInMB = (int)(total / (1024 * 1024));
	return true;
}

void VulkanRenderer::HandleScreenshotRequest(LatteTextureView* texView, bool padView)
{
	if (!m_screenshot_requested && m_screenshot_state == ScreenshotState::None)
		return;

	if (IsSwapchainInfoValid(false))
	{
		if (m_screenshot_state == ScreenshotState::Main && padView)
			return;

		if (m_screenshot_state == ScreenshotState::Pad && !padView)
			return;

		if (m_screenshot_state == ScreenshotState::None)
			m_screenshot_state = padView ? ScreenshotState::Main : ScreenshotState::Pad;
		else
			m_screenshot_state = ScreenshotState::None;
	}
	else
		m_screenshot_state = ScreenshotState::None;

	auto texViewVk = (LatteTextureViewVk*)texView;
	auto baseImageTex = texViewVk->GetBaseImage();

	auto textureVk = baseImageTex->GetImageObj();
	textureVk->flagForCurrentCommandBuffer();

	auto dumpImage = textureVk->m_image;
	auto baseImage = dumpImage;

	int width, height;
	baseImageTex->GetEffectiveSize(width, height, 0);

	VkImage image = nullptr;
	VkDeviceMemory imageMemory = nullptr;

	if (texViewVk->firstMip != 0)
	{
		cemuLog_log(LogType::Force, "Failed to capture screenshot: capturing non-zero mip is not supported");
		return;
	}

	auto format = baseImageTex->GetFormat();
	if (format != VK_FORMAT_R8G8B8A8_UNORM && format != VK_FORMAT_R8G8B8A8_SRGB && format != VK_FORMAT_R8G8B8_UNORM && format != VK_FORMAT_R8G8B8_SNORM)
	{
		VkFormatProperties formatProps;
		vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &formatProps);
		bool supportsBlit = (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) != 0;

		const bool dstUsesSRGB = (!padView && LatteGPUState.tvBufferUsesSRGB) || (padView && LatteGPUState.drcBufferUsesSRGB);
		const auto blitFormat = dstUsesSRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;

		vkGetPhysicalDeviceFormatProperties(m_physicalDevice, blitFormat, &formatProps);
		supportsBlit &= (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) != 0;

		if (!supportsBlit)
		{
			cemuLog_log(LogType::Force, "Screenshot failed: Framebuffer is not in RGB8 format and blitting is unsupported");
			return;
		}

		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.format = blitFormat;
		imageInfo.extent = {(uint32)width, (uint32)height, 1};
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.arrayLayers = 1;
		imageInfo.mipLevels = 1;
		imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;

		if (vkCreateImage(m_logicalDevice, &imageInfo, nullptr, &image) != VK_SUCCESS)
			return;

		VkMemoryRequirements memRequirements;
		vkGetImageMemoryRequirements(m_logicalDevice, image, &memRequirements);

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memRequirements.size;
		uint32 memIndex;
		bool foundMemory = memoryManager->FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memIndex);
		if (!foundMemory)
		{
			vkDestroyImage(m_logicalDevice, image, nullptr);
			cemuLog_log(LogType::Force, "Screenshot request failed due to incompatible vulkan memory types.");
			return;
		}
		allocInfo.memoryTypeIndex = memIndex;

		if (vkAllocateMemory(m_logicalDevice, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS)
		{
			vkDestroyImage(m_logicalDevice, image, nullptr);
			cemuLog_log(LogType::Force, "Screenshot request failed due to failed memory allocation.");
			return;
		}

		vkBindImageMemory(m_logicalDevice, image, imageMemory, 0);

		{
			VkImageSubresourceRange range;
			range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			range.baseMipLevel = 0;
			range.levelCount = 1;
			range.baseArrayLayer = 0;
			range.layerCount = 1;
			barrier_image<TRANSFER_READ, TRANSFER_WRITE>(image, range, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		}
		{
			VkImageSubresourceLayers range;
			range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			range.mipLevel = 0;
			range.baseArrayLayer = texViewVk->firstSlice;
			range.layerCount = 1;
			barrier_image<IMAGE_WRITE | TRANSFER_WRITE, SYNC_OP::TRANSFER_READ>(baseImageTex, range, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
		}

		VkOffset3D blitSize{width, height, 1};
		VkImageBlit imageBlitRegion{};
		imageBlitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageBlitRegion.srcSubresource.mipLevel = 0;
		imageBlitRegion.srcSubresource.baseArrayLayer = texViewVk->firstSlice;
		imageBlitRegion.srcSubresource.layerCount = 1;
		imageBlitRegion.srcOffsets[1] = blitSize;

		imageBlitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageBlitRegion.dstSubresource.mipLevel = 0;
		imageBlitRegion.dstSubresource.baseArrayLayer = 0;
		imageBlitRegion.dstSubresource.layerCount = 1;
		imageBlitRegion.dstOffsets[1] = blitSize;

		vkCmdBlitImage(m_state.currentCommandBuffer, dumpImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageBlitRegion, VK_FILTER_NEAREST);

		{
			VkImageSubresourceRange range;
			range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			range.baseMipLevel = 0;
			range.levelCount = 1;
			range.baseArrayLayer = 0;
			range.layerCount = 1;
			barrier_image<TRANSFER_WRITE, TRANSFER_READ>(image, range, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
		}
		{
			VkImageSubresourceLayers range;
			range.aspectMask = baseImageTex->GetImageAspect();
			range.mipLevel = 0;
			range.baseArrayLayer = texViewVk->firstSlice;
			range.layerCount = 1;
			barrier_image<TRANSFER_READ, TRANSFER_WRITE | IMAGE_WRITE>(baseImageTex, range, VK_IMAGE_LAYOUT_GENERAL);
		}

		format = VK_FORMAT_R8G8B8A8_UNORM;
		dumpImage = image;
	}

	uint32 size;
	switch (format)
	{
	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_R8G8B8A8_SRGB:
		size = 4 * width * height;
		break;
	case VK_FORMAT_R8G8B8_UNORM:
	case VK_FORMAT_R8G8B8_SRGB:
		size = 3 * width * height;
		break;
	default:
		size = 0;
	}

	if (size == 0)
	{
		cemu_assert_debug(false);
		return;
	}

	VkBufferImageCopy region{};
	region.bufferOffset = 0;
	region.bufferRowLength = width;
	region.bufferImageHeight = height;

	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount = 1;
	region.imageSubresource.mipLevel = 0;

	region.imageOffset = { 0,0,0 };
	region.imageExtent = { (uint32)width,(uint32)height,1 };

	void* bufferPtr = nullptr;

	VkBuffer buffer = nullptr;
	VkDeviceMemory bufferMemory = nullptr;
	memoryManager->CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, buffer, bufferMemory);
	vkMapMemory(m_logicalDevice, bufferMemory, 0, VK_WHOLE_SIZE, 0, &bufferPtr);

	if (dumpImage == baseImage)
	{
		region.imageSubresource.baseArrayLayer = texViewVk->firstSlice;
		barrier_image<IMAGE_WRITE | TRANSFER_WRITE, TRANSFER_READ>(baseImageTex, region.imageSubresource, VK_IMAGE_LAYOUT_GENERAL);
	}

	vkCmdCopyImageToBuffer(m_state.currentCommandBuffer, dumpImage, VK_IMAGE_LAYOUT_GENERAL, buffer, 1, &region);

	SubmitCommandBuffer();
	WaitCommandBufferFinished(GetCurrentCommandBufferId());

	bool formatValid = true;

	std::vector<uint8> rgb_data;
	rgb_data.reserve(3 * width * height);

	switch (format)
	{
	case VK_FORMAT_R8G8B8A8_UNORM:
		for (auto ptr = (uint8*)bufferPtr; ptr < (uint8*)bufferPtr + size; ptr += 4)
		{
			rgb_data.emplace_back(*ptr);
			rgb_data.emplace_back(*(ptr + 1));
			rgb_data.emplace_back(*(ptr + 2));
		}
		break;
	case VK_FORMAT_R8G8B8A8_SRGB:
		for (auto ptr = (uint8*)bufferPtr; ptr < (uint8*)bufferPtr + size; ptr += 4)
		{
			rgb_data.emplace_back(SRGBComponentToRGB(*ptr));
			rgb_data.emplace_back(SRGBComponentToRGB(*(ptr + 1)));
			rgb_data.emplace_back(SRGBComponentToRGB(*(ptr + 2)));
		}
		break;
	case VK_FORMAT_R8G8B8_UNORM:
		std::copy((uint8*)bufferPtr, (uint8*)bufferPtr + size, rgb_data.begin());
		break;
	case VK_FORMAT_R8G8B8_SRGB:
		std::transform((uint8*)bufferPtr, (uint8*)bufferPtr + size, rgb_data.begin(), SRGBComponentToRGB);
		break;
	default:
		formatValid = false;
		cemu_assert_debug(false);
	}

	vkUnmapMemory(m_logicalDevice, bufferMemory);
	vkFreeMemory(m_logicalDevice, bufferMemory, nullptr);
	vkDestroyBuffer(m_logicalDevice, buffer, nullptr);

	if (image)
		vkDestroyImage(m_logicalDevice, image, nullptr);
	if (imageMemory)
		vkFreeMemory(m_logicalDevice, imageMemory, nullptr);

	if (formatValid)
		SaveScreenshot(rgb_data, width, height, !padView);
}

static const float kQueuePriority = 1.0f;

std::vector<VkDeviceQueueCreateInfo> VulkanRenderer::CreateQueueCreateInfos(const std::set<sint32>& uniqueQueueFamilies) const
{
	std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;

	for (int queueFamily : uniqueQueueFamilies)
	{
		VkDeviceQueueCreateInfo queueCreateInfo{};
		queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfo.queueFamilyIndex = queueFamily;
		queueCreateInfo.queueCount = 1;
		queueCreateInfo.pQueuePriorities = &kQueuePriority;
		queueCreateInfos.emplace_back(queueCreateInfo);
	}

	return queueCreateInfos;
}

VkDeviceCreateInfo VulkanRenderer::CreateDeviceCreateInfo(const std::vector<VkDeviceQueueCreateInfo>& queueCreateInfos, const VkPhysicalDeviceFeatures& deviceFeatures, const void* deviceExtensionStructs, std::vector<const char*>& used_extensions) const
{
	used_extensions = kRequiredDeviceExtensions;
	if (m_featureControl.deviceExtensions.sampler_mirror_clamp_to_edge)
		used_extensions.emplace_back(VK_KHR_SAMPLER_MIRROR_CLAMP_TO_EDGE_EXTENSION_NAME);
	if (m_featureControl.deviceExtensions.portability_subset)
		used_extensions.emplace_back(kPortabilitySubsetExtName);
	if (m_featureControl.deviceExtensions.memory_budget)
		used_extensions.emplace_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
	if (m_featureControl.deviceExtensions.tooling_info)
		used_extensions.emplace_back(VK_EXT_TOOLING_INFO_EXTENSION_NAME);
	if (m_featureControl.deviceExtensions.depth_range_unrestricted)
		used_extensions.emplace_back(VK_EXT_DEPTH_RANGE_UNRESTRICTED_EXTENSION_NAME);
	if (m_featureControl.deviceExtensions.nv_fill_rectangle)
		used_extensions.emplace_back(VK_NV_FILL_RECTANGLE_EXTENSION_NAME);
	if (m_featureControl.deviceExtensions.pipeline_feedback)
		used_extensions.emplace_back(VK_EXT_PIPELINE_CREATION_FEEDBACK_EXTENSION_NAME);
	if (m_featureControl.deviceExtensions.cubic_filter)
		used_extensions.emplace_back(VK_EXT_FILTER_CUBIC_EXTENSION_NAME);
	if (m_featureControl.deviceExtensions.custom_border_color)
		used_extensions.emplace_back(VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME);
	if (m_featureControl.deviceExtensions.driver_properties)
		used_extensions.emplace_back(VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME);
	if (m_featureControl.deviceExtensions.external_memory_host)
		used_extensions.emplace_back(VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME);
	if (m_featureControl.deviceExtensions.synchronization2)
		used_extensions.emplace_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
	if (m_featureControl.deviceExtensions.dynamic_rendering)
		used_extensions.emplace_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
	if (m_featureControl.deviceExtensions.shader_float_controls)
		used_extensions.emplace_back(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);
	if (m_featureControl.deviceExtensions.depth_clip_enable)
		used_extensions.emplace_back(VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME);
	if (m_featureControl.deviceExtensions.present_wait)
	{
		used_extensions.emplace_back(VK_KHR_PRESENT_ID_EXTENSION_NAME);
		used_extensions.emplace_back(VK_KHR_PRESENT_WAIT_EXTENSION_NAME);
	}
	if (m_featureControl.deviceExtensions.pipeline_robustness)
		used_extensions.emplace_back(VK_EXT_PIPELINE_ROBUSTNESS_EXTENSION_NAME);

	VkDeviceCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	createInfo.pQueueCreateInfos = queueCreateInfos.data();
	createInfo.queueCreateInfoCount = (uint32_t)queueCreateInfos.size();
	createInfo.pEnabledFeatures = &deviceFeatures;
	createInfo.enabledExtensionCount = used_extensions.size();
	createInfo.ppEnabledExtensionNames = used_extensions.data();

	createInfo.pNext = deviceExtensionStructs;

	if (!m_layerNames.empty())
	{
		createInfo.enabledLayerCount = m_layerNames.size();
		createInfo.ppEnabledLayerNames = m_layerNames.data();
	}

	return createInfo;
}

RendererShader* VulkanRenderer::shader_create(RendererShader::ShaderType type, uint64 baseHash, uint64 auxHash, const std::string& source, bool isGameShader, bool isGfxPackShader)
{
	return new RendererShaderVk(type, baseHash, auxHash, isGameShader, isGfxPackShader, source);
}

VulkanRenderer::QueueFamilyIndices VulkanRenderer::FindQueueFamilies(VkSurfaceKHR surface, VkPhysicalDevice device)
{
	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

	std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

	QueueFamilyIndices indices;
	for (int i = 0; i < (int)queueFamilies.size(); ++i)
	{
		const auto& queueFamily = queueFamilies[i];
		if (queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
			indices.graphicsFamily = i;

		VkBool32 presentSupport = false;
		const VkResult result = vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
		if (result != VK_SUCCESS)
			throw std::runtime_error(fmt::format("Error while attempting to check if a surface supports presentation: {}", result));

		if (queueFamily.queueCount > 0 && presentSupport)
			indices.presentFamily = i;

		if (indices.IsComplete())
			break;
	}

	return indices;
}

bool VulkanRenderer::CheckDeviceExtensionSupport(const VkPhysicalDevice device, FeatureControl& info)
{
	std::vector<VkExtensionProperties> availableDeviceExtensions;

	auto isExtensionAvailable = [&availableDeviceExtensions](const char* extensionName) -> bool
	{
		return std::find_if(availableDeviceExtensions.begin(), availableDeviceExtensions.end(),
			[&extensionName](const VkExtensionProperties& prop) -> bool
		{
			return strcmp(prop.extensionName, extensionName) == 0;
		}) != availableDeviceExtensions.cend();
	};

	uint32_t extensionCount;
	VkResult result = vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
	if (result != VK_SUCCESS)
		throw std::runtime_error(fmt::format("Cannot retrieve count of properties for a physical device: {}", result));

	availableDeviceExtensions.resize(extensionCount);
	result = vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableDeviceExtensions.data());
	if (result != VK_SUCCESS)
		throw std::runtime_error(fmt::format("Cannot retrieve properties for a physical device: {}", result));

	std::set<std::string> requiredExtensions(kRequiredDeviceExtensions.begin(), kRequiredDeviceExtensions.end());
	for (const auto& extension : availableDeviceExtensions)
	{
		requiredExtensions.erase(extension.extensionName);
	}

	info.deviceExtensions.sampler_mirror_clamp_to_edge = isExtensionAvailable(VK_KHR_SAMPLER_MIRROR_CLAMP_TO_EDGE_EXTENSION_NAME);
	info.samplerMirrorClampToEdgeCore = false;
	if (!info.deviceExtensions.sampler_mirror_clamp_to_edge)
	{
		VkPhysicalDeviceProperties deviceProperties{};
		vkGetPhysicalDeviceProperties(device, &deviceProperties);
		if (deviceProperties.apiVersion >= VK_API_VERSION_1_2)
		{
			VkPhysicalDeviceVulkan12Features features12{};
			features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
			VkPhysicalDeviceFeatures2 features2{};
			features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
			features2.pNext = &features12;
			vkGetPhysicalDeviceFeatures2(device, &features2);
			info.samplerMirrorClampToEdgeCore = features12.samplerMirrorClampToEdge == VK_TRUE;
		}
		if (!info.samplerMirrorClampToEdgeCore)
			cemuLog_log(LogType::Force, "Vulkan: neither VK_KHR_sampler_mirror_clamp_to_edge nor the Vulkan 1.2 samplerMirrorClampToEdge feature is available, mirror-clamp sampling will fall back to mirrored repeat");
	}

	info.deviceExtensions.portability_subset = isExtensionAvailable(kPortabilitySubsetExtName);
	info.deviceExtensions.memory_budget = isExtensionAvailable(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
	info.deviceExtensions.tooling_info = isExtensionAvailable(VK_EXT_TOOLING_INFO_EXTENSION_NAME);
	info.deviceExtensions.depth_range_unrestricted = isExtensionAvailable(VK_EXT_DEPTH_RANGE_UNRESTRICTED_EXTENSION_NAME);
	info.deviceExtensions.nv_fill_rectangle = isExtensionAvailable(VK_NV_FILL_RECTANGLE_EXTENSION_NAME);
	info.deviceExtensions.pipeline_feedback = isExtensionAvailable(VK_EXT_PIPELINE_CREATION_FEEDBACK_EXTENSION_NAME);
	info.deviceExtensions.cubic_filter = isExtensionAvailable(VK_EXT_FILTER_CUBIC_EXTENSION_NAME);
	info.deviceExtensions.custom_border_color = isExtensionAvailable(VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME);
	info.deviceExtensions.driver_properties = isExtensionAvailable(VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME);
	info.deviceExtensions.external_memory_host = isExtensionAvailable(VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME);
	info.deviceExtensions.synchronization2 = isExtensionAvailable(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
	info.deviceExtensions.shader_float_controls = isExtensionAvailable(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);
	info.deviceExtensions.dynamic_rendering = false;
	info.deviceExtensions.depth_clip_enable = isExtensionAvailable(VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME);
	info.deviceExtensions.pipeline_robustness = isExtensionAvailable(VK_EXT_PIPELINE_ROBUSTNESS_EXTENSION_NAME);
	info.deviceExtensions.present_wait = isExtensionAvailable(VK_KHR_PRESENT_WAIT_EXTENSION_NAME) && isExtensionAvailable(VK_KHR_PRESENT_ID_EXTENSION_NAME);

	info.usingDebugMarkerTool = false;
	info.usingTracingTool = false;
	if (info.deviceExtensions.tooling_info && vkGetPhysicalDeviceToolPropertiesEXT)
	{
		uint32_t toolCount = 0;
		if (vkGetPhysicalDeviceToolPropertiesEXT(device, &toolCount, nullptr) == VK_SUCCESS)
		{
			std::vector<VkPhysicalDeviceToolPropertiesEXT> toolProperties(toolCount);
			if (toolCount > 0 && vkGetPhysicalDeviceToolPropertiesEXT(device, &toolCount, toolProperties.data()) == VK_SUCCESS)
			{
				for (auto& itr : toolProperties)
				{
					if ((itr.purposes & VK_TOOL_PURPOSE_DEBUG_MARKERS_BIT_EXT) != 0 && info.instanceExtensions.debug_utils && vkSetDebugUtilsObjectNameEXT)
						info.usingDebugMarkerTool = true;
					if ((itr.purposes & VK_TOOL_PURPOSE_TRACING_BIT) != 0)
						info.usingTracingTool = true;
				}
			}
		}
	}

	if (!requiredExtensions.empty())
	{
		std::string missing;
		for (const auto& ext : requiredExtensions)
			missing += (missing.empty() ? "" : ", ") + ext;
		cemuLog_log(LogType::Force, "Vulkan: device is missing required extension(s): {}", missing);
	}

	return requiredExtensions.empty();
}

std::vector<const char*> VulkanRenderer::CheckInstanceExtensionSupport(FeatureControl& info)
{
	std::vector<VkExtensionProperties> availableInstanceExtensions;
	std::vector<const char*> enabledInstanceExtensions;
	VkResult err;

	auto isExtensionAvailable = [&availableInstanceExtensions](const char* extensionName) -> bool
	{
		return std::find_if(availableInstanceExtensions.begin(), availableInstanceExtensions.end(),
			[&extensionName](const VkExtensionProperties& prop) -> bool
		{
			return strcmp(prop.extensionName, extensionName) == 0;
		}) != availableInstanceExtensions.cend();
	};

	uint32_t count;
	if ((err = vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr)) != VK_SUCCESS)
		throw std::runtime_error(fmt::format("Failed to retrieve the instance extension properties : {}", err));

	availableInstanceExtensions.resize(count);
	if ((err = vkEnumerateInstanceExtensionProperties(nullptr, &count, availableInstanceExtensions.data())) != VK_SUCCESS)
		throw std::runtime_error(fmt::format("Failed to retrieve the instance extension properties: {}", err));

	std::vector<const char*> requiredInstanceExtensions;
	requiredInstanceExtensions.emplace_back(VK_KHR_SURFACE_EXTENSION_NAME);
	#if BOOST_OS_WINDOWS
	requiredInstanceExtensions.emplace_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
	#elif BOOST_OS_LINUX || BOOST_OS_BSD
	auto backend = WindowSystem::GetWindowInfo().window_main.backend;
	if(backend == WindowSystem::WindowHandleInfo::Backend::X11)
		requiredInstanceExtensions.emplace_back(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
	#if HAS_WAYLAND
	else if (backend == WindowSystem::WindowHandleInfo::Backend::Wayland)
		requiredInstanceExtensions.emplace_back(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
	#endif
	#elif BOOST_OS_MACOS || BOOST_OS_IOS
	requiredInstanceExtensions.emplace_back(VK_EXT_METAL_SURFACE_EXTENSION_NAME);
	#endif
	if (cemuLog_isLoggingEnabled(LogType::VulkanValidation))
		requiredInstanceExtensions.emplace_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);

	for (const auto& extension : availableInstanceExtensions)
	{
		for (auto it = requiredInstanceExtensions.begin(); it < requiredInstanceExtensions.end(); ++it)
		{
			if (strcmp(*it, extension.extensionName) == 0)
			{
				enabledInstanceExtensions.emplace_back(*it);
				requiredInstanceExtensions.erase(it);
				break;
			}
		}
	}
	if (!requiredInstanceExtensions.empty())
	{
		cemuLog_log(LogType::Force, "The following required Vulkan instance extensions are not supported:");

		std::stringstream ss;
		for (const auto& extension : requiredInstanceExtensions)
			cemuLog_log(LogType::Force, "{}", extension);
		cemuLog_waitForFlush();
		throw std::runtime_error(ss.str());
	}

	info.instanceExtensions.debug_utils = isExtensionAvailable(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	if (info.instanceExtensions.debug_utils)
		enabledInstanceExtensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	info.instanceExtensions.portability_enumeration = isExtensionAvailable(kPortabilityEnumerationExtName);
	if (info.instanceExtensions.portability_enumeration)
		enabledInstanceExtensions.emplace_back(kPortabilityEnumerationExtName);

	{
		std::string available, enabled;
		for (const auto& ext : availableInstanceExtensions)
			available += (available.empty() ? "" : ", ") + std::string(ext.extensionName);
		for (const char* ext : enabledInstanceExtensions)
			enabled += (enabled.empty() ? "" : ", ") + std::string(ext);
		cemuLog_log(LogType::Force, "Vulkan diagnostic: {} instance extension(s) available: {}", availableInstanceExtensions.size(), available);
		cemuLog_log(LogType::Force, "Vulkan diagnostic: instance extensions enabled: {}", enabled);
	}
	return enabledInstanceExtensions;
}

bool VulkanRenderer::IsDeviceSuitable(VkSurfaceKHR surface, const VkPhysicalDevice& device)
{
	VkPhysicalDeviceProperties deviceProperties{};
	vkGetPhysicalDeviceProperties(device, &deviceProperties);

	const auto queueFamilies = FindQueueFamilies(surface, device);
	if (!queueFamilies.IsComplete())
	{
		cemuLog_log(LogType::Force, "Vulkan: device '{}' rejected - no queue family with {}",
			deviceProperties.deviceName,
			queueFamilies.graphicsFamily < 0 ? "graphics support" : "presentation support for this surface");
		return false;
	}

	uint32 vkVersionMajor = VK_API_VERSION_MAJOR(deviceProperties.apiVersion);
	uint32 vkVersionMinor = VK_API_VERSION_MINOR(deviceProperties.apiVersion);
	if (vkVersionMajor < 1 || (vkVersionMajor == 1 && vkVersionMinor < 1))
	{
		cemuLog_log(LogType::Force, "Vulkan: device '{}' rejected - reports API version {}.{}, minimum required is 1.1",
			deviceProperties.deviceName, vkVersionMajor, vkVersionMinor);
		return false;
	}

	FeatureControl info;
	if (!CheckDeviceExtensionSupport(device, info))
	{
		cemuLog_log(LogType::Force, "Vulkan: device '{}' rejected - missing a required device extension (see the CheckDeviceExtensionSupport log lines above this one)", deviceProperties.deviceName);
		return false;
	}

	const auto swapchainSupport = SwapchainInfoVk::QuerySwapchainSupport(surface, device);
	if (swapchainSupport.formats.empty() || swapchainSupport.presentModes.empty())
	{
		cemuLog_log(LogType::Force, "Vulkan: device '{}' rejected - surface reports {} formats and {} present modes (both must be nonzero)",
			deviceProperties.deviceName, swapchainSupport.formats.size(), swapchainSupport.presentModes.size());
		return false;
	}

	return true;
}

#if BOOST_OS_WINDOWS
VkSurfaceKHR VulkanRenderer::CreateWinSurface(VkInstance instance, HWND hwindow)
{
	VkWin32SurfaceCreateInfoKHR sci{};
	sci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	sci.hwnd = hwindow;
	sci.hinstance = GetModuleHandle(nullptr);

	VkSurfaceKHR result;
	VkResult err;
	if ((err = vkCreateWin32SurfaceKHR(instance, &sci, nullptr, &result)) != VK_SUCCESS)
	{
		cemuLog_log(LogType::Force, "Cannot create a Win32 Vulkan surface: {}", (sint32)err);
		throw std::runtime_error(fmt::format("Cannot create a Win32 Vulkan surface: {}", err));
	}

	return result;
}
#endif

#if BOOST_OS_LINUX || BOOST_OS_BSD
VkSurfaceKHR VulkanRenderer::CreateXlibSurface(VkInstance instance, Display* dpy, Window window)
{
    VkXlibSurfaceCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    sci.flags = 0;
	sci.dpy = dpy;
    sci.window = window;

    VkSurfaceKHR result;
    VkResult err;
    if ((err = vkCreateXlibSurfaceKHR(instance, &sci, nullptr, &result)) != VK_SUCCESS)
    {
		cemuLog_log(LogType::Force, "Cannot create a X11 Vulkan surface: {}", (sint32)err);
        throw std::runtime_error(fmt::format("Cannot create a X11 Vulkan surface: {}", err));
    }

    return result;
}

VkSurfaceKHR VulkanRenderer::CreateXcbSurface(VkInstance instance, xcb_connection_t* connection, xcb_window_t window)
{
    VkXcbSurfaceCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
    sci.flags = 0;
    sci.connection = connection;
    sci.window = window;

    VkSurfaceKHR result;
    VkResult err;
    if ((err = vkCreateXcbSurfaceKHR(instance, &sci, nullptr, &result)) != VK_SUCCESS)
    {
        cemuLog_log(LogType::Force, "Cannot create a XCB Vulkan surface: {}", (sint32)err);
        throw std::runtime_error(fmt::format("Cannot create a XCB Vulkan surface: {}", err));
    }

    return result;
}
#ifdef HAS_WAYLAND
VkSurfaceKHR VulkanRenderer::CreateWaylandSurface(VkInstance instance, wl_display* display, wl_surface* surface)
{
    VkWaylandSurfaceCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
    sci.flags = 0;
	sci.display = display;
	sci.surface = surface;

    VkSurfaceKHR result;
    VkResult err;
    if ((err = vkCreateWaylandSurfaceKHR(instance, &sci, nullptr, &result)) != VK_SUCCESS)
    {
        cemuLog_log(LogType::Force, "Cannot create a Wayland Vulkan surface: {}", (sint32)err);
        throw std::runtime_error(fmt::format("Cannot create a Wayland Vulkan surface: {}", err));
    }

    return result;
}
#endif
#endif

VkSurfaceKHR VulkanRenderer::CreateFramebufferSurface(VkInstance instance, WindowSystem::WindowHandleInfo& windowInfo)
{
#if BOOST_OS_WINDOWS
	return CreateWinSurface(instance, static_cast<HWND>(windowInfo.surface));
#elif BOOST_OS_LINUX || BOOST_OS_BSD
	if(windowInfo.backend == WindowSystem::WindowHandleInfo::Backend::X11)
		return CreateXlibSurface(instance, static_cast<Display*>(windowInfo.display), reinterpret_cast<Window>(windowInfo.surface));
	#ifdef HAS_WAYLAND
	if(windowInfo.backend == WindowSystem::WindowHandleInfo::Backend::Wayland)
		return CreateWaylandSurface(instance, static_cast<wl_display*>(windowInfo.display), static_cast<wl_surface*>(windowInfo.surface));
	#endif
	return {};
#elif BOOST_OS_MACOS || BOOST_OS_IOS
	return CreateCocoaSurface(instance, windowInfo.surface);
#endif
}

void VulkanRenderer::CreateCommandPool()
{
	VkCommandPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.queueFamilyIndex = m_indices.graphicsFamily;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

	VkResult result = vkCreateCommandPool(m_logicalDevice, &poolInfo, nullptr, &m_commandPool);
	if (result != VK_SUCCESS)
		throw std::runtime_error(fmt::format("Failed to create command pool: {}", result));
}

void VulkanRenderer::CreateCommandBuffers()
{
	auto it = m_cmd_buffer_fences.begin();
	VkFenceCreateInfo fenceInfo{};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	vkCreateFence(m_logicalDevice, &fenceInfo, nullptr, &*it);

	++it;
	fenceInfo.flags = 0;
	for (; it != m_cmd_buffer_fences.end(); ++it)
	{
		vkCreateFence(m_logicalDevice, &fenceInfo, nullptr, &*it);
	}

	VkCommandBufferAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = m_commandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = (uint32_t)m_commandBuffers.size();

	const VkResult result = vkAllocateCommandBuffers(m_logicalDevice, &allocInfo, m_commandBuffers.data());
	if (result != VK_SUCCESS)
	{
		cemuLog_log(LogType::Force, "Failed to allocate command buffers: {}", result);
		throw std::runtime_error(fmt::format("Failed to allocate command buffers: {}", result));
	}

	for (auto& semItr : m_commandBufferSemaphores)
	{
		VkSemaphoreCreateInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		if (vkCreateSemaphore(m_logicalDevice, &info, nullptr, &semItr) != VK_SUCCESS)
			UnrecoverableError("Failed to create semaphore for command buffer");
	}
}

bool VulkanRenderer::IsSwapchainInfoValid(bool mainWindow) const
{
	auto& chainInfo = GetChainInfoPtr(mainWindow);
	return chainInfo && chainInfo->IsValid();
}

void VulkanRenderer::CreateNullTexture(NullTexture& nullTex, VkImageType imageType)
{
	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	if (imageType == VK_IMAGE_TYPE_1D)
	{
		imageInfo.extent.width = 4;
		imageInfo.extent.height = 1;
	}
	else if (imageType == VK_IMAGE_TYPE_2D)
	{
		imageInfo.extent.width = 4;
		imageInfo.extent.height = 1;
	}
	else
	{
		cemu_assert(false);
	}
	imageInfo.mipLevels = 1;
	imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.extent.depth = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.imageType = imageType;
	imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
	if (vkCreateImage(m_logicalDevice, &imageInfo, nullptr, &nullTex.image) != VK_SUCCESS)
		UnrecoverableError("Failed to create nullTex image");
	nullTex.allocation = memoryManager->imageMemoryAllocate(nullTex.image);

	VkClearColorValue clrColor{};
	ClearColorImageRaw(nullTex.image, 0, 0, clrColor, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
	VkImageViewCreateInfo viewInfo{};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = nullTex.image;
	if (imageType == VK_IMAGE_TYPE_1D)
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_1D;
	else if (imageType == VK_IMAGE_TYPE_2D)
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	else
	{
		cemu_assert(false);
	}
	viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = 1;
	if (vkCreateImageView(m_logicalDevice, &viewInfo, nullptr, &nullTex.view) != VK_SUCCESS)
		UnrecoverableError("Failed to create nullTex image view");
	VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.mipLodBias = 0.0f;
	samplerInfo.compareOp = VK_COMPARE_OP_NEVER;
	samplerInfo.minLod = 0.0f;
	samplerInfo.maxLod = 0.0f;
	samplerInfo.maxAnisotropy = 1.0;
	samplerInfo.anisotropyEnable = VK_FALSE;
	samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	vkCreateSampler(m_logicalDevice, &samplerInfo, nullptr, &nullTex.sampler);
}

void VulkanRenderer::CreateNullObjects()
{
	CreateNullTexture(nullTexture1D, VK_IMAGE_TYPE_1D);
	CreateNullTexture(nullTexture2D, VK_IMAGE_TYPE_2D);
}

void VulkanRenderer::DeleteNullTexture(NullTexture& nullTex)
{
	vkDestroySampler(m_logicalDevice, nullTex.sampler, nullptr);
	nullTex.sampler = VK_NULL_HANDLE;
	vkDestroyImageView(m_logicalDevice, nullTex.view, nullptr);
	nullTex.view = VK_NULL_HANDLE;
	vkDestroyImage(m_logicalDevice, nullTex.image, nullptr);
	nullTex.image = VK_NULL_HANDLE;
	memoryManager->imageMemoryFree(nullTex.allocation);
	nullTex.allocation = nullptr;
}

void VulkanRenderer::DeleteNullObjects()
{
	DeleteNullTexture(nullTexture1D);
	DeleteNullTexture(nullTexture2D);
}

void VulkanRenderer::ImguiInit()
{
	VkRenderPass prevRenderPass = m_imguiRenderPass;

	VkAttachmentDescription colorAttachment = {};
	colorAttachment.format = m_mainSwapchainInfo->m_surfaceFormat.format;
	colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference colorAttachmentRef = {};
	colorAttachmentRef.attachment = 0;
	colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorAttachmentRef;

	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = 1;
	renderPassInfo.pAttachments = &colorAttachment;
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;
	const auto result = vkCreateRenderPass(m_logicalDevice, &renderPassInfo, nullptr, &m_imguiRenderPass);
	if (result != VK_SUCCESS)
		throw VkException(result, "can't create imgui renderpass");

	ImGui_ImplVulkan_InitInfo info{};
	info.Instance = m_instance;
	info.PhysicalDevice = m_physicalDevice;
	info.Device = m_logicalDevice;
	info.QueueFamily = m_indices.presentFamily;
	info.Queue = m_presentQueue;
	info.PipelineCache = m_pipeline_cache;
	info.DescriptorPool = m_descriptorPool;
	info.MinImageCount = m_mainSwapchainInfo->m_swapchainImages.size();
	info.ImageCount = info.MinImageCount;

	ImGui_ImplVulkan_Init(&info, m_imguiRenderPass);
	m_imguiBackendInitialized = true;

	if (prevRenderPass != VK_NULL_HANDLE)
		vkDestroyRenderPass(GetLogicalDevice(), prevRenderPass, nullptr);
}

void VulkanRenderer::Initialize()
{
	m_initializeCalled.store(true);
	Renderer::Initialize();
	StartRenderWorker("VulkanSubmit");
	InitFirstCommandBuffer();
	CreatePipelineCache();
	ImguiInit();
	CreateNullObjects();
}

void VulkanRenderer::Shutdown()
{
	cemuLog_log(LogType::Force, "Vulkan: pipelines compiled this session: {} ({} of them asynchronously), presented frames {}", g_compiling_pipelines.load(), g_compiling_pipelines_async.load(), LatteWait::Get().presentedFrames.load());
	SubmitCommandBuffer();
	WaitDeviceIdle();
	StopRenderWorker();
	RendererShaderVk::Shutdown();
	PipelineCompiler::CompileThreadPool_Stop();

	DeleteFontTextures();
	Renderer::Shutdown();
	if (m_imguiRenderPass != VK_NULL_HANDLE)
	{
		vkDestroyRenderPass(m_logicalDevice, m_imguiRenderPass, nullptr);
		m_imguiRenderPass = VK_NULL_HANDLE;
	}
	RendererShaderVk::Shutdown();
}

#if BOOST_OS_IOS
void IOSBridge_VulkanDeviceLost(const char* why);
#endif

void VulkanRenderer::HandleDeviceLost(const char* what)
{
	if (m_deviceLost.exchange(true))
		return;
	cemuLog_log(LogType::Force, "Vulkan: DEVICE LOST ({}). The GPU stopped running this app's work. No further Vulkan work will be submitted and the title is being stopped", what);
	auto& w = LatteWait::Get();
	w.gpuError.store(true);
	w.gpuErrorCode.store((int32_t)VK_ERROR_DEVICE_LOST);
	w.gpuPresumedLost.store(true);
#if BOOST_OS_IOS
	IOSBridge_VulkanDeviceLost(what);
#endif
}

void VulkanRenderer::UnrecoverableError(const char* errMsg) const
{
	cemuLog_log(LogType::Force, "Unrecoverable error in Vulkan renderer");
	cemuLog_log(LogType::Force, "Msg: {}", errMsg);
#if BOOST_OS_IOS
	if (m_initializeCalled.load())
		const_cast<VulkanRenderer*>(this)->HandleDeviceLost(errMsg);
#endif
	throw std::runtime_error(errMsg);
}

struct VulkanRequestedFormat_t
{
	VkFormat fmt;
	const char* name;
	bool isDepth;
	bool mustSupportAttachment;
	bool mustSupportBlending;
};

#define reqColorFormat(__name, __reqAttachment, __reqBlend) {__name, ""#__name, false, __reqAttachment, __reqBlend}
#define reqDepthFormat(__name) {__name, ""#__name, true, true, false}

VulkanRequestedFormat_t requestedFormatList[] =
{
	reqDepthFormat(VK_FORMAT_D32_SFLOAT_S8_UINT),
	reqDepthFormat(VK_FORMAT_D24_UNORM_S8_UINT),
	reqDepthFormat(VK_FORMAT_D32_SFLOAT),
	reqDepthFormat(VK_FORMAT_D16_UNORM),
	reqColorFormat(VK_FORMAT_R32G32B32A32_SFLOAT, true, true),
	reqColorFormat(VK_FORMAT_R32G32B32A32_UINT, true, false),
	reqColorFormat(VK_FORMAT_R16G16B16A16_SFLOAT, true, true),
	reqColorFormat(VK_FORMAT_R16G16B16A16_UINT, true, false),
	reqColorFormat(VK_FORMAT_R16G16B16A16_UNORM, true, true),
	reqColorFormat(VK_FORMAT_R16G16B16A16_SNORM, true, true),
	reqColorFormat(VK_FORMAT_R8G8B8A8_UNORM, true, true),
	reqColorFormat(VK_FORMAT_R8G8B8A8_SNORM, true, true),
	reqColorFormat(VK_FORMAT_R8G8B8A8_SRGB, true, true),
	reqColorFormat(VK_FORMAT_R8G8B8A8_UINT, true, false),
	reqColorFormat(VK_FORMAT_R8G8B8A8_SINT, true, false),
	reqColorFormat(VK_FORMAT_R4G4B4A4_UNORM_PACK16, true, true),
	reqColorFormat(VK_FORMAT_R32G32_SFLOAT, true, true),
	reqColorFormat(VK_FORMAT_R32G32_UINT, true, false),
	reqColorFormat(VK_FORMAT_R16G16_UNORM, true, true),
	reqColorFormat(VK_FORMAT_R16G16_SFLOAT, true, true),
	reqColorFormat(VK_FORMAT_R8G8_UNORM, true, true),
	reqColorFormat(VK_FORMAT_R8G8_SNORM, true, true),
	reqColorFormat(VK_FORMAT_R4G4_UNORM_PACK8, true, true),
	reqColorFormat(VK_FORMAT_R32_SFLOAT, true, true),
	reqColorFormat(VK_FORMAT_R32_UINT, true, false),
	reqColorFormat(VK_FORMAT_R16_SFLOAT, true, true),
	reqColorFormat(VK_FORMAT_R16_UNORM, true, true),
	reqColorFormat(VK_FORMAT_R16_SNORM, true, true),
	reqColorFormat(VK_FORMAT_R8_UNORM, true, true),
	reqColorFormat(VK_FORMAT_R8_SNORM, true, true),
	reqColorFormat(VK_FORMAT_R5G6B5_UNORM_PACK16, true, true),
	reqColorFormat(VK_FORMAT_R5G5B5A1_UNORM_PACK16, true, true),
	reqColorFormat(VK_FORMAT_B10G11R11_UFLOAT_PACK32, true, true),
	reqColorFormat(VK_FORMAT_R16G16B16A16_SNORM, true, true),
	reqColorFormat(VK_FORMAT_BC1_RGBA_SRGB_BLOCK, false, false),
	reqColorFormat(VK_FORMAT_BC1_RGBA_UNORM_BLOCK, false, false),
	reqColorFormat(VK_FORMAT_BC2_UNORM_BLOCK, false, false),
	reqColorFormat(VK_FORMAT_BC2_SRGB_BLOCK, false, false),
	reqColorFormat(VK_FORMAT_BC3_UNORM_BLOCK, false, false),
	reqColorFormat(VK_FORMAT_BC3_SRGB_BLOCK, false, false),
	reqColorFormat(VK_FORMAT_BC4_UNORM_BLOCK, false, false),
	reqColorFormat(VK_FORMAT_BC4_SNORM_BLOCK, false, false),
	reqColorFormat(VK_FORMAT_BC5_UNORM_BLOCK, false, false),
	reqColorFormat(VK_FORMAT_BC5_SNORM_BLOCK, false, false),
	reqColorFormat(VK_FORMAT_A2B10G10R10_UNORM_PACK32, true, true),
	reqColorFormat(VK_FORMAT_R32_SFLOAT, true, true)
};

void VulkanRenderer::QueryMemoryInfo()
{
	VkPhysicalDeviceMemoryProperties memProperties;
	vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);
	cemuLog_log(LogType::Force, "Vulkan device memory info:");
	for (uint32 i = 0; i < memProperties.memoryHeapCount; i++)
	{
		cemuLog_log(LogType::Force, "Heap {} - Size {}MB Flags 0x{:08x}", i, (sint32)(memProperties.memoryHeaps[i].size / 1024ll / 1024ll), (uint32)memProperties.memoryHeaps[i].flags);
	}
	for (uint32 i = 0; i < memProperties.memoryTypeCount; i++)
	{
		cemuLog_log(LogType::Force, "Memory {} - HeapIndex {} Flags 0x{:08x}", i, (sint32)memProperties.memoryTypes[i].heapIndex, (uint32)memProperties.memoryTypes[i].propertyFlags);
	}
#if BOOST_OS_IOS
	{
		const uint64 mb = 1024ull * 1024ull;
		uint64 heapBytes = 0;
		for (uint32 i = 0; i < memProperties.memoryHeapCount; i++)
			if (memProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
				heapBytes = std::max<uint64>(heapBytes, memProperties.memoryHeaps[i].size);
		uint64 limitBytes = heapBytes;
		const uint64 processAvailable = (uint64)os_proc_available_memory();
		if (processAvailable > 0)
			limitBytes = limitBytes ? std::min(limitBytes, processAvailable) : processAvailable;
		uint64 driverBudget = 0;
		if (m_featureControl.deviceExtensions.memory_budget && vkGetPhysicalDeviceMemoryProperties2)
		{
			VkPhysicalDeviceMemoryBudgetPropertiesEXT budget{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT };
			VkPhysicalDeviceMemoryProperties2 props2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2 };
			props2.pNext = &budget;
			vkGetPhysicalDeviceMemoryProperties2(m_physicalDevice, &props2);
			for (uint32 i = 0; i < props2.memoryProperties.memoryHeapCount; i++)
				if (props2.memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
					driverBudget = std::max<uint64>(driverBudget, budget.heapBudget[i]);
			if (driverBudget > 0)
				limitBytes = limitBytes ? std::min(limitBytes, driverBudget) : driverBudget;
		}
		m_textureBudgetBytes = limitBytes ? std::max<uint64>(limitBytes * 45 / 100, 256 * mb) : 0;
		cemuLog_log(LogType::Force, "Vulkan: texture memory budget {} MB (device-local heap {} MB, process can still map {} MB, VK_EXT_memory_budget {})", m_textureBudgetBytes / mb, heapBytes / mb, processAvailable / mb,
			driverBudget ? fmt::format("{} MB", driverBudget / mb) : std::string("not exposed"));
	}
#endif
}

void VulkanRenderer::QueryAvailableFormats()
{
	VkFormatProperties fmtProp{};
	auto supportsSampledTexture = [this](VkFormat format)
	{
		VkFormatProperties properties{};
		vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &properties);
		constexpr VkFormatFeatureFlags requiredFeatures = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
		return (properties.optimalTilingFeatures & requiredFeatures) == requiredFeatures;
	};

	const VkFormat bcFormats[] =
	{
		VK_FORMAT_BC1_RGBA_SRGB_BLOCK,
		VK_FORMAT_BC1_RGBA_UNORM_BLOCK,
		VK_FORMAT_BC2_UNORM_BLOCK,
		VK_FORMAT_BC2_SRGB_BLOCK,
		VK_FORMAT_BC3_UNORM_BLOCK,
		VK_FORMAT_BC3_SRGB_BLOCK,
		VK_FORMAT_BC4_UNORM_BLOCK,
		VK_FORMAT_BC4_SNORM_BLOCK,
		VK_FORMAT_BC5_UNORM_BLOCK,
		VK_FORMAT_BC5_SNORM_BLOCK,
	};
	m_supportedFormatInfo.fmt_bc = m_supportedFormatInfo.fmt_bc && std::all_of(std::begin(bcFormats), std::end(bcFormats), supportsSampledTexture);
	m_supportedFormatInfo.fmt_astc = m_supportedFormatInfo.fmt_astc &&
		supportsSampledTexture(VK_FORMAT_ASTC_4x4_UNORM_BLOCK) &&
		supportsSampledTexture(VK_FORMAT_ASTC_4x4_SRGB_BLOCK);

	if (!m_supportedFormatInfo.fmt_bc)
	{
		cemuLog_log(LogType::Force, "BC texture compression is unavailable; using {} fallback", m_supportedFormatInfo.fmt_astc ? "ASTC 4x4" : "uncompressed");
	}

	vkGetPhysicalDeviceFormatProperties(m_physicalDevice, VK_FORMAT_D24_UNORM_S8_UINT, &fmtProp);
	if (fmtProp.optimalTilingFeatures != 0)
	{
		m_supportedFormatInfo.fmt_d24_unorm_s8_uint = true;
	}
	{
		VkFormatProperties d32s8{};
		vkGetPhysicalDeviceFormatProperties(m_physicalDevice, VK_FORMAT_D32_SFLOAT_S8_UINT, &d32s8);
		const bool d32s8Ok = (d32s8.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
		cemuLog_log(LogType::Force, "Vulkan: depth-stencil formats: D24_UNORM_S8_UINT {}, D32_SFLOAT_S8_UINT {}{}", m_supportedFormatInfo.fmt_d24_unorm_s8_uint ? "yes" : "no", d32s8Ok ? "yes" : "no",
			(!m_supportedFormatInfo.fmt_d24_unorm_s8_uint && !d32s8Ok) ? " - NEITHER is available, depth-stencil rendering will fail" : (m_supportedFormatInfo.fmt_d24_unorm_s8_uint ? "" : " (D24S8 titles use D32S8)"));
	}
	fmtProp = {};
	vkGetPhysicalDeviceFormatProperties(m_physicalDevice, VK_FORMAT_R4G4_UNORM_PACK8, &fmtProp);
	if (fmtProp.optimalTilingFeatures != 0)
	{
		m_supportedFormatInfo.fmt_r4g4_unorm_pack = true;
	}
	fmtProp = {};
	vkGetPhysicalDeviceFormatProperties(m_physicalDevice, VK_FORMAT_R5G6B5_UNORM_PACK16, &fmtProp);
	if (fmtProp.optimalTilingFeatures != 0)
	{
		m_supportedFormatInfo.fmt_r5g6b5_unorm_pack = true;
	}
	fmtProp = {};
	vkGetPhysicalDeviceFormatProperties(m_physicalDevice, VK_FORMAT_R4G4B4A4_UNORM_PACK16, &fmtProp);
	if (fmtProp.optimalTilingFeatures != 0)
	{
		m_supportedFormatInfo.fmt_r4g4b4a4_unorm_pack = true;
	}
	fmtProp = {};
	vkGetPhysicalDeviceFormatProperties(m_physicalDevice, VK_FORMAT_A1R5G5B5_UNORM_PACK16, &fmtProp);
	if (fmtProp.optimalTilingFeatures != 0)
	{
		m_supportedFormatInfo.fmt_a1r5g5b5_unorm_pack = true;
	}
	for (auto& it : requestedFormatList)
	{
		fmtProp = {};
		vkGetPhysicalDeviceFormatProperties(m_physicalDevice, it.fmt, &fmtProp);
		VkFormatFeatureFlags requestedBits = 0;
		if (it.mustSupportAttachment)
		{
			if (it.isDepth)
				requestedBits |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
			else
				requestedBits |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
			if (!it.isDepth && it.mustSupportBlending)
				requestedBits |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;
		}
		requestedBits |= VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
		requestedBits |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;

		if (fmtProp.optimalTilingFeatures == 0)
		{
			cemuLog_log(LogType::Force, "{} not supported", it.name);
		}
		else if ((fmtProp.optimalTilingFeatures & requestedBits) != requestedBits)
		{
		}
	}
}

bool VulkanRenderer::ImguiBegin(bool mainWindow)
{
	if (!Renderer::ImguiBegin(mainWindow))
		return false;

	auto& chainInfo = GetChainInfo(mainWindow);

	if (!AcquireNextSwapchainImage(mainWindow))
		return false;

	draw_endRenderPass();
	m_state.currentPipeline = VK_NULL_HANDLE;

	ImGui_ImplVulkan_CreateFontsTexture(m_state.currentCommandBuffer);
	ImGui_ImplVulkan_NewFrame(m_state.currentCommandBuffer, chainInfo.m_swapchainFramebuffers[chainInfo.swapchainImageIndex], chainInfo.getExtent());
	ImGui_UpdateWindowInformation(mainWindow);
	ImGui::NewFrame();
	return true;
}

void VulkanRenderer::ImguiEnd()
{
	ImGui::Render();
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), m_state.currentCommandBuffer);
	vkCmdEndRenderPass(m_state.currentCommandBuffer);
}

ImTextureID VulkanRenderer::GenerateTexture(const std::vector<uint8>& data, const Vector2i& size)
{
	try
	{
		std::vector <uint8> tmp(size.x * size.y * 4);
		for (size_t i = 0; i < data.size() / 3; ++i)
		{
			tmp[(i * 4) + 0] = data[(i * 3) + 0];
			tmp[(i * 4) + 1] = data[(i * 3) + 1];
			tmp[(i * 4) + 2] = data[(i * 3) + 2];
			tmp[(i * 4) + 3] = 0xFF;
		}
		return (ImTextureID)ImGui_ImplVulkan_GenerateTexture(m_state.currentCommandBuffer, tmp, size);
	}
	catch (const std::exception& ex)
	{
		cemuLog_log(LogType::Force, "can't generate imgui texture: {}", ex.what());
		return nullptr;
	}
}

void VulkanRenderer::DeleteTexture(ImTextureID id)
{
	WaitDeviceIdle();
	ImGui_ImplVulkan_DeleteTexture(id);
}

void VulkanRenderer::DeleteFontTextures()
{
	WaitDeviceIdle();
	ImGui_ImplVulkan_DestroyFontsTexture();
}

bool VulkanRenderer::BeginFrame(bool mainWindow)
{
	if (!AcquireNextSwapchainImage(mainWindow))
		return false;

	auto& chainInfo = GetChainInfo(mainWindow);

	VkClearColorValue clearColor{ 0, 0, 0, 0 };
	ClearColorImageRaw(chainInfo.m_swapchainImages[chainInfo.swapchainImageIndex], 0, 0, clearColor, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

	chainInfo.hasDefinedSwapchainImage = true;

	return true;
}

void VulkanRenderer::DrawEmptyFrame(bool mainWindow)
{
	if (!BeginFrame(mainWindow))
		return;
	SwapBuffers(mainWindow, !mainWindow);
}

void VulkanRenderer::InitFirstCommandBuffer()
{
	cemu_assert_debug(m_state.currentCommandBuffer == nullptr);
	m_commandBufferIndex = 0;
	m_commandBufferSyncIndex = 0;

	m_state.currentCommandBuffer = m_commandBuffers[m_commandBufferIndex];
	vkResetFences(m_logicalDevice, 1, &m_cmd_buffer_fences[m_commandBufferIndex]);
	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(m_state.currentCommandBuffer, &beginInfo);

	vkCmdSetViewport(m_state.currentCommandBuffer, 0, 1, &m_state.currentViewport);
	vkCmdSetScissor(m_state.currentCommandBuffer, 0, 1, &m_state.currentScissorRect);

	m_state.resetCommandBufferState();
}

void VulkanRenderer::ProcessFinishedCommandBuffers()
{
	bool finishedCmdBuffers = false;
	while (m_commandBufferSyncIndex != m_commandBufferIndex)
	{
		VkResult fenceStatus = m_deviceLost.load(std::memory_order_acquire) ? VK_SUCCESS : vkGetFenceStatus(m_logicalDevice, m_cmd_buffer_fences[m_commandBufferSyncIndex]);
		if (fenceStatus == VK_ERROR_DEVICE_LOST)
		{
			HandleDeviceLost("vkGetFenceStatus");
			fenceStatus = VK_SUCCESS;
		}
		if (fenceStatus == VK_SUCCESS)
		{
			ProcessDestructionQueue();
			m_uniformVarBufferReadIndex = m_cmdBufferUniformRingbufIndices[m_commandBufferSyncIndex];
			m_commandBufferSyncIndex = (m_commandBufferSyncIndex + 1) % m_commandBuffers.size();
			memoryManager->cleanupBuffers(m_countCommandBufferFinished);
			m_countCommandBufferFinished++;
			finishedCmdBuffers = true;
			continue;
		}
		else if (fenceStatus == VK_NOT_READY)
		{
			break;
		}
		cemuLog_log(LogType::Force, "vkGetFenceStatus returned unexpected error {}", (sint32)fenceStatus);
		cemu_assert_debug(false);
		break;
	}
	if (finishedCmdBuffers)
	{
		LatteTextureReadback_UpdateFinishedTransfers(false);
	}
	if (!m_commandBuffers.empty())
		LatteWait::Get().executingCommandBuffers.store((uint32_t)((m_commandBufferIndex + m_commandBuffers.size() - m_commandBufferSyncIndex) % m_commandBuffers.size()), std::memory_order_relaxed);
}

void VulkanRenderer::WaitForNextFinishedCommandBuffer()
{
	cemu_assert_debug(m_commandBufferSyncIndex != m_commandBufferIndex);
	if (!m_deviceLost.load(std::memory_order_acquire))
	{
		LatteWait::Scope waitScope("Vulkan: waiting for the GPU to finish a command buffer");
		constexpr int kMaxWaitSeconds = 12;
		VkResult result = VK_TIMEOUT;
		for (int second = 0; second < kMaxWaitSeconds; second++)
		{
			result = vkWaitForFences(m_logicalDevice, 1, &m_cmd_buffer_fences[m_commandBufferSyncIndex], true, 1'000'000'000);
			if (result != VK_TIMEOUT || m_deviceLost.load(std::memory_order_acquire))
				break;
		}
		if (result == VK_TIMEOUT)
		{
			LatteWait::Get().timeouts.fetch_add(1, std::memory_order_relaxed);
			LatteWait::Get().lastTimeoutReason.store("Vulkan: command buffer fence", std::memory_order_relaxed);
			HandleDeviceLost(fmt::format("a command buffer did not finish within {} s (GPU hang)", kMaxWaitSeconds).c_str());
		}
		else if (result == VK_ERROR_DEVICE_LOST)
		{
			HandleDeviceLost("vkWaitForFences returned VK_ERROR_DEVICE_LOST");
		}
		else if (result != VK_SUCCESS)
		{
			cemuLog_log(LogType::Force, "vkWaitForFences: Returned unhandled error {}", (sint32)result);
		}
	}
	ProcessFinishedCommandBuffers();
}

void VulkanRenderer::SubmitCommandBuffer(VkSemaphore signalSemaphore, VkSemaphore waitSemaphore)
{
	draw_endRenderPass();

	occlusionQuery_notifyEndCommandBuffer();

	vkEndCommandBuffer(m_state.currentCommandBuffer);

	std::array<VkSemaphore, 2> signalSemArray{};
	uint32 signalSemaphoreCount = 0;
	if (signalSemaphore != VK_NULL_HANDLE)
	{
		signalSemaphoreCount = 2;
		signalSemArray[0] = m_commandBufferSemaphores[m_commandBufferIndex];
		signalSemArray[1] = signalSemaphore;
	}
	else
	{
		signalSemaphoreCount = 1;
		signalSemArray[0] = m_commandBufferSemaphores[m_commandBufferIndex];
	}

	VkSemaphore prevSem = GetLastSubmittedCmdBufferSemaphore();
	std::array<VkSemaphore, 2> waitSemArray{};
	uint32 waitSemaphoreCount = 0;
	if (m_numSubmittedCmdBuffers > 0)
		waitSemArray[waitSemaphoreCount++] = prevSem;
	if (waitSemaphore != VK_NULL_HANDLE)
		waitSemArray[waitSemaphoreCount++] = waitSemaphore;

	const VkCommandBuffer submittedCommandBuffer = m_state.currentCommandBuffer;
	const VkFence submittedFence = m_cmd_buffer_fences[m_commandBufferIndex];
	QueueRenderWorkerJob([this, submittedCommandBuffer, submittedFence, signalSemArray, signalSemaphoreCount, waitSemArray, waitSemaphoreCount] {
		const VkPipelineStageFlags semWaitStageMask[2] = { VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT };
		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &submittedCommandBuffer;
		submitInfo.signalSemaphoreCount = signalSemaphoreCount;
		submitInfo.pSignalSemaphores = signalSemArray.data();
		submitInfo.waitSemaphoreCount = waitSemaphoreCount;
		submitInfo.pWaitDstStageMask = semWaitStageMask;
		submitInfo.pWaitSemaphores = waitSemArray.data();

		if (m_deviceLost.load(std::memory_order_acquire))
			return;
		const VkResult result = vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, submittedFence);
		if (result == VK_ERROR_DEVICE_LOST)
			HandleDeviceLost("vkQueueSubmit returned VK_ERROR_DEVICE_LOST");
		else if (result != VK_SUCCESS)
			UnrecoverableError(fmt::format("failed to submit command buffer. Error {}", result).c_str());
	});
	m_numSubmittedCmdBuffers++;

	ProcessFinishedCommandBuffers();

	auto nextCmdBufferIndex = (m_commandBufferIndex + 1) % m_commandBuffers.size();
	if (nextCmdBufferIndex == m_commandBufferSyncIndex)
	{
		cemuLog_logDebug(LogType::Force, "Vulkan: Waiting for available command buffer...");
		WaitForNextFinishedCommandBuffer();
	}
	m_cmdBufferUniformRingbufIndices[nextCmdBufferIndex] = m_cmdBufferUniformRingbufIndices[m_commandBufferIndex];
	m_commandBufferIndex = nextCmdBufferIndex;

	m_state.currentCommandBuffer = m_commandBuffers[m_commandBufferIndex];
	vkResetFences(m_logicalDevice, 1, &m_cmd_buffer_fences[m_commandBufferIndex]);
	vkResetCommandBuffer(m_state.currentCommandBuffer, 0);

	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(m_state.currentCommandBuffer, &beginInfo);

	vkCmdSetViewport(m_state.currentCommandBuffer, 0, 1, &m_state.currentViewport);
	vkCmdSetScissor(m_state.currentCommandBuffer, 0, 1, &m_state.currentScissorRect);

	m_state.resetCommandBufferState();

	occlusionQuery_notifyBeginCommandBuffer();

	m_recordedDrawcalls = 0;
	m_submitThreshold = 300;
	m_submitOnIdle = false;
}

void VulkanRenderer::RequestSubmitSoon()
{
	m_submitThreshold = std::min(m_submitThreshold, m_recordedDrawcalls + 10);
}

void VulkanRenderer::RequestSubmitOnIdle()
{
	m_submitOnIdle = true;
}

uint64 VulkanRenderer::GetCurrentCommandBufferId() const
{
	return m_numSubmittedCmdBuffers;
}

bool VulkanRenderer::HasCommandBufferFinished(uint64 commandBufferId) const
{
	return m_countCommandBufferFinished > commandBufferId;
}

void VulkanRenderer::WaitCommandBufferFinished(uint64 commandBufferId)
{
	if (commandBufferId == m_numSubmittedCmdBuffers)
		SubmitCommandBuffer();
	WaitRenderWorkerIdle();
	while (HasCommandBufferFinished(commandBufferId) == false)
	{
		if (m_deviceLost.load(std::memory_order_acquire) && m_commandBufferSyncIndex == m_commandBufferIndex)
			break;
		WaitForNextFinishedCommandBuffer();
	}
}

static fs::path GetDriverPipelineCacheFilename(const fs::path& dir, uint32 driverVersion)
{
	return dir / fmt::format(L"{:016x}_{:08x}.bin", CafeSystem::GetForegroundTitleId(), driverVersion);
}

void VulkanRenderer::PipelineCacheSaveThread(size_t cache_size)
{
	SetThreadName("vkDriverPlCache");
	const auto dir = ActiveSettings::GetCachePath("shaderCache/driver/vk");
	if (!fs::exists(dir))
	{
		try
		{
			fs::create_directories(dir);
		}
		catch (const std::exception& ex)
		{
			cemuLog_log(LogType::Force, "can't create vulkan pipeline cache directory \"{}\": {}", _pathToUtf8(dir), ex.what());
			return;
		}
	}

	VkPhysicalDeviceProperties deviceProps{};
	vkGetPhysicalDeviceProperties(m_physicalDevice, &deviceProps);
	const auto filename = GetDriverPipelineCacheFilename(dir, deviceProps.driverVersion);

	while (true)
	{
		if (m_destructionRequested)
			return;
		m_pipeline_cache_semaphore.wait();
		if (m_destructionRequested)
			return;
		for (sint32 i = 0; i < 15 * 4; i++)
		{
			if (m_destructionRequested)
				return;
			std::this_thread::sleep_for(std::chrono::milliseconds(250));
		}

		while (!m_pipeline_cache_save_mutex.try_lock())
			std::this_thread::sleep_for(std::chrono::milliseconds(250));

		size_t size = 0;
		VkResult res = vkGetPipelineCacheData(m_logicalDevice, m_pipeline_cache, &size, nullptr);
		if (res == VK_SUCCESS && size > 0 && size != cache_size)
		{
			std::vector<uint8_t> cacheData(size);
			res = vkGetPipelineCacheData(m_logicalDevice, m_pipeline_cache, &size, cacheData.data());
			m_pipeline_cache_semaphore.reset();
			m_pipeline_cache_save_mutex.unlock();

			if (res == VK_SUCCESS)
			{

				const auto finalPath = fs::resolvePathCI(filename);
				auto tempPath = finalPath;
				tempPath += L".tmp";
				bool written = false;
				{
					auto file = std::ofstream(tempPath, std::ios::out | std::ios::binary | std::ios::trunc);
					if (file.is_open())
					{
						file.write((char*)cacheData.data(), cacheData.size());
						file.close();
						written = !file.fail();
					}
				}
				std::error_code renameEc;
				if (written)
					fs::rename(tempPath, finalPath, renameEc);
				if (written && !renameEc)
				{
					cache_size = size;
					cemuLog_logDebug(LogType::Force, "pipeline cache saved");
				}
				else
				{
					std::error_code removeEc;
					fs::remove(tempPath, removeEc);
					cemuLog_log(LogType::Force, "can't write pipeline cache to disk");
				}
			}
			else
			{
				cemuLog_log(LogType::Force, "can't retrieve pipeline cache data: 0x{:x}", res);
			}
		}
		else
		{
			m_pipeline_cache_semaphore.reset();
			m_pipeline_cache_save_mutex.unlock();
		}
	}
}

void VulkanRenderer::CreatePipelineCache()
{
	std::vector<uint8_t> cacheData;
	const auto dir = ActiveSettings::GetCachePath("shaderCache/driver/vk");
	if (fs::exists(dir))
	{
		VkPhysicalDeviceProperties deviceProps{};
		vkGetPhysicalDeviceProperties(m_physicalDevice, &deviceProps);
		const auto filename = GetDriverPipelineCacheFilename(dir, deviceProps.driverVersion);
		auto file = std::ifstream(fs::resolvePathCI(filename), std::ios::in | std::ios::binary | std::ios::ate);
		if (file.is_open())
		{
			const size_t fileSize = file.tellg();
			file.seekg(0, std::ifstream::beg);
			cacheData.resize(fileSize);
			file.read((char*)cacheData.data(), cacheData.size());
			file.close();
		}
	}

	VkPipelineCacheCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	createInfo.initialDataSize = cacheData.size();
	createInfo.pInitialData = cacheData.data();
	VkResult result = vkCreatePipelineCache(m_logicalDevice, &createInfo, nullptr, &m_pipeline_cache);
	if (result != VK_SUCCESS)
	{
		cemuLog_log(LogType::Force, "Failed to open Vulkan pipeline cache: {}", result);
		createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
		createInfo.initialDataSize = 0;
		createInfo.pInitialData = nullptr;
		result = vkCreatePipelineCache(m_logicalDevice, &createInfo, nullptr, &m_pipeline_cache);
		if (result != VK_SUCCESS)
			UnrecoverableError(fmt::format("Failed to create new Vulkan pipeline cache: {}", result).c_str());
	}

	size_t cache_size = 0;
	vkGetPipelineCacheData(m_logicalDevice, m_pipeline_cache, &cache_size, nullptr);
	cemuLog_log(LogType::Force, "Vulkan: pipeline cache opened, {} KB read from disk, {} KB in use (async pipeline compile: {})", cacheData.size() / 1024, cache_size / 1024, m_featureControl.deviceExtensions.pipeline_creation_cache_control ? "yes" : "no");

	m_pipeline_cache_save_thread = std::thread(&VulkanRenderer::PipelineCacheSaveThread, this, cache_size);
}

void VulkanRenderer::swapchain_createDescriptorSetLayout()
{
	VkDescriptorSetLayoutBinding bindings[2]{};
	VkDescriptorSetLayoutBinding& samplerLayoutBinding = bindings[0];
	samplerLayoutBinding.binding = 0;
	samplerLayoutBinding.descriptorCount = 1;
	samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	samplerLayoutBinding.pImmutableSamplers = nullptr;
	samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutBinding& uniformBufferBinding = bindings[1];
	uniformBufferBinding.binding = 1;
	uniformBufferBinding.descriptorCount = 1;
	uniformBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	uniformBufferBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = std::size(bindings);
	layoutInfo.pBindings = bindings;

	if (vkCreateDescriptorSetLayout(m_logicalDevice, &layoutInfo, nullptr, &m_swapchainDescriptorSetLayout) != VK_SUCCESS)
		UnrecoverableError("failed to create descriptor set layout for swapchain");
}

void VulkanRenderer::GetTextureFormatInfoVK(Latte::E_GX2SURFFMT format, bool isDepth, Latte::E_DIM dim, sint32 width, sint32 height, FormatInfoVK* formatInfoOut)
{
	formatInfoOut->texelCountX = width;
	formatInfoOut->texelCountY = height;
	formatInfoOut->isCompressed = false;
	if (isDepth)
	{
		switch (format)
		{
		case Latte::E_GX2SURFFMT::D24_S8_UNORM:
			if (m_supportedFormatInfo.fmt_d24_unorm_s8_uint == false)
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;
				formatInfoOut->vkImageAspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
				formatInfoOut->decoder = TextureDecoder_NullData64::getInstance();
			}
			else
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_D24_UNORM_S8_UINT;
				formatInfoOut->vkImageAspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
				formatInfoOut->decoder = TextureDecoder_D24_S8::getInstance();
			}
			break;
		case Latte::E_GX2SURFFMT::D24_S8_FLOAT:
			formatInfoOut->vkImageFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;
			formatInfoOut->vkImageAspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
			formatInfoOut->decoder = TextureDecoder_NullData64::getInstance();
			break;
		case Latte::E_GX2SURFFMT::D32_FLOAT:
			formatInfoOut->vkImageFormat = VK_FORMAT_D32_SFLOAT;
			formatInfoOut->vkImageAspect = VK_IMAGE_ASPECT_DEPTH_BIT;
			formatInfoOut->decoder = TextureDecoder_R32_FLOAT::getInstance();
			break;
		case Latte::E_GX2SURFFMT::D16_UNORM:
			formatInfoOut->vkImageFormat = VK_FORMAT_D16_UNORM;
			formatInfoOut->vkImageAspect = VK_IMAGE_ASPECT_DEPTH_BIT;
			formatInfoOut->decoder = TextureDecoder_R16_UNORM::getInstance();
			break;
		case Latte::E_GX2SURFFMT::D32_S8_FLOAT:
			formatInfoOut->vkImageFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;
			formatInfoOut->vkImageAspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
			formatInfoOut->decoder = TextureDecoder_D32_S8_UINT_X24::getInstance();
			break;
		default:
			cemuLog_log(LogType::Force, "Unsupported depth texture format {:04x}", (uint32)format);
			formatInfoOut->vkImageFormat = VK_FORMAT_D16_UNORM;
			formatInfoOut->vkImageAspect = VK_IMAGE_ASPECT_DEPTH_BIT;
			formatInfoOut->decoder = nullptr;
			break;
		}
	}
	else
	{
		formatInfoOut->vkImageAspect = VK_IMAGE_ASPECT_COLOR_BIT;
		if(format == (Latte::E_GX2SURFFMT::R16_G16_B16_A16_FLOAT | Latte::E_GX2SURFFMT::FMT_BIT_SRGB))
			format = Latte::E_GX2SURFFMT::R16_G16_B16_A16_FLOAT;
		switch (format)
		{
		case Latte::E_GX2SURFFMT::R32_G32_B32_A32_FLOAT:
			formatInfoOut->vkImageFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
			formatInfoOut->decoder = TextureDecoder_R32_G32_B32_A32_FLOAT::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R32_G32_B32_A32_UINT:
			formatInfoOut->vkImageFormat = VK_FORMAT_R32G32B32A32_UINT;
			formatInfoOut->decoder = TextureDecoder_R32_G32_B32_A32_UINT::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R16_G16_B16_A16_FLOAT:
			formatInfoOut->vkImageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
			formatInfoOut->decoder = TextureDecoder_R16_G16_B16_A16_FLOAT::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R16_G16_B16_A16_UINT:
			formatInfoOut->vkImageFormat = VK_FORMAT_R16G16B16A16_UINT;
			formatInfoOut->decoder = TextureDecoder_R16_G16_B16_A16_UINT::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R16_G16_B16_A16_UNORM:
			formatInfoOut->vkImageFormat = VK_FORMAT_R16G16B16A16_UNORM;
			formatInfoOut->decoder = TextureDecoder_R16_G16_B16_A16::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R16_G16_B16_A16_SNORM:
			formatInfoOut->vkImageFormat = VK_FORMAT_R16G16B16A16_SNORM;
			formatInfoOut->decoder = TextureDecoder_R16_G16_B16_A16::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R8_G8_B8_A8_UNORM:
			formatInfoOut->vkImageFormat = VK_FORMAT_R8G8B8A8_UNORM;
			formatInfoOut->decoder = TextureDecoder_R8_G8_B8_A8::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R8_G8_B8_A8_SNORM:
			formatInfoOut->vkImageFormat = VK_FORMAT_R8G8B8A8_SNORM;
			formatInfoOut->decoder = TextureDecoder_R8_G8_B8_A8::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R8_G8_B8_A8_SRGB:
			formatInfoOut->vkImageFormat = VK_FORMAT_R8G8B8A8_SRGB;
			formatInfoOut->decoder = TextureDecoder_R8_G8_B8_A8::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R8_G8_B8_A8_UINT:
			formatInfoOut->vkImageFormat = VK_FORMAT_R8G8B8A8_UINT;
			formatInfoOut->decoder = TextureDecoder_R8_G8_B8_A8::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R8_G8_B8_A8_SINT:
			formatInfoOut->vkImageFormat = VK_FORMAT_R8G8B8A8_SINT;
			formatInfoOut->decoder = TextureDecoder_R8_G8_B8_A8::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R32_G32_FLOAT:
			formatInfoOut->vkImageFormat = VK_FORMAT_R32G32_SFLOAT;
			formatInfoOut->decoder = TextureDecoder_R32_G32_FLOAT::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R32_G32_UINT:
			formatInfoOut->vkImageFormat = VK_FORMAT_R32G32_UINT;
			formatInfoOut->decoder = TextureDecoder_R32_G32_UINT::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R16_G16_UNORM:
			formatInfoOut->vkImageFormat = VK_FORMAT_R16G16_UNORM;
			formatInfoOut->decoder = TextureDecoder_R16_G16::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R16_G16_FLOAT:
			formatInfoOut->vkImageFormat = VK_FORMAT_R16G16_SFLOAT;
			formatInfoOut->decoder = TextureDecoder_R16_G16_FLOAT::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R8_G8_UNORM:
			formatInfoOut->vkImageFormat = VK_FORMAT_R8G8_UNORM;
			formatInfoOut->decoder = TextureDecoder_R8_G8::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R8_G8_SNORM:
			formatInfoOut->vkImageFormat = VK_FORMAT_R8G8_SNORM;
			formatInfoOut->decoder = TextureDecoder_R8_G8::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R4_G4_UNORM:
			if (m_supportedFormatInfo.fmt_r4g4_unorm_pack == false)
			{
				if (m_supportedFormatInfo.fmt_r4g4b4a4_unorm_pack == false) {
					formatInfoOut->vkImageFormat = VK_FORMAT_R8G8B8A8_UNORM;
					formatInfoOut->decoder = TextureDecoder_R4G4_UNORM_To_RGBA8::getInstance();
				}
				else {
					formatInfoOut->vkImageFormat = VK_FORMAT_R4G4B4A4_UNORM_PACK16;
					formatInfoOut->decoder = TextureDecoder_R4_G4_UNORM_To_ABGR4::getInstance();
				}
			}
			else
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_R4G4_UNORM_PACK8;
				formatInfoOut->decoder = TextureDecoder_R4_G4::getInstance();
			}
			break;
		case Latte::E_GX2SURFFMT::R32_FLOAT:
			formatInfoOut->vkImageFormat = VK_FORMAT_R32_SFLOAT;
			formatInfoOut->decoder = TextureDecoder_R32_FLOAT::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R32_UINT:
			formatInfoOut->vkImageFormat = VK_FORMAT_R32_UINT;
			formatInfoOut->decoder = TextureDecoder_R32_UINT::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R16_FLOAT:
			formatInfoOut->vkImageFormat = VK_FORMAT_R16_SFLOAT;
			formatInfoOut->decoder = TextureDecoder_R16_FLOAT::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R16_UNORM:
			formatInfoOut->vkImageFormat = VK_FORMAT_R16_UNORM;
			formatInfoOut->decoder = TextureDecoder_R16_UNORM::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R16_SNORM:
			formatInfoOut->vkImageFormat = VK_FORMAT_R16_SNORM;
			formatInfoOut->decoder = TextureDecoder_R16_SNORM::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R16_UINT:
			formatInfoOut->vkImageFormat = VK_FORMAT_R16_UINT;
			formatInfoOut->decoder = TextureDecoder_R16_UINT::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R8_UNORM:
			formatInfoOut->vkImageFormat = VK_FORMAT_R8_UNORM;
			formatInfoOut->decoder = TextureDecoder_R8::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R8_SNORM:
			formatInfoOut->vkImageFormat = VK_FORMAT_R8_SNORM;
			formatInfoOut->decoder = TextureDecoder_R8::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R8_UINT:
			formatInfoOut->vkImageFormat = VK_FORMAT_R8_UINT;
			formatInfoOut->decoder = TextureDecoder_R8_UINT::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R5_G6_B5_UNORM:
			if (m_supportedFormatInfo.fmt_r5g6b5_unorm_pack == false) {
				formatInfoOut->vkImageFormat = VK_FORMAT_R8G8B8A8_UNORM;
				formatInfoOut->decoder = TextureDecoder_R5G6B5_UNORM_To_RGBA8::getInstance();
			}
			else {
				formatInfoOut->vkImageFormat = VK_FORMAT_R5G6B5_UNORM_PACK16;
				formatInfoOut->decoder = TextureDecoder_R5_G6_B5_swappedRB::getInstance();
			}
			break;
		case Latte::E_GX2SURFFMT::R5_G5_B5_A1_UNORM:
			if (m_supportedFormatInfo.fmt_a1r5g5b5_unorm_pack == false) {
				formatInfoOut->vkImageFormat = VK_FORMAT_R8G8B8A8_UNORM;
				formatInfoOut->decoder = TextureDecoder_R5_G5_B5_A1_UNORM_swappedRB_To_RGBA8::getInstance();
			}
			else {
				formatInfoOut->vkImageFormat = VK_FORMAT_A1R5G5B5_UNORM_PACK16;
				formatInfoOut->decoder = TextureDecoder_R5_G5_B5_A1_UNORM_swappedRB::getInstance();
			}
			break;
		case Latte::E_GX2SURFFMT::A1_B5_G5_R5_UNORM:
			if (m_supportedFormatInfo.fmt_a1r5g5b5_unorm_pack == false) {
				formatInfoOut->vkImageFormat = VK_FORMAT_R8G8B8A8_UNORM;
				formatInfoOut->decoder = TextureDecoder_A1_B5_G5_R5_UNORM_vulkan_To_RGBA8::getInstance();
			}
			else {
				formatInfoOut->vkImageFormat = VK_FORMAT_A1R5G5B5_UNORM_PACK16;
				formatInfoOut->decoder = TextureDecoder_A1_B5_G5_R5_UNORM_vulkan::getInstance();
			}
			break;
		case Latte::E_GX2SURFFMT::R11_G11_B10_FLOAT:
			formatInfoOut->vkImageFormat = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
			formatInfoOut->decoder = TextureDecoder_R11_G11_B10_FLOAT::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R4_G4_B4_A4_UNORM:
			if (m_supportedFormatInfo.fmt_r4g4b4a4_unorm_pack == false) {
				formatInfoOut->vkImageFormat = VK_FORMAT_R8G8B8A8_UNORM;
				formatInfoOut->decoder = TextureDecoder_R4G4B4A4_UNORM_To_RGBA8::getInstance();
			}
			else {
				formatInfoOut->vkImageFormat = VK_FORMAT_R4G4B4A4_UNORM_PACK16;
 				formatInfoOut->decoder = TextureDecoder_R4_G4_B4_A4_UNORM::getInstance();
			}
			break;
		case Latte::E_GX2SURFFMT::R10_G10_B10_A2_UNORM:
			formatInfoOut->vkImageFormat = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
			formatInfoOut->decoder = TextureDecoder_R10_G10_B10_A2_UNORM::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R10_G10_B10_A2_SNORM:
			formatInfoOut->vkImageFormat = VK_FORMAT_R16G16B16A16_SNORM;
			formatInfoOut->decoder = TextureDecoder_R10_G10_B10_A2_SNORM_To_RGBA16::getInstance();
			break;
		case Latte::E_GX2SURFFMT::R10_G10_B10_A2_SRGB:
			formatInfoOut->vkImageFormat = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
			formatInfoOut->decoder = TextureDecoder_R10_G10_B10_A2_UNORM::getInstance();
			break;
		case Latte::E_GX2SURFFMT::BC1_SRGB:
			if (m_supportedFormatInfo.fmt_bc)
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
				formatInfoOut->decoder = TextureDecoder_BC1::getInstance();
			}
			else if (m_supportedFormatInfo.fmt_astc)
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_ASTC_4x4_SRGB_BLOCK;
				formatInfoOut->decoder = TextureDecoder_BC1_SRGB_to_ASTC::getInstance();
			}
			else
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_R8G8B8A8_SRGB;
				formatInfoOut->decoder = TextureDecoder_BC1_RGBA8::getInstance();
			}
			break;
		case Latte::E_GX2SURFFMT::BC1_UNORM:
			if (m_supportedFormatInfo.fmt_bc)
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
				formatInfoOut->decoder = TextureDecoder_BC1::getInstance();
			}
			else if (m_supportedFormatInfo.fmt_astc)
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
				formatInfoOut->decoder = TextureDecoder_BC1_UNORM_to_ASTC::getInstance();
			}
			else
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_R8G8B8A8_UNORM;
				formatInfoOut->decoder = TextureDecoder_BC1_RGBA8::getInstance();
			}
			break;
		case Latte::E_GX2SURFFMT::BC2_UNORM:
			if (m_supportedFormatInfo.fmt_bc)
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_BC2_UNORM_BLOCK;
				formatInfoOut->decoder = TextureDecoder_BC2::getInstance();
			}
			else if (m_supportedFormatInfo.fmt_astc)
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
				formatInfoOut->decoder = TextureDecoder_BC2_UNORM_to_ASTC::getInstance();
			}
			else
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_R8G8B8A8_UNORM;
				formatInfoOut->decoder = TextureDecoder_BC2_RGBA8::getInstance();
			}
			break;
		case Latte::E_GX2SURFFMT::BC2_SRGB:
			if (m_supportedFormatInfo.fmt_bc)
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_BC2_SRGB_BLOCK;
				formatInfoOut->decoder = TextureDecoder_BC2::getInstance();
			}
			else if (m_supportedFormatInfo.fmt_astc)
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_ASTC_4x4_SRGB_BLOCK;
				formatInfoOut->decoder = TextureDecoder_BC2_SRGB_to_ASTC::getInstance();
			}
			else
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_R8G8B8A8_SRGB;
				formatInfoOut->decoder = TextureDecoder_BC2_RGBA8::getInstance();
			}
			break;
		case Latte::E_GX2SURFFMT::BC3_UNORM:
			if (m_supportedFormatInfo.fmt_bc)
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_BC3_UNORM_BLOCK;
				formatInfoOut->decoder = TextureDecoder_BC3::getInstance();
			}
			else if (m_supportedFormatInfo.fmt_astc)
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
				formatInfoOut->decoder = TextureDecoder_BC3_UNORM_to_ASTC::getInstance();
			}
			else
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_R8G8B8A8_UNORM;
				formatInfoOut->decoder = TextureDecoder_BC3_RGBA8::getInstance();
			}
			break;
		case Latte::E_GX2SURFFMT::BC3_SRGB:
			if (m_supportedFormatInfo.fmt_bc)
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_BC3_SRGB_BLOCK;
				formatInfoOut->decoder = TextureDecoder_BC3::getInstance();
			}
			else if (m_supportedFormatInfo.fmt_astc)
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_ASTC_4x4_SRGB_BLOCK;
				formatInfoOut->decoder = TextureDecoder_BC3_SRGB_to_ASTC::getInstance();
			}
			else
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_R8G8B8A8_SRGB;
				formatInfoOut->decoder = TextureDecoder_BC3_RGBA8::getInstance();
			}
			break;
		case Latte::E_GX2SURFFMT::BC4_UNORM:
			if (m_supportedFormatInfo.fmt_bc)
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_BC4_UNORM_BLOCK;
				formatInfoOut->decoder = TextureDecoder_BC4::getInstance();
			}
			else if (m_supportedFormatInfo.fmt_astc)
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
				formatInfoOut->decoder = TextureDecoder_BC4_UNORM_to_ASTC::getInstance();
			}
			else
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_R8_UNORM;
				formatInfoOut->decoder = TextureDecoder_BC4_UNORM_To_R8::getInstance();
			}
			break;
		case Latte::E_GX2SURFFMT::BC4_SNORM:
			if (m_supportedFormatInfo.fmt_bc)
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_BC4_SNORM_BLOCK;
				formatInfoOut->decoder = TextureDecoder_BC4::getInstance();
			}
			else if (m_supportedFormatInfo.fmt_astc)
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
				formatInfoOut->decoder = TextureDecoder_BC4_SNORM_to_ASTC::getInstance();
			}
			else
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_R8_SNORM;
				formatInfoOut->decoder = TextureDecoder_BC4_SNORM_To_R8::getInstance();
			}
			break;
		case Latte::E_GX2SURFFMT::BC5_UNORM:
			if (m_supportedFormatInfo.fmt_bc)
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_BC5_UNORM_BLOCK;
				formatInfoOut->decoder = TextureDecoder_BC5::getInstance();
			}
			else if (m_supportedFormatInfo.fmt_astc)
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
				formatInfoOut->decoder = TextureDecoder_BC5_UNORM_to_ASTC::getInstance();
			}
			else
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_R8G8_UNORM;
				formatInfoOut->decoder = TextureDecoder_BC5_UNORM_To_RG8::getInstance();
			}
			break;
		case Latte::E_GX2SURFFMT::BC5_SNORM:
			if (m_supportedFormatInfo.fmt_bc)
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_BC5_SNORM_BLOCK;
				formatInfoOut->decoder = TextureDecoder_BC5::getInstance();
			}
			else if (m_supportedFormatInfo.fmt_astc)
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
				formatInfoOut->decoder = TextureDecoder_BC5_SNORM_to_ASTC::getInstance();
			}
			else
			{
				formatInfoOut->vkImageFormat = VK_FORMAT_R8G8_SNORM;
				formatInfoOut->decoder = TextureDecoder_BC5_SNORM_To_RG8::getInstance();
			}
			break;
		case Latte::E_GX2SURFFMT::R24_X8_UNORM:
			formatInfoOut->vkImageFormat = VK_FORMAT_R32_SFLOAT;
			formatInfoOut->decoder = TextureDecoder_R24_X8::getInstance();
			break;
		case Latte::E_GX2SURFFMT::X24_G8_UINT:
			formatInfoOut->vkImageFormat = VK_FORMAT_R8G8B8A8_UINT;
			formatInfoOut->decoder = TextureDecoder_X24_G8_UINT::getInstance();
		case Latte::E_GX2SURFFMT::R32_X8_FLOAT:
			formatInfoOut->vkImageFormat = VK_FORMAT_R32_SFLOAT;
			formatInfoOut->decoder = TextureDecoder_NullData64::getInstance();
			break;
		default:
			cemuLog_log(LogType::Force, "Unsupported color texture format {:04x}", (uint32)format);
			cemu_assert_debug(false);
		}
	}
}

VkPipelineShaderStageCreateInfo VulkanRenderer::CreatePipelineShaderStageCreateInfo(VkShaderStageFlagBits stage, VkShaderModule& module, const char* entryName) const
{
	VkPipelineShaderStageCreateInfo shaderStageInfo{};
	shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStageInfo.stage = stage;
	shaderStageInfo.module = module;
	shaderStageInfo.pName = entryName;
	return shaderStageInfo;
}

VkPipeline VulkanRenderer::backbufferBlit_createGraphicsPipeline(VkDescriptorSetLayout descriptorLayout, bool padView, RendererOutputShader* shader)
{
	auto& chainInfo = GetChainInfo(!padView);

	RendererShaderVk* vertexRendererShader = static_cast<RendererShaderVk*>(shader->GetVertexShader());
	RendererShaderVk* fragmentRendererShader = static_cast<RendererShaderVk*>(shader->GetFragmentShader());

	uint64 hash = 0;
	hash += (uint64)vertexRendererShader;
	hash += (uint64)fragmentRendererShader;
	hash += ((uint64)padView) << 1;

	const auto it = m_backbufferBlitPipelineCache.find(hash);
	if (it != m_backbufferBlitPipelineCache.cend())
		return it->second;

	std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
	if (vertexRendererShader)
		shaderStages.emplace_back(CreatePipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, vertexRendererShader->GetShaderModule(), "main"));

	if (fragmentRendererShader)
		shaderStages.emplace_back(CreatePipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, fragmentRendererShader->GetShaderModule(), "main"));

	VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
	vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputInfo.vertexBindingDescriptionCount = 0;
	vertexInputInfo.vertexAttributeDescriptionCount = 0;

	VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	inputAssembly.primitiveRestartEnable = VK_FALSE;

	VkPipelineViewportStateCreateInfo viewportState{};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

	VkPipelineDynamicStateCreateInfo dynamicState = {};
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount = std::size(dynamicStates);
	dynamicState.pDynamicStates = dynamicStates;

	VkPipelineRasterizationStateCreateInfo rasterizer{};
	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.depthClampEnable = VK_FALSE;
	rasterizer.rasterizerDiscardEnable = VK_FALSE;
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.lineWidth = 1.0f;
	rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
	rasterizer.depthBiasEnable = VK_FALSE;

	VkPipelineMultisampleStateCreateInfo multisampling{};
	multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.sampleShadingEnable = VK_FALSE;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState colorBlendAttachment{};
	colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	colorBlendAttachment.blendEnable = VK_FALSE;

	VkPipelineColorBlendStateCreateInfo colorBlending{};
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.logicOpEnable = VK_FALSE;
	colorBlending.logicOp = VK_LOGIC_OP_COPY;
	colorBlending.attachmentCount = 1;
	colorBlending.pAttachments = &colorBlendAttachment;
	colorBlending.blendConstants[0] = 0.0f;
	colorBlending.blendConstants[1] = 0.0f;
	colorBlending.blendConstants[2] = 0.0f;
	colorBlending.blendConstants[3] = 0.0f;

	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = &descriptorLayout;

	VkResult result;
	if (m_pipelineLayout == VK_NULL_HANDLE)
	{
		result = vkCreatePipelineLayout(m_logicalDevice, &pipelineLayoutInfo, nullptr, &m_pipelineLayout);
		if (result != VK_SUCCESS)
			throw std::runtime_error(fmt::format("Failed to create pipeline layout: {}", result));
	}

	VkGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.stageCount = shaderStages.size();
	pipelineInfo.pStages = shaderStages.data();
	pipelineInfo.pVertexInputState = &vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pDynamicState = &dynamicState;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisampling;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.layout = m_pipelineLayout;
	pipelineInfo.renderPass = chainInfo.m_swapchainRenderPass;
	pipelineInfo.subpass = 0;
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

	VkPipeline pipeline = nullptr;
	std::shared_lock lock(m_pipeline_cache_save_mutex);
	result = vkCreateGraphicsPipelines(m_logicalDevice, m_pipeline_cache, 1, &pipelineInfo, nullptr, &pipeline);
	if (result != VK_SUCCESS)
	{
		cemuLog_log(LogType::Force, "Failed to create graphics pipeline. Error {}", result);
		throw std::runtime_error(fmt::format("Failed to create graphics pipeline: {}", result));
	}

	m_backbufferBlitPipelineCache[hash] = pipeline;
	m_pipeline_cache_semaphore.notify();

	return pipeline;
}

bool VulkanRenderer::AcquireNextSwapchainImage(bool mainWindow)
{
	if(!IsSwapchainInfoValid(mainWindow))
		return false;

	if(!mainWindow && m_destroyPadSwapchainNextAcquire.test())
	{
		RecreateSwapchain(mainWindow, true);
		m_padSwapchainInfo.reset();
		m_destroyPadSwapchainNextAcquire.clear();
		m_destroyPadSwapchainNextAcquire.notify_all();
		return false;
	}

	auto& chainInfo = GetChainInfo(mainWindow);
	auto& presentPending = m_swapchainPresentPending[mainWindow ? 0 : 1];
	if (presentPending.load(std::memory_order_acquire))
	{
		WaitRenderWorkerIdle();
		presentPending.store(false, std::memory_order_release);
	}

	if (chainInfo.swapchainImageIndex != -1)
		return true;

#if BOOST_OS_IOS
	const auto outputBit = mainWindow ? 1u : 2u;
	if (!(WindowSystem::GetWindowInfo().visible_outputs.load() & outputBit))
		return false;
#endif

	if (!UpdateSwapchainProperties(mainWindow))
		return false;

	bool result = chainInfo.AcquireImage();
	if (!result)
		return false;

	SubmitCommandBuffer(VK_NULL_HANDLE, chainInfo.ConsumeAcquireSemaphore());
	return true;
}

void VulkanRenderer::RecreateSwapchain(bool mainWindow, bool skipCreate)
{
	SubmitCommandBuffer();
	WaitDeviceIdle();
	auto& chainInfo = GetChainInfo(mainWindow);

	Vector2i size;
	if (mainWindow)
	{
		if (m_imguiBackendInitialized)
		{
			ImGui_ImplVulkan_Shutdown();
			m_imguiBackendInitialized = false;
		}
		WindowSystem::GetWindowPhysSize(size.x, size.y);
	}
	else
	{
		WindowSystem::GetPadWindowPhysSize(size.x, size.y);
	}

	chainInfo.swapchainImageIndex = -1;
	chainInfo.Cleanup(!skipCreate);
	chainInfo.m_desiredExtent = size;
	if(!skipCreate)
	{
		chainInfo.Create();
	}

	if (mainWindow)
		ImguiInit();
}

void VulkanRenderer::NoteSwapchainFailure(bool mainWindow, const char* what)
{
	auto& retry = m_swapchainRetry[mainWindow ? 0 : 1];
	retry.delayMs = retry.delayMs == 0 ? 100 : std::min<uint32>(retry.delayMs * 2, 2000);
	retry.notBefore = std::chrono::steady_clock::now() + std::chrono::milliseconds(retry.delayMs);
	cemuLog_log(LogType::Force, "Vulkan: {} swapchain: {}. Trying again in {} ms", mainWindow ? "TV" : "GamePad", what, retry.delayMs);
}

void VulkanRenderer::RetryDeadSwapchain(bool mainWindow)
{
	auto& chain = GetChainInfoPtr(mainWindow);
	if (!chain || chain->IsValid())
		return;
	auto& retry = m_swapchainRetry[mainWindow ? 0 : 1];
	if (std::chrono::steady_clock::now() < retry.notBefore)
		return;
	try
	{
		RecreateSwapchain(mainWindow);
		chain->m_shouldRecreate = false;
		chain->m_vsyncState = (SwapchainInfoVk::VSync)GetConfig().vsync.GetValue();
		retry.delayMs = 0;
		cemuLog_log(LogType::Force, "Vulkan: {} swapchain recovered", mainWindow ? "TV" : "GamePad");
	}
	catch (const std::exception& ex)
	{
		NoteSwapchainFailure(mainWindow, ex.what());
	}
}

void VulkanRenderer::ApplyPendingPadSurface()
{
	if (!m_padSurfaceChanged.load(std::memory_order_acquire))
		return;
	auto& retry = m_swapchainRetry[1];
	if (std::chrono::steady_clock::now() < retry.notBefore)
		return;
	m_padSurfaceChanged.store(false, std::memory_order_relaxed);
	const Vector2i size{m_padSurfaceWidth.load(std::memory_order_relaxed), m_padSurfaceHeight.load(std::memory_order_relaxed)};
	try
	{
		SubmitCommandBuffer();
		WaitDeviceIdle();
		m_padSwapchainInfo.reset();
		m_swapchainPresentPending[1].store(false, std::memory_order_release);
		auto chain = std::make_unique<SwapchainInfoVk>(false, size);
		chain->Create();
		m_padSwapchainInfo = std::move(chain);
		retry.delayMs = 0;
	}
	catch (const std::exception& ex)
	{
		m_padSurfaceChanged.store(true, std::memory_order_release);
		NoteSwapchainFailure(false, ex.what());
	}
}

bool VulkanRenderer::UpdateSwapchainProperties(bool mainWindow)
{
	auto& chainInfo = GetChainInfo(mainWindow);
	bool stateChanged = chainInfo.m_shouldRecreate;

	const auto configValue =  (VSync)GetConfig().vsync.GetValue();
	if(chainInfo.m_vsyncState != configValue)
		stateChanged = true;

	int width, height;
	if (mainWindow)
		WindowSystem::GetWindowPhysSize(width, height);
	else
		WindowSystem::GetPadWindowPhysSize(width, height);
#if BOOST_OS_IOS || BOOST_OS_MACOS
	{
		VkSurfaceCapabilitiesKHR caps{};
		if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, chainInfo.m_surface, &caps) == VK_SUCCESS &&
			caps.currentExtent.width != std::numeric_limits<uint32>::max() && caps.currentExtent.width > 0 && caps.currentExtent.height > 0)
		{
			width = (int)caps.currentExtent.width;
			height = (int)caps.currentExtent.height;
		}
	}
#endif
	if (width <= 0 || height <= 0)
	{
		static std::atomic<uint32> s_zeroExtentFrames{0};
		const uint32 n = s_zeroExtentFrames.fetch_add(1) + 1;
		if (n == 1 || n % 600 == 0)
			cemuLog_log(LogType::Force, "Vulkan: {} surface is {}x{}, not presenting until it has a size (frames skipped so far: {})", mainWindow ? "TV" : "GamePad", width, height, n);
		return false;
	}
	auto extent = chainInfo.getExtent();
	if (width != extent.width || height != extent.height)
	{
		cemuLog_log(LogType::Force, "Vulkan: {} swapchain {}x{} -> {}x{}", mainWindow ? "TV" : "GamePad", extent.width, extent.height, width, height);
		stateChanged = true;
	}

	if(stateChanged)
	{
		try
		{
			RecreateSwapchain(mainWindow);
		}
		catch (const std::exception& ex)
		{
			NoteSwapchainFailure(mainWindow, ex.what());
			return false;
		}
	}

	chainInfo.m_shouldRecreate = false;
	chainInfo.m_vsyncState = configValue;
	return true;
}

void VulkanRenderer::SwapBuffer(bool mainWindow)
{
	if(!AcquireNextSwapchainImage(mainWindow))
		return;

	auto& chainInfo = GetChainInfo(mainWindow);

	if (!chainInfo.hasDefinedSwapchainImage)
	{
		VkClearColorValue clearColor{ 0, 0, 0, 0 };
		ClearColorImageRaw(chainInfo.m_swapchainImages[chainInfo.swapchainImageIndex], 0, 0, clearColor, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
	}

	const size_t currentFrameCmdBufferID = GetCurrentCommandBufferId();

	VkSemaphore presentSemaphore = chainInfo.m_presentSemaphores[chainInfo.swapchainImageIndex];
	SubmitCommandBuffer(presentSemaphore);

	cemu_assert_debug(m_numSubmittedCmdBuffers > 0);

	WaitCommandBufferFinished(m_commandBufferIDOfPrevFrame);
	m_commandBufferIDOfPrevFrame = currentFrameCmdBufferID;

	chainInfo.WaitAvailableFence();

	auto& presentPending = m_swapchainPresentPending[mainWindow ? 0 : 1];
	presentPending.store(true, std::memory_order_release);
    QueueRenderWorkerJob([this, &chainInfo, &presentPending, presentSemaphore] {
        if (m_deviceLost.load(std::memory_order_acquire))
        {
            chainInfo.hasDefinedSwapchainImage = false;
            chainInfo.swapchainImageIndex = -1;
            presentPending.store(false, std::memory_order_release);
            return;
        }
        VkPresentIdKHR presentId = {};

        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &chainInfo.m_swapchain;
        presentInfo.pImageIndices = &chainInfo.swapchainImageIndex;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &presentSemaphore;

        if (m_featureControl.deviceExtensions.present_wait && chainInfo.m_maxQueued > 0)
        {
            presentId.sType = VK_STRUCTURE_TYPE_PRESENT_ID_KHR;
            presentId.swapchainCount = 1;
            presentId.pPresentIds = &chainInfo.m_presentId;

            presentInfo.pNext = &presentId;

            if(chainInfo.m_queueDepth >= chainInfo.m_maxQueued)
            {
                uint64 waitFrameId = chainInfo.m_presentId - chainInfo.m_queueDepth;
                vkWaitForPresentKHR(m_logicalDevice, chainInfo.m_swapchain, waitFrameId, 40'000'000);
                chainInfo.m_queueDepth--;
            }
        }

        VkResult result = vkQueuePresentKHR(m_presentQueue, &presentInfo);
        if(result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
            chainInfo.m_shouldRecreate = true;

        if(result >= 0)
        {
            chainInfo.m_queueDepth++;
            chainInfo.m_presentId++;
            LatteWait::Get().presentedFrames.fetch_add(1, std::memory_order_relaxed);
            (chainInfo.mainWindow ? PerfTelemetry::Get().tvPresents : PerfTelemetry::Get().padPresents).fetch_add(1, std::memory_order_relaxed);
        }

        chainInfo.hasDefinedSwapchainImage = false;
        chainInfo.swapchainImageIndex = -1;
        presentPending.store(false, std::memory_order_release);

        if (result == VK_ERROR_DEVICE_LOST)
            HandleDeviceLost("vkQueuePresentKHR returned VK_ERROR_DEVICE_LOST");
        else if (result == VK_ERROR_SURFACE_LOST_KHR)
        {
            cemuLog_log(LogType::Force, "Vulkan: present reported the surface lost, the swapchain will be recreated");
            chainInfo.m_shouldRecreate = true;
        }
        else if (result < 0 && result != VK_ERROR_OUT_OF_DATE_KHR)
            HandleDeviceLost(fmt::format("Failed to present image: {}", (sint32)result).c_str());
    });
}

void VulkanRenderer::Flush(bool waitIdle)
{
	if (m_recordedDrawcalls > 0 || m_submitOnIdle)
		SubmitCommandBuffer();
	WaitRenderWorkerIdle();
	if (waitIdle)
		WaitCommandBufferFinished(GetCurrentCommandBufferId());
}

void VulkanRenderer::NotifyLatteCommandProcessorIdle()
{
	if (m_submitOnIdle)
		SubmitCommandBuffer();
}

void VulkanBenchmarkPrintResults();

void VulkanRenderer::SwapBuffers(bool swapTV, bool swapDRC)
{
	if (m_deviceLost.load(std::memory_order_acquire))
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(8));
		return;
	}
	try
	{
		SubmitCommandBuffer();

		ApplyPendingPadSurface();
		RetryDeadSwapchain(true);
		RetryDeadSwapchain(false);

		if (swapTV && IsSwapchainInfoValid(true))
			SwapBuffer(true);

		if (swapDRC && IsSwapchainInfoValid(false))
			SwapBuffer(false);

		if(swapTV)
			VulkanBenchmarkPrintResults();
	}
	catch (const std::exception& ex)
	{
		HandleDeviceLost(fmt::format("exception while presenting: {}", ex.what()).c_str());
	}
}

void VulkanRenderer::ClearColorbuffer(bool padView)
{
	if (!IsSwapchainInfoValid(!padView))
		return;

	auto& chainInfo = GetChainInfo(!padView);
	if (chainInfo.swapchainImageIndex == -1)
		return;

	VkClearColorValue clearColor{ 0, 0, 0, 0 };
	ClearColorImageRaw(chainInfo.m_swapchainImages[chainInfo.swapchainImageIndex], 0, 0, clearColor, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
}

void VulkanRenderer::ClearColorImageRaw(VkImage image, uint32 sliceIndex, uint32 mipIndex, const VkClearColorValue& color, VkImageLayout inputLayout, VkImageLayout outputLayout)
{
	draw_endRenderPass();

	VkImageSubresourceRange subresourceRange{};
	subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	subresourceRange.baseMipLevel = mipIndex;
	subresourceRange.levelCount = 1;
	subresourceRange.baseArrayLayer = sliceIndex;
	subresourceRange.layerCount = 1;

	barrier_image<SYNC_OP::ANY_TRANSFER | SYNC_OP::IMAGE_READ | SYNC_OP::IMAGE_WRITE, SYNC_OP::ANY_TRANSFER>(image, subresourceRange, inputLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	vkCmdClearColorImage(m_state.currentCommandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &subresourceRange);

    barrier_image<ANY_TRANSFER, SYNC_OP::ANY_TRANSFER | SYNC_OP::IMAGE_READ | SYNC_OP::IMAGE_WRITE>(image, subresourceRange, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, outputLayout);
}

void VulkanRenderer::ClearColorImage(LatteTextureVk* vkTexture, uint32 sliceIndex, uint32 mipIndex, const VkClearColorValue& color, VkImageLayout outputLayout)
{
	if(vkTexture->isDepth)
	{
		cemu_assert_suspicious();
		return;
	}
	if (vkTexture->IsCompressedFormat())
	{
		auto imageObj = vkTexture->GetImageObj();
		imageObj->flagForCurrentCommandBuffer();
		VkImageSubresourceLayers subresourceRange{};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.mipLevel = mipIndex;
		subresourceRange.baseArrayLayer = sliceIndex;
		subresourceRange.layerCount = 1;
		barrier_image<ANY_TRANSFER | IMAGE_READ, ANY_TRANSFER | IMAGE_READ | IMAGE_WRITE>(vkTexture, subresourceRange, outputLayout);
		if(color.float32[0] == 0.0f && color.float32[1] == 0.0f && color.float32[2] == 0.0f && color.float32[3] == 0.0f)
		{
			static bool dbgMsgPrinted = false;
			if(!dbgMsgPrinted)
			{
				cemuLog_logDebug(LogType::Force, "Unsupported compressed texture clear to zero");
				dbgMsgPrinted = true;
			}
		}
		return;
	}

	VkImageSubresourceRange subresourceRange;

	subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	subresourceRange.baseMipLevel = mipIndex;
	subresourceRange.levelCount = 1;
	subresourceRange.baseArrayLayer = sliceIndex;
	subresourceRange.layerCount = 1;

	auto imageObj = vkTexture->GetImageObj();
	imageObj->flagForCurrentCommandBuffer();

	VkImageLayout inputLayout = vkTexture->GetImageLayout(subresourceRange);
	ClearColorImageRaw(imageObj->m_image, sliceIndex, mipIndex, color, inputLayout, outputLayout);
	vkTexture->SetImageLayout(subresourceRange, outputLayout);
}

void VulkanRenderer::DrawBackbufferQuad(LatteTextureView* texView, RendererOutputShader* shader, bool useLinearTexFilter, sint32 imageX, sint32 imageY, sint32 imageWidth, sint32 imageHeight, bool padView, bool clearBackground)
{
	if(!AcquireNextSwapchainImage(!padView))
		return;

	auto& chainInfo = GetChainInfo(!padView);
	LatteTextureViewVk* texViewVk = (LatteTextureViewVk*)texView;
	draw_endRenderPass();

	VkMemoryBarrier memoryBarrier{};
	memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
	VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	memoryBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
	memoryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
	vkCmdPipelineBarrier(m_state.currentCommandBuffer, srcStage, dstStage, 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr);

	auto pipeline = backbufferBlit_createGraphicsPipeline(m_swapchainDescriptorSetLayout, padView, shader);

	VkRenderPassBeginInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassInfo.renderPass = chainInfo.m_swapchainRenderPass;
	renderPassInfo.framebuffer = chainInfo.m_swapchainFramebuffers[chainInfo.swapchainImageIndex];
	renderPassInfo.renderArea.offset = { 0, 0 };
	renderPassInfo.renderArea.extent = chainInfo.getExtent();
	renderPassInfo.clearValueCount = 0;

	VkViewport viewport{};
	viewport.x = imageX;
	viewport.y = imageY;
	viewport.width = imageWidth;
	viewport.height = imageHeight;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(m_state.currentCommandBuffer, 0, 1, &viewport);

	VkRect2D scissor{};
	scissor.extent = chainInfo.getExtent();
	vkCmdSetScissor(m_state.currentCommandBuffer, 0, 1, &scissor);

	auto descriptSet = backbufferBlit_createDescriptorSet(m_swapchainDescriptorSetLayout, texViewVk, useLinearTexFilter);

	vkCmdBeginRenderPass(m_state.currentCommandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

	if (clearBackground)
	{
		VkClearAttachment clearAttachment{};
		clearAttachment.clearValue = {0,0,0,0};
		clearAttachment.colorAttachment = 0;
		clearAttachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		VkClearRect clearExtent = {{{0,0},chainInfo.m_actualExtent}, 0, 1};
		vkCmdClearAttachments(m_state.currentCommandBuffer, 1, &clearAttachment, 1, &clearExtent);
	}

	vkCmdBindPipeline(m_state.currentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	m_state.currentPipeline = pipeline;

	auto outputUniforms = shader->FillUniformBlockBuffer(*texView, {imageWidth, imageHeight}, padView);

	auto outputUniformOffset = uniformData_uploadUniformDataBufferGetOffset({(uint8*)&outputUniforms, sizeof(decltype(outputUniforms))});

	vkCmdBindDescriptorSets(m_state.currentCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &descriptSet,
		1, &outputUniformOffset);

	vkCmdDraw(m_state.currentCommandBuffer, 6, 1, 0, 0);

	vkCmdEndRenderPass(m_state.currentCommandBuffer);

	vkCmdSetViewport(m_state.currentCommandBuffer, 0, 1, &m_state.currentViewport);

	chainInfo.hasDefinedSwapchainImage = true;
}

void VulkanRenderer::CreateDescriptorPool()
{
	std::array<VkDescriptorPoolSize, 4> poolSizes = {};
	poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSizes[0].descriptorCount = 1024 * 128;
	poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	poolSizes[1].descriptorCount = 1024 * 1;
	poolSizes[2].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	poolSizes[2].descriptorCount = 1024 * 128;
	poolSizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	poolSizes[3].descriptorCount = 1024 * 4;

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = poolSizes.size();
	poolInfo.pPoolSizes = poolSizes.data();
	poolInfo.maxSets = 1024 * 256;
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

	if (vkCreateDescriptorPool(m_logicalDevice, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS)
		UnrecoverableError("Failed to create descriptor pool!");
}

VkDescriptorSet VulkanRenderer::backbufferBlit_createDescriptorSet(VkDescriptorSetLayout descriptor_set_layout, LatteTextureViewVk* texViewVk, bool useLinearTexFilter)
{
	uint64 hash = 0;
	hash += (uint64)texViewVk->GetViewRGBA();
	hash += (uint64)texViewVk->GetDefaultTextureSampler(useLinearTexFilter);

	const auto it = m_backbufferBlitDescriptorSetCache.find(hash);
	if (it != m_backbufferBlitDescriptorSetCache.cend())
		return it->second;

	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = m_descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &descriptor_set_layout;

	VkDescriptorSet result;
	if (vkAllocateDescriptorSets(m_logicalDevice, &allocInfo, &result) != VK_SUCCESS)
		UnrecoverableError("Failed to allocate descriptor sets for backbuffer blit");
	performanceMonitor.vk.numDescriptorSets.increment();

	VkDescriptorImageInfo imageInfo = {};
	imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	imageInfo.imageView = texViewVk->GetViewRGBA()->m_textureImageView;
	imageInfo.sampler = texViewVk->GetDefaultTextureSampler(useLinearTexFilter);

	VkWriteDescriptorSet descriptorWrites[2]{};

	VkWriteDescriptorSet& samplerWrite = descriptorWrites[0];
	samplerWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	samplerWrite.dstSet = result;
	samplerWrite.dstBinding = 0;
	samplerWrite.dstArrayElement = 0;
	samplerWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	samplerWrite.descriptorCount = 1;
	samplerWrite.pImageInfo = &imageInfo;

	VkWriteDescriptorSet& uniformBufferWrite = descriptorWrites[1];
	uniformBufferWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	uniformBufferWrite.dstSet = result;
	uniformBufferWrite.dstBinding = 1;
	uniformBufferWrite.descriptorCount = 1;
	uniformBufferWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;

	VkDescriptorBufferInfo uniformBufferInfo{};
	uniformBufferInfo.buffer = m_uniformVarBuffer;
	uniformBufferInfo.offset = 0;
	uniformBufferInfo.range = sizeof(RendererOutputShader::OutputUniformVariables);
	uniformBufferWrite.pBufferInfo = &uniformBufferInfo;

	vkUpdateDescriptorSets(m_logicalDevice, std::size(descriptorWrites), descriptorWrites, 0, nullptr);
	performanceMonitor.vk.numDescriptorSamplerTextures.increment();

	m_backbufferBlitDescriptorSetCache[hash] = result;
	return result;
}

void VulkanRenderer::renderTarget_setViewport(float x, float y, float width, float height, float nearZ, float farZ, bool halfZ)
{

	float vpNewX = x;
	float vpNewY = y + height;
	float vpNewWidth = width;
	float vpNewHeight = -height;

	if (m_state.currentViewport.x == vpNewX && m_state.currentViewport.y == vpNewY && m_state.currentViewport.width == vpNewWidth && m_state.currentViewport.height == vpNewHeight && m_state.currentViewport.minDepth == nearZ && m_state.currentViewport.maxDepth == farZ)
		return;

	m_state.currentViewport.x = vpNewX;
	m_state.currentViewport.y = vpNewY;
	m_state.currentViewport.width = vpNewWidth;
	m_state.currentViewport.height = vpNewHeight;

	m_state.currentViewport.minDepth = nearZ;
	m_state.currentViewport.maxDepth = farZ;

	vkCmdSetViewport(m_state.currentCommandBuffer, 0, 1, &m_state.currentViewport);
}

void VulkanRenderer::renderTarget_setScissor(sint32 scissorX, sint32 scissorY, sint32 scissorWidth, sint32 scissorHeight)
{
	m_state.currentScissorRect.offset.x = scissorX;
	m_state.currentScissorRect.offset.y = scissorY;
	m_state.currentScissorRect.extent.width = scissorWidth;
	m_state.currentScissorRect.extent.height = scissorHeight;
	vkCmdSetScissor(m_state.currentCommandBuffer, 0, 1, &m_state.currentScissorRect);
}

LatteCachedFBO* VulkanRenderer::rendertarget_createCachedFBO(uint64 key)
{
	return new CachedFBOVk(key, m_logicalDevice);
}

void VulkanRenderer::rendertarget_deleteCachedFBO(LatteCachedFBO* cfbo)
{
	if (cfbo == m_state.activeFBO)
		m_state.activeFBO = nullptr;
}

void VulkanRenderer::rendertarget_bindFramebufferObject(LatteCachedFBO* cfbo)
{
	m_state.activeFBO = (CachedFBOVk*)cfbo;
}

void* VulkanRenderer::texture_acquireTextureUploadBuffer(uint32 size)
{
	return memoryManager->TextureUploadBufferAcquire(size);
}

void VulkanRenderer::texture_uploadBufferUnavailable(LatteTexture* texture)
{
	constexpr uint32 kMaxFlags = 8;
	constexpr uint32 kQuietFrames = 600;
	if (!texture)
		return;
	auto* vkTexture = static_cast<LatteTextureVk*>(texture);
	const uint32 frame = (uint32)LatteGPUState.frameCounter;
	if (vkTexture->m_uploadRetryCount != 0 && (uint32)(frame - vkTexture->m_uploadRetryFrame) > kQuietFrames)
		vkTexture->m_uploadRetryCount = 0;
	if (vkTexture->m_uploadRetryInverted && vkTexture->texDataHash2 == vkTexture->m_uploadRetryHash)
		return;
	if (vkTexture->m_uploadRetryCount >= kMaxFlags)
		return;
	vkTexture->m_uploadRetryCount++;
	vkTexture->m_uploadRetryFrame = frame;
	vkTexture->texDataHash2 = ~vkTexture->texDataHash2;
	vkTexture->m_uploadRetryHash = vkTexture->texDataHash2;
	vkTexture->m_uploadRetryInverted = true;
}

void VulkanRenderer::texture_releaseTextureUploadBuffer(uint8* mem)
{
	memoryManager->TextureUploadBufferRelease(mem);
}

TextureDecoder* VulkanRenderer::texture_chooseDecodedFormat(Latte::E_GX2SURFFMT format, bool isDepth, Latte::E_DIM dim, uint32 width, uint32 height)
{
	FormatInfoVK texFormatInfo{};
	GetTextureFormatInfoVK(format, isDepth, dim, width, height, &texFormatInfo);
	return texFormatInfo.decoder;
}

void VulkanRenderer::ReleaseDestructibleObject(VKRDestructibleObject* destructibleObject)
{
	if (destructibleObject->canDestroy())
	{
		delete destructibleObject;
		return;
	}
	m_spinlockDestructionQueue.lock();
	m_destructionQueue.emplace_back(destructibleObject);
	m_spinlockDestructionQueue.unlock();
}

void VulkanRenderer::ProcessDestructionQueue()
{
	m_spinlockDestructionQueue.lock();
	for (auto it = m_destructionQueue.begin(); it != m_destructionQueue.end();)
	{
		if ((*it)->canDestroy())
		{
			delete (*it);
			it = m_destructionQueue.erase(it);
			continue;
		}
		++it;
	}
	m_spinlockDestructionQueue.unlock();
}

VkDescriptorSetInfo::~VkDescriptorSetInfo()
{
	for (auto& it : list_referencedViews)
		it->RemoveDescriptorSetReference(this);
	switch (shaderType)
	{
	case LatteConst::ShaderType::Vertex:
	{
		auto r = pipeline_info->vertex_ds_cache.erase(stateHash);
		cemu_assert_debug(r == 1);
		break;
	}
	case LatteConst::ShaderType::Pixel:
	{
		auto r = pipeline_info->pixel_ds_cache.erase(stateHash);
		cemu_assert_debug(r == 1);
		break;
	}
	case LatteConst::ShaderType::Geometry:
	{
		auto r = pipeline_info->geometry_ds_cache.erase(stateHash);
		cemu_assert_debug(r == 1);
		break;
	}
	default:
		UNREACHABLE;
	}
	performanceMonitor.vk.numDescriptorSamplerTextures.decrement(statsNumSamplerTextures);
	performanceMonitor.vk.numDescriptorDynUniformBuffers.decrement(statsNumDynUniformBuffers);
	performanceMonitor.vk.numDescriptorStorageBuffers.decrement(statsNumStorageBuffers);

	auto renderer = VulkanRenderer::GetInstance();
	renderer->ReleaseDestructibleObject(m_vkObjDescriptorSet);
	m_vkObjDescriptorSet = nullptr;
}

void VulkanRenderer::texture_clearSlice(LatteTexture* hostTexture, sint32 sliceIndex, sint32 mipIndex)
{
	draw_endRenderPass();
	auto vkTexture = (LatteTextureVk*)hostTexture;
	if (vkTexture->isDepth)
		texture_clearDepthSlice(hostTexture, sliceIndex, mipIndex, true, vkTexture->hasStencil, 0.0f, 0);
	else
	{
		cemu_assert_debug(vkTexture->dim != Latte::E_DIM::DIM_3D);
		ClearColorImage(vkTexture, sliceIndex, mipIndex, { 0,0,0,0 }, VK_IMAGE_LAYOUT_GENERAL);
	}
}

void VulkanRenderer::texture_clearColorSlice(LatteTexture* hostTexture, sint32 sliceIndex, sint32 mipIndex, float r, float g, float b, float a)
{
	auto vkTexture = (LatteTextureVk*)hostTexture;
	if(vkTexture->dim == Latte::E_DIM::DIM_3D)
	{
		cemu_assert_unimplemented();
	}
	ClearColorImage(vkTexture, sliceIndex, mipIndex, {r, g, b, a}, VK_IMAGE_LAYOUT_GENERAL);
}

void VulkanRenderer::texture_clearDepthSlice(LatteTexture* hostTexture, uint32 sliceIndex, sint32 mipIndex, bool clearDepth, bool clearStencil, float depthValue, uint32 stencilValue)
{
	draw_endRenderPass();

	auto vkTexture = (LatteTextureVk*)hostTexture;

	VkImageAspectFlags imageAspect = vkTexture->GetImageAspect();

	VkImageAspectFlags aspectMask = 0;
	if (clearDepth && (imageAspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0)
		aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
	if (clearStencil && (imageAspect & VK_IMAGE_ASPECT_STENCIL_BIT) != 0)
		aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;

	auto imageObj = vkTexture->GetImageObj();
	imageObj->flagForCurrentCommandBuffer();

	VkImageSubresourceLayers subresourceRange{};
	subresourceRange.aspectMask = vkTexture->GetImageAspect();
	subresourceRange.mipLevel = mipIndex;
	subresourceRange.baseArrayLayer = sliceIndex;
	subresourceRange.layerCount = 1;
	barrier_image<ANY_TRANSFER | IMAGE_READ | IMAGE_WRITE, ANY_TRANSFER>(vkTexture, subresourceRange, VK_IMAGE_LAYOUT_GENERAL);

	VkClearDepthStencilValue depthStencilValue{};
	depthStencilValue.depth = depthValue;
	depthStencilValue.stencil = stencilValue;

	VkImageSubresourceRange range{};
	range.baseMipLevel = mipIndex;
	range.levelCount = 1;
	range.baseArrayLayer = sliceIndex;
	range.layerCount = 1;

	range.aspectMask = aspectMask;

	vkCmdClearDepthStencilImage(m_state.currentCommandBuffer, imageObj->m_image, VK_IMAGE_LAYOUT_GENERAL, &depthStencilValue, 1, &range);

	barrier_image<ANY_TRANSFER, ANY_TRANSFER | IMAGE_READ | IMAGE_WRITE>(vkTexture, subresourceRange, VK_IMAGE_LAYOUT_GENERAL);
}

void VulkanRenderer::texture_loadSlice(LatteTexture* hostTexture, sint32 width, sint32 height, sint32 depth, void* pixelData, sint32 sliceIndex, sint32 mipIndex, uint32 compressedImageSize)
{
	auto vkTexture = (LatteTextureVk*)hostTexture;
	auto vkImageObj = vkTexture->GetImageObj();
	vkImageObj->flagForCurrentCommandBuffer();

	draw_endRenderPass();

	VkMemoryRequirements memRequirements;
	vkGetImageMemoryRequirements(m_logicalDevice, vkImageObj->m_image, &memRequirements);

	uint32 uploadSize = compressedImageSize;
	uint32 uploadAlignment = memRequirements.alignment;

	VKRSynchronizedRingAllocator& vkMemAllocator = memoryManager->getStagingAllocator();

	auto uploadResv = vkMemAllocator.AllocateBufferMemory(uploadSize, uploadAlignment);
	memcpy(uploadResv.memPtr, pixelData, compressedImageSize);
	vkMemAllocator.FlushReservation(uploadResv);

	FormatInfoVK texFormatInfo;
	GetTextureFormatInfoVK(hostTexture->format, hostTexture->isDepth, hostTexture->dim, 0, 0, &texFormatInfo);

	bool is3DTexture = hostTexture->Is3DTexture();

	VkImageSubresourceLayers barrierSubresourceRange{};
	barrierSubresourceRange.aspectMask = texFormatInfo.vkImageAspect;
	barrierSubresourceRange.mipLevel = mipIndex;
	barrierSubresourceRange.baseArrayLayer = is3DTexture ? 0 : sliceIndex;
	barrierSubresourceRange.layerCount = 1;
	barrier_image<ANY_TRANSFER | IMAGE_READ | IMAGE_WRITE | HOST_WRITE, ANY_TRANSFER>(vkTexture, barrierSubresourceRange, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	VkBufferImageCopy imageRegion[2]{};
	sint32 imageRegionCount = 0;
	if (texFormatInfo.vkImageAspect == VK_IMAGE_ASPECT_COLOR_BIT || texFormatInfo.vkImageAspect == VK_IMAGE_ASPECT_DEPTH_BIT)
	{
		imageRegion[0].bufferOffset = uploadResv.bufferOffset;
		imageRegion[0].imageExtent.width = width;
		imageRegion[0].imageExtent.height = height;
		imageRegion[0].imageExtent.depth = 1;
		imageRegion[0].imageOffset.z = is3DTexture ? sliceIndex : 0;

		imageRegion[0].imageSubresource.mipLevel = mipIndex;
		imageRegion[0].imageSubresource.aspectMask = texFormatInfo.vkImageAspect;
		imageRegion[0].imageSubresource.baseArrayLayer = is3DTexture ? 0 : sliceIndex;
		imageRegion[0].imageSubresource.layerCount = 1;
		imageRegionCount = 1;
	}
	else if (texFormatInfo.vkImageAspect == VK_IMAGE_ASPECT_DEPTH_BIT)
	{
		if (is3DTexture)
			cemu_assert_debug(false);

		imageRegion[0].bufferOffset = uploadResv.bufferOffset;
		imageRegion[0].imageExtent.width = width;
		imageRegion[0].imageExtent.height = height;
		imageRegion[0].imageExtent.depth = 1;

		imageRegion[0].imageSubresource.mipLevel = mipIndex;
		imageRegion[0].imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		imageRegion[0].imageSubresource.baseArrayLayer = sliceIndex;
		imageRegion[0].imageSubresource.layerCount = 1;

		imageRegionCount = 1;
	}
	else if (texFormatInfo.vkImageAspect == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
	{
		if (is3DTexture)
			cemu_assert_debug(false);

		imageRegion[0].bufferOffset = uploadResv.bufferOffset;
		imageRegion[0].imageExtent.width = width;
		imageRegion[0].imageExtent.height = height;
		imageRegion[0].imageExtent.depth = 1;

		imageRegion[0].imageSubresource.mipLevel = mipIndex;
		imageRegion[0].imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		imageRegion[0].imageSubresource.baseArrayLayer = sliceIndex;
		imageRegion[0].imageSubresource.layerCount = 1;

		imageRegion[1].bufferOffset = uploadResv.bufferOffset;
		imageRegion[1].imageExtent.width = width;
		imageRegion[1].imageExtent.height = height;
		imageRegion[1].imageExtent.depth = 1;

		imageRegion[1].imageSubresource.mipLevel = mipIndex;
		imageRegion[1].imageSubresource.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
		imageRegion[1].imageSubresource.baseArrayLayer = sliceIndex;
		imageRegion[1].imageSubresource.layerCount = 1;

		imageRegionCount = 2;
	}
	else
		cemu_assert_debug(false);

	vkCmdCopyBufferToImage(m_state.currentCommandBuffer, uploadResv.vkBuffer, vkImageObj->m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, imageRegionCount, imageRegion);

	barrier_image<ANY_TRANSFER, ANY_TRANSFER | IMAGE_READ | IMAGE_WRITE>(vkTexture, barrierSubresourceRange, VK_IMAGE_LAYOUT_GENERAL);
}

LatteTexture* VulkanRenderer::texture_createTextureEx(Latte::E_DIM dim, MPTR physAddress, MPTR physMipAddress, Latte::E_GX2SURFFMT format, uint32 width, uint32 height, uint32 depth, uint32 pitch, uint32 mipLevels,
	uint32 swizzle, Latte::E_HWTILEMODE tileMode, bool isDepth, bool isRenderTarget)
{
	return new LatteTextureVk(this, dim, physAddress, physMipAddress, format, width, height, depth, pitch, mipLevels, swizzle, tileMode, isDepth, isRenderTarget);
}

void VulkanRenderer::texture_setLatteTexture(LatteTextureView* textureView, uint32 textureUnit)
{
	m_state.boundTexture[textureUnit] = static_cast<LatteTextureViewVk*>(textureView);
}

void VulkanRenderer::texture_copyImageSubData(LatteTexture* src, sint32 srcMip, sint32 effectiveSrcX, sint32 effectiveSrcY, sint32 srcSlice, LatteTexture* dst, sint32 dstMip, sint32 effectiveDstX, sint32 effectiveDstY, sint32 dstSlice, sint32 effectiveCopyWidth, sint32 effectiveCopyHeight, sint32 srcDepth)
{
	LatteTextureVk* srcVk = static_cast<LatteTextureVk*>(src);
	LatteTextureVk* dstVk = static_cast<LatteTextureVk*>(dst);

	draw_endRenderPass();

	VKRObjectTexture* srcVkObj = srcVk->GetImageObj();
	VKRObjectTexture* dstVkObj = dstVk->GetImageObj();
	srcVkObj->flagForCurrentCommandBuffer();
	dstVkObj->flagForCurrentCommandBuffer();

	VkImageCopy region{};
	region.srcOffset.x = effectiveSrcX;
	region.srcOffset.y = effectiveSrcY;
	region.dstOffset.x = effectiveDstX;
	region.dstOffset.y = effectiveDstY;
	region.extent.width = effectiveCopyWidth;
	region.extent.height = effectiveCopyHeight;
	region.extent.depth = 1;

	if (src->Is3DTexture())
	{
		region.srcOffset.z = srcSlice;
		region.extent.depth = srcDepth;
		region.srcSubresource.baseArrayLayer = 0;
		region.srcSubresource.layerCount = 1;
	}
	else
	{
		region.srcOffset.z = 0;
		region.extent.depth = 1;
		region.srcSubresource.baseArrayLayer = srcSlice;
		region.srcSubresource.layerCount = srcDepth;
	}

	if (dst->Is3DTexture())
	{
		region.dstOffset.z = dstSlice;
		region.dstSubresource.baseArrayLayer = 0;
		region.dstSubresource.layerCount = 1;
	}
	else
	{
		region.dstOffset.z = 0;
		region.dstSubresource.baseArrayLayer = dstSlice;
		region.dstSubresource.layerCount = srcDepth;
	}

	region.srcSubresource.mipLevel = srcMip;
	region.srcSubresource.aspectMask = srcVk->GetImageAspect();

	region.dstSubresource.mipLevel = dstMip;
	region.dstSubresource.aspectMask = dstVk->GetImageAspect();

	bool srcIsCompressed = Latte::IsCompressedFormat(srcVk->format);
	bool dstIsCompressed = Latte::IsCompressedFormat(dstVk->format);

	if (!srcIsCompressed && dstIsCompressed)
	{
		sint32 mipWidth = std::max(dst->width >> dstMip, 1);
		sint32 mipHeight = std::max(dst->height >> dstMip, 1);

		if (mipWidth < 4 || mipHeight < 4)
		{
			cemuLog_logDebug(LogType::Force, "vkCmdCopyImage - blocked copy for unsupported uncompressed->compressed copy with dst smaller than 4x4");
			return;
		}

	}

	barrier_image<SYNC_OP::IMAGE_WRITE | SYNC_OP::ANY_TRANSFER, SYNC_OP::ANY_TRANSFER>(srcVk, region.srcSubresource, VK_IMAGE_LAYOUT_GENERAL);
	barrier_image<SYNC_OP::IMAGE_READ | SYNC_OP::IMAGE_WRITE | SYNC_OP::ANY_TRANSFER, SYNC_OP::ANY_TRANSFER>(dstVk, region.dstSubresource, VK_IMAGE_LAYOUT_GENERAL);

	vkCmdCopyImage(m_state.currentCommandBuffer, srcVkObj->m_image, VK_IMAGE_LAYOUT_GENERAL, dstVkObj->m_image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);

	barrier_image<SYNC_OP::ANY_TRANSFER, SYNC_OP::IMAGE_READ | SYNC_OP::IMAGE_WRITE | SYNC_OP::ANY_TRANSFER>(dstVk, region.dstSubresource, VK_IMAGE_LAYOUT_GENERAL);
}

LatteTextureReadbackInfo* VulkanRenderer::texture_createReadback(LatteTextureView* textureView)
{
	auto* result = new LatteTextureReadbackInfoVk(m_logicalDevice, textureView);
	const uint32 linearImageSize = result->GetImageSize();
	if (linearImageSize == 0)
	{
		delete result;
		return nullptr;
	}

	const uint32 uploadSize = linearImageSize;
	const uint32 uploadAlignment = 256;
	m_textureReadbackBufferWriteIndex = (m_textureReadbackBufferWriteIndex + uploadAlignment - 1) & ~(uploadAlignment - 1);

	if ((m_textureReadbackBufferWriteIndex + uploadSize + 256) > TEXTURE_READBACK_SIZE)
	{
		m_textureReadbackBufferWriteIndex = 0;
	}

	const uint32 uploadBufferOffset = m_textureReadbackBufferWriteIndex;
	m_textureReadbackBufferWriteIndex += uploadSize;

	result->SetBuffer(m_textureReadbackBuffer, m_textureReadbackBufferPtr, uploadBufferOffset);

	return result;
}

uint32 s_vkCurrentUniqueId = 0;

uint64 VulkanRenderer::GenUniqueId()
{
	s_vkCurrentUniqueId++;
	return s_vkCurrentUniqueId;
}

void VulkanRenderer::streamout_setupXfbBuffer(uint32 bufferIndex, sint32 ringBufferOffset, uint32 rangeAddr, uint32 rangeSize)
{
	VkDeviceSize tfBufferOffset = ringBufferOffset;
	m_streamoutState.buffer[bufferIndex].enabled = true;
	m_streamoutState.buffer[bufferIndex].ringBufferOffset = ringBufferOffset;
}

void VulkanRenderer::streamout_begin()
{
}

void VulkanRenderer::streamout_rendererFinishDrawcall()
{
	m_streamoutState.buffer[0].enabled = false;
	m_streamoutState.buffer[1].enabled = false;
	m_streamoutState.buffer[2].enabled = false;
	m_streamoutState.buffer[3].enabled = false;
}

void VulkanRenderer::buffer_bindVertexBuffer(uint32 bufferIndex, uint32 offset, uint32 size)
{
	cemu_assert_debug(!m_useHostMemoryForCache);
	if (m_state.currentVertexBinding[bufferIndex].offset == offset)
		return;
	cemu_assert_debug(bufferIndex < LATTE_MAX_VERTEX_BUFFERS);
	m_state.currentVertexBinding[bufferIndex].offset = offset;
	VkBuffer attrBuffer = m_bufferCache;
	VkDeviceSize attrOffset = offset;
	vkCmdBindVertexBuffers(m_state.currentCommandBuffer, bufferIndex, 1, &attrBuffer, &attrOffset);
}

void VulkanRenderer::buffer_bindVertexStrideWorkaroundBuffer(VkBuffer fixedBuffer, uint32 offset, uint32 bufferIndex, uint32 size)
{
	cemu_assert_debug(bufferIndex < LATTE_MAX_VERTEX_BUFFERS);
	m_state.currentVertexBinding[bufferIndex].offset = 0xFFFFFFFF;
	VkBuffer attrBuffer = fixedBuffer;
	VkDeviceSize attrOffset = offset;
	vkCmdBindVertexBuffers(m_state.currentCommandBuffer, bufferIndex, 1, &attrBuffer, &attrOffset);
}

std::pair<VkBuffer, uint32> VulkanRenderer::buffer_genStrideWorkaroundVertexBuffer(MPTR buffer, uint32 size, uint32 oldStride)
{
	cemu_assert_debug(oldStride % 4 != 0);

	std::span<uint8> old_buffer{memory_getPointerFromPhysicalOffset(buffer), size};

	uint32 newStride = oldStride + (4-(oldStride % 4));
	uint32 newSize = size / oldStride * newStride;

	auto new_buffer_alloc = memoryManager->getMetalStrideWorkaroundAllocator().AllocateBufferMemory(newSize + 16, 128);

	std::span<uint8> new_buffer{new_buffer_alloc.memPtr, new_buffer_alloc.size};

	for(size_t elem = 0; elem < size / oldStride; elem++)
	{
		memcpy(&new_buffer[elem * newStride], &old_buffer[elem * oldStride], oldStride);
	}
	return {new_buffer_alloc.vkBuffer, new_buffer_alloc.bufferOffset};
}

void VulkanRenderer::buffer_bindUniformBuffer(LatteConst::ShaderType shaderType, uint32 bufferIndex, uint32 offset, uint32 size)
{
	cemu_assert_debug(!m_useHostMemoryForCache);
	cemu_assert_debug(bufferIndex < 16);
	switch (shaderType)
	{
	case LatteConst::ShaderType::Vertex:
		dynamicOffsetInfo.shaderUB[VulkanRendererConst::SHADER_STAGE_INDEX_VERTEX].uniformBufferOffset[bufferIndex] = offset;
		break;
	case LatteConst::ShaderType::Geometry:
		dynamicOffsetInfo.shaderUB[VulkanRendererConst::SHADER_STAGE_INDEX_GEOMETRY].uniformBufferOffset[bufferIndex] = offset;
		break;
	case LatteConst::ShaderType::Pixel:
		dynamicOffsetInfo.shaderUB[VulkanRendererConst::SHADER_STAGE_INDEX_FRAGMENT].uniformBufferOffset[bufferIndex] = offset;
		break;
	default:
		cemu_assert_debug(false);
	}
}

void VulkanRenderer::bufferCache_init(const sint32 bufferSize)
{
	m_importedMemBaseAddress = 0x10000000;
	size_t hostAllocationSize = 0x40000000ull;
	bool configUseHostMemory = false;
	m_useHostMemoryForCache = false;
	if (m_featureControl.deviceExtensions.external_memory_host && configUseHostMemory)
	{
		m_useHostMemoryForCache = memoryManager->CreateBufferFromHostMemory(memory_getPointerFromVirtualOffset(m_importedMemBaseAddress), hostAllocationSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 0, m_importedMem, m_importedMemMemory);
		if (!m_useHostMemoryForCache)
		{
			cemuLog_log(LogType::Force, "Unable to import host memory to Vulkan buffer. Use default cache system instead");
		}
	}
	if(!m_useHostMemoryForCache)
		memoryManager->CreateBuffer((VkDeviceSize)bufferSize + 64 * 1024 + 256, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 0, m_bufferCache, m_bufferCacheMemory);
}

void VulkanRenderer::bufferCache_upload(uint8* buffer, sint32 size, uint32 bufferOffset)
{
	draw_endRenderPass();

	VKRSynchronizedRingAllocator& vkMemAllocator = memoryManager->getStagingAllocator();

	auto uploadResv = vkMemAllocator.AllocateBufferMemory(size, 256);
	memcpy(uploadResv.memPtr, buffer, size);

	vkMemAllocator.FlushReservation(uploadResv);

	barrier_bufferRange<ANY_TRANSFER | HOST_WRITE, ANY_TRANSFER,
		BUFFER_SHADER_READ, TRANSFER_WRITE>(
			uploadResv.vkBuffer, uploadResv.bufferOffset, uploadResv.size,
			m_bufferCache, bufferOffset, size);

	VkBufferCopy region;
	region.srcOffset = uploadResv.bufferOffset;
	region.dstOffset = bufferOffset;
	region.size = size;
	vkCmdCopyBuffer(m_state.currentCommandBuffer, uploadResv.vkBuffer, m_bufferCache, 1, &region);

	barrier_sequentializeTransfer();
}

void VulkanRenderer::bufferCache_copy(uint32 srcOffset, uint32 dstOffset, uint32 size)
{
	cemu_assert_debug(!m_useHostMemoryForCache);
	draw_endRenderPass();

	barrier_sequentializeTransfer();

	bool isOverlapping = (srcOffset + size) > dstOffset && (srcOffset) < (dstOffset + size);
	cemu_assert_debug(!isOverlapping);

	VkBufferCopy bufferCopy{};
	bufferCopy.srcOffset = srcOffset;
	bufferCopy.dstOffset = dstOffset;
	bufferCopy.size = size;
	vkCmdCopyBuffer(m_state.currentCommandBuffer, m_bufferCache, m_bufferCache, 1, &bufferCopy);

	barrier_sequentializeTransfer();
}

void VulkanRenderer::bufferCache_copyStreamoutToMainBuffer(uint32 srcOffset, uint32 dstOffset, uint32 size)
{
	draw_endRenderPass();

	VkBuffer dstBuffer;
	if (m_useHostMemoryForCache)
	{
		dstBuffer = m_importedMem;
		dstOffset -= m_importedMemBaseAddress;
	}
	else
		dstBuffer = m_bufferCache;

	barrier_bufferRange<BUFFER_SHADER_WRITE, TRANSFER_READ,
		ANY_TRANSFER | BUFFER_SHADER_READ, TRANSFER_WRITE>(
			m_xfbRingBuffer, srcOffset, size,
			dstBuffer, dstOffset, size);

	barrier_sequentializeTransfer();

	VkBufferCopy bufferCopy{};
	bufferCopy.srcOffset = srcOffset;
	bufferCopy.dstOffset = dstOffset;
	bufferCopy.size = size;
	vkCmdCopyBuffer(m_state.currentCommandBuffer, m_xfbRingBuffer, dstBuffer, 1, &bufferCopy);

	barrier_sequentializeTransfer();
}

void VulkanRenderer::AppendOverlayDebugInfo()
{
	ImGui::Text("--- Vulkan debug info ---");
	ImGui::Text("GfxPipelines   %u", performanceMonitor.vk.numGraphicPipelines.get());
	ImGui::Text("DescriptorSets %u", performanceMonitor.vk.numDescriptorSets.get());
	ImGui::Text("DS ImgSamplers %u", performanceMonitor.vk.numDescriptorSamplerTextures.get());
	ImGui::Text("DS DynUniform  %u", performanceMonitor.vk.numDescriptorDynUniformBuffers.get());
	ImGui::Text("DS StorageBuf  %u", performanceMonitor.vk.numDescriptorStorageBuffers.get());
	ImGui::Text("Images         %u", performanceMonitor.vk.numImages.get());
	ImGui::Text("ImageView      %u", performanceMonitor.vk.numImageViews.get());
	ImGui::Text("ImageSampler   %u", performanceMonitor.vk.numSamplers.get());
	ImGui::Text("RenderPass     %u", performanceMonitor.vk.numRenderPass.get());
	ImGui::Text("Framebuffer    %u", performanceMonitor.vk.numFramebuffer.get());
	m_spinlockDestructionQueue.lock();
	ImGui::Text("DestructionQ   %u", (unsigned int)m_destructionQueue.size());
	m_spinlockDestructionQueue.unlock();

	ImGui::Text("BeginRP/f      %u", performanceMonitor.vk.numBeginRenderpassPerFrame.get());
	ImGui::Text("Barriers/f     %u", performanceMonitor.vk.numDrawBarriersPerFrame.get());
	ImGui::Text("--- Cache debug info ---");

	uint32 bufferCacheHeapSize = 0;
	uint32 bufferCacheAllocationSize = 0;
	uint32 bufferCacheNumAllocations = 0;

	LatteBufferCache_getStats(bufferCacheHeapSize, bufferCacheAllocationSize, bufferCacheNumAllocations);

	ImGui::Text("Buffer");
	ImGui::SameLine(60.0f);
	ImGui::Text("%06uKB / %06uKB Allocs: %u", (uint32)(bufferCacheAllocationSize + 1023) / 1024, ((uint32)bufferCacheHeapSize + 1023) / 1024, (uint32)bufferCacheNumAllocations);

	uint32 numBuffers;
	size_t totalSize, freeSize;

	memoryManager->getStagingAllocator().GetStats(numBuffers, totalSize, freeSize);
	ImGui::Text("Staging");
	ImGui::SameLine(60.0f);
	ImGui::Text("%06uKB / %06uKB Buffers: %u", ((uint32)(totalSize - freeSize) + 1023) / 1024, ((uint32)totalSize + 1023) / 1024, (uint32)numBuffers);

	memoryManager->GetIndexAllocator().GetStats(numBuffers, totalSize, freeSize);
	ImGui::Text("Index");
	ImGui::SameLine(60.0f);
	ImGui::Text("%06uKB / %06uKB Buffers: %u", ((uint32)(totalSize - freeSize) + 1023) / 1024, ((uint32)totalSize + 1023) / 1024, (uint32)numBuffers);

	ImGui::Text("--- Tex heaps ---");
	memoryManager->appendOverlayHeapDebugInfo();
}

void VKRDestructibleObject::flagForCurrentCommandBuffer()
{
	m_lastCmdBufferId = VulkanRenderer::GetInstance()->GetCurrentCommandBufferId();
}

bool VKRDestructibleObject::canDestroy()
{
	if (m_refCount > 0)
		return false;
	return VulkanRenderer::GetInstance()->HasCommandBufferFinished(m_lastCmdBufferId);
}

VKRObjectTexture::VKRObjectTexture()
{
	performanceMonitor.vk.numImages.increment();
}

VKRObjectTexture::~VKRObjectTexture()
{
	auto vkr = VulkanRenderer::GetInstance();
	if (m_allocation)
	{
		vkr->GetMemoryManager()->imageMemoryFree(m_allocation);
		m_allocation = nullptr;
	}
	if (m_image)
		vkDestroyImage(vkr->GetLogicalDevice(), m_image, nullptr);
	performanceMonitor.vk.numImages.decrement();
}

VKRObjectTextureView::VKRObjectTextureView(VKRObjectTexture* tex, VkImageView view)
{
	m_textureImageView = view;
	this->addRef(tex);
	performanceMonitor.vk.numImageViews.increment();
}

VKRObjectTextureView::~VKRObjectTextureView()
{
	auto logicalDevice = VulkanRenderer::GetInstance()->GetLogicalDevice();
	if (m_textureDefaultSampler[0] != VK_NULL_HANDLE)
		vkDestroySampler(logicalDevice, m_textureDefaultSampler[0], nullptr);
	if (m_textureDefaultSampler[1] != VK_NULL_HANDLE)
		vkDestroySampler(logicalDevice, m_textureDefaultSampler[1], nullptr);
	vkDestroyImageView(logicalDevice, m_textureImageView, nullptr);
	performanceMonitor.vk.numImageViews.decrement();
}

static uint64 CalcHashSamplerCreateInfo(const VkSamplerCreateInfo& info)
{
	uint64 h = 0xcbf29ce484222325ULL;
	auto fnvHashCombine = [](uint64_t &h, auto val) {
		using T = decltype(val);
		static_assert(sizeof(T) <= 8);
		uint64_t val64 = 0;
		std::memcpy(&val64, &val, sizeof(val));
		h ^= val64;
		h *= 0x100000001b3ULL;
	};
	cemu_assert_debug(info.sType == VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
	fnvHashCombine(h, info.flags);
	fnvHashCombine(h, info.magFilter);
	fnvHashCombine(h, info.minFilter);
	fnvHashCombine(h, info.mipmapMode);
	fnvHashCombine(h, info.addressModeU);
	fnvHashCombine(h, info.addressModeV);
	fnvHashCombine(h, info.addressModeW);
	fnvHashCombine(h, info.mipLodBias);
	fnvHashCombine(h, info.anisotropyEnable);
	if(info.anisotropyEnable == VK_TRUE)
		fnvHashCombine(h, info.maxAnisotropy);
	fnvHashCombine(h, info.compareEnable);
	if(info.compareEnable == VK_TRUE)
		fnvHashCombine(h, info.compareOp);
	fnvHashCombine(h, info.minLod);
	fnvHashCombine(h, info.maxLod);
	fnvHashCombine(h, info.borderColor);
	fnvHashCombine(h, info.unnormalizedCoordinates);
	VkBaseOutStructure* ext = (VkBaseOutStructure*)info.pNext;
	while(ext)
	{
		if(ext->sType == VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT)
		{
			auto* extInfo = (VkSamplerCustomBorderColorCreateInfoEXT*)ext;
			fnvHashCombine(h, extInfo->customBorderColor.uint32[0]);
			fnvHashCombine(h, extInfo->customBorderColor.uint32[1]);
			fnvHashCombine(h, extInfo->customBorderColor.uint32[2]);
			fnvHashCombine(h, extInfo->customBorderColor.uint32[3]);
		}
		else
		{
			cemu_assert_unimplemented();
		}
		ext = ext->pNext;
	}
	return h;
}

std::unordered_map<uint64, VKRObjectSampler*> VKRObjectSampler::s_samplerCache;

VKRObjectSampler::VKRObjectSampler(VkSamplerCreateInfo* samplerInfo)
{
	auto* vulkanRenderer = VulkanRenderer::GetInstance();
	if (vkCreateSampler(vulkanRenderer->GetLogicalDevice(), samplerInfo, nullptr, &m_sampler) != VK_SUCCESS)
		vulkanRenderer->UnrecoverableError("Failed to create texture sampler");
	performanceMonitor.vk.numSamplers.increment();
	m_hash = CalcHashSamplerCreateInfo(*samplerInfo);
}

VKRObjectSampler::~VKRObjectSampler()
{
	vkDestroySampler(VulkanRenderer::GetInstance()->GetLogicalDevice(), m_sampler, nullptr);
	performanceMonitor.vk.numSamplers.decrement();
	auto it = s_samplerCache.find(m_hash);
	if(it != s_samplerCache.end())
		s_samplerCache.erase(it);
}

void VKRObjectSampler::RefCountReachedZero()
{
	VulkanRenderer::GetInstance()->ReleaseDestructibleObject(this);
}

VKRObjectSampler* VKRObjectSampler::GetOrCreateSampler(VkSamplerCreateInfo* samplerInfo)
{
	auto* vulkanRenderer = VulkanRenderer::GetInstance();
	uint64 hash = CalcHashSamplerCreateInfo(*samplerInfo);
	auto it = s_samplerCache.find(hash);
	if (it != s_samplerCache.end())
	{
		auto* sampler = it->second;
		return sampler;
	}
	auto* sampler = new VKRObjectSampler(samplerInfo);
	s_samplerCache[hash] = sampler;
	return sampler;
}

void VKRObjectSampler::DestroyCache()
{
	cemu_assert_debug(s_samplerCache.empty());
	for(auto& sampler : s_samplerCache)
	{
		cemu_assert_debug(sampler.second->m_refCount == 0);
		delete sampler.second;
	}
	s_samplerCache.clear();
}

VKRObjectRenderPass::VKRObjectRenderPass(AttachmentInfo_t& attachmentInfo, sint32 colorAttachmentCount)
{
	uint64 stateHash = 0;
	for (int i = 0; i < Latte::GPU_LIMITS::NUM_COLOR_ATTACHMENTS; ++i)
	{
		if (attachmentInfo.colorAttachment[i].isPresent || attachmentInfo.colorAttachment[i].viewObj)
		{
			stateHash += attachmentInfo.colorAttachment[i].format + i * 31;
			stateHash = std::rotl<uint64>(stateHash, 7);
		}
	}
	if (attachmentInfo.depthAttachment.isPresent || attachmentInfo.depthAttachment.viewObj)
	{
		stateHash += attachmentInfo.depthAttachment.format;
		stateHash = std::rotl<uint64>(stateHash, 7);
	}
	m_hashForPipeline = stateHash;

	std::vector<VkAttachmentDescription> attachments_descriptions;
	std::array<VkAttachmentReference, Latte::GPU_LIMITS::NUM_COLOR_ATTACHMENTS> color_attachments_references{};
	cemu_assert(colorAttachmentCount <= color_attachments_references.size());
	sint32 numColorAttachments = 0;
	for (int i = 0; i < 8; ++i)
	{
		if (attachmentInfo.colorAttachment[i].viewObj == nullptr && attachmentInfo.colorAttachment[i].isPresent == false)
		{
			color_attachments_references[i].attachment = VK_ATTACHMENT_UNUSED;
			m_colorAttachmentFormat[i] = VK_FORMAT_UNDEFINED;
			continue;
		}
		m_colorAttachmentFormat[i] = attachmentInfo.colorAttachment[i].format;

		color_attachments_references[i].attachment = (uint32)attachments_descriptions.size();
		color_attachments_references[i].layout = VK_IMAGE_LAYOUT_GENERAL;

		VkAttachmentDescription entry{};
		entry.format = attachmentInfo.colorAttachment[i].format;
		entry.samples = VK_SAMPLE_COUNT_1_BIT;
		entry.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		entry.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		entry.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		entry.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		entry.initialLayout = VK_IMAGE_LAYOUT_GENERAL;
		entry.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
		attachments_descriptions.emplace_back(entry);

		numColorAttachments = i + 1;
	}

	VkAttachmentReference depth_stencil_attachments_references{};
	bool hasDepthStencilAttachment = false;
	if (attachmentInfo.depthAttachment.viewObj == nullptr && attachmentInfo.depthAttachment.isPresent == false)
	{
		depth_stencil_attachments_references.attachment = VK_ATTACHMENT_UNUSED;
		m_depthAttachmentFormat = VK_FORMAT_UNDEFINED;
	}
	else
	{
		hasDepthStencilAttachment = true;
		depth_stencil_attachments_references.attachment = (uint32)attachments_descriptions.size();
		depth_stencil_attachments_references.layout = VK_IMAGE_LAYOUT_GENERAL;
		m_depthAttachmentFormat = attachmentInfo.depthAttachment.format;

		VkAttachmentDescription entry{};
		entry.format = attachmentInfo.depthAttachment.format;
		entry.samples = VK_SAMPLE_COUNT_1_BIT;
		entry.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		entry.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		if (attachmentInfo.depthAttachment.hasStencil)
		{
			entry.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
			entry.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
		}
		else
		{
			entry.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			entry.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		}
		entry.initialLayout = VK_IMAGE_LAYOUT_GENERAL;
		entry.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
		attachments_descriptions.emplace_back(entry);
	}

	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = colorAttachmentCount;
	subpass.pColorAttachments = color_attachments_references.data();
	subpass.inputAttachmentCount = 0;
	subpass.pInputAttachments = nullptr;
	subpass.pDepthStencilAttachment = &depth_stencil_attachments_references;

	VkRenderPassCreateInfo renderPassInfo{};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = (uint32)attachments_descriptions.size();
	renderPassInfo.pAttachments = attachments_descriptions.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;

	renderPassInfo.pDependencies = nullptr;
	renderPassInfo.dependencyCount = 0;

	if (vkCreateRenderPass(VulkanRenderer::GetInstance()->GetLogicalDevice(), &renderPassInfo, nullptr, &m_renderPass) != VK_SUCCESS)
	{
		cemuLog_log(LogType::Force, "Vulkan-Error: Failed to create render pass");
		throw std::runtime_error("failed to create render pass!");
	}

	for (int i = 0; i < 8; ++i)
	{
		if (attachmentInfo.colorAttachment[i].viewObj)
			addRef(attachmentInfo.colorAttachment[i].viewObj);
	}
	if (attachmentInfo.depthAttachment.viewObj)
		addRef(attachmentInfo.depthAttachment.viewObj);
	performanceMonitor.vk.numRenderPass.increment();
}

VKRObjectRenderPass::~VKRObjectRenderPass()
{
	if (m_renderPass != VK_NULL_HANDLE)
		vkDestroyRenderPass(VulkanRenderer::GetInstance()->GetLogicalDevice(), m_renderPass, nullptr);
	performanceMonitor.vk.numRenderPass.decrement();
}

VKRObjectFramebuffer::VKRObjectFramebuffer(VKRObjectRenderPass* renderPass, std::span<VKRObjectTextureView*> attachments, Vector2i size)
{
	std::array<VkImageView, 16> attachmentViews;
	cemu_assert(attachments.size() < attachmentViews.size());
	for (size_t i = 0; i < attachments.size(); i++)
		attachmentViews[i] = attachments[i]->m_textureImageView;

	VkFramebufferCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	createInfo.pAttachments = attachmentViews.data();
	createInfo.attachmentCount = attachments.size();
	createInfo.renderPass = renderPass->m_renderPass;
	createInfo.layers = 1;
	createInfo.width = size.x;
	createInfo.height = size.y;
	if (vkCreateFramebuffer(VulkanRenderer::GetInstance()->GetLogicalDevice(), &createInfo, nullptr, &m_frameBuffer) != VK_SUCCESS)
		throw std::runtime_error("failed to create framebuffer!");

	this->addRef(renderPass);
	for (auto& itr : attachments)
		this->addRef(itr);

	performanceMonitor.vk.numFramebuffer.increment();
}

VKRObjectFramebuffer::~VKRObjectFramebuffer()
{
	if (m_frameBuffer != VK_NULL_HANDLE)
		vkDestroyFramebuffer(VulkanRenderer::GetInstance()->GetLogicalDevice(), m_frameBuffer, nullptr);
	performanceMonitor.vk.numFramebuffer.decrement();
}

VKRObjectPipeline::VKRObjectPipeline()
{
}

void VKRObjectPipeline::SetPipeline(VkPipeline newPipeline)
{
	if (m_pipeline == newPipeline)
		return;
	cemu_assert_debug(m_pipeline == VK_NULL_HANDLE);
	if(m_pipeline == VK_NULL_HANDLE && newPipeline != VK_NULL_HANDLE)
		performanceMonitor.vk.numGraphicPipelines.increment();
	else if(m_pipeline != VK_NULL_HANDLE && newPipeline == VK_NULL_HANDLE)
		performanceMonitor.vk.numGraphicPipelines.decrement();
	m_pipeline = newPipeline;
}

VKRObjectPipeline::~VKRObjectPipeline()
{
	auto vkr = VulkanRenderer::GetInstance();
	if (m_pipeline != VK_NULL_HANDLE)
	{
		vkDestroyPipeline(vkr->GetLogicalDevice(), m_pipeline, nullptr);
		performanceMonitor.vk.numGraphicPipelines.decrement();
	}
	if (m_vertexDSL != VK_NULL_HANDLE)
		vkDestroyDescriptorSetLayout(vkr->GetLogicalDevice(), m_vertexDSL, nullptr);
	if (m_pixelDSL != VK_NULL_HANDLE)
		vkDestroyDescriptorSetLayout(vkr->GetLogicalDevice(), m_pixelDSL, nullptr);
	if (m_geometryDSL != VK_NULL_HANDLE)
		vkDestroyDescriptorSetLayout(vkr->GetLogicalDevice(), m_geometryDSL, nullptr);
	if (m_pipelineLayout != VK_NULL_HANDLE)
		vkDestroyPipelineLayout(vkr->GetLogicalDevice(), m_pipelineLayout, nullptr);
}

VKRObjectDescriptorSet::VKRObjectDescriptorSet()
{
	performanceMonitor.vk.numDescriptorSets.increment();
}

VKRObjectDescriptorSet::~VKRObjectDescriptorSet()
{
	auto vkr = VulkanRenderer::GetInstance();
	vkFreeDescriptorSets(vkr->GetLogicalDevice(), vkr->GetDescriptorPool(), 1, &descriptorSet);
	performanceMonitor.vk.numDescriptorSets.decrement();
}

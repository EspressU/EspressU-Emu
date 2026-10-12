#pragma once

#include "util/math/vector2.h"
#include <vulkan/vulkan_core.h>

struct SwapchainInfoVk
{
	enum class VSync
	{
		Immediate = 0,
		FIFO = 1,
		MAILBOX = 2,
		SYNC_AND_LIMIT = 3,
	};

	struct SwapchainSupportDetails
	{
		VkSurfaceCapabilitiesKHR capabilities;
		std::vector<VkSurfaceFormatKHR> formats;
		std::vector<VkPresentModeKHR> presentModes;
	};

	void Cleanup(bool keepSwapchain = false);
	void Create();

	bool IsValid() const;

	void WaitAvailableFence();
	void ResetAvailableFence() const;

	bool AcquireImage();
	VkSemaphore ConsumeAcquireSemaphore();

	static void UnrecoverableError(const char* errMsg);

	static SwapchainSupportDetails QuerySwapchainSupport(VkSurfaceKHR surface, const VkPhysicalDevice& device);

	VkPresentModeKHR ChoosePresentMode(const std::vector<VkPresentModeKHR>& modes);
	VkSurfaceFormatKHR ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const;
	VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) const;

	VkSwapchainCreateInfoKHR CreateSwapchainCreateInfo(VkSurfaceKHR surface, const SwapchainSupportDetails& swapchainSupport, const VkSurfaceFormatKHR& surfaceFormat, uint32 imageCount, const VkExtent2D& extent);

	VkExtent2D getExtent() const
	{
		return m_actualExtent;
	}

	SwapchainInfoVk(bool mainWindow, Vector2i size);
	SwapchainInfoVk(const SwapchainInfoVk&) = delete;
	SwapchainInfoVk(SwapchainInfoVk&&) noexcept = default;
	~SwapchainInfoVk();

	bool mainWindow{};

	bool m_shouldRecreate = false;
	VSync m_vsyncState = VSync::Immediate;
	bool hasDefinedSwapchainImage{};

	VkInstance m_instance{};
	VkPhysicalDevice m_physicalDevice{};
	VkDevice m_logicalDevice{};
	VkSurfaceKHR m_surface{};
	VkSurfaceFormatKHR m_surfaceFormat{};
	VkSwapchainKHR m_swapchain{};
	Vector2i m_desiredExtent{};
	VkExtent2D m_actualExtent{};
	uint32 swapchainImageIndex = (uint32)-1;
	uint64 m_presentId = 1;
	uint64 m_queueDepth = 0;
	uint64 m_maxQueued = 0;

	std::vector<VkImage> m_swapchainImages;
	std::vector<VkImageView> m_swapchainImageViews;
	std::vector<VkFramebuffer> m_swapchainFramebuffers;
	std::vector<VkSemaphore> m_presentSemaphores;

	VkRenderPass m_swapchainRenderPass = nullptr;

private:
	uint32 m_acquireIndex = 0;
	std::vector<VkSemaphore> m_acquireSemaphores;
	VkFence m_imageAvailableFence{};
	VkFence m_awaitableFence = VK_NULL_HANDLE;
	VkSemaphore m_currentSemaphore = VK_NULL_HANDLE;

	std::array<uint32, 2> m_swapchainQueueFamilyIndices;
};

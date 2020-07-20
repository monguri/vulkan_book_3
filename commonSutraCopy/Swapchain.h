#pragma once
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vector>

class Swapchain
{
public:
	Swapchain(VkInstance instance, VkDevice device, VkSurfaceKHR surface);
	~Swapchain();

	void Prepare(VkPhysicalDevice physDev, uint32_t graphicsQueueIndex, uint32_t width, uint32_t height, VkFormat desireFormat);
	void Cleanup();
	VkResult AcquireNextImage(uint32_t* pImageIndex, VkSemaphore semaphore, uint64_t timeout = UINT64_MAX);
	void QueuePresent(VkQueue queue, uint32_t imageIndex, VkSemaphore waitRenderComplete);

	VkSurfaceFormatKHR GetSurfaceFormat() const { return m_selectFormat; }

	VkExtent2D GetSurfaceExtent() const { return m_surfaceExtent; }
	uint32_t GetImageCount() const { return uint32_t(m_images.size()); }
	VkImageView GetImageView(int index) const { return m_imageViews[index]; }
	VkImage GetImage(int index) const { return m_images[index]; }

private:
	VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
	VkSurfaceKHR m_surface = VK_NULL_HANDLE;
	VkInstance m_vkInstance = VK_NULL_HANDLE;
	VkDevice m_device = VK_NULL_HANDLE;
	VkSurfaceCapabilitiesKHR m_surfaceCaps;

	std::vector<VkSurfaceFormatKHR> m_surfaceFormats;
	VkSurfaceFormatKHR m_selectFormat;
	VkExtent2D m_surfaceExtent;
	VkPresentModeKHR m_presentMode;

	std::vector<VkImage> m_images;
	std::vector<VkImageView> m_imageViews;
};


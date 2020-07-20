#include "Swapchain.h"
#include "VulkanBookUtil.h"
#include <algorithm>

Swapchain::Swapchain(VkInstance instance, VkDevice device, VkSurfaceKHR surface)
	: m_swapchain(VK_NULL_HANDLE), m_surface(surface), m_vkInstance(instance), m_device(device), m_presentMode(VK_PRESENT_MODE_FIFO_KHR)
{
}

Swapchain::~Swapchain()
{
}

void Swapchain::Prepare(VkPhysicalDevice physDev, uint32_t graphicsQueueIndex, uint32_t width, uint32_t height, VkFormat desireFormat)
{
	VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physDev, m_surface, &m_surfaceCaps);
	ThrowIfFailed(result, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR Failed.");

	uint32_t count = 0;
	result = vkGetPhysicalDeviceSurfaceFormatsKHR(physDev, m_surface, &count, nullptr);
	ThrowIfFailed(result, "vkGetPhysicalDeviceSurfaceFormatsKHR Failed.");
	m_surfaceFormats.resize(count);
	result = vkGetPhysicalDeviceSurfaceFormatsKHR(physDev, m_surface, &count, m_surfaceFormats.data());
	ThrowIfFailed(result, "vkGetPhysicalDeviceSurfaceFormatsKHR Failed.");

	m_selectFormat = VkSurfaceFormatKHR{
		VK_FORMAT_B8G8R8A8_UNORM,
		VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
	};

	for (VkSurfaceFormatKHR f : m_surfaceFormats)
	{
		if (f.format == desireFormat)
		{
			m_selectFormat = f;
		}
	}

	// サーフェスがサポートされているか確認
	VkBool32 isSupport = VK_FALSE;
	result = vkGetPhysicalDeviceSurfaceSupportKHR(physDev, graphicsQueueIndex, m_surface, &isSupport);
	ThrowIfFailed(result, "vkGetPhysicalDeviceSurfaceSupportKHR Failed.");
	if (isSupport == VK_FALSE)
	{
		throw book_util::VulkanException("vkGetPhysicalDeviceSurfaceSupportKHR: isSupport = false.");
	}

	uint32_t imageCount = (std::max)(2u, m_surfaceCaps.minImageCount); // windowsのmaxというdefineと間違えられないように()で囲んでいる
	VkExtent2D& extent = m_surfaceCaps.currentExtent;
	if (extent.width == ~0u)
	{
		// 値が無効なのでウィンドウサイズを使用する
		extent.width = uint32_t(width);
		extent.height = uint32_t(height);
	}
	m_surfaceExtent = extent;

	VkSwapchainKHR oldSwapchain = m_swapchain;
	uint32_t queueFamilyIndices[] = {graphicsQueueIndex};

	VkSwapchainCreateInfoKHR swapchainCI{};
	swapchainCI.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swapchainCI.pNext = nullptr;
	swapchainCI.flags = 0;
	swapchainCI.surface = m_surface;
	swapchainCI.minImageCount = imageCount;
	swapchainCI.imageFormat = m_selectFormat.format;
	swapchainCI.imageColorSpace = m_selectFormat.colorSpace;
	swapchainCI.imageExtent = m_surfaceExtent;
	swapchainCI.imageArrayLayers = 1;
	swapchainCI.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	swapchainCI.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	swapchainCI.queueFamilyIndexCount = _countof(queueFamilyIndices);
	swapchainCI.pQueueFamilyIndices = queueFamilyIndices;
	swapchainCI.preTransform = m_surfaceCaps.currentTransform;
	swapchainCI.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	swapchainCI.presentMode = m_presentMode;
	swapchainCI.clipped = VK_TRUE;
	swapchainCI.oldSwapchain = oldSwapchain;

	result = vkCreateSwapchainKHR(m_device, &swapchainCI, nullptr, &m_swapchain);
	ThrowIfFailed(result, "vkCreateSwapchainKHR Failed.");

	if (oldSwapchain != VK_NULL_HANDLE)
	{
		for (const VkImageView& view : m_imageViews)
		{
			vkDestroyImageView(m_device, view, nullptr);
		}
		vkDestroySwapchainKHR(m_device, oldSwapchain, nullptr);
		m_imageViews.clear();
		m_images.clear();
	}

	// スワップチェインのもつカラーバッファ用のビュー作成
	vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, nullptr);
	m_images.resize(imageCount);
	vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, m_images.data());

	m_imageViews.resize(imageCount);
	for (uint32_t i = 0; i < imageCount; ++i)
	{
		VkImageViewCreateInfo viewCI{};
		viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewCI.pNext = nullptr;
		viewCI.flags = 0;
		viewCI.image = m_images[i];
		viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewCI.format = m_selectFormat.format;
		viewCI.components = book_util::DefaultComponentMapping();
		viewCI.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		VkResult result = vkCreateImageView(m_device, &viewCI, nullptr, &m_imageViews[i]);
		ThrowIfFailed(result, "vkCreateImageView Failed.");
	}
}

void Swapchain::Cleanup()
{
	if (m_device != VK_NULL_HANDLE)
	{
		for (const VkImageView& view : m_imageViews)
		{
			vkDestroyImageView(m_device, view, nullptr);
		}

		if (m_swapchain != VK_NULL_HANDLE)
		{
			vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
		}

		m_swapchain = VK_NULL_HANDLE;
	}

	if (m_vkInstance != VK_NULL_HANDLE)
	{
		vkDestroySurfaceKHR(m_vkInstance, m_surface, nullptr);
		m_surface = VK_NULL_HANDLE;
	}

	m_imageViews.clear();
	m_images.clear();
}

VkResult Swapchain::AcquireNextImage(uint32_t* pImageIndex, VkSemaphore semaphore, uint64_t timeout)
{
	VkResult result = vkAcquireNextImageKHR(m_device, m_swapchain, timeout, semaphore, VK_NULL_HANDLE, pImageIndex);
	ThrowIfFailed(result, "vkAcquireNextImageKHR Failed.");
	return result;
}

void Swapchain::QueuePresent(VkQueue queue, uint32_t imageIndex, VkSemaphore waitRenderComplete)
{
	VkPresentInfoKHR presentInfo{};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext = nullptr;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &waitRenderComplete;
	presentInfo.swapchainCount = 1;
	presentInfo.pImageIndices = &imageIndex;
	presentInfo.pSwapchains = &m_swapchain;
	VkResult result = vkQueuePresentKHR(queue, &presentInfo);
	ThrowIfFailed(result, "vkQueuePresentKHR Failed.");
}


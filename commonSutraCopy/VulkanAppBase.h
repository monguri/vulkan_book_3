#pragma once
#define WIN32_LEAD_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <memory>
#include <unordered_map>
#include <functional>

#define VK_USE_PLATFORM_WIN32_KHR
#define GLFW_INCLUDE_VULKAN
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <vulkan/vk_layer.h>
#include <vulkan/vulkan_win32.h>

#include "Swapchain.h"


template<class T>
class VulkanObjectStore
{
public:
	VulkanObjectStore(std::function<void(T)> disposer) : m_disposeFunc(disposer) {}

	void Cleanup()
	{
		std::for_each(m_storeMap.begin(), m_storeMap.end(), [&](const std::pair<std::string, T>& v) { m_disposeFunc(v.second); });
	}

	void Register(const std::string& name, const T& data)
	{
		m_storeMap[name] = data;
	}

	T Get(const std::string& name) const
	{
		auto it = m_storeMap.find(name);
		if (it == m_storeMap.end())
		{
			return VK_NULL_HANDLE;
		}

		return it->second;
	}

private:
	std::unordered_map<std::string, T> m_storeMap;
	std::function<void(T)> m_disposeFunc;
};

class VulkanAppBase
{
public:

	virtual bool OnSizeChanged(uint32_t width, uint32_t height);
	virtual void OnMouseButtonDown(int button) {}
	virtual void OnMouseButtonUp(int button) {}
	virtual void OnMouseMove(int dx, int dy) {}

	uint32_t GetMemoryTypeIndex(uint32_t requestBits, VkMemoryPropertyFlags requestProps) const;
	void SwitchFullscreen(GLFWwindow* window);

	void Initialize(GLFWwindow* window, VkFormat format, bool isFullscreen);
	void Terminate();

	virtual void Prepare() {};
	virtual void Cleanup() {};
	virtual void Render() = 0;

	VkRenderPass GetRenderPass(const std::string& name) { return m_renderPassStore->Get(name); }
	void RegisterRenderPass(const std::string& name, const VkRenderPass& renderPass) { m_renderPassStore->Register(name, renderPass); }

	struct BufferObject
	{
		VkBuffer buffer = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
	};

	struct ImageObject
	{
		VkImage image = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
		VkImageView view = VK_NULL_HANDLE;
	};

	BufferObject CreateBuffer(uint32_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props);
	ImageObject CreateImage(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage);
	VkFramebuffer CreateFramebuffer(VkRenderPass renderPass, uint32_t width, uint32_t height, uint32_t viewCount, VkImageView* views);
	void DestroyBuffer(const BufferObject& bufferObj);
	void DestroyImage(const ImageObject& imageObj);
	void DestroyFramebuffers(uint32_t count, VkFramebuffer* framebuffers);
	VkCommandBuffer CreateCommandBuffer();
	void FinishCommandBuffer(VkCommandBuffer command);

	std::vector<BufferObject> CreateUniformBuffers(uint32_t bufferSize, uint32_t imageCount);

private:
	void CreateInstance();
	void SelectGraphicsQueue();
	void CreateDevice();
	void CreateCommandPool();
	void CreateDescriptorPool();
	
	// デバッグレポート有効化
	void EnableDebugReport();
	void DisableDebugReport();
	PFN_vkCreateDebugReportCallbackEXT m_vkCreateDebugReportCallbackEXT = VK_NULL_HANDLE;
	PFN_vkDebugReportMessageEXT m_vkDebugReportMessageEXT = VK_NULL_HANDLE;
	PFN_vkDestroyDebugReportCallbackEXT m_vkDestroyDebugReportCallbackEXT = VK_NULL_HANDLE;
	VkDebugReportCallbackEXT m_debugReport = VK_NULL_HANDLE;

protected:
	void MsgLoopMinimizedWindow();

	VkDevice m_device = VK_NULL_HANDLE;
	VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
	VkInstance m_vkInstance = VK_NULL_HANDLE;

	VkPhysicalDeviceMemoryProperties m_physicalMemProps;
	VkQueue m_deviceQueue = VK_NULL_HANDLE;
	uint32_t m_gfxQueueIndex = ~0u;
	VkCommandPool m_commandPool = VK_NULL_HANDLE;

	VkSemaphore m_renderCompletedSem, m_presentCompletedSem = VK_NULL_HANDLE;

	VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;

	std::unique_ptr<Swapchain> m_swapchain = VK_NULL_HANDLE;

	GLFWwindow* m_window = nullptr;

	bool m_isMinimizedWindow = false;
	bool m_isFullscreen = false;

	using RenderPassRegistry = VulkanObjectStore<VkRenderPass>;
	using DescriptorSetLayoutManager = VulkanObjectStore<VkDescriptorSetLayout>;
	using PipelineLayoutManager = VulkanObjectStore<VkPipelineLayout>;
	std::unique_ptr<RenderPassRegistry> m_renderPassStore;
	std::unique_ptr<DescriptorSetLayoutManager> m_descriptorSetLayoutStore;
	std::unique_ptr<PipelineLayoutManager> m_pipelineLayoutStore;
};


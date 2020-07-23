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
	virtual bool OnMouseButtonDown(int button);
	virtual bool OnMouseButtonUp(int button);
	virtual bool OnMouseMove(int dx, int dy);

	uint32_t GetMemoryTypeIndex(uint32_t requestBits, VkMemoryPropertyFlags requestProps) const;
	void SwitchFullscreen(GLFWwindow* window);

	void Initialize(GLFWwindow* window, VkFormat format, bool isFullscreen);
	void Terminate();

	virtual void Prepare() {};
	virtual void Cleanup() {};
	virtual void Render() = 0;

	VkPipelineLayout GetPipelineLayout(const std::string& name) { return m_pipelineLayoutStore->Get(name); }
	VkDescriptorSetLayout GetDescriptorSetLayout(const std::string& name) { return m_descriptorSetLayoutStore->Get(name); }
	VkRenderPass GetRenderPass(const std::string& name) { return m_renderPassStore->Get(name); }

	void RegisterLayout(const std::string& name, const VkPipelineLayout& layout) { m_pipelineLayoutStore->Register(name, layout); }
	void RegisterLayout(const std::string& name, const VkDescriptorSetLayout& layout) { m_descriptorSetLayoutStore->Register(name, layout); }
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
	VkFence CreateFence();

	void DestroyBuffer(const BufferObject& bufferObj);
	void DestroyImage(const ImageObject& imageObj);
	void DestroyFramebuffers(uint32_t count, VkFramebuffer* framebuffers);
	void DestroyFence(VkFence fence);
	VkCommandBuffer CreateCommandBuffer(bool bBegin = true);
	void FinishCommandBuffer(VkCommandBuffer command);
	void DestroyCommandBuffer(VkCommandBuffer command);

	std::vector<BufferObject> CreateUniformBuffers(uint32_t bufferSize, uint32_t imageCount);

	// ホストから見えるメモリ領域にデータを書き込む.以下バッファを対象に使用.
	// - ステージングバッファ
	// - ユニフォームバッファ
	void WriteToHostVisibleMemory(VkDeviceMemory memory, uint32_t size, const void* pData);

	VkRenderPass CreateRenderPass(VkFormat colorFormat, VkFormat depthFormat = VK_FORMAT_UNDEFINED, VkImageLayout layoutColor = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

	struct ModelData
	{
		BufferObject resVertexBuffer;
		BufferObject resIndexBuffer;
		uint32_t vertexCount;
		uint32_t indexCount;
	};

	template<class T>
	ModelData CreateSimpleModel(const std::vector<T>& vertices, const std::vector<uint32_t>& indices)
	{
		ModelData model;

		VkMemoryPropertyFlags srcMemoryProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		VkMemoryPropertyFlags dstMemoryProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		VkBufferUsageFlags usageVB = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		VkBufferUsageFlags usageIB = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		VkBufferCopy copyVB{}, copyIB{};
		copyVB.srcOffset = 0;
		copyVB.dstOffset = 0;
		copyIB.srcOffset = 0;
		copyIB.dstOffset = 0;

		uint32_t bufferSize = uint32_t(sizeof(T) * vertices.size());
		const BufferObject& uploadVB = CreateBuffer(bufferSize, usageVB, srcMemoryProps);
		model.resVertexBuffer = CreateBuffer(bufferSize, usageVB, dstMemoryProps);
		WriteToHostVisibleMemory(uploadVB.memory, bufferSize, vertices.data());
		model.vertexCount = uint32_t(vertices.size());
		copyVB.size = bufferSize;

		bufferSize = uint32_t(sizeof(uint32_t) * indices.size());
		const BufferObject& uploadIB = CreateBuffer(bufferSize, usageIB, srcMemoryProps);
		model.resIndexBuffer = CreateBuffer(bufferSize, usageIB, dstMemoryProps);
		WriteToHostVisibleMemory(uploadIB.memory, bufferSize, indices.data());
		model.indexCount = uint32_t(indices.size());
		copyIB.size = bufferSize;

		const VkCommandBuffer& command = CreateCommandBuffer();
		vkCmdCopyBuffer(command, uploadVB.buffer, model.resVertexBuffer.buffer, 1, &copyVB);
		vkCmdCopyBuffer(command, uploadIB.buffer, model.resIndexBuffer.buffer, 1, &copyIB);
		FinishCommandBuffer(command);

		vkFreeCommandBuffers(m_device, m_commandPool, 1, &command);
		DestroyBuffer(uploadVB);
		DestroyBuffer(uploadIB);

		return model;
	}

private:
	void CreateInstance();
	void SelectGraphicsQueue();
	void CreateDevice();
	void CreateCommandPool();
	void CreateDescriptorPool();
	void PrepareImGui();
	void CleanupImGui();
	
	// デバッグレポート有効化
	void EnableDebugReport();
	void DisableDebugReport();
	PFN_vkCreateDebugReportCallbackEXT m_vkCreateDebugReportCallbackEXT = VK_NULL_HANDLE;
	PFN_vkDebugReportMessageEXT m_vkDebugReportMessageEXT = VK_NULL_HANDLE;
	PFN_vkDestroyDebugReportCallbackEXT m_vkDestroyDebugReportCallbackEXT = VK_NULL_HANDLE;
	VkDebugReportCallbackEXT m_debugReport = VK_NULL_HANDLE;

protected:
	VkDeviceMemory AllocateMemory(VkImage image, VkMemoryPropertyFlags memProps);
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


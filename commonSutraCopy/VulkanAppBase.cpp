#include "VulkanAppBase.h"
#include "VulkanBookUtil.h"

#include "imgui.h"
#include "examples/imgui_impl_vulkan.h"
#include "examples/imgui_impl_glfw.h"

#include "sstream"

static VkBool32 VKAPI_CALL DebugReportCallback(
	VkDebugReportFlagsEXT flags,
	VkDebugReportObjectTypeEXT objectType,
	uint64_t object,
	size_t location,
	int32_t messageCode,
	const char* pLayerPrefix,
	const char* pMessage,
	void* pUserData)
{
	VkBool32 ret = VK_FALSE;
	if (flags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT || flags & VK_DEBUG_REPORT_DEBUG_BIT_EXT)
	{
		ret = VK_TRUE;
	}

	std::stringstream ss;
	if (pLayerPrefix)
	{
		ss << "[" << pLayerPrefix << "] ";
	}
	ss << pMessage << std::endl;

	OutputDebugStringA(ss.str().c_str());

	return ret;
}

bool VulkanAppBase::OnSizeChanged(uint32_t width, uint32_t height)
{
	m_isMinimizedWindow = (width == 0 || height == 0);
	if (m_isMinimizedWindow)
	{
		return false;
	}
	VkResult result = vkDeviceWaitIdle(m_device);
	ThrowIfFailed(result, "vkDeviceWaitIdle Failed.");

	// �X���b�v�`�F�C������蒼��
	VkFormat format = m_swapchain->GetSurfaceFormat().format;
	m_swapchain->Prepare(m_physicalDevice, m_gfxQueueIndex, width, height, format);
	return true;
}

bool VulkanAppBase::OnMouseButtonDown(int button)
{
	return ImGui::GetIO().WantCaptureMouse;
}

bool VulkanAppBase::OnMouseButtonUp(int button)
{
	return ImGui::GetIO().WantCaptureMouse;
}

bool VulkanAppBase::OnMouseMove(int dx, int dy)
{
	return ImGui::GetIO().WantCaptureMouse;
}


uint32_t VulkanAppBase::GetMemoryTypeIndex(uint32_t requestBits, VkMemoryPropertyFlags requestProps) const
{
	uint32_t result = ~0u;

	for (uint32_t i = 0; i < m_physicalMemProps.memoryTypeCount; ++i)
	{
		if (requestBits & 1) // i���̃r�b�g��1�̂Ƃ�������r����
		{
			const VkMemoryType& type = m_physicalMemProps.memoryTypes[i];
			if ((type.propertyFlags & requestProps) == requestProps)
			{
				result = i;
				break;
			}
		}
		requestBits >>= 1;
	}

	return result;
}

void VulkanAppBase::SwitchFullscreen(GLFWwindow* window)
{
	static int lastWindowPosX, lastWindowPosY;
	static int lastWindowSizeW, lastWindowSizeH;

	GLFWmonitor* monitor = glfwGetPrimaryMonitor();
	const GLFWvidmode* mode = glfwGetVideoMode(monitor);

	// ���݂̃��j�^�[�ɍ��킹���T�C�Y�ɕύX
	if (!m_isFullscreen)
	{
		// to fullscreen
		glfwGetWindowPos(window, &lastWindowPosX, &lastWindowPosY);
		glfwGetWindowSize(window, &lastWindowSizeW, &lastWindowSizeH);
		glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
	}
	else
	{
		// to windowmode
		glfwSetWindowMonitor(window, nullptr, lastWindowPosX, lastWindowPosY, lastWindowSizeW, lastWindowSizeH, mode->refreshRate);
	}

	m_isFullscreen = !m_isFullscreen;
}

void VulkanAppBase::Initialize(GLFWwindow* window, VkFormat format, bool isFullscreen)
{
	m_window = window;
	CreateInstance();

	// �����f�o�C�X�̑I��
	uint32_t count = 0;
	VkResult result = vkEnumeratePhysicalDevices(m_vkInstance, &count, nullptr);
	ThrowIfFailed(result, "vkEnumeratePhysicalDevices Failed.");
	std::vector<VkPhysicalDevice> physicalDevices(count);
	result = vkEnumeratePhysicalDevices(m_vkInstance, &count, physicalDevices.data());
	ThrowIfFailed(result, "vkEnumeratePhysicalDevices Failed.");

	// �ŏ��̃f�o�C�X���g�p����
	m_physicalDevice = physicalDevices[0];
	vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &m_physicalMemProps);

	// �O���t�B�b�N�X�̃L���[�C���f�b�N�X�l���擾.
	SelectGraphicsQueue();

#ifdef _DEBUG
	EnableDebugReport();
#endif

	// �_���f�o�C�X�̐���
	CreateDevice();

	// �R�}���h�v�[���̐���
	CreateCommandPool();

	VkSurfaceKHR surface = VK_NULL_HANDLE;
	result = glfwCreateWindowSurface(m_vkInstance, window, nullptr, &surface);
	ThrowIfFailed(result, "glfwCreateWindowSurface Failed.");

	// �X���b�v�`�F�C���̐���
	m_swapchain = std::make_unique<Swapchain>(m_vkInstance, m_device, surface);
	int width, height;
	glfwGetWindowSize(window, &width, &height);
	m_swapchain->Prepare(m_physicalDevice, m_gfxQueueIndex, uint32_t(width), uint32_t(height), format);

	// �Z�}�t�H�̐���
	VkSemaphoreCreateInfo semCI{};
	semCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	semCI.pNext = nullptr;
	semCI.flags = 0;

	result = vkCreateSemaphore(m_device, &semCI, nullptr, &m_renderCompletedSem);
	ThrowIfFailed(result, "vkCreateSemaphore Failed.");
	result = vkCreateSemaphore(m_device, &semCI, nullptr, &m_presentCompletedSem);
	ThrowIfFailed(result, "vkCreateSemaphore Failed.");

	// �f�B�X�N���v�^�v�[���̐���
	CreateDescriptorPool();

	m_renderPassStore = std::make_unique<RenderPassRegistry>([&](VkRenderPass renderPass) {
		vkDestroyRenderPass(m_device, renderPass, nullptr);
	});

	m_descriptorSetLayoutStore = std::make_unique<DescriptorSetLayoutManager>([&](VkDescriptorSetLayout layout) {
		vkDestroyDescriptorSetLayout(m_device, layout, nullptr);
	});

	m_pipelineLayoutStore = std::make_unique<PipelineLayoutManager>([&](VkPipelineLayout layout) {
		vkDestroyPipelineLayout(m_device, layout, nullptr);
	});

	Prepare();

	PrepareImGui();
}

void VulkanAppBase::Terminate()
{
	if (m_device != VK_NULL_HANDLE)
	{
		VkResult result = vkDeviceWaitIdle(m_device);
		ThrowIfFailed(result, "vkDeviceWaitIdle Failed.");
	}

	Cleanup();

	if (m_swapchain != nullptr)
	{
		m_swapchain->Cleanup();
	}

#ifdef _DEBUG
	DisableDebugReport();
#endif

	m_renderPassStore->Cleanup();
	m_descriptorSetLayoutStore->Cleanup();
	m_pipelineLayoutStore->Cleanup();
	vkDestroySemaphore(m_device, m_presentCompletedSem, nullptr);
	vkDestroySemaphore(m_device, m_renderCompletedSem, nullptr);

	vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
	vkDestroyCommandPool(m_device, m_commandPool, nullptr);
	vkDestroyDevice(m_device, nullptr);
	vkDestroyInstance(m_vkInstance, nullptr);

	m_presentCompletedSem = VK_NULL_HANDLE;
	m_renderCompletedSem = VK_NULL_HANDLE;
	m_descriptorPool = VK_NULL_HANDLE;
	m_commandPool = VK_NULL_HANDLE;
	m_device = VK_NULL_HANDLE;
	m_vkInstance = VK_NULL_HANDLE;
}

void VulkanAppBase::CreateInstance()
{
	VkApplicationInfo appInfo{};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "VulkanBook2";
	appInfo.pEngineName = "VulkanBook2";
	appInfo.apiVersion = VK_API_VERSION_1_1;
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);

	// �C���X�^���X�g�����̎擾
	uint32_t count = 0;
	vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
	std::vector<VkExtensionProperties> props(count);
	vkEnumerateInstanceExtensionProperties(nullptr, &count, props.data());

	std::vector<const char*> extensions;
	extensions.reserve(count);
	for (const VkExtensionProperties& v : props)
	{
		extensions.push_back(v.extensionName);
	}

	VkInstanceCreateInfo instanceCI{};
	instanceCI.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instanceCI.enabledExtensionCount = count;
	instanceCI.ppEnabledExtensionNames = extensions.data();
	instanceCI.pApplicationInfo = &appInfo;
#ifdef _DEBUG
	// �f�o�b�O�r���h�ł͌��؃��C���[��L����
	const char* layers[] = { "VK_LAYER_LUNARG_standard_validation" };
	instanceCI.enabledLayerCount = 1;
	instanceCI.ppEnabledLayerNames = layers;
#endif

	VkResult result = vkCreateInstance(&instanceCI, nullptr, &m_vkInstance);
	ThrowIfFailed(result, "vkCreateInstance Failed.");
}

void VulkanAppBase::SelectGraphicsQueue()
{
	uint32_t queuePropCount;
	vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queuePropCount, nullptr);
	std::vector<VkQueueFamilyProperties> queueFamilyProps(queuePropCount);
	vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queuePropCount, queueFamilyProps.data());

	uint32_t graphicsQueue = ~0u;
	for (uint32_t i = 0; i < queuePropCount; ++i)
	{
		if (queueFamilyProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
		{
			graphicsQueue = i;
			break;
		}
	}

	m_gfxQueueIndex = graphicsQueue;
}

VkDeviceMemory VulkanAppBase::AllocateMemory(VkImage image, VkMemoryPropertyFlags memProps)
{
	VkMemoryRequirements reqs;
	vkGetImageMemoryRequirements(m_device, image, &reqs);

	VkMemoryAllocateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	info.pNext = nullptr;
	info.allocationSize = reqs.size;
	info.memoryTypeIndex = GetMemoryTypeIndex(reqs.memoryTypeBits, memProps); // �{����memProps�ϐ��������Ŏg���ĂȂ���VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT�����Ă�o�O������

	VkDeviceMemory memory = VK_NULL_HANDLE;

	VkResult result = vkAllocateMemory(m_device, &info, nullptr, &memory);
	ThrowIfFailed(result, "vkAllocateMemory Failed.");

	return memory;
}

#define GetInstanceProcAddr(FuncName) \
m_##FuncName = reinterpret_cast<PFN_##FuncName>(vkGetInstanceProcAddr(m_vkInstance, #FuncName))

void VulkanAppBase::EnableDebugReport()
{
	GetInstanceProcAddr(vkCreateDebugReportCallbackEXT);
	GetInstanceProcAddr(vkDebugReportMessageEXT);
	GetInstanceProcAddr(vkDestroyDebugReportCallbackEXT);

	VkDebugReportFlagsEXT flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;

	VkDebugReportCallbackCreateInfoEXT drcCI{};
	drcCI.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
	drcCI.flags = flags;
	drcCI.pfnCallback = &DebugReportCallback;

	VkResult result = m_vkCreateDebugReportCallbackEXT(m_vkInstance, &drcCI, nullptr, &m_debugReport);
	ThrowIfFailed(result, "vkCreateDebugReportCallbackEXT Failed.");
}

void VulkanAppBase::DisableDebugReport()
{
	if (m_vkDestroyDebugReportCallbackEXT != nullptr)
	{
		m_vkDestroyDebugReportCallbackEXT(m_vkInstance, m_debugReport, nullptr);
	}
}

void VulkanAppBase::CreateDevice()
{
	const float defaultQueuePriority(1.0f);
	VkDeviceQueueCreateInfo devQueueCI{};
	devQueueCI.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	devQueueCI.pNext = nullptr;
	devQueueCI.flags = 0;
	devQueueCI.queueFamilyIndex = m_gfxQueueIndex;
	devQueueCI.queueCount = 1;
	devQueueCI.pQueuePriorities = &defaultQueuePriority;


	// �f�o�C�X�g�����������ł��擾
	uint32_t count = 0;
	VkResult result = vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &count, nullptr);
	ThrowIfFailed(result, "vkEnumerateDeviceExtensionProperties Failed.");
	std::vector<VkExtensionProperties> deviceExtensions(count);
	result = vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &count, deviceExtensions.data());
	ThrowIfFailed(result, "vkEnumerateDeviceExtensionProperties Failed.");

	std::vector<const char*> extensions;
	extensions.reserve(count);
	for (const VkExtensionProperties& v : deviceExtensions)
	{
		extensions.push_back(v.extensionName);
	}

	VkDeviceCreateInfo ci{};
	ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	ci.pNext = nullptr;
	ci.flags = 0;
	ci.queueCreateInfoCount = 1;
	ci.pQueueCreateInfos = &devQueueCI;
	ci.enabledLayerCount = 0;
	ci.ppEnabledLayerNames = nullptr;
	ci.enabledExtensionCount = count;
	ci.ppEnabledExtensionNames = extensions.data();
	ci.pEnabledFeatures = nullptr;

	result = vkCreateDevice(m_physicalDevice, &ci, nullptr, &m_device);
	ThrowIfFailed(result, "vkCreateDevice Failed.");

	vkGetDeviceQueue(m_device, m_gfxQueueIndex, 0, &m_deviceQueue);
}

void VulkanAppBase::CreateCommandPool()
{
	VkCommandPoolCreateInfo cmdPoolCI{};
	cmdPoolCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	cmdPoolCI.pNext = nullptr;
	cmdPoolCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	cmdPoolCI.queueFamilyIndex = m_gfxQueueIndex;

	VkResult result = vkCreateCommandPool(m_device, &cmdPoolCI, nullptr, &m_commandPool);
	ThrowIfFailed(result, "vkCreateCommandPool Failed.");
}

void VulkanAppBase::CreateDescriptorPool()
{
	VkDescriptorPoolSize poolSize[2];
	poolSize[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSize[0].descriptorCount = 1000; // �Ƃ肠����1000
	poolSize[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	poolSize[1].descriptorCount = 1000; // �Ƃ肠����1000

	VkDescriptorPoolCreateInfo descPoolCI{};
	descPoolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	descPoolCI.pNext = nullptr;
	descPoolCI.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	descPoolCI.maxSets = 1000 * _countof(poolSize);
	descPoolCI.poolSizeCount = _countof(poolSize);
	descPoolCI.pPoolSizes = poolSize;
	VkResult result = vkCreateDescriptorPool(m_device, &descPoolCI, nullptr, &m_descriptorPool);
	ThrowIfFailed(result, "vkCreateDescriptorPool Failed.");
}

VulkanAppBase::BufferObject VulkanAppBase::CreateBuffer(uint32_t size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props)
{
	BufferObject obj;

	VkBufferCreateInfo bufferCI{};
	bufferCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferCI.pNext = nullptr;
	bufferCI.flags = 0;
	bufferCI.size = size;
	bufferCI.usage = usage;
	bufferCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	bufferCI.queueFamilyIndexCount = 0;
	bufferCI.pQueueFamilyIndices = nullptr;
	VkResult result = vkCreateBuffer(m_device, &bufferCI, nullptr, &obj.buffer);
	ThrowIfFailed(result, "vkCreateBuffer Failed.");

	// �������ʂ̎Z�o
	VkMemoryRequirements reqs;
	vkGetBufferMemoryRequirements(m_device, obj.buffer, &reqs);

	VkMemoryAllocateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	info.pNext = nullptr;
	info.allocationSize = reqs.size;
	info.memoryTypeIndex = GetMemoryTypeIndex(reqs.memoryTypeBits, props);
	result = vkAllocateMemory(m_device, &info, nullptr, &obj.memory);
	ThrowIfFailed(result, "vkAllocateMemory Failed.");

	result = vkBindBufferMemory(m_device, obj.buffer, obj.memory, 0);
	ThrowIfFailed(result, "vkBindBufferMemory Failed.");

	return obj;
}

VulkanAppBase::ImageObject VulkanAppBase::CreateImage(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage)
{
	ImageObject obj;

	VkImageCreateInfo imageCI{};
	imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageCI.pNext = nullptr;
	imageCI.flags = 0;
	imageCI.imageType = VK_IMAGE_TYPE_2D;
	imageCI.format = format;
	imageCI.extent.width = width;
	imageCI.extent.height = height;
	imageCI.extent.depth = 1;
	imageCI.mipLevels = 1;
	imageCI.arrayLayers = 1;
	imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
	imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageCI.usage = usage;
	imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageCI.queueFamilyIndexCount = 0;
	imageCI.pQueueFamilyIndices = nullptr;
	imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkResult result = vkCreateImage(m_device, &imageCI, nullptr, &obj.image);
	ThrowIfFailed(result, "vkCreateImage Failed.");

	VkMemoryRequirements reqs;
	vkGetImageMemoryRequirements(m_device, obj.image, &reqs);

	VkMemoryAllocateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	info.pNext = nullptr;
	info.allocationSize = reqs.size;
	info.memoryTypeIndex = GetMemoryTypeIndex(reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	result = vkAllocateMemory(m_device, &info, nullptr, &obj.memory);
	ThrowIfFailed(result, "vkAllocateMemory Failed.");
	result = vkBindImageMemory(m_device, obj.image, obj.memory, 0);
	ThrowIfFailed(result, "vkBindImageMemory Failed.");

	VkImageAspectFlags imageAspect = VK_IMAGE_ASPECT_COLOR_BIT;
	if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
	{
		imageAspect = VK_IMAGE_ASPECT_DEPTH_BIT;
	}

	VkImageViewCreateInfo viewCI{};
	viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewCI.pNext = nullptr;
	viewCI.flags = 0;
	viewCI.image = obj.image;
	viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewCI.format = imageCI.format;
	viewCI.components = book_util::DefaultComponentMapping();
	viewCI.subresourceRange = { imageAspect, 0, 1, 0, 1 };
	result = vkCreateImageView(m_device, &viewCI, nullptr, &obj.view);
	ThrowIfFailed(result, "vkCreateImageView Failed.");

	return obj;
}

void VulkanAppBase::DestroyBuffer(const VulkanAppBase::BufferObject& bufferObj)
{
	vkDestroyBuffer(m_device, bufferObj.buffer, nullptr);
	vkFreeMemory(m_device, bufferObj.memory, nullptr);
}

void VulkanAppBase::DestroyImage(const VulkanAppBase::ImageObject& imageObj)
{
	vkFreeMemory(m_device, imageObj.memory, nullptr);
	vkDestroyImage(m_device, imageObj.image, nullptr);
	if (imageObj.view != VK_NULL_HANDLE)
	{
		vkDestroyImageView(m_device, imageObj.view, nullptr);
	}
}

VkFramebuffer VulkanAppBase::CreateFramebuffer(VkRenderPass renderPass, uint32_t width, uint32_t height, uint32_t viewCount, VkImageView* views)
{
	VkFramebufferCreateInfo fbCI{};
	fbCI.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	fbCI.pNext = nullptr;
	fbCI.flags = 0;
	fbCI.renderPass = renderPass;
	fbCI.attachmentCount = viewCount;
	fbCI.pAttachments = views;
	fbCI.width = width;
	fbCI.height = height;
	fbCI.layers = 1;

	VkFramebuffer framebuffer = nullptr;
	VkResult result = vkCreateFramebuffer(m_device, &fbCI, nullptr, &framebuffer);
	ThrowIfFailed(result, "vkCreateFramebuffer Failed.");
	return framebuffer;
}

void VulkanAppBase::DestroyFramebuffers(uint32_t count, VkFramebuffer* framebuffers)
{
	for (uint32_t i = 0; i < count; i++)
	{
		vkDestroyFramebuffer(m_device, framebuffers[i], nullptr);
	}
}

VkFence VulkanAppBase::CreateFence()
{
	VkFenceCreateInfo fenceCI{};
	fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceCI.pNext = nullptr;
	fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	VkFence fence = VK_NULL_HANDLE;
	VkResult result = vkCreateFence(m_device, &fenceCI, nullptr, &fence);
	ThrowIfFailed(result, "vkCreateFence Failed.");

	return fence;
}

void VulkanAppBase::DestroyFence(VkFence fence)
{
	vkDestroyFence(m_device, fence, nullptr);
}

VkCommandBuffer VulkanAppBase::CreateCommandBuffer(bool bBegin)
{
	VkCommandBuffer command;

	VkCommandBufferAllocateInfo commandAI{};
	commandAI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	commandAI.pNext = nullptr;
	commandAI.commandPool = m_commandPool;
	commandAI.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	commandAI.commandBufferCount = 1;
	VkResult result = vkAllocateCommandBuffers(m_device, &commandAI, &command);
	ThrowIfFailed(result, "vkAllocateCommandBuffers Failed.");

	if (bBegin)
	{
		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

		result = vkBeginCommandBuffer(command, &beginInfo);
		ThrowIfFailed(result, "vkBeginCommandBuffer Failed.");
	}

	return command;
}

void VulkanAppBase::FinishCommandBuffer(VkCommandBuffer command)
{
	VkResult result = vkEndCommandBuffer(command);
	ThrowIfFailed(result, "vkEndCommandBuffer Failed.");

	VkFenceCreateInfo fenceCI{};
	fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceCI.pNext = nullptr;
	fenceCI.flags = 0;

	VkFence fence = VK_NULL_HANDLE;
	result = vkCreateFence(m_device, &fenceCI, nullptr, &fence);
	ThrowIfFailed(result, "vkCreateFence Failed.");

	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.waitSemaphoreCount = 0;
	submitInfo.pWaitSemaphores = nullptr;
	submitInfo.pWaitDstStageMask = nullptr;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &command;
	submitInfo.signalSemaphoreCount = 0;
	submitInfo.pSignalSemaphores = nullptr;

	result = vkQueueSubmit(m_deviceQueue, 1, &submitInfo, fence);
	ThrowIfFailed(result, "vkQueueSubmit Failed.");

	result = vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX);
	ThrowIfFailed(result, "vkWaitForFences Failed.");

	vkDestroyFence(m_device, fence, nullptr);
}

void VulkanAppBase::DestroyCommandBuffer(VkCommandBuffer command)
{
	vkFreeCommandBuffers(m_device, m_commandPool, 1, &command);
}

std::vector<VulkanAppBase::BufferObject> VulkanAppBase::CreateUniformBuffers(uint32_t bufferSize, uint32_t imageCount)
{
	std::vector<BufferObject> buffers(imageCount);

	for (BufferObject& b : buffers)
	{
		VkMemoryPropertyFlags props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		b = CreateBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, props);
	}

	return buffers;
}

VkRenderPass VulkanAppBase::CreateRenderPass(VkFormat colorFormat, VkFormat depthFormat, VkImageLayout layoutColor)
{
	std::vector<VkAttachmentDescription> attachments;

	if (colorFormat == VK_FORMAT_UNDEFINED)
	{
		colorFormat = m_swapchain->GetSurfaceFormat().format;
	}

	VkAttachmentDescription colorTarget, depthTarget;
	colorTarget = book_util::GetAttachmentDescription(
		colorFormat,
		VK_IMAGE_LAYOUT_UNDEFINED,
		layoutColor
	);
	depthTarget = book_util::GetAttachmentDescription(
		depthFormat,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
	);

	VkAttachmentReference colorRef{};
	colorRef.attachment = 0;
	colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depthRef{};
	depthRef.attachment = 1;
	depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpassDesc{};
	subpassDesc.flags = 0;
	subpassDesc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpassDesc.inputAttachmentCount = 0;
	subpassDesc.pInputAttachments = nullptr;
	subpassDesc.colorAttachmentCount = 1;
	subpassDesc.pColorAttachments = &colorRef;
	subpassDesc.pResolveAttachments = nullptr;
	subpassDesc.pDepthStencilAttachment = nullptr;
	subpassDesc.preserveAttachmentCount = 0;
	subpassDesc.pPreserveAttachments = nullptr;

	attachments.push_back(colorTarget);
	if (depthFormat != VK_FORMAT_UNDEFINED)
	{
		attachments.push_back(depthTarget);
		subpassDesc.pDepthStencilAttachment = &depthRef;
	}

	VkRenderPassCreateInfo rpCI{};
	rpCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	rpCI.pNext = nullptr;
	rpCI.flags = 0;
	rpCI.attachmentCount = uint32_t(attachments.size());
	rpCI.pAttachments = attachments.data();
	rpCI.subpassCount = 1;
	rpCI.pSubpasses = &subpassDesc;
	rpCI.dependencyCount = 0;
	rpCI.pDependencies = nullptr;

	VkRenderPass renderPass = VK_NULL_HANDLE;
	VkResult result = vkCreateRenderPass(m_device, &rpCI, nullptr, &renderPass);
	ThrowIfFailed(result, "vkCreateRenderPass Failed.");

	return renderPass;
}

void VulkanAppBase::PrepareImGui()
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	bool success = ImGui_ImplGlfw_InitForVulkan(m_window, true);

	ImGui_ImplVulkan_InitInfo info{};
	info.Instance = m_vkInstance;
	info.PhysicalDevice = m_physicalDevice;
	info.Device = m_device;
	info.QueueFamily = m_gfxQueueIndex;
	info.Queue = m_deviceQueue;
	info.DescriptorPool = m_descriptorPool;
	info.MinImageCount = m_swapchain->GetImageCount();
	info.ImageCount = m_swapchain->GetImageCount();
	success = ImGui_ImplVulkan_Init(&info, GetRenderPass("default"));

	// �t�H���g�e�N�X�`����]������
	VkCommandBufferAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.pNext = nullptr;
	allocInfo.commandPool = m_commandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = 1;

	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.pNext = nullptr;
	beginInfo.flags = 0;
	beginInfo.pInheritanceInfo = nullptr;

	VkCommandBuffer command;
	VkResult result = vkAllocateCommandBuffers(m_device, &allocInfo, &command);
	ThrowIfFailed(result, "vkAllocateCommandBuffers Failed.");

	result = vkBeginCommandBuffer(command, &beginInfo);
	ThrowIfFailed(result, "vkBeginCommandBuffer Failed.");
	ImGui_ImplVulkan_CreateFontsTexture(command);
	result = vkEndCommandBuffer(command);
	ThrowIfFailed(result, "vkEndCommandBuffer Failed.");

	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.pNext = nullptr;
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &m_presentCompletedSem;
	submitInfo.pWaitDstStageMask = nullptr;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &command;
	submitInfo.signalSemaphoreCount = 0;
	submitInfo.pSignalSemaphores = nullptr;
	result = vkQueueSubmit(m_deviceQueue, 1, &submitInfo, VK_NULL_HANDLE);
	ThrowIfFailed(result, "vkQueueSubmit Failed.");

	// �t�H���g�e�N�X�`���]���̊�����҂�
	result = vkDeviceWaitIdle(m_device);
	ThrowIfFailed(result, "vkDeviceWaitIdle Failed.");
	vkFreeCommandBuffers(m_device, m_commandPool, 1, &command);
}

// TODO:���̏����͉��̂��߂ɂ��邩�����ł��ĂȂ�
void VulkanAppBase::MsgLoopMinimizedWindow()
{
	int width, height;

	do
	{
		glfwGetWindowSize(m_window, &width, &height);
		glfwWaitEvents();
	} while (width == 0 || height == 0);
}


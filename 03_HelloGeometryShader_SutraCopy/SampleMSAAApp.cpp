#include "SampleMSAAApp.h"
#include "VulkanBookUtil.h"
#include "TeapotModel.h"

#include <random>
#include <array>

#include <glm/gtc/matrix_transform.hpp>

void SampleMSAAApp::Prepare()
{
	CreateRenderPass();
	CreateRenderPassRT();
	CreateRenderPassMSAA();

	// デプスバッファを準備する
	const VkExtent2D& extent = m_swapchain->GetSurfaceExtent();
	m_depthBuffer = CreateImage(extent.width, extent.height, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);

	// フレームバッファを準備
	PrepareFramebuffers();

	uint32_t imageCount = m_swapchain->GetImageCount();

	m_commandFences.resize(imageCount);
	VkFenceCreateInfo fenceCI{};
	fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceCI.pNext = nullptr;
	fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	for (uint32_t i = 0; i < imageCount; i++)
	{
		VkResult result = vkCreateFence(m_device, &fenceCI, nullptr, &m_commandFences[i]);
		ThrowIfFailed(result, "vkCreateFence Failed.");
	}

	m_commandBuffers.resize(imageCount);
	VkCommandBufferAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.pNext = nullptr;
	allocInfo.commandPool = m_commandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = imageCount;

	VkResult result = vkAllocateCommandBuffers(m_device, &allocInfo, m_commandBuffers.data());
	ThrowIfFailed(result, "vkAllocateCommandBuffers Failed.");

	PrepareRenderTexture();
	PrepareMsaaTexture();

	PrepareFramebufferMSAA();

	PrepareTeapot();
	PreparePlane();

	CreatePipelineTeapot();
	CreatePipelinePlane();
}

void SampleMSAAApp::Cleanup()
{
	DestroyModelData(m_teapot);
	DestroyModelData(m_plane);

	for (const LayoutInfo& layout : { m_layoutTeapot, m_layoutPlane })
	{
		vkDestroyDescriptorSetLayout(m_device, layout.descriptorSet, nullptr);
		vkDestroyPipelineLayout(m_device, layout.pipeline, nullptr);
	}

	DestroyImage(m_depthBuffer);
	uint32_t count = uint32_t(m_framebuffers.size());
	DestroyFramebuffers(count, m_framebuffers.data());
	m_framebuffers.clear();

	DestroyImage(m_colorTarget);
	DestroyImage(m_depthTarget);

	for (const VkFence& f : m_commandFences)
	{
		vkDestroyFence(m_device, f, nullptr);
	}
	m_commandFences.clear();

	vkFreeCommandBuffers(m_device, m_commandPool, uint32_t(m_commandBuffers.size()), m_commandBuffers.data());
	m_commandBuffers.clear();
}

void SampleMSAAApp::Render()
{
	if (m_isMinimizedWindow)
	{
		MsgLoopMinimizedWindow();
	}

	uint32_t imageIndex = 0;
	VkResult result = m_swapchain->AcquireNextImage(&imageIndex, m_presentCompletedSem);
	if (result == VK_ERROR_OUT_OF_DATE_KHR)
	{
		return;
	}

	m_frameIndex = imageIndex;
	VkCommandBuffer& command = m_commandBuffers[m_frameIndex];
	const VkFence& fence = m_commandFences[m_frameIndex];
	result = vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX);
	ThrowIfFailed(result, "vkWaitForFences Failed.");
	result = vkResetFences(m_device, 1, &fence);
	ThrowIfFailed(result, "vkResetFences Failed.");

	VkCommandBufferBeginInfo commandBI{};
	commandBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	commandBI.pNext = nullptr;
	commandBI.flags = 0;
	commandBI.pInheritanceInfo = nullptr;
	result = vkBeginCommandBuffer(command, &commandBI);
	ThrowIfFailed(result, "vkBeginCommandBuffer Failed.");

	RenderToTexture(command);

	VkImageMemoryBarrier imageBarrier{};
	imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	imageBarrier.pNext = nullptr;
	imageBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	imageBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	imageBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	imageBarrier.image = m_colorTarget.image;
	imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	imageBarrier.subresourceRange.baseMipLevel = 0;
	imageBarrier.subresourceRange.levelCount = 1;
	imageBarrier.subresourceRange.baseArrayLayer = 0;
	imageBarrier.subresourceRange.layerCount = 1;

	vkCmdPipelineBarrier(
		command,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_DEPENDENCY_BY_REGION_BIT,
		0, nullptr, // memoryBarrier
		0, nullptr, // bufferMemoryBarrier
		1, &imageBarrier // imageMemoryBarrier
	);

	RenderToMSAABuffer(command);

	const VkImage& swapchainImage = m_swapchain->GetImage(imageIndex);

	VkImageMemoryBarrier swapchainToDstImageBarrier{};
	swapchainToDstImageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	swapchainToDstImageBarrier.pNext = nullptr;
	swapchainToDstImageBarrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	swapchainToDstImageBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	swapchainToDstImageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	swapchainToDstImageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	swapchainToDstImageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	swapchainToDstImageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	swapchainToDstImageBarrier.image = swapchainImage;
	swapchainToDstImageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	swapchainToDstImageBarrier.subresourceRange.baseMipLevel = 0;
	swapchainToDstImageBarrier.subresourceRange.levelCount = 1;
	swapchainToDstImageBarrier.subresourceRange.baseArrayLayer = 0;
	swapchainToDstImageBarrier.subresourceRange.layerCount = 1;

	vkCmdPipelineBarrier(
		command,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_DEPENDENCY_BY_REGION_BIT,
		0, nullptr, // memoryBarrier
		0, nullptr, // bufferMemoryBarrier
		1, &swapchainToDstImageBarrier // imageMemoryBarrier
	);

	const VkExtent2D& surfaceExtent = m_swapchain->GetSurfaceExtent();
	VkImageResolve regionMsaa{};
	regionMsaa.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	regionMsaa.srcSubresource.mipLevel = 0;
	regionMsaa.srcSubresource.baseArrayLayer = 0;
	regionMsaa.srcSubresource.layerCount = 1;
	regionMsaa.srcOffset.x = 0;
	regionMsaa.srcOffset.y = 0;
	regionMsaa.srcOffset.z = 0;
	regionMsaa.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	regionMsaa.dstSubresource.mipLevel = 0;
	regionMsaa.dstSubresource.baseArrayLayer = 0;
	regionMsaa.dstSubresource.layerCount = 1;
	regionMsaa.extent.width = surfaceExtent.width;
	regionMsaa.extent.height = surfaceExtent.height;
	regionMsaa.extent.depth = 1;

	vkCmdResolveImage(command, m_msaaColor.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapchainImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &regionMsaa);

	VkImageMemoryBarrier swapchainToPresentSrcBarrier{};
	swapchainToPresentSrcBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	swapchainToPresentSrcBarrier.pNext = nullptr;
	swapchainToPresentSrcBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	swapchainToPresentSrcBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	swapchainToPresentSrcBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	swapchainToPresentSrcBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	swapchainToPresentSrcBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	swapchainToPresentSrcBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	swapchainToPresentSrcBarrier.image = swapchainImage;
	swapchainToPresentSrcBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	swapchainToPresentSrcBarrier.subresourceRange.baseMipLevel = 0;
	swapchainToPresentSrcBarrier.subresourceRange.levelCount = 1;
	swapchainToPresentSrcBarrier.subresourceRange.baseArrayLayer = 0;
	swapchainToPresentSrcBarrier.subresourceRange.layerCount = 1;

	vkCmdPipelineBarrier(
		command,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		VK_DEPENDENCY_BY_REGION_BIT,
		0, nullptr, // memoryBarrier
		0, nullptr, // bufferMemoryBarrier
		1, &swapchainToPresentSrcBarrier // imageMemoryBarrier
	);

	VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.pNext = nullptr;
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &m_presentCompletedSem;
	submitInfo.pWaitDstStageMask = &waitStageMask;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &command;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &m_renderCompletedSem;

	result = vkEndCommandBuffer(command);
	ThrowIfFailed(result, "vkEndCommandBuffer Failed.");
	result = vkQueueSubmit(m_deviceQueue, 1, &submitInfo, fence);
	ThrowIfFailed(result, "vkQueueSubmit Failed.");

	m_swapchain->QueuePresent(m_deviceQueue, imageIndex, m_renderCompletedSem);

	m_frameCount++;
}

void SampleMSAAApp::CreateRenderPass()
{
	std::array<VkAttachmentDescription, 2> attachments;
	VkAttachmentDescription& colorTarget = attachments[0];
	VkAttachmentDescription& depthTarget = attachments[1];

	colorTarget = VkAttachmentDescription{};
	colorTarget.flags = 0;
	colorTarget.format = m_swapchain->GetSurfaceFormat().format;
	colorTarget.samples = VK_SAMPLE_COUNT_1_BIT;
	colorTarget.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorTarget.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorTarget.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorTarget.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorTarget.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorTarget.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	depthTarget = VkAttachmentDescription{};
	depthTarget.flags = 0;
	depthTarget.format = VK_FORMAT_D32_SFLOAT;
	depthTarget.samples = VK_SAMPLE_COUNT_1_BIT;
	depthTarget.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthTarget.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depthTarget.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	depthTarget.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthTarget.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depthTarget.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

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
	subpassDesc.pDepthStencilAttachment = &depthRef;
	subpassDesc.preserveAttachmentCount = 0;
	subpassDesc.pPreserveAttachments = nullptr;

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

	VkRenderPass renderPass;
	VkResult result = vkCreateRenderPass(m_device, &rpCI, nullptr, &renderPass);
	ThrowIfFailed(result, "vkCreateRenderPass Failed.");
	RegisterRenderPass("default", renderPass);
}

void SampleMSAAApp::CreateRenderPassRT()
{
	std::array<VkAttachmentDescription, 2> attachments;
	attachments[0] = VkAttachmentDescription{};
	attachments[0].flags = 0;
	attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	attachments[1] = VkAttachmentDescription{};
	attachments[1].flags = 0;
	attachments[1].format = VK_FORMAT_D32_SFLOAT;
	attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

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
	subpassDesc.pDepthStencilAttachment = &depthRef;
	subpassDesc.preserveAttachmentCount = 0;
	subpassDesc.pPreserveAttachments = nullptr;

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

	VkRenderPass texturePass;
	VkResult result = vkCreateRenderPass(m_device, &rpCI, nullptr, &texturePass);
	ThrowIfFailed(result, "vkCreateRenderPass Failed.");
	RegisterRenderPass("render_target", texturePass);
}

void SampleMSAAApp::CreateRenderPassMSAA()
{
	VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_4_BIT;
	VkFormat format = m_swapchain->GetSurfaceFormat().format;

	std::array<VkAttachmentDescription, 2> attachments;
	attachments[0] = VkAttachmentDescription{};
	attachments[0].flags = 0;
	attachments[0].format = format;
	attachments[0].samples = samples;
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

	attachments[1] = VkAttachmentDescription{};
	attachments[1].flags = 0;
	attachments[1].format = VK_FORMAT_D32_SFLOAT;
	attachments[1].samples = samples;
	attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

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
	subpassDesc.pDepthStencilAttachment = &depthRef;
	subpassDesc.preserveAttachmentCount = 0;
	subpassDesc.pPreserveAttachments = nullptr;

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

	VkRenderPass renderPass;
	VkResult result = vkCreateRenderPass(m_device, &rpCI, nullptr, &renderPass);
	ThrowIfFailed(result, "vkCreateRenderPass Failed.");
	RegisterRenderPass("draw_msaa", renderPass);
}

void SampleMSAAApp::PrepareFramebuffers()
{
	uint32_t imageCount = m_swapchain->GetImageCount();
	const VkExtent2D& extent = m_swapchain->GetSurfaceExtent();

	m_framebuffers.resize(imageCount);
	for (uint32_t i = 0; i < imageCount; i++)
	{
		std::vector<VkImageView> views;
		views.push_back(m_swapchain->GetImageView(i));
		views.push_back(m_depthBuffer.view);

		VkRenderPass renderPass = GetRenderPass("default");
		m_framebuffers[i] = CreateFramebuffer(renderPass, extent.width, extent.height, uint32_t(views.size()), views.data());
	}
}

void SampleMSAAApp::PrepareFramebufferMSAA()
{
	uint32_t imageCount = m_swapchain->GetImageCount();
	const VkExtent2D& extent = m_swapchain->GetSurfaceExtent();

	std::vector<VkImageView> views;
	views.push_back(m_msaaColor.view);
	views.push_back(m_msaaDepth.view);

	VkRenderPass renderPass = GetRenderPass("draw_msaa");
	m_framebufferMSAA = CreateFramebuffer(renderPass, extent.width, extent.height, uint32_t(views.size()), views.data());
}

bool SampleMSAAApp::OnSizeChanged(uint32_t width, uint32_t height)
{
	bool result = VulkanAppBase::OnSizeChanged(width, height);
	if (result)
	{
		DestroyImage(m_depthBuffer);
		DestroyFramebuffers(uint32_t(m_framebuffers.size()), m_framebuffers.data());

		// デプスバッファを再生成
		const VkExtent2D& extent = m_swapchain->GetSurfaceExtent();
		m_depthBuffer = CreateImage(extent.width, extent.height, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);

		// フレームバッファを準備
		PrepareFramebuffers();
	}

	return result;
}

void SampleMSAAApp::PrepareTeapot()
{
	// ステージ用のVBとIB、ターゲットのVBとIBの用意
	uint32_t bufferSizeVB = uint32_t(sizeof(TeapotModel::TeapotVerticesPN));
	VkBufferUsageFlags usageVB = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	VkMemoryPropertyFlags srcMemoryProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	VkMemoryPropertyFlags dstMemoryProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	const BufferObject& stageVB = CreateBuffer(bufferSizeVB, usageVB | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, srcMemoryProps);
	const BufferObject& targetVB = CreateBuffer(bufferSizeVB, usageVB | VK_BUFFER_USAGE_TRANSFER_DST_BIT, dstMemoryProps);

	uint32_t bufferSizeIB = uint32_t(sizeof(TeapotModel::TeapotIndices));
	VkBufferUsageFlags usageIB = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	const BufferObject& stageIB = CreateBuffer(bufferSizeIB, usageIB | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, srcMemoryProps);
	const BufferObject& targetIB = CreateBuffer(bufferSizeIB, usageIB | VK_BUFFER_USAGE_TRANSFER_DST_BIT, dstMemoryProps);

	// ステージ用のVBとIBにデータをコピー
	void* p = nullptr;
	vkMapMemory(m_device, stageVB.memory, 0, VK_WHOLE_SIZE, 0, &p);
	memcpy(p, TeapotModel::TeapotVerticesPN, bufferSizeVB);
	vkUnmapMemory(m_device, stageVB.memory);
	vkMapMemory(m_device, stageIB.memory, 0, VK_WHOLE_SIZE, 0, &p);
	memcpy(p, TeapotModel::TeapotIndices, bufferSizeIB);
	vkUnmapMemory(m_device, stageIB.memory);

	// ターゲットのVBとIBにデータをコピーするコマンドの実行
	VkCommandBuffer command = CreateCommandBuffer();
	VkBufferCopy copyRegionVB{}, copyRegionIB{};
	copyRegionVB.size = bufferSizeVB;
	copyRegionIB.size = bufferSizeIB;
	vkCmdCopyBuffer(command, stageVB.buffer, targetVB.buffer, 1, &copyRegionVB);
	vkCmdCopyBuffer(command, stageIB.buffer, targetIB.buffer, 1, &copyRegionIB);
	FinishCommandBuffer(command);
	vkFreeCommandBuffers(m_device, m_commandPool, 1, &command);

	m_teapot.vertexBuffer = targetVB;
	m_teapot.indexBuffer = targetIB;
	m_teapot.indexCount = _countof(TeapotModel::TeapotIndices);
	m_teapot.vertexCount = _countof(TeapotModel::TeapotVerticesPN);

	DestroyBuffer(stageVB);
	DestroyBuffer(stageIB);

	// 定数バッファの準備
	uint32_t imageCount = m_swapchain->GetImageCount();
	VkMemoryPropertyFlags uboMemoryProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	uint32_t bufferSize = uint32_t(sizeof(ShaderParameters));
	m_teapot.sceneUB = CreateUniformBuffers(bufferSize, imageCount);

	// teapot用のディスクリプタセット/レイアウトを準備
	LayoutInfo layout{};
	VkDescriptorSetLayoutBinding descSetLayoutBindings[2];
	descSetLayoutBindings[0].binding = 0;
	descSetLayoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	descSetLayoutBindings[0].descriptorCount = 1;
	descSetLayoutBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	descSetLayoutBindings[0].pImmutableSamplers = nullptr;
	descSetLayoutBindings[1].binding = 1;
	descSetLayoutBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	descSetLayoutBindings[1].descriptorCount = 1;
	descSetLayoutBindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	descSetLayoutBindings[1].pImmutableSamplers = nullptr;

	VkDescriptorSetLayoutCreateInfo descSetLayoutCI{};
	descSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descSetLayoutCI.pNext = nullptr;
	descSetLayoutCI.flags = 0;
	descSetLayoutCI.bindingCount = _countof(descSetLayoutBindings);
	descSetLayoutCI.pBindings = descSetLayoutBindings;
	VkResult result = vkCreateDescriptorSetLayout(m_device, &descSetLayoutCI, nullptr, &layout.descriptorSet);
	ThrowIfFailed(result, "vkCreateDescriptorSetLayout Failed.");

	// teapot用のディスクリプタセット/レイアウトを準備
	VkDescriptorSetAllocateInfo descriptorSetAI{};
	descriptorSetAI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descriptorSetAI.pNext = nullptr;
	descriptorSetAI.descriptorPool = m_descriptorPool;
	descriptorSetAI.descriptorSetCount = 1;
	descriptorSetAI.pSetLayouts = &layout.descriptorSet;

	m_teapot.descriptorSet.reserve(imageCount);
	for (uint32_t i = 0; i < imageCount; ++i)
	{
		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
		result = vkAllocateDescriptorSets(m_device, &descriptorSetAI, &descriptorSet);
		ThrowIfFailed(result, "vkAllocateDescriptorSets Failed.");

		m_teapot.descriptorSet.push_back(descriptorSet);
	}

	// ディスクリプタに書き込む
	for (size_t i = 0; i < imageCount; ++i)
	{
		VkDescriptorBufferInfo uniformBufferInfo{};
		uniformBufferInfo.buffer = m_teapot.sceneUB[i].buffer;
		uniformBufferInfo.offset = 0;
		uniformBufferInfo.range = VK_WHOLE_SIZE;

		VkWriteDescriptorSet descSetSceneUB = book_util::PrepareWriteDescriptorSet(
			m_teapot.descriptorSet[i],
			0,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
		);
		descSetSceneUB.pBufferInfo = &uniformBufferInfo;
		vkUpdateDescriptorSets(m_device, 1, &descSetSceneUB, 0, nullptr);
	}

	// パイプラインレイアウトを準備
	VkPipelineLayoutCreateInfo pipelineLayoutCI{};
	pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutCI.pNext = nullptr;
	pipelineLayoutCI.flags = 0;
	pipelineLayoutCI.setLayoutCount = 1;
	pipelineLayoutCI.pSetLayouts = &layout.descriptorSet;
	pipelineLayoutCI.pushConstantRangeCount = 0;
	pipelineLayoutCI.pPushConstantRanges = nullptr;
	result = vkCreatePipelineLayout(m_device, &pipelineLayoutCI, nullptr, &layout.pipeline);
	ThrowIfFailed(result, "vkCreatePipelineLayout Failed.");

	m_layoutTeapot = layout;
}

void SampleMSAAApp::PreparePlane()
{
	// UVは逆にする
	VertexPT vertices[] = {
		{glm::vec3(-1.0f, 1.0f, 0.0f), glm::vec2(0.0f, 1.0f)},
		{glm::vec3(-1.0f, -1.0f, 0.0f), glm::vec2(0.0f, 0.0f)},
		{glm::vec3(1.0f, 1.0f, 0.0f), glm::vec2(1.0f, 1.0f)},
		{glm::vec3(1.0f, -1.0f, 0.0f), glm::vec2(1.0f, 0.0f)},
	};
	uint32_t indices[] = {0, 1, 2, 3};

	// TODO:こっちにはステージ、ターゲットという違いは作らず、最初からvkMapMemoryで
	// 本番にコピーする。なぜ？
	VkMemoryPropertyFlags memoryProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	VkBufferUsageFlags usageVB = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	VkBufferUsageFlags usageIB = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	uint32_t bufferSizeVB = uint32_t(sizeof(vertices));
	uint32_t bufferSizeIB = uint32_t(sizeof(indices));
	m_plane.vertexBuffer = CreateBuffer(bufferSizeVB, usageVB | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, memoryProps);
	m_plane.indexBuffer = CreateBuffer(bufferSizeIB, usageIB | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, memoryProps);
	m_plane.vertexCount = _countof(vertices);
	m_plane.indexCount = _countof(indices);

	// VBとIBにデータをコピー
	void* p = nullptr;
	vkMapMemory(m_device, m_plane.vertexBuffer.memory, 0, VK_WHOLE_SIZE, 0, &p);
	memcpy(p, vertices, bufferSizeVB);
	vkUnmapMemory(m_device, m_plane.vertexBuffer.memory);
	vkMapMemory(m_device, m_plane.indexBuffer.memory, 0, VK_WHOLE_SIZE, 0, &p);
	memcpy(p, indices, bufferSizeIB);
	vkUnmapMemory(m_device, m_plane.indexBuffer.memory);

	// 定数バッファの準備
	uint32_t imageCount = m_swapchain->GetImageCount();
	VkMemoryPropertyFlags uboMemoryProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	uint32_t bufferSize = uint32_t(sizeof(ShaderParameters));
	m_plane.sceneUB = CreateUniformBuffers(bufferSize, imageCount);

	// テクスチャを貼る板用のディスクリプタセット/レイアウトを準備
	LayoutInfo layout{};
	VkDescriptorSetLayoutBinding descSetLayoutBindings[2];
	descSetLayoutBindings[0].binding = 0;
	descSetLayoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	descSetLayoutBindings[0].descriptorCount = 1;
	descSetLayoutBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	descSetLayoutBindings[0].pImmutableSamplers = nullptr;
	descSetLayoutBindings[1].binding = 1;
	descSetLayoutBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descSetLayoutBindings[1].descriptorCount = 1;
	descSetLayoutBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	descSetLayoutBindings[1].pImmutableSamplers = nullptr;

	VkDescriptorSetLayoutCreateInfo descSetLayoutCI{};
	descSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descSetLayoutCI.pNext = nullptr;
	descSetLayoutCI.flags = 0;
	descSetLayoutCI.bindingCount = _countof(descSetLayoutBindings);
	descSetLayoutCI.pBindings = descSetLayoutBindings;
	VkResult result = vkCreateDescriptorSetLayout(m_device, &descSetLayoutCI, nullptr, &layout.descriptorSet);
	ThrowIfFailed(result, "vkCreateDescriptorSetLayout Failed.");

	VkDescriptorSetAllocateInfo descriptorSetAI{};
	descriptorSetAI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descriptorSetAI.pNext = nullptr;
	descriptorSetAI.descriptorPool = m_descriptorPool;
	descriptorSetAI.descriptorSetCount = 1;
	descriptorSetAI.pSetLayouts = &layout.descriptorSet;

	m_plane.descriptorSet.reserve(imageCount);
	for (uint32_t i = 0; i < imageCount; ++i)
	{
		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
		result = vkAllocateDescriptorSets(m_device, &descriptorSetAI, &descriptorSet);
		ThrowIfFailed(result, "vkAllocateDescriptorSets Failed.");

		m_plane.descriptorSet.push_back(descriptorSet);
	}

	VkSamplerCreateInfo samplerCI{};
	samplerCI.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerCI.pNext = nullptr;
	samplerCI.flags = 0;
	samplerCI.magFilter = VK_FILTER_LINEAR;
	samplerCI.minFilter = VK_FILTER_LINEAR;
	samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerCI.mipLodBias = 0.0f;
	samplerCI.anisotropyEnable = VK_FALSE;
	samplerCI.maxAnisotropy = 1.0f;
	samplerCI.compareEnable = VK_FALSE;
	samplerCI.compareOp = VK_COMPARE_OP_NEVER;
	samplerCI.minLod = 0.0f;
	samplerCI.maxLod = 0.0f;
	samplerCI.borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE;
	samplerCI.unnormalizedCoordinates = VK_FALSE;
	result = vkCreateSampler(m_device, &samplerCI, nullptr, &m_sampler);
	ThrowIfFailed(result, "vkCreateSampler Failed.");

	// ディスクリプタに書き込む
	for (size_t i = 0; i < imageCount; ++i)
	{
		VkDescriptorBufferInfo uboInfo{};
		uboInfo.buffer = m_plane.sceneUB[i].buffer;
		uboInfo.offset = 0;
		uboInfo.range = VK_WHOLE_SIZE;

		VkWriteDescriptorSet descSetSceneUB = book_util::PrepareWriteDescriptorSet(
			m_plane.descriptorSet[i],
			0,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
		);
		descSetSceneUB.pBufferInfo = &uboInfo;
		vkUpdateDescriptorSets(m_device, 1, &descSetSceneUB, 0, nullptr);

		VkDescriptorImageInfo texInfo{};
		texInfo.sampler = m_sampler;
		texInfo.imageView = m_colorTarget.view;
		texInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		VkWriteDescriptorSet descSetTexture = book_util::PrepareWriteDescriptorSet(
			m_plane.descriptorSet[i],
			1,
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
		);
		descSetTexture.pImageInfo = &texInfo;
		vkUpdateDescriptorSets(m_device, 1, &descSetTexture, 0, nullptr);
	}

	// パイプラインレイアウトを準備
	VkPipelineLayoutCreateInfo pipelineLayoutCI{};
	pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutCI.pNext = nullptr;
	pipelineLayoutCI.flags = 0;
	pipelineLayoutCI.setLayoutCount = 1;
	pipelineLayoutCI.pSetLayouts = &layout.descriptorSet;
	pipelineLayoutCI.pushConstantRangeCount = 0;
	pipelineLayoutCI.pPushConstantRanges = nullptr;
	result = vkCreatePipelineLayout(m_device, &pipelineLayoutCI, nullptr, &layout.pipeline);
	ThrowIfFailed(result, "vkCreatePipelineLayout Failed.");

	m_layoutPlane = layout;
}

void SampleMSAAApp::CreatePipelineTeapot()
{
	// Teapot用パイプライン
	uint32_t stride = uint32_t(sizeof(TeapotModel::Vertex));

	VkVertexInputBindingDescription vibDesc{};
	vibDesc.binding = 0;
	vibDesc.stride = stride;
	vibDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	std::array<VkVertexInputAttributeDescription, 2> inputAttribs{};
	inputAttribs[0].location = 0;
	inputAttribs[0].binding = 0;
	inputAttribs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	inputAttribs[0].offset = offsetof(TeapotModel::Vertex, Position);
	inputAttribs[1].location = 1;
	inputAttribs[1].binding = 0;
	inputAttribs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
	inputAttribs[1].offset = offsetof(TeapotModel::Vertex, Normal);

	VkPipelineVertexInputStateCreateInfo pipelineVisCI{};
	pipelineVisCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	pipelineVisCI.pNext = nullptr;
	pipelineVisCI.flags = 0;
	pipelineVisCI.vertexBindingDescriptionCount = 1;
	pipelineVisCI.pVertexBindingDescriptions = &vibDesc;
	pipelineVisCI.vertexAttributeDescriptionCount = uint32_t(inputAttribs.size());
	pipelineVisCI.pVertexAttributeDescriptions = inputAttribs.data();

	const VkPipelineColorBlendAttachmentState& colorBlendAttachmentState = book_util::GetOpaqueColorBlendAttachmentState();

	VkPipelineColorBlendStateCreateInfo colorBlendStateCI{};
	colorBlendStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlendStateCI.pNext = nullptr;
	colorBlendStateCI.flags = 0;
	colorBlendStateCI.logicOpEnable = VK_FALSE;
	colorBlendStateCI.logicOp = VK_LOGIC_OP_CLEAR;
	colorBlendStateCI.attachmentCount = 1;
	colorBlendStateCI.pAttachments = &colorBlendAttachmentState;
	colorBlendStateCI.blendConstants[0] = 0.0f;
	colorBlendStateCI.blendConstants[1] = 0.0f;
	colorBlendStateCI.blendConstants[2] = 0.0f;
	colorBlendStateCI.blendConstants[3] = 0.0f;

	VkPipelineInputAssemblyStateCreateInfo inputAssemblyCI{};
	inputAssemblyCI.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssemblyCI.pNext = nullptr;
	inputAssemblyCI.flags = 0;
	inputAssemblyCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	inputAssemblyCI.primitiveRestartEnable = VK_FALSE;

	VkPipelineMultisampleStateCreateInfo multisampleCI{};
	multisampleCI.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampleCI.pNext = nullptr;
	multisampleCI.flags = 0;
	multisampleCI.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	multisampleCI.sampleShadingEnable = VK_FALSE;
	multisampleCI.minSampleShading = 0.0f;
	multisampleCI.pSampleMask = nullptr;
	multisampleCI.alphaToCoverageEnable = VK_FALSE;
	multisampleCI.alphaToOneEnable = VK_FALSE;
	
	VkViewport viewport{};
	viewport.x = 0;
	viewport.y = 0;
	viewport.width = TextureWidth;
	viewport.height = TextureHeight;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	VkOffset2D offset{};
	offset.x = 0;
	offset.y = 0;
	VkRect2D scissor{};
	scissor.offset = offset;
	scissor.extent.width = TextureWidth;
	scissor.extent.height = TextureHeight;

	VkPipelineViewportStateCreateInfo viewportCI{};
	viewportCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportCI.pNext = nullptr;
	viewportCI.flags = 0;
	viewportCI.viewportCount = 1;
	viewportCI.pViewports = &viewport;
	viewportCI.scissorCount = 1;
	viewportCI.pScissors = &scissor;

	// シェーダのロード
	std::vector<VkPipelineShaderStageCreateInfo> shaderStages
	{
		book_util::LoadShader(m_device, "modelVS.spv", VK_SHADER_STAGE_VERTEX_BIT),
		book_util::LoadShader(m_device, "modelFS.spv", VK_SHADER_STAGE_FRAGMENT_BIT),
	};

	const VkPipelineRasterizationStateCreateInfo& rasterizerState = book_util::GetDefaultRasterizerState();

	const VkPipelineDepthStencilStateCreateInfo& dsState = book_util::GetDefaultDepthStencilState();

	// パイプライン構築
	VkRenderPass renderPass = GetRenderPass("render_target");
	VkGraphicsPipelineCreateInfo pipelineCI{};
	pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineCI.pNext = nullptr;
	pipelineCI.stageCount = uint32_t(shaderStages.size());
	pipelineCI.pStages = shaderStages.data();
	pipelineCI.pVertexInputState = &pipelineVisCI;
	pipelineCI.pInputAssemblyState = &inputAssemblyCI;
	pipelineCI.pTessellationState = nullptr;
	pipelineCI.pViewportState = &viewportCI;
	pipelineCI.pRasterizationState = &rasterizerState;
	pipelineCI.pMultisampleState = &multisampleCI;
	pipelineCI.pDepthStencilState = &dsState;
	pipelineCI.pColorBlendState = &colorBlendStateCI;
	pipelineCI.pDynamicState = nullptr; // DynamicState不要
	pipelineCI.layout = m_layoutTeapot.pipeline;
	pipelineCI.renderPass = renderPass;
	pipelineCI.subpass = 0;
	pipelineCI.basePipelineHandle = VK_NULL_HANDLE;
	pipelineCI.basePipelineIndex = 0;
	VkResult result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &m_teapot.pipeline);
	ThrowIfFailed(result, "vkCreateGraphicsPipelines Failed.");

	book_util::DestroyShaderModules(m_device, shaderStages);
}

void SampleMSAAApp::CreatePipelinePlane()
{
	// Plane用パイプライン
	uint32_t stride = uint32_t(sizeof(VertexPT));

	VkVertexInputBindingDescription vibDesc{};
	vibDesc.binding = 0;
	vibDesc.stride = stride;
	vibDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	std::array<VkVertexInputAttributeDescription, 2> inputAttribs{};
	inputAttribs[0].location = 0;
	inputAttribs[0].binding = 0;
	inputAttribs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	inputAttribs[0].offset = offsetof(VertexPT, position);
	inputAttribs[1].location = 1;
	inputAttribs[1].binding = 0;
	inputAttribs[1].format = VK_FORMAT_R32G32_SFLOAT;
	inputAttribs[1].offset = offsetof(VertexPT, uv);

	VkPipelineVertexInputStateCreateInfo pipelineVisCI{};
	pipelineVisCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	pipelineVisCI.pNext = nullptr;
	pipelineVisCI.flags = 0;
	pipelineVisCI.vertexBindingDescriptionCount = 1;
	pipelineVisCI.pVertexBindingDescriptions = &vibDesc;
	pipelineVisCI.vertexAttributeDescriptionCount = uint32_t(inputAttribs.size());
	pipelineVisCI.pVertexAttributeDescriptions = inputAttribs.data();

	const VkPipelineColorBlendAttachmentState& colorBlendAttachmentState = book_util::GetOpaqueColorBlendAttachmentState();

	VkPipelineColorBlendStateCreateInfo colorBlendStateCI{};
	colorBlendStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlendStateCI.pNext = nullptr;
	colorBlendStateCI.flags = 0;
	colorBlendStateCI.logicOpEnable = VK_FALSE;
	colorBlendStateCI.logicOp = VK_LOGIC_OP_CLEAR;
	colorBlendStateCI.attachmentCount = 1;
	colorBlendStateCI.pAttachments = &colorBlendAttachmentState;
	colorBlendStateCI.blendConstants[0] = 0.0f;
	colorBlendStateCI.blendConstants[1] = 0.0f;
	colorBlendStateCI.blendConstants[2] = 0.0f;
	colorBlendStateCI.blendConstants[3] = 0.0f;

	VkPipelineInputAssemblyStateCreateInfo inputAssemblyCI{};
	inputAssemblyCI.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssemblyCI.pNext = nullptr;
	inputAssemblyCI.flags = 0;
	inputAssemblyCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP; // こちらはSTRIP インデックスバッファが0,1,2,3
	inputAssemblyCI.primitiveRestartEnable = VK_FALSE;

	VkPipelineMultisampleStateCreateInfo multisampleCI{};
	multisampleCI.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampleCI.pNext = nullptr;
	multisampleCI.flags = 0;
	multisampleCI.rasterizationSamples = VK_SAMPLE_COUNT_4_BIT; // MSAA
	multisampleCI.sampleShadingEnable = VK_FALSE;
	multisampleCI.minSampleShading = 0.0f;
	multisampleCI.pSampleMask = nullptr;
	multisampleCI.alphaToCoverageEnable = VK_FALSE;
	multisampleCI.alphaToOneEnable = VK_FALSE;
	
	const VkExtent2D& extent = m_swapchain->GetSurfaceExtent();

	const VkViewport& viewport = book_util::GetViewportFlipped(float(extent.width), float(extent.height));

	VkRect2D scissor{};
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	scissor.extent = extent;

	VkPipelineViewportStateCreateInfo viewportCI{};
	viewportCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportCI.pNext = nullptr;
	viewportCI.flags = 0;
	viewportCI.viewportCount = 1;
	viewportCI.pViewports = &viewport;
	viewportCI.scissorCount = 1;
	viewportCI.pScissors = &scissor;

	// シェーダのロード
	std::vector<VkPipelineShaderStageCreateInfo> shaderStages
	{
		book_util::LoadShader(m_device, "planeVS.spv", VK_SHADER_STAGE_VERTEX_BIT),
		book_util::LoadShader(m_device, "planeFS.spv", VK_SHADER_STAGE_FRAGMENT_BIT),
	};

	std::vector<VkDynamicState> dynamicStates{
		VK_DYNAMIC_STATE_SCISSOR,
		VK_DYNAMIC_STATE_VIEWPORT
	};
	VkPipelineDynamicStateCreateInfo pipelineDynamicStateCI{};
	pipelineDynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	pipelineDynamicStateCI.pNext = nullptr;
	pipelineDynamicStateCI.flags = 0;
	pipelineDynamicStateCI.dynamicStateCount = uint32_t(dynamicStates.size());
	pipelineDynamicStateCI.pDynamicStates = dynamicStates.data();

	const VkPipelineRasterizationStateCreateInfo& rasterizerState = book_util::GetDefaultRasterizerState();

	const VkPipelineDepthStencilStateCreateInfo& dsState = book_util::GetDefaultDepthStencilState();

	// パイプライン構築
	VkRenderPass renderPass = GetRenderPass("draw_msaa");
	VkGraphicsPipelineCreateInfo pipelineCI{};
	pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineCI.pNext = nullptr;
	pipelineCI.stageCount = uint32_t(shaderStages.size());
	pipelineCI.pStages = shaderStages.data();
	pipelineCI.pVertexInputState = &pipelineVisCI;
	pipelineCI.pInputAssemblyState = &inputAssemblyCI;
	pipelineCI.pTessellationState = nullptr;
	pipelineCI.pViewportState = &viewportCI;
	pipelineCI.pRasterizationState = &rasterizerState;
	pipelineCI.pMultisampleState = &multisampleCI;
	pipelineCI.pDepthStencilState = &dsState;
	pipelineCI.pColorBlendState = &colorBlendStateCI;
	pipelineCI.pDynamicState = &pipelineDynamicStateCI;
	pipelineCI.layout = m_layoutPlane.pipeline;
	pipelineCI.renderPass = renderPass;
	pipelineCI.subpass = 0;
	pipelineCI.basePipelineHandle = VK_NULL_HANDLE;
	pipelineCI.basePipelineIndex = 0;
	VkResult result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &m_plane.pipeline);
	ThrowIfFailed(result, "vkCreateGraphicsPipelines Failed.");

	book_util::DestroyShaderModules(m_device, shaderStages);
}

void SampleMSAAApp::PrepareRenderTexture()
{
	// 描画先テクスチャの準備
	ImageObject colorTarget;
	VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
	VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;

	{
		VkImageCreateInfo imageCI{};
		imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageCI.pNext = nullptr;
		imageCI.flags = 0;
		imageCI.imageType = VK_IMAGE_TYPE_2D;
		imageCI.format = colorFormat;
		imageCI.extent.width = TextureWidth;
		imageCI.extent.height = TextureHeight;
		imageCI.extent.depth = 1;
		imageCI.mipLevels = 1;
		imageCI.arrayLayers = 1;
		imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCI.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageCI.queueFamilyIndexCount = 0;
		imageCI.pQueueFamilyIndices = nullptr;
		imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkResult result = vkCreateImage(m_device, &imageCI, nullptr, &colorTarget.image);
		ThrowIfFailed(result, "vkCreateImage Failed.");

		// メモリ量の算出
		VkMemoryRequirements reqs;
		vkGetImageMemoryRequirements(m_device, colorTarget.image, &reqs);

		VkMemoryAllocateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		info.pNext = nullptr;
		info.allocationSize = reqs.size;
		info.memoryTypeIndex = GetMemoryTypeIndex(reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		result = vkAllocateMemory(m_device, &info, nullptr, &colorTarget.memory);
		ThrowIfFailed(result, "vkAllocateMemory Failed.");
		result = vkBindImageMemory(m_device, colorTarget.image, colorTarget.memory, 0);
		ThrowIfFailed(result, "vkBindImageMemory Failed.");

		VkImageViewCreateInfo viewCI{};
		viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewCI.pNext = nullptr;
		viewCI.flags = 0;
		viewCI.image = colorTarget.image;
		viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewCI.format = imageCI.format;
		viewCI.components = book_util::DefaultComponentMapping();
		viewCI.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		result = vkCreateImageView(m_device, &viewCI, nullptr, &colorTarget.view);
		ThrowIfFailed(result, "vkCreateImageView Failed.");
	}

	// 描画先デプスバッファの準備
	ImageObject depthTarget;
	{
		VkImageCreateInfo imageCI{};
		imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageCI.pNext = nullptr;
		imageCI.flags = 0;
		imageCI.imageType = VK_IMAGE_TYPE_2D;
		imageCI.format = depthFormat;
		imageCI.extent.width = TextureWidth;
		imageCI.extent.height = TextureHeight;
		imageCI.extent.depth = 1;
		imageCI.mipLevels = 1;
		imageCI.arrayLayers = 1;
		imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCI.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageCI.queueFamilyIndexCount = 0;
		imageCI.pQueueFamilyIndices = nullptr;
		imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkResult result = vkCreateImage(m_device, &imageCI, nullptr, &depthTarget.image);
		ThrowIfFailed(result, "vkCreateImage Failed.");

		// メモリ量の算出
		VkMemoryRequirements reqs;
		vkGetImageMemoryRequirements(m_device, depthTarget.image, &reqs);

		VkMemoryAllocateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		info.pNext = nullptr;
		info.allocationSize = reqs.size;
		info.memoryTypeIndex = GetMemoryTypeIndex(reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		result = vkAllocateMemory(m_device, &info, nullptr, &depthTarget.memory);
		ThrowIfFailed(result, "vkAllocateMemory Failed.");
		result = vkBindImageMemory(m_device, depthTarget.image, depthTarget.memory, 0);
		ThrowIfFailed(result, "vkBindImageMemory Failed.");

		VkImageViewCreateInfo viewCI{};
		viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewCI.pNext = nullptr;
		viewCI.flags = 0;
		viewCI.image = depthTarget.image;
		viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewCI.format = imageCI.format;
		viewCI.components = book_util::DefaultComponentMapping();
		viewCI.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
		result = vkCreateImageView(m_device, &viewCI, nullptr, &depthTarget.view);
		ThrowIfFailed(result, "vkCreateImageView Failed.");
	}

	m_colorTarget = colorTarget;
	m_depthTarget = depthTarget;

	std::vector<VkImageView> views;
	views.push_back(m_colorTarget.view);
	views.push_back(m_depthTarget.view);
	VkRenderPass renderPass = GetRenderPass("render_target");
	m_renderTextureFB = CreateFramebuffer(renderPass, TextureWidth, TextureHeight, uint32_t(views.size()), views.data());
}

void SampleMSAAApp::PrepareMsaaTexture()
{
	// カラー
	VkFormat colorFormat = m_swapchain->GetSurfaceFormat().format;
	const VkExtent2D& surfaceExtent = m_swapchain->GetSurfaceExtent();
	VkExtent3D extent;
	extent.width = surfaceExtent.width;
	extent.height = surfaceExtent.height;
	extent.depth = 1;
	VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_4_BIT;
	VkImageCreateInfo imageCI = book_util::CreateEasyImageCreateInfo(colorFormat, extent, usage, samples);
	VkResult result = vkCreateImage(m_device, &imageCI, nullptr, &m_msaaColor.image);
	ThrowIfFailed(result, "vkCreateImage Failed.");

	VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	m_msaaColor.memory = AllocateMemory(m_msaaColor.image, memProps);
	result = vkBindImageMemory(m_device, m_msaaColor.image, m_msaaColor.memory, 0);
	ThrowIfFailed(result, "vkBindImageMemory Failed.");

	VkImageViewCreateInfo viewCI{};
	viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewCI.pNext = nullptr;
	viewCI.flags = 0;
	viewCI.image = m_msaaColor.image;
	viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewCI.format = imageCI.format;
	viewCI.components = book_util::DefaultComponentMapping();
	viewCI.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	result = vkCreateImageView(m_device, &viewCI, nullptr, &m_msaaColor.view);
	ThrowIfFailed(result, "vkCreateImageView Failed.");

	// デプス
	VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
	usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	imageCI = book_util::CreateEasyImageCreateInfo(depthFormat, extent, usage, samples);
	result = vkCreateImage(m_device, &imageCI, nullptr, &m_msaaDepth.image);
	ThrowIfFailed(result, "vkCreateImage Failed.");

	m_msaaDepth.memory = AllocateMemory(m_msaaDepth.image, memProps);
	result = vkBindImageMemory(m_device, m_msaaDepth.image, m_msaaDepth.memory, 0);
	ThrowIfFailed(result, "vkBindImageMemory Failed.");

	viewCI.image = m_msaaDepth.image;
	viewCI.format = depthFormat;
	viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	result = vkCreateImageView(m_device, &viewCI, nullptr, &m_msaaDepth.view);
	ThrowIfFailed(result, "vkCreateImageView Failed.");

	// サンプル数をデバイスがどれだけ許してるか取得するおまけコード
	VkPhysicalDeviceProperties physProps;
	vkGetPhysicalDeviceProperties(m_physicalDevice, &physProps);
}

void SampleMSAAApp::RenderToTexture(const VkCommandBuffer& command)
{
	std::array<VkClearValue, 2> clearValue = {
		{
			{1.0f, 0.0f, 0.0f, 0.0f}, // for Color
			{1.0f, 0}, // for Depth
		}
	};

	VkRect2D renderArea{};
	renderArea.offset.x = 0;
	renderArea.offset.y = 0;
	renderArea.extent.width = TextureWidth;
	renderArea.extent.height = TextureHeight;

	VkRenderPass renderPass = GetRenderPass("render_target");
	VkRenderPassBeginInfo rpBI{};
	rpBI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rpBI.pNext = nullptr;
	rpBI.renderPass = renderPass;
	rpBI.framebuffer = m_renderTextureFB;
	rpBI.renderArea = renderArea;
	rpBI.clearValueCount = uint32_t(clearValue.size());
	rpBI.pClearValues = clearValue.data();

	{
		ShaderParameters shaderParams{};
		shaderParams.world = glm::mat4(1.0f);
		shaderParams.view = glm::lookAtRH(
			glm::vec3(0.0f, 2.0f, 5.0f),
			glm::vec3(0.0f, 0.0f, 0.0f),
			glm::vec3(0.0f, 1.0f, 0.0f)
		);

		const VkExtent2D& extent = m_swapchain->GetSurfaceExtent();
		shaderParams.proj = glm::perspectiveRH(
			glm::radians(45.0f),
			float(extent.width) / float(extent.height),
			0.1f,
			1000.0f
		);

		const BufferObject& ubo = m_teapot.sceneUB[m_frameIndex];
		void* p = nullptr;
		VkResult result = vkMapMemory(m_device, ubo.memory, 0, VK_WHOLE_SIZE, 0, &p);
		ThrowIfFailed(result, "vkMapMemory Failed.");
		memcpy(p, &shaderParams, sizeof(ShaderParameters));
		vkUnmapMemory(m_device, ubo.memory);
	}

	vkCmdBeginRenderPass(command, &rpBI, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_teapot.pipeline);
	vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layoutTeapot.pipeline, 0, 1, &m_teapot.descriptorSet[m_frameIndex], 0, nullptr);
	vkCmdBindIndexBuffer(command, m_teapot.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
	VkDeviceSize offsets[] = {0};
	vkCmdBindVertexBuffers(command, 0, 1, &m_teapot.vertexBuffer.buffer, offsets);
	vkCmdDrawIndexed(command, m_teapot.indexCount, 1, 0, 0, 0);

	vkCmdEndRenderPass(command);
}

void SampleMSAAApp::RenderToMSAABuffer(const VkCommandBuffer& command)
{
	std::array<VkClearValue, 2> clearValue = {
		{
			{0.0f, 0.0f, 0.0f, 0.0f}, // for Color
			{1.0f, 0}, // for Depth
		}
	};

	VkRect2D renderArea{};
	renderArea.offset.x = 0;
	renderArea.offset.y = 0;
	renderArea.extent = m_swapchain->GetSurfaceExtent();

	VkRenderPassBeginInfo rpBI{};
	rpBI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rpBI.pNext = nullptr;
	rpBI.renderPass = GetRenderPass("draw_msaa");
	rpBI.framebuffer = m_framebufferMSAA;
	rpBI.renderArea = renderArea;
	rpBI.clearValueCount = uint32_t(clearValue.size());
	rpBI.pClearValues = clearValue.data();

	{
		ShaderParameters shaderParam{};
		shaderParam.world = glm::rotate(glm::mat4(1.0f), glm::radians(float(m_frameCount)), glm::vec3(0.0f, 1.0f, 0.0f));
		shaderParam.view = glm::lookAtRH(
			glm::vec3(0.0f, 0.0f, 5.0f),
			glm::vec3(0.0f, 0.0f, 0.0f),
			glm::vec3(0.0f, 1.0f, 0.0f)
		);

		const VkExtent2D& extent = m_swapchain->GetSurfaceExtent();
		shaderParam.proj = glm::perspectiveRH(
			glm::radians(45.0f),
			float(extent.width) / float(extent.height),
			0.1f,
			1000.0f
		);

		const BufferObject& ubo = m_plane.sceneUB[m_frameIndex];
		void* p = nullptr;
		VkResult result = vkMapMemory(m_device, ubo.memory, 0, VK_WHOLE_SIZE, 0, &p);
		ThrowIfFailed(result, "vkMapMemory Failed.");
		memcpy(p, &shaderParam, sizeof(ShaderParameters));
		vkUnmapMemory(m_device, ubo.memory);
	}

	vkCmdBeginRenderPass(command, &rpBI, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_plane.pipeline);

	const VkExtent2D& extent = m_swapchain->GetSurfaceExtent();
	const VkViewport& viewport = book_util::GetViewportFlipped(float(extent.width), float(extent.height));

	VkOffset2D offset{};
	offset.x = 0;
	offset.y = 0;
	VkRect2D scissor{};
	scissor.offset = offset;
	scissor.extent = extent;

	vkCmdSetScissor(command, 0, 1, &scissor);
	vkCmdSetViewport(command, 0, 1, &viewport);

	vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layoutPlane.pipeline, 0, 1, &m_plane.descriptorSet[m_frameIndex], 0, nullptr);
	vkCmdBindIndexBuffer(command, m_plane.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
	VkDeviceSize offsets[] = {0};
	vkCmdBindVertexBuffers(command, 0, 1, &m_plane.vertexBuffer.buffer, offsets);
	vkCmdDrawIndexed(command, m_plane.indexCount, 1, 0, 0, 0);

	vkCmdEndRenderPass(command);
}

void SampleMSAAApp::DestroyModelData(ModelData& model)
{
	for (const BufferObject& bufObj : { model.vertexBuffer, model.indexBuffer })
	{
		DestroyBuffer(bufObj);
	}

	for (const BufferObject& ubo : model.sceneUB)
	{
		DestroyBuffer(ubo);
	}
	model.sceneUB.clear();

	vkDestroyPipeline(m_device, model.pipeline, nullptr);
	vkFreeDescriptorSets(m_device, m_descriptorPool, uint32_t(model.descriptorSet.size()), model.descriptorSet.data());
	model.descriptorSet.clear();
}


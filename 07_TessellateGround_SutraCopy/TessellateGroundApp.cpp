#include "TessellateGroundApp.h"
#include "VulkanBookUtil.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <glm/gtc/matrix_transform.hpp>

#include "imgui.h"
#include "examples/imgui_impl_vulkan.h"
#include "examples/imgui_impl_glfw.h"

#include <array>

TessellateGroundApp::TessellateGroundApp()
{
	m_camera.SetLookAt(
		glm::vec3(48.5f, 25.0f, 65.0f),
		glm::vec3(0.0f, 0.0f, 0.0f)
	);
}

void TessellateGroundApp::Prepare()
{
	CreateSampleLayouts();

	VkRenderPass renderPass = CreateRenderPass(m_swapchain->GetSurfaceFormat().format, VK_FORMAT_D32_SFLOAT);
	RegisterRenderPass("default", renderPass);

	PrepareDepthbuffer();

	PrepareFramebuffers();

	uint32_t imageCount = m_swapchain->GetImageCount();
	m_commandBuffers.resize(imageCount);

	for (FrameCommandBuffer& c : m_commandBuffers)
	{
		c.fence = CreateFence();
		c.commandBuffer = CreateCommandBuffer(false); // �R�}���h�o�b�t�@�͊J�n��Ԃɂ��Ȃ�
	}

	PrepareSceneResource();
	PreparePrimitiveResource();
}

void TessellateGroundApp::Cleanup()
{
	DestroyBuffer(m_quad.resVertexBuffer);
	DestroyBuffer(m_quad.resIndexBuffer);
	vkDestroySampler(m_device, m_texSampler, nullptr);

	DestroyImage(m_normalMap);
	DestroyImage(m_heightMap);

	// CenterTeapot
	{
		vkDestroyPipeline(m_device, m_tessGroundPipeline, nullptr);
		vkDestroyPipeline(m_device, m_tessGroundWired, nullptr);

		for (const BufferObject& ubo : m_tessUniform)
		{
			DestroyBuffer(ubo);
		}
		m_tessUniform.clear();

		for (const VkDescriptorSet& ds : m_dsTessSample)
		{
			// vkFreeDescriptorSets�ŕ�������x�ɉ���ł��邪�������֐��Ƃ̑Ώ̐����d�񂶂�
			DeallocateDescriptorset(ds);
		}
		m_dsTessSample.clear();
	}

	DestroyImage(m_depthBuffer);
	uint32_t count = uint32_t(m_framebuffers.size());
	DestroyFramebuffers(count, m_framebuffers.data());
	m_framebuffers.clear();

	for (const FrameCommandBuffer& c : m_commandBuffers)
	{
		DestroyCommandBuffer(c.commandBuffer);
		DestroyFence(c.fence);
	}
	m_commandBuffers.clear();
}

bool TessellateGroundApp::OnMouseButtonDown(int button)
{
	if (VulkanAppBase::OnMouseButtonDown(button))
	{
		return true;
	}

	m_camera.OnMouseButtonDown(button);
	return true;
}

bool TessellateGroundApp::OnMouseButtonUp(int button)
{
	if (VulkanAppBase::OnMouseButtonUp(button))
	{
		return true;
	}

	m_camera.OnMouseButtonUp();
	return true;
}

bool TessellateGroundApp::OnMouseMove(int dx, int dy)
{
	if (VulkanAppBase::OnMouseMove(dx, dy))
	{
		return true;
	}

	m_camera.OnMouseMove(dx, dy);
	return true;
}

void TessellateGroundApp::Render()
{
	if (m_isMinimizedWindow)
	{
		MsgLoopMinimizedWindow();
	}

	VkResult result = m_swapchain->AcquireNextImage(&m_imageIndex, m_presentCompletedSem);
	if (result == VK_ERROR_OUT_OF_DATE_KHR)
	{
		return;
	}

	{
		// �����e�B�[�|�b�g�p�̒萔�o�b�t�@�ւ̒l�̐ݒ�
		TessellationShaderParameters tessParams{};
		tessParams.world = glm::mat4(1.0f);
		tessParams.view = m_camera.GetViewMatrix();
		const VkExtent2D& extent = m_swapchain->GetSurfaceExtent();
		tessParams.proj = glm::perspectiveRH(
			glm::radians(45.0f),
			float(extent.width) / float(extent.height),
			0.1f,
			1000.0f
		);
		tessParams.lightDir = glm::vec4(0.0f, 10.0f, 10.0f, 0.0f);
		tessParams.cameraPos = glm::vec4(m_camera.GetPosition(), 1.0f);
		tessParams.tessOuterLevel = m_tessFactor;
		tessParams.tessInnerLevel = m_tessFactor;
		WriteToHostVisibleMemory(m_tessUniform[m_imageIndex].memory, sizeof(tessParams), &tessParams);
	}

	std::array<VkClearValue, 2> clearValue = {
		{
			{0.25f, 0.25f, 0.25f, 0.0f}, // for Color
			{1.0f, 0}, // for Depth
		}
	};

	VkRect2D renderArea{};
	renderArea.offset = VkOffset2D{ 0, 0 };
	renderArea.extent = m_swapchain->GetSurfaceExtent();

	VkRenderPassBeginInfo rpBI{};
	rpBI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rpBI.pNext = nullptr;
	rpBI.renderPass = GetRenderPass("default");
	rpBI.framebuffer = m_framebuffers[m_imageIndex];
	rpBI.renderArea = renderArea;
	rpBI.clearValueCount = uint32_t(clearValue.size());
	rpBI.pClearValues = clearValue.data();

	VkCommandBufferBeginInfo commandBI{};
	commandBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	commandBI.pNext = nullptr;
	commandBI.flags = 0;
	commandBI.pInheritanceInfo = nullptr;

	const VkFence& fence = m_commandBuffers[m_imageIndex].fence;
	result = vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX);
	ThrowIfFailed(result, "vkWaitForFences Failed.");

	const VkCommandBuffer& command = m_commandBuffers[m_imageIndex].commandBuffer;
	result = vkBeginCommandBuffer(command, &commandBI);
	ThrowIfFailed(result, "vkBeginCommandBuffer Failed.");

	vkCmdBeginRenderPass(command, &rpBI, VK_SUBPASS_CONTENTS_INLINE);

	RenderToMain(command);
	RenderHUD(command);

	vkCmdEndRenderPass(command);

	result = vkEndCommandBuffer(command);
	ThrowIfFailed(result, "vkEndCommandBuffer Failed.");

	// �R�}���h�o�b�t�@���s
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

	result = vkResetFences(m_device, 1, &fence);
	ThrowIfFailed(result, "vkResetFences Failed.");
	result = vkQueueSubmit(m_deviceQueue, 1, &submitInfo, fence);
	ThrowIfFailed(result, "vkQueueSubmit Failed.");

	m_swapchain->QueuePresent(m_deviceQueue, m_imageIndex, m_renderCompletedSem);
}

void TessellateGroundApp::RenderToMain(const VkCommandBuffer& command)
{
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

	// �����e�B�[�|�b�g�̕`��
	if (m_isWireframe)
	{
		vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_tessGroundWired);
	}
	else
	{
		vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_tessGroundPipeline);
	}

	VkDescriptorSet ds = m_dsTessSample[m_imageIndex];

	VkPipelineLayout pipelineLayout = GetPipelineLayout("u1t2");
	vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &ds, 0, nullptr);
	vkCmdBindIndexBuffer(command, m_quad.resIndexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
	VkDeviceSize offsets[] = {0};
	vkCmdBindVertexBuffers(command, 0, 1, &m_quad.resVertexBuffer.buffer, offsets);
	vkCmdDrawIndexed(command, m_quad.indexCount, 1, 0, 0, 0);
}

void TessellateGroundApp::RenderHUD(const VkCommandBuffer& command)
{
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	// ImGui�E�B�W�F�b�g��`�悷��
	ImGui::Begin("Information");
	ImGui::Text("Framerate %.1f FPS", ImGui::GetIO().Framerate);
	ImGui::SliderFloat("TessFactor", &m_tessFactor, 1.0f, 32.0f, "%.1f");
	ImGui::Checkbox("WireFrame", &m_isWireframe);
	ImGui::End();

	ImGui::Render();
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command);
}

void TessellateGroundApp::PrepareDepthbuffer()
{
	// �f�v�X�o�b�t�@����������
	const VkExtent2D& extent = m_swapchain->GetSurfaceExtent();
	m_depthBuffer = CreateImage(extent.width, extent.height, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

void TessellateGroundApp::PrepareFramebuffers()
{
	uint32_t imageCount = m_swapchain->GetImageCount();
	const VkExtent2D& extent = m_swapchain->GetSurfaceExtent();
	VkRenderPass renderPass = GetRenderPass("default");

	m_framebuffers.resize(imageCount);
	for (uint32_t i = 0; i < imageCount; i++)
	{
		std::vector<VkImageView> views;
		views.push_back(m_swapchain->GetImageView(i));
		views.push_back(m_depthBuffer.view);

		m_framebuffers[i] = CreateFramebuffer(renderPass, extent.width, extent.height, uint32_t(views.size()), views.data());
	}
}

bool TessellateGroundApp::OnSizeChanged(uint32_t width, uint32_t height)
{
	bool isResized = VulkanAppBase::OnSizeChanged(width, height);
	if (isResized)
	{
		// �Â��f�v�X�o�b�t�@��j��
		DestroyImage(m_depthBuffer);

		// �Â��t���[���o�b�t�@��j��
		DestroyFramebuffers(uint32_t(m_framebuffers.size()), m_framebuffers.data());

		// �V�𑜓x�ł̃f�v�X�o�b�t�@�쐬
		PrepareDepthbuffer();

		// �V�𑜓x�ł̃t���[���o�b�t�@���쐬
		PrepareFramebuffers();
	}

	return isResized;
}

void TessellateGroundApp::PrepareSceneResource()
{
	// �T���v���[�̏����B�����2D�e�N�X�`���̂Ƃ��Ɖ����ς��Ȃ�
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
	samplerCI.maxLod = 1.0f;
	samplerCI.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	samplerCI.unnormalizedCoordinates = VK_FALSE;
	VkResult result = vkCreateSampler(m_device, &samplerCI, nullptr, &m_texSampler);
	ThrowIfFailed(result, "vkCreateSampler Failed.");

	m_heightMap = Load2DTextureFromFile("heightmap.png");
	m_normalMap = Load2DTextureFromFile("normalmap.png");
}

VulkanAppBase::ImageObject TessellateGroundApp::Load2DTextureFromFile(const char* fileName)
{
	int width, height = 0;
	stbi_uc* rawimage = { nullptr };
	rawimage  = stbi_load(fileName, &width, &height, nullptr, 4);

	VkImageCreateInfo imageCI{};
	imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageCI.pNext = nullptr;
	imageCI.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT; // Cubemap�Ƃ��Ĉ�������
	imageCI.imageType = VK_IMAGE_TYPE_2D;
	imageCI.format = VK_FORMAT_R8G8B8A8_UNORM;
	imageCI.extent.width = width;
	imageCI.extent.height = height;
	imageCI.extent.depth = 1;
	imageCI.mipLevels = 1;
	imageCI.arrayLayers = 1;
	imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
	imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageCI.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageCI.queueFamilyIndexCount = 0;
	imageCI.pQueueFamilyIndices = nullptr;
	imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VkImage image = VK_NULL_HANDLE;
	VkResult result = vkCreateImage(m_device, &imageCI, nullptr, &image);
	ThrowIfFailed(result, "vkCreateImage Failed.");

	VkMemoryRequirements reqs;
	vkGetImageMemoryRequirements(m_device, image, &reqs);

	VkMemoryAllocateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	info.pNext = nullptr;
	info.allocationSize = reqs.size;
	info.memoryTypeIndex = GetMemoryTypeIndex(reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VkDeviceMemory memory = VK_NULL_HANDLE;
	result = vkAllocateMemory(m_device, &info, nullptr, &memory);
	ThrowIfFailed(result, "vkAllocateMemory Failed.");
	result = vkBindImageMemory(m_device, image, memory, 0);
	ThrowIfFailed(result, "vkBindImageMemory Failed.");

	VkImageAspectFlags imageAspect = VK_IMAGE_ASPECT_COLOR_BIT;

	VkImageViewCreateInfo viewCI{};
	viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewCI.pNext = nullptr;
	viewCI.flags = 0;
	viewCI.image = image;
	viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewCI.format = imageCI.format;
	viewCI.components = book_util::DefaultComponentMapping();
	viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewCI.subresourceRange.baseMipLevel = 0;
	viewCI.subresourceRange.levelCount = 1;
	viewCI.subresourceRange.baseArrayLayer = 0;
	viewCI.subresourceRange.layerCount = 1;

	VkImageView view = VK_NULL_HANDLE;
	result = vkCreateImageView(m_device, &viewCI, nullptr, &view);
	ThrowIfFailed(result, "vkCreateImageView Failed.");

	// �X�e�[�W���O�p����
	uint32_t bufferSize = uint32_t(width * height * sizeof(uint32_t));
	BufferObject bufferSrc;
	bufferSrc = CreateBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	WriteToHostVisibleMemory(bufferSrc.memory, bufferSize, rawimage);

	const VkCommandBuffer& command = CreateCommandBuffer();

	VkImageMemoryBarrier imb{};
	imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	imb.pNext = nullptr;
	imb.srcAccessMask = 0;
	imb.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	imb.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imb.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	imb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	imb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	imb.image = image;
	imb.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	imb.subresourceRange.baseMipLevel = 0;
	imb.subresourceRange.levelCount = 1;
	imb.subresourceRange.baseArrayLayer = 0;
	imb.subresourceRange.layerCount = 1;

	vkCmdPipelineBarrier(
		command,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		0,
		0, // memoryBarrierCount
		nullptr,
		0, // bufferMemoryBarrierCount
		nullptr,
		1, // imageMemoryBarrierCount
		&imb
	);

	VkBufferImageCopy region{};
	region.imageExtent.width = uint32_t(width);
	region.imageExtent.height = uint32_t(height);
	region.imageExtent.depth = 1;
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = 0;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount = 1;

	vkCmdCopyBufferToImage(
		command,
		bufferSrc.buffer,
		image,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1,
		&region
	);

	imb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	imb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	imb.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	imb.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	vkCmdPipelineBarrier(
		command,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		0,
		0, // memoryBarrierCount
		nullptr,
		0, // bufferMemoryBarrierCount
		nullptr,
		1, // imageMemoryBarrierCount
		&imb
	);

	FinishCommandBuffer(command);
	DestroyCommandBuffer(command);

	stbi_image_free(rawimage);
	DestroyBuffer(bufferSrc);

	ImageObject texture;
	texture.image = image;
	texture.memory = memory;
	texture.view = view;
	return texture;
}

void TessellateGroundApp::PreparePrimitiveResource()
{
	using namespace glm;
	using VertexData = std::vector<Vertex>;
	using IndexData = std::vector<uint32_t>;
	const float edge = 200.0f;
	const int divide = 10;

	VertexData vertices;
	for (int z = 0; z < divide + 1; ++z)
	{
		for (int x = 0; x < divide + 1; ++x)
		{
			Vertex v;
			v.Position = vec3(
				edge * x / divide,
				0.0f,
				edge * z / divide
			);
			v.UV = vec2(
				v.Position.x / edge,
				v.Position.y / edge
			);

			vertices.push_back(v);
		}
	}

	// �C���f�b�N�X�͂��̂悤�Ȃ���
	// 0     1     2
	// |------------//
	// |    /|    /|
	// |   / |   / |
	// |  /  |  /  |
	// | /   | /   |
	// |/    |/    |
	// |------------//
	// 11    12    13
	//
	IndexData indices;
	for (int z = 0; z < divide; ++z)
	{
		for (int x = 0; x < divide; ++x)
		{
			const int rows = divide + 1;
			int v0 = x, v1 = x + 1;
			v0 = v0 + rows * z;
			v1 = v1 + rows * z;
#if 0 // TODO:�܂���TringleList��
			indices.push_back(v0);
			indices.push_back(v1);
			indices.push_back(v0 + rows);
			indices.push_back(v1 + rows);
#else
			indices.push_back(v0);
			indices.push_back(v1);
			indices.push_back(v0 + rows);

			indices.push_back(v1 + rows);
			indices.push_back(v0 + rows);
			indices.push_back(v1);
#endif
		}
	}

	// ���S�␳
	for (Vertex& v : vertices)
	{
		v.Position.x -= edge * 0.5f;
		v.Position.y -= edge * 0.5f;
	}

	m_quad = CreateSimpleModel(vertices, indices);

	// �f�B�X�N���v�^�Z�b�g�̍쐬
	const VkDescriptorSetLayout& dsLayout = GetDescriptorSetLayout("u1t2");
	uint32_t imageCount = m_swapchain->GetImageCount();

	uint32_t bufferSize = uint32_t(sizeof(TessellationShaderParameters));
	m_tessUniform = CreateUniformBuffers(bufferSize, imageCount);

	m_dsTessSample.resize(imageCount);
	for (uint32_t i = 0; i < imageCount; ++i)
	{
		const VkDescriptorSet& ds = AllocateDescriptorset(dsLayout);
		m_dsTessSample[i] = ds;

		VkDescriptorBufferInfo bufferInfo{};
		bufferInfo.buffer = m_tessUniform[i].buffer;
		bufferInfo.offset = 0;
		bufferInfo.range = VK_WHOLE_SIZE;

		VkDescriptorImageInfo imageInfo{};
		imageInfo.sampler = m_texSampler;
		imageInfo.imageView = m_heightMap.view;
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkDescriptorImageInfo imageInfo2{};
		imageInfo2.sampler = m_texSampler;
		imageInfo2.imageView = m_normalMap.view;
		imageInfo2.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		std::vector<VkWriteDescriptorSet> writeDS =
		{
			book_util::CreateWriteDescriptorSet(ds, 0, &bufferInfo),
			book_util::CreateWriteDescriptorSet(ds, 1, &imageInfo),
			book_util::CreateWriteDescriptorSet(ds, 2, &imageInfo2)
		};

		vkUpdateDescriptorSets(m_device, uint32_t(writeDS.size()), writeDS.data(), 0, nullptr);
	}

	// ���_�̓��͂̐ݒ�
	uint32_t stride = uint32_t(sizeof(Vertex));
	VkVertexInputBindingDescription vibDesc{};
	vibDesc.binding = 0;
	vibDesc.stride = stride;
	vibDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	std::array<VkVertexInputAttributeDescription, 2> inputAttribs{};
	inputAttribs[0].location = 0;
	inputAttribs[0].binding = 0;
	inputAttribs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	inputAttribs[0].offset = offsetof(Vertex, Position);
	inputAttribs[1].location = 1;
	inputAttribs[1].binding = 0;
	inputAttribs[1].format = VK_FORMAT_R32G32_SFLOAT;
	inputAttribs[1].offset = offsetof(Vertex, UV);

	VkPipelineVertexInputStateCreateInfo pipelineVisCI{};
	pipelineVisCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	pipelineVisCI.pNext = nullptr;
	pipelineVisCI.flags = 0;
	pipelineVisCI.vertexBindingDescriptionCount = 1;
	pipelineVisCI.pVertexBindingDescriptions = &vibDesc;
	pipelineVisCI.vertexAttributeDescriptionCount = uint32_t(inputAttribs.size());
	pipelineVisCI.pVertexAttributeDescriptions = inputAttribs.data();

	const VkPipelineColorBlendAttachmentState& blendAttachmentState = book_util::GetOpaqueColorBlendAttachmentState();

	VkPipelineColorBlendStateCreateInfo colorBlendStateCI{};
	colorBlendStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlendStateCI.pNext = nullptr;
	colorBlendStateCI.flags = 0;
	colorBlendStateCI.logicOpEnable = VK_FALSE;
	colorBlendStateCI.logicOp = VK_LOGIC_OP_CLEAR;
	colorBlendStateCI.attachmentCount = 1;
	colorBlendStateCI.pAttachments = &blendAttachmentState;
	colorBlendStateCI.blendConstants[0] = 0.0f;
	colorBlendStateCI.blendConstants[1] = 0.0f;
	colorBlendStateCI.blendConstants[2] = 0.0f;
	colorBlendStateCI.blendConstants[3] = 0.0f;

	VkPipelineInputAssemblyStateCreateInfo inputAssemblyCI{};
	inputAssemblyCI.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssemblyCI.pNext = nullptr;
	inputAssemblyCI.flags = 0;
#if 0 // TODO:�܂���TringleList��
	inputAssemblyCI.topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
#else
	inputAssemblyCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
#endif
	inputAssemblyCI.primitiveRestartEnable = VK_FALSE;

#if 0 // TODO:�܂���TringleList��
	VkPipelineTessellationStateCreateInfo tessStateCI{};
	tessStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
	tessStateCI.pNext = nullptr;
	tessStateCI.flags = 0;
	tessStateCI.patchControlPoints = 4;
#endif

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
	
	const VkExtent2D& extentBackBuffer = m_swapchain->GetSurfaceExtent();
	const VkViewport& viewport = book_util::GetViewportFlipped(float(extentBackBuffer.width), float(extentBackBuffer.height));

	VkRect2D scissor{};
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	scissor.extent = extentBackBuffer;

	VkPipelineViewportStateCreateInfo viewportStateCI{};
	viewportStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportStateCI.pNext = nullptr;
	viewportStateCI.flags = 0;
	viewportStateCI.viewportCount = 1;
	viewportStateCI.pViewports = &viewport;
	viewportStateCI.scissorCount = 1;
	viewportStateCI.pScissors = &scissor;

	VkPipelineRasterizationStateCreateInfo rasterizerState = book_util::GetDefaultRasterizerState(VK_CULL_MODE_BACK_BIT); // �o�b�N�t�F�C�X�J�����O����

	const VkPipelineDepthStencilStateCreateInfo& dsState = book_util::GetDefaultDepthStencilState();

	// DynamicState
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

	std::vector<VkPipelineShaderStageCreateInfo> shaderStages
	{
		book_util::LoadShader(m_device, "tessTeapotVS.spv", VK_SHADER_STAGE_VERTEX_BIT),
#if 0 // TODO:�܂���TringleList��
		book_util::LoadShader(m_device, "tessTeapotTCS.spv", VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT),
		book_util::LoadShader(m_device, "tessTeapotTES.spv", VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT),
#endif
		book_util::LoadShader(m_device, "tessTeapotFS.spv", VK_SHADER_STAGE_FRAGMENT_BIT),
	};

	// �p�C�v���C���\�z
	VkGraphicsPipelineCreateInfo pipelineCI{};
	pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineCI.pNext = nullptr;
	pipelineCI.flags = 0;
	pipelineCI.stageCount = uint32_t(shaderStages.size());
	pipelineCI.pStages = shaderStages.data();
	pipelineCI.pVertexInputState = &pipelineVisCI;
	pipelineCI.pInputAssemblyState = &inputAssemblyCI;
#if 0 // TODO:�܂���TringleList��
	pipelineCI.pTessellationState = &tessStateCI;
#else
	pipelineCI.pTessellationState = nullptr;
#endif
	pipelineCI.pViewportState = &viewportStateCI;
	pipelineCI.pRasterizationState = &rasterizerState;
	pipelineCI.pMultisampleState = &multisampleCI;
	pipelineCI.pDepthStencilState = &dsState;
	pipelineCI.pColorBlendState = &colorBlendStateCI;
	pipelineCI.pDynamicState = &pipelineDynamicStateCI;
	pipelineCI.layout = GetPipelineLayout("u1t2");
	pipelineCI.renderPass = GetRenderPass("default");
	pipelineCI.subpass = 0;
	pipelineCI.basePipelineHandle = VK_NULL_HANDLE;
	pipelineCI.basePipelineIndex = 0;

	VkResult result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &m_tessGroundPipeline);
	ThrowIfFailed(result, "vkCreateGraphicsPipelines Failed.");

	// ���C�A�[�t���[���`��p���쐬
	rasterizerState.polygonMode = VK_POLYGON_MODE_LINE;
	result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &m_tessGroundWired);
	ThrowIfFailed(result, "vkCreateGraphicsPipelines Failed.");

	book_util::DestroyShaderModules(m_device, shaderStages);
}

void TessellateGroundApp::CreateSampleLayouts()
{
	// �f�B�X�N���v�^�Z�b�g���C�A�E�g
	std::array<VkDescriptorSetLayoutBinding, 3> dsLayoutBindings;
	dsLayoutBindings[0].binding = 0;
	dsLayoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	dsLayoutBindings[0].descriptorCount = 1;
	dsLayoutBindings[0].stageFlags = VK_SHADER_STAGE_ALL;
	dsLayoutBindings[0].pImmutableSamplers = nullptr;
	dsLayoutBindings[1].binding = 1;
	dsLayoutBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	dsLayoutBindings[1].descriptorCount = 1;
	dsLayoutBindings[1].stageFlags = VK_SHADER_STAGE_ALL;
	dsLayoutBindings[1].pImmutableSamplers = nullptr;
	dsLayoutBindings[2].binding = 2;
	dsLayoutBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	dsLayoutBindings[2].descriptorCount = 1;
	dsLayoutBindings[2].stageFlags = VK_SHADER_STAGE_ALL;
	dsLayoutBindings[2].pImmutableSamplers = nullptr;

	VkDescriptorSetLayoutCreateInfo descSetLayoutCI{};
	descSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descSetLayoutCI.pNext = nullptr;
	descSetLayoutCI.bindingCount = uint32_t(dsLayoutBindings.size());
	descSetLayoutCI.pBindings = dsLayoutBindings.data();

	VkDescriptorSetLayout dsLayout = VK_NULL_HANDLE;

	VkResult result = vkCreateDescriptorSetLayout(m_device, &descSetLayoutCI, nullptr, &dsLayout);
	ThrowIfFailed(result, "vkCreateDescriptorSetLayout Failed.");
	RegisterLayout("u1t2", dsLayout);

	// �p�C�v���C�����C�A�E�g
	dsLayout = GetDescriptorSetLayout("u1t2");
	VkPipelineLayoutCreateInfo layoutCI{};
	layoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutCI.pNext = nullptr;
	layoutCI.flags = 0;
	layoutCI.setLayoutCount = 1;
	layoutCI.pSetLayouts = &dsLayout;
	layoutCI.pushConstantRangeCount = 0;
	layoutCI.pPushConstantRanges = nullptr;

	VkPipelineLayout layout = VK_NULL_HANDLE;
	result = vkCreatePipelineLayout(m_device, &layoutCI, nullptr, &layout);
	ThrowIfFailed(result, "vkCreatePipelineLayout Failed.");
	RegisterLayout("u1t2", layout);
}


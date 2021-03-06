#include "HelloGeometryShaderApp.h"
#include "VulkanBookUtil.h"
#include "TeapotModelAlignment.h"
#include <array>
#include <glm/gtc/matrix_transform.hpp>
#include "imgui.h"
#include "examples/imgui_impl_vulkan.h"
#include "examples/imgui_impl_glfw.h"

HelloGeometryShaderApp::HelloGeometryShaderApp()
{
	m_camera.SetLookAt(
		glm::vec3(0.0f, 2.0f, 10.0f),
		glm::vec3(0.0f, 0.0f, 0.0f)
	);
}

void HelloGeometryShaderApp::Prepare()
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
		c.commandBuffer = CreateCommandBuffer(false); // コマンドバッファは開始状態にしない
	}

	PrepareTeapot();

	CreatePipeline();

	PrepareComputeResource();
}

void HelloGeometryShaderApp::Cleanup()
{
	for (const BufferObject& ubo : m_uniformBuffers)
	{
		DestroyBuffer(ubo);
	}
	m_uniformBuffers.clear();

	DestroyBuffer(m_counterUBO);

	DestroyBuffer(m_teapot.resVertexBuffer);
	DestroyBuffer(m_teapot.resIndexBuffer);

	vkFreeDescriptorSets(m_device, m_descriptorPool, uint32_t(m_descriptorSets.size()), m_descriptorSets.data());
	m_descriptorSets.clear();

	vkFreeDescriptorSets(m_device, m_descriptorPool, 1, &m_computeDS);

	for (auto& v : m_pipelines)
	{
		vkDestroyPipeline(m_device, v.second, nullptr);
	}
	m_pipelines.clear();

	vkDestroyPipeline(m_device, m_computePipeline, nullptr);

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

bool HelloGeometryShaderApp::OnMouseButtonDown(int button)
{
	if (VulkanAppBase::OnMouseButtonDown(button))
	{
		return true;
	}

	m_camera.OnMouseButtonDown(button);
	return true;
}

bool HelloGeometryShaderApp::OnMouseButtonUp(int button)
{
	if (VulkanAppBase::OnMouseButtonUp(button))
	{
		return true;
	}

	m_camera.OnMouseButtonUp();
	return true;
}

bool HelloGeometryShaderApp::OnMouseMove(int dx, int dy)
{
	if (VulkanAppBase::OnMouseMove(dx, dy))
	{
		return true;
	}

	m_camera.OnMouseMove(dx, dy);
	return true;
}

void HelloGeometryShaderApp::Render()
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

	std::array<VkClearValue, 2> clearValue = {
		{
			{0.85f, 0.5f, 0.5f, 0.0f}, // for Color
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
	rpBI.framebuffer = m_framebuffers[imageIndex];
	rpBI.renderArea = renderArea;
	rpBI.clearValueCount = uint32_t(clearValue.size());
	rpBI.pClearValues = clearValue.data();

	VkCommandBufferBeginInfo commandBI{};
	commandBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	commandBI.pNext = nullptr;
	commandBI.flags = 0;
	commandBI.pInheritanceInfo = nullptr;

	{
		ShaderParameters shaderParam{};
		shaderParam.world = glm::mat4(1.0f);

		shaderParam.view = m_camera.GetViewMatrix();

		const VkExtent2D& extent = m_swapchain->GetSurfaceExtent();
		shaderParam.proj = glm::perspectiveRH(
			glm::radians(45.0f),
			float(extent.width) / float(extent.height),
			0.1f,
			1000.0f
		);

		shaderParam.lightDir = glm::vec4(0.0f, 1.0f, 1.0f, 0.0f);

		const BufferObject& ubo = m_uniformBuffers[imageIndex];
		void* p = nullptr;
		result = vkMapMemory(m_device, ubo.memory, 0, VK_WHOLE_SIZE, 0, &p);
		ThrowIfFailed(result, "vkMapMemory Failed.");
		memcpy(p, &shaderParam, sizeof(ShaderParameters));
		vkUnmapMemory(m_device, ubo.memory);
	}

	{
		void* p = nullptr;
		result = vkMapMemory(m_device, m_counterUBO.memory, 0, VK_WHOLE_SIZE, 0, &p);
		ThrowIfFailed(result, "vkMapMemory Failed.");
		memcpy(p, &m_counter, sizeof(m_counter));
		vkUnmapMemory(m_device, m_counterUBO.memory);

		m_counter++;
	}

	const VkFence& fence = m_commandBuffers[imageIndex].fence;
	result = vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX);
	ThrowIfFailed(result, "vkWaitForFences Failed.");

	const VkCommandBuffer& command = m_commandBuffers[imageIndex].commandBuffer;
	result = vkBeginCommandBuffer(command, &commandBI);
	ThrowIfFailed(result, "vkBeginCommandBuffer Failed.");

	// コンピュートパス
	VkBufferMemoryBarrier barrier;
	barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
	barrier.pNext = nullptr;
	barrier.srcAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.buffer = m_teapot.resVertexBuffer.buffer;
	barrier.offset = 0;
	barrier.size = VK_WHOLE_SIZE;
	
	vkCmdPipelineBarrier(
		command,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		0,
		0, // memoryBarrierCount
		nullptr,
		1, // bufferMemoryBarrierCount
		&barrier,
		0, // imageMemoryBarrierCount
		nullptr
	);

	const VkPipelineLayout& pipelineLayout = GetPipelineLayout("compute");
	vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &m_computeDS, 0, nullptr);
	vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipeline);

	uint32_t groupX = _countof(TeapotModel::TeapotVerticesPN);
	vkCmdDispatch(command, groupX, 1, 1);

	barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
	
	vkCmdPipelineBarrier(
		command,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
		0,
		0, // memoryBarrierCount
		nullptr,
		1, // bufferMemoryBarrierCount
		&barrier,
		0, // imageMemoryBarrierCount
		nullptr
	);

	// グラフィックスパス
	vkCmdBeginRenderPass(command, &rpBI, VK_SUBPASS_CONTENTS_INLINE);

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

	switch (m_mode)
	{
		case DrawMode_Flat:
		{
			// フラットシェーディング
			vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelines[FlatShaderPipeline]);
			const VkPipelineLayout& layout = GetPipelineLayout("u1");
			vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &m_descriptorSets[imageIndex], 0, nullptr);
			vkCmdBindIndexBuffer(command, m_teapot.resIndexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
			VkDeviceSize offsets[] = {0};
			vkCmdBindVertexBuffers(command, 0, 1, &m_teapot.resVertexBuffer.buffer, offsets);
			vkCmdDrawIndexed(command, m_teapot.indexCount, 1, 0, 0, 0);
		}
			break;
		case DrawMode_NormalVector:
		{
			// Lambertシェーディング
			vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelines[SmoothShaderPipeline]);
			const VkPipelineLayout& layout = GetPipelineLayout("u1");
			vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &m_descriptorSets[imageIndex], 0, nullptr);
			vkCmdBindIndexBuffer(command, m_teapot.resIndexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
			VkDeviceSize offsets[] = {0};
			vkCmdBindVertexBuffers(command, 0, 1, &m_teapot.resVertexBuffer.buffer, offsets);
			vkCmdDrawIndexed(command, m_teapot.indexCount, 1, 0, 0, 0);

			// 法線描画
			vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelines[NormalVectorPipeline]);
			vkCmdDrawIndexed(command, m_teapot.indexCount, 1, 0, 0, 0);
		}
			break;
		default:
			assert(false);
			break;
	}

	RenderHUD(command);

	vkCmdEndRenderPass(command);
	result = vkEndCommandBuffer(command);
	ThrowIfFailed(result, "vkEndCommandBuffer Failed.");

	// コマンドバッファ実行
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

	m_swapchain->QueuePresent(m_deviceQueue, imageIndex, m_renderCompletedSem);
}

void HelloGeometryShaderApp::RenderHUD(const VkCommandBuffer& command)
{
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	// ImGuiウィジェットを描画する
	ImGui::Begin("Information");
	ImGui::Text("Framerate %.1f FPS", ImGui::GetIO().Framerate);
	ImGui::Combo("Mode", (int*)&m_mode, "Flat\0NormalVector\0\0");
	ImGui::End();

	ImGui::Render();
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command);
}

void HelloGeometryShaderApp::PrepareDepthbuffer()
{
	// デプスバッファを準備する
	const VkExtent2D& extent = m_swapchain->GetSurfaceExtent();
	m_depthBuffer = CreateImage(extent.width, extent.height, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

void HelloGeometryShaderApp::PrepareFramebuffers()
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

bool HelloGeometryShaderApp::OnSizeChanged(uint32_t width, uint32_t height)
{
	bool isResized = VulkanAppBase::OnSizeChanged(width, height);
	if (isResized)
	{
		// 古いデプスバッファを破棄
		DestroyImage(m_depthBuffer);

		// 古いフレームバッファを破棄
		DestroyFramebuffers(uint32_t(m_framebuffers.size()), m_framebuffers.data());

		// 新解像度でのデプスバッファ作成
		PrepareDepthbuffer();

		// 新解像度でのフレームバッファを作成
		PrepareFramebuffers();
	}

	return isResized;
}

void HelloGeometryShaderApp::PrepareTeapot()
{
	std::vector<TeapotModel::Vertex> vertices(std::begin(TeapotModel::TeapotVerticesPN), std::end(TeapotModel::TeapotVerticesPN));
	std::vector<uint32_t> indices(std::begin(TeapotModel::TeapotIndices), std::end(TeapotModel::TeapotIndices));
	// コンピュートシェーダで加工可能にする
	bool isVBComputable = true;
	m_teapot = CreateSimpleModel(vertices, indices, isVBComputable);

	// ディスクリプタセット
	const VkDescriptorSetLayout& dsLayout = GetDescriptorSetLayout("u1");

	VkDescriptorSetAllocateInfo descriptorSetAI{};
	descriptorSetAI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descriptorSetAI.pNext = nullptr;
	descriptorSetAI.descriptorPool = m_descriptorPool;
	descriptorSetAI.descriptorSetCount = 1;
	descriptorSetAI.pSetLayouts = &dsLayout;

	uint32_t imageCount = m_swapchain->GetImageCount();
	for (uint32_t i = 0; i < imageCount; ++i)
	{
		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
		VkResult result = vkAllocateDescriptorSets(m_device, &descriptorSetAI, &descriptorSet);
		ThrowIfFailed(result, "vkAllocateDescriptorSets Failed.");

		m_descriptorSets.push_back(descriptorSet);
	}

	// 定数バッファの準備
	VkMemoryPropertyFlags uboMemoryProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	m_uniformBuffers.resize(imageCount);

	for (uint32_t i = 0; i < imageCount; ++i)
	{
		uint32_t buffersize = uint32_t(sizeof(ShaderParameters));
		m_uniformBuffers[i] = CreateBuffer(buffersize , VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, uboMemoryProps);
	}

	for (size_t i = 0; i < imageCount; ++i)
	{
		VkDescriptorBufferInfo bufferInfo{};
		bufferInfo.buffer = m_uniformBuffers[i].buffer;
		bufferInfo.offset = 0;
		bufferInfo.range = VK_WHOLE_SIZE;

		VkWriteDescriptorSet writeDescSet{};
		writeDescSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writeDescSet.pNext = nullptr;
		writeDescSet.dstSet = m_descriptorSets[i],
		writeDescSet.dstBinding = 0;
		writeDescSet.dstArrayElement = 0;
		writeDescSet.descriptorCount = 1;
		writeDescSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		writeDescSet.pImageInfo = nullptr;
		writeDescSet.pBufferInfo = &bufferInfo;
		writeDescSet.pTexelBufferView = nullptr;

		vkUpdateDescriptorSets(m_device, 1, &writeDescSet, 0, nullptr);
	}
}

void HelloGeometryShaderApp::CreatePipeline()
{
	// 頂点の入力の設定
	uint32_t stride = uint32_t(sizeof(TeapotModel::Vertex));
	VkVertexInputBindingDescription vibDesc{};
	vibDesc.binding = 0;
	vibDesc.stride = stride;
	vibDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	std::array<VkVertexInputAttributeDescription, 2> inputAttribs{};
	inputAttribs[0].location = 0;
	inputAttribs[0].binding = 0;
	inputAttribs[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
	inputAttribs[0].offset = offsetof(TeapotModel::Vertex, Position);
	inputAttribs[1].location = 1;
	inputAttribs[1].binding = 0;
	inputAttribs[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
	inputAttribs[1].offset = offsetof(TeapotModel::Vertex, Normal);

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
	
	const VkExtent2D& extent = m_swapchain->GetSurfaceExtent();

	const VkViewport& viewport = book_util::GetViewportFlipped(float(extent.width), float(extent.height));

	VkOffset2D offset{};
	offset.x = 0;
	offset.y = 0;
	VkRect2D scissor{};
	scissor.offset = offset;
	scissor.extent = extent;

	VkPipelineViewportStateCreateInfo viewportCI{};
	viewportCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportCI.pNext = nullptr;
	viewportCI.flags = 0;
	viewportCI.viewportCount = 1;
	viewportCI.pViewports = &viewport;
	viewportCI.scissorCount = 1;
	viewportCI.pScissors = &scissor;

	const VkPipelineRasterizationStateCreateInfo& rasterizerState = book_util::GetDefaultRasterizerState();

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

	VkRenderPass renderPass = GetRenderPass("default");

	// パイプライン構築
	const VkPipelineLayout& layout = GetPipelineLayout("u1");

	VkGraphicsPipelineCreateInfo pipelineCI{};
	pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineCI.pNext = nullptr;
	pipelineCI.pVertexInputState = &pipelineVisCI;
	pipelineCI.pInputAssemblyState = &inputAssemblyCI;
	pipelineCI.pTessellationState = nullptr;
	pipelineCI.pViewportState = &viewportCI;
	pipelineCI.pRasterizationState = &rasterizerState;
	pipelineCI.pMultisampleState = &multisampleCI;
	pipelineCI.pDepthStencilState = &dsState;
	pipelineCI.pColorBlendState = &colorBlendStateCI;
	pipelineCI.pDynamicState = &pipelineDynamicStateCI;
	pipelineCI.layout = layout;
	pipelineCI.renderPass = renderPass;
	pipelineCI.subpass = 0;
	pipelineCI.basePipelineHandle = VK_NULL_HANDLE;
	pipelineCI.basePipelineIndex = 0;

	// フラットシェーディング用のパイプラインの構築
	{
		std::vector<VkPipelineShaderStageCreateInfo> shaderStages
		{
			book_util::LoadShader(m_device, "flatVS.spv", VK_SHADER_STAGE_VERTEX_BIT),
			book_util::LoadShader(m_device, "flatGS.spv", VK_SHADER_STAGE_GEOMETRY_BIT),
			book_util::LoadShader(m_device, "flatFS.spv", VK_SHADER_STAGE_FRAGMENT_BIT),
		};

		pipelineCI.stageCount = uint32_t(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();

		VkPipeline pipeline;
		VkResult result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &pipeline);
		ThrowIfFailed(result, "vkCreateGraphicsPipelines Failed.");

		book_util::DestroyShaderModules(m_device, shaderStages);
		m_pipelines[FlatShaderPipeline] = pipeline;
	}

	// 法線描画用のパイプラインの構築
	{
		std::vector<VkPipelineShaderStageCreateInfo> shaderStages
		{
			book_util::LoadShader(m_device, "drawNormalVS.spv", VK_SHADER_STAGE_VERTEX_BIT),
			book_util::LoadShader(m_device, "drawNormalGS.spv", VK_SHADER_STAGE_GEOMETRY_BIT),
			book_util::LoadShader(m_device, "drawNormalFS.spv", VK_SHADER_STAGE_FRAGMENT_BIT),
		};

		pipelineCI.stageCount = uint32_t(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();

		VkPipeline pipeline;
		VkResult result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &pipeline);
		ThrowIfFailed(result, "vkCreateGraphicsPipelines Failed.");

		book_util::DestroyShaderModules(m_device, shaderStages);
		m_pipelines[NormalVectorPipeline] = pipeline;
	}

	// 法線描画用のモデル本体描画パイプラインの構築
	{
		std::vector<VkPipelineShaderStageCreateInfo> shaderStages
		{
			book_util::LoadShader(m_device, "shaderVS.spv", VK_SHADER_STAGE_VERTEX_BIT),
			book_util::LoadShader(m_device, "shaderFS.spv", VK_SHADER_STAGE_FRAGMENT_BIT),
		};

		pipelineCI.stageCount = uint32_t(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();

		VkPipeline pipeline;
		VkResult result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &pipeline);
		ThrowIfFailed(result, "vkCreateGraphicsPipelines Failed.");

		book_util::DestroyShaderModules(m_device, shaderStages);
		m_pipelines[SmoothShaderPipeline] = pipeline;
	}
}

void HelloGeometryShaderApp::CreateSampleLayouts()
{
	// ディスクリプタセットレイアウト
	VkDescriptorSetLayoutBinding descSetLayoutBindings[1];

	VkDescriptorSetLayoutBinding bindingUBO{};
	bindingUBO.binding = 0;
	bindingUBO.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	bindingUBO.stageFlags = VK_SHADER_STAGE_ALL;
	bindingUBO.descriptorCount = 1;
	descSetLayoutBindings[0] = bindingUBO;

	VkDescriptorSetLayoutCreateInfo descSetLayoutCI{};
	descSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descSetLayoutCI.pNext = nullptr;
	descSetLayoutCI.bindingCount = _countof(descSetLayoutBindings);
	descSetLayoutCI.pBindings = descSetLayoutBindings;

	VkDescriptorSetLayout dsLayout = VK_NULL_HANDLE;

	VkResult result = vkCreateDescriptorSetLayout(m_device, &descSetLayoutCI, nullptr, &dsLayout);
	ThrowIfFailed(result, "vkCreateDescriptorSetLayout Failed.");
	RegisterLayout("u1", dsLayout);
	dsLayout = GetDescriptorSetLayout("u1");

	// パイプラインレイアウト
	VkPipelineLayoutCreateInfo pipelineLayoutCI{};
	pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutCI.pNext = nullptr;
	pipelineLayoutCI.flags = 0;
	pipelineLayoutCI.setLayoutCount = 1;
	pipelineLayoutCI.pSetLayouts = &dsLayout;
	pipelineLayoutCI.pushConstantRangeCount = 0;
	pipelineLayoutCI.pPushConstantRanges = nullptr;

	VkPipelineLayout layout = VK_NULL_HANDLE;
	result = vkCreatePipelineLayout(m_device, &pipelineLayoutCI, nullptr, &layout);
	ThrowIfFailed(result, "vkCreatePipelineLayout Failed.");
	RegisterLayout("u1", layout);
}

void HelloGeometryShaderApp::PrepareComputeResource()
{
	// 定数バッファの準備
	VkMemoryPropertyFlags uboMemoryProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	uint32_t buffersize = uint32_t(sizeof(ShaderParameters));
	m_counterUBO = CreateBuffer(buffersize , VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, uboMemoryProps);

	// ディスクリプタセットレイアウト
	std::array<VkDescriptorSetLayoutBinding, 2> dsLayoutBindings;
	dsLayoutBindings[0].binding = 0;
	dsLayoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	dsLayoutBindings[0].descriptorCount = 1;
	dsLayoutBindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	dsLayoutBindings[0].pImmutableSamplers = nullptr;
	dsLayoutBindings[1].binding = 1;
	dsLayoutBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	dsLayoutBindings[1].descriptorCount = 1;
	dsLayoutBindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	dsLayoutBindings[1].pImmutableSamplers = nullptr;

	VkDescriptorSetLayoutCreateInfo descSetLayoutCI{};
	descSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descSetLayoutCI.pNext = nullptr;
	descSetLayoutCI.bindingCount = uint32_t(dsLayoutBindings.size());
	descSetLayoutCI.pBindings = dsLayoutBindings.data();

	VkDescriptorSetLayout dsLayout = VK_NULL_HANDLE;

	VkResult result = vkCreateDescriptorSetLayout(m_device, &descSetLayoutCI, nullptr, &dsLayout);
	ThrowIfFailed(result, "vkCreateDescriptorSetLayout Failed.");
	RegisterLayout("compute", dsLayout);

	// ディスクリプタセット
	VkDescriptorSetAllocateInfo descriptorSetAI{};
	descriptorSetAI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descriptorSetAI.pNext = nullptr;
	descriptorSetAI.descriptorPool = m_descriptorPool;
	descriptorSetAI.descriptorSetCount = 1;
	descriptorSetAI.pSetLayouts = &dsLayout;

	result = vkAllocateDescriptorSets(m_device, &descriptorSetAI, &m_computeDS);
	ThrowIfFailed(result, "vkAllocateDescriptorSets Failed.");

	VkDescriptorBufferInfo bufferSBO{};
	bufferSBO.buffer = m_teapot.resVertexBuffer.buffer;
	bufferSBO.offset = 0;
	bufferSBO.range = VK_WHOLE_SIZE;

	VkWriteDescriptorSet writeDescSetSBO{};
	writeDescSetSBO.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writeDescSetSBO.pNext = nullptr;
	writeDescSetSBO.dstSet = m_computeDS;
	writeDescSetSBO.dstBinding = 0;
	writeDescSetSBO.dstArrayElement = 0;
	writeDescSetSBO.descriptorCount = 1;
	writeDescSetSBO.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	writeDescSetSBO.pImageInfo = nullptr;
	writeDescSetSBO.pBufferInfo = &bufferSBO;
	writeDescSetSBO.pTexelBufferView = nullptr;

	VkDescriptorBufferInfo bufferUBO{};
	bufferUBO.buffer = m_counterUBO.buffer;
	bufferUBO.offset = 0;
	bufferUBO.range = VK_WHOLE_SIZE;

	std::vector<VkWriteDescriptorSet> writeDS =
	{
		writeDescSetSBO,
		book_util::CreateWriteDescriptorSet(m_computeDS, 0, &bufferUBO),
	};

	vkUpdateDescriptorSets(m_device, uint32_t(writeDS.size()), writeDS.data(), 0, nullptr);

	// パイプラインレイアウト
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
	RegisterLayout("compute", layout);

	// パイプラインの作成
	VkPipelineShaderStageCreateInfo computeStage = book_util::LoadShader(m_device, "editvbCS.spv", VK_SHADER_STAGE_COMPUTE_BIT);

	VkComputePipelineCreateInfo pipelineCI{};
	pipelineCI.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	pipelineCI.pNext = nullptr;
	pipelineCI.flags = 0;
	pipelineCI.stage = computeStage;
	pipelineCI.layout = layout;
	pipelineCI.basePipelineHandle = VK_NULL_HANDLE;
	pipelineCI.basePipelineIndex = 0;

	result = vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &m_computePipeline);
	ThrowIfFailed(result, "vkCreateComputePipelines Failed.");

	vkDestroyShaderModule(m_device, computeStage.module, nullptr);
}


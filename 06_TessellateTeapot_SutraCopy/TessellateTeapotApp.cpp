#include "TessellateTeapotApp.h"
#include "VulkanBookUtil.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "TeapotPatch.h"

#include <glm/gtc/matrix_transform.hpp>

#include "imgui.h"
#include "examples/imgui_impl_vulkan.h"
#include "examples/imgui_impl_glfw.h"

#include <array>

TessellateTeapotApp::TessellateTeapotApp()
{
	m_camera.SetLookAt(
		glm::vec3(0.0f, 2.0f, 10.0f),
		glm::vec3(0.0f, 0.0f, 0.0f)
	);
}

void TessellateTeapotApp::Prepare()
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

	PrepareTessTeapot();
}

void TessellateTeapotApp::Cleanup()
{
	DestroyBuffer(m_tessTeapot.resVertexBuffer);
	DestroyBuffer(m_tessTeapot.resIndexBuffer);

	// CenterTeapot
	{
		vkDestroyPipeline(m_device, m_tessTeapotPipeline, nullptr);
		vkDestroyPipeline(m_device, m_tessTeapotWired, nullptr);

		for (const BufferObject& ubo : m_tessTeapotUniform)
		{
			DestroyBuffer(ubo);
		}
		m_tessTeapotUniform.clear();

		for (const VkDescriptorSet& ds : m_dsTeapot)
		{
			// vkFreeDescriptorSetsで複数を一度に解放できるが生成時関数との対称性を重んじて
			DeallocateDescriptorset(ds);
		}
		m_dsTeapot.clear();
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

bool TessellateTeapotApp::OnMouseButtonDown(int button)
{
	if (VulkanAppBase::OnMouseButtonDown(button))
	{
		return true;
	}

	m_camera.OnMouseButtonDown(button);
	return true;
}

bool TessellateTeapotApp::OnMouseButtonUp(int button)
{
	if (VulkanAppBase::OnMouseButtonUp(button))
	{
		return true;
	}

	m_camera.OnMouseButtonUp();
	return true;
}

bool TessellateTeapotApp::OnMouseMove(int dx, int dy)
{
	if (VulkanAppBase::OnMouseMove(dx, dy))
	{
		return true;
	}

	m_camera.OnMouseMove(dx, dy);
	return true;
}

void TessellateTeapotApp::Render()
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
		// 中央ティーポット用の定数バッファへの値の設定
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
		WriteToHostVisibleMemory(m_tessTeapotUniform[m_imageIndex].memory, sizeof(tessParams), &tessParams);
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

	m_swapchain->QueuePresent(m_deviceQueue, m_imageIndex, m_renderCompletedSem);
}

void TessellateTeapotApp::RenderToMain(const VkCommandBuffer& command)
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

	// 中央ティーポットの描画
	if (m_isWireframe)
	{
		vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_tessTeapotWired);
	}
	else
	{
		vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_tessTeapotPipeline);
	}

	VkDescriptorSet ds = m_dsTeapot[m_imageIndex];

	VkPipelineLayout pipelineLayout = GetPipelineLayout("u1");
	vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &ds, 0, nullptr);
	vkCmdBindIndexBuffer(command, m_tessTeapot.resIndexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
	VkDeviceSize offsets[] = {0};
	vkCmdBindVertexBuffers(command, 0, 1, &m_tessTeapot.resVertexBuffer.buffer, offsets);
	vkCmdDrawIndexed(command, m_tessTeapot.indexCount, 1, 0, 0, 0);
}

void TessellateTeapotApp::RenderHUD(const VkCommandBuffer& command)
{
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	// ImGuiウィジェットを描画する
	ImGui::Begin("Information");
	ImGui::Text("Framerate %.1f FPS", ImGui::GetIO().Framerate);
	ImGui::SliderFloat("TessFactor", &m_tessFactor, 1.0f, 32.0f, "%.1f");
	ImGui::Checkbox("WireFrame", &m_isWireframe);
	ImGui::End();

	ImGui::Render();
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command);
}

void TessellateTeapotApp::PrepareDepthbuffer()
{
	// デプスバッファを準備する
	const VkExtent2D& extent = m_swapchain->GetSurfaceExtent();
	m_depthBuffer = CreateImage(extent.width, extent.height, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

void TessellateTeapotApp::PrepareFramebuffers()
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

bool TessellateTeapotApp::OnSizeChanged(uint32_t width, uint32_t height)
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

void TessellateTeapotApp::PrepareTessTeapot()
{
	const std::vector<glm::vec3>& teapotPoints = TeapotPatch::GetTeapotPatchPoints();
	const std::vector<unsigned int>& teapotIndices = TeapotPatch::GetTeapotPatchIndices();

	// ティーポットのモデルをロード
	m_tessTeapot = CreateSimpleModel(teapotPoints, teapotIndices);

	// 頂点の入力の設定
	uint32_t stride = uint32_t(sizeof(TeapotPatch::ControlPoint));
	VkVertexInputBindingDescription vibDesc{};
	vibDesc.binding = 0;
	vibDesc.stride = stride;
	vibDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	std::array<VkVertexInputAttributeDescription, 1> inputAttribs{};
	inputAttribs[0].location = 0;
	inputAttribs[0].binding = 0;
	inputAttribs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	inputAttribs[0].offset = offsetof(TeapotPatch::ControlPoint, Position);

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
	inputAssemblyCI.topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
	inputAssemblyCI.primitiveRestartEnable = VK_FALSE;

	VkPipelineTessellationStateCreateInfo tessStateCI{};
	tessStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
	tessStateCI.pNext = nullptr;
	tessStateCI.flags = 0;
	tessStateCI.patchControlPoints = 16;

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

	VkPipelineRasterizationStateCreateInfo rasterizerState = book_util::GetDefaultRasterizerState();

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
		book_util::LoadShader(m_device, "tessTeapotTCS.spv", VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT),
		book_util::LoadShader(m_device, "tessTeapotTES.spv", VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT),
		book_util::LoadShader(m_device, "tessTeapotFS.spv", VK_SHADER_STAGE_FRAGMENT_BIT),
	};

	// パイプライン構築
	VkGraphicsPipelineCreateInfo pipelineCI{};
	pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineCI.pNext = nullptr;
	pipelineCI.flags = 0;
	pipelineCI.stageCount = uint32_t(shaderStages.size());
	pipelineCI.pStages = shaderStages.data();
	pipelineCI.pVertexInputState = &pipelineVisCI;
	pipelineCI.pInputAssemblyState = &inputAssemblyCI;
	pipelineCI.pTessellationState = &tessStateCI;
	pipelineCI.pViewportState = &viewportStateCI;
	pipelineCI.pRasterizationState = &rasterizerState;
	pipelineCI.pMultisampleState = &multisampleCI;
	pipelineCI.pDepthStencilState = &dsState;
	pipelineCI.pColorBlendState = &colorBlendStateCI;
	pipelineCI.pDynamicState = &pipelineDynamicStateCI;
	pipelineCI.layout = GetPipelineLayout("u1");
	pipelineCI.renderPass = GetRenderPass("default");
	pipelineCI.subpass = 0;
	pipelineCI.basePipelineHandle = VK_NULL_HANDLE;
	pipelineCI.basePipelineIndex = 0;

	VkResult result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &m_tessTeapotPipeline);
	ThrowIfFailed(result, "vkCreateGraphicsPipelines Failed.");

	// ワイアーフレーム表示
	rasterizerState.polygonMode = VK_POLYGON_MODE_LINE;
	result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &m_tessTeapotWired);
	ThrowIfFailed(result, "vkCreateGraphicsPipelines Failed.");

	const VkDescriptorSetLayout& dsLayout = GetDescriptorSetLayout("u1");
	uint32_t imageCount = m_swapchain->GetImageCount();

	uint32_t bufferSize = uint32_t(sizeof(TessellationShaderParameters));
	m_tessTeapotUniform = CreateUniformBuffers(bufferSize, imageCount);

	// ファイルから読み込んだキューブマップを使用して描画するパスのディスクリプタを準備
	m_dsTeapot.resize(imageCount);
	for (uint32_t i = 0; i < imageCount; ++i)
	{
		const VkDescriptorSet& ds = AllocateDescriptorset(dsLayout);
		m_dsTeapot[i] = ds;

		VkDescriptorBufferInfo bufferInfo{};
		bufferInfo.buffer = m_tessTeapotUniform[i].buffer;
		bufferInfo.offset = 0;
		bufferInfo.range = VK_WHOLE_SIZE;

		VkWriteDescriptorSet writeSet = book_util::CreateWriteDescriptorSet(
			ds,
			0,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			&bufferInfo
		);

		vkUpdateDescriptorSets(m_device, 1, &writeSet, 0, nullptr);
	}

	book_util::DestroyShaderModules(m_device, shaderStages);
}

void TessellateTeapotApp::CreateSampleLayouts()
{
	// ディスクリプタセットレイアウト
	VkDescriptorSetLayoutBinding descSetLayoutBinding;

	descSetLayoutBinding.binding = 0;
	descSetLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	descSetLayoutBinding.descriptorCount = 1;
	descSetLayoutBinding.stageFlags = VK_SHADER_STAGE_ALL;
	descSetLayoutBinding.pImmutableSamplers = nullptr;

	VkDescriptorSetLayoutCreateInfo descSetLayoutCI{};
	descSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descSetLayoutCI.pNext = nullptr;
	descSetLayoutCI.bindingCount = 1;
	descSetLayoutCI.pBindings = &descSetLayoutBinding;

	VkDescriptorSetLayout dsLayout = VK_NULL_HANDLE;

	VkResult result = vkCreateDescriptorSetLayout(m_device, &descSetLayoutCI, nullptr, &dsLayout);
	ThrowIfFailed(result, "vkCreateDescriptorSetLayout Failed.");
	RegisterLayout("u1", dsLayout);

	// パイプラインレイアウト
	dsLayout = GetDescriptorSetLayout("u1");
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
	RegisterLayout("u1", layout);
}


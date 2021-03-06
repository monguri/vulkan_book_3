#include "ComputeFilterApp.h"
#include "VulkanBookUtil.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <glm/gtc/matrix_transform.hpp>

#include "imgui.h"
#include "examples/imgui_impl_vulkan.h"
#include "examples/imgui_impl_glfw.h"

#include <array>

using namespace std;

ComputeFilterApp::ComputeFilterApp()
{
	m_camera.SetLookAt(
		glm::vec3(48.5f, 25.0f, 65.0f),
		glm::vec3(0.0f, 0.0f, 0.0f)
	);
}

void ComputeFilterApp::Prepare()
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

	PrepareSceneResource();
	PrepareComputeResource();
	CreatePrimitiveResource();
}

void ComputeFilterApp::CreateSampleLayouts()
{
	// ディスクリプタセットレイアウト
	std::array<VkDescriptorSetLayoutBinding, 2> dsLayoutBindings;
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

	VkDescriptorSetLayoutCreateInfo descSetLayoutCI{};
	descSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descSetLayoutCI.pNext = nullptr;
	descSetLayoutCI.bindingCount = uint32_t(dsLayoutBindings.size());
	descSetLayoutCI.pBindings = dsLayoutBindings.data();

	VkDescriptorSetLayout dsLayout = VK_NULL_HANDLE;

	VkResult result = vkCreateDescriptorSetLayout(m_device, &descSetLayoutCI, nullptr, &dsLayout);
	ThrowIfFailed(result, "vkCreateDescriptorSetLayout Failed.");
	RegisterLayout("u1t1", dsLayout);

	// パイプラインレイアウト
	dsLayout = GetDescriptorSetLayout("u1t1");
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
	RegisterLayout("u1t1", layout);


	// コンピュートシェーダ用のディスクリプタセットレイアウト
	dsLayoutBindings[0].binding = 0;
	dsLayoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; // シェーダストレージバッファテクスチャ
	dsLayoutBindings[0].descriptorCount = 1;
	dsLayoutBindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	dsLayoutBindings[0].pImmutableSamplers = nullptr;
	dsLayoutBindings[1].binding = 1;
	dsLayoutBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; // シェーダストレージバッファテクスチャ
	dsLayoutBindings[1].descriptorCount = 1;
	dsLayoutBindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	dsLayoutBindings[1].pImmutableSamplers = nullptr;

	descSetLayoutCI.bindingCount = uint32_t(dsLayoutBindings.size());
	descSetLayoutCI.pBindings = dsLayoutBindings.data();

	result = vkCreateDescriptorSetLayout(m_device, &descSetLayoutCI, nullptr, &dsLayout);
	ThrowIfFailed(result, "vkCreateDescriptorSetLayout Failed.");
	RegisterLayout("compute_filter", dsLayout);

	// コンピュートシェーダ用のパイプラインレイアウト
	dsLayout = GetDescriptorSetLayout("compute_filter");
	layoutCI.setLayoutCount = 1;
	layoutCI.pSetLayouts = &dsLayout;

	result = vkCreatePipelineLayout(m_device, &layoutCI, nullptr, &layout);
	ThrowIfFailed(result, "vkCreatePipelineLayout Failed.");
	RegisterLayout("compute_filter", layout);
}

void ComputeFilterApp::Cleanup()
{
	DestroyBuffer(m_quad.resVertexBuffer);
	DestroyBuffer(m_quad.resIndexBuffer);

	DestroyBuffer(m_quad2.resVertexBuffer);
	DestroyBuffer(m_quad2.resIndexBuffer);

	vkDestroySampler(m_device, m_texSampler, nullptr);

	DestroyImage(m_sourceBuffer);
	DestroyImage(m_destBuffer);

	vkDestroyPipeline(m_device, m_pipeline, nullptr);
	vkDestroyPipeline(m_device, m_compSepiaPipeline, nullptr);
	vkDestroyPipeline(m_device, m_compSobelPipeline, nullptr);


	for (const BufferObject& ubo : m_shaderUniforms)
	{
		DestroyBuffer(ubo);
	}
	m_shaderUniforms.clear();

	for (std::vector<VkDescriptorSet>& ds : m_dsDrawTextures)
	{
		vkFreeDescriptorSets(m_device, m_descriptorPool, uint32_t(ds.size()), ds.data());
		ds.clear();
	}

	vkFreeDescriptorSets(m_device, m_descriptorPool, 1, &m_dsWriteToTexture);

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

void ComputeFilterApp::Render()
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
		ShaderParameters shaderParams{};
		const VkExtent2D& extent = m_swapchain->GetSurfaceExtent();
		shaderParams.proj = glm::ortho(-640.0f, 640.0f, -360.0f, 360.0f, -100.0f, 100.0f);
		WriteToHostVisibleMemory(m_shaderUniforms[m_imageIndex].memory, sizeof(shaderParams), &shaderParams);
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

	// テクスチャをコンピュートシェーダでアクセス可能にしておく
	// TODO:本だと、この処理は先頭では行わず、メイン描画が終わってから次のフレーム用に行うが、
	// それだと最初のフレームのために初期化時にこの処理をやっておかねばならず無駄なので、ここでは先頭で
	// 行うようにする
	vkCmdPipelineBarrier(
		command,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		0,
		0, // memoryBarrierCount
		nullptr,
		0, // bufferMemoryBarrierCount
		nullptr,
		1, // imageMemoryBarrierCount
		&CreateImageMemoryBarrier(m_sourceBuffer.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL)
	);
	vkCmdPipelineBarrier(
		command,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		0,
		0, // memoryBarrierCount
		nullptr,
		0, // bufferMemoryBarrierCount
		nullptr,
		1, // imageMemoryBarrierCount
		&CreateImageMemoryBarrier(m_destBuffer.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL)
	);

	// グラフィックスキューでコンピュートシェーダを走らせる場合はレンダーパスの外で実行する必要がある
	const VkPipelineLayout& pipelineLayout = GetPipelineLayout("compute_filter");
	vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &m_dsWriteToTexture, 0, nullptr);
	switch (m_selectedFilter)
	{
		case 0:
			vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, m_compSepiaPipeline);
			break;
		case 1:
			vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, m_compSobelPipeline);
			break;
		default:
			assert(false);
			break;
	}
	int groupX = 1280 / 16 + 1;
	int groupY = 720 / 16 + 1;
	vkCmdDispatch(command, groupX, groupY, 1);

	// 書き込んだ内容をテクスチャとしてフラグメントシェーダから参照するためのレイアウト変更
	vkCmdPipelineBarrier(
		command,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		0,
		0, // memoryBarrierCount
		nullptr,
		0, // bufferMemoryBarrierCount
		nullptr,
		1, // imageMemoryBarrierCount
		&CreateImageMemoryBarrier(m_sourceBuffer.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
	);
	vkCmdPipelineBarrier(
		command,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		0,
		0, // memoryBarrierCount
		nullptr,
		0, // bufferMemoryBarrierCount
		nullptr,
		1, // imageMemoryBarrierCount
		&CreateImageMemoryBarrier(m_destBuffer.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
	);

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

void ComputeFilterApp::RenderToMain(const VkCommandBuffer& command)
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

	vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

	VkDeviceSize offsets[] = {0};

	VkPipelineLayout pipelineLayout = GetPipelineLayout("u1t1");
	vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &m_dsDrawTextures[0][m_imageIndex], 0, nullptr);
	vkCmdBindIndexBuffer(command, m_quad.resIndexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
	vkCmdBindVertexBuffers(command, 0, 1, &m_quad.resVertexBuffer.buffer, offsets);
	vkCmdDrawIndexed(command, m_quad.indexCount, 1, 0, 0, 0);

	vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &m_dsDrawTextures[1][m_imageIndex], 0, nullptr);
	vkCmdBindIndexBuffer(command, m_quad2.resIndexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
	vkCmdBindVertexBuffers(command, 0, 1, &m_quad2.resVertexBuffer.buffer, offsets);
	vkCmdDrawIndexed(command, m_quad2.indexCount, 1, 0, 0, 0);
}

void ComputeFilterApp::PrepareDepthbuffer()
{
	// デプスバッファを準備する
	const VkExtent2D& extent = m_swapchain->GetSurfaceExtent();
	m_depthBuffer = CreateImage(extent.width, extent.height, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

void ComputeFilterApp::PrepareFramebuffers()
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

bool ComputeFilterApp::OnSizeChanged(uint32_t width, uint32_t height)
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

void ComputeFilterApp::CreatePrimitiveResource()
{
	// ディスクリプタセットの作成
	const VkDescriptorSetLayout& dsLayout = GetDescriptorSetLayout("u1t1");
	uint32_t imageCount = m_swapchain->GetImageCount();

	VkDescriptorImageInfo textureImage[2];
	textureImage[0].sampler = m_texSampler;
	textureImage[0].imageView = m_sourceBuffer.view;
	textureImage[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	textureImage[1].sampler = m_texSampler;
	textureImage[1].imageView = m_destBuffer.view;
	textureImage[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	for (int type = 0; type < 2; ++type)
	{
		m_dsDrawTextures[type].resize(imageCount);

		for (uint32_t i = 0; i < imageCount; ++i)
		{
			const VkDescriptorSet& ds = AllocateDescriptorset(dsLayout);
			m_dsDrawTextures[type][i] = ds;

			VkDescriptorBufferInfo bufferInfo{};
			bufferInfo.buffer = m_shaderUniforms[i].buffer;
			bufferInfo.offset = 0;
			bufferInfo.range = VK_WHOLE_SIZE;

			std::vector<VkWriteDescriptorSet> writeDS =
			{
				book_util::CreateWriteDescriptorSet(ds, 0, &bufferInfo),
				book_util::CreateWriteDescriptorSet(ds, 1, &textureImage[type])
			};

			vkUpdateDescriptorSets(m_device, uint32_t(writeDS.size()), writeDS.data(), 0, nullptr);
		}
	}

	// 頂点の入力の設定
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
	inputAssemblyCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
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

	std::vector<VkPipelineShaderStageCreateInfo> shaderStages
	{
		book_util::LoadShader(m_device, "shaderVS.spv", VK_SHADER_STAGE_VERTEX_BIT),
		book_util::LoadShader(m_device, "shaderFS.spv", VK_SHADER_STAGE_FRAGMENT_BIT),
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
	pipelineCI.pTessellationState = nullptr;
	pipelineCI.pViewportState = &viewportStateCI;
	pipelineCI.pRasterizationState = &rasterizerState;
	pipelineCI.pMultisampleState = &multisampleCI;
	pipelineCI.pDepthStencilState = &dsState;
	pipelineCI.pColorBlendState = &colorBlendStateCI;
	pipelineCI.pDynamicState = &pipelineDynamicStateCI;
	pipelineCI.layout = GetPipelineLayout("u1t1");
	pipelineCI.renderPass = GetRenderPass("default");
	pipelineCI.subpass = 0;
	pipelineCI.basePipelineHandle = VK_NULL_HANDLE;
	pipelineCI.basePipelineIndex = 0;

	VkResult result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &m_pipeline);
	ThrowIfFailed(result, "vkCreateGraphicsPipelines Failed.");

	book_util::DestroyShaderModules(m_device, shaderStages);
}

void ComputeFilterApp::PrepareSceneResource()
{
	// サンプラーの準備。これは2Dテクスチャのときと何も変わらない
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

	m_sourceBuffer = Load2DTextureFromFile("image.png");
}

VulkanAppBase::ImageObject ComputeFilterApp::Load2DTextureFromFile(const char* fileName, VkImageLayout layout)
{
	int width, height = 0;
	stbi_uc* rawimage = { nullptr };
	rawimage  = stbi_load(fileName, &width, &height, nullptr, 4);

	VkImageCreateInfo imageCI{};
	imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageCI.pNext = nullptr;
	imageCI.flags = 0;
	imageCI.imageType = VK_IMAGE_TYPE_2D;
	imageCI.format = VK_FORMAT_R8G8B8A8_UNORM;
	imageCI.extent.width = width;
	imageCI.extent.height = height;
	imageCI.extent.depth = 1;
	imageCI.mipLevels = 1;
	imageCI.arrayLayers = 1;
	imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
	imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageCI.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT; // コンピュートシェーダにも使えるように
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

	// ステージング用準備
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
	imb.newLayout = layout;

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

void ComputeFilterApp::PrepareComputeResource()
{
	using namespace glm;
	std::vector<Vertex> vertices;
	const float OFFSET = 10.0f;

	// テクスチャを貼り付ける矩形の作成
	vertices = {
		{vec3(-480.0f - OFFSET, -135.0f, 0.0f), vec2(0.0f, 1.0f)},
		{vec3(0.0f - OFFSET, -135.0f, 0.0f), vec2(1.0f, 1.0f)},
		{vec3(-480.0f - OFFSET, 135.0f, 0.0f), vec2(0.0f, 0.0f)},
		{vec3(0.0f - OFFSET, 135.0f, 0.0f), vec2(1.0f, 0.0f)},
	};

	std::vector<uint32_t> indices = {
		0, 1, 2, 3
	};

	m_quad = CreateSimpleModel(vertices, indices);

	vertices = {
		{vec3(+480.0f + OFFSET, -135.0f, 0.0f), vec2(1.0f, 1.0f)},
		{vec3(0.0f + OFFSET, -135.0f, 0.0f), vec2(0.0f, 1.0f)},
		{vec3(+480.0f + OFFSET, 135.0f, 0.0f), vec2(1.0f, 0.0f)},
		{vec3(0.0f + OFFSET, 135.0f, 0.0f), vec2(0.0f, 0.0f)},
	};

	m_quad2 = CreateSimpleModel(vertices, indices);

	// シェーダストレージバッファテクスチャの作成
	int width = 1280, height = 720;
	{
		VkImageCreateInfo imageCI{};
		imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageCI.pNext = nullptr;
		imageCI.flags = 0;
		imageCI.imageType = VK_IMAGE_TYPE_2D;
		imageCI.format = VK_FORMAT_R8G8B8A8_UNORM;
		imageCI.extent.width = width;
		imageCI.extent.height = height;
		imageCI.extent.depth = 1;
		imageCI.mipLevels = 1;
		imageCI.arrayLayers = 1;
		imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCI.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT; // コンピュートシェーダ用にシェーダストレージバッファ
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

		m_destBuffer.image = image;
		m_destBuffer.memory = memory;
		m_destBuffer.view = view;
	}

	uint32_t imageCount = m_swapchain->GetImageCount();
	uint32_t bufferSize = uint32_t(sizeof(ShaderParameters));
	m_shaderUniforms = CreateUniformBuffers(bufferSize, imageCount);

	// ディスクリプタセットの作成
	const VkDescriptorSetLayout& dsLayout = GetDescriptorSetLayout("compute_filter");
	m_dsWriteToTexture = AllocateDescriptorset(dsLayout);

	VkDescriptorImageInfo srcImage{};
	srcImage.sampler = m_texSampler; // サンプラはグラフィックスパイプラインのものと共通でよい
	srcImage.imageView = m_sourceBuffer.view;
	srcImage.imageLayout = VK_IMAGE_LAYOUT_GENERAL; // コンピュートシェーダ用

	VkDescriptorImageInfo dstImage{};
	dstImage.sampler = m_texSampler; // サンプラはグラフィックスパイプラインのものと共通でよい
	dstImage.imageView = m_destBuffer.view;
	dstImage.imageLayout = VK_IMAGE_LAYOUT_GENERAL; // コンピュートシェーダ用

	std::vector<VkWriteDescriptorSet> writeDS =
	{
		book_util::CreateWriteDescriptorSet(m_dsWriteToTexture, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &srcImage), //コンピュートシェーダ用
		book_util::CreateWriteDescriptorSet(m_dsWriteToTexture, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &dstImage) //コンピュートシェーダ用
	};

	vkUpdateDescriptorSets(m_device, uint32_t(writeDS.size()), writeDS.data(), 0, nullptr);

	// パイプラインの作成
	VkPipelineShaderStageCreateInfo computeStage = book_util::LoadShader(m_device, "sepiaCS.spv", VK_SHADER_STAGE_COMPUTE_BIT);

	VkComputePipelineCreateInfo pipelineCI{};
	pipelineCI.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	pipelineCI.pNext = nullptr;
	pipelineCI.flags = 0;
	pipelineCI.stage = computeStage;
	pipelineCI.layout = GetPipelineLayout("compute_filter");
	pipelineCI.basePipelineHandle = VK_NULL_HANDLE;
	pipelineCI.basePipelineIndex = 0;

	VkResult result = vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &m_compSepiaPipeline);
	ThrowIfFailed(result, "vkCreateComputePipelines Failed.");

	vkDestroyShaderModule(m_device, computeStage.module, nullptr);

	computeStage = book_util::LoadShader(m_device, "sobelCS.spv", VK_SHADER_STAGE_COMPUTE_BIT);
	pipelineCI.stage = computeStage;

	result = vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &m_compSobelPipeline);
	ThrowIfFailed(result, "vkCreateComputePipelines Failed.");

	vkDestroyShaderModule(m_device, computeStage.module, nullptr);
}

void ComputeFilterApp::RenderHUD(const VkCommandBuffer& command)
{
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	// ImGuiウィジェットを描画する
	ImGui::Begin("Information");
	ImGui::Text("Framerate %.1f FPS", ImGui::GetIO().Framerate);
	ImGui::Combo("Filter", &m_selectedFilter, "Sepia Filter\0Sobel Filter\0\0");
	ImGui::End();

	ImGui::Render();
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command);
}

VkImageMemoryBarrier ComputeFilterApp::CreateImageMemoryBarrier(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout)
{
	VkImageMemoryBarrier imgageLayoutDst{};
	imgageLayoutDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	imgageLayoutDst.pNext = nullptr;
	imgageLayoutDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	imgageLayoutDst.oldLayout = oldLayout;
	imgageLayoutDst.newLayout = newLayout;
	imgageLayoutDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	imgageLayoutDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	imgageLayoutDst.image = image;
	imgageLayoutDst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	imgageLayoutDst.subresourceRange.baseMipLevel = 0;
	imgageLayoutDst.subresourceRange.levelCount = 1;
	imgageLayoutDst.subresourceRange.baseArrayLayer = 0;
	imgageLayoutDst.subresourceRange.layerCount = 1;

	switch (oldLayout)
	{
		case VK_IMAGE_LAYOUT_UNDEFINED:
			imgageLayoutDst.srcAccessMask = 0;
			break;
		case VK_IMAGE_LAYOUT_GENERAL:
			imgageLayoutDst.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			imgageLayoutDst.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			break;
		default:
			assert(false);
			break;
	}

	switch (newLayout)
	{
		case VK_IMAGE_LAYOUT_GENERAL:
			imgageLayoutDst.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			imgageLayoutDst.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			break;
		default:
			assert(false);
			break;
	}

	return imgageLayoutDst;
}


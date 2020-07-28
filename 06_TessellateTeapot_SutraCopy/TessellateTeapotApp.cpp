#include "TessellateTeapotApp.h"
#include "VulkanBookUtil.h"
#include "TeapotModel.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

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

	renderPass = CreateRenderPass(CubemapFormat, VK_FORMAT_D32_SFLOAT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	RegisterRenderPass("cubemap", renderPass);

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
	
	// レンダーターゲットの準備
	PrepareRenderTargetForMultiPass();
	PrepareRenderTargetForSinglePass();

	PrepareCenterTeapotDescriptos();
	PrepareAroundTeapotDescriptos();

	// ティーポットのモデルをロード
	std::vector<TeapotModel::Vertex> vertices(std::begin(TeapotModel::TeapotVerticesPN), std::end(TeapotModel::TeapotVerticesPN));
	std::vector<uint32_t> indices(std::begin(TeapotModel::TeapotIndices), std::end(TeapotModel::TeapotIndices));
	m_teapot = CreateSimpleModel(vertices, indices);
}

void TessellateTeapotApp::Cleanup()
{
	DestroyBuffer(m_teapot.resVertexBuffer);
	DestroyBuffer(m_teapot.resIndexBuffer);

	// AroundTeapots(Main)
	{
		vkDestroyPipeline(m_device, m_aroundTeapotsToMain.pipeline, nullptr);

		for (const BufferObject& bufferObj : m_aroundTeapotsToMain.cameraViewUniform)
		{
			DestroyBuffer(bufferObj);
		}
		m_aroundTeapotsToMain.cameraViewUniform.clear();

		for (const VkDescriptorSet& ds : m_aroundTeapotsToMain.descriptors)
		{
			// vkFreeDescriptorSetsで複数を一度に解放できるが生成時関数との対称性を重んじて
			DeallocateDescriptorset(ds);
		}
		m_aroundTeapotsToMain.descriptors.clear();
	}

	// AroundTeapots(Face)
	{
		vkDestroyPipeline(m_device, m_aroundTeapotsToFace.pipeline, nullptr);

		for (int face = 0; face < 6; ++face)
		{
			for (const BufferObject& bufferObj : m_aroundTeapotsToFace.cameraViewUniform[face])
			{
				DestroyBuffer(bufferObj);
			}
			m_aroundTeapotsToFace.cameraViewUniform[face].clear();

			for (const VkDescriptorSet& ds : m_aroundTeapotsToFace.descriptors[face])
			{
				// vkFreeDescriptorSetsで複数を一度に解放できるが生成時関数との対称性を重んじて
				DeallocateDescriptorset(ds);
			}
			m_aroundTeapotsToFace.descriptors[face].clear();
		}
	}

	// AroundTeapots(Cube)
	{
		vkDestroyPipeline(m_device, m_aroundTeapotsToCubemap.pipeline, nullptr);

		for (const BufferObject& bufferObj : m_aroundTeapotsToCubemap.cameraViewUniform)
		{
			DestroyBuffer(bufferObj);
		}
		m_aroundTeapotsToCubemap.cameraViewUniform.clear();

		for (const VkDescriptorSet& ds : m_aroundTeapotsToCubemap.descriptors)
		{
			// vkFreeDescriptorSetsで複数を一度に解放できるが生成時関数との対称性を重んじて
			DeallocateDescriptorset(ds);
		}
		m_aroundTeapotsToCubemap.descriptors.clear();
	}

	// CenterTeapot
	{
		vkDestroyPipeline(m_device, m_centerTeapot.pipeline, nullptr);

		for (const BufferObject& ubo : m_centerTeapot.sceneUBO)
		{
			DestroyBuffer(ubo);
		}
		m_centerTeapot.sceneUBO.clear();

		for (const VkDescriptorSet& ds : m_centerTeapot.dsCubemapStatic)
		{
			// vkFreeDescriptorSetsで複数を一度に解放できるが生成時関数との対称性を重んじて
			DeallocateDescriptorset(ds);
		}
		m_centerTeapot.dsCubemapStatic.clear();

		for (const VkDescriptorSet& ds : m_centerTeapot.dsCubemapRendered)
		{
			// vkFreeDescriptorSetsで複数を一度に解放できるが生成時関数との対称性を重んじて
			DeallocateDescriptorset(ds);
		}
		m_centerTeapot.dsCubemapRendered.clear();
	}

	// CubeFaceScene
	{
		for (const VkImageView& view : m_cubeFaceScene.viewFaces)
		{
			vkDestroyImageView(m_device, view, nullptr);
		}

		DestroyImage(m_cubeFaceScene.depth);
		DestroyFramebuffers(_countof(m_cubeFaceScene.fbFaces), m_cubeFaceScene.fbFaces);
	}

	// CubeScene
	{
		vkDestroyImageView(m_device, m_cubeScene.view, nullptr);
		DestroyImage(m_cubeScene.depth);
		DestroyFramebuffers(1, &m_cubeScene.framebuffer);
	}

	DestroyImage(m_staticCubemap);
	DestroyImage(m_cubemapRendered);
	vkDestroySampler(m_device, m_cubemapSampler, nullptr);

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
		shaderParam.lightDir = glm::vec4(0.0f, 10.0f, 10.0f, 0.0f);
		shaderParam.cameraPos = glm::vec4(m_camera.GetPosition(), 1.0f);
		WriteToHostVisibleMemory(m_centerTeapot.sceneUBO[m_imageIndex].memory, sizeof(shaderParam), &shaderParam);

		// 周辺ティーポットのキューブマップへのマルチパス描画用の定数バッファへの値の設定
		// 原点にあるカメラがキューブマップの各面の方を向くようにする
		glm::vec3 eye(0.0f, 0.0f, 0.0f);
		glm::vec3 dir[] = {
			glm::vec3(1.0f, 0.0f, 0.0f),
			glm::vec3(-1.0f, 0.0f, 0.0f),
			glm::vec3(0.0f, 1.0f, 0.0f),
			glm::vec3(0.0f, -1.0f, 0.0f),
			glm::vec3(0.0f, 0.0f, 1.0f),
			glm::vec3(0.0f, 0.0f, -1.0f),
		};
		//TODO:このupの方向についてはとりあえずそういうものかと思うようにする
		glm::vec3 up[] = {
			glm::vec3(0.0f, -1.0f, 0.0f),
			glm::vec3(0.0f, -1.0f, 0.0f),
			glm::vec3(0.0f, 0.0f, 1.0f),
			glm::vec3(0.0f, 0.0f, -1.0f),
			glm::vec3(0.0f, -1.0f, 0.0f),
			glm::vec3(0.0f, -1.0f, 0.0f),
		};
		for (int i = 0; i < 6; ++i)
		{
			ViewProjMatrices matrices;
			matrices.view = glm::lookAt(eye, dir[i], up[i]);
			matrices.proj = glm::perspectiveRH(
				glm::radians(45.0f),
				float(CubeEdge) / float(CubeEdge),
				0.1f,
				100.0f
			);
			matrices.lightDir = shaderParam.lightDir;
			WriteToHostVisibleMemory(m_aroundTeapotsToFace.cameraViewUniform[i][m_imageIndex].memory, sizeof(matrices), &matrices);
		}

		// 周辺ティーポットのメイン描画用の定数バッファへの値の設定
		ViewProjMatrices view;
		view.view = shaderParam.view;
		view.proj = shaderParam.proj;
		view.lightDir = shaderParam.lightDir;
		WriteToHostVisibleMemory(m_aroundTeapotsToMain.cameraViewUniform[m_imageIndex].memory, sizeof(view), &view);

		// 周辺ティーポットのキューブマップへのシングルパス描画用の定数バッファへの値の設定
		MultiViewProjMatrices allViews;
		for (int face = 0; face < 6; ++face)
		{
			allViews.view[face] = glm::lookAt(eye, dir[face], up[face]);
		}
		allViews.proj = glm::perspectiveRH(
			glm::radians(45.0f),
			float(CubeEdge) / float(CubeEdge),
			0.1f,
			100.0f
		);
		allViews.lightDir = shaderParam.lightDir;
		WriteToHostVisibleMemory(m_aroundTeapotsToCubemap.cameraViewUniform[m_imageIndex].memory, sizeof(allViews), &allViews);
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

	// 描画した内容をテクスチャとして使うためのバリアを設定
	BarrierRTToTexture(command);

	vkCmdBeginRenderPass(command, &rpBI, VK_SUBPASS_CONTENTS_INLINE);

	RenderToMain(command);
	RenderHUD(command);

	vkCmdEndRenderPass(command);

	// 次回の描画に備えてバリアを設定
	BarrierTextureToRT(command);

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
	vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_centerTeapot.pipeline);

	VkDescriptorSet ds = VK_NULL_HANDLE;
	switch (m_mode)
	{
		case TessellateTeapotApp::Mode_StaticCubemap:
			ds = m_centerTeapot.dsCubemapStatic[m_imageIndex];
			break;
		case TessellateTeapotApp::Mode_MultiPassCubemap:
		case TessellateTeapotApp::Mode_SinglePassCubemap:
			ds = m_centerTeapot.dsCubemapRendered[m_imageIndex];
			break;
		default:
			assert(false);
			break;
	}

	VkPipelineLayout pipelineLayout = GetPipelineLayout("u1t1");
	vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &ds, 0, nullptr);
	vkCmdBindIndexBuffer(command, m_teapot.resIndexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
	VkDeviceSize offsets[] = {0};
	vkCmdBindVertexBuffers(command, 0, 1, &m_teapot.resVertexBuffer.buffer, offsets);
	vkCmdDrawIndexed(command, m_teapot.indexCount, 1, 0, 0, 0);

	// 周囲の多数のティーポットの描画
	vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_aroundTeapotsToMain.pipeline);
	pipelineLayout = GetPipelineLayout("u2");
	vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &m_aroundTeapotsToMain.descriptors[m_imageIndex], 0, nullptr);
	vkCmdBindIndexBuffer(command, m_teapot.resIndexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
	vkCmdBindVertexBuffers(command, 0, 1, &m_teapot.resVertexBuffer.buffer, offsets);
	vkCmdDrawIndexed(command, m_teapot.indexCount, 6, 0, 0, 0); // 6個描画する
}

void TessellateTeapotApp::RenderHUD(const VkCommandBuffer& command)
{
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	// ImGuiウィジェットを描画する
	ImGui::Begin("Information");
	ImGui::Text("Framerate %.1f FPS", ImGui::GetIO().Framerate);
	ImGui::Combo("Mode", (int*)&m_mode, "Static\0MultiPass\0SinglePass\0\0");
	ImGui::End();

	ImGui::Render();
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command);
}

void TessellateTeapotApp::BarrierRTToTexture(const VkCommandBuffer& command)
{
	VkImageMemoryBarrier imageBarrier{};
	imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	imageBarrier.pNext = nullptr;
	imageBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	imageBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	imageBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	imageBarrier.image = m_cubemapRendered.image;
	imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	imageBarrier.subresourceRange.baseMipLevel = 0;
	imageBarrier.subresourceRange.levelCount = 1;
	imageBarrier.subresourceRange.baseArrayLayer = 0;
	imageBarrier.subresourceRange.layerCount = 6; // キューブマップ

	vkCmdPipelineBarrier(
		command,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		0, // dependencyFlags
		0, nullptr, // memoryBarrier
		0, nullptr, // bufferMemoryBarrier
		1, &imageBarrier // imageMemoryBarrier
	);
}

void TessellateTeapotApp::BarrierTextureToRT(const VkCommandBuffer& command)
{
	VkImageMemoryBarrier imageBarrier{};
	imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	imageBarrier.pNext = nullptr;
	imageBarrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	imageBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	imageBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	imageBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	imageBarrier.image = m_cubemapRendered.image;
	imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	imageBarrier.subresourceRange.baseMipLevel = 0;
	imageBarrier.subresourceRange.levelCount = 1;
	imageBarrier.subresourceRange.baseArrayLayer = 0;
	imageBarrier.subresourceRange.layerCount = 6; // キューブマップ

	vkCmdPipelineBarrier(
		command,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		0, // dependencyFlags
		0, nullptr, // memoryBarrier
		0, nullptr, // bufferMemoryBarrier
		1, &imageBarrier // imageMemoryBarrier
	);
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

void TessellateTeapotApp::PrepareSceneResource()
{
	// 静的なキューブマップの準備
	const char* files[6] = {
		"posx.jpg",
		"negx.jpg",
		"posy.jpg",
		"negy.jpg",
		"posz.jpg",
		"negz.jpg",
	};
	m_staticCubemap = LoadCubeTextureFromFile(files);

	// 描画先となるキューブマップの準備
	VkImageCreateInfo imageCI{};
	imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageCI.pNext = nullptr;
	imageCI.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT; // Cubemapとして扱うため
	imageCI.imageType = VK_IMAGE_TYPE_2D;
	imageCI.format = CubemapFormat;
	imageCI.extent.width = CubeEdge;
	imageCI.extent.height = CubeEdge;
	imageCI.extent.depth = 1;
	imageCI.mipLevels = 1;
	imageCI.arrayLayers = 6; // Cubemapとして扱うため
	imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
	imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageCI.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageCI.queueFamilyIndexCount = 0;
	imageCI.pQueueFamilyIndices = nullptr;
	imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VkResult result = vkCreateImage(m_device, &imageCI, nullptr, &m_cubemapRendered.image);
	ThrowIfFailed(result, "vkCreateImage Failed.");

	m_cubemapRendered.memory = AllocateMemory(m_cubemapRendered.image, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	result = vkBindImageMemory(m_device, m_cubemapRendered.image, m_cubemapRendered.memory, 0);
	ThrowIfFailed(result, "vkBindImageMemory Failed.");

	VkImageAspectFlags imageAspect = VK_IMAGE_ASPECT_COLOR_BIT;

	VkImageViewCreateInfo viewCI{};
	viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewCI.pNext = nullptr;
	viewCI.flags = 0;
	viewCI.image = m_cubemapRendered.image;
	viewCI.viewType = VK_IMAGE_VIEW_TYPE_CUBE; // Cubemap用
	viewCI.format = imageCI.format;
	viewCI.components = book_util::DefaultComponentMapping();
	viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewCI.subresourceRange.baseMipLevel = 0;
	viewCI.subresourceRange.levelCount = 1;
	viewCI.subresourceRange.baseArrayLayer = 0;
	viewCI.subresourceRange.layerCount = 1; // 書き込みなので1

	result = vkCreateImageView(m_device, &viewCI, nullptr, &m_cubemapRendered.view);
	ThrowIfFailed(result, "vkCreateImageView Failed.");

	// 周辺へのティーポットのインスタンス配置用のユニフォームバッファの準備
	// 値を動的に変更しないのでダブルバッファ用に2つ作る必要がない
	uint32_t bufferSize = uint32_t(sizeof(TeapotInstanceParameters));
	m_cubemapEnvUniform = CreateBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	TeapotInstanceParameters params{};
	params.world[0] = glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 0.0f, 0.0f));
	params.world[1] = glm::translate(glm::mat4(1.0f), glm::vec3(-5.0f, 0.0f, 0.0f));
	params.world[2] = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 5.0f, 0.0f));
	params.world[3] = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -5.0f, 0.0f));
	params.world[4] = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 5.0f));
	params.world[5] = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -5.0f));
	params.colors[0] = glm::vec4(0.6f, 1.0f, 0.6f, 1.0f);
	params.colors[1] = glm::vec4(0.0f, 0.75f, 1.0f, 1.0f);
	params.colors[2] = glm::vec4(0.0f, 0.5f, 1.0f, 1.0f);
	params.colors[3] = glm::vec4(0.5f, 0.5f, 0.25f, 1.0f);
	params.colors[4] = glm::vec4(1.0f, 0.1f, 0.6f, 1.0f);
	params.colors[5] = glm::vec4(1.0f, 0.55f, 0.0f, 1.0f);

	WriteToHostVisibleMemory(m_cubemapEnvUniform.memory, bufferSize, &params);

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
	result = vkCreateSampler(m_device, &samplerCI, nullptr, &m_cubemapSampler);
	ThrowIfFailed(result, "vkCreateSampler Failed.");
}

VulkanAppBase::ImageObject TessellateTeapotApp::LoadCubeTextureFromFile(const char* faceFiles[6])
{
	int width, height = 0;
	stbi_uc* faceImages[6] = { nullptr };
	for (int i = 0; i < 6; ++i)
	{
		faceImages[i] = stbi_load(faceFiles[i], &width, &height, nullptr, 4);
	}

	VkImageCreateInfo imageCI{};
	imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageCI.pNext = nullptr;
	imageCI.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT; // Cubemapとして扱うため
	imageCI.imageType = VK_IMAGE_TYPE_2D;
	imageCI.format = CubemapFormat; // 固定
	imageCI.extent.width = width;
	imageCI.extent.height = height;
	imageCI.extent.depth = 1;
	imageCI.mipLevels = 1;
	imageCI.arrayLayers = 6; // Cubemapとして扱うため
	imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
	imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageCI.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageCI.queueFamilyIndexCount = 0;
	imageCI.pQueueFamilyIndices = nullptr;
	imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VkImage cubemapImage = VK_NULL_HANDLE;
	VkDeviceMemory cubemapMemory = VK_NULL_HANDLE;

	VkResult result = vkCreateImage(m_device, &imageCI, nullptr, &cubemapImage);
	ThrowIfFailed(result, "vkCreateImage Failed.");

	VkMemoryRequirements reqs;
	vkGetImageMemoryRequirements(m_device, cubemapImage, &reqs);

	VkMemoryAllocateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	info.pNext = nullptr;
	info.allocationSize = reqs.size;
	info.memoryTypeIndex = GetMemoryTypeIndex(reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	result = vkAllocateMemory(m_device, &info, nullptr, &cubemapMemory);
	ThrowIfFailed(result, "vkAllocateMemory Failed.");
	result = vkBindImageMemory(m_device, cubemapImage, cubemapMemory, 0);
	ThrowIfFailed(result, "vkBindImageMemory Failed.");

	VkImageAspectFlags imageAspect = VK_IMAGE_ASPECT_COLOR_BIT;

	VkImageViewCreateInfo viewCI{};
	viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewCI.pNext = nullptr;
	viewCI.flags = 0;
	viewCI.image = cubemapImage;
	viewCI.viewType = VK_IMAGE_VIEW_TYPE_CUBE; // Cubemap用
	viewCI.format = imageCI.format;
	viewCI.components = book_util::DefaultComponentMapping();
	viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewCI.subresourceRange.baseMipLevel = 0;
	viewCI.subresourceRange.levelCount = 1;
	viewCI.subresourceRange.baseArrayLayer = 0;
	viewCI.subresourceRange.layerCount = 6; // Cubemap用

	VkImageView cubemapView = VK_NULL_HANDLE;
	result = vkCreateImageView(m_device, &viewCI, nullptr, &cubemapView);
	ThrowIfFailed(result, "vkCreateImageView Failed.");

	// ステージング用準備
	uint32_t bufferSize = uint32_t(width * height * sizeof(uint32_t));
	BufferObject bufferSrc[6];
	for (int i = 0; i < 6; ++i)
	{
		bufferSrc[i] = CreateBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		WriteToHostVisibleMemory(bufferSrc[i].memory, bufferSize, faceImages[i]);
	}

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
	imb.image = cubemapImage;
	imb.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	imb.subresourceRange.baseMipLevel = 0;
	imb.subresourceRange.levelCount = 1;
	imb.subresourceRange.baseArrayLayer = 0;
	imb.subresourceRange.layerCount = 6; // Cubemap用

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

	for (int i = 0; i < 6; ++i)
	{
		VkBufferImageCopy region{};
		region.imageExtent.width = uint32_t(width);
		region.imageExtent.height = uint32_t(height);
		region.imageExtent.depth = 1;
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.mipLevel = 0;
		region.imageSubresource.baseArrayLayer = i; // 各バッファ
		region.imageSubresource.layerCount = 1;

		vkCmdCopyBufferToImage(
			command,
			bufferSrc[i].buffer,
			cubemapImage,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1,
			&region
		);
	}

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

	for (int i = 0; i < 6; ++i)
	{
		stbi_image_free(faceImages[i]);
		vkDestroyBuffer(m_device, bufferSrc[i].buffer, nullptr);
		vkFreeMemory(m_device, bufferSrc[i].memory, nullptr);
	}

	ImageObject cubemap;
	cubemap.image = cubemapImage;
	cubemap.memory = cubemapMemory;
	cubemap.view = cubemapView;
	return cubemap;
}

VkPipeline TessellateTeapotApp::CreateRenderTeapotPipeline(
	const std::string& renderPassName,
	uint32_t width,
	uint32_t height,
	const std::string& layoutName,
	const std::vector<VkPipelineShaderStageCreateInfo> shaderStages
)
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
	
	const VkViewport& viewport = book_util::GetViewportFlipped(float(width), float(height));

	VkRect2D scissor{};
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	scissor.extent.width = width;
	scissor.extent.height = height;

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
	pipelineCI.layout = GetPipelineLayout(layoutName);
	pipelineCI.renderPass = GetRenderPass(renderPassName);
	pipelineCI.subpass = 0;
	pipelineCI.basePipelineHandle = VK_NULL_HANDLE;
	pipelineCI.basePipelineIndex = 0;

	VkPipeline pipeline = VK_NULL_HANDLE;
	VkResult result = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &pipeline);
	ThrowIfFailed(result, "vkCreateGraphicsPipelines Failed.");
	return pipeline;
}

void TessellateTeapotApp::PrepareCenterTeapotDescriptos()
{
	const VkDescriptorSetLayout& dsLayout = GetDescriptorSetLayout("u1t1");
	uint32_t imageCount = m_swapchain->GetImageCount();

	uint32_t bufferSize = uint32_t(sizeof(ShaderParameters));
	m_centerTeapot.sceneUBO = CreateUniformBuffers(bufferSize, imageCount);

	// ファイルから読み込んだキューブマップを使用して描画するパスのディスクリプタを準備
	m_centerTeapot.dsCubemapStatic.resize(imageCount);
	for (uint32_t i = 0; i < imageCount; ++i)
	{
		const VkDescriptorSet& ds = AllocateDescriptorset(dsLayout);
		m_centerTeapot.dsCubemapStatic[i] = ds;

		VkDescriptorBufferInfo sceneUBO{};
		sceneUBO.buffer = m_centerTeapot.sceneUBO[i].buffer;
		sceneUBO.offset = 0;
		sceneUBO.range = VK_WHOLE_SIZE;

		VkDescriptorImageInfo staticCubemap{};
		staticCubemap.sampler = m_cubemapSampler;
		staticCubemap.imageView = m_staticCubemap.view;
		staticCubemap.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		std::vector<VkWriteDescriptorSet> writeSet = {
			book_util::CreateWriteDescriptorSet(
				ds,
				0,
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				&sceneUBO
			),
			book_util::CreateWriteDescriptorSet(
				ds,
				1,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				&staticCubemap
			)
		};

		vkUpdateDescriptorSets(m_device, uint32_t(writeSet.size()), writeSet.data(), 0, nullptr);
	}

	// 動的に描画したキューブマップを使用して描画するパスのディスクリプタを準備
	m_centerTeapot.dsCubemapRendered.resize(imageCount);
	for (uint32_t i = 0; i < imageCount; ++i)
	{
		const VkDescriptorSet& ds = AllocateDescriptorset(dsLayout);
		m_centerTeapot.dsCubemapRendered[i] = ds;

		VkDescriptorBufferInfo sceneUBO{};
		sceneUBO.buffer = m_centerTeapot.sceneUBO[i].buffer;
		sceneUBO.offset = 0;
		sceneUBO.range = VK_WHOLE_SIZE;

		VkDescriptorImageInfo renderedCubemap{};
		renderedCubemap.sampler = m_cubemapSampler;
		renderedCubemap.imageView = m_cubemapRendered.view;
		renderedCubemap.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		std::vector<VkWriteDescriptorSet> writeSet = {
			book_util::CreateWriteDescriptorSet(
				ds,
				0,
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				&sceneUBO
			),
			book_util::CreateWriteDescriptorSet(
				ds,
				1,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				&renderedCubemap
			)
		};

		vkUpdateDescriptorSets(m_device, uint32_t(writeSet.size()), writeSet.data(), 0, nullptr);
	}
	std::vector<VkPipelineShaderStageCreateInfo> shaderStages
	{
		book_util::LoadShader(m_device, "shaderVS.spv", VK_SHADER_STAGE_VERTEX_BIT),
		book_util::LoadShader(m_device, "shaderFS.spv", VK_SHADER_STAGE_FRAGMENT_BIT),
	};

	const VkExtent2D& extent = m_swapchain->GetSurfaceExtent();
	m_centerTeapot.pipeline = CreateRenderTeapotPipeline("default", extent.width, extent.height, "u1t1", shaderStages);
	book_util::DestroyShaderModules(m_device, shaderStages);
}

void TessellateTeapotApp::PrepareAroundTeapotDescriptos()
{
	const VkDescriptorSetLayout& dsLayout = GetDescriptorSetLayout("u2");
	uint32_t imageCount = m_swapchain->GetImageCount();

	// キューブマップへマルチパスで描画するための定数バッファを準備
	uint32_t bufferSize = uint32_t(sizeof(ViewProjMatrices));
	for (int face = 0; face < 6; ++face)
	{
		m_aroundTeapotsToFace.cameraViewUniform[face] = CreateUniformBuffers(bufferSize, imageCount);
	}

	// メインの描画パスで描画するための定数バッファを準備
	m_aroundTeapotsToMain.cameraViewUniform = CreateUniformBuffers(bufferSize, imageCount);

	// キューブマップへシングルパスで描画するための定数バッファを準備
	bufferSize = uint32_t(sizeof(MultiViewProjMatrices));
	m_aroundTeapotsToCubemap.cameraViewUniform = CreateUniformBuffers(bufferSize, imageCount);

	// キューブマップへ描画するためのディスクリプタを準備
	for (int face = 0; face < 6; ++face)
	{
		m_aroundTeapotsToFace.descriptors[face].resize(imageCount);
		for (uint32_t i = 0; i < imageCount; ++i)
		{
			const VkDescriptorSet& ds = AllocateDescriptorset(dsLayout);
			m_aroundTeapotsToFace.descriptors[face][i] = ds;

			VkDescriptorBufferInfo instanceUbo{};
			instanceUbo.buffer = m_cubemapEnvUniform.buffer;
			instanceUbo.offset = 0;
			instanceUbo.range = VK_WHOLE_SIZE;

			VkDescriptorBufferInfo viewProjParamUbo{};
			viewProjParamUbo.buffer = m_aroundTeapotsToFace.cameraViewUniform[face][i].buffer;
			viewProjParamUbo.offset = 0;
			viewProjParamUbo.range = VK_WHOLE_SIZE;

			std::vector<VkWriteDescriptorSet> writeSet = {
				book_util::CreateWriteDescriptorSet(
					ds,
					0,
					VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					&instanceUbo
				),
				book_util::CreateWriteDescriptorSet(
					ds,
					1,
					VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
					&viewProjParamUbo
				)
			};

			vkUpdateDescriptorSets(m_device, uint32_t(writeSet.size()), writeSet.data(), 0, nullptr);
		}
	}



	m_aroundTeapotsToCubemap.descriptors.resize(imageCount);
	//m_aroundTeapotsToCubemap.cameraViewUniform.resize(imageCount); // TODO:本ではやってるが、CreateBuffersですでに2個作っているため不要
	for (uint32_t i = 0; i < imageCount; ++i)
	{
		const VkDescriptorSet& ds = AllocateDescriptorset(dsLayout);
		m_aroundTeapotsToCubemap.descriptors[i] = ds;

		VkDescriptorBufferInfo instanceUbo{};
		instanceUbo.buffer = m_cubemapEnvUniform.buffer;
		instanceUbo.offset = 0;
		instanceUbo.range = VK_WHOLE_SIZE;

		VkDescriptorBufferInfo viewProjParamUbo{};
		viewProjParamUbo.buffer = m_aroundTeapotsToCubemap.cameraViewUniform[i].buffer;
		viewProjParamUbo.offset = 0;
		viewProjParamUbo.range = VK_WHOLE_SIZE;

		std::vector<VkWriteDescriptorSet> writeSet = {
			book_util::CreateWriteDescriptorSet(
				ds,
				0,
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				&instanceUbo
			),
			book_util::CreateWriteDescriptorSet(
				ds,
				1,
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				&viewProjParamUbo
			)
		};

		vkUpdateDescriptorSets(m_device, uint32_t(writeSet.size()), writeSet.data(), 0, nullptr);
	}

	// メインの描画パスで描画するためのディスクリプタを準備
	m_aroundTeapotsToMain.descriptors.resize(imageCount);
	for (uint32_t i = 0; i < imageCount; ++i)
	{
		const VkDescriptorSet& ds = AllocateDescriptorset(dsLayout);
		m_aroundTeapotsToMain.descriptors[i] = ds;

		VkDescriptorBufferInfo instanceUbo{};
		instanceUbo.buffer = m_cubemapEnvUniform.buffer;
		instanceUbo.offset = 0;
		instanceUbo.range = VK_WHOLE_SIZE;

		VkDescriptorBufferInfo viewProjParamUbo{};
		viewProjParamUbo.buffer = m_aroundTeapotsToMain.cameraViewUniform[i].buffer;
		viewProjParamUbo.offset = 0;
		viewProjParamUbo.range = VK_WHOLE_SIZE;

		std::vector<VkWriteDescriptorSet> writeSet = {
			book_util::CreateWriteDescriptorSet(
				ds,
				0,
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				&instanceUbo
			),
			book_util::CreateWriteDescriptorSet(
				ds,
				1,
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				&viewProjParamUbo
			)
		};

		vkUpdateDescriptorSets(m_device, uint32_t(writeSet.size()), writeSet.data(), 0, nullptr);
	}

	// マルチ描画パス
	std::vector<VkPipelineShaderStageCreateInfo> shaderStages
	{
		book_util::LoadShader(m_device, "teapotVS.spv", VK_SHADER_STAGE_VERTEX_BIT),
		book_util::LoadShader(m_device, "teapotFS.spv", VK_SHADER_STAGE_FRAGMENT_BIT),
	};
	m_aroundTeapotsToFace.pipeline = CreateRenderTeapotPipeline("cubemap", CubeEdge, CubeEdge, "u2", shaderStages);

	// メイン描画パス。マルチ描画パスとシェーダは変えない
	const VkExtent2D& extent = m_swapchain->GetSurfaceExtent();
	m_aroundTeapotsToMain.pipeline = CreateRenderTeapotPipeline("default", extent.width, extent.height, "u2", shaderStages);
	book_util::DestroyShaderModules(m_device, shaderStages);

	// シングル描画パス。レンダーパスとレイアウトはマルチ描画パスと変えない。レンダーパスは描画先フォーマットやスワップバッファかレンダーターゲットかくらいでしか通常は変わらないもの。
	shaderStages =
	{
		book_util::LoadShader(m_device, "cubemapVS.spv", VK_SHADER_STAGE_VERTEX_BIT),
		book_util::LoadShader(m_device, "cubemapGS.spv", VK_SHADER_STAGE_GEOMETRY_BIT),
		book_util::LoadShader(m_device, "cubemapFS.spv", VK_SHADER_STAGE_FRAGMENT_BIT),
	};
	m_aroundTeapotsToCubemap.pipeline = CreateRenderTeapotPipeline("cubemap", CubeEdge, CubeEdge, "u2", shaderStages);
	book_util::DestroyShaderModules(m_device, shaderStages);
}

void TessellateTeapotApp::PrepareRenderTargetForMultiPass()
{
	VkResult result = VK_SUCCESS;

	// vkImageViewをm_cubemapRenderedのキューブマップとしての
	// VK_IMAGE_VIEW_TYPE_CUBEのひとつでなく
	// 各テクスチャごとのVK_IMAGE_VIEW_TYPE_2Dのものを6つ用意する
	for (int face = 0; face < 6; ++face)
	{
		VkImageViewCreateInfo viewCI{};
		viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewCI.pNext = nullptr;
		viewCI.flags = 0;
		viewCI.image = m_cubemapRendered.image;
		viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewCI.format = CubemapFormat;
		viewCI.components = book_util::DefaultComponentMapping();
		viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewCI.subresourceRange.baseMipLevel = 0;
		viewCI.subresourceRange.levelCount = 1;
		viewCI.subresourceRange.baseArrayLayer = uint32_t(face); // これでキューブマップ内の一面のみ2Dテクスチャビューとして扱う
		viewCI.subresourceRange.layerCount = 1;
		result = vkCreateImageView(m_device, &viewCI, nullptr, &m_cubeFaceScene.viewFaces[face]);
		ThrowIfFailed(result, "vkCreateImageView Failed.");
	}

	// 描画先デプスバッファの準備
	VkImageCreateInfo depthImageCI{};
	depthImageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	depthImageCI.pNext = nullptr;
	depthImageCI.flags = 0;
	depthImageCI.imageType = VK_IMAGE_TYPE_2D;
	depthImageCI.format = VK_FORMAT_D32_SFLOAT;
	depthImageCI.extent.width = CubeEdge;
	depthImageCI.extent.height = CubeEdge;
	depthImageCI.extent.depth = 1;
	depthImageCI.mipLevels = 1;
	depthImageCI.arrayLayers = 1;
	depthImageCI.samples = VK_SAMPLE_COUNT_1_BIT;
	depthImageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
	depthImageCI.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	depthImageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	depthImageCI.queueFamilyIndexCount = 0;
	depthImageCI.pQueueFamilyIndices = nullptr;
	depthImageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	result = vkCreateImage(m_device, &depthImageCI, nullptr, &m_cubeFaceScene.depth.image);
	ThrowIfFailed(result, "vkCreateImage Failed.");

	m_cubeFaceScene.depth.memory = AllocateMemory(m_cubeFaceScene.depth.image, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	result = vkBindImageMemory(m_device, m_cubeFaceScene.depth.image, m_cubeFaceScene.depth.memory, 0);
	ThrowIfFailed(result, "vkBindImageMemory Failed.");

	VkImageViewCreateInfo depthViewCI{};
	depthViewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	depthViewCI.pNext = nullptr;
	depthViewCI.flags = 0;
	depthViewCI.image = m_cubeFaceScene.depth.image;
	depthViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
	depthViewCI.format = depthImageCI.format;
	depthViewCI.components = book_util::DefaultComponentMapping();
	depthViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	depthViewCI.subresourceRange.baseMipLevel = 0;
	depthViewCI.subresourceRange.levelCount = 1;
	depthViewCI.subresourceRange.baseArrayLayer = 0;
	depthViewCI.subresourceRange.layerCount = 1;
	result = vkCreateImageView(m_device, &depthViewCI, nullptr, &m_cubeFaceScene.depth.view);
	ThrowIfFailed(result, "vkCreateImageView Failed.");

	m_cubeFaceScene.renderPass = GetRenderPass("cubemap");

	for (int i = 0; i < 6; ++i)
	{
		std::array<VkImageView, 2> attachments;
		attachments[0] = m_cubeFaceScene.viewFaces[i];
		attachments[1] = m_cubeFaceScene.depth.view;

		VkFramebufferCreateInfo fbCI{};
		fbCI.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbCI.pNext = nullptr;
		fbCI.flags = 0;
		fbCI.renderPass = m_cubeFaceScene.renderPass;
		fbCI.attachmentCount = uint32_t(attachments.size());
		fbCI.pAttachments = attachments.data();
		fbCI.width = CubeEdge;
		fbCI.height = CubeEdge;
		fbCI.layers = 1;

		result = vkCreateFramebuffer(m_device, &fbCI, nullptr, &m_cubeFaceScene.fbFaces[i]);
		ThrowIfFailed(result, "vkCreateFramebuffer Failed.");
	}
}

void TessellateTeapotApp::PrepareRenderTargetForSinglePass()
{
	VkImageViewCreateInfo viewCI{};
	viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewCI.pNext = nullptr;
	viewCI.flags = 0;
	viewCI.image = m_cubemapRendered.image;
	viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D; //ここは2Dでいいらしい。CUBEもあるのに
	viewCI.format = CubemapFormat;
	viewCI.components = book_util::DefaultComponentMapping();
	viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewCI.subresourceRange.baseMipLevel = 0;
	viewCI.subresourceRange.levelCount = 1;
	viewCI.subresourceRange.baseArrayLayer = 0; // 6面を別々に扱うのと違ってここが0になる
	viewCI.subresourceRange.layerCount = 6; // レイヤーの数が6。シェーダでのgl_Layerと対応。
	VkResult result = vkCreateImageView(m_device, &viewCI, nullptr, &m_cubeScene.view);
	ThrowIfFailed(result, "vkCreateImageView Failed.");

	// 描画先デプスバッファの準備
	VkImageCreateInfo depthImageCI{};
	depthImageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	depthImageCI.pNext = nullptr;
	depthImageCI.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT; // DepthもCubemapのサイズ分が必要
	depthImageCI.imageType = VK_IMAGE_TYPE_2D;
	depthImageCI.format = VK_FORMAT_D32_SFLOAT;
	depthImageCI.extent.width = CubeEdge;
	depthImageCI.extent.height = CubeEdge;
	depthImageCI.extent.depth = 1;
	depthImageCI.mipLevels = 1;
	depthImageCI.arrayLayers = 6; // レイヤーの数が6。シェーダでのgl_Layerと対応。
	depthImageCI.samples = VK_SAMPLE_COUNT_1_BIT;
	depthImageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
	depthImageCI.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	depthImageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	depthImageCI.queueFamilyIndexCount = 0;
	depthImageCI.pQueueFamilyIndices = nullptr;
	depthImageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	result = vkCreateImage(m_device, &depthImageCI, nullptr, &m_cubeScene.depth.image);
	ThrowIfFailed(result, "vkCreateImage Failed.");

	m_cubeScene.depth.memory = AllocateMemory(m_cubeScene.depth.image, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	result = vkBindImageMemory(m_device, m_cubeScene.depth.image, m_cubeScene.depth.memory, 0);
	ThrowIfFailed(result, "vkBindImageMemory Failed.");

	VkImageViewCreateInfo depthViewCI{};
	depthViewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	depthViewCI.pNext = nullptr;
	depthViewCI.flags = 0;
	depthViewCI.image = m_cubeScene.depth.image;
	depthViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D; // なぜかviewは常に2D
	depthViewCI.format = depthImageCI.format;
	depthViewCI.components = book_util::DefaultComponentMapping();
	depthViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	depthViewCI.subresourceRange.baseMipLevel = 0;
	depthViewCI.subresourceRange.levelCount = 1;
	depthViewCI.subresourceRange.baseArrayLayer = 0;
	depthViewCI.subresourceRange.layerCount = 6; // レイヤーの数が6。シェーダでのgl_Layerと対応。
	result = vkCreateImageView(m_device, &depthViewCI, nullptr, &m_cubeScene.depth.view);
	ThrowIfFailed(result, "vkCreateImageView Failed.");

	m_cubeScene.renderPass = GetRenderPass("cubemap");

	// 6つの面（VkImageView）へ接続するVkFramebufferを準備
	// VkFramebufferもCubemapに対応したものをがひとつあればいい。
	// レイヤー数だけ6になる
	std::array<VkImageView, 2> attachments;
	attachments[0] = m_cubeScene.view;
	attachments[1] = m_cubeScene.depth.view;

	VkFramebufferCreateInfo fbCI{};
	fbCI.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	fbCI.pNext = nullptr;
	fbCI.flags = 0;
	fbCI.renderPass = m_cubeScene.renderPass;
	fbCI.attachmentCount = uint32_t(attachments.size());
	fbCI.pAttachments = attachments.data();
	fbCI.width = CubeEdge;
	fbCI.height = CubeEdge;
	fbCI.layers = 6; // レイヤーの数が6。シェーダでのgl_Layerと対応。

	result = vkCreateFramebuffer(m_device, &fbCI, nullptr, &m_cubeScene.framebuffer);
	ThrowIfFailed(result, "vkCreateFramebuffer Failed.");
}

void TessellateTeapotApp::CreateSampleLayouts()
{
	// ディスクリプタセットレイアウト
	std::array<VkDescriptorSetLayoutBinding, 2> descSetLayoutBindings;

	// 0: uniformBuffer, 1: texture(+sampler) を使用するシェーダ用レイアウト
	descSetLayoutBindings[0].binding = 0;
	descSetLayoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	descSetLayoutBindings[0].descriptorCount = 1;
	descSetLayoutBindings[0].stageFlags = VK_SHADER_STAGE_ALL;
	descSetLayoutBindings[0].pImmutableSamplers = nullptr;
	descSetLayoutBindings[1].binding = 1;
	descSetLayoutBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descSetLayoutBindings[1].descriptorCount = 1;
	descSetLayoutBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	descSetLayoutBindings[1].pImmutableSamplers = nullptr;

	VkDescriptorSetLayoutCreateInfo descSetLayoutCI{};
	descSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descSetLayoutCI.pNext = nullptr;
	descSetLayoutCI.bindingCount = uint32_t(descSetLayoutBindings.size());
	descSetLayoutCI.pBindings = descSetLayoutBindings.data();

	VkDescriptorSetLayout dsLayout = VK_NULL_HANDLE;

	VkResult result = vkCreateDescriptorSetLayout(m_device, &descSetLayoutCI, nullptr, &dsLayout);
	ThrowIfFailed(result, "vkCreateDescriptorSetLayout Failed.");
	RegisterLayout("u1t1", dsLayout);

	// 0: uniformBuffer, 1: uniformBuffer を使用するシェーダ用レイアウト
	descSetLayoutBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	descSetLayoutBindings[1].stageFlags = VK_SHADER_STAGE_ALL;
	result = vkCreateDescriptorSetLayout(m_device, &descSetLayoutCI, nullptr, &dsLayout);
	ThrowIfFailed(result, "vkCreateDescriptorSetLayout Failed.");
	RegisterLayout("u2", dsLayout);

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

	dsLayout = GetDescriptorSetLayout("u2");
	layoutCI.pSetLayouts = &dsLayout;

	result = vkCreatePipelineLayout(m_device, &layoutCI, nullptr, &layout);
	ThrowIfFailed(result, "vkCreatePipelineLayout Failed.");
	RegisterLayout("u2", layout);
}


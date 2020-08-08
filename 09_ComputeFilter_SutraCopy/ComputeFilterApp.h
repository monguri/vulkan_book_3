#pragma once
#include "VulkanAppBase.h"
#include <glm/glm.hpp>
#include "Camera.h"

class ComputeFilterApp : public VulkanAppBase
{
public:
	ComputeFilterApp();

	virtual void Prepare() override;
	virtual void Cleanup() override;
	virtual void Render() override;

	virtual bool OnSizeChanged(uint32_t width, uint32_t height) override;

private:
	struct Vertex
	{
		glm::vec3 Position;
		glm::vec2 UV;
	};

	ImageObject m_depthBuffer;
	std::vector<VkFramebuffer> m_framebuffers;

	struct FrameCommandBuffer
	{
		VkCommandBuffer commandBuffer;
		VkFence fence;
	};

	std::vector<FrameCommandBuffer> m_commandBuffers;

	uint32_t m_imageIndex = 0;

	Camera m_camera;
	VkSampler m_texSampler = VK_NULL_HANDLE;

	struct ShaderParameters
	{
		glm::mat4 proj;
	};

	std::vector<BufferObject> m_shaderUniforms;
	std::vector<VkDescriptorSet> m_dsDrawTextures[2];

	VkDescriptorSet m_dsWriteToTexture = VK_NULL_HANDLE;

	VkPipeline m_pipeline = VK_NULL_HANDLE;
	VkPipeline m_compSepiaPipeline = VK_NULL_HANDLE;
	VkPipeline m_compSobelPipeline = VK_NULL_HANDLE;
	ModelData m_quad, m_quad2;

	int m_selectedFilter = 0;

	ImageObject m_sourceBuffer;
	ImageObject m_destBuffer;

	void CreateSampleLayouts();
	void PrepareDepthbuffer();
	void PrepareFramebuffers();
	void PrepareSceneResource();
	ImageObject Load2DTextureFromFile(const char* fileName, VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	void PrepareComputeResource();
	void CreatePrimitiveResource();
	void RenderToMain(const VkCommandBuffer& command);
#if 1
	void RenderHUD(const VkCommandBuffer& command);
#else
	void RenderHUD(VkCommandBuffer command);
#endif

#if 1
	BufferObject CreateStorageBuffer(size_t bufferSize, VkBufferUsageFlags usage, VkMemoryPropertyFlags props);
#endif
	VkImageMemoryBarrier CreateImageMemoryBarrier(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);
};


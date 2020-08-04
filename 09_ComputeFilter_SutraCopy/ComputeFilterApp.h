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
	virtual bool OnMouseButtonDown(int button) override;
	virtual bool OnMouseButtonUp(int button) override;
	virtual bool OnMouseMove(int dx, int dy) override;

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

	std::vector<BufferObject> m_tessUniform;
	std::vector<VkDescriptorSet> m_dsTessSample;
	VkPipeline m_tessGroundPipeline = VK_NULL_HANDLE;
	ModelData m_quad, m_quad2;
	ImageObject m_heightMap;
	ImageObject m_normalMap;

	void CreateSampleLayouts();
	void PrepareDepthbuffer();
	void PrepareFramebuffers();
	void PrepareSceneResource();
	ImageObject Load2DTextureFromFile(const char* fileName);
	void PrepareComputeResource();
	void PreparePrimitiveResource();
	void RenderToMain(const VkCommandBuffer& command);
	void RenderHUD(const VkCommandBuffer& command);
};


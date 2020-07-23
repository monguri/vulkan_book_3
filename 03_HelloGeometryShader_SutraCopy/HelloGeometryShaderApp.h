#pragma once
#include "VulkanAppBase.h"
#include <glm/glm.hpp>
#include "Camera.h"

class HelloGeometryShaderApp : public VulkanAppBase
{
public:
	HelloGeometryShaderApp();

	virtual void Prepare() override;
	virtual void Cleanup() override;
	virtual void Render() override;

	virtual bool OnSizeChanged(uint32_t width, uint32_t height) override;
	virtual bool OnMouseButtonDown(int button) override;
	virtual bool OnMouseButtonUp(int button) override;
	virtual bool OnMouseMove(int dx, int dy) override;

private:
	ImageObject m_depthBuffer;
	std::vector<VkFramebuffer> m_framebuffers;

	struct FrameCommandBuffer
	{
		VkCommandBuffer commandBuffer;
		VkFence fence;
	};

	std::vector<FrameCommandBuffer> m_commandBuffers;
	std::vector<VkDescriptorSet> m_descriptorSets;
	VkPipeline m_pipeline = nullptr;

	struct ModelData
	{
		BufferObject vertexBuffer;
		BufferObject indexBuffer;
		uint32_t vertexCount;
		uint32_t indexCount;
	};
	ModelData m_teapot;

	struct ShaderParameters
	{
		glm::mat4 world;
		glm::mat4 view;
		glm::mat4 proj;
		glm::vec4 lightPos;
		glm::vec4 cameraPos;
	};

	Camera m_camera;
	std::vector<BufferObject> m_uniformBuffers;

	float m_factor = 0.0f;
	float m_color[4];

	void CreateSampleLayouts();
	void PrepareDepthbuffer();
	void PrepareFramebuffers();
	void PrepareTeapot();
	void CreatePipeline();
	void CleanupImGui();
	void RenderImGui(const VkCommandBuffer& command);
};


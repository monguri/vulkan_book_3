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
	std::unordered_map<std::string, VkPipeline> m_pipelines;

	struct ShaderParameters
	{
		glm::mat4 world;
		glm::mat4 view;
		glm::mat4 proj;
		glm::vec4 lightDir;
	};

	Camera m_camera;
	ModelData m_teapot;
	std::vector<BufferObject> m_uniformBuffers;

	const std::string FlatShaderPipeline = "flatShade";
	const std::string SmoothShaderPipeline = "smoothShade";
	const std::string NormalVectorPipeline = "drawNormalVector";

	void CreateSampleLayouts();
	void PrepareDepthbuffer();
	void PrepareFramebuffers();
	void PrepareTeapot();
	void CreatePipeline();
	void RenderHUD(const VkCommandBuffer& command);

	enum DrawMode
	{
		DrawMode_Flat, // = 0
		DrawMode_NormalVector,
	};
	DrawMode m_mode = DrawMode_Flat;
};


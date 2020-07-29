#pragma once
#include "VulkanAppBase.h"
#include <glm/glm.hpp>
#include "Camera.h"

class TessellateTeapotApp : public VulkanAppBase
{
public:
	TessellateTeapotApp();

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

	uint32_t m_imageIndex = 0;

	Camera m_camera;
	ModelData m_teapot;

	struct ShaderParameters
	{
		glm::mat4 world;
		glm::mat4 view;
		glm::mat4 proj;
		glm::vec4 lightDir;
		glm::vec4 cameraPos;
	};

	// 中心のティーポット
	struct CenterTeapot
	{
		std::vector<VkDescriptorSet> dsCubemapStatic;
		std::vector<BufferObject> sceneUBO;
		VkPipeline pipeline;
	};
	CenterTeapot m_centerTeapot;

	void CreateSampleLayouts();
	void PrepareDepthbuffer();
	void PrepareFramebuffers();
	VkPipeline CreateRenderTeapotPipeline(
		const std::string& renderPassName,
		uint32_t width,
		uint32_t height,
		const std::string& layoutName,
		const std::vector<VkPipelineShaderStageCreateInfo> shaderStages
	);

	void PrepareCenterTeapotDescriptos();
	void RenderToMain(const VkCommandBuffer& command);
	void RenderHUD(const VkCommandBuffer& command);
};


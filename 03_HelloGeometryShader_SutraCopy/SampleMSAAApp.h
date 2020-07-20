#pragma once
#include "VulkanAppBase.h"
#include <glm/glm.hpp>

class SampleMSAAApp : public VulkanAppBase
{
public:
	virtual void Prepare() override;
	virtual void Cleanup() override;
	virtual void Render() override;

	virtual bool OnSizeChanged(uint32_t width, uint32_t height) override;

	enum
	{
		TextureWidth = 512,
		TextureHeight = 512,
	};

private:
	ImageObject m_depthBuffer;
	std::vector<VkFramebuffer> m_framebuffers;
	std::vector<VkFence> m_commandFences;
	std::vector<VkCommandBuffer> m_commandBuffers;

	struct VertexPT
	{
		glm::vec3 position;
		glm::vec2 uv;
	};

	struct ModelData
	{
		BufferObject vertexBuffer;
		BufferObject indexBuffer;
		uint32_t vertexCount;
		uint32_t indexCount;

		std::vector<BufferObject> sceneUB;
		std::vector<VkDescriptorSet> descriptorSet;

		VkPipeline pipeline;
	};
	ModelData m_teapot;
	ModelData m_plane;

	struct LayoutInfo
	{
		VkDescriptorSetLayout descriptorSet = VK_NULL_HANDLE;
		VkPipelineLayout pipeline = VK_NULL_HANDLE;
	};
	LayoutInfo m_layoutTeapot;
	LayoutInfo m_layoutPlane;

	uint32_t m_frameIndex = 0;
	uint32_t m_frameCount = 0;

	struct ShaderParameters
	{
		glm::mat4 world;
		glm::mat4 view;
		glm::mat4 proj;
	};

	ImageObject m_colorTarget, m_depthTarget;
	VkFramebuffer m_renderTextureFB = VK_NULL_HANDLE;
	VkSampler m_sampler = VK_NULL_HANDLE;

	ImageObject m_msaaColor, m_msaaDepth;
	VkFramebuffer m_framebufferMSAA = VK_NULL_HANDLE;

	void CreateRenderPass();
	void CreateRenderPassRT();
	void CreateRenderPassMSAA();
	void PrepareFramebuffers();
	void PrepareFramebufferMSAA();
	void PrepareRenderTexture();
	void PrepareMsaaTexture();
	void PrepareTeapot();
	void PreparePlane();
	void CreatePipelineTeapot();
	void CreatePipelinePlane();
	void RenderToTexture(const VkCommandBuffer& command);
	void RenderToMSAABuffer(const VkCommandBuffer& command);
	void DestroyModelData(ModelData& model);
};


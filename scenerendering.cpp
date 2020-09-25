/*
* Vulkan Example - Scene rendering
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*
* Summary:
* Renders a scene made of multiple parts with different materials and textures.
*
* The example loads a scene made up of multiple parts into one vertex and index buffer to only
* have one (big) memory allocation. In Vulkan it's advised to keep number of memory allocations
* down and try to allocate large blocks of memory at once instead of having many small allocations.
*
* Every part has a separate material and multiple descriptor sets (set = x layout qualifier in GLSL)
* are used to bind a uniform buffer with global matrices and the part's material's sampler at once.
*
* To demonstrate another way of passing data the example also uses push constants for passing
* material properties.
*
* Note that this example is just one way of rendering a scene made up of multiple parts in Vulkan.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <vector>
#include <random>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vulkan/vulkan.h>
#include "vulkanexamplebase.h"
#include "VulkanTexture.hpp"
#include "VulkanModel.hpp"
#include<math.h>

#define VERTEX_BUFFER_BIND_ID 0
#define ENABLE_VALIDATION false
#define SHADOWMAP_DIM 512
class FrustumJitter
{
public:
	float* points_Halton_2_3_x16 = new float[16 * 2];
	int m_currentIndex = 0;
	glm::vec4 activeSample = glm::vec4(0.0);

public:
	FrustumJitter()
	{

		InitializeHalton_2_3(points_Halton_2_3_x16, 32);

	}
	~FrustumJitter() {
		delete[]points_Halton_2_3_x16;
	}
	glm::vec2 Sample(int index)
	{
		float patternScale = 1.0f;
		float* points = new float[16 * 2];
		int n = 16;
		int i = index % n;

		float x = patternScale * points[2 * i + 0];
		float y = patternScale * points[2 * i + 1];
		return glm::vec2(x, y);
	}


	float HaltonSeq(uint32_t prime, uint32_t index)
	{
		float r = 0.0f;
		float f = 1.0f;
		uint32_t i = index;
		while (i > 0)
		{
			f /= prime;
			r += f * (i % prime);
			i = (uint32_t)std::floorf(i / (float)prime);
		}
		return r;
	}

	void InitializeHalton_2_3(float* seq, int len)
	{
		for (int i = 0, n = len / 2; i != n; i++)
		{
			float u = HaltonSeq(2, i + 1) - 0.5f;
			float v = HaltonSeq(3, i + 1) - 0.5f;
			seq[2 * i + 0] = u;
			seq[2 * i + 1] = v;
		}
	}

	glm::vec2 GetHaltonJitter(uint64_t index)
	{
		activeSample.x = activeSample.z;
		activeSample.y = activeSample.w;
		//glm::vec2 zw=Sample(index);
		activeSample.z = points_Halton_2_3_x16[index % 16];
		activeSample.w = points_Halton_2_3_x16[(index % 16) + 1];
		
		m_currentIndex = (m_currentIndex + 2) % 32;
		return glm::vec2(activeSample.x, activeSample.y);
	}

};

glm::mat4 GetPerspectiveProjection(float left, float right, float bottom, float top, float n, float f)
{
	float x = (2.0f * n) / (right - left);
	float y = (2.0f * n) / (top - bottom);
	float a = (right + left) / (right - left);
	float b = (top + bottom) / (top - bottom);
	float c = -f / (f - n);
	float d = -(f * n) / (f - n);
	float e = -1.0f;

	glm::mat4 m;
	m[0][0] = x; m[0][1] = 0; m[0][2] = a; m[0][3] = 0;
	m[1][0] = 0; m[1][1] = y; m[1][2] = b; m[1][3] = 0;
	m[2][0] = 0; m[2][1] = 0; m[2][2] = c; m[2][3] = d;
	m[3][0] = 0; m[3][1] = 0; m[3][2] = e; m[3][3] = 0;
	return m;
}


class VulkanExample : public VulkanExampleBase
{
public:
	FrustumJitter frustumJitter;

	int current = 0;
	int first = 0;
	// Vertex layout for the models


	vks::VertexLayout vertexLayout = vks::VertexLayout({
		vks::VERTEX_COMPONENT_POSITION,
		vks::VERTEX_COMPONENT_NORMAL,
		vks::VERTEX_COMPONENT_UV,

		});
	struct {
		vks::Model scene;
	} models;

	struct UBOSceneMatrices {
		glm::mat4 projection;
		glm::mat4 model;
		glm::mat4 view;
	} uboSceneMatrices;

	struct FrameBufferAttachment {
		VkImage image;
		VkDeviceMemory mem;
		VkImageView view;

	};
	struct FrameBuffer {
		VkFramebuffer framebuffer;
		FrameBufferAttachment color, depth;

	};
	struct OffscreenPass {
		int32_t width, height;
		VkRenderPass renderPass;
		std::vector<FrameBuffer> framebuffers;
	};
	VkSampler colorsampler;
	struct UBO1 {

		glm::mat4 _CurrVP;
		glm::mat4 _CurrM;
		glm::mat4 _PrevVP;
		glm::mat4 _PrevM;


	} velocity_ubo;
	struct UBO2 {

		glm::vec4 _SinTime;
		glm::vec4 _FeedbackMin_Max_Mscale;
		glm::vec4 JitterUV;


	} temprolReproj_ubo;
	// Resources for the graphics part of the example
	struct Graphics {
		vks::Buffer uniformbuffer;
		VkDescriptorSetLayout descriptorSetLayout;
		VkDescriptorSet descriptorSet;
		VkPipeline pipeline;
		VkPipelineLayout pipelineLayout;
		OffscreenPass pass;
	} velocity, temproalReproj, velocityMax, building;



	VkDescriptorSetLayout descriptorSetLayout;
	VkDescriptorSet descriptorSet;
	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;

	VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
	{
		title = "Multi-part scene rendering";


		camera.type = Camera::CameraType::lookat;
		camera.setPosition(glm::vec3(0.0f, -0.0f, -20.0f));
		camera.setRotation(glm::vec3(-15.0f, -390.0f, 0.0f));
		camera.setPerspective(60.0f, (float)width / (float)height, 1.0f, 256.0f);

		settings.overlay = true;

	}

	~VulkanExample()
	{

		// Meshes
		models.scene.destroy();
		velocity.uniformbuffer.destroy();
		velocityMax.uniformbuffer.destroy();
		temproalReproj.uniformbuffer.destroy();
		building.uniformbuffer.destroy();

		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyPipelineLayout(device, velocity.pipelineLayout, nullptr);
		vkDestroyPipelineLayout(device, temproalReproj.pipelineLayout, nullptr);
		vkDestroyPipelineLayout(device, building.pipelineLayout, nullptr);
		vkDestroyPipelineLayout(device, velocityMax.pipelineLayout, nullptr);

		vkDestroyDescriptorSetLayout(device, velocity.descriptorSetLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, velocityMax.descriptorSetLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, temproalReproj.descriptorSetLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, building.descriptorSetLayout, nullptr);


		vkDestroyPipeline(device, pipeline, nullptr);
		vkDestroyPipeline(device, velocity.pipeline, nullptr);
		vkDestroyPipeline(device, velocityMax.pipeline, nullptr);
		vkDestroyPipeline(device, building.pipeline, nullptr);
		vkDestroyPipeline(device, temproalReproj.pipeline, nullptr);


		vkDestroyRenderPass(device, velocity.pass.renderPass, nullptr);
		vkDestroyRenderPass(device, velocityMax.pass.renderPass, nullptr);
		vkDestroyRenderPass(device, temproalReproj.pass.renderPass, nullptr);
		vkDestroyRenderPass(device, building.pass.renderPass, nullptr);

		vkDestroySampler(device, colorsampler, nullptr);

		for (auto framebuffer : velocity.pass.framebuffers)
		{
			vkDestroyFramebuffer(device, framebuffer.framebuffer, nullptr);

		}
		for (auto framebuffer : building.pass.framebuffers)
		{
			vkDestroyFramebuffer(device, framebuffer.framebuffer, nullptr);

		}
		for (auto framebuffer : velocityMax.pass.framebuffers)
			vkDestroyFramebuffer(device, framebuffer.framebuffer, nullptr);
		for (auto framebuffer : temproalReproj.pass.framebuffers)
			vkDestroyFramebuffer(device, framebuffer.framebuffer, nullptr);


		for (auto framebuffer : velocity.pass.framebuffers)
		{
			vkDestroyImage(device, framebuffer.color.image, nullptr);
			vkDestroyImageView(device, framebuffer.color.view, nullptr);
			vkFreeMemory(device, framebuffer.color.mem, nullptr);
		}
		for (auto framebuffer : velocityMax.pass.framebuffers) {
			vkDestroyImage(device, framebuffer.color.image, nullptr);
			vkDestroyImageView(device, framebuffer.color.view, nullptr);
			vkFreeMemory(device, framebuffer.color.mem, nullptr);
		}
		for (auto framebuffer : temproalReproj.pass.framebuffers) {
			vkDestroyImage(device, framebuffer.color.image, nullptr);
			vkDestroyImageView(device, framebuffer.color.view, nullptr);
			vkFreeMemory(device, framebuffer.color.mem, nullptr);
		}
		for (auto framebuffer : building.pass.framebuffers)
		{
			vkDestroyImage(device, framebuffer.depth.image, nullptr);
			vkDestroyImage(device, framebuffer.color.image, nullptr);
			vkDestroyImageView(device, framebuffer.depth.view, nullptr);
			vkDestroyImageView(device, framebuffer.color.view, nullptr);
			vkFreeMemory(device, framebuffer.color.mem, nullptr);
			vkFreeMemory(device, framebuffer.depth.mem, nullptr);
		}



	}


	void prepareFramebuffer(OffscreenPass &offscreenPass, VkFormat colorFormat, FrameBuffer &framebuffer, int width, int height)
	{
		// Color attachment
		VkImageCreateInfo image = vks::initializers::imageCreateInfo();
		image.imageType = VK_IMAGE_TYPE_2D;
		image.format = colorFormat;
		image.extent.width = width;
		image.extent.height = height;
		image.extent.depth = 1;
		image.mipLevels = 1;
		image.arrayLayers = 1;
		image.samples = VK_SAMPLE_COUNT_1_BIT;
		image.tiling = VK_IMAGE_TILING_OPTIMAL;
		// We will sample directly from the color attachment
		image.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

		VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
		VkMemoryRequirements memReqs;

		VkImageViewCreateInfo colorImageView = vks::initializers::imageViewCreateInfo();
		colorImageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		colorImageView.format = colorFormat;
		colorImageView.flags = 0;
		colorImageView.subresourceRange = {};
		colorImageView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		colorImageView.subresourceRange.baseMipLevel = 0;
		colorImageView.subresourceRange.levelCount = 1;
		colorImageView.subresourceRange.baseArrayLayer = 0;
		colorImageView.subresourceRange.layerCount = 1;

		VK_CHECK_RESULT(vkCreateImage(device, &image, nullptr, &framebuffer.color.image));


		vkGetImageMemoryRequirements(device, framebuffer.color.image, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &framebuffer.color.mem));
		VK_CHECK_RESULT(vkBindImageMemory(device, framebuffer.color.image, framebuffer.color.mem, 0));

		colorImageView.image = framebuffer.color.image;
		VK_CHECK_RESULT(vkCreateImageView(device, &colorImageView, nullptr, &framebuffer.color.view));



		VkImageView attachments[1];
		attachments[0] = framebuffer.color.view;


		VkFramebufferCreateInfo fbufCreateInfo = vks::initializers::framebufferCreateInfo();
		fbufCreateInfo.renderPass = offscreenPass.renderPass;
		fbufCreateInfo.attachmentCount = 1;
		fbufCreateInfo.pAttachments = attachments;
		fbufCreateInfo.width = width;
		fbufCreateInfo.height = height;
		fbufCreateInfo.layers = 1;

		VK_CHECK_RESULT(vkCreateFramebuffer(device, &fbufCreateInfo, nullptr, &framebuffer.framebuffer));



	}
	void prepareOffscreenRenderpass(OffscreenPass &offscreenPass, VkFormat format, int width, int height, int framebuffercount, VkAttachmentLoadOp op)
	{
		offscreenPass.width = width;
		offscreenPass.height = height;

		// Create a separate render pass for the scene rendering as it may differ from the one used for scene rendering

		VkAttachmentDescription attchmentDescription = {};
		// Color attachment
		attchmentDescription.format = format;
		attchmentDescription.samples = VK_SAMPLE_COUNT_1_BIT;
		attchmentDescription.loadOp = op;
		attchmentDescription.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attchmentDescription.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attchmentDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attchmentDescription.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attchmentDescription.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;


		VkAttachmentReference colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };


		VkSubpassDescription subpassDescription = {};
		subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescription.colorAttachmentCount = 1;
		subpassDescription.pColorAttachments = &colorReference;


		// Use subpass dependencies for layout transitions
		std::array<VkSubpassDependency, 2> dependencies;

		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		// Create the actual renderpass
		VkRenderPassCreateInfo renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = 1;
		renderPassInfo.pAttachments = &attchmentDescription;
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpassDescription;
		renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
		renderPassInfo.pDependencies = dependencies.data();

		VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &offscreenPass.renderPass));



		offscreenPass.framebuffers.resize(framebuffercount);
		// Create two frame buffers
		for (int i = 0; i < framebuffercount; i++)
			prepareFramebuffer(offscreenPass, format, offscreenPass.framebuffers[i], width, height);



	}

	void buildCommandBuffers()
	{
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();


		for (int32_t i = 0; i < drawCmdBuffers.size(); ++i)
		{
			VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));
			{
				VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
				VkClearValue clearValues[2];
				clearValues[0].color = defaultClearColor;
				clearValues[1].depthStencil = { 1.0f, 0 };
				renderPassBeginInfo.renderPass = building.pass.renderPass;
				renderPassBeginInfo.renderArea.offset.x = 0;
				renderPassBeginInfo.renderArea.offset.y = 0;
				renderPassBeginInfo.renderArea.extent.width = width;
				renderPassBeginInfo.renderArea.extent.height = height;
				renderPassBeginInfo.clearValueCount = 2;
				renderPassBeginInfo.pClearValues = clearValues;
				// Set target frame buffer
				renderPassBeginInfo.framebuffer =  building.pass.framebuffers[0].framebuffer;

				vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
				VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);

				vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

				VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
				vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);
				VkDeviceSize offsets[1] = { 0 };

				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, building.pipeline);
				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, building.pipelineLayout, 0, 1, &building.descriptorSet, 0, NULL);

				vkCmdBindVertexBuffers(drawCmdBuffers[i], 0, 1, &models.scene.vertices.buffer, offsets);
				vkCmdBindIndexBuffer(drawCmdBuffers[i], models.scene.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
				vkCmdDrawIndexed(drawCmdBuffers[i], models.scene.indexCount, 1, 0, 0, 0);

				// Display ray traced image generated by compute shader as a full screen quad

				vkCmdEndRenderPass(drawCmdBuffers[i]);
			}
			{
				VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
				VkClearValue clearValues[1];
				clearValues[0].color = defaultClearColor;
				renderPassBeginInfo.renderPass = velocity.pass.renderPass;
				renderPassBeginInfo.renderArea.offset.x = 0;
				renderPassBeginInfo.renderArea.offset.y = 0;
				renderPassBeginInfo.renderArea.extent.width = width;
				renderPassBeginInfo.renderArea.extent.height = height;
				renderPassBeginInfo.clearValueCount = 1;
				renderPassBeginInfo.pClearValues = clearValues;
				// Set target frame buffer
				renderPassBeginInfo.framebuffer = velocity.pass.framebuffers[0].framebuffer;

				vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
				VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);

				vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

				VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
				vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

				// Display ray traced image generated by compute shader as a full screen quad
				// Quad vertices are generated in the vertex shader
				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, velocity.pipelineLayout, 0, 1, &velocity.descriptorSet, 0, NULL);
				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, velocity.pipeline);
				VkDeviceSize offsets[1] = { 0 };
				vkCmdBindVertexBuffers(drawCmdBuffers[i], 0, 1, &models.scene.vertices.buffer, offsets);
				vkCmdBindIndexBuffer(drawCmdBuffers[i], models.scene.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
				vkCmdDrawIndexed(drawCmdBuffers[i], models.scene.indexCount, 1, 0, 0, 0);
				vkCmdEndRenderPass(drawCmdBuffers[i]);
			}
			{
				VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
				VkClearValue clearValues[1];
				clearValues[0].color = defaultClearColor;
				renderPassBeginInfo.renderPass = velocityMax.pass.renderPass;
				renderPassBeginInfo.renderArea.offset.x = 0;
				renderPassBeginInfo.renderArea.offset.y = 0;
				renderPassBeginInfo.renderArea.extent.width = width;
				renderPassBeginInfo.renderArea.extent.height = height;
				renderPassBeginInfo.clearValueCount = 1;
				renderPassBeginInfo.pClearValues = clearValues;
				// Set target frame buffer
				renderPassBeginInfo.framebuffer = velocityMax.pass.framebuffers[0].framebuffer;

				vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
				VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);

				vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

				VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
				vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

				// Display ray traced image generated by compute shader as a full screen quad
				// Quad vertices are generated in the vertex shader
				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, velocityMax.pipelineLayout, 0, 1, &velocityMax.descriptorSet, 0, NULL);
				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, velocityMax.pipeline);
				vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);
				vkCmdEndRenderPass(drawCmdBuffers[i]);
			}
			{
				VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
				VkClearValue clearValues[1];
				clearValues[0].color = defaultClearColor;
				renderPassBeginInfo.renderPass = temproalReproj.pass.renderPass;
				renderPassBeginInfo.renderArea.offset.x = 0;
				renderPassBeginInfo.renderArea.offset.y = 0;
				renderPassBeginInfo.renderArea.extent.width = width;
				renderPassBeginInfo.renderArea.extent.height = height;
				renderPassBeginInfo.clearValueCount = 1;
				renderPassBeginInfo.pClearValues = clearValues;
				// Set target frame buffer
				renderPassBeginInfo.framebuffer = temproalReproj.pass.framebuffers[current].framebuffer;

				vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
				VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);

				vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

				VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
				vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, temproalReproj.pipelineLayout, 0, 1, &temproalReproj.descriptorSet, 0, NULL);
				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, temproalReproj.pipeline);
				vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);
				vkCmdEndRenderPass(drawCmdBuffers[i]);
			}
			{
				VkClearValue clearValues[2];
				clearValues[0].color = defaultClearColor;
				clearValues[1].depthStencil = { 1.0f, 0 };

				VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
				renderPassBeginInfo.renderPass = renderPass;
				renderPassBeginInfo.renderArea.offset.x = 0;
				renderPassBeginInfo.renderArea.offset.y = 0;
				renderPassBeginInfo.renderArea.extent.width = width;
				renderPassBeginInfo.renderArea.extent.height = height;
				renderPassBeginInfo.clearValueCount = 2;
				renderPassBeginInfo.pClearValues = clearValues;

				renderPassBeginInfo.framebuffer = frameBuffers[i];



				vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

				VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
				vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

				VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
				vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

				// Display ray traced image generated by compute shader as a full screen quad
				// Quad vertices are generated in the vertex shader
				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, NULL);
				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
				vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);

				//drawUI(drawCmdBuffers[i]);

				vkCmdEndRenderPass(drawCmdBuffers[i]);
			}
			VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
		}
	}

	void preparePipelines()
	{
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleState = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		// Vertex input state for scene rendering
		const std::vector<VkVertexInputBindingDescription> vertexInputBindings = {
			vks::initializers::vertexInputBindingDescription(0, vertexLayout.stride(), VK_VERTEX_INPUT_RATE_VERTEX),
		};

		// Attribute descriptions
		std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = {
			vks::initializers::vertexInputAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0),					// Position
			vks::initializers::vertexInputAttributeDescription(0, 1, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 3),	// Normal
			vks::initializers::vertexInputAttributeDescription(0, 2, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 6),		// UV
		};

		VkPipelineVertexInputStateCreateInfo vertexInputState = vks::initializers::pipelineVertexInputStateCreateInfo();
		vertexInputState.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexInputBindings.size());
		vertexInputState.pVertexBindingDescriptions = vertexInputBindings.data();
		vertexInputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
		vertexInputState.pVertexAttributeDescriptions = vertexInputAttributes.data();

		// Empty vertex input state for fullscreen passes
		VkPipelineVertexInputStateCreateInfo emptyVertexInputState = vks::initializers::pipelineVertexInputStateCreateInfo();

		VkGraphicsPipelineCreateInfo pipelineCreateInfo = vks::initializers::pipelineCreateInfo(building.pipelineLayout, building.pass.renderPass, 0);

		pipelineCreateInfo.pVertexInputState = &vertexInputState;
		pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
		pipelineCreateInfo.pRasterizationState = &rasterizationState;
		pipelineCreateInfo.pColorBlendState = &colorBlendState;
		pipelineCreateInfo.pMultisampleState = &multisampleState;
		pipelineCreateInfo.pViewportState = &viewportState;
		pipelineCreateInfo.pDepthStencilState = &depthStencilState;
		pipelineCreateInfo.pDynamicState = &dynamicState;
		pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCreateInfo.pStages = shaderStages.data();
		// Solid rendering pipeline
		shaderStages[0] = loadShader(getAssetPath() + "shaders/scenerendering/scene.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/scenerendering/scene.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &building.pipeline));

		pipelineCreateInfo.renderPass = velocity.pass.renderPass;
		pipelineCreateInfo.layout = velocity.pipelineLayout;
		// Solid rendering pipeline
		shaderStages[0] = loadShader(getAssetPath() + "shaders/scenerendering/velocityMotion.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/scenerendering/velocityMotion.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &velocity.pipeline));

		pipelineCreateInfo.pVertexInputState = &emptyVertexInputState;
		pipelineCreateInfo.layout = temproalReproj.pipelineLayout;
		pipelineCreateInfo.renderPass = temproalReproj.pass.renderPass;

		// Solid rendering pipeline
		shaderStages[0] = loadShader(getAssetPath() + "shaders/scenerendering/velocity.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/scenerendering/TemprolReprojectionMotion.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &temproalReproj.pipeline));

		pipelineCreateInfo.renderPass = velocityMax.pass.renderPass;
		pipelineCreateInfo.layout = velocityMax.pipelineLayout;
		// Solid rendering pipeline
	//	shaderStages[0] = loadShader(getAssetPath() + "shaders/scenerendering/velocity.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/scenerendering/velocityMax.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &velocityMax.pipeline));

		pipelineCreateInfo.renderPass = renderPass;
		pipelineCreateInfo.layout = pipelineLayout;
		// Solid rendering pipeline
	//	shaderStages[0] = loadShader(getAssetPath() + "shaders/scenerendering/velocity.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/scenerendering/quad.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipeline));
	}

	void updateUniformBuffers()
	{


		uboSceneMatrices.model = velocity_ubo._CurrM;

		uboSceneMatrices.projection = camera.matrices.perspective;
		uboSceneMatrices.view = camera.matrices.view;
		memcpy(building.uniformbuffer.mapped, &uboSceneMatrices, sizeof(uboSceneMatrices));

	}

	void draw()
	{
		VulkanExampleBase::prepareFrame();

		// Command buffer to be sumitted to the queue
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];

		// Submit to queue
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

		VulkanExampleBase::submitFrame();
	}

	void loadAssets()
	{
		models.scene.loadFromFile(getAssetPath() + "models/cube.obj", vertexLayout, 1.0f, vulkanDevice, queue);
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void prepareUniformBuffers()
	{
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&building.uniformbuffer,
			sizeof(uboSceneMatrices)));

		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&velocity.uniformbuffer,
			sizeof(velocity_ubo)));


		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&temproalReproj.uniformbuffer,
			sizeof(temprolReproj_ubo)));
		;
		VK_CHECK_RESULT(building.uniformbuffer.map());

		// Map persistent
		VK_CHECK_RESULT(velocity.uniformbuffer.map());
		VK_CHECK_RESULT(temproalReproj.uniformbuffer.map());
		velocity_ubo._CurrM = glm::mat4(1.0);

		velocity_ubo._CurrVP = camera.matrices.perspective*camera.matrices.view;
		updateTemproalUniformBuffers();

	}
	glm::vec4 GetProjectionExtents(float texelOffsetX, float texelOffsetY)
	{


		float oneExtentY = tan(0.5f * 0.0174532924F * camera.fov);
		float oneExtentX = oneExtentY * camera.aspect;
		float texelSizeX = oneExtentX / (0.5f * width);
		float texelSizeY = oneExtentY / (0.5f * height);
		float oneJitterX = texelSizeX * texelOffsetX;
		float oneJitterY = texelSizeY * texelOffsetY;

		return glm::vec4(oneExtentX, oneExtentY, oneJitterX, oneJitterY);// xy = frustum extents at distance 1, zw = jitter at distance 1
	}
	glm::mat4 GetProjectionMatrix(float texelOffsetX, float texelOffsetY)
	{

		glm::vec4 extents = GetProjectionExtents(texelOffsetX, texelOffsetY);
		float cf = camera.zfar;
		float cn = camera.znear;
		float xm = extents.z - extents.x;
		float xp = extents.z + extents.x;
		float ym = extents.w - extents.y;
		float yp = extents.w + extents.y;

		return GetPerspectiveProjection(xm * cn, xp * cn, ym * cn, yp * cn, cn, cf);
	}
	// Update uniform buffers for rendering the 3D scene
	void updateTemproalUniformBuffers()
	{
		first++;
		velocity_ubo._PrevM = velocity_ubo._CurrM;
		velocity_ubo._PrevVP = velocity_ubo._CurrVP;
		velocity_ubo._CurrM[3][0] = sin(timer) * 10;
		camera.matrices.perspective = glm::transpose(GetProjectionMatrix(frustumJitter.activeSample.z, frustumJitter.activeSample.w));
		velocity_ubo._CurrVP = camera.matrices.perspective*camera.matrices.view;
		
		updateUniformBuffers();
		memcpy(velocity.uniformbuffer.mapped, &velocity_ubo, sizeof(velocity_ubo));

		glm::vec2 texelOffset = frustumJitter.GetHaltonJitter(frustumJitter.m_currentIndex);
		temprolReproj_ubo.JitterUV = frustumJitter.activeSample;
		temprolReproj_ubo.JitterUV.x /= width;
		temprolReproj_ubo.JitterUV.y /= height;
		temprolReproj_ubo.JitterUV.z /= width;
		temprolReproj_ubo.JitterUV.w /= height;


		temprolReproj_ubo._FeedbackMin_Max_Mscale = glm::vec4(0.88f, 0.97f, 0.0f, 0.0f);
		temprolReproj_ubo._SinTime = glm::vec4(timer / 8.0, timer / 4.0, timer / 2.0, timer);
		memcpy(temproalReproj.uniformbuffer.mapped, &temprolReproj_ubo, sizeof(temprolReproj_ubo));

	}
	void setupDescriptorSetLayout()
	{
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings;
		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo;
		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo;

		setLayoutBindings = {
		vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0),			// Binding 0: Fragment shader uniform buffer
	//	vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1)	// Binding 1: Fragment shader image sampler
		};
		descriptorSetLayoutCreateInfo = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings.data(), static_cast<uint32_t>(setLayoutBindings.size()));
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, nullptr, &building.descriptorSetLayout));
		pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&building.descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &building.pipelineLayout));

		setLayoutBindings = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0),			// Binding 0: Fragment shader uniform buffer
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1)	// Binding 1: Fragment shader image sampler
		};
		descriptorSetLayoutCreateInfo = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings.data(), static_cast<uint32_t>(setLayoutBindings.size()));
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, nullptr, &velocity.descriptorSetLayout));
		pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&velocity.descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &velocity.pipelineLayout));

		setLayoutBindings = {
		vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0)	// Binding 1: Fragment shader image sampler
		};
		descriptorSetLayoutCreateInfo = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings.data(), static_cast<uint32_t>(setLayoutBindings.size()));
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, nullptr, &velocityMax.descriptorSetLayout));
		pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&velocityMax.descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &velocityMax.pipelineLayout));

		// Scene rendering
		setLayoutBindings = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 0),			// Binding 0: Fragment shader uniform buffer

			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1),	// Binding 1 : Fragment shader image sampler			
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 2),	// Binding 1 : Fragment shader image sampler			
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 3),	// Binding 1 : Fragment shader image sampler			
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 4),	// Binding 1 : Fragment shader image sampler			
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 5),	// Binding 1 : Fragment shader image sampler			

		};

		descriptorSetLayoutCreateInfo = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings.data(), setLayoutBindings.size());
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, nullptr, &temproalReproj.descriptorSetLayout));
		pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&temproalReproj.descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &temproalReproj.pipelineLayout));

		// Scene rendering
		setLayoutBindings = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0)	// Binding 1 : Fragment shader image sampler			


		};

		descriptorSetLayoutCreateInfo = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings.data(), setLayoutBindings.size());
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, nullptr, &descriptorSetLayout));
		pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));
	}
	void setupDescriptorPool()
	{
		std::vector<VkDescriptorPoolSize> poolSizes =
		{
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 5),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10)
		};

		VkDescriptorPoolCreateInfo descriptorPoolInfo =
			vks::initializers::descriptorPoolCreateInfo(
				poolSizes.size(),
				poolSizes.data(),
				10);

		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
	}
	void setupDescriptorSet()
	{
		VkDescriptorSetAllocateInfo descriptorSetAllocInfo;

		descriptorSetAllocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &building.descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &building.descriptorSet));


		descriptorSetAllocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &descriptorSet));


		descriptorSetAllocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &velocity.descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &velocity.descriptorSet));



		descriptorSetAllocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &velocityMax.descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &velocityMax.descriptorSet));



		descriptorSetAllocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &temproalReproj.descriptorSetLayout, 1);

		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &temproalReproj.descriptorSet));

		updateDescriptorSet();


	}
	void updateDescriptorSet() {
		std::vector<VkWriteDescriptorSet> writeDescriptorSets;

		VkDescriptorImageInfo depthMapDescriptor =
			vks::initializers::descriptorImageInfo(
				colorsampler,
				building.pass.framebuffers[0].depth.view,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		VkDescriptorImageInfo colorMapDescriptor =
			vks::initializers::descriptorImageInfo(
				colorsampler,
				building.pass.framebuffers[0].color.view,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		VkDescriptorImageInfo velocityDescriptor =
			vks::initializers::descriptorImageInfo(
				colorsampler,
				velocity.pass.framebuffers[0].color.view,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		VkDescriptorImageInfo currDescriptor =
			vks::initializers::descriptorImageInfo(
				colorsampler,
				temproalReproj.pass.framebuffers[current].color.view,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		VkDescriptorImageInfo preDescriptor;
		if (first == 1)
			preDescriptor =
			vks::initializers::descriptorImageInfo(
				colorsampler,
				building.pass.framebuffers[0].color.view,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		else
		{
			preDescriptor =
				vks::initializers::descriptorImageInfo(
					colorsampler,
					temproalReproj.pass.framebuffers[1 - current].color.view,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		}





		VkDescriptorImageInfo velocityMaxDescriptor =
			vks::initializers::descriptorImageInfo(
				colorsampler,
				velocityMax.pass.framebuffers[0].color.view,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);


		writeDescriptorSets = {
		vks::initializers::writeDescriptorSet(temproalReproj.descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &temproalReproj.uniformbuffer.descriptor),
		vks::initializers::writeDescriptorSet(temproalReproj.descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &depthMapDescriptor),	// Binding 1: Fragment shader texture sampler

		vks::initializers::writeDescriptorSet(temproalReproj.descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &colorMapDescriptor),	// Binding 1: Fragment shader texture sampler
		vks::initializers::writeDescriptorSet(temproalReproj.descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3, &preDescriptor),	// Binding 1: Fragment shader texture sampler
		vks::initializers::writeDescriptorSet(temproalReproj.descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4, &velocityMaxDescriptor),	// Binding 1: Fragment shader texture sampler
		vks::initializers::writeDescriptorSet(temproalReproj.descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 5, &velocityDescriptor)	// Binding 1: Fragment shader texture sampler

		};
		vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);

		writeDescriptorSets = {
		vks::initializers::writeDescriptorSet(building.descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &building.uniformbuffer.descriptor),	// Binding 1: Fragment shader texture sampler
		};
		vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);



		writeDescriptorSets = {
			vks::initializers::writeDescriptorSet(velocityMax.descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &velocityDescriptor)			// Binding 0: Fragment shader uniform buffer			
		};
		vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);

		writeDescriptorSets = {
			vks::initializers::writeDescriptorSet(velocity.descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &velocity.uniformbuffer.descriptor),				// Binding 0: Fragment shader uniform buffer			
			vks::initializers::writeDescriptorSet(velocity.descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &depthMapDescriptor),	// Binding 1: Fragment shader texture sampler
		};
		vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);

		writeDescriptorSets = {
		vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, &currDescriptor),	// Binding 1: Fragment shader texture sampler
		};

		vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);






	}
	// Prepare the offscreen framebuffers used for the vertical- and horizontal blur 
	void prepareBuilding(int width, int height, VkFormat FB_COLOR_FORMAT)
	{
		building.pass.width = width;
		building.pass.height = height;

		// Find a suitable depth format
		VkFormat fbDepthFormat = VK_FORMAT_D16_UNORM;
		//VkBool32 validDepthFormat = vks::tools::getSupportedDepthFormat(physicalDevice, &fbDepthFormat);
		//assert(validDepthFormat);

		// Create a separate render pass for the offscreen rendering as it may differ from the one used for scene rendering

		std::array<VkAttachmentDescription, 2> attchmentDescriptions = {};
		// Color attachment
		attchmentDescriptions[0].format = FB_COLOR_FORMAT;
		attchmentDescriptions[0].samples = VK_SAMPLE_COUNT_1_BIT;
		attchmentDescriptions[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attchmentDescriptions[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attchmentDescriptions[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attchmentDescriptions[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attchmentDescriptions[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attchmentDescriptions[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		// Depth attachment
		attchmentDescriptions[1].format = fbDepthFormat;
		attchmentDescriptions[1].samples = VK_SAMPLE_COUNT_1_BIT;
		attchmentDescriptions[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attchmentDescriptions[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attchmentDescriptions[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attchmentDescriptions[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attchmentDescriptions[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attchmentDescriptions[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
		VkAttachmentReference depthReference = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

		VkSubpassDescription subpassDescription = {};
		subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescription.colorAttachmentCount = 1;
		subpassDescription.pColorAttachments = &colorReference;
		subpassDescription.pDepthStencilAttachment = &depthReference;

		// Use subpass dependencies for layout transitions
		std::array<VkSubpassDependency, 2> dependencies;

		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		// Create the actual renderpass
		VkRenderPassCreateInfo renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = static_cast<uint32_t>(attchmentDescriptions.size());
		renderPassInfo.pAttachments = attchmentDescriptions.data();
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpassDescription;
		renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
		renderPassInfo.pDependencies = dependencies.data();

		VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &building.pass.renderPass));

		building.pass.framebuffers.resize(1);
		// Create two frame buffers
		prepareBuildingFramebuffer(&building.pass.framebuffers[0], FB_COLOR_FORMAT, fbDepthFormat, width, height);

	}
	void prepareBuildingFramebuffer(FrameBuffer *frameBuf, VkFormat colorFormat, VkFormat depthFormat, int width, int height)
	{
		// Color attachment
		VkImageCreateInfo image = vks::initializers::imageCreateInfo();
		image.imageType = VK_IMAGE_TYPE_2D;
		image.format = colorFormat;
		image.extent.width = width;
		image.extent.height = height;
		image.extent.depth = 1;
		image.mipLevels = 1;
		image.arrayLayers = 1;
		image.samples = VK_SAMPLE_COUNT_1_BIT;
		image.tiling = VK_IMAGE_TILING_OPTIMAL;
		// We will sample directly from the color attachment
		image.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

		VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
		VkMemoryRequirements memReqs;

		VkImageViewCreateInfo colorImageView = vks::initializers::imageViewCreateInfo();
		colorImageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		colorImageView.format = colorFormat;
		colorImageView.flags = 0;
		colorImageView.subresourceRange = {};
		colorImageView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		colorImageView.subresourceRange.baseMipLevel = 0;
		colorImageView.subresourceRange.levelCount = 1;
		colorImageView.subresourceRange.baseArrayLayer = 0;
		colorImageView.subresourceRange.layerCount = 1;

		VK_CHECK_RESULT(vkCreateImage(device, &image, nullptr, &frameBuf->color.image));
		vkGetImageMemoryRequirements(device, frameBuf->color.image, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &frameBuf->color.mem));
		VK_CHECK_RESULT(vkBindImageMemory(device, frameBuf->color.image, frameBuf->color.mem, 0));

		colorImageView.image = frameBuf->color.image;
		VK_CHECK_RESULT(vkCreateImageView(device, &colorImageView, nullptr, &frameBuf->color.view));

		// Depth stencil attachment
		image.format = depthFormat;
		image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

		VkImageViewCreateInfo depthStencilView = vks::initializers::imageViewCreateInfo();
		depthStencilView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		depthStencilView.format = depthFormat;
		depthStencilView.flags = 0;
		depthStencilView.subresourceRange = {};
		depthStencilView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		depthStencilView.subresourceRange.baseMipLevel = 0;
		depthStencilView.subresourceRange.levelCount = 1;
		depthStencilView.subresourceRange.baseArrayLayer = 0;
		depthStencilView.subresourceRange.layerCount = 1;

		VK_CHECK_RESULT(vkCreateImage(device, &image, nullptr, &frameBuf->depth.image));
		vkGetImageMemoryRequirements(device, frameBuf->depth.image, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &frameBuf->depth.mem));
		VK_CHECK_RESULT(vkBindImageMemory(device, frameBuf->depth.image, frameBuf->depth.mem, 0));

		depthStencilView.image = frameBuf->depth.image;
		VK_CHECK_RESULT(vkCreateImageView(device, &depthStencilView, nullptr, &frameBuf->depth.view));

		VkImageView attachments[2];
		attachments[0] = frameBuf->color.view;
		attachments[1] = frameBuf->depth.view;

		VkFramebufferCreateInfo fbufCreateInfo = vks::initializers::framebufferCreateInfo();
		fbufCreateInfo.renderPass = building.pass.renderPass;
		fbufCreateInfo.attachmentCount = 2;
		fbufCreateInfo.pAttachments = attachments;
		fbufCreateInfo.width = width;
		fbufCreateInfo.height = height;
		fbufCreateInfo.layers = 1;

		VK_CHECK_RESULT(vkCreateFramebuffer(device, &fbufCreateInfo, nullptr, &frameBuf->framebuffer));


	}
	void createSampler() {
		// Create sampler to sample from the color attachments
		VkSamplerCreateInfo sampler = vks::initializers::samplerCreateInfo();
		sampler.magFilter = VK_FILTER_LINEAR;
		sampler.minFilter = VK_FILTER_LINEAR;
		sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler.addressModeV = sampler.addressModeU;
		sampler.addressModeW = sampler.addressModeU;
		sampler.mipLodBias = 0.0f;
		sampler.maxAnisotropy = 1.0f;
		sampler.minLod = 0.0f;
		sampler.maxLod = 1.0f;
		sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		VK_CHECK_RESULT(vkCreateSampler(device, &sampler, nullptr, &colorsampler));
	}
	void prepare()
	{
		VulkanExampleBase::prepare();
		loadAssets();
		createSampler();
		prepareUniformBuffers();

		prepareBuilding(width, height, VK_FORMAT_R8G8B8A8_UNORM);
		prepareOffscreenRenderpass(velocity.pass, VK_FORMAT_R32G32B32A32_SFLOAT, width, height, 1, VK_ATTACHMENT_LOAD_OP_CLEAR);
		prepareOffscreenRenderpass(velocityMax.pass, VK_FORMAT_R32G32B32A32_SFLOAT, width, height, 1, VK_ATTACHMENT_LOAD_OP_CLEAR);
		prepareOffscreenRenderpass(temproalReproj.pass, VK_FORMAT_R8G8B8A8_UNORM, width, height, 2, VK_ATTACHMENT_LOAD_OP_CLEAR);

		setupDescriptorSetLayout();
		setupDescriptorPool();
		setupDescriptorSet();

		preparePipelines();
		buildCommandBuffers();
		prepared = true;
	}

	virtual void render()
	{
		if (!prepared)
			return;
		draw();
		current = 1 - current;
		//	updateUniformBuffers();
		updateTemproalUniformBuffers();

		updateDescriptorSet();
		buildCommandBuffers();

	}

	virtual void viewChanged()
	{
		//	updateUniformBuffers();
		//	updateTemproalUniformBuffers();

	}


};

VULKAN_EXAMPLE_MAIN()
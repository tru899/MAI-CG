#include "graphics.hpp"

#include <cstdint>
#include <cstring>

#include <fstream>
#include <iostream>
#include <vector>

#include "graphics_internal.hpp"

namespace graphics {

namespace {

VkCommandPool vk_upload_command_pool = VK_NULL_HANDLE;
VkCommandBuffer vk_upload_command_buffer = VK_NULL_HANDLE;
VkFence vk_upload_fence = VK_NULL_HANDLE;
Buffer staging_buffer;
VkDeviceSize staging_offset = 0;

bool reserveStaging(const void* data, VkDeviceSize size, VkDeviceSize& offset) {
	offset = (staging_offset + 15) / 16 * 16;
	if (offset + size > staging_buffer.size) {
		std::cerr << "Staging buffer is too small for " << size << " bytes\n";
		return false;
	}

	std::memcpy(static_cast<char*>(staging_buffer.memory) + offset, data, size);
	vmaFlushAllocation(internal::context.allocator, staging_buffer.allocation, offset, size);
	staging_offset = offset + size;
	return true;
}

}

bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, Buffer& buffer) {
	auto& context = internal::context;

	const VkBufferCreateInfo buffer_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};

	const VmaAllocationCreateInfo allocation_info = {
		.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
		         VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
		.usage = VMA_MEMORY_USAGE_AUTO,
	};

	VmaAllocationInfo allocation_result = {};
	if (vmaCreateBuffer(context.allocator, &buffer_info, &allocation_info,
	                    &buffer.buffer, &buffer.allocation, &allocation_result) != VK_SUCCESS) {
		std::cerr << "Failed to create and allocate buffer\n";
		return false;
	}

	buffer.memory = allocation_result.pMappedData;
	buffer.size = size;
	return true;
}

bool createDeviceBuffer(VkDeviceSize size, VkBufferUsageFlags usage, Buffer& buffer) {
	const VkBufferCreateInfo buffer_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};

	const VmaAllocationCreateInfo allocation_info = {
		.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
	};

	if (vmaCreateBuffer(internal::context.allocator, &buffer_info, &allocation_info,
	                    &buffer.buffer, &buffer.allocation, nullptr) != VK_SUCCESS) {
		std::cerr << "Failed to create and allocate device buffer\n";
		return false;
	}

	buffer.memory = nullptr;
	buffer.size = size;
	return true;
}

void writeBuffer(const Buffer& buffer, const void* data, size_t size, size_t offset) {
	if (buffer.memory == nullptr || offset + size > buffer.size) {
		std::cerr << "Buffer write of " << size << " bytes does not fit\n";
		return;
	}

	std::memcpy(static_cast<char*>(buffer.memory) + offset, data, size);

	vmaFlushAllocation(internal::context.allocator, buffer.allocation, offset, size);
}

void destroyBuffer(Buffer& buffer) {
	vmaDestroyBuffer(internal::context.allocator, buffer.buffer, buffer.allocation);
	buffer = {};
}

bool createImage(uint32_t width, uint32_t height, VkFormat format, Image& image) {
	auto& context = internal::context;

	const VkImageCreateInfo image_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = format,
		.extent = { width, height, 1 },
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};

	const VmaAllocationCreateInfo allocation_info = {
		.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
	};

	if (vmaCreateImage(context.allocator, &image_info, &allocation_info,
	                   &image.image, &image.allocation, nullptr) != VK_SUCCESS) {
		std::cerr << "Failed to create and allocate image\n";
		return false;
	}

	const VkImageViewCreateInfo view_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = image.image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = format,
		.subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		},
	};

	if (vkCreateImageView(context.device, &view_info, nullptr, &image.view) != VK_SUCCESS) {
		std::cerr << "Failed to create image view\n";
		destroyImage(image);
		return false;
	}

	image.width = width;
	image.height = height;
	return true;
}

void destroyImage(Image& image) {
	vkDestroyImageView(internal::context.device, image.view, nullptr);
	vmaDestroyImage(internal::context.allocator, image.image, image.allocation);
	image = {};
}

bool createUploader(VkDeviceSize staging_size) {
	auto& context = internal::context;

	const VkCommandPoolCreateInfo command_pool = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = context.graphics_queue_index,
	};

	if (vkCreateCommandPool(context.device, &command_pool, nullptr, &vk_upload_command_pool) != VK_SUCCESS) {
		std::cerr << "Failed to create upload command pool\n";
		return false;
	}

	const VkCommandBufferAllocateInfo command_buffer = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = vk_upload_command_pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};

	if (vkAllocateCommandBuffers(context.device, &command_buffer, &vk_upload_command_buffer) != VK_SUCCESS) {
		std::cerr << "Failed to allocate upload command buffer\n";
		return false;
	}

	const VkFenceCreateInfo fence = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
	};

	if (vkCreateFence(context.device, &fence, nullptr, &vk_upload_fence) != VK_SUCCESS) {
		std::cerr << "Failed to create upload fence\n";
		return false;
	}

	return createBuffer(staging_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging_buffer);
}

void destroyUploader() {
	auto& context = internal::context;

	destroyBuffer(staging_buffer);
	vkDestroyFence(context.device, vk_upload_fence, nullptr);
	vkDestroyCommandPool(context.device, vk_upload_command_pool, nullptr);

	vk_upload_fence = VK_NULL_HANDLE;
	vk_upload_command_buffer = VK_NULL_HANDLE;
	vk_upload_command_pool = VK_NULL_HANDLE;
}

bool beginUpload() {
	staging_offset = 0;

	vkResetCommandBuffer(vk_upload_command_buffer, 0);

	const VkCommandBufferBeginInfo begin = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};

	return vkBeginCommandBuffer(vk_upload_command_buffer, &begin) == VK_SUCCESS;
}

bool uploadBuffer(const Buffer& buffer, const void* data, VkDeviceSize size) {
	VkDeviceSize offset = 0;
	if (size == 0 || size > buffer.size || !reserveStaging(data, size, offset)) {
		return false;
	}

	const VkBufferCopy region = {
		.srcOffset = offset,
		.dstOffset = 0,
		.size = size,
	};

	vkCmdCopyBuffer(vk_upload_command_buffer, staging_buffer.buffer, buffer.buffer, 1, &region);

	const VkBufferMemoryBarrier barrier = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.buffer = buffer.buffer,
		.offset = 0,
		.size = size,
	};

	vkCmdPipelineBarrier(vk_upload_command_buffer,
	                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
	                     0, 0, nullptr, 1, &barrier, 0, nullptr);
	return true;
}

bool uploadImage(const Image& image, const void* pixels) {
	VkDeviceSize offset = 0;
	const VkDeviceSize size = VkDeviceSize(image.width) * image.height * 4;
	if (!reserveStaging(pixels, size, offset)) {
		return false;
	}

	const VkImageSubresourceRange range = {
		.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.baseMipLevel = 0,
		.levelCount = 1,
		.baseArrayLayer = 0,
		.layerCount = 1,
	};

	const VkImageMemoryBarrier to_transfer = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = image.image,
		.subresourceRange = range,
	};

	vkCmdPipelineBarrier(vk_upload_command_buffer,
	                     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
	                     0, 0, nullptr, 0, nullptr, 1, &to_transfer);

	const VkBufferImageCopy region = {
		.bufferOffset = offset,
		.bufferRowLength = 0,
		.bufferImageHeight = 0,
		.imageSubresource = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.mipLevel = 0,
			.baseArrayLayer = 0,
			.layerCount = 1,
		},
		.imageOffset = { 0, 0, 0 },
		.imageExtent = { image.width, image.height, 1 },
	};

	vkCmdCopyBufferToImage(vk_upload_command_buffer, staging_buffer.buffer, image.image,
	                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	const VkImageMemoryBarrier to_shader = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = image.image,
		.subresourceRange = range,
	};

	vkCmdPipelineBarrier(vk_upload_command_buffer,
	                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
	                     0, 0, nullptr, 0, nullptr, 1, &to_shader);
	return true;
}

bool endUpload() {
	auto& context = internal::context;

	if (vkEndCommandBuffer(vk_upload_command_buffer) != VK_SUCCESS) {
		std::cerr << "Failed to record upload commands\n";
		return false;
	}

	const VkSubmitInfo submit = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.commandBufferCount = 1,
		.pCommandBuffers = &vk_upload_command_buffer,
	};

	vkResetFences(context.device, 1, &vk_upload_fence);
	if (vkQueueSubmit(context.graphics_queue, 1, &submit, vk_upload_fence) != VK_SUCCESS) {
		std::cerr << "Failed to submit upload commands\n";
		return false;
	}

	return vkWaitForFences(context.device, 1, &vk_upload_fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS;
}

VkShaderModule loadShaderModule(const char* path) {
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file) {
		std::cerr << "Failed to open shader file " << path
		          << " (run from the project root, compile shaders with glslc)\n";
		return VK_NULL_HANDLE;
	}

	const size_t size = size_t(file.tellg());
	file.seekg(0);

	std::vector<uint32_t> code((size + 3) / 4);
	file.read(reinterpret_cast<char*>(code.data()), std::streamsize(size));

	const VkShaderModuleCreateInfo shader_module = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = size,
		.pCode = code.data(),
	};

	VkShaderModule module = VK_NULL_HANDLE;
	if (vkCreateShaderModule(internal::context.device, &shader_module, nullptr, &module) != VK_SUCCESS) {
		std::cerr << "Failed to create shader module from " << path << '\n';
		return VK_NULL_HANDLE;
	}

	return module;
}

VkPipeline createPipeline(const PipelineDescription& description) {
	auto& context = internal::context;

	const VkShaderModule vertex_shader = loadShaderModule(description.vertex_shader);
	const VkShaderModule fragment_shader = loadShaderModule(description.fragment_shader);

	if (vertex_shader == VK_NULL_HANDLE || fragment_shader == VK_NULL_HANDLE) {
		vkDestroyShaderModule(context.device, vertex_shader, nullptr);
		vkDestroyShaderModule(context.device, fragment_shader, nullptr);
		return VK_NULL_HANDLE;
	}

	const VkPipelineShaderStageCreateInfo stages[] = {
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = vertex_shader,
			.pName = "main",
		},
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = fragment_shader,
			.pName = "main",
		},
	};

	const VkVertexInputBindingDescription vertex_binding = {
		.binding = 0,
		.stride = description.vertex_stride,
		.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
	};

	const VkPipelineVertexInputStateCreateInfo vertex_input = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &vertex_binding,
		.vertexAttributeDescriptionCount = description.attribute_count,
		.pVertexAttributeDescriptions = description.attributes,
	};

	const VkPipelineInputAssemblyStateCreateInfo input_assembly = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = description.topology,
	};

	const VkPipelineViewportStateCreateInfo viewport = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1,
	};

	const VkPipelineRasterizationStateCreateInfo rasterization = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = description.cull_mode,
		.frontFace = VK_FRONT_FACE_CLOCKWISE,
		.lineWidth = 1.0f,
	};

	const VkPipelineMultisampleStateCreateInfo multisample = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
	};

	const VkPipelineDepthStencilStateCreateInfo depth_stencil = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_TRUE,
		.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
	};

	const VkPipelineColorBlendAttachmentState color_blend_attachment = {
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
		                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
	};

	const VkPipelineColorBlendStateCreateInfo color_blend = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &color_blend_attachment,
	};

	const VkDynamicState dynamic_states[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
	};

	const VkPipelineDynamicStateCreateInfo dynamic_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = sizeof(dynamic_states) / sizeof(dynamic_states[0]),
		.pDynamicStates = dynamic_states,
	};

	const VkGraphicsPipelineCreateInfo pipeline = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = sizeof(stages) / sizeof(stages[0]),
		.pStages = stages,
		.pVertexInputState = &vertex_input,
		.pInputAssemblyState = &input_assembly,
		.pViewportState = &viewport,
		.pRasterizationState = &rasterization,
		.pMultisampleState = &multisample,
		.pDepthStencilState = &depth_stencil,
		.pColorBlendState = &color_blend,
		.pDynamicState = &dynamic_state,
		.layout = description.layout,
		.renderPass = context.render_pass,
		.subpass = 0,
	};

	VkPipeline result = VK_NULL_HANDLE;
	if (vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &result) != VK_SUCCESS) {
		std::cerr << "Failed to create graphics pipeline\n";
		result = VK_NULL_HANDLE;
	}

	vkDestroyShaderModule(context.device, vertex_shader, nullptr);
	vkDestroyShaderModule(context.device, fragment_shader, nullptr);

	return result;
}

}

#pragma once

#include <cstddef>
#include <cstdint>

#include <vulkan/vulkan_core.h>

#include <vk_mem_alloc.h>

namespace graphics {

struct Buffer {
	VkBuffer buffer = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;
	void* memory = nullptr;
	VkDeviceSize size = 0;
};

bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, Buffer& buffer);

void writeBuffer(const Buffer& buffer, const void* data, size_t size);

void destroyBuffer(Buffer& buffer);

VkShaderModule loadShaderModule(const char* path);

struct PipelineDescription {
	const char* vertex_shader;
	const char* fragment_shader;
	uint32_t vertex_stride;
	const VkVertexInputAttributeDescription* attributes;
	uint32_t attribute_count;
	VkPrimitiveTopology topology;
	VkCullModeFlags cull_mode;
	VkPipelineLayout layout;
};

VkPipeline createPipeline(const PipelineDescription& description);

}

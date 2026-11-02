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

struct Image {
	VkImage image = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;
	VkImageView view = VK_NULL_HANDLE;
	uint32_t width = 0;
	uint32_t height = 0;
};

bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, Buffer& buffer);

bool createDeviceBuffer(VkDeviceSize size, VkBufferUsageFlags usage, Buffer& buffer);

void writeBuffer(const Buffer& buffer, const void* data, size_t size, size_t offset = 0);

void destroyBuffer(Buffer& buffer);

bool createImage(uint32_t width, uint32_t height, VkFormat format, Image& image);

void destroyImage(Image& image);

bool createUploader(VkDeviceSize staging_size);

void destroyUploader();

bool beginUpload();

bool uploadBuffer(const Buffer& buffer, const void* data, VkDeviceSize size);

bool uploadImage(const Image& image, const void* pixels);

bool endUpload();

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

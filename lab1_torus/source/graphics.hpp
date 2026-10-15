#pragma once

#include <cstddef>

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

}

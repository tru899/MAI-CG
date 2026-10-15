#include "graphics.hpp"

#include <cstdint>
#include <cstring>

#include <fstream>
#include <iostream>
#include <vector>

#include "graphics_internal.hpp"

namespace graphics {

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

void writeBuffer(const Buffer& buffer, const void* data, size_t size) {
	std::memcpy(buffer.memory, data, size);

	vmaFlushAllocation(internal::context.allocator, buffer.allocation, 0, size);
}

void destroyBuffer(Buffer& buffer) {
	vmaDestroyBuffer(internal::context.allocator, buffer.buffer, buffer.allocation);
	buffer = {};
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

}

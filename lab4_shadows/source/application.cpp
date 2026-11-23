#include "application.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <imgui.h>
#include <stb_image.h>

#include "gizmos.hpp"
#include "graphics.hpp"
#include "math.hpp"
#include "mesh.hpp"
#include "plane.hpp"
#include "shader_data.hpp"
#include "torus.hpp"

namespace application {

namespace {

constexpr int max_segments = 128; 	// шаги по кругу u
constexpr int max_sides = 64; 		// шаги вокруг трубки v

constexpr uint32_t initial_light_capacity = 16;
constexpr uint32_t initial_gizmo_capacity = 1024;
constexpr float gizmo_cube_half_size = 0.08f;
constexpr float gizmo_cone_length = 1.2f;
constexpr VkDeviceSize staging_buffer_size = 64 * 1024 * 1024;
constexpr char textures_directory[] = "textures";
constexpr uint32_t shadow_map_size = 4096;

enum Projection : int {
	ProjectionPerspective,
	ProjectionOrthographic,
};

enum Trajectory : int {
	TrajectoryNone,
	TrajectoryCircle,
	TrajectoryLissajous,
	TrajectoryRose,
};

enum ModelIndex : uint32_t {
	ModelTorus,
	ModelFloor,
	ModelCount,
};

struct Material {
	math::Vector3 specular;
	float shininess;
	int texture;
	int tiling[2];
};

struct Texture {
	std::string name;
	graphics::Image image;
	VkDescriptorSet set = VK_NULL_HANDLE;
};

struct PointLightSettings {
	math::Vector3 position;
	math::Vector3 color;
	float intensity;
};

struct SpotLightSettings {
	math::Vector3 position;
	math::Vector3 direction;
	math::Vector3 color;
	float intensity;
	float inner_degrees;
	float outer_degrees;
};

struct CameraSettings {
	int mode = ProjectionPerspective;
	math::Vector3 position = { 0.0f, -2.5f, -7.0f };
	math::Vector3 rotation_degrees = { -20.0f, 0.0f, 0.0f };
	float fov_degrees = 60.0f;
	float z_near = 0.1f;
	float z_far = 100.0f;
};

CameraSettings camera;

struct {
	math::Vector3 position = { 0.0f, 0.0f, 0.0f };
	math::Vector3 rotation_degrees = { -120.0f, 0.0f, 0.0f };
	math::Vector3 scale = { 1.0f, 1.0f, 1.0f };
} transform;

struct {
	bool playing = true;
	float speed = 1.0f;
	float time = 0.0f;
	int trajectory = TrajectoryLissajous;
	float radius = 1.5f;
	float depth = 1.0f;
	int frequency_x = 3;
	int frequency_y = 2;
	int petals = 3;
	math::Vector3 spin_degrees_per_second = { 0.0f, 45.0f, 0.0f };
} animation;

Material torus_material = { { 0.6f, 0.6f, 0.6f }, 32.0f, 0, { 3, 1 } };

struct {
	bool enabled = false;
	float amplitude = 0.03f;
	int frequency = 2;
	float speed = 2.0f;
	float time = 0.0f;
} wobbling;

struct {
	Material material = { { 0.25f, 0.25f, 0.25f }, 16.0f, 1, { 3, 3 } };
	float height = 3.0f;
	float size = 6.0f;
} floor_settings;

struct {
	math::Vector3 color = { 1.0f, 1.0f, 1.0f };
	float intensity = 0.1f;
} ambient;

struct {
	math::Vector3 direction = { 0.4f, 1.0f, 0.6f };
	math::Vector3 color = { 1.0f, 0.95f, 0.85f };
	float intensity = 0.5f;
} directional;

// Карта теней для направленного света
struct {
	bool enabled = true;
	float area = 10.0f;			// половина стороны ортографического объёма: покрыта область 2·area × 2·area
	float distance = 20.0f;		// расстояние от центра сцены до «камеры солнца»
	float bias_constant = 1.25f;	// смещение глубины (depth bias): постоянная составляющая
	float bias_slope = 1.75f;	// смещение глубины: составляющая, зависящая от наклона поверхности
} shadow;

std::vector<PointLightSettings> point_lights = {
	{ { -1.0f, -2.0f, -2.5f }, { 1.0f, 0.55f, 0.25f }, 4.0f },
};

std::vector<SpotLightSettings> spot_lights = {
	{ { 1.5f, -3.5f, 0.0f }, { -0.3f, 1.0f, 0.0f }, { 0.35f, 0.65f, 1.0f }, 20.0f, 15.0f, 25.0f },
};

bool show_gizmos = true;

torus::Parameters torus_parameters = { 1.0f, 0.4f, 48, 24 };
bool mesh_dirty = true;

double previous_time = -1.0;
math::Matrix4 torus_model = math::Matrix4::identity();
math::Matrix4 torus_normal_matrix = math::Matrix4::identity();
math::Matrix4 floor_model = math::Matrix4::identity();
math::Matrix4 floor_normal_matrix = math::Matrix4::identity();

graphics::Buffer torus_vertex_buffer;
graphics::Buffer torus_index_buffer;
uint32_t torus_index_count = 0;

graphics::Buffer floor_vertex_buffer;
graphics::Buffer floor_index_buffer;
uint32_t floor_index_count = 0;

graphics::Buffer scene_uniform_buffer;
graphics::Buffer model_uniform_buffer;
VkDeviceSize model_uniform_stride = 0;

graphics::Buffer point_light_buffer;
uint32_t point_light_capacity = 0;
graphics::Buffer spot_light_buffer;
uint32_t spot_light_capacity = 0;

graphics::Buffer gizmo_vertex_buffer;
uint32_t gizmo_vertex_capacity = 0;
uint32_t gizmo_triangle_vertex_count = 0;
uint32_t gizmo_line_vertex_count = 0;

std::vector<Texture> textures;
VkSampler vk_sampler;

VkDescriptorSetLayout vk_scene_set_layout;
VkDescriptorSetLayout vk_material_set_layout;
VkDescriptorPool vk_descriptor_pool;
VkDescriptorSet vk_scene_set;

VkPipelineLayout vk_model_pipeline_layout;
VkPipelineLayout vk_gizmo_pipeline_layout;
VkPipeline vk_model_pipeline;
VkPipeline vk_gizmo_triangle_pipeline;
VkPipeline vk_gizmo_line_pipeline;

graphics::Image shadow_map;
VkFormat shadow_format = VK_FORMAT_UNDEFINED;
VkSampler vk_shadow_sampler;
graphics::Buffer shadow_uniform_buffer;
VkDescriptorSetLayout vk_shadow_set_layout;
VkDescriptorSet vk_shadow_set;
VkPipelineLayout vk_shadow_pipeline_layout;
VkPipeline vk_shadow_pipeline;

void writeBufferDescriptor(VkDescriptorSet set, uint32_t binding, VkDescriptorType type,
                           const graphics::Buffer& buffer, VkDeviceSize range = VK_WHOLE_SIZE) {
	const VkDescriptorBufferInfo buffer_info = {
		.buffer = buffer.buffer,
		.offset = 0,
		.range = range,
	};

	const VkWriteDescriptorSet write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = set,
		.dstBinding = binding,
		.descriptorCount = 1,
		.descriptorType = type,
		.pBufferInfo = &buffer_info,
	};

	vkUpdateDescriptorSets(graphics::internal::context.device, 1, &write, 0, nullptr);
}

void writeImageDescriptor(VkDescriptorSet set, uint32_t binding, const graphics::Image& image,
                          VkSampler sampler) {
	const VkDescriptorImageInfo image_info = {
		.sampler = sampler,
		.imageView = image.view,
		.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	};

	const VkWriteDescriptorSet write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = set,
		.dstBinding = binding,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		.pImageInfo = &image_info,
	};

	vkUpdateDescriptorSets(graphics::internal::context.device, 1, &write, 0, nullptr);
}

bool growBuffer(graphics::Buffer& buffer, uint32_t& capacity, size_t count,
                VkDeviceSize element_size, VkBufferUsageFlags usage, bool device_local = false) {
	if (count <= capacity) {
		return false;
	}

	const uint32_t new_capacity = std::max(uint32_t(count), capacity * 2);
	graphics::Buffer bigger;
	const bool created = device_local
		? graphics::createDeviceBuffer(new_capacity * element_size, usage, bigger)
		: graphics::createBuffer(new_capacity * element_size, usage, bigger);
	if (!created) {
		return false;
	}

	graphics::destroyBuffer(buffer);
	buffer = bigger;
	capacity = new_capacity;
	return true;
}

bool isImageFile(const std::filesystem::path& path) {
	std::string extension = path.extension().string();
	std::transform(extension.begin(), extension.end(), extension.begin(),
	               [](unsigned char c) { return char(std::tolower(c)); });

	for (const char* known : { ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif" }) {
		if (extension == known) {
			return true;
		}
	}
	return false;
}

void loadTexture(const std::filesystem::path& path) {
	int width = 0;
	int height = 0;
	int channels = 0;
	stbi_uc* pixels = stbi_load(path.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
	if (pixels == nullptr) {
		std::cerr << "Failed to load " << path.string() << ": " << stbi_failure_reason() << '\n';
		return;
	}

	Texture texture = { .name = path.filename().string() };
	const bool uploaded =
		graphics::createImage(uint32_t(width), uint32_t(height), VK_FORMAT_R8G8B8A8_SRGB, texture.image) &&
		graphics::beginUpload() &&
		graphics::uploadImage(texture.image, pixels) &&
		graphics::endUpload();

	stbi_image_free(pixels);

	if (!uploaded) {
		std::cerr << "Failed to upload " << path.string() << '\n';
		graphics::destroyImage(texture.image);
		return;
	}

	textures.push_back(texture);
}

bool createTextures() {
	std::vector<std::filesystem::path> paths;
	std::error_code error;
	for (const auto& entry : std::filesystem::directory_iterator(textures_directory, error)) {
		if (entry.is_regular_file() && isImageFile(entry.path())) {
			paths.push_back(entry.path());
		}
	}
	std::sort(paths.begin(), paths.end());

	for (const auto& path : paths) {
		loadTexture(path);
	}

	if (textures.empty()) {
		std::cerr << "No images found in " << textures_directory << "/ (run from the project root)\n";
		return false;
	}

	floor_settings.material.texture = std::min(floor_settings.material.texture, int(textures.size()) - 1);

	const VkSamplerCreateInfo sampler = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.maxLod = 0.0f,
	};

	if (vkCreateSampler(graphics::internal::context.device, &sampler, nullptr, &vk_sampler) != VK_SUCCESS) {
		std::cerr << "Failed to create sampler\n";
		return false;
	}

	return true;
}

bool createShadowMap() {
	auto& context = graphics::internal::context;

	shadow_format = graphics::findShadowMapFormat();
	if (shadow_format == VK_FORMAT_UNDEFINED) {
		std::cerr << "No depth format that can be both rendered to and sampled\n";
		return false;
	}

	if (!graphics::createDepthImage(shadow_map_size, shadow_map_size, shadow_format, shadow_map)) {
		return false;
	}

	// Линейная фильтрация вместе со сравнением даёт «аппаратный» PCF: усреднение 2×2 результатов сравнения.
	// Если формат её не поддерживает (на некоторых GPU и MoltenVK), остаётся жёсткая граница тени.
	VkFormatProperties properties;
	vkGetPhysicalDeviceFormatProperties(context.physical_device, shadow_format, &properties);
	const bool can_filter = (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
	const VkFilter filter = can_filter ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;

	// Сэмплер со сравнением глубины: возвращает 1.0, если глубина точки меньше глубины в карте, иначе 0.0.
	// Вне карты берётся белый цвет рамки (глубина 1.0: ничто не закрывает свет).
	const VkSamplerCreateInfo sampler = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = filter,
		.minFilter = filter,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.compareEnable = VK_TRUE,
		.compareOp = VK_COMPARE_OP_LESS,
		.maxLod = 0.0f,
		.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
	};

	if (vkCreateSampler(context.device, &sampler, nullptr, &vk_shadow_sampler) != VK_SUCCESS) {
		std::cerr << "Failed to create shadow sampler\n";
		return false;
	}

	return true;
}

bool createBuffers() {
	auto& context = graphics::internal::context;

	const VkDeviceSize max_torus_vertices = VkDeviceSize(max_segments + 1) * (max_sides + 1);
	const VkDeviceSize max_torus_indices = VkDeviceSize(max_segments) * max_sides * 6;

	if (!graphics::createDeviceBuffer(max_torus_vertices * sizeof(mesh::Vertex),
	                                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, torus_vertex_buffer) ||
	    !graphics::createDeviceBuffer(max_torus_indices * sizeof(uint32_t),
	                                  VK_BUFFER_USAGE_INDEX_BUFFER_BIT, torus_index_buffer)) {
		return false;
	}

	const mesh::Mesh floor_mesh = plane::generate();
	const VkDeviceSize floor_vertices_size = floor_mesh.vertices.size() * sizeof(mesh::Vertex);
	const VkDeviceSize floor_indices_size = floor_mesh.indices.size() * sizeof(uint32_t);

	if (!graphics::createDeviceBuffer(floor_vertices_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, floor_vertex_buffer) ||
	    !graphics::createDeviceBuffer(floor_indices_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, floor_index_buffer) ||
	    !graphics::beginUpload() ||
	    !graphics::uploadBuffer(floor_vertex_buffer, floor_mesh.vertices.data(), floor_vertices_size) ||
	    !graphics::uploadBuffer(floor_index_buffer, floor_mesh.indices.data(), floor_indices_size) ||
	    !graphics::endUpload()) {
		return false;
	}
	floor_index_count = uint32_t(floor_mesh.indices.size());

	VkPhysicalDeviceProperties properties;
	vkGetPhysicalDeviceProperties(context.physical_device, &properties);
	const VkDeviceSize alignment = properties.limits.minUniformBufferOffsetAlignment;
	model_uniform_stride = (sizeof(shader_data::ModelUniforms) + alignment - 1) / alignment * alignment;

	point_light_capacity = initial_light_capacity;
	spot_light_capacity = initial_light_capacity;
	gizmo_vertex_capacity = initial_gizmo_capacity;

	return graphics::createBuffer(sizeof(shader_data::SceneUniforms),
	                              VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, scene_uniform_buffer) &&
	       graphics::createBuffer(model_uniform_stride * ModelCount,
	                              VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, model_uniform_buffer) &&
	       graphics::createBuffer(point_light_capacity * sizeof(shader_data::PointLight),
	                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, point_light_buffer) &&
	       graphics::createBuffer(spot_light_capacity * sizeof(shader_data::SpotLight),
	                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, spot_light_buffer) &&
	       graphics::createDeviceBuffer(gizmo_vertex_capacity * sizeof(gizmos::Vertex),
	                                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, gizmo_vertex_buffer) &&
	       graphics::createBuffer(sizeof(math::Matrix4),
	                              VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, shadow_uniform_buffer);
}

bool createDescriptors() {
	auto& context = graphics::internal::context;

	const VkDescriptorSetLayoutBinding scene_bindings[] = {
		{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		},
		{
			.binding = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		},
		{
			.binding = 2,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		},
		{
			// Карта теней: изображение глубины и сэмплер со сравнением
			.binding = 3,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		},
	};

	const VkDescriptorSetLayoutCreateInfo scene_layout = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = sizeof(scene_bindings) / sizeof(scene_bindings[0]),
		.pBindings = scene_bindings,
	};

	if (vkCreateDescriptorSetLayout(context.device, &scene_layout, nullptr,
	                                &vk_scene_set_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create scene descriptor set layout\n";
		return false;
	}

	const VkDescriptorSetLayoutBinding material_bindings[] = {
		{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		},
		{
			.binding = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		},
	};

	const VkDescriptorSetLayoutCreateInfo material_layout = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = sizeof(material_bindings) / sizeof(material_bindings[0]),
		.pBindings = material_bindings,
	};

	if (vkCreateDescriptorSetLayout(context.device, &material_layout, nullptr,
	                                &vk_material_set_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create material descriptor set layout\n";
		return false;
	}

	// Проход теней: вместо набора сцены используется набор с одной матрицей «камеры солнца»
	const VkDescriptorSetLayoutBinding shadow_binding = {
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
	};

	const VkDescriptorSetLayoutCreateInfo shadow_layout = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &shadow_binding,
	};

	if (vkCreateDescriptorSetLayout(context.device, &shadow_layout, nullptr,
	                                &vk_shadow_set_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create shadow descriptor set layout\n";
		return false;
	}

	const uint32_t texture_count = uint32_t(textures.size());

	const VkDescriptorPoolSize pool_sizes[] = {
		{
			.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 2, // сцена и проход теней
		},
		{
			.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 2,
		},
		{
			.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			.descriptorCount = texture_count,
		},
		{
			.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = texture_count + 1, // текстуры и карта теней
		},
	};

	const VkDescriptorPoolCreateInfo descriptor_pool = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 2 + texture_count,
		.poolSizeCount = sizeof(pool_sizes) / sizeof(pool_sizes[0]),
		.pPoolSizes = pool_sizes,
	};

	if (vkCreateDescriptorPool(context.device, &descriptor_pool, nullptr,
	                           &vk_descriptor_pool) != VK_SUCCESS) {
		std::cerr << "Failed to create descriptor pool\n";
		return false;
	}

	std::vector<VkDescriptorSetLayout> set_layouts(2 + texture_count, vk_material_set_layout);
	set_layouts[0] = vk_scene_set_layout;
	set_layouts[1 + texture_count] = vk_shadow_set_layout;

	const VkDescriptorSetAllocateInfo descriptor_sets = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = vk_descriptor_pool,
		.descriptorSetCount = uint32_t(set_layouts.size()),
		.pSetLayouts = set_layouts.data(),
	};

	std::vector<VkDescriptorSet> sets(set_layouts.size());
	if (vkAllocateDescriptorSets(context.device, &descriptor_sets, sets.data()) != VK_SUCCESS) {
		std::cerr << "Failed to allocate descriptor sets\n";
		return false;
	}

	vk_scene_set = sets[0];

	writeBufferDescriptor(vk_scene_set, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, scene_uniform_buffer);
	writeBufferDescriptor(vk_scene_set, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, point_light_buffer);
	writeBufferDescriptor(vk_scene_set, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, spot_light_buffer);
	writeImageDescriptor(vk_scene_set, 3, shadow_map, vk_shadow_sampler);

	vk_shadow_set = sets[1 + texture_count];
	writeBufferDescriptor(vk_shadow_set, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, shadow_uniform_buffer);

	for (uint32_t i = 0; i < texture_count; ++i) {
		Texture& texture = textures[i];
		texture.set = sets[1 + i];
		writeBufferDescriptor(texture.set, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, model_uniform_buffer,
		                      sizeof(shader_data::ModelUniforms));
		writeImageDescriptor(texture.set, 1, texture.image, vk_sampler);
	}

	return true;
}

bool createPipelines() {
	auto& context = graphics::internal::context;

	const VkDescriptorSetLayout model_set_layouts[] = {
		vk_scene_set_layout,
		vk_material_set_layout,
	};

	const VkPushConstantRange sampling_effect_range = {
		.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		.offset = 0,
		.size = sizeof(shader_data::SamplingEffect),
	};

	const VkPipelineLayoutCreateInfo model_pipeline_layout = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = sizeof(model_set_layouts) / sizeof(model_set_layouts[0]),
		.pSetLayouts = model_set_layouts,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &sampling_effect_range,
	};

	const VkPipelineLayoutCreateInfo gizmo_pipeline_layout = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &vk_scene_set_layout,
	};

	// Проход теней: набор 0 - матрица солнца, набор 1 - тот же набор материала (матрица модели)
	const VkDescriptorSetLayout shadow_set_layouts[] = {
		vk_shadow_set_layout,
		vk_material_set_layout,
	};

	const VkPipelineLayoutCreateInfo shadow_pipeline_layout = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = sizeof(shadow_set_layouts) / sizeof(shadow_set_layouts[0]),
		.pSetLayouts = shadow_set_layouts,
	};

	if (vkCreatePipelineLayout(context.device, &model_pipeline_layout, nullptr,
	                           &vk_model_pipeline_layout) != VK_SUCCESS ||
	    vkCreatePipelineLayout(context.device, &gizmo_pipeline_layout, nullptr,
	                           &vk_gizmo_pipeline_layout) != VK_SUCCESS ||
	    vkCreatePipelineLayout(context.device, &shadow_pipeline_layout, nullptr,
	                           &vk_shadow_pipeline_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create pipeline layouts\n";
		return false;
	}

	const VkVertexInputAttributeDescription model_attributes[] = {
		{
			.location = 0,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(mesh::Vertex, position),
		},
		{
			.location = 1,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(mesh::Vertex, normal),
		},
		{
			.location = 2,
			.binding = 0,
			.format = VK_FORMAT_R32G32_SFLOAT,
			.offset = offsetof(mesh::Vertex, uv),
		},
	};

	const VkVertexInputAttributeDescription gizmo_attributes[] = {
		{
			.location = 0,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(gizmos::Vertex, position),
		},
		{
			.location = 1,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(gizmos::Vertex, color),
		},
	};

	vk_model_pipeline = graphics::createPipeline({
		.vertex_shader = "shaders/model.vert.spv",
		.fragment_shader = "shaders/model.frag.spv",
		.vertex_stride = sizeof(mesh::Vertex),
		.attributes = model_attributes,
		.attribute_count = sizeof(model_attributes) / sizeof(model_attributes[0]),
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		.cull_mode = VK_CULL_MODE_BACK_BIT,
		.layout = vk_model_pipeline_layout,
	});

	vk_gizmo_triangle_pipeline = graphics::createPipeline({
		.vertex_shader = "shaders/gizmo.vert.spv",
		.fragment_shader = "shaders/gizmo.frag.spv",
		.vertex_stride = sizeof(gizmos::Vertex),
		.attributes = gizmo_attributes,
		.attribute_count = sizeof(gizmo_attributes) / sizeof(gizmo_attributes[0]),
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		.cull_mode = VK_CULL_MODE_NONE,
		.layout = vk_gizmo_pipeline_layout,
	});

	vk_gizmo_line_pipeline = graphics::createPipeline({
		.vertex_shader = "shaders/gizmo.vert.spv",
		.fragment_shader = "shaders/gizmo.frag.spv",
		.vertex_stride = sizeof(gizmos::Vertex),
		.attributes = gizmo_attributes,
		.attribute_count = sizeof(gizmo_attributes) / sizeof(gizmo_attributes[0]),
		.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
		.cull_mode = VK_CULL_MODE_NONE,
		.layout = vk_gizmo_pipeline_layout,
	});

	// Конвейер карты теней: только положение вершины, динамический рендеринг без VkRenderPass.
	// Отсекаются передние грани (обращённые к солнцу): в карте остаётся задняя сторона замкнутых тел,
	// это почти полностью убирает самозатенение («shadow acne») на освещённых поверхностях.
	const VkVertexInputAttributeDescription shadow_attributes[] = {
		{
			.location = 0,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(mesh::Vertex, position),
		},
	};

	vk_shadow_pipeline = graphics::createDepthPipeline({
		.vertex_shader = "shaders/shadow.vert.spv",
		.vertex_stride = sizeof(mesh::Vertex),
		.attributes = shadow_attributes,
		.attribute_count = sizeof(shadow_attributes) / sizeof(shadow_attributes[0]),
		.cull_mode = VK_CULL_MODE_FRONT_BIT,
		.depth_format = shadow_format,
		.layout = vk_shadow_pipeline_layout,
	});

	return vk_model_pipeline != VK_NULL_HANDLE &&
	       vk_gizmo_triangle_pipeline != VK_NULL_HANDLE &&
	       vk_gizmo_line_pipeline != VK_NULL_HANDLE &&
	       vk_shadow_pipeline != VK_NULL_HANDLE;
}

void uploadMesh() {
	torus_parameters.segments = std::clamp(torus_parameters.segments, 3, max_segments);
	torus_parameters.sides = std::clamp(torus_parameters.sides, 3, max_sides);

	const mesh::Mesh torus_mesh = torus::generate(torus_parameters);

	const bool uploaded =
		graphics::uploadBuffer(torus_vertex_buffer, torus_mesh.vertices.data(),
		                       torus_mesh.vertices.size() * sizeof(mesh::Vertex)) &&
		graphics::uploadBuffer(torus_index_buffer, torus_mesh.indices.data(),
		                       torus_mesh.indices.size() * sizeof(uint32_t));
	torus_index_count = uploaded ? uint32_t(torus_mesh.indices.size()) : 0;
}

math::Vector3 trajectoryOffset(float t) {
	const float a = animation.radius;
	const float d = animation.depth;

	switch (animation.trajectory) {
	case TrajectoryCircle:
		return { a * std::cos(t), 0.0f, a * std::sin(t) };

	case TrajectoryLissajous:
		return {
			a * std::sin(float(animation.frequency_x) * t + math::pi / 2.0f),
			a * std::sin(float(animation.frequency_y) * t),
			d * std::sin(t),
		};

	case TrajectoryRose: {
		const float rho = a * std::cos(float(animation.petals) * t);
		return { rho * std::cos(t), rho * std::sin(t), d * std::sin(t) };
	}

	default:
		return { 0.0f, 0.0f, 0.0f };
	}
}

void computeModelMatrices() {
	const float t = animation.time;

	const math::Vector3 position = transform.position + trajectoryOffset(t);
	const math::Vector3 angles = transform.rotation_degrees + animation.spin_degrees_per_second * t;
	const math::Vector3 inverse_scale = {
		1.0f / transform.scale.x,
		1.0f / transform.scale.y,
		1.0f / transform.scale.z,
	};

	const math::Matrix4 rotation = math::Matrix4::rotationZ(math::radians(angles.z)) *
	                               math::Matrix4::rotationY(math::radians(angles.y)) *
	                               math::Matrix4::rotationX(math::radians(angles.x));

	torus_model = math::Matrix4::translation(position) * rotation * math::Matrix4::scale(transform.scale);
	torus_normal_matrix = rotation * math::Matrix4::scale(inverse_scale);

	floor_model = math::Matrix4::translation({ 0.0f, floor_settings.height, 0.0f }) *
	              math::Matrix4::scale({ floor_settings.size, 1.0f, floor_settings.size });
	floor_normal_matrix = math::Matrix4::scale({ 1.0f / floor_settings.size, 1.0f, 1.0f / floor_settings.size });
}

math::Matrix4 computeViewProjection(float aspect) {
	const math::Vector3 angles = camera.rotation_degrees;
	const math::Matrix4 view = math::Matrix4::rotationZ(math::radians(-angles.z)) *
	                           math::Matrix4::rotationX(math::radians(-angles.x)) *
	                           math::Matrix4::rotationY(math::radians(-angles.y)) *
	                           math::Matrix4::translation(camera.position * -1.0f);

	const float fov = math::radians(camera.fov_degrees);
	math::Matrix4 proj;

	if (camera.mode == ProjectionPerspective) {
		proj = math::Matrix4::perspective(fov, aspect, camera.z_near, camera.z_far);
	} else {
		const float half_height = math::length(CameraSettings{}.position) * std::tan(fov / 2.0f);
		const float half_width = half_height * aspect;
		proj = math::Matrix4::orthographic(-half_width, half_width, -half_height, half_height,
		                                   camera.z_near, camera.z_far);
	}

	return proj * view;
}

// Матрица «камеры солнца». LookAt задаёт ориентацию (камера стоит против направления света и смотрит
// в центр сцены), ортографическая проекция - параллельные лучи и покрываемую область.
math::Matrix4 computeShadowViewProjection() {
	const math::Vector3 direction = math::normalize(directional.direction);
	const math::Vector3 target = { 0.0f, 0.0f, 0.0f };
	const math::Vector3 eye = target - direction * shadow.distance;

	// «Низ» картинки - мировое +Y; если свет почти вертикален, берём другое направление
	math::Vector3 down = { 0.0f, 1.0f, 0.0f };
	if (std::abs(direction.y) > 0.99f) {
		down = { 0.0f, 0.0f, 1.0f };
	}
	const math::Matrix4 view = math::Matrix4::look_at(eye, target, down);

	const float half_size = shadow.area;
	const float z_far = shadow.distance + 2.0f * shadow.area;
	const math::Matrix4 proj = math::Matrix4::orthographic(-half_size, half_size, -half_size, half_size, 0.1f, z_far);

	return proj * view;
}

void editMaterial(Material& material) {
	if (ImGui::BeginCombo("Texture", textures[size_t(material.texture)].name.c_str())) {
		for (int i = 0; i < int(textures.size()); ++i) {
			const bool selected = i == material.texture;
			if (ImGui::Selectable(textures[size_t(i)].name.c_str(), selected)) {
				material.texture = i;
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SliderInt2("Tiling", material.tiling, 1, 16, "%d", ImGuiSliderFlags_AlwaysClamp);
	ImGui::ColorEdit3("Specular", &material.specular.x);
	ImGui::SliderFloat("Shininess", &material.shininess, 1.0f, 256.0f, "%.0f",
	                   ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
}

void drawInterface() {
	ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(400.0f, 700.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin("Lab 4: Shadow mapping");

	ImGui::Text("%.1f FPS", double(ImGui::GetIO().Framerate));
	ImGui::Text("Textures loaded from %s/: %d", textures_directory, int(textures.size()));

	if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::PushID("camera");
		ImGui::RadioButton("Perspective", &camera.mode, ProjectionPerspective);
		ImGui::SameLine();
		ImGui::RadioButton("Orthographic", &camera.mode, ProjectionOrthographic);
		ImGui::DragFloat3("Position", &camera.position.x, 0.05f);
		ImGui::DragFloat3("Rotation (deg)", &camera.rotation_degrees.x, 0.5f);
		ImGui::SliderFloat("FOV (deg)", &camera.fov_degrees, 20.0f, 120.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Near", &camera.z_near, 0.01f, 5.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Far", &camera.z_far, 10.0f, 200.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
		if (ImGui::Button("Reset camera")) {
			camera = CameraSettings{};
		}
		ImGui::PopID();
	}

	if (ImGui::CollapsingHeader("Ambient light", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::PushID("ambient");
		ImGui::ColorEdit3("Color", &ambient.color.x);
		ImGui::SliderFloat("Intensity", &ambient.intensity, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::PopID();
	}

	if (ImGui::CollapsingHeader("Directional light", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::PushID("directional");
		ImGui::DragFloat3("Direction", &directional.direction.x, 0.01f, -1.0f, 1.0f);
		ImGui::ColorEdit3("Color", &directional.color.x);
		ImGui::SliderFloat("Intensity", &directional.intensity, 0.0f, 5.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::PopID();
	}

	if (ImGui::CollapsingHeader("Shadows (directional light)", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::PushID("shadows");
		ImGui::Checkbox("Enabled", &shadow.enabled);
		ImGui::SliderFloat("Covered area", &shadow.area, 2.0f, 30.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Light distance", &shadow.distance, 5.0f, 60.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Depth bias (constant)", &shadow.bias_constant, 0.0f, 20.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Depth bias (slope)", &shadow.bias_slope, 0.0f, 10.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::TextDisabled("Shadow map: %u x %u", shadow_map_size, shadow_map_size);
		if (ImGui::Button("Reset shadows")) {
			shadow = decltype(shadow){};
		}
		ImGui::PopID();
	}

	if (ImGui::CollapsingHeader("Point lights", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::PushID("point");
		if (ImGui::Button("Add point light")) {
			point_lights.push_back({ { 0.0f, -3.5f, -2.0f }, { 1.0f, 1.0f, 1.0f }, 4.0f });
		}
		int remove_index = -1;
		for (int i = 0; i < int(point_lights.size()); ++i) {
			PointLightSettings& light = point_lights[size_t(i)];
			ImGui::PushID(i);
			if (ImGui::TreeNodeEx("light", ImGuiTreeNodeFlags_DefaultOpen, "Point light %d", i)) {
				ImGui::DragFloat3("Position", &light.position.x, 0.05f);
				ImGui::ColorEdit3("Color", &light.color.x);
				ImGui::SliderFloat("Intensity", &light.intensity, 0.0f, 50.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				if (ImGui::Button("Remove")) {
					remove_index = i;
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
		if (remove_index >= 0) {
			point_lights.erase(point_lights.begin() + remove_index);
		}
		ImGui::PopID();
	}

	if (ImGui::CollapsingHeader("Spot lights", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::PushID("spot");
		if (ImGui::Button("Add spot light")) {
			spot_lights.push_back({ { 0.0f, -3.5f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, 20.0f, 15.0f, 25.0f });
		}
		int remove_index = -1;
		for (int i = 0; i < int(spot_lights.size()); ++i) {
			SpotLightSettings& light = spot_lights[size_t(i)];
			ImGui::PushID(i);
			if (ImGui::TreeNodeEx("light", ImGuiTreeNodeFlags_DefaultOpen, "Spot light %d", i)) {
				ImGui::DragFloat3("Position", &light.position.x, 0.05f);
				ImGui::DragFloat3("Direction", &light.direction.x, 0.01f, -1.0f, 1.0f);
				ImGui::ColorEdit3("Color", &light.color.x);
				ImGui::SliderFloat("Intensity", &light.intensity, 0.0f, 50.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				ImGui::SliderFloat("Inner angle", &light.inner_degrees, 0.0f, 85.0f, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
				ImGui::SliderFloat("Outer angle", &light.outer_degrees, 0.0f, 85.0f, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
				if (ImGui::Button("Remove")) {
					remove_index = i;
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
		if (remove_index >= 0) {
			spot_lights.erase(spot_lights.begin() + remove_index);
		}
		ImGui::PopID();
	}

	if (ImGui::CollapsingHeader("Light sources", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Show light sources", &show_gizmos);
	}

	if (ImGui::CollapsingHeader("Torus")) {
		ImGui::PushID("torus");

		ImGui::SeparatorText("Material");
		editMaterial(torus_material);

		ImGui::SeparatorText("Wobbling");
		ImGui::Checkbox("Enabled", &wobbling.enabled);
		ImGui::SliderFloat("Amplitude", &wobbling.amplitude, 0.0f, 0.2f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderInt("Frequency", &wobbling.frequency, 0, 10, "%d", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Wobbling speed", &wobbling.speed, 0.0f, 10.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);

		ImGui::SeparatorText("Transform");
		ImGui::DragFloat3("Position", &transform.position.x, 0.01f);
		ImGui::DragFloat3("Rotation (deg)", &transform.rotation_degrees.x, 0.5f);
		ImGui::DragFloat3("Scale", &transform.scale.x, 0.01f, 0.05f, 5.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (ImGui::Button("Reset transform")) {
			transform.position = { 0.0f, 0.0f, 0.0f };
			transform.rotation_degrees = { -120.0f, 0.0f, 0.0f };
			transform.scale = { 1.0f, 1.0f, 1.0f };
		}

		ImGui::SeparatorText("Animation");
		if (ImGui::Button(animation.playing ? "Pause" : "Play")) {
			animation.playing = !animation.playing;
		}
		ImGui::SameLine();
		if (ImGui::Button("Restart")) {
			animation.time = 0.0f;
		}
		ImGui::SliderFloat("Speed", &animation.speed, 0.0f, 5.0f);
		ImGui::Combo("Trajectory", &animation.trajectory, "None\0Circle\0Lissajous\0Rose\0");
		ImGui::SliderFloat("Radius", &animation.radius, 0.0f, 4.0f);
		if (animation.trajectory == TrajectoryLissajous || animation.trajectory == TrajectoryRose) {
			ImGui::SliderFloat("Depth amplitude", &animation.depth, 0.0f, 3.0f);
		}
		if (animation.trajectory == TrajectoryLissajous) {
			ImGui::SliderInt("Frequency X", &animation.frequency_x, 1, 7);
			ImGui::SliderInt("Frequency Y", &animation.frequency_y, 1, 7);
		}
		if (animation.trajectory == TrajectoryRose) {
			ImGui::SliderInt("Rose k", &animation.petals, 1, 8);
		}
		ImGui::DragFloat3("Spin (deg/s)", &animation.spin_degrees_per_second.x, 1.0f, -360.0f, 360.0f);

		ImGui::SeparatorText("Mesh");
		if (ImGui::SliderFloat("Major radius R", &torus_parameters.major_radius, 0.2f, 3.0f,
		                       "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
			mesh_dirty = true;
		}
		if (ImGui::SliderFloat("Minor radius r", &torus_parameters.minor_radius, 0.05f, 1.5f,
		                       "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
			mesh_dirty = true;
		}
		if (ImGui::SliderInt("Segments", &torus_parameters.segments, 3, max_segments,
		                     "%d", ImGuiSliderFlags_AlwaysClamp)) {
			mesh_dirty = true;
		}
		if (ImGui::SliderInt("Sides", &torus_parameters.sides, 3, max_sides,
		                     "%d", ImGuiSliderFlags_AlwaysClamp)) {
			mesh_dirty = true;
		}
		ImGui::Text("%d vertices, %u triangles",
		            (torus_parameters.segments + 1) * (torus_parameters.sides + 1), torus_index_count / 3);

		ImGui::PopID();
	}

	if (ImGui::CollapsingHeader("Floor")) {
		ImGui::PushID("floor");
		editMaterial(floor_settings.material);
		ImGui::SliderFloat("Height", &floor_settings.height, -5.0f, 5.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Size", &floor_settings.size, 1.0f, 20.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::PopID();
	}

	ImGui::End();
}

math::Vector3 brightColor(math::Vector3 color) {
	const float brightest = std::max({ color.x, color.y, color.z });
	return brightest > 1e-4f ? color * (1.0f / brightest) : color;
}

void uploadLights() {
	std::vector<shader_data::PointLight> points;
	points.reserve(point_lights.size());
	for (const PointLightSettings& light : point_lights) {
		points.push_back({ light.position, light.intensity, light.color, 0.0f });
	}

	std::vector<shader_data::SpotLight> spots;
	spots.reserve(spot_lights.size());
	for (const SpotLightSettings& light : spot_lights) {
		spots.push_back({
			light.position,
			light.intensity,
			math::normalize(light.direction),
			std::cos(math::radians(light.inner_degrees)),
			light.color,
			std::cos(math::radians(light.outer_degrees)),
		});
	}

	if (growBuffer(point_light_buffer, point_light_capacity, points.size(),
	               sizeof(shader_data::PointLight), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) {
		writeBufferDescriptor(vk_scene_set, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, point_light_buffer);
	}
	if (growBuffer(spot_light_buffer, spot_light_capacity, spots.size(),
	               sizeof(shader_data::SpotLight), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) {
		writeBufferDescriptor(vk_scene_set, 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, spot_light_buffer);
	}

	points.resize(std::min(points.size(), size_t(point_light_capacity)));
	spots.resize(std::min(spots.size(), size_t(spot_light_capacity)));

	if (!points.empty()) {
		graphics::writeBuffer(point_light_buffer, points.data(), points.size() * sizeof(shader_data::PointLight));
	}
	if (!spots.empty()) {
		graphics::writeBuffer(spot_light_buffer, spots.data(), spots.size() * sizeof(shader_data::SpotLight));
	}
}

void uploadGizmos() {
	std::vector<gizmos::Vertex> triangles;
	std::vector<gizmos::Vertex> lines;

	if (show_gizmos) {
		for (const PointLightSettings& light : point_lights) {
			gizmos::appendCube(triangles, light.position, gizmo_cube_half_size, brightColor(light.color));
		}
		for (const SpotLightSettings& light : spot_lights) {
			gizmos::appendCube(triangles, light.position, gizmo_cube_half_size, brightColor(light.color));
			gizmos::appendCone(lines, light.position, light.direction, math::radians(light.outer_degrees),
			                   gizmo_cone_length, { 1.0f, 1.0f, 1.0f });
		}
	}

	std::vector<gizmos::Vertex> vertices = triangles;
	vertices.insert(vertices.end(), lines.begin(), lines.end());

	growBuffer(gizmo_vertex_buffer, gizmo_vertex_capacity, vertices.size(),
	           sizeof(gizmos::Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);

	if (vertices.size() > gizmo_vertex_capacity) {
		triangles.clear();
		lines.clear();
		vertices.clear();
	}

	gizmo_triangle_vertex_count = uint32_t(triangles.size());
	gizmo_line_vertex_count = uint32_t(lines.size());

	if (!vertices.empty() &&
	    !graphics::uploadBuffer(gizmo_vertex_buffer, vertices.data(), vertices.size() * sizeof(gizmos::Vertex))) {
		gizmo_triangle_vertex_count = 0;
		gizmo_line_vertex_count = 0;
	}
}

void writeModelUniforms(ModelIndex index, const math::Matrix4& model, const math::Matrix4& normal_matrix,
                        const Material& material) {
	const shader_data::ModelUniforms uniforms = {
		.model = model,
		.normal_matrix = normal_matrix,
		.specular = material.specular,
		.shininess = material.shininess,
		.uv_scale = { float(material.tiling[0]), float(material.tiling[1]) },
	};
	graphics::writeBuffer(model_uniform_buffer, &uniforms, sizeof(uniforms), index * model_uniform_stride);
}

void drawMesh(VkCommandBuffer command_buffer, ModelIndex index, const Material& material,
              const shader_data::SamplingEffect& effect, const graphics::Buffer& vertex_buffer,
              const graphics::Buffer& index_buffer, uint32_t index_count) {
	const uint32_t dynamic_offset = uint32_t(index * model_uniform_stride);
	vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_model_pipeline_layout,
	                        1, 1, &textures[size_t(material.texture)].set, 1, &dynamic_offset);
	vkCmdPushConstants(command_buffer, vk_model_pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
	                   0, sizeof(effect), &effect);

	const VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(command_buffer, 0, 1, &vertex_buffer.buffer, &offset);
	vkCmdBindIndexBuffer(command_buffer, index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);
	vkCmdDrawIndexed(command_buffer, index_count, 1, 0, 0, 0);
}

// Динамический рендеринг не использует VkRenderPass, который сам менял бы layout изображения,
// поэтому переходы между layout-ами и зависимости по памяти задаются барьерами вручную.
void transitionShadowMap(VkCommandBuffer command_buffer, VkImageLayout old_layout, VkImageLayout new_layout,
                         VkPipelineStageFlags source_stage, VkAccessFlags source_access,
                         VkPipelineStageFlags destination_stage, VkAccessFlags destination_access) {
	const VkImageMemoryBarrier barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = source_access,
		.dstAccessMask = destination_access,
		.oldLayout = old_layout,
		.newLayout = new_layout,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = shadow_map.image,
		.subresourceRange = {
			.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		},
	};

	vkCmdPipelineBarrier(command_buffer, source_stage, destination_stage,
	                     0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void drawMeshShadow(VkCommandBuffer command_buffer, ModelIndex index, const Material& material,
                    const graphics::Buffer& vertex_buffer, const graphics::Buffer& index_buffer,
                    uint32_t index_count) {
	// Набор 1 тот же, что и в основном проходе: из него берётся матрица модели со сдвигом в динамическом буфере
	const uint32_t dynamic_offset = uint32_t(index * model_uniform_stride);
	vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_shadow_pipeline_layout,
	                        1, 1, &textures[size_t(material.texture)].set, 1, &dynamic_offset);

	const VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(command_buffer, 0, 1, &vertex_buffer.buffer, &offset);
	vkCmdBindIndexBuffer(command_buffer, index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);
	vkCmdDrawIndexed(command_buffer, index_count, 1, 0, 0, 0);
}

// Проход 1: сцена рисуется с точки зрения солнца в изображение глубины (динамический рендеринг)
void recordShadowPass(VkCommandBuffer command_buffer) {
	// Перед записью глубины изображение переводится в layout для вложения глубины. Старое содержимое
	// не нужно (UNDEFINED): карта целиком перерисовывается каждый кадр.
	transitionShadowMap(command_buffer,
	                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
	                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
	                    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
	                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

	const VkRenderingAttachmentInfo depth_attachment = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView = shadow_map.view,
		.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.clearValue = { .depthStencil = { 1.0f, 0 } },
	};

	const VkRenderingInfo rendering = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.renderArea = { .extent = { shadow_map_size, shadow_map_size } },
		.layerCount = 1,
		.colorAttachmentCount = 0,
		.pDepthAttachment = &depth_attachment,
	};

	vkCmdBeginRendering(command_buffer, &rendering);

	if (shadow.enabled) {
		vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_shadow_pipeline);

		const VkViewport viewport = {
			.x = 0.0f,
			.y = 0.0f,
			.width = float(shadow_map_size),
			.height = float(shadow_map_size),
			.minDepth = 0.0f,
			.maxDepth = 1.0f,
		};
		vkCmdSetViewport(command_buffer, 0, 1, &viewport);

		const VkRect2D scissor = { .offset = { 0, 0 }, .extent = { shadow_map_size, shadow_map_size } };
		vkCmdSetScissor(command_buffer, 0, 1, &scissor);

		// Смещение глубины (depth bias) убирает самозатенение: значения из интерфейса
		vkCmdSetDepthBias(command_buffer, shadow.bias_constant, 0.0f, shadow.bias_slope);

		vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_shadow_pipeline_layout,
		                        0, 1, &vk_shadow_set, 0, nullptr);

		drawMeshShadow(command_buffer, ModelTorus, torus_material,
		               torus_vertex_buffer, torus_index_buffer, torus_index_count);
		drawMeshShadow(command_buffer, ModelFloor, floor_settings.material,
		               floor_vertex_buffer, floor_index_buffer, floor_index_count);
	}

	vkCmdEndRendering(command_buffer);

	// Дальше карту только читает фрагментный шейдер: ждём завершения записи глубины и меняем layout
	transitionShadowMap(command_buffer,
	                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	                    VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
	                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
}

}

bool initialize() {
	if (!graphics::createUploader(staging_buffer_size) || !createTextures() || !createShadowMap() ||
	    !createBuffers() || !createDescriptors() || !createPipelines()) {
		shutdown();
		return false;
	}

	return true;
}

void shutdown() {
	auto& context = graphics::internal::context;
	vkQueueWaitIdle(context.graphics_queue);

	vkDestroyPipeline(context.device, vk_shadow_pipeline, nullptr);
	vkDestroyPipeline(context.device, vk_gizmo_line_pipeline, nullptr);
	vkDestroyPipeline(context.device, vk_gizmo_triangle_pipeline, nullptr);
	vkDestroyPipeline(context.device, vk_model_pipeline, nullptr);
	vkDestroyPipelineLayout(context.device, vk_shadow_pipeline_layout, nullptr);
	vkDestroyPipelineLayout(context.device, vk_gizmo_pipeline_layout, nullptr);
	vkDestroyPipelineLayout(context.device, vk_model_pipeline_layout, nullptr);
	vkDestroyDescriptorPool(context.device, vk_descriptor_pool, nullptr);
	vkDestroyDescriptorSetLayout(context.device, vk_shadow_set_layout, nullptr);
	vkDestroyDescriptorSetLayout(context.device, vk_material_set_layout, nullptr);
	vkDestroyDescriptorSetLayout(context.device, vk_scene_set_layout, nullptr);

	vkDestroySampler(context.device, vk_shadow_sampler, nullptr);
	graphics::destroyImage(shadow_map);

	vkDestroySampler(context.device, vk_sampler, nullptr);
	for (Texture& texture : textures) {
		graphics::destroyImage(texture.image);
	}
	textures.clear();

	graphics::destroyBuffer(gizmo_vertex_buffer);
	graphics::destroyBuffer(spot_light_buffer);
	graphics::destroyBuffer(point_light_buffer);
	graphics::destroyBuffer(shadow_uniform_buffer);
	graphics::destroyBuffer(model_uniform_buffer);
	graphics::destroyBuffer(scene_uniform_buffer);
	graphics::destroyBuffer(floor_index_buffer);
	graphics::destroyBuffer(floor_vertex_buffer);
	graphics::destroyBuffer(torus_index_buffer);
	graphics::destroyBuffer(torus_vertex_buffer);

	graphics::destroyUploader();
}

void update(double time) {
	const float dt = previous_time < 0.0 ? 0.0f : float(time - previous_time);
	previous_time = time;

	if (animation.playing) {
		animation.time += dt * animation.speed;
	}
	wobbling.time += dt * wobbling.speed;

	drawInterface();

	computeModelMatrices();
}

void render(const graphics::internal::FrameData& fd) {
	auto& context = graphics::internal::context;

	if (fd.command_buffer == VK_NULL_HANDLE) {
		return;
	}

	uploadLights();

	graphics::beginUpload();
	if (mesh_dirty) {
		uploadMesh();
		mesh_dirty = false;
	}
	uploadGizmos();
	graphics::endUpload();

	const VkExtent2D extent = context.swapchain_extent;
	const float aspect = float(extent.width) / float(std::max(extent.height, 1u));

	const math::Matrix4 shadow_view_projection = computeShadowViewProjection();

	const shader_data::SceneUniforms scene_uniforms = {
		.view_projection = computeViewProjection(aspect),
		.shadow_view_projection = shadow_view_projection,
		.camera_position = camera.position,
		.point_light_count = uint32_t(std::min(point_lights.size(), size_t(point_light_capacity))),
		.ambient_color = ambient.color,
		.ambient_intensity = ambient.intensity,
		.directional_direction = math::normalize(directional.direction),
		.spot_light_count = uint32_t(std::min(spot_lights.size(), size_t(spot_light_capacity))),
		.directional_color = directional.color,
		.directional_intensity = directional.intensity,
		.shadow_enabled = shadow.enabled ? 1u : 0u,
	};
	graphics::writeBuffer(scene_uniform_buffer, &scene_uniforms, sizeof(scene_uniforms));
	graphics::writeBuffer(shadow_uniform_buffer, &shadow_view_projection, sizeof(shadow_view_projection));

	writeModelUniforms(ModelTorus, torus_model, torus_normal_matrix, torus_material);
	writeModelUniforms(ModelFloor, floor_model, floor_normal_matrix, floor_settings.material);

	const shader_data::SamplingEffect torus_effect = {
		.wobbling = wobbling.enabled ? 1u : 0u,
		.time = wobbling.time,
		.amplitude = wobbling.amplitude,
		.frequency = float(wobbling.frequency),
	};
	const shader_data::SamplingEffect no_effect = {};

	vkResetCommandBuffer(fd.command_buffer, 0);

	const VkCommandBufferBeginInfo command_buffer_begin = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};

	vkBeginCommandBuffer(fd.command_buffer, &command_buffer_begin);

	// Проход 1: карта теней (динамический рендеринг), затем проход 2: основная сцена
	recordShadowPass(fd.command_buffer);

	const VkClearValue clear_values[] = {
		{ .color = { .float32 = { 0.1f, 0.1f, 0.1f, 1.0f } } },
		{ .depthStencil = { 1.0f, 0 } },
	};

	const VkRenderPassBeginInfo render_pass_begin = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = context.render_pass,
		.framebuffer = fd.framebuffer,
		.renderArea = { .extent = extent },
		.clearValueCount = sizeof(clear_values) / sizeof(clear_values[0]),
		.pClearValues = clear_values,
	};

	vkCmdBeginRenderPass(fd.command_buffer, &render_pass_begin, VK_SUBPASS_CONTENTS_INLINE);

	const VkViewport viewport = {
		.x = 0.0f,
		.y = 0.0f,
		.width = float(extent.width),
		.height = float(extent.height),
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	};
	vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);

	const VkRect2D scissor = { .offset = { 0, 0 }, .extent = extent };
	vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);

	vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_model_pipeline);
	vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_model_pipeline_layout,
	                        0, 1, &vk_scene_set, 0, nullptr);

	drawMesh(fd.command_buffer, ModelTorus, torus_material, torus_effect,
		               torus_vertex_buffer, torus_index_buffer, torus_index_count);
	drawMesh(fd.command_buffer, ModelFloor, floor_settings.material, no_effect,
		               floor_vertex_buffer, floor_index_buffer, floor_index_count);

	if (gizmo_triangle_vertex_count + gizmo_line_vertex_count > 0) {
		const VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &gizmo_vertex_buffer.buffer, &offset);

		vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_gizmo_triangle_pipeline);
		vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_gizmo_pipeline_layout,
		                        0, 1, &vk_scene_set, 0, nullptr);
		if (gizmo_triangle_vertex_count > 0) {
			vkCmdDraw(fd.command_buffer, gizmo_triangle_vertex_count, 1, 0, 0);
		}

		vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_gizmo_line_pipeline);
		if (gizmo_line_vertex_count > 0) {
			vkCmdDraw(fd.command_buffer, gizmo_line_vertex_count, 1, gizmo_triangle_vertex_count, 0);
		}
	}

	vkCmdEndRenderPass(fd.command_buffer);

	vkEndCommandBuffer(fd.command_buffer);
}

} // namespace application

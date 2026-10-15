#include "application.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>

#include <imgui.h>

#include "graphics.hpp"
#include "math.hpp"
#include "torus.hpp"

namespace application {

namespace {

constexpr int max_segments = 128; 	// шаги по кругу u
constexpr int max_sides = 64; 		// шаги вокруг трубки v

struct Uniforms {
	math::Matrix4 model;
	math::Matrix4 view_projection;
	math::Vector3 color;
	float use_vertex_colors;
};

static_assert(offsetof(Uniforms, model) == 0);
static_assert(offsetof(Uniforms, view_projection) == 64);
static_assert(offsetof(Uniforms, color) == 128);
static_assert(offsetof(Uniforms, use_vertex_colors) == 140);
static_assert(sizeof(Uniforms) == 144);

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

struct {
	int mode = ProjectionPerspective;
	float fov_degrees = 60.0f;
	float z_near = 0.1f;
	float z_far = 100.0f;
	float camera_distance = 6.0f;
} projection;

struct {
	math::Vector3 position = { 0.0f, 0.0f, 0.0f };
	math::Vector3 rotation_degrees = { 60.0f, 0.0f, 0.0f };
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

struct {
	float color[3] = { 1.0f, 1.0f, 1.0f };
	bool use_vertex_colors = true;
} appearance;

torus::Parameters torus_parameters = { 1.0f, 0.4f, 48, 24 };
bool mesh_dirty = true;

double previous_time = -1.0;
math::Matrix4 model_matrix = math::Matrix4::identity();

graphics::Buffer vertex_buffer;
graphics::Buffer index_buffer;
graphics::Buffer uniform_buffer;
uint32_t index_count = 0;

VkDescriptorSetLayout vk_descriptor_set_layout;
VkDescriptorPool vk_descriptor_pool;
VkDescriptorSet vk_descriptor_set;

VkPipelineLayout vk_pipeline_layout;
VkPipeline vk_pipeline;

bool createBuffers() {
	const VkDeviceSize max_vertices = VkDeviceSize(max_segments) * max_sides;

	return graphics::createBuffer(max_vertices * sizeof(torus::Vertex),
	                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertex_buffer) &&
	       graphics::createBuffer(max_vertices * 6 * sizeof(uint32_t),
	                              VK_BUFFER_USAGE_INDEX_BUFFER_BIT, index_buffer) &&
	       graphics::createBuffer(sizeof(Uniforms),
	                              VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, uniform_buffer);
}

bool createDescriptors() {
	auto& context = graphics::internal::context;

	const VkDescriptorSetLayoutBinding descriptor_set_bindings[] = {
		{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
		},
	};

	const VkDescriptorSetLayoutCreateInfo descriptor_set_layout = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = sizeof(descriptor_set_bindings) / sizeof(descriptor_set_bindings[0]),
		.pBindings = descriptor_set_bindings,
	};

	if (vkCreateDescriptorSetLayout(context.device, &descriptor_set_layout, nullptr,
	                                &vk_descriptor_set_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create descriptor set layout\n";
		return false;
	}

	const VkDescriptorPoolSize descriptor_pool_sizes[] = {
		{
			.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
		},
	};

	const VkDescriptorPoolCreateInfo descriptor_pool = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 1,
		.poolSizeCount = sizeof(descriptor_pool_sizes) / sizeof(descriptor_pool_sizes[0]),
		.pPoolSizes = descriptor_pool_sizes,
	};

	if (vkCreateDescriptorPool(context.device, &descriptor_pool, nullptr,
	                           &vk_descriptor_pool) != VK_SUCCESS) {
		std::cerr << "Failed to create descriptor pool\n";
		return false;
	}

	const VkDescriptorSetAllocateInfo descriptor_set = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = vk_descriptor_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &vk_descriptor_set_layout,
	};

	if (vkAllocateDescriptorSets(context.device, &descriptor_set, &vk_descriptor_set) != VK_SUCCESS) {
		std::cerr << "Failed to allocate descriptor set\n";
		return false;
	}

	const VkDescriptorBufferInfo uniform_buffer_descriptor = {
		.buffer = uniform_buffer.buffer,
		.offset = 0,
		.range = sizeof(Uniforms),
	};

	const VkWriteDescriptorSet descriptor_writes[] = {
		{
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = vk_descriptor_set,
			.dstBinding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.pBufferInfo = &uniform_buffer_descriptor,
		},
	};

	vkUpdateDescriptorSets(context.device, sizeof(descriptor_writes) / sizeof(descriptor_writes[0]),
	                       descriptor_writes, 0, nullptr);

	return true;
}

bool createPipeline() {
	auto& context = graphics::internal::context;

	const VkShaderModule vertex_shader = graphics::loadShaderModule("shaders/torus.vert.spv");
	const VkShaderModule fragment_shader = graphics::loadShaderModule("shaders/torus.frag.spv");

	if (vertex_shader == VK_NULL_HANDLE || fragment_shader == VK_NULL_HANDLE) {
		vkDestroyShaderModule(context.device, vertex_shader, nullptr);
		vkDestroyShaderModule(context.device, fragment_shader, nullptr);
		return false;
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

	const VkVertexInputBindingDescription vertex_bindings[] = {
		{
			.binding = 0,
			.stride = sizeof(torus::Vertex),
			.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
		},
	};

	const VkVertexInputAttributeDescription vertex_attributes[] = {
		{
			.location = 0,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(torus::Vertex, position),
		},
		{
			.location = 1,
			.binding = 0,
			.format = VK_FORMAT_R32G32B32_SFLOAT,
			.offset = offsetof(torus::Vertex, color),
		},
	};

	const VkPipelineVertexInputStateCreateInfo vertex_input = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = sizeof(vertex_bindings) / sizeof(vertex_bindings[0]),
		.pVertexBindingDescriptions = vertex_bindings,
		.vertexAttributeDescriptionCount = sizeof(vertex_attributes) / sizeof(vertex_attributes[0]),
		.pVertexAttributeDescriptions = vertex_attributes,
	};

	const VkPipelineInputAssemblyStateCreateInfo input_assembly = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
	};

	const VkPipelineViewportStateCreateInfo viewport = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1,
	};

	const VkPipelineRasterizationStateCreateInfo rasterization = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_BACK_BIT,
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

	const VkPipelineLayoutCreateInfo pipeline_layout = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &vk_descriptor_set_layout,
	};

	if (vkCreatePipelineLayout(context.device, &pipeline_layout, nullptr,
	                           &vk_pipeline_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create pipeline layout\n";
		vkDestroyShaderModule(context.device, vertex_shader, nullptr);
		vkDestroyShaderModule(context.device, fragment_shader, nullptr);
		return false;
	}

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
		.layout = vk_pipeline_layout,
		.renderPass = context.render_pass,
		.subpass = 0,
	};

	const VkResult result = vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &pipeline,
	                                                  nullptr, &vk_pipeline);

	vkDestroyShaderModule(context.device, vertex_shader, nullptr);
	vkDestroyShaderModule(context.device, fragment_shader, nullptr);

	if (result != VK_SUCCESS) {
		std::cerr << "Failed to create graphics pipeline\n";
		return false;
	}

	return true;
}

void uploadMesh() {
	torus_parameters.segments = std::clamp(torus_parameters.segments, 3, max_segments);
	torus_parameters.sides = std::clamp(torus_parameters.sides, 3, max_sides);

	const torus::Mesh mesh = torus::generate(torus_parameters);

	graphics::writeBuffer(vertex_buffer, mesh.vertices.data(), mesh.vertices.size() * sizeof(torus::Vertex));
	graphics::writeBuffer(index_buffer, mesh.indices.data(), mesh.indices.size() * sizeof(uint32_t));
	index_count = uint32_t(mesh.indices.size());
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

math::Matrix4 computeModelMatrix() {
	const float t = animation.time;

	const math::Vector3 position = transform.position + trajectoryOffset(t);
	const math::Vector3 rotation = transform.rotation_degrees + animation.spin_degrees_per_second * t;

	return math::Matrix4::translation(position) *
	       math::Matrix4::rotationZ(math::radians(rotation.z)) *
	       math::Matrix4::rotationY(math::radians(rotation.y)) *
	       math::Matrix4::rotationX(math::radians(rotation.x)) *
	       math::Matrix4::scale(transform.scale);
}

math::Matrix4 computeViewProjection(float aspect) {
	const math::Matrix4 view = math::Matrix4::translation({ 0.0f, 0.0f, projection.camera_distance });

	const float fov = math::radians(projection.fov_degrees);
	math::Matrix4 proj;

	if (projection.mode == ProjectionPerspective) {
		proj = math::Matrix4::perspective(fov, aspect, projection.z_near, projection.z_far);
	} else {
		const float half_height = projection.camera_distance * std::tan(fov / 2.0f);
		const float half_width = half_height * aspect;
		proj = math::Matrix4::orthographic(-half_width, half_width, -half_height, half_height,
		                                   projection.z_near, projection.z_far);
	}

	return proj * view;
}

void drawInterface() {
	ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(380.0f, 0.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin("Lab 1: Torus (variant 7)");

	ImGui::Text("%.1f FPS", double(ImGui::GetIO().Framerate));

	if (ImGui::CollapsingHeader("Projection", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::RadioButton("Perspective", &projection.mode, ProjectionPerspective);
		ImGui::SameLine();
		ImGui::RadioButton("Orthographic", &projection.mode, ProjectionOrthographic);
		ImGui::SliderFloat("FOV (deg)", &projection.fov_degrees, 20.0f, 120.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Camera distance", &projection.camera_distance, 1.0f, 20.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Near", &projection.z_near, 0.01f, 5.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Far", &projection.z_far, 10.0f, 200.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
	}

	if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::DragFloat3("Position", &transform.position.x, 0.01f);
		ImGui::DragFloat3("Rotation (deg)", &transform.rotation_degrees.x, 0.5f);
		ImGui::DragFloat3("Scale", &transform.scale.x, 0.01f, 0.05f, 5.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (ImGui::Button("Reset transform")) {
			transform.position = { 0.0f, 0.0f, 0.0f };
			transform.rotation_degrees = { 60.0f, 0.0f, 0.0f };
			transform.scale = { 1.0f, 1.0f, 1.0f };
		}
	}

	if (ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen)) {
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
			ImGui::SliderInt("Petals k", &animation.petals, 1, 8);
		}
		ImGui::DragFloat3("Spin (deg/s)", &animation.spin_degrees_per_second.x, 1.0f, -360.0f, 360.0f);
	}

	if (ImGui::CollapsingHeader("Color", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::ColorEdit3("Color", appearance.color);
		ImGui::Checkbox("Procedural vertex colors", &appearance.use_vertex_colors);
	}

	if (ImGui::CollapsingHeader("Torus")) {
		if (ImGui::SliderFloat("Major radius R", &torus_parameters.major_radius, 0.2f, 3.0f)) {
			mesh_dirty = true;
		}
		if (ImGui::SliderFloat("Minor radius r", &torus_parameters.minor_radius, 0.05f, 1.5f)) {
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
		            torus_parameters.segments * torus_parameters.sides, index_count / 3);
	}

	ImGui::End();

	projection.z_far = std::max(projection.z_far, projection.z_near + 0.1f);
}

}

bool initialize() {
	if (!createBuffers() || !createDescriptors() || !createPipeline()) {
		shutdown();
		return false;
	}

	return true;
}

void shutdown() {
	auto& context = graphics::internal::context;
	vkQueueWaitIdle(context.graphics_queue);

	vkDestroyPipeline(context.device, vk_pipeline, nullptr);
	vkDestroyPipelineLayout(context.device, vk_pipeline_layout, nullptr);
	vkDestroyDescriptorPool(context.device, vk_descriptor_pool, nullptr);
	vkDestroyDescriptorSetLayout(context.device, vk_descriptor_set_layout, nullptr);

	graphics::destroyBuffer(uniform_buffer);
	graphics::destroyBuffer(index_buffer);
	graphics::destroyBuffer(vertex_buffer);
}

void update(double time) {
	const float dt = previous_time < 0.0 ? 0.0f : float(time - previous_time);
	previous_time = time;

	if (animation.playing) {
		animation.time += dt * animation.speed;
	}

	drawInterface();

	model_matrix = computeModelMatrix();
}

void render(const graphics::internal::FrameData& fd) {
	auto& context = graphics::internal::context;

	if (fd.command_buffer == VK_NULL_HANDLE) {
		return;
	}

	if (mesh_dirty) {
		uploadMesh();
		mesh_dirty = false;
	}

	const VkExtent2D extent = context.swapchain_extent;
	const float aspect = float(extent.width) / float(std::max(extent.height, 1u));

	const Uniforms uniforms = {
		.model = model_matrix,
		.view_projection = computeViewProjection(aspect),
		.color = { appearance.color[0], appearance.color[1], appearance.color[2] },
		.use_vertex_colors = appearance.use_vertex_colors ? 1.0f : 0.0f,
	};
	graphics::writeBuffer(uniform_buffer, &uniforms, sizeof(uniforms));

	vkResetCommandBuffer(fd.command_buffer, 0);

	const VkCommandBufferBeginInfo command_buffer_begin = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};

	vkBeginCommandBuffer(fd.command_buffer, &command_buffer_begin);

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

	vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline);

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

	const VkDeviceSize vertex_buffer_offset = 0;
	vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &vertex_buffer.buffer, &vertex_buffer_offset);
	vkCmdBindIndexBuffer(fd.command_buffer, index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);

	vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline_layout,
	                        0, 1, &vk_descriptor_set, 0, nullptr);

	vkCmdDrawIndexed(fd.command_buffer, index_count, 1, 0, 0, 0);

	vkCmdEndRenderPass(fd.command_buffer);

	vkEndCommandBuffer(fd.command_buffer);
}

} // namespace application

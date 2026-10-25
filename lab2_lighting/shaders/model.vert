#version 450 core

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_color;

layout(location = 0) out vec3 out_world_position;
layout(location = 1) out vec3 out_world_normal;
layout(location = 2) out vec3 out_color;

layout(std140, set = 0, binding = 0) uniform SceneUniforms {
	mat4 view_projection;
	vec3 camera_position;
	uint point_light_count;
	vec3 ambient_color;
	float ambient_intensity;
	vec3 directional_direction;
	uint spot_light_count;
	vec3 directional_color;
	float directional_intensity;
} scene_uniforms;

layout(std140, set = 1, binding = 0) uniform ModelUniforms {
	mat4 model;
	mat4 normal_matrix;
	vec3 albedo;
	float shininess;
	vec3 specular;
	float use_vertex_colors;
} model_uniforms;

void main() {
	vec4 world_position = model_uniforms.model * vec4(in_position, 1.0);
	gl_Position = scene_uniforms.view_projection * world_position;

	out_world_position = world_position.xyz;
	out_world_normal = mat3(model_uniforms.normal_matrix) * in_normal;
	out_color = in_color;
}

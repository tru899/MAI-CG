#version 450 core

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;

layout(location = 0) out vec3 out_color;

layout(std140, set = 0, binding = 0) uniform SceneUniforms {
	mat4 view_projection;
	mat4 shadow_view_projection;
	vec3 camera_position;
	uint point_light_count;
	vec3 ambient_color;
	float ambient_intensity;
	vec3 directional_direction;
	uint spot_light_count;
	vec3 directional_color;
	float directional_intensity;
	uint shadow_enabled;
} scene_uniforms;

void main() {
	gl_Position = scene_uniforms.view_projection * vec4(in_position, 1.0);
	out_color = in_color;
}

#version 450 core

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;

layout(location = 0) out vec3 out_color;

layout(std140, set = 0, binding = 0) uniform Uniforms {
	mat4 model;
	mat4 view_projection;
	vec3 color;
	float use_vertex_colors;
} uniforms;

void main() {
	gl_Position = uniforms.view_projection * uniforms.model * vec4(in_position, 1.0);

	vec3 vertex_color = mix(vec3(1.0), in_color, uniforms.use_vertex_colors);
	out_color = vertex_color * uniforms.color;
}

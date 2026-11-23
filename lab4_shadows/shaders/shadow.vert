#version 450 core

layout(location = 0) in vec3 in_position;

// Набор 0 в проходе теней содержит только матрицу «камеры солнца»
layout(std140, set = 0, binding = 0) uniform ShadowUniforms {
	mat4 light_view_projection;
} shadow_uniforms;

// Набор 1 тот же, что и в основном проходе (матрица модели берётся из динамического буфера)
layout(std140, set = 1, binding = 0) uniform ModelUniforms {
	mat4 model;
} model_uniforms;

void main() {
	gl_Position = shadow_uniforms.light_view_projection * model_uniforms.model * vec4(in_position, 1.0);
}

#version 450 core

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;

layout(location = 0) out vec3 out_world_position;
layout(location = 1) out vec3 out_world_normal;
layout(location = 2) out vec2 out_uv;
layout(location = 3) out vec4 out_shadow_position;

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

layout(std140, set = 1, binding = 0) uniform ModelUniforms {
	mat4 model;
	mat4 normal_matrix;
	vec3 specular;
	float shininess;
	vec2 uv_scale;
} model_uniforms;

void main() {
	vec4 world_position = model_uniforms.model * vec4(in_position, 1.0);
	gl_Position = scene_uniforms.view_projection * world_position;

	out_world_position = world_position.xyz;
	out_world_normal = mat3(model_uniforms.normal_matrix) * in_normal;
	out_uv = in_uv * model_uniforms.uv_scale;

	// Положение точки в системе координат «камеры солнца»: по нему во фрагментном шейдере
	// выбирается пиксель карты теней
	out_shadow_position = scene_uniforms.shadow_view_projection * world_position;
}

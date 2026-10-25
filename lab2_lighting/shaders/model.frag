#version 450 core

layout(location = 0) in vec3 in_world_position;
layout(location = 1) in vec3 in_world_normal;
layout(location = 2) in vec3 in_color;

layout(location = 0) out vec4 out_color;

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

struct PointLight {
	vec3 position;
	float intensity;
	vec3 color;
};

struct SpotLight {
	vec3 position;
	float intensity;
	vec3 direction;
	float cos_inner;
	vec3 color;
	float cos_outer;
};

layout(std430, set = 0, binding = 1) readonly buffer PointLights {
	PointLight point_lights[];
};

layout(std430, set = 0, binding = 2) readonly buffer SpotLights {
	SpotLight spot_lights[];
};

layout(std140, set = 1, binding = 0) uniform ModelUniforms {
	mat4 model;
	mat4 normal_matrix;
	vec3 albedo;
	float shininess;
	vec3 specular;
	float use_vertex_colors;
} model_uniforms;

vec3 blinnPhong(vec3 n, vec3 v, vec3 l, vec3 radiance, vec3 albedo) {
	float cos_theta = max(dot(n, l), 0.0);
	vec3 h = normalize(l + v);
	float cos_phi = max(dot(n, h), 0.0);
	float specular = cos_theta > 0.0 ? pow(cos_phi, model_uniforms.shininess) : 0.0;
	return radiance * (cos_theta * albedo + specular * model_uniforms.specular);
}

void main() {
	vec3 n = normalize(in_world_normal);
	vec3 v = normalize(scene_uniforms.camera_position - in_world_position);
	vec3 albedo = model_uniforms.albedo * mix(vec3(1.0), in_color, model_uniforms.use_vertex_colors);

	vec3 color = scene_uniforms.ambient_color * scene_uniforms.ambient_intensity * albedo;

	color += blinnPhong(n, v, -scene_uniforms.directional_direction,
	                    scene_uniforms.directional_color * scene_uniforms.directional_intensity, albedo);

	for (uint i = 0; i < scene_uniforms.point_light_count; ++i) {
		PointLight light = point_lights[i];
		vec3 to_light = light.position - in_world_position;
		float distance_squared = max(dot(to_light, to_light), 1e-4);
		vec3 radiance = light.color * light.intensity / distance_squared;
		color += blinnPhong(n, v, to_light * inversesqrt(distance_squared), radiance, albedo);
	}

	for (uint i = 0; i < scene_uniforms.spot_light_count; ++i) {
		SpotLight light = spot_lights[i];
		vec3 to_light = light.position - in_world_position;
		float distance_squared = max(dot(to_light, to_light), 1e-4);
		vec3 l = to_light * inversesqrt(distance_squared);

		float cos_angle = dot(-l, light.direction);
		float cone = light.cos_inner > light.cos_outer
			? clamp((cos_angle - light.cos_outer) / (light.cos_inner - light.cos_outer), 0.0, 1.0)
			: step(light.cos_outer, cos_angle);

		vec3 radiance = light.color * light.intensity * cone / distance_squared;
		color += blinnPhong(n, v, l, radiance, albedo);
	}

	out_color = vec4(color, 1.0);
}

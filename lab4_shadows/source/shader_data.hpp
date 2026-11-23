#pragma once

#include <cstddef>
#include <cstdint>

#include "math.hpp"

namespace shader_data {

struct SceneUniforms {
	math::Matrix4 view_projection;
	math::Matrix4 shadow_view_projection;
	math::Vector3 camera_position;
	uint32_t point_light_count;
	math::Vector3 ambient_color;
	float ambient_intensity;
	math::Vector3 directional_direction;
	uint32_t spot_light_count;
	math::Vector3 directional_color;
	float directional_intensity;
	uint32_t shadow_enabled;
	float padding[3];
};

static_assert(offsetof(SceneUniforms, shadow_view_projection) == 64);
static_assert(offsetof(SceneUniforms, camera_position) == 128);
static_assert(offsetof(SceneUniforms, point_light_count) == 140);
static_assert(offsetof(SceneUniforms, ambient_color) == 144);
static_assert(offsetof(SceneUniforms, ambient_intensity) == 156);
static_assert(offsetof(SceneUniforms, directional_direction) == 160);
static_assert(offsetof(SceneUniforms, spot_light_count) == 172);
static_assert(offsetof(SceneUniforms, directional_color) == 176);
static_assert(offsetof(SceneUniforms, directional_intensity) == 188);
static_assert(offsetof(SceneUniforms, shadow_enabled) == 192);
static_assert(sizeof(SceneUniforms) == 208);

struct ModelUniforms {
	math::Matrix4 model;
	math::Matrix4 normal_matrix;
	math::Vector3 specular;
	float shininess;
	math::Vector2 uv_scale;
	float padding[2];
};

static_assert(offsetof(ModelUniforms, normal_matrix) == 64);
static_assert(offsetof(ModelUniforms, specular) == 128);
static_assert(offsetof(ModelUniforms, shininess) == 140);
static_assert(offsetof(ModelUniforms, uv_scale) == 144);
static_assert(sizeof(ModelUniforms) == 160);

struct SamplingEffect {
	uint32_t wobbling;
	float time;
	float amplitude;
	float frequency;
};

static_assert(sizeof(SamplingEffect) == 16);

struct PointLight {
	math::Vector3 position;
	float intensity;
	math::Vector3 color;
	float padding;
};

static_assert(offsetof(PointLight, intensity) == 12);
static_assert(offsetof(PointLight, color) == 16);
static_assert(sizeof(PointLight) == 32);

struct SpotLight {
	math::Vector3 position;
	float intensity;
	math::Vector3 direction;
	float cos_inner;
	math::Vector3 color;
	float cos_outer;
};

static_assert(offsetof(SpotLight, intensity) == 12);
static_assert(offsetof(SpotLight, direction) == 16);
static_assert(offsetof(SpotLight, cos_inner) == 28);
static_assert(offsetof(SpotLight, color) == 32);
static_assert(offsetof(SpotLight, cos_outer) == 44);
static_assert(sizeof(SpotLight) == 48);

}

#pragma once

#include <cstddef>
#include <cstdint>

#include "math.hpp"

namespace shader_data {

struct SceneUniforms {
	math::Matrix4 view_projection;
	math::Vector3 camera_position;
	uint32_t point_light_count;
	math::Vector3 ambient_color;
	float ambient_intensity;
	math::Vector3 directional_direction;
	uint32_t spot_light_count;
	math::Vector3 directional_color;
	float directional_intensity;
};

static_assert(offsetof(SceneUniforms, camera_position) == 64);
static_assert(offsetof(SceneUniforms, point_light_count) == 76);
static_assert(offsetof(SceneUniforms, ambient_color) == 80);
static_assert(offsetof(SceneUniforms, ambient_intensity) == 92);
static_assert(offsetof(SceneUniforms, directional_direction) == 96);
static_assert(offsetof(SceneUniforms, spot_light_count) == 108);
static_assert(offsetof(SceneUniforms, directional_color) == 112);
static_assert(offsetof(SceneUniforms, directional_intensity) == 124);
static_assert(sizeof(SceneUniforms) == 128);

struct ModelUniforms {
	math::Matrix4 model;
	math::Matrix4 normal_matrix;
	math::Vector3 albedo;
	float shininess;
	math::Vector3 specular;
	float use_vertex_colors;
};

static_assert(offsetof(ModelUniforms, normal_matrix) == 64);
static_assert(offsetof(ModelUniforms, albedo) == 128);
static_assert(offsetof(ModelUniforms, shininess) == 140);
static_assert(offsetof(ModelUniforms, specular) == 144);
static_assert(offsetof(ModelUniforms, use_vertex_colors) == 156);
static_assert(sizeof(ModelUniforms) == 160);

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

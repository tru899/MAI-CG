#pragma once

#include <cstdint>
#include <vector>

#include "math.hpp"

namespace torus {

struct Vertex {
	math::Vector3 position;
	math::Vector3 color;
};

struct Parameters {
	float major_radius;
	float minor_radius;
	int segments;
	int sides;
};

struct Mesh {
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
};

math::Vector3 point(const Parameters& parameters, float u, float v);

Mesh generate(const Parameters& parameters);

}

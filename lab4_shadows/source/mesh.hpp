#pragma once

#include <cstdint>
#include <vector>

#include "math.hpp"

namespace mesh {

struct Vertex {
	math::Vector3 position;
	math::Vector3 normal;
	math::Vector2 uv;
};

struct Mesh {
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
};

}

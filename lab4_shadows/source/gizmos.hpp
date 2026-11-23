#pragma once

#include <vector>

#include "math.hpp"

namespace gizmos {

struct Vertex {
	math::Vector3 position;
	math::Vector3 color;
};

void appendCube(std::vector<Vertex>& triangles, math::Vector3 center, float half_size, math::Vector3 color);

void appendCone(std::vector<Vertex>& lines, math::Vector3 apex, math::Vector3 direction,
                float angle, float length, math::Vector3 color);

}

#pragma once

#include "math.hpp"
#include "mesh.hpp"

namespace torus {

struct Parameters {
	float major_radius;
	float minor_radius;
	int segments;
	int sides;
};

math::Vector3 point(const Parameters& parameters, float u, float v);

math::Vector3 normal(float u, float v);

mesh::Mesh generate(const Parameters& parameters);

}

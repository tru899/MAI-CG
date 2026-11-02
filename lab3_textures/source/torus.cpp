#include "torus.hpp"

#include <algorithm>
#include <cmath>

namespace torus {

math::Vector3 point(const Parameters& parameters, float u, float v) {
	const float distance_from_axis = parameters.major_radius + parameters.minor_radius * std::cos(v);
	return {
		distance_from_axis * std::cos(u),
		parameters.minor_radius * std::sin(v),
		distance_from_axis * std::sin(u),
	};
}

math::Vector3 normal(float u, float v) {
	return { std::cos(v) * std::cos(u), std::sin(v), std::cos(v) * std::sin(u) };
}

mesh::Mesh generate(const Parameters& parameters) {
	const int segments = std::max(parameters.segments, 3);
	const int sides = std::max(parameters.sides, 3);

	mesh::Mesh mesh;
	mesh.vertices.reserve(size_t(segments + 1) * size_t(sides + 1));
	mesh.indices.reserve(size_t(segments) * size_t(sides) * 6);

	for (int i = 0; i <= segments; ++i) {
		const float u = 2.0f * math::pi * float(i % segments) / float(segments);
		for (int j = 0; j <= sides; ++j) {
			const float v = 2.0f * math::pi * float(j % sides) / float(sides);
			const math::Vector2 uv = { float(i) / float(segments), float(j) / float(sides) };
			mesh.vertices.push_back({ point(parameters, u, v), normal(u, v), uv });
		}
	}

	auto index = [&](int i, int j) {
		return uint32_t(i * (sides + 1) + j);
	};

	for (int i = 0; i < segments; ++i) {
		for (int j = 0; j < sides; ++j) {
			const uint32_t a = index(i, j);
			const uint32_t b = index(i + 1, j);
			const uint32_t c = index(i, j + 1);
			const uint32_t d = index(i + 1, j + 1);

			mesh.indices.insert(mesh.indices.end(), { a, b, c });
			mesh.indices.insert(mesh.indices.end(), { b, d, c });
		}
	}

	return mesh;
}

}

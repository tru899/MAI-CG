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
	mesh.vertices.reserve(size_t(segments) * size_t(sides));
	mesh.indices.reserve(size_t(segments) * size_t(sides) * 6);

	const float outer_radius = parameters.major_radius + parameters.minor_radius;
	const float tube_radius = std::max(parameters.minor_radius, 1e-6f);

	for (int i = 0; i < segments; ++i) {
		const float u = 2.0f * math::pi * float(i) / float(segments);
		for (int j = 0; j < sides; ++j) {
			const float v = 2.0f * math::pi * float(j) / float(sides);
			const math::Vector3 position = point(parameters, u, v);
			const math::Vector3 color = {
				0.5f + 0.5f * position.x / outer_radius,
				0.5f + 0.5f * position.y / tube_radius,
				0.5f + 0.5f * position.z / outer_radius,
			};
			mesh.vertices.push_back({ position, normal(u, v), color });
		}
	}

	auto index = [&](int i, int j) {
		return uint32_t((i % segments) * sides + (j % sides));
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

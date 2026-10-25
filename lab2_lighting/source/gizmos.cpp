#include "gizmos.hpp"

#include <cmath>

namespace gizmos {

void appendCube(std::vector<Vertex>& triangles, math::Vector3 center, float half_size, math::Vector3 color) {
	math::Vector3 corners[8];
	for (int i = 0; i < 8; ++i) {
		corners[i] = {
			center.x + ((i & 1) ? half_size : -half_size),
			center.y + ((i & 2) ? half_size : -half_size),
			center.z + ((i & 4) ? half_size : -half_size),
		};
	}

	const int faces[6][4] = {
		{ 0, 1, 3, 2 }, { 4, 6, 7, 5 },
		{ 0, 4, 5, 1 }, { 2, 3, 7, 6 },
		{ 0, 2, 6, 4 }, { 1, 5, 7, 3 },
	};

	for (const auto& face : faces) {
		for (int k : { 0, 1, 2, 0, 2, 3 }) {
			triangles.push_back({ corners[face[k]], color });
		}
	}
}

void appendCone(std::vector<Vertex>& lines, math::Vector3 apex, math::Vector3 direction,
                float angle, float length, math::Vector3 color) {
	constexpr int circle_points = 24;
	constexpr int side_lines = 8;

	const math::Vector3 axis = math::normalize(direction);
	const math::Vector3 helper = std::fabs(axis.y) < 0.99f ? math::Vector3{ 0.0f, 1.0f, 0.0f }
	                                                       : math::Vector3{ 1.0f, 0.0f, 0.0f };
	const math::Vector3 side = math::normalize(math::cross(axis, helper));
	const math::Vector3 up = math::cross(axis, side);

	const math::Vector3 base = apex + axis * (length * std::cos(angle));
	const float radius = length * std::sin(angle);

	auto circle = [&](int k) {
		const float t = 2.0f * math::pi * float(k) / float(circle_points);
		return base + side * (radius * std::cos(t)) + up * (radius * std::sin(t));
	};

	for (int k = 0; k < circle_points; ++k) {
		lines.push_back({ circle(k), color });
		lines.push_back({ circle(k + 1), color });
	}

	for (int k = 0; k < circle_points; k += circle_points / side_lines) {
		lines.push_back({ apex, color });
		lines.push_back({ circle(k), color });
	}

	lines.push_back({ apex, color });
	lines.push_back({ apex + axis * length, color });
}

}

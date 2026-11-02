#include "plane.hpp"

namespace plane {

mesh::Mesh generate() {
	const math::Vector3 up = { 0.0f, -1.0f, 0.0f };

	mesh::Mesh mesh;
	mesh.vertices = {
		{ { -1.0f, 0.0f, -1.0f }, up, { 0.0f, 1.0f } },
		{ { 1.0f, 0.0f, -1.0f }, up, { 1.0f, 1.0f } },
		{ { -1.0f, 0.0f, 1.0f }, up, { 0.0f, 0.0f } },
		{ { 1.0f, 0.0f, 1.0f }, up, { 1.0f, 0.0f } },
	};
	mesh.indices = { 0, 2, 1, 1, 2, 3 };
	return mesh;
}

}

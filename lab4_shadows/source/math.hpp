#pragma once

#include <cmath>

namespace math {

constexpr float pi = 3.14159265358979323846f;

inline float radians(float degrees) {
	return degrees * pi / 180.0f;
}

struct Vector2 {
	float x, y;
};

struct Vector3 {
	float x, y, z;
};

inline Vector3 operator+(Vector3 a, Vector3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline Vector3 operator-(Vector3 a, Vector3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline Vector3 operator*(Vector3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }

inline float dot(Vector3 a, Vector3 b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vector3 cross(Vector3 a, Vector3 b) {
	return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

inline float length(Vector3 a) {
	return std::sqrt(dot(a, a));
}

inline Vector3 normalize(Vector3 a, Vector3 fallback = { 0.0f, 1.0f, 0.0f }) {
	const float l = length(a);
	return l > 1e-6f ? a * (1.0f / l) : fallback;
}

struct Matrix4 {
	float elements[4][4];

	float& at(int row, int column) { return elements[column][row]; }
	float at(int row, int column) const { return elements[column][row]; }

	static Matrix4 zero() {
		return {};
	}

	static Matrix4 identity() {
		Matrix4 m = zero();
		for (int i = 0; i < 4; ++i) {
			m.at(i, i) = 1.0f;
		}
		return m;
	}

	static Matrix4 translation(Vector3 t) {
		Matrix4 m = identity();
		m.at(0, 3) = t.x;
		m.at(1, 3) = t.y;
		m.at(2, 3) = t.z;
		return m;
	}

	static Matrix4 scale(Vector3 s) {
		Matrix4 m = identity();
		m.at(0, 0) = s.x;
		m.at(1, 1) = s.y;
		m.at(2, 2) = s.z;
		return m;
	}

	static Matrix4 rotationX(float angle) {
		const float c = std::cos(angle), s = std::sin(angle);
		Matrix4 m = identity();
		m.at(1, 1) = c; m.at(1, 2) = -s;
		m.at(2, 1) = s; m.at(2, 2) = c;
		return m;
	}

	static Matrix4 rotationY(float angle) {
		const float c = std::cos(angle), s = std::sin(angle);
		Matrix4 m = identity();
		m.at(0, 0) = c;  m.at(0, 2) = s;
		m.at(2, 0) = -s; m.at(2, 2) = c;
		return m;
	}

	static Matrix4 rotationZ(float angle) {
		const float c = std::cos(angle), s = std::sin(angle);
		Matrix4 m = identity();
		m.at(0, 0) = c; m.at(0, 1) = -s;
		m.at(1, 0) = s; m.at(1, 1) = c;
		return m;
	}

	static Matrix4 orthographic(float left, float right, float top, float bottom,
	                            float z_near, float z_far) {
		Matrix4 m = identity();
		m.at(0, 0) = 2.0f / (right - left);
		m.at(0, 3) = -(right + left) / (right - left);
		m.at(1, 1) = 2.0f / (bottom - top);
		m.at(1, 3) = -(bottom + top) / (bottom - top);
		m.at(2, 2) = 1.0f / (z_far - z_near);
		m.at(2, 3) = -z_near / (z_far - z_near);
		return m;
	}

	static Matrix4 perspective(float fov_y, float aspect, float z_near, float z_far) {
		const float tan_half_fov = std::tan(fov_y / 2.0f);
		Matrix4 m = zero();
		m.at(0, 0) = 1.0f / (aspect * tan_half_fov);
		m.at(1, 1) = 1.0f / tan_half_fov;
		m.at(2, 2) = z_far / (z_far - z_near);
		m.at(2, 3) = -(z_far * z_near) / (z_far - z_near);
		m.at(3, 2) = 1.0f;
		return m;
	}

	// Матрица вида для камеры в точке eye, смотрящей на target. Пространство камеры то же, что и у
	// основной камеры: +X вправо, +Y вниз, +Z вперёд. down - мировое направление, которое должно
	// оказаться «внизу» картинки (для основной сцены это +Y).
	static Matrix4 look_at(Vector3 eye, Vector3 target, Vector3 down) {
		const Vector3 forward = normalize(target - eye, { 0.0f, 0.0f, 1.0f });
		const Vector3 right = normalize(cross(down, forward), { 1.0f, 0.0f, 0.0f });
		const Vector3 below = cross(forward, right);

		Matrix4 m = identity();
		m.at(0, 0) = right.x;   m.at(0, 1) = right.y;   m.at(0, 2) = right.z;   m.at(0, 3) = -dot(right, eye);
		m.at(1, 0) = below.x;   m.at(1, 1) = below.y;   m.at(1, 2) = below.z;   m.at(1, 3) = -dot(below, eye);
		m.at(2, 0) = forward.x; m.at(2, 1) = forward.y; m.at(2, 2) = forward.z; m.at(2, 3) = -dot(forward, eye);
		return m;
	}
};

inline Matrix4 operator*(const Matrix4& a, const Matrix4& b) {
	Matrix4 result = Matrix4::zero();
	for (int row = 0; row < 4; ++row) {
		for (int column = 0; column < 4; ++column) {
			float sum = 0.0f;
			for (int k = 0; k < 4; ++k) {
				sum += a.at(row, k) * b.at(k, column);
			}
			result.at(row, column) = sum;
		}
	}
	return result;
}

}

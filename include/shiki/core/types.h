#pragma once

#include <shiki/core/result.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace shiki {

/** Two-dimensional vector in engine coordinates. */
struct Vec2 {
	float x = 0.0f; ///< Horizontal component.
	float y = 0.0f; ///< Vertical component.
	/** Creates the zero vector. */
	Vec2() = default;
	/** Creates a vector from horizontal and vertical components. */
	Vec2(float x_, float y_) : x(x_), y(y_) {}

	/** Returns the component-wise sum. */
	Vec2 operator+(const Vec2 &other) const {
		return Vec2(x + other.x, y + other.y);
	}
	/** Returns the component-wise difference. */
	Vec2 operator-(const Vec2 &other) const {
		return Vec2(x - other.x, y - other.y);
	}
	/** Returns both components multiplied by scalar. */
	Vec2 operator*(float scalar) const { return Vec2(x * scalar, y * scalar); }
	/** Returns both components divided by scalar. */
	Vec2 operator/(float scalar) const { return Vec2(x / scalar, y / scalar); }
	/** Adds other component-wise and returns this vector. */
	Vec2 &operator+=(const Vec2 &other) {
		x += other.x;
		y += other.y;
		return *this;
	}
	/** Subtracts other component-wise and returns this vector. */
	Vec2 &operator-=(const Vec2 &other) {
		x -= other.x;
		y -= other.y;
		return *this;
	}
};

/** Three-dimensional vector used by rendering and presentation APIs. */
struct Vec3 {
	float x = 0.0f; ///< First component.
	float y = 0.0f; ///< Second component.
	float z = 0.0f; ///< Third component.
	/** Creates the zero vector. */
	Vec3() = default;
	/** Creates a vector from three components. */
	Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
};

/** Four-component vector used for homogeneous coordinates and colors. */
struct Vec4 {
	float x = 0.0f; ///< First component or normalized red channel.
	float y = 0.0f; ///< Second component or normalized green channel.
	float z = 0.0f; ///< Third component or normalized blue channel.
	float w = 0.0f; ///< Fourth component or normalized alpha channel.
	/** Creates the zero vector. */
	Vec4() = default;
	/** Creates a vector from four components. */
	Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
};

/** Column-major 4x4 transformation matrix. */
struct Mat4 {
	float data[16] = {}; ///< Column-major matrix elements.
	/** Creates a zero matrix. */
	Mat4() = default;
	/** Creates a diagonal matrix with all other elements set to zero. */
	explicit Mat4(float diagonal) {
		data[0] = data[5] = data[10] = data[15] = diagonal;
	}
};

/** Normalized red, green, blue, and alpha components. */
using Color = Vec4;

/** Axis-aligned rectangle in engine coordinates. */
struct Rect {
	float x = 0.0f;      ///< Left edge.
	float y = 0.0f;      ///< Top edge.
	float width = 0.0f;  ///< Horizontal extent.
	float height = 0.0f; ///< Vertical extent.

	/** Creates an empty rectangle at the origin. */
	Rect() = default;
	/** Creates a rectangle from its top-left position and extents. */
	Rect(float x_, float y_, float w_, float h_)
	    : x(x_), y(y_), width(w_), height(h_) {}

	/** Returns the left edge. */
	float left() const { return x; }
	/** Returns the right edge. */
	float right() const { return x + width; }
	/** Returns the top edge. */
	float top() const { return y; }
	/** Returns the bottom edge. */
	float bottom() const { return y + height; }
	/** Returns the midpoint of both extents. */
	Vec2 center() const { return Vec2(x + width / 2.0f, y + height / 2.0f); }

	/** Reports whether point lies on or inside all four edges. */
	bool contains(const Vec2 &point) const {
		return point.x >= x && point.x <= right() && point.y >= y &&
		       point.y <= bottom();
	}

	/** Reports whether this rectangle overlaps other with positive area. */
	bool intersects(const Rect &other) const {
		return x < other.right() && right() > other.x && y < other.bottom() &&
		       bottom() > other.y;
	}
};

/** Sprite and render-target blending modes. */
enum class BlendMode { None, Alpha, Add, Multiply, Screen };

/** Clamps a value to an inclusive range. */
template <typename T> [[nodiscard]] constexpr T clamp(T value, T min, T max) {
	return value < min ? min : (value > max ? max : value);
}

/** Linearly interpolates between two compatible values. */
template <typename T> [[nodiscard]] constexpr T lerp(T a, T b, float t) {
	return a + (b - a) * t;
}

} // namespace shiki

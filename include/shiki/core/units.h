#pragma once

#include <shiki/core/types.h>

#include <numbers>

namespace shiki {

/** Represents an orientation measured in radians. */
struct Angle final {
	float radians{};

	auto operator<=>(const Angle &) const = default;

	/** Creates an angle from an explicit degree value. */
	[[nodiscard]] static constexpr Angle fromDegrees(float degrees) noexcept {
		return Angle{degrees * std::numbers::pi_v<float> / 180.0f};
	}

	/** Converts this angle to degrees for presentation boundaries. */
	[[nodiscard]] constexpr float degrees() const noexcept {
		return radians * 180.0f / std::numbers::pi_v<float>;
	}
};

/** Distinguishes a world-space position from an arbitrary vector. */
struct WorldPosition final {
	Vec2 value{};

	auto operator<=>(const WorldPosition &) const = default;
};

} // namespace shiki

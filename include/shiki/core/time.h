#pragma once

#include <compare>
#include <cstdint>

namespace shiki {

/** Identifies one fixed simulation step. */
struct Tick final {
	std::uint64_t value{};

	auto operator<=>(const Tick &) const = default;
};

/** Represents a duration measured in fixed simulation steps. */
struct TickSpan final {
	std::uint32_t value{};

	auto operator<=>(const TickSpan &) const = default;
};

/** Defines the immutable fixed-step frequency of a session. */
struct TickRate final {
	std::uint32_t ticksPerSecond{60};

	/** Returns whether the rate can drive a simulation. */
	[[nodiscard]] constexpr bool isValid() const noexcept {
		return ticksPerSecond != 0;
	}
};

/** Advances a tick by a fixed duration. */
[[nodiscard]] constexpr Tick operator+(Tick tick, TickSpan duration) noexcept {
	return Tick{tick.value + duration.value};
}

/** Returns the duration between two ordered ticks. */
[[nodiscard]] constexpr std::uint64_t operator-(Tick end, Tick begin) noexcept {
	return end.value - begin.value;
}

} // namespace shiki

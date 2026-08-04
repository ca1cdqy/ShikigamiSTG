#pragma once

#include <shiki/control/external_command.h>
#include <shiki/core/time.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace shiki::control {

/** Stores normalized deterministic input for one simulation tick. */
struct InputFrame final {
	Tick tick{};
	std::uint64_t digital{};
	std::array<float, 4> axes{};
	std::vector<ExternalCommand> commands;

	/** Returns whether a game-defined digital input bit is active. */
	[[nodiscard]] constexpr bool pressed(std::uint8_t bit) const noexcept {
		return bit < 64 && (digital & (std::uint64_t{1} << bit)) != 0;
	}

	/** Returns a normalized analog axis or zero when the index is invalid. */
	[[nodiscard]] constexpr float axis(std::size_t index) const noexcept {
		return index < axes.size() ? axes[index] : 0.0f;
	}
};

} // namespace shiki::control

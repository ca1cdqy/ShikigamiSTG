#pragma once

#include <shiki/core/result.h>
#include <shiki/core/time.h>
#include <shiki/game/entity.h>
#include <shiki/game/event.h>
#include <shiki/game/type_id.h>
#include <shiki/game/world.h>

#include <cstdint>

namespace shiki::flow {

/** Identifies a game-defined phase kind independently from one occurrence. */
struct PhaseId final {
	game::TypeKey key{};
	auto operator<=>(const PhaseId &) const = default;
};

/** Identifies one live phase occurrence inside a Session. */
struct PhaseHandle final {
	std::uint64_t value{};
	auto operator<=>(const PhaseHandle &) const = default;
	[[nodiscard]] explicit operator bool() const noexcept { return value != 0; }
};

/** Describes generic phase completion conditions. */
struct PhaseSpec final {
	PhaseId id{};
	TickSpan timeout{};
	bool clearWhenAllMembersDefeated{true};
};

/** Describes why a phase left the active Flow set. */
enum class PhaseResult : std::uint8_t {
	Cleared, ///< Every tracked member was defeated.
	Timeout, ///< The deterministic timeout elapsed.
	Forced   ///< User code explicitly finished the phase.
};

/** Reports activation of one phase occurrence. */
struct PhaseStarted final {
	PhaseHandle phase{};
	PhaseId id{};
	Tick enteredAt{};
};

/** Reports immutable completion facts for one phase occurrence. */
struct PhaseFinished final {
	PhaseHandle phase{};
	PhaseId id{};
	PhaseResult result{PhaseResult::Forced};
	Tick finishedAt{};
};

/** Stores registered event tokens required by Session phase handling. */
struct PhaseEvents final {
	game::EventToken<PhaseStarted> started;
	game::EventToken<PhaseFinished> finished;

	/** Registers the built-in phase event schema before the first tick. */
	[[nodiscard]] static Result<PhaseEvents> registerWith(game::World &world);
};

} // namespace shiki::flow

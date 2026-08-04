#pragma once

/** @file Component schema and procedural APIs for per-entity score state. */

#include <shiki/core/result.h>
#include <shiki/game/commands.h>
#include <shiki/game/world_view.h>

#include <cstdint>

namespace shiki {
class GameDefinition;
}

namespace shiki::stg {

/** Stores game-neutral score counters on any entity. */
struct Score final {
	std::int64_t points{};      ///< Current run score.
	std::int64_t highScore{};   ///< Highest score retained by game policy.
	std::uint64_t graze{};      ///< Number of accepted graze events.
	std::uint64_t pointItems{}; ///< Number of collected point items.
};

/** Describes an atomic score mutation interpreted by game rules. */
struct ScoreDelta final {
	std::int64_t points{};      ///< Signed score contribution.
	std::uint64_t graze{};      ///< Graze count contribution.
	std::uint64_t pointItems{}; ///< Point-item count contribution.
};

/** Holds the World registration for score state. */
struct ScoreComponents final {
	game::ComponentToken<Score> score;

	/** Registers observable deterministic score state. */
	[[nodiscard]] static Result<ScoreComponents>
	registerWith(GameDefinition &definition);
};

/** Returns a score value after applying one policy-neutral delta. */
[[nodiscard]] Score applyScoreDelta(Score current, ScoreDelta delta) noexcept;

/** Provides phase-scoped score mutation without owning gameplay state. */
class ScoreApi final {
  public:
	/** Creates a non-owning API valid for the current system callback. */
	ScoreApi(game::WorldView world, game::Commands &commands,
	         ScoreComponents components) noexcept;

	/** Replaces one entity's score component. */
	[[nodiscard]] game::CommandStatus set(game::EntityHandle entity,
	                                      Score score);

	/** Applies one delta to the currently visible score component. */
	[[nodiscard]] Result<Score> add(game::EntityHandle entity,
	                                ScoreDelta delta);

  private:
	game::WorldView world_;
	game::Commands *commands_{};
	ScoreComponents components_;
};

} // namespace shiki::stg

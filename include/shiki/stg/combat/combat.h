#pragma once

#include <shiki/core/units.h>
#include <shiki/game/entity.h>
#include <shiki/stg/projectile/projectile.h>

#include <cstdint>

namespace shiki::stg {

/** Reports one geometry overlap without applying gameplay policy. */
struct ContactEvent final {
	game::EntityHandle first{};  ///< First overlapping entity.
	game::EntityHandle second{}; ///< Second overlapping entity.
	float distanceSquared{};     ///< Squared center distance in world units.
};

/** Requests policy-controlled health damage from one entity to another. */
struct DamageRequest final {
	game::EntityHandle source{}; ///< Entity requesting damage.
	game::EntityHandle target{}; ///< Entity that may receive damage.
	std::int64_t amount{};       ///< Non-negative requested health reduction.
};

/** Reports the deterministic result of aggregated damage to one target. */
struct DamageResult final {
	game::EntityHandle source{}; ///< Entity credited as the damage source.
	game::EntityHandle target{}; ///< Entity whose health was resolved.
	std::int64_t requested{};    ///< Aggregated requested damage.
	std::int64_t applied{};      ///< Damage accepted by policy.
	std::int64_t remaining{};    ///< Target health after application.
	bool killed{};               ///< Whether remaining health reached zero.
};

/** Identifies why a projectile cancellation operation was requested. */
enum class CancelCause : std::uint8_t {
	Bomb,       ///< A player ability cleared projectiles.
	PhaseEnd,   ///< A stage phase ended.
	Script,     ///< Stage or game code requested cancellation.
	Destruction ///< Another gameplay entity caused cancellation.
};

/** Selects projectiles in one circular world-space cancellation area. */
struct CancelRequest final {
	WorldPosition center{}; ///< Center of the cancellation circle.
	float radius{};         ///< Cancellation radius in world units.
	FactionId faction{};    ///< Faction selected when filtering is enabled.
	bool filterFaction{};   ///< Whether faction limits eligible projectiles.
	bool convertToReward{}; ///< Whether accepted projectiles request rewards.
	CancelCause cause{CancelCause::Script}; ///< Reason recorded in results.
	game::EntityHandle source{}; ///< Entity responsible for cancellation.
};

/** Reports one projectile accepted by a cancellation policy. */
struct CancelResult final {
	game::EntityHandle projectile{}; ///< Accepted projectile entity.
	game::EntityHandle source{};     ///< Entity responsible for cancellation.
	WorldPosition position{};        ///< Projectile position at cancellation.
	CancelCause cause{CancelCause::Script}; ///< Recorded cancellation reason.
	bool convertToReward{}; ///< Whether reward conversion was requested.
};

/** Identifies a generic reward fact consumed by game-specific score rules. */
enum class RewardKind : std::uint8_t {
	Kill,            ///< An actor reached zero health.
	ProjectileCancel ///< A projectile was converted by cancellation.
};

/** Reports a reward fact without owning score, item, or resource state. */
struct RewardEvent final {
	RewardKind kind{};           ///< Kind of gameplay fact being rewarded.
	game::EntityHandle source{}; ///< Entity credited with the reward.
	game::EntityHandle subject{}; ///< Entity that produced the reward fact.
	WorldPosition position{};    ///< World position associated with the fact.
	std::int64_t amount{1};      ///< Rule-defined reward quantity.
};

} // namespace shiki::stg

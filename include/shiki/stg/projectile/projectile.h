#pragma once

#include <shiki/core/time.h>
#include <shiki/core/types.h>
#include <shiki/core/units.h>
#include <shiki/game/entity.h>

#include <compare>
#include <cstddef>
#include <cstdint>

namespace shiki::stg {

/** Identifies a collision and targeting faction within one game definition. */
struct FactionId final {
	std::uint32_t value{}; ///< Game-defined stable faction value.

	/** Compares faction values. */
	auto operator<=>(const FactionId &) const = default;
};

/** Identifies a prevalidated projectile presentation style. */
struct ProjectileStyleId final {
	std::uint32_t value{}; ///< Game-definition-local presentation style value.

	/** Compares style values. */
	auto operator<=>(const ProjectileStyleId &) const = default;
};

/** Selects optional behavior implemented by projectile systems. */
enum class ProjectileFlags : std::uint32_t {
	None = 0,                   ///< Uses only movement and lifetime behavior.
	CancelImmune = 1U << 0U,    ///< Ignores ordinary projectile cancellation.
	RotateToVelocity = 1U << 1U ///< Derives orientation from current velocity.
};

/** Combines independent projectile behavior flags. */
[[nodiscard]] constexpr ProjectileFlags
operator|(ProjectileFlags left, ProjectileFlags right) noexcept {
	return static_cast<ProjectileFlags>(static_cast<std::uint32_t>(left) |
	                                    static_cast<std::uint32_t>(right));
}

/** Returns whether a projectile behavior flag is enabled. */
[[nodiscard]] constexpr bool hasFlag(ProjectileFlags value,
                                     ProjectileFlags flag) noexcept {
	return (static_cast<std::uint32_t>(value) &
	        static_cast<std::uint32_t>(flag)) != 0;
}

/** Stores the world transform shared by projectile systems and presentation. */
struct Transform final {
	WorldPosition position{}; ///< Position in deterministic world coordinates.
	Angle orientation{};      ///< Visual and directional orientation.
};

/** Stores linear velocity in world units per fixed simulation tick. */
struct Motion final {
	Vec2 velocityPerTick{}; ///< Displacement applied during one fixed tick.
};

/** Stores a circular collision radius in world units. */
struct CircleCollision final {
	float radius{}; ///< Collision radius in world units.
};

/** Stores ownership and faction information used by relation policies. */
struct Relation final {
	game::EntityHandle owner{}; ///< Spawning or controlling entity.
	FactionId faction{};        ///< Relation-policy faction.
};

/** Stores the remaining deterministic lifetime of a projectile. */
struct ProjectileLifetime final {
	TickSpan remaining{}; ///< Fixed ticks before expiration.
};

/** Stores presentation identity without owning a renderer resource. */
struct ProjectileVisual final {
	ProjectileStyleId style{}; ///< Presentation style resolved by the client.
};

/** Marks an entity as a projectile and stores its optional behavior flags. */
struct ProjectileIdentity final {
	ProjectileFlags flags{ProjectileFlags::None}; ///< Optional system behavior.
};

/** Stores damage policy input for a projectile or other damaging entity. */
struct DamageSource final {
	std::int64_t amount{1}; ///< Damage requested for each accepted contact.
	bool consumeOnHit{true}; ///< Whether a successful hit consumes the source.
};

/** Defines data shared by every projectile in one spawn operation. */
struct ProjectileSpec final {
	float collisionRadius{}; ///< Shared collision radius in world units.
	TickSpan lifetime{600};  ///< Shared lifetime in fixed ticks.
	ProjectileStyleId style{}; ///< Shared presentation style.
	FactionId faction{};       ///< Shared relation-policy faction.
	ProjectileFlags flags{ProjectileFlags::None}; ///< Shared optional behavior.
	std::int64_t damage{1}; ///< Shared damage requested per accepted contact.
};

/** Defines per-projectile placement and ownership data. */
struct ProjectileSpawn final {
	WorldPosition position{}; ///< Initial world position.
	Vec2 velocityPerTick{};   ///< Initial displacement per fixed tick.
	Angle orientation{};      ///< Initial orientation.
	game::EntityHandle owner{}; ///< Spawning or controlling entity.
};

/** Reports the size of a successfully recorded projectile batch. */
struct ProjectileBatch final {
	std::size_t count{}; ///< Number of projectile entities recorded.
};

} // namespace shiki::stg

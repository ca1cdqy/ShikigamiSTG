#pragma once

#include <shiki/control/controller.h>
#include <shiki/game/type_id.h>
#include <shiki/stg/projectile/projectile.h>

#include <cstdint>
#include <optional>

namespace shiki::stg {

/** Marks an entity as an actor created from a registered behavior type. */
struct ActorIdentity final {
	game::TypeKey type{};
};

/** Stores mutable health independently from damage and lifecycle policies. */
struct Health final {
	std::int64_t current{};
	std::int64_t maximum{};
};

/** Defines invariant data shared by one actor type or spawn site. */
struct ActorSpec final {
	game::TypeKey type{};
	FactionId faction{};
	float collisionRadius{};
	std::int64_t health{1};
	std::optional<control::ControllerTypeId> controller;
};

/** Defines world placement and optional ownership for one actor spawn. */
struct ActorSpawn final {
	WorldPosition position{};
	Vec2 velocityPerTick{};
	Angle orientation{};
	game::EntityHandle owner{};
};

} // namespace shiki::stg

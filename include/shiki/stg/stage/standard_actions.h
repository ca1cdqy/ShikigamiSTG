#pragma once

#include <shiki/core/result.h>
#include <shiki/flow/action.h>
#include <shiki/stg/actor/actor.h>
#include <shiki/stg/actor/actor_behavior.h>
#include <shiki/stg/projectile/projectile.h>

#include <vector>

namespace shiki {
class GameDefinition;
}

namespace shiki::stg {

/** Serializable spawn placement that never stores a Session EntityHandle. */
struct StageSpawn final {
	WorldPosition position{};
	Vec2 velocityPerTick{};
	Angle orientation{};
};

/** Payload for spawning one neutral procedural actor from stage flow. */
struct SpawnActorAction final {
	ActorSpec spec;
	StageSpawn spawn;
};

/** Payload for spawning one registered object-oriented actor behavior. */
struct SpawnRegisteredActorAction final {
	ActorTypeId type{};
	StageSpawn spawn;
};

/** Serializable projectile placement without a Session owner handle. */
using StageProjectileSpawn = StageSpawn;

/** Payload for one homogeneous projectile batch emitted by stage flow. */
struct SpawnProjectileBatchAction final {
	ProjectileSpec spec;
	std::vector<StageProjectileSpawn> projectiles;
};

/** Typed tokens for standard low-level stage gameplay actions. */
struct StandardActionSchema final {
	flow::ActionToken<SpawnActorAction> spawnActor;
	flow::ActionToken<SpawnRegisteredActorAction> spawnRegisteredActor;
	flow::ActionToken<SpawnProjectileBatchAction> spawnProjectileBatch;
};

/** Registers standard actions used by parsers and programmatic stages. */
class StandardActions final {
  public:
	/** Installs spawn actions into an unfrozen game definition. */
	[[nodiscard]] static Result<StandardActionSchema>
	install(GameDefinition &definition);
};

} // namespace shiki::stg

#pragma once

#include <shiki/core/result.h>
#include <shiki/game/commands.h>
#include <shiki/game/world.h>
#include <shiki/stg/projectile/projectile.h>

#include <span>

namespace shiki::stg {

/** Holds the World registrations required by the standard projectile systems.
 */
struct ProjectileComponents final {
	game::ComponentToken<Transform> transform;
	game::ComponentToken<Motion> motion;
	game::ComponentToken<CircleCollision> collision;
	game::ComponentToken<Relation> relation;
	game::ComponentToken<ProjectileLifetime> lifetime;
	game::ComponentToken<ProjectileVisual> visual;
	game::ComponentToken<ProjectileIdentity> identity;
	game::ComponentToken<DamageSource> damage;

	/** Registers the standard projectile schema before the first World tick. */
	[[nodiscard]] static Result<ProjectileComponents>
	registerWith(game::World &world);
};

/** Records projectile creation through one phase-scoped command buffer. */
class ProjectileApi final {
  public:
	/**
	 * Creates a non-owning API view valid while commands remains active.
	 *
	 * The caller must not retain this object beyond the current system
	 * callback.
	 */
	ProjectileApi(game::Commands &commands,
	              const ProjectileComponents &components) noexcept;

	/** Records one projectile and returns its stable pending handle. */
	[[nodiscard]] Result<game::EntityHandle>
	spawn(const ProjectileSpec &spec, const ProjectileSpawn &spawn);

	/** Records a homogeneous projectile batch in input order. */
	[[nodiscard]] Result<ProjectileBatch>
	spawnBatch(const ProjectileSpec &spec,
	           std::span<const ProjectileSpawn> spawns);

  private:
	[[nodiscard]] Result<game::EntityHandle>
	record(const ProjectileSpec &spec, const ProjectileSpawn &spawn);

	game::Commands *commands_{};
	const ProjectileComponents *components_{};
};

} // namespace shiki::stg

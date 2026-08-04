#include <shiki/stg/projectile/projectile_systems.h>

#include <cmath>

namespace shiki::stg {

void updateProjectileMovement(const game::WorldView &world,
                              game::Commands &commands,
                              const ProjectileComponents &components) {
	const auto projectiles = world.query(
	    components.transform, components.motion, components.identity);
	for (const auto row : projectiles) {
		Transform transform = row.get<Transform>();
		const auto &motion = row.get<Motion>();
		transform.position.value += motion.velocityPerTick;
		if (hasFlag(row.get<ProjectileIdentity>().flags,
		            ProjectileFlags::RotateToVelocity)) {
			transform.orientation.radians =
			    std::atan2(motion.velocityPerTick.y, motion.velocityPerTick.x);
		}
		static_cast<void>(
		    commands.set(row.entity(), components.transform, transform));
	}
}

void updateProjectileLifetimes(const game::WorldView &world,
                               game::Commands &commands,
                               const ProjectileComponents &components) {
	const auto projectiles =
	    world.query(components.lifetime, components.identity);
	for (const auto row : projectiles) {
		ProjectileLifetime lifetime = row.get<ProjectileLifetime>();
		if (lifetime.remaining.value <= 1) {
			static_cast<void>(commands.destroy(row.entity()));
			continue;
		}
		--lifetime.remaining.value;
		static_cast<void>(
		    commands.set(row.entity(), components.lifetime, lifetime));
	}
}

} // namespace shiki::stg

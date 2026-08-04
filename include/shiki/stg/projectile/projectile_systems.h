#pragma once

#include <shiki/game/commands.h>
#include <shiki/game/world_view.h>
#include <shiki/stg/projectile/projectile_api.h>

namespace shiki::stg {

/** Advances all active standard projectiles by one fixed simulation tick. */
void updateProjectileMovement(const game::WorldView &world,
                              game::Commands &commands,
                              const ProjectileComponents &components);

/** Decrements projectile lifetimes and destroys expired entities. */
void updateProjectileLifetimes(const game::WorldView &world,
                               game::Commands &commands,
                               const ProjectileComponents &components);

} // namespace shiki::stg

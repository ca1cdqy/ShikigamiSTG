#pragma once

#include <shiki/core/result.h>
#include <shiki/game/component.h>
#include <shiki/game/event.h>
#include <shiki/stg/actor/actor.h>
#include <shiki/stg/combat/combat.h>

namespace shiki {
class GameDefinition;
}

namespace shiki::stg {

/** Exposes typed registrations installed by the standard combat rule package.
 */
struct DefaultRuleSchema final {
	game::ComponentToken<Transform> transform;
	game::ComponentToken<CircleCollision> collision;
	game::ComponentToken<Relation> relation;
	game::ComponentToken<ProjectileIdentity> projectile;
	game::ComponentToken<DamageSource> damageSource;
	game::ComponentToken<Health> health;
	game::EventToken<ContactEvent> contact;
	game::EventToken<DamageRequest> damageRequest;
	game::EventToken<DamageResult> damageResult;
	game::EventToken<CancelRequest> cancelRequest;
	game::EventToken<CancelResult> cancelResult;
	game::EventToken<RewardEvent> reward;
};

/** Installs replaceable contact, damage, cancellation, and reward systems. */
class DefaultRules final {
  public:
	/** Registers the standard rule schema and deterministic system chain. */
	[[nodiscard]] static Result<DefaultRuleSchema>
	install(GameDefinition &definition);
};

} // namespace shiki::stg

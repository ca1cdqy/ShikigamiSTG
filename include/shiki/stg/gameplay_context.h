#pragma once

#include <shiki/control/input_frame.h>
#include <shiki/core/time.h>
#include <shiki/flow/flow_api.h>
#include <shiki/game/commands.h>
#include <shiki/game/world_view.h>
#include <shiki/stg/actor/actor_api.h>
#include <shiki/stg/pattern/pattern_api.h>
#include <shiki/stg/projectile/projectile_api.h>

namespace shiki::stg {

/** Provides immutable timing data for one gameplay callback. */
struct TimeView final {
	Tick tick{};
	TickRate rate{};
};

/**
 * Collects phase-scoped public gameplay capabilities for user code.
 *
 * A context and every API reference obtained from it expire when the current
 * callback returns. Simulation changes remain deferred through Commands.
 */
class GameplayContext final {
  public:
	/** Creates a context for one World phase and command source. */
	GameplayContext(
	    game::WorldView world, game::Commands &commands,
	    const ProjectileComponents &projectileComponents,
	    const ActorComponents &actorComponents, TimeView time,
	    const control::InputFrame &input,
	    const ActorRegistry *actorRegistry = nullptr,
	    ActorBehaviorPool *actorBehaviors = nullptr,
	    const control::ControllerRegistry *controllerRegistry = nullptr,
	    control::ControllerPool *controllers = nullptr,
	    flow::FlowPool *flow = nullptr,
	    const PatternRegistry *patternRegistry = nullptr,
	    PatternPool *patterns = nullptr) noexcept;

	/** Returns the read-only World state visible in the current phase. */
	[[nodiscard]] const game::WorldView &world() const noexcept {
		return world_;
	}

	/** Returns immutable events published at completed system barriers. */
	[[nodiscard]] const game::EventStream &events() const noexcept {
		return world_.events();
	}

	/** Returns the structural command buffer owned by this callback. */
	[[nodiscard]] game::Commands &commands() noexcept { return *commands_; }

	/** Returns projectile creation capabilities backed by the same commands. */
	[[nodiscard]] ProjectileApi &projectiles() noexcept { return projectiles_; }

	/** Returns neutral actor creation capabilities for this callback. */
	[[nodiscard]] ActorApi &actors() noexcept { return actors_; }

	/** Returns stage-flow capabilities for this callback. */
	[[nodiscard]] flow::FlowApi flow() noexcept { return flow_->api(); }

	/** Returns typed immediate and scheduled Pattern capabilities. */
	[[nodiscard]] PatternApi &patterns() noexcept { return patterns_; }

	/** Returns immutable fixed-tick timing information. */
	[[nodiscard]] TimeView time() const noexcept { return time_; }

	/** Returns normalized deterministic input for the current tick. */
	[[nodiscard]] const control::InputFrame &input() const noexcept {
		return *input_;
	}

	/** Returns the current tick normalized intent for one controlled actor. */
	[[nodiscard]] const control::ActorIntent *
	intent(game::EntityHandle actor) const noexcept;

  private:
	game::WorldView world_;
	game::Commands *commands_{};
	ProjectileApi projectiles_;
	ActorApi actors_;
	PatternApi patterns_;
	TimeView time_{};
	const control::InputFrame *input_{};
	const control::ControllerPool *controllers_{};
	flow::FlowPool *flow_{};
};

} // namespace shiki::stg

#pragma once

#include <shiki/control/controller.h>
#include <shiki/control/input_frame.h>
#include <shiki/core/result.h>
#include <shiki/core/time.h>
#include <shiki/flow/flow_api.h>
#include <shiki/game/world.h>
#include <shiki/game_definition.h>
#include <shiki/presentation/presentation_events.h>
#include <shiki/presentation/presentation_snapshot.h>
#include <shiki/stg/actor/actor_api.h>
#include <shiki/stg/pattern/pattern_api.h>
#include <shiki/stg/projectile/projectile_api.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace shiki {

/** Configures immutable deterministic state for one Session. */
struct SessionConfig final {
	TickRate tickRate{};
	std::uint64_t randomSeed{};
	bool requireSequentialInputTicks{true};
	std::size_t eventRetentionTicks{4};
};

/** Describes whether a Session can continue advancing. */
enum class SessionState : std::uint8_t {
	Running,  ///< The Session accepts the next input frame.
	Complete, ///< Game flow reached a terminal successful state.
	Faulted   ///< A callback or invariant prevented further simulation.
};

/** Summarizes one completed fixed simulation step. */
struct StepResult final {
	Tick tick{};
	SessionState state{SessionState::Running};
};

/** Stable failures produced while creating or advancing a Session. */
enum class SessionError : std::uint32_t {
	InvalidTickRate = 1,
	InputTickMismatch,
	SessionNotRunning,
	SystemCallbackFailed,
	CommandBufferFailed,
	CommitFailed
};

/** Owns all mutable deterministic state for one independent game run. */
class Session final {
  public:
	Session(const Session &) = delete;
	Session &operator=(const Session &) = delete;
	Session(Session &&) = delete;
	Session &operator=(Session &&) = delete;
	~Session() = default;

	/** Creates a headless Session and freezes the supplied definition. */
	[[nodiscard]] static Result<std::unique_ptr<Session>>
	create(GameDefinition definition, SessionConfig config);

	/** Queues one validated StageProgram before the next fixed tick. */
	[[nodiscard]] Result<void>
	startStage(std::shared_ptr<const flow::StageProgram> program);

	/** Advances deterministic simulation by exactly one fixed tick. */
	[[nodiscard]] Result<StepResult> step(const control::InputFrame &input);

	/** Returns a read-only view of committed simulation state. */
	[[nodiscard]] game::WorldView worldView() const noexcept {
		return world_.view();
	}

	/** Returns the most recently started simulation tick. */
	[[nodiscard]] Tick tick() const noexcept { return world_.tick(); }

	/** Returns whether this Session can continue advancing. */
	[[nodiscard]] SessionState state() const noexcept { return state_; }

	/** Returns immutable typed events retained by this Session. */
	[[nodiscard]] const game::EventStream &events() const noexcept {
		return world_.events();
	}

	/** Returns the event boundary intended for renderer and audio frontends. */
	[[nodiscard]] presentation::PresentationEvents
	presentationEvents() const noexcept {
		return presentation::PresentationEvents{world_.events()};
	}

	/** Returns the latest immutable completed-tick presentation snapshot. */
	[[nodiscard]] const presentation::PresentationSnapshot &
	currentPresentation() const noexcept {
		return currentPresentation_;
	}

	/** Returns the immutable snapshot preceding currentPresentation(). */
	[[nodiscard]] const presentation::PresentationSnapshot &
	previousPresentation() const noexcept {
		return previousPresentation_;
	}

  private:
	Session(GameDefinition definition, SessionConfig config) noexcept;

	[[nodiscard]] Result<void> runSystems(game::CommitPhase commitPhase,
	                                      const control::InputFrame &input);
	[[nodiscard]] Result<void>
	runProjectileSystems(const control::InputFrame &input);
	[[nodiscard]] Result<void>
	runActorBehaviors(game::CommitPhase commitPhase,
	                  const control::InputFrame &input, bool runTick);
	[[nodiscard]] Result<void> runControllers(const control::InputFrame &input);
	[[nodiscard]] Result<void>
	runExternalCommands(const control::InputFrame &input);
	[[nodiscard]] Result<void> runFlow(const control::InputFrame &input);
	[[nodiscard]] Result<void>
	runPatternSystems(const control::InputFrame &input);

	GameDefinition definition_;
	SessionConfig config_;
	game::World world_;
	std::optional<stg::ProjectileComponents> projectileComponents_;
	std::optional<stg::ActorComponents> actorComponents_;
	std::optional<game::ComponentToken<control::ActorIntent>> actorIntent_;
	std::optional<flow::PhaseEvents> phaseEvents_;
	stg::ActorBehaviorPool actorBehaviors_;
	control::ControllerPool controllers_;
	flow::FlowPool flow_;
	stg::PatternPool patterns_;
	presentation::PresentationSnapshot previousPresentation_;
	presentation::PresentationSnapshot currentPresentation_;
	SessionState state_{SessionState::Running};
};

} // namespace shiki

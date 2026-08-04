#include <shiki/session.h>

#include <shiki/stg/gameplay_context.h>
#include <shiki/stg/projectile/projectile_systems.h>

#include <string>

namespace shiki {
namespace {

[[nodiscard]] Error makeSessionError(SessionError code, std::string message) {
	return Error{ErrorDomain::Core, static_cast<std::uint32_t>(code),
	             std::move(message)};
}

[[nodiscard]] game::CommitPhase
commitPhaseFor(game::SystemPhase phase) noexcept {
	if (phase <= game::SystemPhase::Flow)
		return game::CommitPhase::Flow;
	if (phase <= game::SystemPhase::Simulation)
		return game::CommitPhase::Simulation;
	return game::CommitPhase::Resolution;
}

} // namespace

Session::Session(GameDefinition definition, SessionConfig config) noexcept
    : definition_(std::move(definition)), config_(config) {}

Result<void>
Session::startStage(std::shared_ptr<const flow::StageProgram> program) {
	if (state_ != SessionState::Running)
		return std::unexpected(makeSessionError(
		    SessionError::SessionNotRunning,
		    "Session cannot start stage flow after it has stopped"));
	return flow_.api().start(std::move(program));
}

Result<std::unique_ptr<Session>> Session::create(GameDefinition definition,
                                                 SessionConfig config) {
	if (!config.tickRate.isValid()) {
		return std::unexpected(
		    makeSessionError(SessionError::InvalidTickRate,
		                     "Session tick rate must be greater than zero"));
	}
	using game::ComponentFlags;
	constexpr ComponentFlags observable =
	    ComponentFlags::Observable | ComponentFlags::Deterministic;
	const auto registerProjectileSchema = [&](auto descriptor) -> Result<void> {
		auto result = definition.registerComponent(std::move(descriptor));
		if (!result)
			return std::unexpected(result.error());
		return {};
	};
	if (auto result =
	        registerProjectileSchema(game::ComponentDescriptor<stg::Transform>{
	            .name = "shiki.transform.v1", .flags = observable});
	    !result)
		return std::unexpected(result.error());
	if (auto result =
	        registerProjectileSchema(game::ComponentDescriptor<stg::Motion>{
	            .name = "shiki.motion.v1", .flags = observable});
	    !result)
		return std::unexpected(result.error());
	if (auto result = registerProjectileSchema(
	        game::ComponentDescriptor<stg::CircleCollision>{
	            .name = "shiki.circle_collision.v1", .flags = observable});
	    !result)
		return std::unexpected(result.error());
	if (auto result =
	        registerProjectileSchema(game::ComponentDescriptor<stg::Relation>{
	            .name = "shiki.relation.v1",
	            .flags = ComponentFlags::Deterministic});
	    !result)
		return std::unexpected(result.error());
	if (auto result = registerProjectileSchema(
	        game::ComponentDescriptor<stg::ProjectileLifetime>{
	            .name = "shiki.projectile.lifetime.v1",
	            .flags = ComponentFlags::Deterministic});
	    !result)
		return std::unexpected(result.error());
	if (auto result = registerProjectileSchema(
	        game::ComponentDescriptor<stg::ProjectileVisual>{
	            .name = "shiki.projectile.visual.v1", .flags = observable});
	    !result)
		return std::unexpected(result.error());
	if (auto result = registerProjectileSchema(
	        game::ComponentDescriptor<stg::ProjectileIdentity>{
	            .name = "shiki.projectile.identity.v1",
	            .flags = ComponentFlags::Deterministic});
	    !result)
		return std::unexpected(result.error());
	if (auto result = registerProjectileSchema(
	        game::ComponentDescriptor<stg::DamageSource>{
	            .name = "shiki.damage_source.v1",
	            .flags = ComponentFlags::Deterministic});
	    !result)
		return std::unexpected(result.error());
	if (auto result = registerProjectileSchema(
	        game::ComponentDescriptor<stg::ActorIdentity>{
	            .name = "shiki.actor.identity.v1",
	            .flags = ComponentFlags::Deterministic});
	    !result)
		return std::unexpected(result.error());
	if (auto result =
	        registerProjectileSchema(game::ComponentDescriptor<stg::Health>{
	            .name = "shiki.health.v1", .flags = observable});
	    !result)
		return std::unexpected(result.error());
	if (auto result = registerProjectileSchema(
	        game::ComponentDescriptor<control::ActorIntent>{
	            .name = "shiki.actor_intent.v1",
	            .flags = ComponentFlags::Deterministic});
	    !result)
		return std::unexpected(result.error());
	const auto registerFlowEvent = [&](auto descriptor) -> Result<void> {
		auto result = definition.registerEvent(std::move(descriptor));
		if (!result)
			return std::unexpected(result.error());
		return {};
	};
	using game::EventVisibility;
	if (auto result =
	        registerFlowEvent(game::EventDescriptor<flow::PhaseStarted>{
	            .name = "shiki.phase_started.v1",
	            .visibility = EventVisibility::SimulationAndPresentation});
	    !result)
		return std::unexpected(result.error());
	if (auto result =
	        registerFlowEvent(game::EventDescriptor<flow::PhaseFinished>{
	            .name = "shiki.phase_finished.v1",
	            .visibility = EventVisibility::SimulationAndPresentation});
	    !result)
		return std::unexpected(result.error());
	auto frozen = definition.freeze();
	if (!frozen)
		return std::unexpected(frozen.error());

	auto session =
	    std::unique_ptr<Session>(new Session(std::move(definition), config));
	auto retention =
	    session->world_.setEventRetentionTicks(config.eventRetentionTicks);
	if (!retention)
		return std::unexpected(retention.error());
	for (auto &installer : session->definition_.componentInstallers_) {
		auto installed = installer(session->world_);
		if (!installed)
			return std::unexpected(installed.error());
	}
	for (auto &installer : session->definition_.eventInstallers_) {
		auto installed = installer(session->world_);
		if (!installed)
			return std::unexpected(installed.error());
	}
	for (auto &installer : session->definition_.stateInstallers_) {
		auto installed = installer(session->world_);
		if (!installed)
			return std::unexpected(installed.error());
	}
	auto projectiles = stg::ProjectileComponents::registerWith(session->world_);
	if (!projectiles)
		return std::unexpected(projectiles.error());
	session->projectileComponents_ = *projectiles;
	auto actors = stg::ActorComponents::registerWith(session->world_);
	if (!actors)
		return std::unexpected(actors.error());
	session->actorComponents_ = *actors;
	auto intent = session->world_.registerComponent<control::ActorIntent>(
	    {.name = "shiki.actor_intent.v1",
	     .flags = ComponentFlags::Deterministic});
	if (!intent)
		return std::unexpected(intent.error());
	session->actorIntent_ = *intent;
	auto phaseEvents = flow::PhaseEvents::registerWith(session->world_);
	if (!phaseEvents)
		return std::unexpected(phaseEvents.error());
	session->phaseEvents_ = *phaseEvents;
	return session;
}

Result<StepResult> Session::step(const control::InputFrame &input) {
	if (state_ != SessionState::Running) {
		return std::unexpected(makeSessionError(
		    SessionError::SessionNotRunning,
		    "Session cannot step after reaching a terminal state"));
	}
	const Tick expected{world_.tick().value + 1};
	if (config_.requireSequentialInputTicks && input.tick != expected) {
		return std::unexpected(makeSessionError(
		    SessionError::InputTickMismatch,
		    "Input frame tick does not match the next Session tick"));
	}

	auto begin = world_.beginTick();
	if (!begin) {
		state_ = SessionState::Faulted;
		return std::unexpected(begin.error());
	}
	auto external = runExternalCommands(input);
	if (!external) {
		state_ = SessionState::Faulted;
		return std::unexpected(external.error());
	}
	auto controllers = runControllers(input);
	if (!controllers) {
		state_ = SessionState::Faulted;
		return std::unexpected(controllers.error());
	}
	auto flow = runFlow(input);
	if (!flow) {
		state_ = SessionState::Faulted;
		return std::unexpected(flow.error());
	}
	for (const game::CommitPhase phase :
	     {game::CommitPhase::Flow, game::CommitPhase::Simulation,
	      game::CommitPhase::Resolution}) {
		auto systems = runSystems(phase, input);
		if (!systems) {
			state_ = SessionState::Faulted;
			return std::unexpected(systems.error());
		}
		if (phase == game::CommitPhase::Simulation) {
			auto patterns = runPatternSystems(input);
			if (!patterns) {
				state_ = SessionState::Faulted;
				return std::unexpected(patterns.error());
			}
			auto projectiles = runProjectileSystems(input);
			if (!projectiles) {
				state_ = SessionState::Faulted;
				return std::unexpected(projectiles.error());
			}
		}
		auto committed = world_.commit(phase);
		if (!committed) {
			state_ = SessionState::Faulted;
			return std::unexpected(committed.error());
		}
	}
	previousPresentation_ = std::move(currentPresentation_);
	currentPresentation_ = world_.buildPresentationSnapshot();
	return StepResult{world_.tick(), state_};
}

Result<void> Session::runSystems(game::CommitPhase commitPhase,
                                 const control::InputFrame &input) {
	auto actors = runActorBehaviors(
	    commitPhase, input, commitPhase == game::CommitPhase::Simulation);
	if (!actors)
		return std::unexpected(actors.error());
	for (std::size_t order = 0; order < definition_.schedule_.size(); ++order) {
		auto &system = definition_.systems_[definition_.schedule_[order]];
		if (commitPhaseFor(system.descriptor.phase) != commitPhase)
			continue;
		const game::CommandSource source{
		    .producer = {static_cast<std::uint32_t>(order + 1)},
		    .system = static_cast<std::uint16_t>(order),
		    .partition = 0,
		    .buffer = 0};
		auto commandResult = world_.commands(commitPhase, source);
		if (!commandResult)
			return std::unexpected(commandResult.error());
		game::Commands commands = std::move(*commandResult);
		stg::GameplayContext context{
		    world_.view(),
		    commands,
		    *projectileComponents_,
		    *actorComponents_,
		    stg::TimeView{world_.tick(), config_.tickRate},
		    input,
		    &definition_.actors_,
		    &actorBehaviors_,
		    &definition_.controllers_,
		    &controllers_,
		    &flow_,
		    &definition_.patterns_,
		    &patterns_};
#if defined(__cpp_exceptions)
		try {
			system.callback(context);
		} catch (...) {
			return std::unexpected(makeSessionError(
			    SessionError::SystemCallbackFailed,
			    "Gameplay system callback threw an exception"));
		}
#else
		system.callback(context);
#endif
		world_.publishEvents(commitPhase);
	}
	return {};
}

Result<void> Session::runActorBehaviors(game::CommitPhase commitPhase,
                                        const control::InputFrame &input,
                                        bool runTick) {
	const std::size_t system = definition_.systemCount() + 3;
	const game::CommandSource source{
	    .producer = {static_cast<std::uint32_t>(system + 1)},
	    .system = static_cast<std::uint16_t>(system),
	    .partition = 0,
	    .buffer = 0};
	auto commandResult = world_.commands(commitPhase, source);
	if (!commandResult)
		return std::unexpected(commandResult.error());
	game::Commands commands = std::move(*commandResult);
	stg::GameplayContext context{world_.view(),
	                             commands,
	                             *projectileComponents_,
	                             *actorComponents_,
	                             stg::TimeView{world_.tick(), config_.tickRate},
	                             input,
	                             &definition_.actors_,
	                             &actorBehaviors_,
	                             &definition_.controllers_,
	                             &controllers_,
	                             &flow_,
	                             &definition_.patterns_,
	                             &patterns_};
#if defined(__cpp_exceptions)
	try {
		actorBehaviors_.dispatch(context, runTick);
	} catch (...) {
		return std::unexpected(
		    makeSessionError(SessionError::SystemCallbackFailed,
		                     "Actor behavior callback threw an exception"));
	}
#else
	actorBehaviors_.dispatch(context, runTick);
#endif
	world_.publishEvents(commitPhase);
	return {};
}

Result<void> Session::runControllers(const control::InputFrame &input) {
	const std::size_t system = definition_.systemCount() + 4;
	const game::CommandSource source{
	    .producer = {static_cast<std::uint32_t>(system + 1)},
	    .system = static_cast<std::uint16_t>(system),
	    .partition = 0,
	    .buffer = 0};
	auto commandResult = world_.commands(game::CommitPhase::Flow, source);
	if (!commandResult)
		return std::unexpected(commandResult.error());
	game::Commands commands = std::move(*commandResult);
#if defined(__cpp_exceptions)
	try {
		controllers_.dispatch(world_.view(), commands, *actorIntent_, input);
	} catch (...) {
		return std::unexpected(
		    makeSessionError(SessionError::SystemCallbackFailed,
		                     "Controller callback threw an exception"));
	}
#else
	controllers_.dispatch(world_.view(), commands, *actorIntent_, input);
#endif
	return {};
}

Result<void> Session::runExternalCommands(const control::InputFrame &input) {
	if (input.commands.empty())
		return {};
	const std::size_t system = definition_.systemCount() + 2;
	const game::CommandSource source{
	    .producer = {static_cast<std::uint32_t>(system + 1)},
	    .system = static_cast<std::uint16_t>(system),
	    .partition = 0,
	    .buffer = 0};
	auto commandResult = world_.commands(game::CommitPhase::Flow, source);
	if (!commandResult)
		return std::unexpected(commandResult.error());
	game::Commands commands = std::move(*commandResult);
	stg::GameplayContext context{world_.view(),
	                             commands,
	                             *projectileComponents_,
	                             *actorComponents_,
	                             stg::TimeView{world_.tick(), config_.tickRate},
	                             input,
	                             &definition_.actors_,
	                             &actorBehaviors_,
	                             &definition_.controllers_,
	                             &controllers_,
	                             &flow_,
	                             &definition_.patterns_,
	                             &patterns_};
	for (const control::ExternalCommand &command : input.commands) {
#if defined(__cpp_exceptions)
		try {
			auto executed =
			    definition_.externalCommands_.execute(command, context);
			if (!executed)
				return std::unexpected(executed.error());
		} catch (...) {
			return std::unexpected(makeSessionError(
			    SessionError::SystemCallbackFailed,
			    "External command handler threw an exception"));
		}
#else
		auto executed = definition_.externalCommands_.execute(command, context);
		if (!executed)
			return std::unexpected(executed.error());
#endif
	}
	world_.publishEvents(game::CommitPhase::Flow);
	return {};
}

Result<void> Session::runFlow(const control::InputFrame &input) {
	const std::size_t system = definition_.systemCount() + 5;
	const game::CommandSource source{
	    .producer = {static_cast<std::uint32_t>(system + 1)},
	    .system = static_cast<std::uint16_t>(system),
	    .partition = 0,
	    .buffer = 0};
	auto commandResult = world_.commands(game::CommitPhase::Flow, source);
	if (!commandResult)
		return std::unexpected(commandResult.error());
	game::Commands commands = std::move(*commandResult);
	stg::GameplayContext context{world_.view(),
	                             commands,
	                             *projectileComponents_,
	                             *actorComponents_,
	                             stg::TimeView{world_.tick(), config_.tickRate},
	                             input,
	                             &definition_.actors_,
	                             &actorBehaviors_,
	                             &definition_.controllers_,
	                             &controllers_,
	                             &flow_,
	                             &definition_.patterns_,
	                             &patterns_};
	auto result = flow_.dispatch(definition_.actions_, *phaseEvents_,
	                             definition_.conditions_,
	                             actorComponents_->health, context);
	if (!result)
		return std::unexpected(result.error());
	world_.publishEvents(game::CommitPhase::Flow);
	return {};
}

Result<void> Session::runPatternSystems(const control::InputFrame &input) {
	const std::size_t system = definition_.systemCount() + 6;
	const game::CommandSource source{
	    .producer = {static_cast<std::uint32_t>(system + 1)},
	    .system = static_cast<std::uint16_t>(system),
	    .partition = 0,
	    .buffer = 0};
	auto commandResult = world_.commands(game::CommitPhase::Simulation, source);
	if (!commandResult)
		return std::unexpected(commandResult.error());
	game::Commands commands = std::move(*commandResult);
	stg::GameplayContext context{world_.view(),
	                             commands,
	                             *projectileComponents_,
	                             *actorComponents_,
	                             stg::TimeView{world_.tick(), config_.tickRate},
	                             input,
	                             &definition_.actors_,
	                             &actorBehaviors_,
	                             &definition_.controllers_,
	                             &controllers_,
	                             &flow_,
	                             &definition_.patterns_,
	                             &patterns_};
	auto result = patterns_.dispatch(context);
	if (!result)
		return std::unexpected(result.error());
	world_.publishEvents(game::CommitPhase::Simulation);
	return {};
}

Result<void> Session::runProjectileSystems(const control::InputFrame &input) {
	static_cast<void>(input);
	const std::size_t firstSystem = definition_.systemCount();
	const game::CommandSource movementSource{
	    .producer = {static_cast<std::uint32_t>(firstSystem + 1)},
	    .system = static_cast<std::uint16_t>(firstSystem),
	    .partition = 0,
	    .buffer = 0};
	auto movementResult =
	    world_.commands(game::CommitPhase::Simulation, movementSource);
	if (!movementResult)
		return std::unexpected(movementResult.error());
	game::Commands movementCommands = std::move(*movementResult);
	stg::updateProjectileMovement(world_.view(), movementCommands,
	                              *projectileComponents_);

	const game::CommandSource lifetimeSource{
	    .producer = {static_cast<std::uint32_t>(firstSystem + 2)},
	    .system = static_cast<std::uint16_t>(firstSystem + 1),
	    .partition = 0,
	    .buffer = 0};
	auto lifetimeResult =
	    world_.commands(game::CommitPhase::Simulation, lifetimeSource);
	if (!lifetimeResult)
		return std::unexpected(lifetimeResult.error());
	game::Commands lifetimeCommands = std::move(*lifetimeResult);
	stg::updateProjectileLifetimes(world_.view(), lifetimeCommands,
	                               *projectileComponents_);
	return {};
}

} // namespace shiki

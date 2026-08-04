#include <shiki/stg/actor/actor_api.h>

#include <string>

namespace shiki::stg {
namespace {

[[nodiscard]] Error actorCommandError(game::CommandStatus status) {
	return Error{ErrorDomain::World,
	             static_cast<std::uint32_t>(status),
	             "Actor command was rejected",
	             {{"command_status",
	               std::to_string(static_cast<std::uint32_t>(status))}}};
}

} // namespace

Result<ActorComponents> ActorComponents::registerWith(game::World &world) {
	using game::ComponentFlags;
	constexpr ComponentFlags observable =
	    ComponentFlags::Observable | ComponentFlags::Deterministic;
	auto transform = world.registerComponent<Transform>(
	    {.name = "shiki.transform.v1", .flags = observable});
	if (!transform)
		return std::unexpected(transform.error());
	auto motion = world.registerComponent<Motion>(
	    {.name = "shiki.motion.v1", .flags = observable});
	if (!motion)
		return std::unexpected(motion.error());
	auto collision = world.registerComponent<CircleCollision>(
	    {.name = "shiki.circle_collision.v1", .flags = observable});
	if (!collision)
		return std::unexpected(collision.error());
	auto relation = world.registerComponent<Relation>(
	    {.name = "shiki.relation.v1", .flags = ComponentFlags::Deterministic});
	if (!relation)
		return std::unexpected(relation.error());
	auto identity = world.registerComponent<ActorIdentity>(
	    {.name = "shiki.actor.identity.v1",
	     .flags = ComponentFlags::Deterministic});
	if (!identity)
		return std::unexpected(identity.error());
	auto health = world.registerComponent<Health>(
	    {.name = "shiki.health.v1", .flags = observable});
	if (!health)
		return std::unexpected(health.error());
	return ActorComponents{*transform, *motion,   *collision,
	                       *relation,  *identity, *health};
}

ActorApi::ActorApi(game::Commands &commands, const ActorComponents &components,
                   const ActorRegistry *registry, ActorBehaviorPool *behaviors,
                   const control::ControllerRegistry *controllerRegistry,
                   control::ControllerPool *controllers) noexcept
    : commands_(&commands), components_(&components), registry_(registry),
      behaviors_(behaviors), controllerRegistry_(controllerRegistry),
      controllers_(controllers) {}

Result<game::EntityHandle> ActorApi::spawn(const ActorSpec &spec,
                                           const ActorSpawn &spawn) {
	auto entity = commands_->spawn();
	if (!entity)
		return std::unexpected(entity.error());
	const auto write = [&](game::CommandStatus status) -> Result<void> {
		if (status == game::CommandStatus::Accepted)
			return {};
		return std::unexpected(actorCommandError(status));
	};
	const auto abort = [&] { static_cast<void>(commands_->destroy(*entity)); };

	for (const auto result :
	     {write(commands_->set(*entity, components_->transform,
	                           Transform{spawn.position, spawn.orientation})),
	      write(commands_->set(*entity, components_->motion,
	                           Motion{spawn.velocityPerTick})),
	      write(commands_->set(*entity, components_->collision,
	                           CircleCollision{spec.collisionRadius})),
	      write(commands_->set(*entity, components_->relation,
	                           Relation{spawn.owner, spec.faction})),
	      write(commands_->set(*entity, components_->identity,
	                           ActorIdentity{spec.type})),
	      write(commands_->set(*entity, components_->health,
	                           Health{spec.health, spec.health}))}) {
		if (!result) {
			abort();
			return std::unexpected(result.error());
		}
	}
	if (spec.controller) {
		if (controllerRegistry_ == nullptr || controllers_ == nullptr) {
			abort();
			return std::unexpected(
			    Error{ErrorDomain::Definition,
			          static_cast<std::uint32_t>(
			              control::ControllerError::UnknownType),
			          "Controller services are unavailable in this context"});
		}
		auto controller = controllerRegistry_->create(*spec.controller);
		if (!controller) {
			abort();
			return std::unexpected(controller.error());
		}
		controllers_->attach(*entity, std::move(*controller));
	}
	return *entity;
}

Result<game::EntityHandle> ActorApi::spawn(ActorTypeId type,
                                           const ActorSpawn &spawnData) {
	if (registry_ == nullptr || behaviors_ == nullptr) {
		return std::unexpected(
		    Error{ErrorDomain::Definition,
		          static_cast<std::uint32_t>(ActorError::UnknownType),
		          "Actor behavior services are unavailable in this context"});
	}
	auto behavior = registry_->create(type);
	if (!behavior)
		return std::unexpected(behavior.error());
	ActorBuilder builder{type};
#if defined(__cpp_exceptions)
	try {
		(*behavior)->configure(builder);
	} catch (...) {
		return std::unexpected(Error{
		    ErrorDomain::Definition,
		    static_cast<std::uint32_t>(ActorError::BehaviorCallbackFailed),
		    "Actor configure callback threw an exception"});
	}
#else
	(*behavior)->configure(builder);
#endif
	auto spec = builder.build();
	if (!spec)
		return std::unexpected(spec.error());
	auto entity = spawn(*spec, spawnData);
	if (!entity)
		return std::unexpected(entity.error());
	behaviors_->attach(*entity, std::move(*behavior));
	return entity;
}

} // namespace shiki::stg

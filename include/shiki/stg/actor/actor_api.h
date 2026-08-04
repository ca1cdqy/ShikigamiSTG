#pragma once

#include <shiki/core/result.h>
#include <shiki/game/commands.h>
#include <shiki/game/world.h>
#include <shiki/stg/actor/actor.h>
#include <shiki/stg/actor/actor_behavior.h>

namespace shiki::stg {

/** Holds the World registrations required by neutral actor entities. */
struct ActorComponents final {
	game::ComponentToken<Transform> transform;
	game::ComponentToken<Motion> motion;
	game::ComponentToken<CircleCollision> collision;
	game::ComponentToken<Relation> relation;
	game::ComponentToken<ActorIdentity> identity;
	game::ComponentToken<Health> health;

	/** Registers the neutral actor schema before the first World tick. */
	[[nodiscard]] static Result<ActorComponents>
	registerWith(game::World &world);
};

/** Records neutral actor creation through a phase-scoped command buffer. */
class ActorApi final {
  public:
	/** Creates a non-owning API view for the current callback. */
	ActorApi(game::Commands &commands, const ActorComponents &components,
	         const ActorRegistry *registry = nullptr,
	         ActorBehaviorPool *behaviors = nullptr,
	         const control::ControllerRegistry *controllerRegistry = nullptr,
	         control::ControllerPool *controllers = nullptr) noexcept;

	/** Records one actor and returns its stable pending handle. */
	[[nodiscard]] Result<game::EntityHandle> spawn(const ActorSpec &spec,
	                                               const ActorSpawn &spawn);

	/** Creates, configures, and records one registered Actor behavior type. */
	[[nodiscard]] Result<game::EntityHandle> spawn(ActorTypeId type,
	                                               const ActorSpawn &spawn);

	/** Creates a registered Actor behavior with typed per-spawn arguments. */
	template <ActorArguments T>
	[[nodiscard]] Result<game::EntityHandle>
	spawn(ActorType<T> type, T arguments, const ActorSpawn &spawnData);

  private:
	game::Commands *commands_{};
	const ActorComponents *components_{};
	const ActorRegistry *registry_{};
	ActorBehaviorPool *behaviors_{};
	const control::ControllerRegistry *controllerRegistry_{};
	control::ControllerPool *controllers_{};
};

template <ActorArguments T>
Result<game::EntityHandle> ActorApi::spawn(ActorType<T> type, T arguments,
                                           const ActorSpawn &spawnData) {
	if (registry_ == nullptr || behaviors_ == nullptr)
		return std::unexpected(
		    Error{ErrorDomain::Definition,
		          static_cast<std::uint32_t>(ActorError::UnknownType),
		          "Actor behavior services are unavailable in this context"});
	auto behavior =
	    registry_->create(ActorInvocation::create(type, std::move(arguments)));
	if (!behavior)
		return std::unexpected(behavior.error());
	ActorBuilder builder{type.id()};
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

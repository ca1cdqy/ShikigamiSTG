#include <shiki/stg/actor/actor_behavior.h>

#include <shiki/stg/gameplay_context.h>

#include <algorithm>
#include <string>

namespace shiki::stg {
namespace {

[[nodiscard]] Error makeActorError(ActorError code, std::string message) {
	return Error{ErrorDomain::Definition, static_cast<std::uint32_t>(code),
	             std::move(message)};
}

} // namespace

ActorBuilder &ActorBuilder::faction(FactionId value) noexcept {
	faction_ = value;
	return *this;
}

ActorBuilder &ActorBuilder::collisionRadius(float value) noexcept {
	collisionRadius_ = value;
	return *this;
}

ActorBuilder &ActorBuilder::health(std::int64_t value) noexcept {
	health_ = value;
	return *this;
}

ActorBuilder &
ActorBuilder::controller(control::ControllerTypeId value) noexcept {
	controller_ = value;
	return *this;
}

Result<ActorSpec> ActorBuilder::build() const {
	if (health_ <= 0) {
		return std::unexpected(makeActorError(ActorError::InvalidHealth,
		                                      "Actor health must be positive"));
	}
	if (collisionRadius_ < 0.0f) {
		return std::unexpected(
		    makeActorError(ActorError::InvalidHealth,
		                   "Actor collision radius cannot be negative"));
	}
	return ActorSpec{type_.key, faction_, collisionRadius_, health_,
	                 controller_};
}

void Actor::onSpawn(GameplayContext &game) { static_cast<void>(game); }

void Actor::onTick(GameplayContext &game) { static_cast<void>(game); }

void Actor::onDespawn(GameplayContext &game, DespawnReason reason) {
	static_cast<void>(game);
	static_cast<void>(reason);
}

Result<ActorTypeId> ActorRegistry::add(std::string name, Factory factory) {
	if (!factory)
		return std::unexpected(makeActorError(ActorError::FactoryFailed,
		                                      "Actor factory cannot be empty"));
	auto typed = add<ActorNoArguments>(
	    std::move(name),
	    [factory = std::move(factory)](const ActorNoArguments &)
	        -> Result<std::unique_ptr<Actor>> { return factory(); });
	if (!typed)
		return std::unexpected(typed.error());
	return typed->id();
}

Result<std::unique_ptr<Actor>> ActorRegistry::create(ActorTypeId type) const {
	const auto entry = std::ranges::find(entries_, type, &Entry::type);
	if (entry == entries_.end()) {
		return std::unexpected(makeActorError(ActorError::UnknownType,
		                                      "Actor type is not registered"));
	}
	if (entry->typeTag != &actorArgumentTypeTag<ActorNoArguments>)
		return std::unexpected(
		    makeActorError(ActorError::ArgumentMismatch,
		                   "Actor type requires typed construction arguments"));
	const ActorNoArguments arguments;
	auto created = entry->factory(&arguments);
	if (!created)
		return std::unexpected(created.error());
	std::unique_ptr<Actor> behavior = std::move(*created);
	if (!behavior) {
		return std::unexpected(
		    makeActorError(ActorError::FactoryFailed,
		                   "Actor factory returned a null behavior"));
	}
	return behavior;
}

Result<std::unique_ptr<Actor>>
ActorRegistry::create(const ActorInvocation &invocation) const {
	const auto entry =
	    std::ranges::find(entries_, invocation.type_, &Entry::type);
	if (entry == entries_.end())
		return std::unexpected(makeActorError(ActorError::UnknownType,
		                                      "Actor type is not registered"));
	if (entry->typeTag != invocation.typeTag_)
		return std::unexpected(makeActorError(
		    ActorError::ArgumentMismatch,
		    "Actor invocation arguments do not match the registered type"));
	auto behavior = entry->factory(invocation.arguments_.get());
	if (!behavior)
		return std::unexpected(behavior.error());
	if (!*behavior)
		return std::unexpected(
		    makeActorError(ActorError::FactoryFailed,
		                   "Actor factory returned a null behavior"));
	return behavior;
}

std::vector<std::string> ActorRegistry::names() const {
	std::vector<std::string> result;
	result.reserve(entries_.size());
	for (const Entry &entry : entries_)
		result.push_back(entry.name);
	return result;
}

void ActorBehaviorPool::attach(game::EntityHandle entity,
                               std::unique_ptr<Actor> behavior) {
	behavior->entity_ = entity;
	entries_.push_back(Entry{entity, std::move(behavior), false});
}

void ActorBehaviorPool::dispatch(GameplayContext &game, bool runTick) {
	const std::size_t dispatchCount = entries_.size();
	for (std::size_t index = 0; index < dispatchCount; ++index) {
		auto &entry = entries_[index];
		const game::EntityState state = game.world().state(entry.entity);
		if (state == game::EntityState::Alive) {
			if (!entry.spawned) {
				entry.spawned = true;
				entry.behavior->onSpawn(game);
			}
			if (runTick)
				entry.behavior->onTick(game);
		} else if (state == game::EntityState::Dead) {
			if (entry.spawned)
				entry.behavior->onDespawn(game, DespawnReason::Destroyed);
			entry.behavior.reset();
		}
	}
	std::erase_if(entries_,
	              [](const Entry &entry) { return entry.behavior == nullptr; });
}

} // namespace shiki::stg

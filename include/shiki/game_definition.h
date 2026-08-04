#pragma once

#include <shiki/control/controller.h>
#include <shiki/control/external_command.h>
#include <shiki/core/result.h>
#include <shiki/flow/action.h>
#include <shiki/flow/condition.h>
#include <shiki/flow/stage_parser.h>
#include <shiki/game/component.h>
#include <shiki/game/event.h>
#include <shiki/game/state.h>
#include <shiki/game/system.h>
#include <shiki/game/type_registry.h>
#include <shiki/game/world.h>
#include <shiki/stg/actor/actor_behavior.h>
#include <shiki/stg/pattern/pattern.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace shiki::stg {
class GameplayContext;
}

namespace shiki {

/** Stable failures produced while assembling and freezing a game definition. */
enum class DefinitionError : std::uint32_t {
	EmptySystemName = 1,
	DuplicateSystem,
	MissingDependency,
	ReversePhaseDependency,
	ScheduleCycle,
	DefinitionFrozen,
	TooManySystems,
	InvalidSystemCallback,
	EmptyTypeName,
	DuplicateType
};

/** Builds the immutable type schema and deterministic gameplay schedule. */
class GameDefinition final {
  public:
	using GameplaySystem = std::function<void(stg::GameplayContext &)>;

	GameDefinition() = default;
	~GameDefinition() = default;
	GameDefinition(const GameDefinition &) = delete;
	GameDefinition &operator=(const GameDefinition &) = delete;
	GameDefinition(GameDefinition &&) noexcept = default;
	GameDefinition &operator=(GameDefinition &&) noexcept = default;

	/** Adds one gameplay callback before the definition is frozen. */
	[[nodiscard]] Result<void> addSystem(game::SystemDescriptor descriptor,
	                                     GameplaySystem system);

	/** Registers one user component and returns its persistent typed token. */
	template <game::Component T>
	[[nodiscard]] Result<game::ComponentToken<T>>
	registerComponent(game::ComponentDescriptor<T> descriptor);

	/** Registers one typed event and returns its persistent typed token. */
	template <game::Event T>
	[[nodiscard]] Result<game::EventToken<T>>
	registerEvent(game::EventDescriptor<T> descriptor);

	/** Registers one typed Session State entry and its persistent codec. */
	template <game::State T>
	[[nodiscard]] Result<game::StateKey<T>>
	registerState(game::StateDescriptor<T> descriptor);

	/** Returns the actor behavior registry owned by this definition. */
	[[nodiscard]] stg::ActorRegistry &actors() noexcept { return actors_; }

	/** Returns the normalized controller registry owned by this definition. */
	[[nodiscard]] control::ControllerRegistry &controllers() noexcept {
		return controllers_;
	}

	/** Returns the deterministic InputFrame command registry. */
	[[nodiscard]] control::ExternalCommandRegistry &
	externalCommands() noexcept {
		return externalCommands_;
	}

	/** Returns the typed stage action registry owned by this definition. */
	[[nodiscard]] flow::ActionRegistry &actions() noexcept { return actions_; }
	/** Returns the typed read-only Flow condition registry. */
	[[nodiscard]] flow::ConditionRegistry &conditions() noexcept {
		return conditions_;
	}

	/** Returns the external stage parser registry owned by this definition. */
	[[nodiscard]] flow::StageParserRegistry &stages() noexcept {
		return stages_;
	}

	/** Returns the typed Pattern factory registry owned by this definition. */
	[[nodiscard]] stg::PatternRegistry &patterns() noexcept {
		return patterns_;
	}

	/** Returns whether no further registrations are accepted. */
	[[nodiscard]] bool isFrozen() const noexcept { return frozen_; }

	/** Returns the frozen schema digest. */
	[[nodiscard]] game::SchemaDigest schemaDigest() const noexcept {
		return types_.digest();
	}

  private:
	struct SystemEntry final {
		game::SystemDescriptor descriptor;
		game::TypeKey key{};
		GameplaySystem callback;
	};

	using ComponentInstaller = std::function<Result<void>(game::World &)>;
	using EventInstaller = std::function<Result<void>(game::World &)>;
	using StateInstaller = std::function<Result<void>(game::World &)>;
	struct RegistrationMetadata final {
		const void *typeTag{};
		std::string name;
		std::uint32_t version{};
		std::uint64_t flags{};
		std::string codec;
	};

	[[nodiscard]] Result<void> freeze();
	[[nodiscard]] std::size_t systemCount() const noexcept {
		return systems_.size();
	}

	game::TypeRegistry types_;
	std::vector<SystemEntry> systems_;
	std::vector<std::size_t> schedule_;
	std::map<game::TypeKey, RegistrationMetadata> componentTypes_;
	std::vector<ComponentInstaller> componentInstallers_;
	std::map<game::TypeKey, RegistrationMetadata> eventTypes_;
	std::vector<EventInstaller> eventInstallers_;
	std::map<game::TypeKey, RegistrationMetadata> stateTypes_;
	std::vector<StateInstaller> stateInstallers_;
	stg::ActorRegistry actors_;
	control::ControllerRegistry controllers_;
	control::ExternalCommandRegistry externalCommands_;
	flow::ActionRegistry actions_;
	flow::ConditionRegistry conditions_;
	flow::StageParserRegistry stages_;
	stg::PatternRegistry patterns_;
	bool frozen_{};

	friend class Session;
};

} // namespace shiki

namespace shiki {

template <game::State T>
Result<game::StateKey<T>>
GameDefinition::registerState(game::StateDescriptor<T> descriptor) {
	if (frozen_)
		return std::unexpected(
		    Error{ErrorDomain::Definition,
		          static_cast<std::uint32_t>(DefinitionError::DefinitionFrozen),
		          "Cannot register State after the definition is frozen"});
	if (descriptor.name.empty())
		return std::unexpected(
		    Error{ErrorDomain::Definition,
		          static_cast<std::uint32_t>(DefinitionError::EmptyTypeName),
		          "State name cannot be empty"});
	if (descriptor.codec.identity.empty() || !descriptor.codec.encode ||
	    !descriptor.codec.decode)
		return std::unexpected(
		    Error{ErrorDomain::Definition,
		          static_cast<std::uint32_t>(game::StateError::InvalidCodec),
		          "State codec must have an identity, encoder, and decoder"});
	const game::TypeKey key = game::typeKeyFromName(descriptor.name);
	const std::uint64_t flags = static_cast<std::uint64_t>(descriptor.flags);
	const auto existing = stateTypes_.find(key);
	if (existing != stateTypes_.end()) {
		if (existing->second.typeTag == &game::stateTypeTag<T> &&
		    existing->second.name == descriptor.name &&
		    existing->second.version == descriptor.version &&
		    existing->second.flags == flags &&
		    existing->second.codec == descriptor.codec.identity)
			return game::StateKey<T>{key, &game::stateTypeTag<T>};
		return std::unexpected(
		    Error{ErrorDomain::Definition,
		          static_cast<std::uint32_t>(DefinitionError::DuplicateType),
		          "State key has incompatible type or metadata"});
	}
	auto type = types_.add({.domain = game::TypeDomain::State,
	                        .name = descriptor.name,
	                        .version = descriptor.version,
	                        .flags = flags,
	                        .codec = descriptor.codec.identity});
	if (!type)
		return std::unexpected(type.error());
	stateTypes_.emplace(key, RegistrationMetadata{&game::stateTypeTag<T>,
	                                              descriptor.name,
	                                              descriptor.version, flags,
	                                              descriptor.codec.identity});
	stateInstallers_.push_back(
	    [descriptor = std::move(descriptor)](game::World &world) mutable {
		    auto result = world.registerState<T>(std::move(descriptor));
		    if (!result)
			    return Result<void>{std::unexpected(result.error())};
		    return Result<void>{};
	    });
	return game::StateKey<T>{key, &game::stateTypeTag<T>};
}

} // namespace shiki

namespace shiki {

template <game::Event T>
Result<game::EventToken<T>>
GameDefinition::registerEvent(game::EventDescriptor<T> descriptor) {
	if (frozen_) {
		return std::unexpected(
		    Error{ErrorDomain::Definition,
		          static_cast<std::uint32_t>(DefinitionError::DefinitionFrozen),
		          "Cannot register an event after the definition is frozen"});
	}
	if (descriptor.name.empty()) {
		return std::unexpected(
		    Error{ErrorDomain::Definition,
		          static_cast<std::uint32_t>(DefinitionError::EmptyTypeName),
		          "Event name cannot be empty"});
	}
	const game::TypeKey key = game::typeKeyFromName(descriptor.name);
	const auto existing = eventTypes_.find(key);
	if (existing != eventTypes_.end()) {
		if (existing->second.typeTag == &game::eventTypeTag<T> &&
		    existing->second.name == descriptor.name &&
		    existing->second.version == descriptor.version &&
		    existing->second.flags ==
		        static_cast<std::uint64_t>(descriptor.visibility))
			return game::EventToken<T>{key, &game::eventTypeTag<T>};
		return std::unexpected(
		    Error{ErrorDomain::Definition,
		          static_cast<std::uint32_t>(DefinitionError::DuplicateType),
		          "Event type key has incompatible type or metadata"});
	}
	auto type = types_.add(
	    {.domain = game::TypeDomain::Event,
	     .name = descriptor.name,
	     .version = descriptor.version,
	     .flags = static_cast<std::uint64_t>(descriptor.visibility)});
	if (!type)
		return std::unexpected(type.error());
	eventTypes_.emplace(
	    key, RegistrationMetadata{
	             &game::eventTypeTag<T>, descriptor.name, descriptor.version,
	             static_cast<std::uint64_t>(descriptor.visibility)});
	eventInstallers_.push_back(
	    [descriptor = std::move(descriptor)](game::World &world) mutable {
		    auto result = world.registerEvent<T>(std::move(descriptor));
		    if (!result)
			    return Result<void>{std::unexpected(result.error())};
		    return Result<void>{};
	    });
	return game::EventToken<T>{key, &game::eventTypeTag<T>};
}

} // namespace shiki

namespace shiki {

template <game::Component T>
Result<game::ComponentToken<T>>
GameDefinition::registerComponent(game::ComponentDescriptor<T> descriptor) {
	if (frozen_) {
		return std::unexpected(Error{
		    ErrorDomain::Definition,
		    static_cast<std::uint32_t>(DefinitionError::DefinitionFrozen),
		    "Cannot register a component after the definition is frozen"});
	}
	if (descriptor.name.empty()) {
		return std::unexpected(
		    Error{ErrorDomain::Definition,
		          static_cast<std::uint32_t>(DefinitionError::EmptyTypeName),
		          "Component name cannot be empty"});
	}
	const game::TypeKey key = game::typeKeyFromName(descriptor.name);
	const auto existing = componentTypes_.find(key);
	if (existing != componentTypes_.end()) {
		if (existing->second.typeTag == &game::componentTypeTag<T> &&
		    existing->second.name == descriptor.name &&
		    existing->second.version == descriptor.version &&
		    existing->second.flags ==
		        static_cast<std::uint64_t>(descriptor.flags))
			return game::ComponentToken<T>{key, &game::componentTypeTag<T>};
		return std::unexpected(
		    Error{ErrorDomain::Definition,
		          static_cast<std::uint32_t>(DefinitionError::DuplicateType),
		          "Component type key has incompatible type or metadata"});
	}
	const auto type =
	    types_.add({.domain = game::TypeDomain::Component,
	                .name = descriptor.name,
	                .version = descriptor.version,
	                .flags = static_cast<std::uint64_t>(descriptor.flags)});
	if (!type)
		return std::unexpected(type.error());
	componentTypes_.emplace(
	    key,
	    RegistrationMetadata{&game::componentTypeTag<T>, descriptor.name,
	                         descriptor.version,
	                         static_cast<std::uint64_t>(descriptor.flags)});
	componentInstallers_.push_back(
	    [descriptor = std::move(descriptor)](game::World &world) mutable {
		    auto result =
		        world.template registerComponent<T>(std::move(descriptor));
		    if (!result)
			    return Result<void>{std::unexpected(result.error())};
		    return Result<void>{};
	    });
	return game::ComponentToken<T>{key, &game::componentTypeTag<T>};
}

} // namespace shiki

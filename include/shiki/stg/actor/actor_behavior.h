#pragma once

#include <shiki/core/result.h>
#include <shiki/game/entity.h>
#include <shiki/stg/actor/actor.h>

#include <concepts>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace shiki::stg {

class GameplayContext;

/** Identifies a registered object-oriented actor behavior factory. */
struct ActorTypeId final {
	game::TypeKey key{};
	auto operator<=>(const ActorTypeId &) const = default;
};

template <class T>
concept ActorArguments = std::movable<T> && std::destructible<T>;

template <class T> inline const char actorArgumentTypeTag = 0;

/** Empty argument value used by the no-argument Actor convenience API. */
struct ActorNoArguments final {};

/** A typed persistent actor factory reference. */
template <ActorArguments T> class ActorType final {
  public:
	/** Returns the untyped persistent identity used by ECS components. */
	[[nodiscard]] constexpr ActorTypeId id() const noexcept { return type_; }
	[[nodiscard]] constexpr explicit operator bool() const noexcept {
		return typeTag_ != nullptr;
	}

  private:
	constexpr ActorType(ActorTypeId type, const void *typeTag) noexcept
	    : type_(type), typeTag_(typeTag) {}
	ActorTypeId type_{};
	const void *typeTag_{};
	friend class ActorRegistry;
	friend class ActorInvocation;
};

/** Argument value paired with one validated typed actor factory. */
class ActorInvocation final {
  public:
	template <ActorArguments T>
	[[nodiscard]] static ActorInvocation create(ActorType<T> type,
	                                            T arguments) {
		return ActorInvocation{type.type_, &actorArgumentTypeTag<T>,
		                       std::make_shared<const T>(std::move(arguments))};
	}

  private:
	ActorInvocation(ActorTypeId type, const void *typeTag,
	                std::shared_ptr<const void> arguments) noexcept
	    : type_(type), typeTag_(typeTag), arguments_(std::move(arguments)) {}
	ActorTypeId type_{};
	const void *typeTag_{};
	std::shared_ptr<const void> arguments_;
	friend class ActorRegistry;
};

/** Describes why an activated actor behavior is leaving the World. */
enum class DespawnReason : std::uint8_t {
	Destroyed, ///< The owning entity reached the Dead lifecycle state.
	SessionEnd ///< The Session terminated while the actor remained active.
};

/** Stable failures produced by actor registration and construction. */
enum class ActorError : std::uint32_t {
	EmptyTypeName = 1,
	DuplicateType,
	UnknownType,
	FactoryFailed,
	InvalidHealth,
	BehaviorCallbackFailed,
	RegistryFrozen,
	ArgumentMismatch
};

/** Collects the neutral entity defaults configured by an Actor behavior. */
class ActorBuilder final {
  public:
	explicit ActorBuilder(ActorTypeId type) noexcept : type_(type) {}
	ActorBuilder &faction(FactionId value) noexcept;
	ActorBuilder &collisionRadius(float value) noexcept;
	ActorBuilder &health(std::int64_t value) noexcept;
	/** Selects the normalized input source constructed for this actor. */
	ActorBuilder &controller(control::ControllerTypeId value) noexcept;
	[[nodiscard]] Result<ActorSpec> build() const;

  private:
	ActorTypeId type_{};
	FactionId faction_{};
	float collisionRadius_{};
	std::int64_t health_{1};
	std::optional<control::ControllerTypeId> controller_;
};

/** Optional object-oriented behavior attached to one neutral actor entity. */
class Actor {
  public:
	Actor() = default;
	virtual ~Actor() = default;
	Actor(const Actor &) = delete;
	Actor &operator=(const Actor &) = delete;
	Actor(Actor &&) = delete;
	Actor &operator=(Actor &&) = delete;

	virtual void configure(ActorBuilder &builder) = 0;
	virtual void onSpawn(GameplayContext &game);
	virtual void onTick(GameplayContext &game);
	virtual void onDespawn(GameplayContext &game, DespawnReason reason);

	[[nodiscard]] game::EntityHandle entity() const noexcept { return entity_; }

  private:
	game::EntityHandle entity_{};
	friend class ActorBehaviorPool;
};

/** Stores factories for optional Actor behavior types, never live instances. */
class ActorRegistry final {
  public:
	using Factory = std::function<std::unique_ptr<Actor>()>;

	[[nodiscard]] Result<ActorTypeId> add(std::string name, Factory factory);

	/** Registers one actor factory with typed per-spawn arguments. */
	template <ActorArguments T>
	[[nodiscard]] Result<ActorType<T>>
	add(std::string name,
	    std::function<Result<std::unique_ptr<Actor>>(const T &)> factory);

	[[nodiscard]] Result<std::unique_ptr<Actor>> create(ActorTypeId type) const;
	/** Constructs an actor from a validated typed invocation. */
	[[nodiscard]] Result<std::unique_ptr<Actor>>
	create(const ActorInvocation &invocation) const;
	[[nodiscard]] std::vector<std::string> names() const;
	/** Prevents factory mutation after the owning definition is frozen. */
	void freeze() noexcept { frozen_ = true; }

  private:
	struct Entry final {
		std::string name;
		ActorTypeId type{};
		const void *typeTag{};
		std::function<Result<std::unique_ptr<Actor>>(const void *)> factory;
	};
	std::vector<Entry> entries_;
	bool frozen_{};
};

template <ActorArguments T>
Result<ActorType<T>> ActorRegistry::add(
    std::string name,
    std::function<Result<std::unique_ptr<Actor>>(const T &)> factory) {
	if (frozen_)
		return std::unexpected(
		    Error{ErrorDomain::Definition,
		          static_cast<std::uint32_t>(ActorError::RegistryFrozen),
		          "Actor registry cannot change after definition freeze"});
	if (name.empty())
		return std::unexpected(
		    Error{ErrorDomain::Definition,
		          static_cast<std::uint32_t>(ActorError::EmptyTypeName),
		          "Actor type name cannot be empty"});
	if (!factory)
		return std::unexpected(
		    Error{ErrorDomain::Definition,
		          static_cast<std::uint32_t>(ActorError::FactoryFailed),
		          "Actor factory cannot be empty"});
	const ActorTypeId type{game::typeKeyFromName(name)};
	for (const Entry &entry : entries_)
		if (entry.type == type)
			return std::unexpected(Error{
			    ErrorDomain::Definition,
			    static_cast<std::uint32_t>(ActorError::DuplicateType),
			    "Actor type name or persistent key is already registered"});
	entries_.push_back(
	    Entry{std::move(name), type, &actorArgumentTypeTag<T>,
	          [factory = std::move(factory)](const void *arguments) {
		          return factory(*static_cast<const T *>(arguments));
	          }});
	return ActorType<T>{type, &actorArgumentTypeTag<T>};
}

/** Owns live Actor behavior objects while ECS owns all gameplay state. */
class ActorBehaviorPool final {
  public:
	void attach(game::EntityHandle entity, std::unique_ptr<Actor> behavior);
	void dispatch(GameplayContext &game, bool runTick);

  private:
	struct Entry final {
		game::EntityHandle entity{};
		std::unique_ptr<Actor> behavior;
		bool spawned{};
	};
	std::vector<Entry> entries_;
};

} // namespace shiki::stg

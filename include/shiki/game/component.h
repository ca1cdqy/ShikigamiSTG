#pragma once

#include <shiki/game/type_id.h>

#include <concepts>
#include <cstdint>
#include <string>
#include <utility>

namespace shiki {
class GameDefinition;
}

namespace shiki::game {

class World;
class WorldView;
class Commands;

/** Defines storage, snapshot, and determinism behavior for a component type. */
enum class ComponentFlags : std::uint32_t {
	None = 0,              ///< No optional behavior is enabled.
	Observable = 1U << 0U, ///< The component can enter presentation snapshots.
	Deterministic = 1U << 1U, ///< The component participates in state hashing.
	Transient =
	    1U << 2U,  ///< The component can be rebuilt from deterministic state.
	Tag = 1U << 3U ///< The component carries identity without payload data.
};

/** Combines independent component behavior flags. */
[[nodiscard]] constexpr ComponentFlags
operator|(ComponentFlags left, ComponentFlags right) noexcept {
	return static_cast<ComponentFlags>(static_cast<std::uint32_t>(left) |
	                                   static_cast<std::uint32_t>(right));
}

/** Returns whether a component behavior flag is enabled. */
[[nodiscard]] constexpr bool hasFlag(ComponentFlags value,
                                     ComponentFlags flag) noexcept {
	return (static_cast<std::uint32_t>(value) &
	        static_cast<std::uint32_t>(flag)) != 0;
}

/** Marks a value type that can be stored on an entity. */
template <class T>
concept Component = std::movable<T> && std::destructible<T>;

/** Describes a component before it is registered with a World. */
template <Component T> struct ComponentDescriptor final {
	std::string name;
	std::uint32_t version{1};
	ComponentFlags flags{ComponentFlags::Deterministic};
};

template <class T> inline const char componentTypeTag = 0;

/** A typed reference to one component registration. */
template <Component T> class ComponentToken final {
  public:
	/** Returns the persistent identity of this component type. */
	[[nodiscard]] constexpr TypeKey key() const noexcept { return key_; }

	/** Returns whether this token belongs to a successful registration. */
	[[nodiscard]] constexpr explicit operator bool() const noexcept {
		return typeTag_ != nullptr;
	}

  private:
	constexpr ComponentToken(TypeKey key, const void *typeTag) noexcept
	    : key_(key), typeTag_(typeTag) {}

	TypeKey key_{};
	const void *typeTag_{};

	friend class World;
	friend class WorldView;
	friend class Commands;
	friend class ::shiki::GameDefinition;
};

} // namespace shiki::game

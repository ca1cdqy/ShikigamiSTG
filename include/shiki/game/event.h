#pragma once

#include <shiki/core/time.h>
#include <shiki/game/type_id.h>

#include <concepts>
#include <cstdint>
#include <string>

namespace shiki {
class GameDefinition;
}

namespace shiki::game {

class EventStream;
class World;
class Commands;

/** Selects whether an event may cross the simulation/presentation boundary. */
enum class EventVisibility : std::uint8_t {
	Simulation, ///< The event remains inside deterministic gameplay.
	SimulationAndPresentation ///< The event is copied for presentation
	                          ///< consumers.
};

/** Marks a value type that can be published through the event stream. */
template <class T>
concept Event = std::movable<T> && std::destructible<T>;

/** Describes a typed event before a GameDefinition is frozen. */
template <Event T> struct EventDescriptor final {
	std::string name;
	std::uint32_t version{1};
	EventVisibility visibility{EventVisibility::Simulation};
};

template <class T> inline const char eventTypeTag = 0;

/** A typed persistent reference to one event registration. */
template <Event T> class EventToken final {
  public:
	/** Returns the persistent identity of this event type. */
	[[nodiscard]] constexpr TypeKey key() const noexcept { return key_; }

	/** Returns whether this token came from a successful registration. */
	[[nodiscard]] constexpr explicit operator bool() const noexcept {
		return typeTag_ != nullptr;
	}

  private:
	constexpr EventToken(TypeKey key, const void *typeTag) noexcept
	    : key_(key), typeTag_(typeTag) {}

	TypeKey key_{};
	const void *typeTag_{};

	friend class EventStream;
	friend class World;
	friend class Commands;
	friend class ::shiki::GameDefinition;
};

/** Identifies one immutable event in deterministic publication order. */
struct EventHeader final {
	Tick tick{};
	std::uint32_t sequence{};
	TypeKey type{};
};

} // namespace shiki::game

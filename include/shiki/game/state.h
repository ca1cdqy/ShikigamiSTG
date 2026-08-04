#pragma once

#include <shiki/core/result.h>
#include <shiki/game/type_id.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace shiki {
class GameDefinition;
}

namespace shiki::game {

class Commands;
class World;
class WorldView;

/** Marks a movable value that can be stored once per Session. */
template <class T>
concept State = std::movable<T> && std::destructible<T>;

/** Defines whether a State value participates in deterministic persistence. */
enum class StateFlags : std::uint32_t {
	Deterministic = 0,
	Transient = 1U << 0U
};

/** Encodes one State value for snapshots, replay, and state hashing. */
template <State T> struct StateCodec final {
	std::string identity;
	std::function<Result<std::vector<std::byte>>(const T &)> encode;
	std::function<Result<T>(std::span<const std::byte>)> decode;
};

/** Describes one typed Session State entry before definition freeze. */
template <State T> struct StateDescriptor final {
	std::string name;
	std::uint32_t version{1};
	StateFlags flags{StateFlags::Deterministic};
	T initialValue{};
	StateCodec<T> codec;
};

template <class T> inline const char stateTypeTag = 0;

/** A typed persistent key for one Session State entry. */
template <State T> class StateKey final {
  public:
	/** Returns the persistent identity included in the game schema. */
	[[nodiscard]] constexpr TypeKey key() const noexcept { return key_; }
	[[nodiscard]] constexpr explicit operator bool() const noexcept {
		return typeTag_ != nullptr;
	}

  private:
	constexpr StateKey(TypeKey key, const void *typeTag) noexcept
	    : key_(key), typeTag_(typeTag) {}
	TypeKey key_{};
	const void *typeTag_{};
	friend class Commands;
	friend class World;
	friend class WorldView;
	friend class ::shiki::GameDefinition;
};

/** Stable failures produced by State registration and persistence. */
enum class StateError : std::uint32_t {
	EmptyName = 1,
	DuplicateType,
	InvalidCodec,
	RegistryLocked,
	TypeMismatch,
	EncodeFailed
};

} // namespace shiki::game

#pragma once

#include <compare>
#include <cstdint>
#include <limits>
#include <string_view>

namespace shiki::game {

inline constexpr std::uint64_t fnv1a64OffsetBasis = 14695981039346656037ULL;
inline constexpr std::uint64_t fnv1a64Prime = 1099511628211ULL;

/** Persistent identity derived from an explicitly registered type name. */
struct TypeKey final {
	std::uint64_t value{};

	auto operator<=>(const TypeKey &) const = default;
};

/** Dense identity assigned when a type registry is frozen. */
struct TypeIndex final {
	std::uint32_t value{std::numeric_limits<std::uint32_t>::max()};

	auto operator<=>(const TypeIndex &) const = default;
	[[nodiscard]] constexpr bool isValid() const noexcept {
		return value != std::numeric_limits<std::uint32_t>::max();
	}
};

/** Identifies an immutable schema assembled from registered types. */
struct SchemaDigest final {
	std::uint64_t value{fnv1a64OffsetBasis};

	auto operator<=>(const SchemaDigest &) const = default;
};

/** Separates identities belonging to different public registries. */
enum class TypeDomain : std::uint8_t {
	Component,      ///< Entity component types.
	Event,          ///< Simulation and presentation event types.
	Intent,         ///< Controller intent types.
	Action,         ///< Stage program action types.
	State,          ///< Typed session state entries.
	Asset,          ///< Asset payload types.
	System,         ///< Deterministically scheduled system identities.
	Actor,          ///< Registered object-oriented actor behavior identities.
	Controller,     ///< Registered normalized actor input sources.
	Pattern,        ///< Registered deterministic projectile/pattern producers.
	Condition,      ///< Registered read-only Flow predicates.
	ExternalCommand ///< Deterministic commands accepted from InputFrame.
};

/** A persistent type identity qualified by its registry domain. */
struct QualifiedTypeKey final {
	TypeDomain domain{};
	TypeKey key{};

	auto operator<=>(const QualifiedTypeKey &) const = default;
};

/** Computes the stable FNV-1a 64 identity for a canonical UTF-8 name. */
[[nodiscard]] constexpr TypeKey
typeKeyFromName(std::string_view name) noexcept {
	std::uint64_t hash = fnv1a64OffsetBasis;
	for (const char character : name) {
		hash ^= static_cast<std::uint8_t>(character);
		hash *= fnv1a64Prime;
	}
	return TypeKey{hash};
}

/** A typed reference to a frozen registry entry. */
template <class T> class TypeToken final {
  public:
	constexpr TypeToken(TypeKey persistentKey, TypeIndex runtimeIndex) noexcept
	    : key_(persistentKey), index_(runtimeIndex) {}

	/** Returns the persistent type identity. */
	[[nodiscard]] constexpr TypeKey key() const noexcept { return key_; }

	/** Returns the dense registry identity. */
	[[nodiscard]] constexpr TypeIndex index() const noexcept { return index_; }

  private:
	TypeKey key_{};
	TypeIndex index_{};
};

} // namespace shiki::game

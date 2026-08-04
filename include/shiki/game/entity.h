#pragma once

#include <cstdint>

namespace shiki::game {

class World;
class WorldView;

/** Identifies one World instance within the current process. */
struct WorldId final {
	std::uint32_t value{};

	auto operator<=>(const WorldId &) const = default;
};

/** Identifies a deterministic command producer within a World. */
struct ProducerId final {
	std::uint32_t value{};

	auto operator<=>(const ProducerId &) const = default;
};

/** Describes the observable lifecycle state of an entity handle. */
enum class EntityState : std::uint8_t {
	Pending,        ///< Reserved but not visible to queries or presentation.
	Alive,          ///< Active in the current World.
	DestroyPending, ///< Scheduled for removal at the current commit point.
	Dead,           ///< No longer owned by its World.
	Foreign         ///< Belongs to another World.
};

/** A stable entity identity that does not expose physical ECS storage. */
class EntityHandle final {
  public:
	constexpr EntityHandle() noexcept = default;

	/** Returns whether this is not the null handle. */
	[[nodiscard]] constexpr explicit operator bool() const noexcept {
		return slot_ != 0 || generation_ != 0 || producer_ != 0 ||
		       world_.value != 0;
	}

	[[nodiscard]] constexpr bool
	operator==(const EntityHandle &) const noexcept = default;

  private:
	constexpr EntityHandle(std::uint32_t slot, std::uint32_t generation,
	                       ProducerId producer, WorldId world) noexcept
	    : slot_(slot), generation_(generation), producer_(producer.value),
	      world_(world) {}

	std::uint32_t slot_{};
	std::uint32_t generation_{};
	std::uint32_t producer_{};
	WorldId world_{};

	friend class World;
	friend class WorldView;
};

inline constexpr EntityHandle nullEntity{};

static_assert(sizeof(EntityHandle) == 16);

} // namespace shiki::game

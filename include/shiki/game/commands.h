#pragma once

#include <shiki/core/result.h>
#include <shiki/game/component.h>
#include <shiki/game/entity.h>
#include <shiki/game/event.h>
#include <shiki/game/state.h>

#include <cstddef>
#include <cstdint>
#include <tuple>
#include <vector>

namespace shiki::game {

class World;

/** Identifies one structural command commit point. */
enum class CommitPhase : std::uint8_t {
	Flow,       ///< Makes entities visible to simulation and collision.
	Simulation, ///< Makes entities visible to collision only this tick.
	Resolution  ///< Defers new entity activation until the next tick.
};

/** Defines the stable source of one command buffer. */
struct CommandSource final {
	ProducerId producer{};
	std::uint16_t system{};
	std::uint16_t partition{};
	std::uint16_t buffer{};
};

/** Reports whether a structural command was accepted. */
enum class CommandStatus : std::uint8_t {
	Accepted,      ///< The command was recorded.
	Expired,       ///< The command buffer no longer belongs to the open phase.
	InvalidHandle, ///< The null handle was supplied.
	ForeignWorld,  ///< The handle belongs to another World.
	EntityDead,    ///< The entity has already been removed.
	InvalidComponent, ///< The component token is not registered with this
	                  ///< World.
	InvalidState,     ///< The State key is not registered with this World.
	SequenceLimit     ///< The command buffer exhausted its sequence space.
};

/** Records deterministic structural changes for one system partition. */
class Commands final {
  public:
	Commands(const Commands &) = delete;
	Commands &operator=(const Commands &) = delete;
	Commands(Commands &&) noexcept = default;
	Commands &operator=(Commands &&) noexcept = default;

	/** Reserves a stable pending entity handle. */
	[[nodiscard]] Result<EntityHandle> spawn();

	/**
	 * Records a homogeneous entity batch without per-entity command allocation.
	 *
	 * Individual handles are intentionally not materialized. Component values
	 * are moved into the command buffer and become visible at the phase commit.
	 */
	template <Component... Components>
	[[nodiscard]] Result<std::size_t>
	spawnBatch(std::tuple<ComponentToken<Components>...> tokens,
	           std::vector<std::tuple<Components...>> records);

	/** Schedules an entity for removal at the current commit point. */
	[[nodiscard]] CommandStatus destroy(EntityHandle entity);

	/** Records a component replacement for an entity. */
	template <Component T>
	[[nodiscard]] CommandStatus set(EntityHandle entity,
	                                ComponentToken<T> token, T value);

	/** Records removal of a component from an entity. */
	template <Component T>
	[[nodiscard]] CommandStatus remove(EntityHandle entity,
	                                   ComponentToken<T> token);

	/** Records a typed immutable event for publication at the phase barrier. */
	template <Event T>
	[[nodiscard]] CommandStatus emit(EventToken<T> token, T value);

	/** Records a typed Session State replacement at the current commit point.
	 */
	template <State T>
	[[nodiscard]] CommandStatus set(StateKey<T> key, T value);

  private:
	Commands(World &world, CommitPhase phase, CommandSource source,
	         std::uint64_t epoch) noexcept;

	World *world_{};
	CommitPhase phase_{};
	CommandSource source_{};
	std::uint64_t epoch_{};
	std::uint32_t sequence_{};

	friend class World;
};

} // namespace shiki::game

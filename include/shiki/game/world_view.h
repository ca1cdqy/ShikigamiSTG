#pragma once

#include <shiki/game/component.h>
#include <shiki/game/entity.h>
#include <shiki/game/event_stream.h>
#include <shiki/game/query.h>
#include <shiki/game/state.h>

#include <cstddef>

namespace shiki::game {

class World;
template <Component... Components> class QueryView;

/** Provides read-only access to committed and pending entity lifecycles. */
class WorldView final {
  public:
	/** Returns the current state of an entity handle. */
	[[nodiscard]] EntityState state(EntityHandle entity) const noexcept;

	/** Returns the number of entities active in simulation. */
	[[nodiscard]] std::size_t aliveCount() const noexcept;

	/** Returns the number of reserved entities awaiting activation. */
	[[nodiscard]] std::size_t pendingCount() const noexcept;

	/** Returns a component pointer when the entity is active and has it. */
	template <Component T>
	[[nodiscard]] const T *tryGet(EntityHandle entity,
	                              ComponentToken<T> token) const noexcept;

	/** Returns whether an active entity contains the requested component. */
	template <Component T>
	[[nodiscard]] bool contains(EntityHandle entity,
	                            ComponentToken<T> token) const noexcept;

	/** Returns all active entities containing every requested component. */
	template <Component... Components>
	[[nodiscard]] QueryView<Components...>
	query(ComponentToken<Components>... tokens) const;

	/** Returns matching entities using the requested deterministic order. */
	template <Component... Components>
	[[nodiscard]] QueryView<Components...>
	query(QueryOrder order, ComponentToken<Components>... tokens) const;

	/** Returns a registered immutable Session State value. */
	template <State T>
	[[nodiscard]] const T *tryGet(StateKey<T> key) const noexcept;

	/** Returns immutable events published by completed system barriers. */
	[[nodiscard]] const EventStream &events() const noexcept;

  private:
	explicit WorldView(const World &world) noexcept : world_(&world) {}

	const World *world_{};

	friend class World;
};

} // namespace shiki::game

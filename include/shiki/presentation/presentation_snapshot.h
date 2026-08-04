#pragma once

#include <shiki/core/time.h>
#include <shiki/game/component.h>
#include <shiki/game/entity.h>
#include <shiki/game/query.h>

#include <algorithm>
#include <cstddef>
#include <map>
#include <memory>
#include <tuple>
#include <vector>

namespace shiki::game {
class World;
}

namespace shiki::presentation {

template <game::Component... Components> class PresentationQuery;

/** One immutable typed row copied from a completed simulation tick. */
template <game::Component... Components> class PresentationEntry final {
  public:
	/** Returns the stable entity represented by this snapshot row. */
	[[nodiscard]] game::EntityHandle entity() const noexcept { return entity_; }

	/** Returns one copied observable component. */
	template <game::Component T> [[nodiscard]] const T &get() const noexcept {
		return *std::get<const T *>(values_);
	}

  private:
	PresentationEntry(game::EntityHandle entity,
	                  std::tuple<const Components *...> values)
	    : entity_(entity), values_(std::move(values)) {}
	game::EntityHandle entity_{};
	std::tuple<const Components *...> values_;
	template <game::Component...> friend class PresentationQuery;
};

/** A deterministic materialized query over one immutable snapshot. */
template <game::Component... Components> class PresentationQuery final {
  public:
	using value_type = PresentationEntry<Components...>;
	/** Returns deterministic matching rows. */
	[[nodiscard]] auto begin() const noexcept { return entries_.begin(); }
	/** Returns the end of deterministic matching rows. */
	[[nodiscard]] auto end() const noexcept { return entries_.end(); }
	/** Returns the exact matching row count. */
	[[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
	/** Returns whether no entities matched. */
	[[nodiscard]] bool empty() const noexcept { return entries_.empty(); }

  private:
	std::vector<value_type> entries_;
	friend class PresentationSnapshot;
};

/** Owns immutable copies of explicitly observable World components. */
class PresentationSnapshot final {
  public:
	/** Returns the completed simulation tick represented by this snapshot. */
	[[nodiscard]] Tick tick() const noexcept { return tick_; }

	/** Returns the number of observable entities copied into this snapshot. */
	[[nodiscard]] std::size_t entityCount() const noexcept {
		return entities_.size();
	}

	/** Returns one copied component or null when it was not observable. */
	template <game::Component T>
	[[nodiscard]] const T *tryGet(game::EntityHandle entity,
	                              game::ComponentToken<T> token) const noexcept;

	/** Materializes an immutable typed query in deterministic order. */
	template <game::Component... Components>
	[[nodiscard]] PresentationQuery<Components...>
	query(game::QueryOrder order,
	      game::ComponentToken<Components>... tokens) const;

	/** Materializes an immutable typed query in snapshot storage order. */
	template <game::Component... Components>
	[[nodiscard]] PresentationQuery<Components...>
	query(game::ComponentToken<Components>... tokens) const {
		return query(game::QueryOrder::Storage, tokens...);
	}

  private:
	struct Value final {
		const void *typeTag{};
		std::shared_ptr<const void> data;
	};
	struct Entity final {
		game::EntityHandle handle{};
		std::tuple<std::uint32_t, std::uint32_t, std::uint32_t> stableId{};
		std::map<game::TypeKey, Value> components;
	};

	Tick tick_{};
	std::vector<Entity> entities_;
	friend class game::World;
};

template <game::Component T>
const T *
PresentationSnapshot::tryGet(game::EntityHandle entity,
                             game::ComponentToken<T> token) const noexcept {
	const auto row = std::ranges::find(entities_, entity, &Entity::handle);
	if (row == entities_.end())
		return nullptr;
	const auto value = row->components.find(token.key());
	if (value == row->components.end() ||
	    value->second.typeTag != &game::componentTypeTag<T>)
		return nullptr;
	return static_cast<const T *>(value->second.data.get());
}

template <game::Component... Components>
PresentationQuery<Components...>
PresentationSnapshot::query(game::QueryOrder order,
                            game::ComponentToken<Components>... tokens) const {
	PresentationQuery<Components...> result;
	std::vector<const Entity *> ordered;
	ordered.reserve(entities_.size());
	for (const Entity &entity : entities_)
		ordered.push_back(&entity);
	if (order == game::QueryOrder::EntityId) {
		std::ranges::sort(
		    ordered, {}, [](const Entity *entity) { return entity->stableId; });
	}
	for (const Entity *entity : ordered) {
		if (((tryGet(entity->handle, tokens) != nullptr) && ...)) {
			result.entries_.push_back(PresentationEntry<Components...>{
			    entity->handle, std::tuple{tryGet(entity->handle, tokens)...}});
		}
	}
	return result;
}

} // namespace shiki::presentation

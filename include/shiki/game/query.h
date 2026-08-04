#pragma once

#include <shiki/game/component.h>
#include <shiki/game/entity.h>

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <tuple>
#include <utility>
#include <vector>

namespace shiki::game {

class World;
class WorldView;
template <Component... Components> class QueryView;

/** Selects the deterministic ordering used to materialize a query. */
enum class QueryOrder : std::uint8_t {
	Storage, ///< Uses archetype, chunk, and row order without sorting.
	EntityId ///< Sorts results by stable producer, generation, and slot
	         ///< identity.
};

/** One read-only row returned by a typed entity query. */
template <Component... Components> class QueryEntry final {
  public:
	/** Returns the entity matched by this query row. */
	[[nodiscard]] EntityHandle entity() const noexcept { return entity_; }

	/** Returns one component reference from this query row. */
	template <Component T> [[nodiscard]] const T &get() const noexcept {
		return *std::get<const T *>(values_);
	}

  private:
	QueryEntry(EntityHandle entity, std::tuple<const Components *...> values)
	    : entity_(entity), values_(std::move(values)) {}

	EntityHandle entity_{};
	std::tuple<const Components *...> values_;

	template <Component...> friend class QueryView;
};

/** Provides a deterministic, read-only collection of matching entities. */
template <Component... Components> class QueryView final {
  public:
	/** Forward iterator over query rows. */
	class Iterator final {
	  public:
		using value_type = QueryEntry<Components...>;
		using difference_type = std::ptrdiff_t;
		using iterator_category = std::forward_iterator_tag;

		[[nodiscard]] value_type operator*() const {
			return view_->entryAt(index_);
		}

		Iterator &operator++() noexcept {
			++index_;
			return *this;
		}

		Iterator operator++(int) noexcept {
			Iterator copy = *this;
			++*this;
			return copy;
		}

		[[nodiscard]] bool
		operator==(const Iterator &) const noexcept = default;

	  private:
		Iterator(const QueryView *view, std::size_t index) noexcept
		    : view_(view), index_(index) {}

		const QueryView *view_{};
		std::size_t index_{};

		friend class QueryView;
	};

	/** Returns whether no structural commit invalidated this view. */
	[[nodiscard]] bool isValid() const noexcept;

	/** Returns the number of matching entities captured by this view. */
	[[nodiscard]] std::size_t size() const noexcept { return entities_.size(); }

	/** Returns the currently materialized result count. */
	[[nodiscard]] std::size_t estimateSize() const noexcept {
		return entities_.size();
	}

	/** Returns an iterator over the first matching entity. */
	[[nodiscard]] Iterator begin() const noexcept { return Iterator{this, 0}; }

	/** Returns the end iterator. */
	[[nodiscard]] Iterator end() const noexcept {
		return Iterator{this, entities_.size()};
	}

  private:
	QueryView(const World &world, std::uint64_t structuralVersion,
	          std::tuple<ComponentToken<Components>...> tokens,
	          std::vector<EntityHandle> entities)
	    : world_(&world), structuralVersion_(structuralVersion),
	      tokens_(std::move(tokens)), entities_(std::move(entities)) {}

	[[nodiscard]] QueryEntry<Components...> entryAt(std::size_t index) const;

	const World *world_{};
	std::uint64_t structuralVersion_{};
	std::tuple<ComponentToken<Components>...> tokens_;
	std::vector<EntityHandle> entities_;

	friend class WorldView;
};

} // namespace shiki::game

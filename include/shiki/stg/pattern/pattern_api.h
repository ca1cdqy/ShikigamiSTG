#pragma once

#include <shiki/stg/pattern/pattern.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace shiki::stg {

/** Identifies one scheduled live Pattern in a Session. */
struct PatternInstanceId final {
	std::uint64_t value{};
	auto operator<=>(const PatternInstanceId &) const = default;
};

class PatternPool;

/** Starts typed Pattern invocations through one GameplayContext. */
class PatternApi final {
  public:
	/** Starts a pattern now and advances it once in the current phase. */
	template <PatternArguments T>
	[[nodiscard]] Result<PatternInstanceId> emit(PatternHandle<T> pattern,
	                                             T arguments);

	/** Queues a pattern for the next Session Pattern dispatch. */
	template <PatternArguments T>
	[[nodiscard]] Result<PatternInstanceId> schedule(PatternHandle<T> pattern,
	                                                 T arguments);

  private:
	PatternApi(const PatternRegistry *registry, PatternPool *pool,
	           GameplayContext &game) noexcept
	    : registry_(registry), pool_(pool), game_(&game) {}
	const PatternRegistry *registry_{};
	PatternPool *pool_{};
	GameplayContext *game_{};
	friend class GameplayContext;
};

/** Owns active cross-tick Pattern instances for one Session. */
class PatternPool final {
  public:
	/** Advances scheduled and active patterns in stable creation order. */
	[[nodiscard]] Result<void> dispatch(GameplayContext &game);

  private:
	struct Entry final {
		PatternInstanceId id{};
		std::unique_ptr<Pattern> pattern;
	};
	[[nodiscard]] Result<PatternInstanceId>
	emit(const PatternRegistry &registry, PatternInvocation invocation,
	     GameplayContext &game);
	[[nodiscard]] Result<PatternInstanceId>
	schedule(const PatternRegistry &registry, PatternInvocation invocation);
	[[nodiscard]] Result<PatternInstanceId> allocateId();
	std::vector<Entry> pending_;
	std::vector<Entry> active_;
	std::uint64_t nextId_{1};
	friend class PatternApi;
};

template <PatternArguments T>
Result<PatternInstanceId> PatternApi::emit(PatternHandle<T> pattern,
                                           T arguments) {
	return pool_->emit(*registry_,
	                   PatternInvocation::create(pattern, std::move(arguments)),
	                   *game_);
}

template <PatternArguments T>
Result<PatternInstanceId> PatternApi::schedule(PatternHandle<T> pattern,
                                               T arguments) {
	return pool_->schedule(
	    *registry_, PatternInvocation::create(pattern, std::move(arguments)));
}

} // namespace shiki::stg

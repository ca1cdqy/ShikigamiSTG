#include <shiki/stg/pattern/pattern_api.h>

#include <shiki/stg/gameplay_context.h>

#include <algorithm>

namespace shiki::stg {

Result<PatternInstanceId> PatternPool::allocateId() {
	if (nextId_ == 0)
		return std::unexpected(
		    Error{ErrorDomain::Core,
		          static_cast<std::uint32_t>(PatternError::InstanceExhausted),
		          "Pattern instance identity space is exhausted"});
	return PatternInstanceId{nextId_++};
}

Result<PatternInstanceId> PatternPool::emit(const PatternRegistry &registry,
                                            PatternInvocation invocation,
                                            GameplayContext &game) {
	auto id = allocateId();
	if (!id)
		return std::unexpected(id.error());
	auto pattern = registry.create(invocation);
	if (!pattern)
		return std::unexpected(pattern.error());
	auto status = (*pattern)->tick(game);
	if (!status)
		return std::unexpected(status.error());
	if (*status == PatternStatus::Running)
		active_.push_back(Entry{*id, std::move(*pattern)});
	return id;
}

Result<PatternInstanceId> PatternPool::schedule(const PatternRegistry &registry,
                                                PatternInvocation invocation) {
	auto id = allocateId();
	if (!id)
		return std::unexpected(id.error());
	auto pattern = registry.create(invocation);
	if (!pattern)
		return std::unexpected(pattern.error());
	pending_.push_back(Entry{*id, std::move(*pattern)});
	return id;
}

Result<void> PatternPool::dispatch(GameplayContext &game) {
	for (auto &entry : pending_)
		active_.push_back(std::move(entry));
	pending_.clear();
	for (auto &entry : active_) {
		auto status = entry.pattern->tick(game);
		if (!status)
			return std::unexpected(status.error());
		if (*status == PatternStatus::Complete)
			entry.pattern.reset();
	}
	std::erase_if(active_,
	              [](const Entry &entry) { return entry.pattern == nullptr; });
	return {};
}

} // namespace shiki::stg

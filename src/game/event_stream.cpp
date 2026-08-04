#include <shiki/game/event_stream.h>

#include <algorithm>

namespace shiki::game {

Result<void> EventStream::setRetentionTicks(std::size_t ticks) {
	if (locked_) {
		return std::unexpected(Error{
		    ErrorDomain::Definition,
		    static_cast<std::uint32_t>(EventStreamError::RegistrationLocked),
		    "Event retention must be configured before the first tick"});
	}
	if (ticks == 0) {
		return std::unexpected(
		    Error{ErrorDomain::Definition,
		          static_cast<std::uint32_t>(EventStreamError::CursorExpired),
		          "Event retention must keep at least one tick"});
	}
	retentionTicks_ = ticks;
	return {};
}

void EventStream::beginTick(Tick tick) {
	locked_ = true;
	if (!pages_.empty() && pages_.back().tick == tick)
		return;
	pages_.push_back(Page{tick, {}});
	trim();
}

const EventStream::TypeEntry *EventStream::find(TypeKey key) const noexcept {
	const auto entry = std::ranges::find(types_, key, &TypeEntry::key);
	return entry == types_.end() ? nullptr : &*entry;
}

const EventStream::Page *EventStream::findPage(Tick tick) const noexcept {
	const auto page = std::ranges::find(pages_, tick, &Page::tick);
	return page == pages_.end() ? nullptr : &*page;
}

void EventStream::trim() {
	while (pages_.size() > retentionTicks_)
		pages_.pop_front();
}

} // namespace shiki::game

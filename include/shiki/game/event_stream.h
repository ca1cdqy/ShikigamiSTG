#pragma once

#include <shiki/core/result.h>
#include <shiki/game/event.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace shiki::game {

/** Stable failures produced by event registration and retained-page access. */
enum class EventStreamError : std::uint32_t {
	EmptyName = 1,
	TypeConflict,
	RegistrationLocked,
	CursorExpired,
	EventNotVisible
};

/** References one retained typed event without copying its payload. */
template <Event T> struct EventReference final {
	EventHeader header{};
	const T &value;
};

/** Owns a temporary list of references into retained immutable event pages. */
template <Event T> class EventBatch final {
  public:
	/** Returns the number of matching retained events. */
	[[nodiscard]] std::size_t size() const noexcept { return events_.size(); }

	/** Returns whether no matching events were published. */
	[[nodiscard]] bool empty() const noexcept { return events_.empty(); }

	/** Provides deterministic publication-order iteration. */
	[[nodiscard]] auto begin() const noexcept { return events_.begin(); }

	/** Provides deterministic publication-order iteration. */
	[[nodiscard]] auto end() const noexcept { return events_.end(); }

  private:
	std::vector<EventReference<T>> events_;
	friend class EventStream;
};

/** Stores immutable typed events in bounded per-tick pages. */
class EventStream final {
  public:
	EventStream() = default;
	~EventStream() = default;
	EventStream(const EventStream &) = delete;
	EventStream &operator=(const EventStream &) = delete;

	/** Registers one event type before the first event is published. */
	template <Event T>
	[[nodiscard]] Result<EventToken<T>>
	registerEvent(EventDescriptor<T> descriptor);

	/** Sets the number of completed tick pages retained for consumers. */
	[[nodiscard]] Result<void> setRetentionTicks(std::size_t ticks);

	/** Returns typed events from one retained tick in publication order. */
	template <Event T>
	[[nodiscard]] Result<EventBatch<T>> events(EventToken<T> token,
	                                           Tick tick) const;

	/** Returns one retained event batch only when exported to presentation. */
	template <Event T>
	[[nodiscard]] Result<EventBatch<T>> presentationEvents(EventToken<T> token,
	                                                       Tick tick) const;

	/** Returns typed events published so far in the current tick. */
	template <Event T>
	[[nodiscard]] EventBatch<T> current(EventToken<T> token) const;

  private:
	class Value {
	  public:
		virtual ~Value() = default;
		[[nodiscard]] virtual const void *typeTag() const noexcept = 0;
		[[nodiscard]] virtual const void *data() const noexcept = 0;
	};

	template <Event T> class TypedValue final : public Value {
	  public:
		explicit TypedValue(T value) : value_(std::move(value)) {}
		[[nodiscard]] const void *typeTag() const noexcept override {
			return &eventTypeTag<T>;
		}
		[[nodiscard]] const void *data() const noexcept override {
			return &value_;
		}

	  private:
		T value_;
	};

	struct TypeEntry final {
		TypeKey key{};
		std::string name;
		std::uint32_t version{};
		EventVisibility visibility{};
		const void *typeTag{};
	};

	struct Record final {
		EventHeader header{};
		std::unique_ptr<Value> value;
	};

	struct Page final {
		Tick tick{};
		std::vector<Record> records;
	};

	void beginTick(Tick tick);
	template <Event T>
	void publish(EventToken<T> token, EventHeader header, T value);
	[[nodiscard]] const TypeEntry *find(TypeKey key) const noexcept;
	[[nodiscard]] const Page *findPage(Tick tick) const noexcept;
	void trim();

	std::vector<TypeEntry> types_;
	std::deque<Page> pages_;
	std::size_t retentionTicks_{4};
	bool locked_{};

	friend class World;
};

template <Event T>
Result<EventToken<T>>
EventStream::registerEvent(EventDescriptor<T> descriptor) {
	if (locked_) {
		return std::unexpected(Error{
		    ErrorDomain::Definition,
		    static_cast<std::uint32_t>(EventStreamError::RegistrationLocked),
		    "Events must be registered before the first tick"});
	}
	if (descriptor.name.empty()) {
		return std::unexpected(
		    Error{ErrorDomain::Definition,
		          static_cast<std::uint32_t>(EventStreamError::EmptyName),
		          "Event name cannot be empty"});
	}
	const TypeKey key = typeKeyFromName(descriptor.name);
	if (const TypeEntry *existing = find(key); existing != nullptr) {
		if (existing->typeTag == &eventTypeTag<T> &&
		    existing->name == descriptor.name &&
		    existing->version == descriptor.version &&
		    existing->visibility == descriptor.visibility) {
			return EventToken<T>{key, &eventTypeTag<T>};
		}
		return std::unexpected(Error{
		    ErrorDomain::Definition,
		    static_cast<std::uint32_t>(EventStreamError::TypeConflict),
		    "Event key is already registered with incompatible metadata"});
	}
	types_.push_back(TypeEntry{key, std::move(descriptor.name),
	                           descriptor.version, descriptor.visibility,
	                           &eventTypeTag<T>});
	return EventToken<T>{key, &eventTypeTag<T>};
}

template <Event T>
Result<EventBatch<T>> EventStream::events(EventToken<T> token,
                                          Tick tick) const {
	const TypeEntry *type = find(token.key_);
	if (!token || type == nullptr || type->typeTag != token.typeTag_) {
		return std::unexpected(
		    Error{ErrorDomain::Definition,
		          static_cast<std::uint32_t>(EventStreamError::TypeConflict),
		          "Event token is not registered with this stream"});
	}
	const Page *page = findPage(tick);
	if (page == nullptr) {
		return std::unexpected(
		    Error{ErrorDomain::Core,
		          static_cast<std::uint32_t>(EventStreamError::CursorExpired),
		          "Requested event tick is outside the retained window"});
	}
	EventBatch<T> result;
	for (const Record &record : page->records) {
		if (record.header.type == token.key_) {
			result.events_.push_back(EventReference<T>{
			    record.header, *static_cast<const T *>(record.value->data())});
		}
	}
	return result;
}

template <Event T>
Result<EventBatch<T>> EventStream::presentationEvents(EventToken<T> token,
                                                      Tick tick) const {
	const TypeEntry *type = find(token.key_);
	if (!token || type == nullptr || type->typeTag != token.typeTag_) {
		return std::unexpected(
		    Error{ErrorDomain::Definition,
		          static_cast<std::uint32_t>(EventStreamError::TypeConflict),
		          "Event token is not registered with this stream"});
	}
	if (type->visibility != EventVisibility::SimulationAndPresentation) {
		return std::unexpected(
		    Error{ErrorDomain::Presentation,
		          static_cast<std::uint32_t>(EventStreamError::EventNotVisible),
		          "Event is not exported to presentation"});
	}
	return events(token, tick);
}

template <Event T>
EventBatch<T> EventStream::current(EventToken<T> token) const {
	EventBatch<T> result;
	if (pages_.empty())
		return result;
	const TypeEntry *type = find(token.key_);
	if (!token || type == nullptr || type->typeTag != token.typeTag_)
		return result;
	for (const Record &record : pages_.back().records) {
		if (record.header.type == token.key_) {
			result.events_.push_back(EventReference<T>{
			    record.header, *static_cast<const T *>(record.value->data())});
		}
	}
	return result;
}

template <Event T>
void EventStream::publish(EventToken<T> token, EventHeader header, T value) {
	if (pages_.empty() || pages_.back().tick != header.tick)
		beginTick(header.tick);
	header.type = token.key_;
	pages_.back().records.push_back(
	    Record{header, std::make_unique<TypedValue<T>>(std::move(value))});
}

} // namespace shiki::game

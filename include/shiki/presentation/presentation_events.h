#pragma once

#include <shiki/game/event_stream.h>

namespace shiki {
class Session;
}

namespace shiki::presentation {

/** Read-only access to events explicitly exported by deterministic gameplay. */
class PresentationEvents final {
  public:
	/** Returns exported events from one retained completed tick. */
	template <game::Event T>
	[[nodiscard]] Result<game::EventBatch<T>> events(game::EventToken<T> token,
	                                                 Tick tick) const {
		return stream_->presentationEvents(token, tick);
	}

  private:
	explicit PresentationEvents(const game::EventStream &stream) noexcept
	    : stream_(&stream) {}
	const game::EventStream *stream_{};
	friend class ::shiki::Session;
};

} // namespace shiki::presentation

#pragma once

#include <shiki/core/result.h>
#include <shiki/game_definition.h>
#include <shiki/session.h>

#include <memory>

namespace shiki {

/** Configures process-wide services without adding gameplay state. */
struct RuntimeConfig final {};

/** Creates independent Sessions and will own shared platform services. */
class Runtime final {
  public:
	explicit Runtime(RuntimeConfig config = {}) noexcept : config_(config) {}

	/** Creates one deterministic Session from a movable definition builder. */
	[[nodiscard]] Result<std::unique_ptr<Session>>
	createSession(GameDefinition definition, SessionConfig config);

  private:
	RuntimeConfig config_;
};

} // namespace shiki

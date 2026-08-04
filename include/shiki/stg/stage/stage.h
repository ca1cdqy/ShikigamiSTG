#pragma once

#include <shiki/stg/gameplay_context.h>

namespace shiki::stg {

/**
 * Optional object-oriented facade for procedural stage systems.
 *
 * Stage owns no actors, renderer, audio backend, or resource manager. User
 * code calls ActorApi, ProjectileApi, PatternApi, and FlowApi from the supplied
 * short-lived GameplayContext.
 */
class Stage {
  public:
	Stage() = default;
	virtual ~Stage() = default;
	Stage(const Stage &) = delete;
	Stage &operator=(const Stage &) = delete;
	Stage(Stage &&) = delete;
	Stage &operator=(Stage &&) = delete;

	/** Handles deterministic stage activation. */
	virtual void onEnter(GameplayContext &game) = 0;
	/** Advances procedural stage behavior by one fixed tick. */
	virtual void onTick(GameplayContext &game) = 0;
	/** Handles deterministic stage completion or Session shutdown. */
	virtual void onExit(GameplayContext &game) = 0;
};

} // namespace shiki::stg

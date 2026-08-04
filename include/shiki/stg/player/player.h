#pragma once

#include <shiki/stg/actor/actor_behavior.h>

#include <optional>

namespace shiki::stg {

/** Mutable defaults configured by the object-oriented player facade. */
struct PlayerConfig final {
	FactionId faction{1};
	float collisionRadius{2.0F};
	std::int64_t health{1};
	std::optional<control::ControllerTypeId> controller;
};

/** Convenience Actor base for a user-controlled gameplay entity. */
class Player : public Actor {
  public:
	/** Applies PlayerConfig to the neutral ActorBuilder. */
	void configure(ActorBuilder &builder) final;
	/** Dispatches the first visible tick to onPlayerSpawn. */
	void onSpawn(GameplayContext &game) final;
	/** Dispatches each fixed simulation tick to onPlayerTick. */
	void onTick(GameplayContext &game) final;
	/** Dispatches actor removal to onPlayerDespawn. */
	void onDespawn(GameplayContext &game, DespawnReason reason) final;

  protected:
	/** Configures health, collision, faction, and optional input controller. */
	virtual void configurePlayer(PlayerConfig &config) = 0;
	/** Handles activation after the actor becomes visible. */
	virtual void onPlayerSpawn(GameplayContext &game);
	/** Handles normalized intent and player abilities for one fixed tick. */
	virtual void onPlayerTick(GameplayContext &game) = 0;
	/** Handles deterministic player removal. */
	virtual void onPlayerDespawn(GameplayContext &game, DespawnReason reason);
};

} // namespace shiki::stg

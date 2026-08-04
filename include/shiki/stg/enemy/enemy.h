#pragma once

#include <shiki/stg/actor/actor_behavior.h>

namespace shiki::stg {

/** Mutable defaults configured by the object-oriented enemy facade. */
struct EnemyConfig final {
	FactionId faction{2};
	float collisionRadius{16.0F};
	std::int64_t health{1};
};

/** Convenience Actor base for an AI or stage-controlled gameplay entity. */
class Enemy : public Actor {
  public:
	/** Applies EnemyConfig to the neutral ActorBuilder. */
	void configure(ActorBuilder &builder) final;
	/** Dispatches the first visible tick to onEnemySpawn. */
	void onSpawn(GameplayContext &game) final;
	/** Dispatches each fixed simulation tick to onEnemyTick. */
	void onTick(GameplayContext &game) final;
	/** Dispatches actor removal to onEnemyDespawn. */
	void onDespawn(GameplayContext &game, DespawnReason reason) final;

  protected:
	/** Configures health, collision, and relationship faction. */
	virtual void configureEnemy(EnemyConfig &config) = 0;
	/** Handles activation after the actor becomes visible. */
	virtual void onEnemySpawn(GameplayContext &game);
	/** Handles movement and abilities for one fixed tick. */
	virtual void onEnemyTick(GameplayContext &game) = 0;
	/** Handles deterministic enemy removal. */
	virtual void onEnemyDespawn(GameplayContext &game, DespawnReason reason);
};

/**
 * Semantic convenience facade for a boss actor.
 *
 * Boss uses the same neutral Actor storage, faction rules, controllers, and
 * projectile APIs as Enemy. It only renames the lifecycle customization
 * points so a traditional boss implementation is easy to read.
 */
class Boss : public Enemy {
  protected:
	/** Configures the boss through the ordinary neutral enemy defaults. */
	virtual void configureBoss(EnemyConfig &config) = 0;
	/** Handles boss activation after its entity becomes visible. */
	virtual void onBossSpawn(GameplayContext &game);
	/** Advances boss behavior for one fixed simulation tick. */
	virtual void onBossTick(GameplayContext &game) = 0;
	/** Handles deterministic boss removal. */
	virtual void onBossDespawn(GameplayContext &game, DespawnReason reason);

  private:
	void configureEnemy(EnemyConfig &config) final;
	void onEnemySpawn(GameplayContext &game) final;
	void onEnemyTick(GameplayContext &game) final;
	void onEnemyDespawn(GameplayContext &game, DespawnReason reason) final;
};

} // namespace shiki::stg

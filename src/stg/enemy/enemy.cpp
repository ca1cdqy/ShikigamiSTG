#include <shiki/stg/enemy/enemy.h>

#include <shiki/stg/gameplay_context.h>

namespace shiki::stg {

void Enemy::configure(ActorBuilder &builder) {
	EnemyConfig config;
	configureEnemy(config);
	builder.faction(config.faction)
	    .collisionRadius(config.collisionRadius)
	    .health(config.health);
}

void Enemy::onSpawn(GameplayContext &game) { onEnemySpawn(game); }
void Enemy::onTick(GameplayContext &game) { onEnemyTick(game); }
void Enemy::onDespawn(GameplayContext &game, DespawnReason reason) {
	onEnemyDespawn(game, reason);
}
void Enemy::onEnemySpawn(GameplayContext &) {}
void Enemy::onEnemyDespawn(GameplayContext &, DespawnReason) {}

void Boss::configureEnemy(EnemyConfig &config) { configureBoss(config); }
void Boss::onEnemySpawn(GameplayContext &game) { onBossSpawn(game); }
void Boss::onEnemyTick(GameplayContext &game) { onBossTick(game); }
void Boss::onEnemyDespawn(GameplayContext &game, DespawnReason reason) {
	onBossDespawn(game, reason);
}
void Boss::onBossSpawn(GameplayContext &) {}
void Boss::onBossDespawn(GameplayContext &, DespawnReason) {}

} // namespace shiki::stg

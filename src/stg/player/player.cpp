#include <shiki/stg/player/player.h>

#include <shiki/stg/gameplay_context.h>

namespace shiki::stg {

void Player::configure(ActorBuilder &builder) {
	PlayerConfig config;
	configurePlayer(config);
	builder.faction(config.faction)
	    .collisionRadius(config.collisionRadius)
	    .health(config.health);
	if (config.controller)
		builder.controller(*config.controller);
}

void Player::onSpawn(GameplayContext &game) { onPlayerSpawn(game); }
void Player::onTick(GameplayContext &game) { onPlayerTick(game); }
void Player::onDespawn(GameplayContext &game, DespawnReason reason) {
	onPlayerDespawn(game, reason);
}
void Player::onPlayerSpawn(GameplayContext &) {}
void Player::onPlayerDespawn(GameplayContext &, DespawnReason) {}

} // namespace shiki::stg

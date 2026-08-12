#pragma once

#include <cmath>
#include <numbers>

#include <shiki/stg/gameplay_context.h>
#include <shiki/stg/projectile/projectile.h>

struct WaveParticleState final {
	float angle{std::numbers::pi_v<float> * 0.5F};
	float angularSpeed{};
};

[[nodiscard]] inline shiki::stg::ProjectileSpec
waveParticleSpec(unsigned style) {
	using namespace shiki::stg;
	return {.collisionRadius = 3.0F,
	        .lifetime = {520},
	        .style = {style},
	        .faction = {1},
	        .flags = ProjectileFlags::RotateToVelocity,
	        .damage = 0};
}

[[nodiscard]] inline shiki::stg::ProjectileSpawn waveParticleShot(float angle) {
	return {.position = {{320.0F, 310.0F}},
	        .velocityPerTick = {std::cos(angle) * 2.5F, std::sin(angle) * 2.5F},
	        .orientation = {angle}};
}

inline void emitWaveParticle(shiki::stg::GameplayContext &game,
                             WaveParticleState &wave) {
	wave.angle += wave.angularSpeed;
	wave.angularSpeed += 0.00065449846F;

	for (int port = 0; port < 5; ++port) {
		const float angle =
		    wave.angle + static_cast<float>(port) *
		                     (2.0F * std::numbers::pi_v<float> / 5.0F);
		static_cast<void>(game.projectiles().spawn(
		    waveParticleSpec(static_cast<unsigned>(port & 1)),
		    waveParticleShot(angle)));
	}
}

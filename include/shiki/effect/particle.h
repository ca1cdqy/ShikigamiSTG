#pragma once

#include <memory>
#include <random>
#include <shiki/core/types.h>
#include <string>
#include <vector>

namespace shiki {

/** Mutable presentation state for one particle. */
struct Particle {
	Vec2 position;               ///< Position in presentation coordinates.
	Vec2 velocity;               ///< Velocity in coordinate units per second.
	Vec2 size;                   ///< Unscaled particle width and height.
	float rotation = 0.0f;       ///< Rotation in radians.
	float rotationSpeed = 0.0f;  ///< Angular velocity in radians per second.
	float lifetime = 0.0f;       ///< Elapsed lifetime in seconds.
	float maxLifetime = 1.0f;    ///< Lifetime at which the particle expires.
	uint32_t color = 0xFFFFFFFF; ///< Packed RGBA base color.
	float alpha = 1.0f;          ///< Additional normalized opacity multiplier.
	float scale = 1.0f;          ///< Uniform visual scale.
	bool active = false; ///< Whether update and render passes include it.
};

/** Parameters used to initialize a ParticleEmitter. */
struct ParticleEmitterConfig {
	Vec2 position; ///< Emitter origin in presentation coordinates.
	Vec2 emitDirection = {0.0f, -1.0f}; ///< Center direction for emission.
	float emitAngle =
	    3.14159265359f / 4.0f; ///< Full angular spread in radians.
	float minSpeed = 50.0f;    ///< Minimum initial speed per second.
	float maxSpeed = 150.0f;   ///< Maximum initial speed per second.
	float minSize = 2.0f;      ///< Minimum generated visual size.
	float maxSize = 8.0f;      ///< Maximum generated visual size.
	float minLifetime = 0.5f;  ///< Minimum generated lifetime in seconds.
	float maxLifetime = 2.0f;  ///< Maximum generated lifetime in seconds.
	uint32_t startColor = 0xFFFFFFFF; ///< Packed RGBA color at birth.
	uint32_t endColor = 0x00FFFFFF;   ///< Packed RGBA color at expiration.
	float emissionRate = 100.0f;      ///< Continuous particles per second.
	int maxParticles = 1000; ///< Maximum simultaneously stored particles.
	BlendMode blendMode = BlendMode::Add; ///< Rendering blend mode.
	bool loop = true; ///< Whether continuous emission remains enabled.
};

/**
 * Owns and simulates a bounded pool of presentation-only particles.
 *
 * @determinism Uses an internal random generator and is not part of
 * deterministic gameplay state.
 * @thread_safety Not thread-safe.
 */
class ParticleEmitter {
  public:
	/** Creates an emitter with default-initialized state. */
	ParticleEmitter() = default;
	/** Creates and initializes an emitter from a configuration snapshot. */
	explicit ParticleEmitter(const ParticleEmitterConfig &config);
	/** Releases the particle pool. */
	~ParticleEmitter() = default;

	/** Emitters cannot be copied because they own mutable particle state. */
	ParticleEmitter(const ParticleEmitter &) = delete;
	/** Emitters cannot be copy-assigned. */
	ParticleEmitter &operator=(const ParticleEmitter &) = delete;

	/** Transfers the configuration, random generator, and particle pool. */
	ParticleEmitter(ParticleEmitter &&) noexcept = default;
	/** Replaces this emitter with moved state. */
	ParticleEmitter &operator=(ParticleEmitter &&) noexcept = default;

	/** Resets the emitter from a configuration snapshot. */
	void initialize(const ParticleEmitterConfig &config);
	/** Releases all particles and stops emission. */
	void shutdown();

	/** Advances continuous emission and active particles by seconds. */
	void update(float dt);

	/** Immediately activates up to count available particles. */
	void emit(int count);
	/** Emits count particles as one instantaneous burst. */
	void emitBurst(int count);

	/** Moves the emitter origin without resetting active particles. */
	void setPosition(const Vec2 &pos) { config_.position = pos; }
	/** Sets the continuous emission rate in particles per second. */
	void setEmissionRate(float rate) { config_.emissionRate = rate; }
	/** Sets the particle-pool limit used by subsequent initialization. */
	void setMaxParticles(int max) { config_.maxParticles = max; }

	/** Returns every particle slot, including inactive slots. */
	[[nodiscard]] const std::vector<Particle> &getParticles() const {
		return particles_;
	}
	/** Returns the number of active particle slots. */
	[[nodiscard]] int getActiveCount() const { return activeCount_; }

	/** Enables continuous emission. */
	void start() { isRunning_ = true; }
	/** Suspends continuous emission without removing active particles. */
	void stop() { isRunning_ = false; }
	/** Deactivates particles and resets emission timing. */
	void reset();

  private:
	ParticleEmitterConfig config_;
	std::vector<Particle> particles_;
	int activeCount_ = 0;
	float emissionAccumulator_ = 0.0f;
	bool isRunning_ = true;
	std::mt19937 rng_;

	/// Internal helpers
	void spawnParticle();
	void updateParticle(Particle &particle, float dt);
	[[nodiscard]] float randomFloat(float min, float max);
	[[nodiscard]] uint32_t lerpColor(uint32_t color1, uint32_t color2, float t);
};

/**
 * Owns presentation emitters created through predefined effect factories.
 * Returned emitter pointers remain valid until their effect is removed or the
 * manager is cleared.
 *
 * @thread_safety Not thread-safe.
 */
class EffectManager {
  public:
	/** Creates an empty effect manager. */
	EffectManager() = default;
	/** Destroys all owned emitters. */
	~EffectManager() = default;

	/** Managers cannot be copied because they own emitters. */
	EffectManager(const EffectManager &) = delete;
	/** Managers cannot be copy-assigned. */
	EffectManager &operator=(const EffectManager &) = delete;

	/** Transfers emitter ownership. */
	EffectManager(EffectManager &&) noexcept = default;
	/** Replaces this manager by moving emitter ownership. */
	EffectManager &operator=(EffectManager &&) noexcept = default;

	/** Initializes manager runtime state. */
	void initialize();
	/** Releases every owned emitter. */
	void shutdown();

	/** Creates an explosion emitter at position with a visual scale multiplier.
	 */
	ParticleEmitter *createExplosion(const Vec2 &position, float scale = 1.0f);
	/** Creates a sparkle emitter at position with a visual scale multiplier. */
	ParticleEmitter *createSparkle(const Vec2 &position, float scale = 1.0f);
	/** Creates a smoke emitter at position with a visual scale multiplier. */
	ParticleEmitter *createSmoke(const Vec2 &position, float scale = 1.0f);
	/** Creates a fire emitter at position with a visual scale multiplier. */
	ParticleEmitter *createFire(const Vec2 &position, float scale = 1.0f);
	/** Creates an emitter from config after replacing its origin with position.
	 */
	ParticleEmitter *createCustomEffect(const Vec2 &position,
	                                    const ParticleEmitterConfig &config);

	/** Advances emitters and removes effects that have completed. */
	void update(float dt);

	/** Returns the owned emitters in creation order. */
	[[nodiscard]] const std::vector<std::unique_ptr<ParticleEmitter>> &
	getEmitters() const {
		return emitters_;
	}

	/** Immediately removes and destroys every emitter. */
	void clear();

  private:
	std::vector<std::unique_ptr<ParticleEmitter>> emitters_;
};

} // namespace shiki

#include <shiki/effect/particle.h>
#include <cmath>
#include <algorithm>

namespace shiki {

ParticleEmitter::ParticleEmitter(const ParticleEmitterConfig& config) : config_(config), rng_(std::random_device{}()) {
    particles_.resize(config.maxParticles);
}

void ParticleEmitter::initialize(const ParticleEmitterConfig& config) {
    config_ = config;
    particles_.resize(config.maxParticles);
    activeCount_ = 0;
    emissionAccumulator_ = 0.0f;
    isRunning_ = true;
}

void ParticleEmitter::shutdown() {
    reset();
}

void ParticleEmitter::update(float dt) {
    if (!isRunning_) return;

    if (config_.emissionRate > 0.0f) {
        emissionAccumulator_ += config_.emissionRate * dt;
        int emitCount = static_cast<int>(emissionAccumulator_);
        if (emitCount > 0) {
            emissionAccumulator_ -= static_cast<float>(emitCount);
            emit(emitCount);
        }
    }

    activeCount_ = 0;
    for (auto& particle : particles_) {
        if (particle.active) {
            updateParticle(particle, dt);
            if (particle.active) {
                activeCount_++;
            }
        }
    }
}

void ParticleEmitter::emit(int count) {
    for (int i = 0; i < count; ++i) {
        spawnParticle();
    }
}

void ParticleEmitter::emitBurst(int count) {
    emit(count);
}

void ParticleEmitter::reset() {
    for (auto& particle : particles_) {
        particle.active = false;
    }
    activeCount_ = 0;
    emissionAccumulator_ = 0.0f;
}

void ParticleEmitter::spawnParticle() {
    for (auto& particle : particles_) {
        if (!particle.active) {
            particle.active = true;
            particle.position = config_.position;

            float angle = randomFloat(-config_.emitAngle, config_.emitAngle);
            float speed = randomFloat(config_.minSpeed, config_.maxSpeed);
            particle.velocity.x = std::cos(angle) * speed;
            particle.velocity.y = std::sin(angle) * speed;

            float size = randomFloat(config_.minSize, config_.maxSize);
            particle.size = {size, size};

            particle.lifetime = 0.0f;
            particle.maxLifetime = randomFloat(config_.minLifetime, config_.maxLifetime);

            particle.color = config_.startColor;
            particle.alpha = 1.0f;
            particle.scale = 1.0f;

            activeCount_++;
            return;
        }
    }
}

void ParticleEmitter::updateParticle(Particle& particle, float dt) {
    particle.position.x += particle.velocity.x * dt;
    particle.position.y += particle.velocity.y * dt;

    particle.lifetime += dt;
    float lifeRatio = particle.lifetime / particle.maxLifetime;

    if (lifeRatio >= 1.0f) {
        particle.active = false;
        return;
    }

    particle.color = lerpColor(config_.startColor, config_.endColor, lifeRatio);

    particle.alpha = 1.0f - lifeRatio;

    particle.scale = 1.0f - lifeRatio * 0.5f;
}

float ParticleEmitter::randomFloat(float min, float max) {
    std::uniform_real_distribution<float> dist(min, max);
    return dist(rng_);
}

uint32_t ParticleEmitter::lerpColor(uint32_t color1, uint32_t color2, float t) {
    t = std::clamp(t, 0.0f, 1.0f);

    uint8_t r1 = (color1 >> 24) & 0xFF;
    uint8_t g1 = (color1 >> 16) & 0xFF;
    uint8_t b1 = (color1 >> 8) & 0xFF;
    uint8_t a1 = color1 & 0xFF;

    uint8_t r2 = (color2 >> 24) & 0xFF;
    uint8_t g2 = (color2 >> 16) & 0xFF;
    uint8_t b2 = (color2 >> 8) & 0xFF;
    uint8_t a2 = color2 & 0xFF;

    uint8_t r = static_cast<uint8_t>(static_cast<float>(r1) + static_cast<float>(r2 - r1) * t);
    uint8_t g = static_cast<uint8_t>(static_cast<float>(g1) + static_cast<float>(g2 - g1) * t);
    uint8_t b = static_cast<uint8_t>(static_cast<float>(b1) + static_cast<float>(b2 - b1) * t);
    uint8_t a = static_cast<uint8_t>(static_cast<float>(a1) + static_cast<float>(a2 - a1) * t);

    return (r << 24) | (g << 16) | (b << 8) | a;
}


void EffectManager::initialize() {
    clear();
}

void EffectManager::shutdown() {
    clear();
}

ParticleEmitter* EffectManager::createExplosion(const Vec2& position, float scale) {
    ParticleEmitterConfig config;
    config.position = position;
    config.emitAngle = 3.14159265359f;
    config.minSpeed = 100.0f * scale;
    config.maxSpeed = 300.0f * scale;
    config.minSize = 2.0f * scale;
    config.maxSize = 8.0f * scale;
    config.minLifetime = 0.3f;
    config.maxLifetime = 1.0f;
    config.startColor = 0xFFFF0000;
    config.endColor = 0x00FF0000;
    config.emissionRate = 0.0f;
    config.maxParticles = 100;
    config.blendMode = BlendMode::Add;
    config.loop = false;

    auto emitter = std::make_unique<ParticleEmitter>(config);
    emitter->emitBurst(50);
    emitters_.push_back(std::move(emitter));
    return emitters_.back().get();
}

ParticleEmitter* EffectManager::createSparkle(const Vec2& position, float scale) {
    ParticleEmitterConfig config;
    config.position = position;
    config.emitAngle = 3.14159265359f / 2.0f;
    config.minSpeed = 50.0f * scale;
    config.maxSpeed = 150.0f * scale;
    config.minSize = 1.0f * scale;
    config.maxSize = 4.0f * scale;
    config.minLifetime = 0.5f;
    config.maxLifetime = 1.5f;
    config.startColor = 0xFFFFFF00;
    config.endColor = 0x00FFFFFF;
    config.emissionRate = 20.0f;
    config.maxParticles = 50;
    config.blendMode = BlendMode::Add;
    config.loop = true;

    auto emitter = std::make_unique<ParticleEmitter>(config);
    emitters_.push_back(std::move(emitter));
    return emitters_.back().get();
}

ParticleEmitter* EffectManager::createSmoke(const Vec2& position, float scale) {
    ParticleEmitterConfig config;
    config.position = position;
    config.emitAngle = 3.14159265359f / 4.0f;
    config.minSpeed = 20.0f * scale;
    config.maxSpeed = 60.0f * scale;
    config.minSize = 4.0f * scale;
    config.maxSize = 16.0f * scale;
    config.minLifetime = 1.0f;
    config.maxLifetime = 3.0f;
    config.startColor = 0xFF808080;
    config.endColor = 0x00808080;
    config.emissionRate = 10.0f;
    config.maxParticles = 100;
    config.blendMode = BlendMode::Alpha;
    config.loop = true;

    auto emitter = std::make_unique<ParticleEmitter>(config);
    emitters_.push_back(std::move(emitter));
    return emitters_.back().get();
}

ParticleEmitter* EffectManager::createFire(const Vec2& position, float scale) {
    ParticleEmitterConfig config;
    config.position = position;
    config.emitAngle = 3.14159265359f / 6.0f;
    config.minSpeed = 50.0f * scale;
    config.maxSpeed = 150.0f * scale;
    config.minSize = 2.0f * scale;
    config.maxSize = 8.0f * scale;
    config.minLifetime = 0.3f;
    config.maxLifetime = 1.0f;
    config.startColor = 0xFFFF0000;
    config.endColor = 0x00FF0000;
    config.emissionRate = 50.0f;
    config.maxParticles = 200;
    config.blendMode = BlendMode::Add;
    config.loop = true;

    auto emitter = std::make_unique<ParticleEmitter>(config);
    emitters_.push_back(std::move(emitter));
    return emitters_.back().get();
}

ParticleEmitter* EffectManager::createCustomEffect(const Vec2& position, const ParticleEmitterConfig& config) {
    auto emitter = std::make_unique<ParticleEmitter>(config);
    emitter->setPosition(position);
    emitters_.push_back(std::move(emitter));
    return emitters_.back().get();
}

void EffectManager::update(float dt) {
    for (auto& emitter : emitters_) {
        emitter->update(dt);
    }

    emitters_.erase(
        std::remove_if(emitters_.begin(), emitters_.end(),
            [](const std::unique_ptr<ParticleEmitter>& emitter) {
                return !emitter->getActiveCount();
            }),
        emitters_.end()
    );
}

void EffectManager::clear() {
    emitters_.clear();
}

} // namespace shiki

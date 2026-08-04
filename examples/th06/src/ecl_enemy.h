#pragma once

#include <array>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <memory>
#include <shiki/ecl/ecl_executor.h>
#include <shiki/render/texture.h>
#include <shiki/resource/resource_manager.h>
#include <shiki/shiki.h>
#include <spdlog/spdlog.h>
#include <unordered_map>
#include <vector>

#include "th06_anm.h"
#include "th06_effect_color.h"
#include "th06_menu_anm.h"

inline int getBulletSpriteIndex(int bulletType, int bulletColor) {
	int color = std::abs(bulletColor) & 0xFF;
	switch (bulletType) {
	case 0:
		return 14 + (color % 16); // PELLET
	case 1:
		return 30 + (color % 16); // RING_BALL
	case 2:
		return 46 + (color % 16); // RICE
	case 3:
		return 62 + (color % 16); // BALL
	case 4:
		return 78 + (color % 16); // KUNAI
	case 5:
		return 94 + (color % 16); // SHARD
	case 6:
		return 110 + (color % 8); // BIG_BALL
	case 7:
		return 118 + (color % 4); // FIREBALL
	case 8:
		return 122 + (color % 8); // DAGGER
	case 9:
		return color % 4; // BUBBLE (etama4)
	default:
		return 14 + (color % 16);
	}
}

inline const char *getBulletSpriteAtlas(int bulletType) {
	return bulletType == 9 ? "etama4" : "etama3";
}

inline shiki::BlendMode getTH06BulletBlendMode(int bulletType) {
	// etama4 script 0 selects additive blending for the bubble body. The
	// etama3 body scripts, including BIG_BALL, retain standard source-alpha
	// blending and obtain their translucency from the auxiliary alpha texture.
	return bulletType == 9 ? shiki::BlendMode::Add : shiki::BlendMode::Alpha;
}

inline constexpr float TH06_PLAYER_HITBOX_HALF_SIZE = 1.25f;

inline shiki::Vec2 getTH06BulletCollisionSize(int bulletType) {
	// BulletManager::AddedCallback derives these full AABB sizes from the
	// selected ANM sprite. TH06 rotates the visual VM, but not ordinary bullet
	// collision; only lasers transform the player into beam-local space.
	static constexpr std::array<float, 10> SIZES = {
	    4.0f, 6.0f, 4.0f, 6.0f, 5.0f, 4.0f, 16.0f, 11.0f, 9.0f, 32.0f};
	const size_t index = static_cast<size_t>(std::clamp(bulletType, 0, 9));
	return {SIZES[index], SIZES[index]};
}

enum TH06ItemType {
	TH06_ITEM_NONE = -2,
	TH06_ITEM_RANDOM = -1,
	TH06_ITEM_POWER_SMALL = 0,
	TH06_ITEM_POINT = 1,
	TH06_ITEM_POWER_BIG = 2,
	TH06_ITEM_BOMB = 3,
	TH06_ITEM_FULL_POWER = 4,
	TH06_ITEM_LIFE = 5,
	TH06_ITEM_POINT_BULLET = 6,
};

inline int getTH06PointItemScore(int difficulty, float y) {
	static constexpr std::array<int, 5> TOP = {100000, 100000, 150000, 200000,
	                                           300000};
	static constexpr std::array<int, 5> BOTTOM = {60000, 60000, 100000, 150000,
	                                              200000};
	static constexpr std::array<int, 5> MULTIPLIER = {100, 100, 180, 270, 400};
	const size_t index = static_cast<size_t>(std::clamp(difficulty, 0, 4));
	return y < 128.0f ? TOP[index]
	                  : BOTTOM[index] -
	                        (static_cast<int>(y) - 128) * MULTIPLIER[index];
}

enum class TH06BulletSpawnState { Fired, Fast, Normal, Slow };

struct TH06BulletSpawnAnimation {
	TH06BulletSpawnState state = TH06BulletSpawnState::Fired;
	const char *atlas = "etama3";
	int spriteIndex = 0;
	int durationFrames = 0;
	int alphaFrames = 0;
	float movementDivisor = 1.0f;
	float initialScale = 1.0f;
	float scalePerFrame = 0.0f;
	float targetAlpha = 1.0f;
	bool fadeOut = false;

	[[nodiscard]] bool active() const {
		return state != TH06BulletSpawnState::Fired;
	}

	[[nodiscard]] float alphaAtAge(int age) const {
		if (alphaFrames <= 0)
			return targetAlpha;
		const float progress =
		    std::clamp(static_cast<float>(age) / alphaFrames, 0.0f, 1.0f);
		return targetAlpha * (fadeOut ? 1.0f - progress : progress);
	}
};

// BulletManager.cpp::SpawnSingleBullet and etama3/etama4 ANM scripts.
// Flags are tested in original priority order: fast, normal, then slow.
inline TH06BulletSpawnAnimation
getTH06BulletSpawnAnimation(int bulletType, int bulletColor, int flags) {
	TH06BulletSpawnAnimation animation;
	if ((flags & 0x2) != 0) {
		animation.state = TH06BulletSpawnState::Fast;
		animation.movementDivisor = 2.0f;
	} else if ((flags & 0x4) != 0) {
		animation.state = TH06BulletSpawnState::Normal;
		animation.movementDivisor = 2.5f;
	} else if ((flags & 0x8) != 0) {
		animation.state = TH06BulletSpawnState::Slow;
		animation.movementDivisor = 3.0f;
	} else {
		return animation;
	}

	const int color = std::abs(bulletColor) & 0xf;
	if (bulletType == 9) {
		// etama4 script 2: SPAWN_BUBBLE_SLOW. All three states use this VM,
		// while movement still follows the state selected above.
		animation.atlas = "etama4";
		animation.spriteIndex = color % 4;
		animation.durationFrames = 24;
		animation.alphaFrames = 32;
		animation.initialScale = 2.0f;
		animation.scalePerFrame = -1.0f / 24.0f;
		animation.targetAlpha = 1.0f;
		return animation;
	}

	static constexpr std::array<int, 16> OFFSETS_16PX = {
	    0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 0};
	static constexpr std::array<int, 8> OFFSETS_32PX = {0, 1, 1, 2, 2, 3, 4, 0};
	animation.spriteIndex =
	    140 + (bulletType <= 5 ? OFFSETS_16PX[static_cast<size_t>(color)]
	                           : OFFSETS_32PX[static_cast<size_t>(color & 7)]);
	animation.targetAlpha = 144.0f / 255.0f;

	if (bulletType >= 6) {
		// etama3 script 20: SPAWN_BIG_BALL_HUGE.
		animation.durationFrames = 32;
		animation.alphaFrames = 32;
		animation.initialScale = 6.0f;
		animation.scalePerFrame = -0.1f;
		return animation;
	}

	switch (animation.state) {
	case TH06BulletSpawnState::Fast:
		// The renderer does not yet execute TH06's ANM color/blend commands.
		// Preserve the visible fast-spawn fallback verified by the stage
		// patterns.
		animation.spriteIndex = 135 + OFFSETS_16PX[static_cast<size_t>(color)];
		animation.durationFrames = 8;
		animation.alphaFrames = 8;
		animation.initialScale = 2.4f;
		animation.scalePerFrame = -1.4f / 8.0f;
		animation.targetAlpha = 1.0f;
		animation.fadeOut = true;
		break;
	case TH06BulletSpawnState::Normal:
		animation.durationFrames = 16;
		animation.alphaFrames = 16;
		animation.initialScale = 3.0f;
		animation.scalePerFrame = -1.0f / 12.0f;
		break;
	case TH06BulletSpawnState::Slow:
		animation.durationFrames = 32;
		animation.alphaFrames = 32;
		animation.initialScale = 4.0f;
		animation.scalePerFrame = -0.075f;
		break;
	case TH06BulletSpawnState::Fired:
		break;
	}
	return animation;
}

struct ECLBulletSpawn {
	float x = 0.0f;
	float y = 0.0f;
	float vx = 0.0f;
	float vy = 0.0f;
	float angle = 0.0f;
	int bulletType = 0;
	int bulletColor = 0;
	int flags = 0;
	std::array<int, 4> exInts{};
	std::array<float, 4> exFloats{};
};

struct ECLVars {
	std::array<int32_t, 32> ints{};
	std::array<float, 32> floats{};

	int32_t &getInt(int32_t id) {
		size_t idx = static_cast<size_t>(-id - 10001);
		if (idx < ints.size())
			return ints[idx];
		static int32_t dummy = 0;
		return dummy;
	}

	float &getFloat(int32_t id) {
		size_t idx = static_cast<size_t>(-id - 10001);
		if (idx < floats.size())
			return floats[idx];
		static float dummy = 0;
		return dummy;
	}
};

struct ECLEnemy {
	int32_t type = 0;
	int32_t subId = 0;
	float x = 0, y = 0, z = 0;
	float vx = 0, vy = 0;
	float speed = 0;
	float angle = 0;
	float ax = 0, ay = 0;
	float angularVelocity = 0;
	float linearAccel = 0;
	int movementMode = 0; // 0=axis, 1=angle+speed+accel, 2=interpolation
	float shootOffsetX = 0, shootOffsetY = 0;
	bool inFiring = true;
	int rowFlag = 0;
	int difficulty = 1; // TH06: 0=Easy, 1=Normal, 2=Hard, 3=Lunatic
	int rank = 16;
	int playerCharacter = 0;
	int hp = 100;
	int maxHp = 100;
	int itemDrop = TH06_ITEM_NONE;
	int scoreValue = 100;
	int deathAnm1 = 0;
	int deathAnm2 = 0;
	int deathAnm3 = 0;
	bool alive = true;
	bool collidable = true;
	bool damageable = true;
	bool interactable = true;
	bool invisible = false;
	int deathMode = 0;
	bool deathResolved = false;

	shiki::Sprite sprite;
	std::shared_ptr<shiki::Texture> texture;
	const shiki::ResourceManager *resourceManager = nullptr;
	int stageNumber = 7;

	std::function<void(const ECLBulletSpawn &)> onSpawnBullet;
	std::function<void(int enemyType, int subId, float posX, float posY,
	                   float posZ, float velX, float velY, float accX,
	                   float accY, int hp)>
	    onSpawnChildEnemy;
	std::function<void(int soundId)> onSound;
	std::function<void()> onBulletCancel;
	std::function<void(int itemType, float x, float y, int state)> onDropItem;
	std::function<void(int effectId, float x, float y, int count,
	                   uint32_t color)>
	    onSpawnEffect;
	std::function<void(int effectId, float x, float y, uint32_t color, float vx,
	                   float vy, float ax, float ay)>
	    onSpawnMovingEffect;
	std::function<void()> onKillAllEnemies;
	std::function<void(bool enabled)> onBossChanged;
	std::function<void(int spellId, int portraitId, const std::string &name)>
	    onSpellStart;
	std::function<void(int secondsRemaining)> onSpellEnd;
	std::function<bool()> isGlobalSpellActive;
	std::function<void()> onSpellCaptureFailed;
	std::function<void(bool boss)> onDeath;
	std::function<void()> onDamage;
	// TH06 stage-specific EX instructions that operate on the global bullet
	// pool. Returns the number of large bullets inspected where the original
	// instruction stores that value in var3.
	std::function<int(int instruction, int parameter, float enemyX,
	                  float enemyY)>
	    onBulletTransform;
	std::function<void(bool stopped)> onTimeStop;

	const shiki::ecl::ECLParser *eclParser = nullptr;

	const shiki::ecl::ECLSubroutine *sub = nullptr;
	size_t instrIndex = 0;
	float elapsed = 0.0f;
	float waitTimer = 0.0f;
	float shootTimer = 0.0f;
	float shootInterval = 0.0f;
	bool isBoss = false;
	bool spellActive = false;
	bool timeoutSpell = false;
	bool phaseTransitionPending = false;
	int pendingPlayerDamage = 0;
	bool pendingDamageFromBomb = false;
	int bossId = -1;
	int bossLifeCount = 0;
	int currentSubId = -1;
	uint64_t spawnedBulletCount = 0;
	int deathCallbackSub = -1;
	int lifeCallbackThreshold = -1;
	int lifeCallbackSub = -1;
	int timerCallbackThreshold = -1;
	int timerCallbackSub = -1;
	int bossTimerFrames = 0;
	int repeatingExInstruction = -1;
	float batWingEffectAngle = 0.0f;
	int batWingEffectTimer = 0;
	shiki::Vec2 starPatternEnemyOrigin;
	shiki::Vec2 starPatternPlayerOrigin;
	std::array<float, 6> starPatternAngles{};
	float bulletRankSpeedLow = -0.5f;
	float bulletRankSpeedHigh = 0.5f;
	int bulletRankAmount1Low = 0;
	int bulletRankAmount1High = 0;
	int bulletRankAmount2Low = 0;
	int bulletRankAmount2High = 0;
	std::unordered_map<int, int> interrupts;
	ECLVars vars;
	int compareRegister = 0;

	struct SubCallState {
		const shiki::ecl::ECLSubroutine *savedSub = nullptr;
		size_t savedInstrIndex = 0;
		float savedElapsed = 0.0f;
		float savedWaitTimer = 0.0f;
		ECLVars savedVars;
		int savedCompareRegister = 0;
		int savedRepeatingExInstruction = -1;
	};
	std::vector<SubCallState> callStack;

	bool shouldClampPos = false;
	float boundMinX = 0, boundMinY = 0, boundMaxX = 0, boundMaxY = 0;

	float moveInterpX = 0, moveInterpY = 0;
	float moveInterpStartX = 0, moveInterpStartY = 0;
	float moveInterpTimer = 0;
	float moveInterpDuration = 0;
	int movementEaseType = 0;

	bool invertX = false;

	float playerX = 192.0f, playerY = 400.0f;

	int behaviorTimer_ = 0;

	struct BulletPatternState {
		int bulletType = 0;
		int bulletColor = 0;
		int count1 = 1;
		int count2 = 1;
		float speed1 = 0.0f;
		float speed2 = 0.0f;
		float angle1 = 0.0f;
		float angle2 = 0.0f;
		int flags = 0;
		std::array<int, 4> exInts{};
		std::array<float, 4> exFloats{};
		int soundId = 0;
		int aimMode = 0;
		bool configured = false;
	};
	BulletPatternState bulletPattern;

	struct LaserState {
		shiki::Sprite sprite;
		shiki::Sprite flareSprite;
		float x = 0.0f;
		float y = 0.0f;
		float angle = 0.0f;
		float speed = 0.0f;
		float startOffset = 0.0f;
		float endOffset = 0.0f;
		float startLength = 0.0f;
		float width = 0.0f;
		int startTime = 0;
		int duration = 0;
		int despawnDuration = 0;
		int hitboxStartTime = 0;
		int hitboxEndDelay = 0;
		int flags = 0;
		int state = 0;
		int stateTimer = 0;
		bool referenced = true;
		int ageFrames = 0;
		int cancelFrames = -1;
		bool active = false;
	};
	std::array<LaserState, 64> lasers;
	std::array<int, 8> laserReferences = [] {
		std::array<int, 8> references{};
		references.fill(-1);
		return references;
	}();
	int laserStore = 0;
	bool globalTimeStopped = false;

	struct SpellEffectState {
		float axisX = 0.0f;
		float axisY = 0.0f;
		float axisZ = 1.0f;
		float targetDistance = 0.0f;
		float distance = 0.0f;
		float angle = 0.0f;
		int colorId = 27;
		int ageFrames = 0;
	};
	std::array<SpellEffectState, 12> spellEffects;
	size_t spellEffectCount = 0;

	struct AuxiliaryAnimationState {
		int scriptId = -1;
		TH06MenuAnmVm vm;
		shiki::Sprite sprite;
		bool active = false;
	};
	std::array<AuxiliaryAnimationState, 8> auxiliaryAnimations;

	std::string atlasName;
	TH06MenuAnmVm primaryAnmVm;
	int animationScript = -1;
	int anmPoseDefault = -1;
	int anmPoseFarLeft = -1;
	int anmPoseFarRight = -1;
	int anmPoseLeft = -1;
	int anmPoseRight = -1;
	int anmPoseState = 0xff;

	static constexpr double LOGIC_DT = 1.0 / 60.0;
	double logicAccum = 0.0;

	std::vector<int32_t> animSpriteIndices;
	size_t animFrameIndex = 0;
	float animFrameTimer = 0.0f;
	float animFrameDuration = 0.1f;
	TH06AnmScript anmScript;
	float anmElapsedFrames = 0.0f;
	float hitboxWidth = 32.0f;
	float hitboxHeight = 32.0f;
	bool rotateAnm = false;
	bool disableCallStack = false;

#include "ecl_enemy_runtime.inl"
};

inline void updateECLEnemies(std::vector<ECLEnemy> &enemies, float dt) {
	for (auto &e : enemies) {
		if (e.alive) {
			e.update(dt);
		}
	}
}

// Supervisor::LoadConfig enables GCOS_USE_D3D_HW_TEXTURE_BLENDING by default.
// BulletManager::RegisterChain consequently selects TH06's pale fallback
// palette rather than g_EffectsColorWithTextureBlending's saturated colors.
inline constexpr std::array<uint32_t, 28> TH06_SPELL_EFFECT_COLORS = {
    0xfff0f0f0, 0xfff0f0f0, 0xffffffff, 0xffffe0e0, 0xffffe0e0, 0xffffe0e0,
    0xffffe0ff, 0xffffe0ff, 0xffffe0ff, 0xffe0e0ff, 0xffe0e0ff, 0xffe0e0ff,
    0xffe0ffff, 0xffe0ffff, 0xffe0ffff, 0xffe0ffe0, 0xffe0ffe0, 0xffe0ffe0,
    0xffe0ffe0, 0xffe0ffe0, 0xffe0ffe0, 0xffffffe0, 0xffffffe0, 0xffffffe0,
    0xffffe0e0, 0xffffe0e0, 0xffffe0e0, 0xffffffff};

[[nodiscard]] inline uint32_t th06SpellEffectColor(int colorId) {
	return TH06_SPELL_EFFECT_COLORS[std::clamp(colorId, 0, 27)];
}

inline void renderECLEnemies(std::vector<ECLEnemy> &enemies,
                             shiki::Renderer *renderer,
                             bool tintProjectilesRed = false) {
	for (auto &e : enemies) {
		if (!e.alive)
			continue;
		// TH06 lasers are owned by the global bullet manager.  They remain
		// visible while the emitter's body is hidden by ECL.
		for (auto &laser : e.lasers) {
			if (laser.active && laser.sprite.getTexture()) {
				if (tintProjectilesRed) {
					auto sprite = laser.sprite;
					sprite.setColor({1.0f, 0.12f, 0.12f, sprite.getColor().w});
					renderer->drawSprite(sprite, 0.15f, true);
				} else {
					renderer->drawSprite(laser.sprite, 0.15f, true);
				}
				if (laser.flareSprite.getTexture() &&
				    (laser.startOffset < 16.0f || laser.speed == 0.0f)) {
					if (tintProjectilesRed) {
						auto flare = laser.flareSprite;
						flare.setColor(
						    {1.0f, 0.12f, 0.12f, flare.getColor().w});
						renderer->drawSprite(flare, 0.16f, true);
					} else {
						renderer->drawSprite(laser.flareSprite, 0.16f, true);
					}
				}
			}
		}
		if (e.invisible)
			continue;
		for (size_t slot = 0; slot < 4; ++slot) {
			const auto &animation = e.auxiliaryAnimations[slot];
			if (animation.active && animation.sprite.getTexture())
				renderer->drawSprite(animation.sprite, 0.0f, true);
		}
		if (!e.primaryAnmVm.file || e.primaryAnmVm.visible)
			renderer->drawSprite(e.sprite, 0.0f, true);
		for (size_t index = 0; index < e.spellEffectCount; ++index) {
			const auto &effect = e.spellEffects[index];
			const float axisLength = std::sqrt(effect.axisX * effect.axisX +
			                                   effect.axisY * effect.axisY +
			                                   effect.axisZ * effect.axisZ);
			if (axisLength <= 0.0001f || !e.resourceManager)
				continue;
			const float ax = effect.axisX / axisLength;
			const float ay = effect.axisY / axisLength;
			const float az = effect.axisZ / axisLength;
			float px = ay;
			float py = -ax;
			float pz = 0.0f;
			const float perpendicularLength = std::hypot(px, py);
			if (perpendicularLength <= 0.0001f) {
				px = 1.0f;
				py = 0.0f;
			} else {
				px /= perpendicularLength;
				py /= perpendicularLength;
			}
			const float sine = std::sin(effect.angle);
			const float cosine = std::cos(effect.angle);
			const float crossX = ay * pz - az * py;
			const float crossY = az * px - ax * pz;
			const float offsetX =
			    effect.distance * (px * cosine + crossX * sine);
			const float offsetY =
			    effect.distance * (py * cosine + crossY * sine);

			const auto texture =
			    e.resourceManager->getSpriteTexture("etama4", 24);
			if (!texture || !texture->isValid())
				continue;
			shiki::Sprite sprite(texture);
			const float width = static_cast<float>(texture->getWidth());
			const float height = static_cast<float>(texture->getHeight());
			sprite.setSourceRect({0.0f, 0.0f, width, height});
			sprite.setOrigin({width * 0.5f, height * 0.5f});
			sprite.setPosition(e.x + offsetX, e.y + offsetY);
			sprite.setRotation(-effect.ageFrames * 0.15f * 180.0f /
			                   std::numbers::pi_v<float>);
			sprite.setBlendMode(shiki::BlendMode::Add);
			const uint32_t packed = th06AdditiveEffectDisplayColor(
			    th06SpellEffectColor(effect.colorId));
			const float age = static_cast<float>(effect.ageFrames);
			const float pulseFrame =
			    age < 240.0f ? age : 60.0f + std::fmod(age - 60.0f, 180.0f);
			float alpha = 1.0f;
			if (pulseFrame >= 120.0f && pulseFrame < 180.0f)
				alpha = 1.0f - (pulseFrame - 120.0f) * 0.75f / 60.0f;
			else if (pulseFrame >= 180.0f)
				alpha = 0.25f + (pulseFrame - 180.0f) * 0.75f / 60.0f;
			sprite.setColor({static_cast<float>((packed >> 16) & 0xff) / 255.0f,
			                 static_cast<float>((packed >> 8) & 0xff) / 255.0f,
			                 static_cast<float>(packed & 0xff) / 255.0f,
			                 alpha});
			renderer->drawSprite(sprite, 0.1f, true);
		}
		for (size_t slot = 4; slot < e.auxiliaryAnimations.size(); ++slot) {
			const auto &animation = e.auxiliaryAnimations[slot];
			if (animation.active && animation.sprite.getTexture())
				renderer->drawSprite(animation.sprite, 0.1f, true);
		}
	}
}

template <typename PlayerBullet>
inline void
checkPlayerBulletsVsECLEnemies(std::vector<PlayerBullet> &playerBullets,
                               std::vector<ECLEnemy> &enemies, int &score) {
	std::vector<int> frameDamage(enemies.size(), 0);
	std::vector<bool> damageFromBomb(enemies.size(), false);
	for (size_t enemyIndex = 0; enemyIndex < enemies.size(); ++enemyIndex) {
		auto &enemy = enemies[enemyIndex];
		frameDamage[enemyIndex] = enemy.pendingPlayerDamage;
		damageFromBomb[enemyIndex] = enemy.pendingDamageFromBomb;
		enemy.pendingPlayerDamage = 0;
		enemy.pendingDamageFromBomb = false;
	}
	for (auto &bullet : playerBullets) {
		if (!bullet.active)
			continue;
		auto bpos = bullet.sprite.getPosition();
		for (size_t enemyIndex = 0; enemyIndex < enemies.size(); ++enemyIndex) {
			auto &e = enemies[enemyIndex];
			if (!e.alive || !e.interactable || !e.damageable ||
			    e.phaseTransitionPending)
				continue;
			float dx = bpos.x - e.x;
			float dy = bpos.y - e.y;
			bool isLaser = false;
			if constexpr (requires { bullet.laser; })
				isLaser = bullet.laser;
			if constexpr (requires { bullet.damageReady; }) {
				if (isLaser && !bullet.damageReady)
					continue;
			}
			const bool hit = isLaser ? std::abs(dx) < e.hitboxWidth * 0.5f &&
			                               e.y <= bpos.y * 2.0f
			                         : std::abs(dx) < e.hitboxWidth * 0.5f &&
			                               std::abs(dy) < e.hitboxHeight * 0.5f;
			if (hit) {
				frameDamage[enemyIndex] += bullet.damage;
				if (e.onSpawnEffect)
					e.onSpawnEffect(5, bpos.x, bpos.y, 1, 0xffffffff);
				if (!isLaser) {
					bullet.sprite.setPosition(-1000, -1000);
					bullet.active = false;
				} else if constexpr (requires { bullet.damageReady; }) {
					bullet.damageReady = false;
				}
				break;
			}
		}
	}

	for (size_t enemyIndex = 0; enemyIndex < enemies.size(); ++enemyIndex) {
		auto &e = enemies[enemyIndex];
		int damage = std::min(frameDamage[enemyIndex], 70);
		if (e.spellActive && damage > 0) {
			const int divisor = damageFromBomb[enemyIndex] ? 3 : 7;
			damage = damage > divisor ? damage / divisor : 1;
		}
		if (damage <= 0 || !e.alive)
			continue;
		if (damageFromBomb[enemyIndex] && e.onSpawnEffect &&
		    e.bossTimerFrames % 4 == 0)
			e.onSpawnEffect(3, e.x, e.y, 1, 0xffffffff);
		e.hp -= damage;
		if (e.onDamage)
			e.onDamage();
		const bool phaseChanged = e.handleDamageCallbacks();
		if (e.hp <= 0 && !phaseChanged) {
			const bool bossEnemy =
			    e.isBoss || e.bossId >= 0 || e.subId == 16 || e.subId == 31;
			if (bossEnemy) {
				e.hp = 1;
			} else {
				e.alive = false;
				score += e.scoreValue;
				e.notifyDeath(false, damageFromBomb[enemyIndex] ? 1 : 0);
				// spdlog::info("Enemy killed! Score: {}, pos=({:.0f},{:.0f})",
				//              score, e.x, e.y);
			}
		}
	}
}

inline void cleanupECLEnemies(std::vector<ECLEnemy> &enemies, float gameHeight,
                              float gameWidth = 384.0f) {
	enemies.erase(std::remove_if(enemies.begin(), enemies.end(),
	                             [&](const ECLEnemy &e) {
		                             return !e.alive ||
		                                    e.y > gameHeight + 100 ||
		                                    e.y < -200 || e.x < -200 ||
		                                    e.x > gameWidth + 200;
	                             }),
	              enemies.end());
}

inline ECLEnemy
createECLEnemy(const shiki::ecl::ECLParser &parser, int32_t enemyType,
               int32_t subId, float posX, float posY, float velX, float velY,
               std::shared_ptr<shiki::Texture> defaultTexture,
               const shiki::ResourceManager *resourceManager = nullptr) {
	ECLEnemy e;
	e.type = enemyType;
	e.subId = subId;
	e.currentSubId = subId;
	e.x = posX;
	e.y = posY;
	e.vx = velX;
	e.vy = velY;
	e.hp = 100;
	e.maxHp = 100;
	e.texture = defaultTexture;
	e.resourceManager = resourceManager;
	e.eclParser = &parser;

	if (defaultTexture && defaultTexture->isValid()) {
		e.sprite = shiki::Sprite(defaultTexture);
		float tw = static_cast<float>(defaultTexture->getWidth());
		float th = static_cast<float>(defaultTexture->getHeight());
		e.sprite.setSourceRect(shiki::Rect(0, 0, tw, th));
		e.sprite.setOrigin({tw / 2.0f, th / 2.0f});
		e.sprite.setScale(1.0f, 1.0f);
	}

	e.sub = parser.getSubroutine(static_cast<size_t>(subId));
	if (!e.sub) {
		spdlog::warn("ECLEnemy: sub {} not found!", subId);
	}

	// The stage-specific atlas is selected by the first animation instruction.
	// Keeping this empty prevents a newly created Stage 1 enemy from briefly
	// resolving a Stage 7 sprite before its time-zero ECL has executed.
	e.atlasName.clear();

	e.logicAccum = 0.0f;

	e.sprite.setPosition(posX, posY);
	return e;
}

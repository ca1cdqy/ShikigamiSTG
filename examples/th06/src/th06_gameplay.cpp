#include "ecl_enemy.h"
#include "th06_effect_manager.h"
#include "th06_game_state.h"
#include "th06_menu_anm.h"
#include "th06_stage_background.h"
#include "th06_systems.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <numbers>
#include <shiki/audio/audio_manager.h>
#include <shiki/ecl/ecl_executor.h>
#include <shiki/frontend/realtime.h>
#include <shiki/render/texture.h>
#include <shiki/resource/resource_manager.h>
#include <shiki/shiki.h>
#include <spdlog/spdlog.h>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif

int resolveTH06EnemyItemDrop(GameState &state, int requestedType) {
	if (requestedType != TH06_ITEM_RANDOM)
		return requestedType;
	static constexpr std::array<int, 32> RANDOM_ITEMS = {
	    TH06_ITEM_POWER_SMALL, TH06_ITEM_POWER_SMALL, TH06_ITEM_POINT,
	    TH06_ITEM_POWER_SMALL, TH06_ITEM_POINT,       TH06_ITEM_POWER_SMALL,
	    TH06_ITEM_POWER_SMALL, TH06_ITEM_POINT,       TH06_ITEM_POINT,
	    TH06_ITEM_POINT,       TH06_ITEM_POWER_SMALL, TH06_ITEM_POWER_SMALL,
	    TH06_ITEM_POWER_SMALL, TH06_ITEM_POINT,       TH06_ITEM_POINT,
	    TH06_ITEM_POWER_SMALL, TH06_ITEM_POINT,       TH06_ITEM_POWER_SMALL,
	    TH06_ITEM_POINT,       TH06_ITEM_POWER_SMALL, TH06_ITEM_POINT,
	    TH06_ITEM_POWER_SMALL, TH06_ITEM_POINT,       TH06_ITEM_POWER_SMALL,
	    TH06_ITEM_POINT,       TH06_ITEM_POWER_SMALL, TH06_ITEM_POWER_SMALL,
	    TH06_ITEM_POINT,       TH06_ITEM_POINT,       TH06_ITEM_POINT,
	    TH06_ITEM_POWER_SMALL, TH06_ITEM_POWER_BIG};
	int result = TH06_ITEM_NONE;
	if (state.randomItemSpawnIndex % 3 == 0) {
		result = RANDOM_ITEMS[static_cast<size_t>(state.randomItemTableIndex)];
		state.randomItemTableIndex = (state.randomItemTableIndex + 1) %
		                             static_cast<int>(RANDOM_ITEMS.size());
	}
	++state.randomItemSpawnIndex;
	return result;
}

void spawnTH06Item(GameState &state, int requestedType, float x, float y,
                   int itemState) {
	const int type = resolveTH06EnemyItemDrop(state, requestedType);
	if (type < TH06_ITEM_POWER_SMALL || type > TH06_ITEM_POINT_BULLET ||
	    state.items.size() >= 512)
		return;

	auto texture = state.resourceManager
	                   ? state.resourceManager->getSpriteTexture("etama3", type)
	                   : nullptr;
	if (!texture || !texture->isValid())
		return;

	GameState::Item item;
	item.sprite = shiki::Sprite(texture);
	const float width = static_cast<float>(texture->getWidth());
	const float height = static_cast<float>(texture->getHeight());
	item.sprite.setSourceRect({0.0f, 0.0f, width, height});
	item.sprite.setOrigin({width * 0.5f, height * 0.5f});
	item.sprite.setPosition(x, y);
	item.x = x;
	item.y = y;
	item.type = type;
	item.state = itemState;
	if (itemState == 2) {
		item.startX = x;
		item.startY = y;
		item.targetX = static_cast<float>(std::rand()) /
		                   static_cast<float>(RAND_MAX) * 288.0f +
		               48.0f;
		item.targetY = static_cast<float>(std::rand()) /
		                   static_cast<float>(RAND_MAX) * 192.0f -
		               64.0f;
	}
	state.items.push_back(std::move(item));
}

void beginEnemyBulletDespawn(GameState &state, bool createPoints) {
	for (auto &bullet : state.enemyBullets) {
		if (bullet.despawning)
			continue;
		const auto position = bullet.sprite.getPosition();
		if (createPoints)
			spawnTH06Item(state, TH06_ITEM_POINT_BULLET, position.x, position.y,
			              1);
		bullet.despawning = true;
		bullet.despawnFrames = 0;
		if (state.resourceManager) {
			static constexpr std::array<int, 16> OFFSETS_16PX = {
			    0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 0};
			static constexpr std::array<int, 8> OFFSETS_32PX = {0, 1, 1, 2,
			                                                    2, 3, 4, 0};
			const int color = std::abs(bullet.bulletColor) & 0xf;
			const char *atlas = bullet.bulletType == 9 ? "etama4" : "etama3";
			int effectSprite = 0;
			if (bullet.bulletType <= 5)
				effectSprite = 130 + OFFSETS_16PX[static_cast<size_t>(color)];
			else if (bullet.bulletType == 7)
				effectSprite = 131;
			else if (bullet.bulletType <= 8)
				effectSprite =
				    130 + OFFSETS_32PX[static_cast<size_t>(color & 7)];
			auto texture =
			    state.resourceManager->getSpriteTexture(atlas, effectSprite);
			if (texture && texture->isValid()) {
				bullet.despawnSprite = shiki::Sprite(texture);
				const float width = static_cast<float>(texture->getWidth());
				const float height = static_cast<float>(texture->getHeight());
				bullet.despawnSprite.setSourceRect({0.0f, 0.0f, width, height});
				bullet.despawnSprite.setOrigin({width * 0.5f, height * 0.5f});
				bullet.despawnSprite.setPosition(position);
				const float initialScale = bullet.bulletType == 0   ? 1.0f
				                           : bullet.bulletType <= 5 ? 1.5f
				                           : bullet.bulletType <= 8 ? 3.0f
				                                                    : 1.0f;
				bullet.despawnSprite.setScale(initialScale, initialScale);
				bullet.despawnSprite.setBlendMode(
				    bullet.bulletType == 9 ? shiki::BlendMode::Add
				                           : shiki::BlendMode::Alpha);
			}
		}
		bullet.spawnAnimation.state = TH06BulletSpawnState::Fired;
		bullet.fastSpawnFrames = 0;
	}
}

void turnEnemyBulletsIntoPoints(GameState &state) {
	beginEnemyBulletDespawn(state, true);
}

void turnEnemyProjectilesIntoPoints(GameState &state) {
	turnEnemyBulletsIntoPoints(state);
	for (auto &enemy : state.eclEnemies) {
		for (auto &laser : enemy.lasers) {
			if (!laser.active || laser.cancelFrames >= 0)
				continue;
			const float step = 32.0f;
			for (float offset = laser.startOffset; offset < laser.endOffset;
			     offset += step) {
				spawnTH06Item(state, TH06_ITEM_POINT_BULLET,
				              laser.x + std::cos(laser.angle) * offset,
				              laser.y + std::sin(laser.angle) * offset, 1);
			}
			laser.hitboxEndDelay = 0;
			laser.state = 2;
			laser.stateTimer = 0;
			laser.cancelFrames = 0;
		}
	}
}

void updateTH06ItemsTick(GameState &state) {
	static constexpr std::array<int, 31> POWER_ITEM_SCORE = {
	    10,   20,   30,   40,   50,   60,    70,    80,    90,   100,  200,
	    300,  400,  500,  600,  700,  800,   900,   1000,  2000, 3000, 4000,
	    5000, 6000, 7000, 8000, 9000, 10000, 11000, 12000, 51200};
	const auto playerPosition = state.player.getPosition();
	bool collectedAny = false;
	bool turnBulletsIntoPoints = false;
	for (auto &item : state.items) {
		if (!item.active)
			continue;

		if (item.state == 2 && item.timerFrames < 60) {
			const float progress = static_cast<float>(item.timerFrames) / 60.0f;
			item.x = item.startX * (1.0f - progress) + item.targetX * progress;
			item.y = item.startY * (1.0f - progress) + item.targetY * progress;
		} else {
			if (item.state == 2 && item.timerFrames == 60) {
				item.startX = 0.0f;
				item.startY = 0.0f;
			} else if (item.state != 2 &&
			           (item.state == 1 ||
			            (state.power >= 128 && playerPosition.y < 128.0f))) {
				const float angle = std::atan2(playerPosition.y - item.y,
				                               playerPosition.x - item.x);
				item.startX = std::cos(angle) * 8.0f;
				item.startY = std::sin(angle) * 8.0f;
				item.state = 1;
			} else if (item.state != 2) {
				item.startX = 0.0f;
				item.startY = std::max(item.startY, -2.2f);
			}
			item.x += item.startX;
			item.y += item.startY;
			if (item.y >= GAME_HEIGHT + 16.0f) {
				item.active = false;
				continue;
			}
			item.startY = std::min(item.startY + 0.03f, 3.0f);
		}

		item.sprite.setPosition(item.x, item.y);
		const bool collected = std::abs(playerPosition.x - item.x) <= 20.0f &&
		                       std::abs(playerPosition.y - item.y) <= 20.0f;
		if (collected) {
			switch (item.type) {
			case TH06_ITEM_POWER_SMALL:
			case TH06_ITEM_POWER_BIG: {
				const int amount = item.type == TH06_ITEM_POWER_SMALL ? 1 : 8;
				if (state.power >= 128) {
					state.powerItemCountForScore =
					    std::min(30, state.powerItemCountForScore + amount);
					addTH06Score(state, POWER_ITEM_SCORE[static_cast<size_t>(
					                        state.powerItemCountForScore)]);
				} else {
					state.power = std::min(128, state.power + amount);
					state.powerItemCountForScore = 0;
					addTH06Score(state, 10);
					if (state.power == 128)
						turnBulletsIntoPoints = true;
				}
				break;
			}
			case TH06_ITEM_POINT:
				addTH06Score(state, getTH06PointItemScore(4, item.y));
				++state.pointItemsCollected;
				break;
			case TH06_ITEM_BOMB:
				state.bombs = std::min(8, state.bombs + 1);
				break;
			case TH06_ITEM_FULL_POWER:
				if (state.power < 128)
					turnBulletsIntoPoints = true;
				state.power = 128;
				addTH06Score(state, 1000);
				break;
			case TH06_ITEM_LIFE:
				state.lives = std::min(8, state.lives + 1);
				// The original collection rule uses SOUND_1UP (extend.wav),
				// not the boss laser sound at index 16.
				playTH06Sound(state, 28);
				break;
			case TH06_ITEM_POINT_BULLET:
				addTH06Score(state, state.bombFrame >= 0
				                        ? 100
				                        : (state.graze / 3) * 10 + 500);
				break;
			default:
				break;
			}
			item.active = false;
			collectedAny = true;
		}
		++item.timerFrames;
	}
	if (collectedAny)
		playTH06Sound(state, 21);
	state.items.erase(std::remove_if(state.items.begin(), state.items.end(),
	                                 [](const GameState::Item &item) {
		                                 return !item.active;
	                                 }),
	                  state.items.end());
	if (turnBulletsIntoPoints)
		turnEnemyBulletsIntoPoints(state);
}

// Calculate the playfield position and size within the window.
// TH06 draws a 384x448 playfield inside its 640x480 logical canvas.
void calculateGameLayout(GameState &state, int windowWidth, int windowHeight) {
	state.windowWidth = windowWidth;
	state.windowHeight = windowHeight;

	const float canvasScale =
	    std::min(static_cast<float>(windowWidth) / TH06_CANVAS_WIDTH,
	             static_cast<float>(windowHeight) / TH06_CANVAS_HEIGHT);
	const float canvasWidth = TH06_CANVAS_WIDTH * canvasScale;
	const float canvasHeight = TH06_CANVAS_HEIGHT * canvasScale;
	state.gameScreenWidth = GAME_WIDTH * canvasScale;
	state.gameScreenHeight = GAME_HEIGHT * canvasScale;

	state.gameScreenX = (static_cast<float>(windowWidth) - canvasWidth) * 0.5f +
	                    32.0f * canvasScale;
	state.gameScreenY =
	    (static_cast<float>(windowHeight) - canvasHeight) * 0.5f +
	    16.0f * canvasScale;
}

// Convert gameplay coordinates to screen coordinates.
shiki::Vec2 gameToScreen(const GameState &state, float x, float y) {
	float scaleX = state.gameScreenWidth / GAME_WIDTH;
	float scaleY = state.gameScreenHeight / GAME_HEIGHT;
	return shiki::Vec2(state.gameScreenX + x * scaleX,
	                   state.gameScreenY + y * scaleY);
}

ECLEnemy *findHomingTarget(GameState &state, const shiki::Vec2 &origin) {
	ECLEnemy *target = nullptr;
	float closestDistanceSquared = std::numeric_limits<float>::max();
	for (auto &enemy : state.eclEnemies) {
		if (!enemy.alive || !enemy.interactable || !enemy.damageable)
			continue;
		if (enemy.isBoss)
			return &enemy;
		const float dx = enemy.x - origin.x;
		const float dy = enemy.y - origin.y;
		const float distanceSquared = dx * dx + dy * dy;
		if (distanceSquared < closestDistanceSquared) {
			closestDistanceSquared = distanceSquared;
			target = &enemy;
		}
	}
	return target;
}

void spawnReimuABullet(GameState &state,
                       const std::shared_ptr<shiki::Texture> &texture,
                       const shiki::Vec2 &position, float angleDegrees,
                       float speedPerFrame, int damage, bool homing,
                       float visualScale = 1.5f, float alphaOverride = -1.0f,
                       bool autoRotate = false) {
	if (!texture)
		return;
	GameState::PlayerBullet bullet;
	bullet.sprite = shiki::Sprite(texture);
	const float width = static_cast<float>(texture->getWidth());
	const float height = static_cast<float>(texture->getHeight());
	bullet.sprite.setSourceRect({0.0f, 0.0f, width, height});
	bullet.sprite.setOrigin({width * 0.5f, height * 0.5f});
	bullet.sprite.setPosition(position);
	bullet.sprite.setScale(visualScale, visualScale);
	const float alpha = alphaOverride >= 0.0f
	                        ? alphaOverride
	                        : (homing ? 96.0f / 255.0f : 128.0f / 255.0f);
	bullet.sprite.setColor({1.0f, 1.0f, 1.0f, alpha});
	// Keep the authored vertical shot exactly vertical.  The original data
	// uses -90 degrees, and preserving a tiny cosine residue can accumulate
	// into a visible lateral drift over a long flight.
	const auto velocity = th06PlayerShotVelocity(angleDegrees, speedPerFrame);
	bullet.vx = velocity.x;
	bullet.vy = velocity.y;
	bullet.speed = speedPerFrame * 60.0f;
	bullet.damage = damage;
	bullet.homing = homing;
	bullet.autoRotate = autoRotate;
	if (autoRotate)
		bullet.sprite.setRotation(-angleDegrees - 90.0f);
	state.playerBullets.push_back(std::move(bullet));
}

int th06PlayerPowerRank(int power) {
	static constexpr std::array<int, 9> THRESHOLDS = {0,  8,  16, 32, 48,
	                                                  64, 80, 96, 127};
	int rank = 0;
	for (int index = 0; index < static_cast<int>(THRESHOLDS.size()); ++index)
		if (power >= THRESHOLDS[static_cast<size_t>(index)])
			rank = index;
	return rank;
}

void fireReimuA(GameState &state) {
	static const std::array<std::vector<TH06PlayerShotSpec>, 9> PATTERNS = {{
	    {TH06_REIMU_A_ZERO_POWER_SHOT},
	    {{5, 0, 0, -90, 12, 48, 0, true},
	     {30, 0, 0, -120, 10, 14, 1, false},
	     {30, 0, 0, -60, 10, 14, 2, false}},
	    {{5, 0, -4, -91, 12, 30, 0, false},
	     {5, 0, 4, -89, 12, 30, 0, false},
	     {30, 0, 0, -120, 10, 14, 1, false},
	     {30, 0, 0, -60, 10, 14, 2, false}},
	    {{5, 0, 0, -96, 12, 24, 0, true},
	     {5, 0, 0, -90, 12, 30, 0, false},
	     {5, 0, 0, -84, 12, 24, 0, false},
	     {30, 0, 0, -120, 10, 14, 1, false},
	     {30, 0, 0, -60, 10, 14, 2, false}},
	    {{5, 0, 0, -97, 12, 24, 0, true},
	     {5, 0, 0, -90, 12, 30, 0, false},
	     {5, 0, 0, -83, 12, 24, 0, false},
	     {15, 0, 0, -120, 10, 12, 1, false},
	     {15, 0, 0, -60, 10, 12, 2, false}},
	    {{5, 0, 0, -97, 12, 24, 0, true},
	     {5, 0, 0, -90, 12, 29, 0, false},
	     {5, 0, 0, -83, 12, 24, 0, false},
	     {15, 0, 0, -120, 10, 9, 1, false},
	     {15, 0, 0, -60, 10, 9, 2, false},
	     {30, 0, 0, -150, 10, 12, 1, false},
	     {30, 0, 0, -30, 10, 12, 2, false}},
	    {{5, 0, 0, -97, 12, 24, 0, true},
	     {5, 0, 0, -90, 12, 28, 0, false},
	     {5, 0, 0, -83, 12, 24, 0, false},
	     {30, 0, 0, -110, 10, 10, 1, false},
	     {30, 0, 0, -70, 10, 10, 2, false},
	     {30, 10, 0, -130, 10, 9, 1, false},
	     {30, 10, 0, -50, 10, 9, 2, false},
	     {30, 20, 0, -150, 10, 11, 1, false},
	     {30, 20, 0, -30, 10, 11, 2, false}},
	    {{5, 0, 0, -97, 12, 24, 0, true},
	     {5, 0, 0, -90, 12, 28, 0, false},
	     {5, 0, 0, -83, 12, 24, 0, false},
	     {15, 0, 0, -110, 10, 8, 1, false},
	     {15, 0, 0, -70, 10, 8, 2, false},
	     {15, 5, 0, -130, 10, 8, 1, false},
	     {15, 5, 0, -50, 10, 8, 2, false},
	     {15, 10, 0, -150, 10, 8, 1, false},
	     {15, 10, 0, -30, 10, 8, 2, false}},
	    {{5, 0, -8, -97, 12, 23, 0, true},
	     {5, 0, -8, -90, 12, 24, 0, false},
	     {5, 0, 8, -90, 12, 24, 0, false},
	     {5, 0, 8, -83, 12, 23, 0, false},
	     {16, 0, 0, -110, 10, 10, 1, false},
	     {16, 0, 0, -70, 10, 10, 2, false},
	     {16, 4, 0, -130, 10, 8, 1, false},
	     {16, 4, 0, -50, 10, 8, 2, false},
	     {16, 8, 0, -150, 10, 7, 1, false},
	     {16, 8, 0, -30, 10, 7, 2, false},
	     {16, 12, 0, -170, 10, 10, 1, false},
	     {16, 12, 0, -10, 10, 10, 2, false}},
	}};

	const auto playerPosition = state.player.getPosition();
	const auto &pattern = PATTERNS[static_cast<size_t>(
	    std::clamp(th06PlayerPowerRank(state.power), 0, 8))];
	for (const auto &shot : pattern) {
		if (state.shootingFrame % shot.interval != shot.frame)
			continue;
		const shiki::Vec2 origin =
		    shot.option == 0
		        ? shiki::Vec2{playerPosition.x + shot.offsetX, playerPosition.y}
		        : shiki::Vec2{state.playerOptionPositions[shot.option - 1].x +
		                          shot.offsetX,
		                      state.playerOptionPositions[shot.option - 1].y};
		spawnReimuABullet(
		    state,
		    shot.option == 0 ? state.bulletTexture : state.homingBulletTexture,
		    origin, shot.angle, shot.speed, shot.damage, shot.option != 0);
		if (shot.sound)
			playTH06Sound(state, 0);
	}
}

void fireMarisaB(GameState &state) {
	const auto playerPos = state.player.getPosition();
	const int powerRank = th06PlayerPowerRank(state.power);
	struct MainShot {
		float offsetX;
		float angle;
		int damage;
	};
	static const std::array<std::vector<MainShot>, 9> MAIN_PATTERNS = {{
	    {{TH06_MARISA_B_ZERO_POWER_SHOT.offsetX,
	      TH06_MARISA_B_ZERO_POWER_SHOT.angle,
	      TH06_MARISA_B_ZERO_POWER_SHOT.damage}},
	    {{0, -90, 32}},
	    {{0, -90, 32}},
	    {{-8, -92, 22}, {8, -88, 22}},
	    {{-8, -92, 22}, {8, -88, 22}},
	    {{-8, -92, 20}, {8, -88, 20}},
	    {{0, -95, 15}, {0, -90, 20}, {0, -85, 15}},
	    {{0, -95, 15}, {0, -90, 20}, {0, -85, 15}},
	    {{0, -100, 12}, {0, -95, 15}, {0, -90, 20}, {0, -85, 15}, {0, -80, 12}},
	}};
	if (state.shootingFrame % 5 == 0) {
		for (const auto &shot : MAIN_PATTERNS[static_cast<size_t>(powerRank)])
			spawnReimuABullet(state, state.marisaBulletTexture,
			                  {playerPos.x + shot.offsetX, playerPos.y - 8.0f},
			                  shot.angle, 12.0f, shot.damage, false, 1.0f,
			                  128.0f / 255.0f, true);
		playTH06Sound(state, 0);
	}

	if (powerRank < 1)
		return;
	static constexpr std::array<int, 9> LASER_DURATIONS = {
	    0, 120, 170, 200, 210, 230, 250, 270, 330};
	static constexpr std::array<int, 9> LASER_DAMAGES = {0, 3, 3, 3, 3,
	                                                     4, 4, 5, 6};
	for (int index = 0; index < 2; ++index) {
		if (state.marisaLaserTimers[static_cast<size_t>(index)] != 0 ||
		    !state.marisaPlayerAnm)
			continue;
		GameState::PlayerBullet laser;
		if (!laser.anmVm.initialize(state.marisaPlayerAnm, 71))
			continue;
		laser.anmVm.visible = true;
		if (!applyTH06AnmVmSprite(state, laser.anmVm, laser.sprite))
			continue;
		const auto optionPosition =
		    state.playerOptionPositions[static_cast<size_t>(index)];
		laser.sprite.setPosition(optionPosition.x, optionPosition.y * 0.5f);
		laser.sprite.setScale(laser.anmVm.scaleX, optionPosition.y / 14.0f);
		laser.damage = LASER_DAMAGES[static_cast<size_t>(powerRank)];
		laser.laser = true;
		laser.laserTimerIndex = index;
		state.marisaLaserTimers[static_cast<size_t>(index)] =
		    LASER_DURATIONS[static_cast<size_t>(powerRank)];
		state.playerBullets.push_back(std::move(laser));
	}
}

void beginTH06CardUi(GameState &state, bool player, std::string name,
                     std::shared_ptr<shiki::Texture> portrait) {
	state.cardUi.name = std::move(name);
	state.cardUi.portrait = std::move(portrait);
	state.cardUi.frame = 0;
	state.cardUi.endFrame = -1;
	state.cardUi.player = player;
	state.cardUi.active = true;
}

void endTH06CardUi(GameState &state, bool player) {
	if (!state.cardUi.active || state.cardUi.player != player ||
	    state.cardUi.endFrame >= 0)
		return;
	// Gui::EndPlayerSpellcard/EndEnemySpellcard sends interrupt 1 to the
	// corresponding text.anm VM. The name then exits over exactly 30 frames.
	state.cardUi.endFrame = 0;
}

void updateTH06CardUiTick(GameState &state) {
	if (!state.cardUi.active)
		return;
	++state.cardUi.frame;
	if (state.cardUi.endFrame >= 0 && ++state.cardUi.endFrame >= 30)
		state.cardUi = {};
}

void registerTH06ScreenShake(GameState &state, int duration, float start,
                             float end) {
	state.screenShakes.push_back({0, duration, start, end});
}

int nextTH06ScreenShakeAxis(GameState &state) {
	state.screenShakeRng = state.screenShakeRng * 0x343fdu + 0x269ec3u;
	return static_cast<int>((state.screenShakeRng >> 16) & 0x7fffu) % 3;
}

void updateTH06ScreenShakesTick(GameState &state) {
	state.playfieldRegion = {32.0f, 16.0f, 384.0f, 448.0f};
	for (auto &shake : state.screenShakes) {
		if (++shake.frame >= shake.duration)
			continue;
		const float offset =
		    std::lerp(shake.start, shake.end,
		              shake.frame / static_cast<float>(shake.duration));
		switch (nextTH06ScreenShakeAxis(state)) {
		case 1:
			state.playfieldRegion.x = 32.0f + offset;
			state.playfieldRegion.width = 384.0f - offset;
			break;
		case 2:
			state.playfieldRegion.width = 384.0f - offset;
			break;
		default:
			break;
		}
		switch (nextTH06ScreenShakeAxis(state)) {
		case 1:
			state.playfieldRegion.y = 16.0f + offset;
			state.playfieldRegion.height = 448.0f - offset;
			break;
		case 2:
			state.playfieldRegion.height = 448.0f - offset;
			break;
		default:
			break;
		}
	}
	std::erase_if(state.screenShakes, [](const GameState::ScreenShake &shake) {
		return shake.frame >= shake.duration;
	});
}

void startReimuABomb(GameState &state, int bombCost) {
	state.spellCaptureEligible = false;
	turnEnemyProjectilesIntoPoints(state);
	const auto playerPosition = state.player.getPosition();
	state.effects.spawn(12, playerPosition.x, playerPosition.y, 1, 0xff4040ff);
	state.bombFrame = 0;
	state.deathbombFrame = -1;
	state.invincibleTimer = 360.0f / 60.0f;
	for (auto &orb : state.bombOrbs)
		orb = {};
	state.bombs -= bombCost;
	state.bombCooldown = BOMB_COOLDOWN;
	beginTH06CardUi(state, true, "霊符「夢想封印」",
	                state.resourceManager
	                    ? state.resourceManager->getSpriteTexture("face00a", 1)
	                    : nullptr);
	playTH06Sound(state, 14);
	spdlog::info("Reimu A bomb started; bombs left: {}", state.bombs);
}

void queueBombDamage(ECLEnemy &enemy, int damage);

void startMarisaBBomb(GameState &state, int bombCost) {
	state.spellCaptureEligible = false;
	turnEnemyProjectilesIntoPoints(state);
	state.bombFrame = 0;
	state.deathbombFrame = -1;
	state.invincibleTimer = 360.0f / 60.0f;
	state.bombs -= bombCost;
	state.bombCooldown = BOMB_COOLDOWN;
	const auto bombAnm = loadTH06MenuAnmFile(state.resourceManager, "player01");
	for (size_t index = 0; index < state.marisaBombVms.size(); ++index) {
		state.marisaBombVms[index] = {};
		if (state.marisaBombVms[index].initialize(bombAnm,
		                                          8 + static_cast<int>(index)))
			state.marisaBombVms[index].visible = true;
	}
	beginTH06CardUi(state, true, "恋符「マスタースパーク」",
	                state.resourceManager
	                    ? state.resourceManager->getSpriteTexture("face01a", 1)
	                    : nullptr);
	playTH06Sound(state, 19);
}

void updateMarisaBBombTick(GameState &state) {
	if (state.bombFrame < 0)
		return;
	const auto position = state.player.getPosition();
	for (auto &vm : state.marisaBombVms)
		vm.tick();
	if (state.bombFrame == 60)
		registerTH06ScreenShake(state, 60, 1.0f, 7.0f);
	else if (state.bombFrame == 120)
		registerTH06ScreenShake(state, 200, 24.0f, 0.0f);
	if (state.bombFrame % 4 != 0) {
		for (auto &enemy : state.eclEnemies)
			queueBombDamage(enemy, 12);
	}
	turnEnemyProjectilesIntoPoints(state);
	if (++state.bombFrame >= 300) {
		state.bombFrame = -1;
		state.marisaBombVms = {};
		endTH06CardUi(state, true);
	}
}

void queueBombDamage(ECLEnemy &enemy, int damage) {
	if (!enemy.alive || !enemy.interactable || !enemy.damageable)
		return;
	enemy.pendingPlayerDamage += damage;
	enemy.pendingDamageFromBomb = true;
}

void updateReimuABombTick(GameState &state) {
	if (state.bombFrame < 0)
		return;

	if (state.bombFrame >= 60 && state.bombFrame < 180 &&
	    (state.bombFrame - 60) % 16 == 0) {
		const size_t index = static_cast<size_t>((state.bombFrame - 60) / 16);
		if (index < state.bombOrbs.size()) {
			auto &orb = state.bombOrbs[index];
			orb.state = 1;
			orb.position = state.player.getPosition();
			for (size_t layer = 0; layer < orb.vms.size(); ++layer) {
				orb.vms[layer] = {};
				if (orb.vms[layer].initialize(state.reimuPlayerAnm,
				                              133 + static_cast<int>(layer)))
					orb.vms[layer].visible = true;
			}
			// BombReimuACalc uses one random angle in [-pi, pi] for each orb.
			const float angle =
			    static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX) *
			    (2.0f * std::numbers::pi_v<float>)-std::numbers::pi_v<float>;
			orb.vx = std::cos(angle) * 4.0f;
			orb.vy = std::sin(angle) * 4.0f;
			playTH06Sound(state, 13);
		}
	}

	for (auto &orb : state.bombOrbs) {
		if (orb.state == 0)
			continue;
		auto position = orb.position;
		if (orb.state == 1) {
			if (auto *target = findHomingTarget(state, position)) {
				float dx = target->x - position.x;
				float dy = target->y - position.y;
				float divisor =
				    std::sqrt(dx * dx + dy * dy) / (orb.speed / 8.0f);
				divisor = std::max(divisor, 1.0f);
				dx = dx / divisor + orb.vx;
				dy = dy / divisor + orb.vy;
				const float length =
				    std::max(std::sqrt(dx * dx + dy * dy), 0.001f);
				orb.speed = std::clamp(length, 1.0f, 10.0f);
				orb.vx = dx * orb.speed / length;
				orb.vy = dy * orb.speed / length;
			}
			position.x += orb.vx;
			position.y += orb.vy;
			orb.position = position;

			for (auto &enemy : state.eclEnemies) {
				const float dx = enemy.x - position.x;
				const float dy = enemy.y - position.y;
				if (enemy.alive && enemy.interactable && enemy.damageable &&
				    dx * dx + dy * dy <= 24.0f * 24.0f) {
					queueBombDamage(enemy, 8);
					orb.accumulatedDamage += 8;
				}
			}

			if (orb.accumulatedDamage >= 100 || state.bombFrame >= 270) {
				orb.state = 2;
				orb.explosionFrames = 30;
				state.effects.spawn(6, position.x, position.y, 8, 0xffffffff);
				state.effects.spawn(12, position.x, position.y, 1, 0xff4040ff);
				for (auto &vm : orb.vms)
					vm.interrupt(1);
				for (auto &enemy : state.eclEnemies) {
					const float dx = enemy.x - position.x;
					const float dy = enemy.y - position.y;
					if (enemy.alive && enemy.interactable && enemy.damageable &&
					    dx * dx + dy * dy <= 128.0f * 128.0f)
						queueBombDamage(enemy, 200);
				}
				playTH06Sound(state, 15);
				registerTH06ScreenShake(state, 16, 8.0f, 0.0f);
			}
		} else {
			--orb.explosionFrames;
			if (orb.explosionFrames <= 0)
				orb.state = 0;
		}
		for (auto &vm : orb.vms)
			vm.tick();
	}

	// TH06 bomb collision regions remain active for the whole 300-frame bomb,
	// so newly spawned bullets are cancelled on every tick rather than once.
	turnEnemyProjectilesIntoPoints(state);

	if (++state.bombFrame >= 300) {
		state.bombFrame = -1;
		endTH06CardUi(state, true);
	}
}

void updatePlayerOptionsTick(GameState &state) {
	float horizontalOffset = 24.0f;
	float verticalOffset = 0.0f;
	auto &optionState = state.playerOptionState;
	auto &transitionFrame = state.playerOptionTransitionFrame;

	if (optionState == GameState::PlayerOptionState::Unfocused &&
	    state.isFocused) {
		optionState = GameState::PlayerOptionState::Focusing;
		transitionFrame = 0;
	} else if (optionState == GameState::PlayerOptionState::Focused &&
	           !state.isFocused) {
		optionState = GameState::PlayerOptionState::Unfocusing;
		transitionFrame = 0;
	} else if (optionState == GameState::PlayerOptionState::Focusing &&
	           !state.isFocused) {
		optionState = GameState::PlayerOptionState::Unfocusing;
		transitionFrame = 8 - transitionFrame;
	} else if (optionState == GameState::PlayerOptionState::Unfocusing &&
	           state.isFocused) {
		optionState = GameState::PlayerOptionState::Focusing;
		transitionFrame = 8 - transitionFrame;
	}

	if (optionState == GameState::PlayerOptionState::Focusing) {
		transitionFrame = std::min(8, transitionFrame + 1);
		const float progress = transitionFrame / 8.0f;
		horizontalOffset = 24.0f - 16.0f * progress * progress;
		verticalOffset = -32.0f * progress;
		if (transitionFrame >= 8)
			optionState = GameState::PlayerOptionState::Focused;
	} else if (optionState == GameState::PlayerOptionState::Unfocusing) {
		transitionFrame = std::min(8, transitionFrame + 1);
		const float progress = transitionFrame / 8.0f;
		horizontalOffset = 8.0f + 16.0f * progress * progress;
		verticalOffset = -32.0f + 32.0f * progress;
		if (transitionFrame >= 8)
			optionState = GameState::PlayerOptionState::Unfocused;
	} else if (optionState == GameState::PlayerOptionState::Focused) {
		horizontalOffset = 8.0f;
		verticalOffset = -32.0f;
	}

	const auto playerPosition = state.player.getPosition();
	state.playerOptionPositions[0] = {playerPosition.x - horizontalOffset,
	                                  playerPosition.y + verticalOffset};
	state.playerOptionPositions[1] = {playerPosition.x + horizontalOffset,
	                                  playerPosition.y + verticalOffset};
	for (auto &vm : state.playerOptionVms)
		vm.tick();
}

void updateMarisaLasersTick(GameState &state) {
	for (auto &timer : state.marisaLaserTimers)
		if (timer > 0)
			--timer;

	for (auto &bullet : state.playerBullets) {
		if (!bullet.active || !bullet.laser || bullet.laserTimerIndex < 0)
			continue;
		const size_t index = static_cast<size_t>(bullet.laserTimerIndex);
		const int timer = state.marisaLaserTimers[index];
		if (timer == 70 || timer == 1)
			bullet.anmVm.interrupt(1);
		bullet.anmVm.tick();
		if (!bullet.anmVm.visible && bullet.anmVm.stopped) {
			bullet.active = false;
			continue;
		}

		if (!applyTH06AnmVmSprite(state, bullet.anmVm, bullet.sprite)) {
			bullet.active = false;
			continue;
		}
		// player01.anm script 71 tints the beam red. The extracted laser
		// texture already carries the intended color; retain only the script's
		// alpha fade.
		const float laserAlpha = bullet.sprite.getColor().w;
		bullet.sprite.setColor({1.0f, 1.0f, 1.0f, laserAlpha});
		const auto optionPosition = state.playerOptionPositions[index];
		bullet.sprite.setPosition(optionPosition.x, optionPosition.y * 0.5f);
		bullet.sprite.setScale(bullet.anmVm.scaleX, optionPosition.y / 14.0f);
		bullet.damageReady = true;
	}
}

void updateReimuATick(GameState &state) {
	updatePlayerOptionsTick(state);
	if (state.playerCharacter == 1)
		updateMarisaLasersTick(state);
	if (state.playerDeathTimer < 0 && state.keyShoot && state.bombFrame < 0 &&
	    state.deathbombFrame < 0 && !state.dialogue.active) {
		if (state.playerCharacter == 0)
			fireReimuA(state);
		else
			fireMarisaB(state);
		++state.shootingFrame;
	} else {
		state.shootingFrame = 0;
	}

	for (auto &bullet : state.playerBullets) {
		if (!bullet.active)
			continue;
		if (bullet.laser)
			continue;
		auto position = bullet.sprite.getPosition();
		if (bullet.homing && bullet.ageFrames < 40) {
			if (auto *target = findHomingTarget(state, position)) {
				float dx = target->x - position.x;
				float dy = target->y - position.y;
				const float speedPerFrame = bullet.speed / 60.0f;
				float divisor = std::sqrt(dx * dx + dy * dy) /
				                std::max(speedPerFrame / 4.0f, 0.001f);
				divisor = std::max(divisor, 1.0f);
				dx = dx / divisor + bullet.vx / 60.0f;
				dy = dy / divisor + bullet.vy / 60.0f;
				const float length =
				    std::max(std::sqrt(dx * dx + dy * dy), 0.001f);
				const float newSpeed = std::clamp(length, 1.0f, 10.0f);
				bullet.speed = newSpeed * 60.0f;
				bullet.vx = dx * bullet.speed / length;
				bullet.vy = dy * bullet.speed / length;
			}
		}
		position.x += bullet.vx / 60.0f;
		position.y += bullet.vy / 60.0f;
		bullet.sprite.setPosition(position);
		if (bullet.autoRotate) {
			const float direction = std::atan2(bullet.vy, bullet.vx);
			bullet.sprite.setRotation(
			    -direction * 180.0f / std::numbers::pi_v<float> - 90.0f);
		}
		++bullet.ageFrames;
	}
	if (state.playerCharacter == 0)
		updateReimuABombTick(state);
	else
		updateMarisaBBombTick(state);
}

void beginDeathbombWindow(GameState &state) {
	if (state.deathbombFrame >= 0 || state.bombFrame >= 0)
		return;
	state.deathbombFrame = 0;
	playTH06Sound(state, 4);
}

void resolveMissedDeathbomb(GameState &state) {
	state.spellCaptureEligible = false;
	state.deathbombFrame = -1;
	const auto playerPosition = state.player.getPosition();
	state.effects.spawn(12, playerPosition.x, playerPosition.y, 1, 0xff4040ff);
	state.effects.spawn(6, playerPosition.x, playerPosition.y, 16, 0xffffffff);
	// Player::Die enters the dead state, while the respawn path clears bullets
	// during its grace period.  Keep the transition explicit instead of using
	// invincibility as a substitute for the death state.
	const bool hadReserveLife = state.lives > 0;
	if (!state.debugInfiniteLives && hadReserveLife)
		--state.lives;
	// Player.cpp restores bombsRemaining even when a debug option suppresses
	// the life decrement. Extra mode uses the fixed fallback value of three.
	state.bombs = 3;
	if (hadReserveLife || state.debugInfiniteLives)
		state.power =
		    state.debugMode ? 128 : (state.power <= 16 ? 0 : state.power - 16);
	state.invincibleTimer = 0.0f;
	state.playerBulletGraceFrames = 0;
	if (hadReserveLife || state.debugInfiniteLives) {
		// Continue through the normal six-frame death animation and respawn.
		state.playerDeathTimer = 0;
		state.isGameOver = false;
		return;
	}
	state.playerDeathTimer = -1;
	state.isGameOver = true;
	state.mainMenu.active = true;
	state.stageRestartRequested = true;
	state.mainMenu.screen = TH06MainMenuState::Screen::PressStart;
	state.mainMenu.frame = 0;
	loadTH06TitleMenuVms(state.mainMenu, state.resourceManager);
	if (state.audioManager)
		state.audioManager->stopMusic();
	spdlog::info("Player death returned to title menu; lives left: {}",
	             state.lives);
}

void setPlayerAnmScript(GameState &state, int scriptId) {
	const char *atlas = state.playerCharacter == 0 ? "player00" : "player01";
	state.playerAnmScript = loadTH06AnmScript(
	    state.resourceManager ? state.resourceManager->getAssetStore()
	                          : nullptr,
	    atlas, scriptId);
	state.playerAnmFrames = 0.0f;
	state.playerAnmScriptId = scriptId;
}

void updatePlayerAnm(GameState &state, float dt) {
	if (state.playerAnmScript.empty() || !state.resourceManager)
		return;
	state.playerAnmFrames += dt * 60.0f;
	const auto *frame =
	    sampleTH06AnmFrame(state.playerAnmScript, state.playerAnmFrames);
	if (!frame)
		return;
	const char *atlas = state.playerCharacter == 0 ? "player00" : "player01";
	auto texture =
	    state.resourceManager->getSpriteTexture(atlas, frame->sprite);
	if (!texture || !texture->isValid())
		return;
	if (texture != state.player.getTexture()) {
		state.player.setTexture(texture);
		const float width = static_cast<float>(texture->getWidth());
		const float height = static_cast<float>(texture->getHeight());
		state.player.setSourceRect({0.0f, 0.0f, width, height});
		state.player.setOrigin({width * 0.5f, height * 0.5f});
	}
	state.player.setScale(frame->flipX ? -1.0f : 1.0f, 1.0f);
}

void configurePlayerCharacter(GameState &state, int character) {
	state.playerCharacter = std::clamp(character, 0, 1);
	const char *atlas = state.playerCharacter == 0 ? "player00" : "player01";
	auto texture = state.resourceManager
	                   ? state.resourceManager->getSpriteTexture(atlas, 0)
	                   : nullptr;
	if (texture && texture->isValid()) {
		state.playerTexture = texture;
		state.player.setTexture(texture);
		const float width = static_cast<float>(texture->getWidth());
		const float height = static_cast<float>(texture->getHeight());
		state.player.setSourceRect({0.0f, 0.0f, width, height});
		state.player.setOrigin({width * 0.5f, height * 0.5f});
	}
	// Player.cpp g_CharData: Reimu A is 4/2 px per tick; Marisa B is 5/2.5.
	state.playerSpeed = state.playerCharacter == 0 ? 240.0f : 300.0f;
	state.player.setPosition(GAME_WIDTH * 0.5f, GAME_HEIGHT - 60.0f);
	state.previousHorizontalSpeed = 0.0f;
	state.playerOptionState = GameState::PlayerOptionState::Unfocused;
	state.playerOptionTransitionFrame = 0;
	state.marisaLaserTimers = {0, 0};
	state.playerBullets.erase(
	    std::remove_if(
	        state.playerBullets.begin(), state.playerBullets.end(),
	        [](const GameState::PlayerBullet &bullet) { return bullet.laser; }),
	    state.playerBullets.end());
	const auto optionAnm = state.playerCharacter == 0 ? state.reimuPlayerAnm
	                                                  : state.marisaPlayerAnm;
	for (size_t index = 0; index < state.playerOptionVms.size(); ++index) {
		state.playerOptionVms[index] = {};
		if (state.playerOptionVms[index].initialize(
		        optionAnm, 128 + static_cast<int>(index)))
			state.playerOptionVms[index].visible = true;
	}
	setPlayerAnmScript(state, 0);
}

void updateTH06DebugAiInput(GameState &state) {
	const auto playerPosition = state.player.getPosition();
	constexpr float LOOKAHEAD_SECONDS = 0.28f;
	constexpr int LOOKAHEAD_SAMPLES = 14;
	constexpr float PLAN_SECONDS = 0.56f;
	constexpr int PLAN_SAMPLES = 28;

	shiki::Vec2 combatTarget{GAME_WIDTH * 0.5f, GAME_HEIGHT - 56.0f};
	const ECLEnemy *attackTarget = nullptr;
	bool bossTarget = false;
	float closestEnemyDistance = std::numeric_limits<float>::max();
	for (const auto &enemy : state.eclEnemies) {
		if (!enemy.alive || !enemy.interactable || !enemy.damageable)
			continue;
		if (enemy.isBoss || enemy.bossId >= 0) {
			attackTarget = &enemy;
			bossTarget = true;
			break;
		}
		const float distance =
		    std::hypot(enemy.x - playerPosition.x, enemy.y - playerPosition.y);
		if (distance < closestEnemyDistance) {
			closestEnemyDistance = distance;
			attackTarget = &enemy;
		}
	}
	if (attackTarget) {
		combatTarget.x =
		    shiki::clamp(attackTarget->x, 24.0f, GAME_WIDTH - 24.0f);
		combatTarget.y =
		    shiki::clamp(attackTarget->y + 160.0f, 160.0f, GAME_HEIGHT - 56.0f);
	}

	shiki::Vec2 positiveTarget = combatTarget;
	float bestItemUtility = 80.0f;
	float visibleItemValue = 0.0f;
	bool criticalItemTarget = false;
	for (const auto &item : state.items) {
		if (!item.active || item.state == 1)
			continue;
		float value = 100.0f;
		switch (item.type) {
		case TH06_ITEM_LIFE:
			value = 2200.0f;
			break;
		case TH06_ITEM_BOMB:
			value = 2200.0f;
			break;
		case TH06_ITEM_FULL_POWER:
			value = state.power < 128 ? 650.0f : 80.0f;
			break;
		case TH06_ITEM_POWER_BIG:
			value = state.power < 128 ? 360.0f : 70.0f;
			break;
		case TH06_ITEM_POWER_SMALL:
			value = state.power < 128 ? 180.0f : 60.0f;
			break;
		case TH06_ITEM_POINT:
			value = 150.0f;
			break;
		case TH06_ITEM_POINT_BULLET:
			value = 45.0f;
			break;
		default:
			break;
		}
		visibleItemValue += value;
		const float distance =
		    std::hypot(item.x - playerPosition.x, item.y - playerPosition.y);
		const float urgency = item.y > GAME_HEIGHT - 96.0f ? 100.0f : 0.0f;
		const float utility = value + urgency - distance;
		if (utility > bestItemUtility) {
			bestItemUtility = utility;
			positiveTarget = {item.x, item.y};
			criticalItemTarget =
			    item.type == TH06_ITEM_LIFE || item.type == TH06_ITEM_BOMB;
		}
	}

	int nearbyBulletCount = 0;
	for (const auto &bullet : state.enemyBullets) {
		if (bullet.despawning || bullet.fastSpawnFrames > 0 ||
		    bullet.spawnAnimation.active())
			continue;
		const auto position = bullet.sprite.getPosition();
		if (std::hypot(position.x - playerPosition.x,
		               position.y - playerPosition.y) < 160.0f)
			++nearbyBulletCount;
	}
	bool activeLaser = false;
	for (const auto &enemy : state.eclEnemies)
		for (const auto &laser : enemy.lasers)
			activeLaser |= laser.active && laser.cancelFrames < 0 &&
			               laser.ageFrames >= laser.hitboxStartTime;
	const bool sparseStage =
	    !bossTarget && nearbyBulletCount < 10 && !activeLaser;
	const bool collectionOutweighsCombat = visibleItemValue >= 600.0f;
	if ((sparseStage || collectionOutweighsCombat) && !criticalItemTarget)
		positiveTarget = {combatTarget.x, 112.0f};

	const auto clearanceAt = [&](const shiki::Vec2 &player, float seconds) {
		// Treat the playfield border as another obstacle. Without this margin,
		// an open corner looks artificially optimal because threats can only
		// approach it from part of the surrounding plane.
		float safety =
		    std::min({player.x - 8.0f, GAME_WIDTH - 8.0f - player.x,
		              player.y - 16.0f, GAME_HEIGHT - 16.0f - player.y});
		for (const auto &bullet : state.enemyBullets) {
			if (bullet.despawning || bullet.fastSpawnFrames > 0 ||
			    bullet.spawnAnimation.active())
				continue;
			const auto bulletPosition = bullet.sprite.getPosition();
			const auto bulletSize =
			    getTH06BulletCollisionSize(bullet.bulletType);
			const float outsideX =
			    std::abs(player.x - (bulletPosition.x + bullet.vx * seconds)) -
			    bulletSize.x * 0.5f - TH06_PLAYER_HITBOX_HALF_SIZE;
			const float outsideY =
			    std::abs(player.y - (bulletPosition.y + bullet.vy * seconds)) -
			    bulletSize.y * 0.5f - TH06_PLAYER_HITBOX_HALF_SIZE;
			const float clearance = outsideX <= 0.0f && outsideY <= 0.0f
			                            ? std::max(outsideX, outsideY)
			                            : std::hypot(std::max(outsideX, 0.0f),
			                                         std::max(outsideY, 0.0f));
			safety = std::min(safety, clearance);
		}

		for (const auto &enemy : state.eclEnemies) {
			if (!enemy.alive)
				continue;
			for (const auto &laser : enemy.lasers) {
				if (!laser.active || laser.cancelFrames >= 0 ||
				    laser.ageFrames < laser.hitboxStartTime)
					continue;
				const float dx = player.x - laser.x;
				const float dy = player.y - laser.y;
				const float along =
				    dx * std::cos(laser.angle) + dy * std::sin(laser.angle);
				const float across =
				    -dx * std::sin(laser.angle) + dy * std::cos(laser.angle);
				const float outsideAlong = std::max(laser.startOffset - along,
				                                    along - laser.endOffset);
				const float outsideAcross = std::abs(across) -
				                            laser.width * 0.5f -
				                            TH06_PLAYER_HITBOX_HALF_SIZE;
				const float clearance =
				    outsideAlong <= 0.0f && outsideAcross <= 0.0f
				        ? std::max(outsideAlong, outsideAcross)
				        : std::hypot(std::max(outsideAlong, 0.0f),
				                     std::max(outsideAcross, 0.0f));
				safety = std::min(safety, clearance);
			}
		}
		return safety;
	};
	const auto pathSafety = [&](const shiki::Vec2 &destination) {
		float safety = std::numeric_limits<float>::max();
		// Starting at sample one lets escape directions differ when a bullet is
		// already close. Including sample zero gave every candidate the same
		// minimum and allowed the attack-position bias to suppress dodging.
		for (int sample = 1; sample <= LOOKAHEAD_SAMPLES; ++sample) {
			const float ratio = static_cast<float>(sample) / LOOKAHEAD_SAMPLES;
			const shiki::Vec2 player{
			    std::lerp(playerPosition.x, destination.x, ratio),
			    std::lerp(playerPosition.y, destination.y, ratio)};
			safety = std::min(safety,
			                  clearanceAt(player, LOOKAHEAD_SECONDS * ratio));
		}
		return safety;
	};

	static const std::array<shiki::Vec2, 9> DIRECTIONS = {
	    shiki::Vec2{0.0f, 0.0f},        shiki::Vec2{-1.0f, 0.0f},
	    shiki::Vec2{1.0f, 0.0f},        shiki::Vec2{0.0f, -1.0f},
	    shiki::Vec2{0.0f, 1.0f},        shiki::Vec2{-0.7071f, -0.7071f},
	    shiki::Vec2{0.7071f, -0.7071f}, shiki::Vec2{-0.7071f, 0.7071f},
	    shiki::Vec2{0.7071f, 0.7071f}};
	const float currentClearance = clearanceAt(playerPosition, 0.0f);
	shiki::Vec2 objectiveDirection{positiveTarget.x - playerPosition.x,
	                               positiveTarget.y - playerPosition.y};
	const float objectiveLength =
	    std::hypot(objectiveDirection.x, objectiveDirection.y);
	if (objectiveLength > 0.0f) {
		objectiveDirection.x /= objectiveLength;
		objectiveDirection.y /= objectiveLength;
	}
	shiki::Vec2 baselineDestination{
	    playerPosition.x +
	        objectiveDirection.x * state.playerSpeed * LOOKAHEAD_SECONDS,
	    playerPosition.y +
	        objectiveDirection.y * state.playerSpeed * LOOKAHEAD_SECONDS};
	baselineDestination.x =
	    shiki::clamp(baselineDestination.x, 8.0f, GAME_WIDTH - 8.0f);
	baselineDestination.y =
	    shiki::clamp(baselineDestination.y, 16.0f, GAME_HEIGHT - 16.0f);
	const float baselineSafety = pathSafety(baselineDestination);
	const bool maximizeOutput =
	    state.invincibleTimer > 0.0f && attackTarget && !criticalItemTarget;
	shiki::Vec2 outputTarget = combatTarget;
	if (attackTarget) {
		outputTarget.y =
		    shiki::clamp(attackTarget->y + 104.0f, 104.0f, GAME_HEIGHT - 56.0f);
	}
	const bool mustDodge =
	    !maximizeOutput && (currentClearance < 3.0f || baselineSafety < 2.0f);
	if (mustDodge) {
		if (state.debugAiDodgeRecoveryFrames <= 0)
			state.debugAiDodgeOrigin = playerPosition;
		state.debugAiDodgeRecoveryFrames = 45;
	}

	shiki::Vec2 movementTarget = maximizeOutput ? outputTarget : positiveTarget;
	if (!mustDodge && !criticalItemTarget && !maximizeOutput &&
	    state.debugAiDodgeRecoveryFrames > 0) {
		const float returnDistance =
		    std::hypot(state.debugAiDodgeOrigin.x - playerPosition.x,
		               state.debugAiDodgeOrigin.y - playerPosition.y);
		if (returnDistance <= 8.0f)
			state.debugAiDodgeRecoveryFrames = 0;
		else {
			movementTarget = state.debugAiDodgeOrigin;
			--state.debugAiDodgeRecoveryFrames;
		}
	}

	shiki::Vec2 bestDirection{};
	float bestScore = -std::numeric_limits<float>::max();
	float bestSafety = -std::numeric_limits<float>::max();
	float bestSafeSeconds = 0.0f;
	bool bestFocused = false;
	bool committedCandidateAvailable = false;
	shiki::Vec2 committedDirection{};
	float committedSafety = -std::numeric_limits<float>::max();
	float committedSafeSeconds = 0.0f;
	bool committedFocused = false;
	const auto attackQuality = [&](const shiki::Vec2 &position) {
		if (!attackTarget)
			return 0.0f;
		// Player shots travel upward, so crossing above a target must not gain
		// an attack-position reward.
		if (position.y <= attackTarget->y + 72.0f)
			return -240.0f;
		const float enemyDistance = std::hypot(position.x - attackTarget->x,
		                                       position.y - attackTarget->y);
		const float minimumDistance = maximizeOutput ? 80.0f : 112.0f;
		if (enemyDistance < minimumDistance)
			return -240.0f - (minimumDistance - enemyDistance) * 8.0f;
		const float desiredY = maximizeOutput ? outputTarget.y : combatTarget.y;
		return -std::abs(position.x - attackTarget->x) -
		       std::abs(position.y - desiredY) * 0.8f;
	};
	const float currentGoalDistance =
	    std::hypot(movementTarget.x - playerPosition.x,
	               movementTarget.y - playerPosition.y);
	for (const bool focused : {false, true}) {
		const float speed = state.playerSpeed * (focused ? 0.5f : 1.0f);
		for (const auto &direction : DIRECTIONS) {
			constexpr float FIRST_LEG_SECONDS = PLAN_SECONDS * 0.35f;
			shiki::Vec2 midpoint{
			    playerPosition.x + direction.x * speed * FIRST_LEG_SECONDS,
			    playerPosition.y + direction.y * speed * FIRST_LEG_SECONDS};
			midpoint.x = shiki::clamp(midpoint.x, 8.0f, GAME_WIDTH - 8.0f);
			midpoint.y = shiki::clamp(midpoint.y, 16.0f, GAME_HEIGHT - 16.0f);
			shiki::Vec2 secondDirection{movementTarget.x - midpoint.x,
			                            movementTarget.y - midpoint.y};
			const float secondLength =
			    std::hypot(secondDirection.x, secondDirection.y);
			if (secondLength > 0.0f) {
				secondDirection.x /= secondLength;
				secondDirection.y /= secondLength;
			}

			float safety = std::numeric_limits<float>::max();
			float safeSeconds = PLAN_SECONDS;
			shiki::Vec2 candidate = playerPosition;
			for (int sample = 1; sample <= PLAN_SAMPLES; ++sample) {
				const float seconds =
				    PLAN_SECONDS * static_cast<float>(sample) / PLAN_SAMPLES;
				if (seconds <= FIRST_LEG_SECONDS) {
					candidate = {
					    playerPosition.x + direction.x * speed * seconds,
					    playerPosition.y + direction.y * speed * seconds};
				} else {
					candidate = {midpoint.x + secondDirection.x * speed *
					                              (seconds - FIRST_LEG_SECONDS),
					             midpoint.y +
					                 secondDirection.y * speed *
					                     (seconds - FIRST_LEG_SECONDS)};
				}
				candidate.x =
				    shiki::clamp(candidate.x, 8.0f, GAME_WIDTH - 8.0f);
				candidate.y =
				    shiki::clamp(candidate.y, 16.0f, GAME_HEIGHT - 16.0f);
				const float clearance = clearanceAt(candidate, seconds);
				safety = std::min(safety, clearance);
				if (clearance <= 1.0f && safeSeconds == PLAN_SECONDS)
					safeSeconds = seconds;
			}

			const float candidateGoalDistance = std::hypot(
			    movementTarget.x - candidate.x, movementTarget.y - candidate.y);
			const float goalProgress =
			    currentGoalDistance - candidateGoalDistance;
			const float borderMargin = std::min(
			    {candidate.x - 8.0f, GAME_WIDTH - 8.0f - candidate.x,
			     candidate.y - 16.0f, GAME_HEIGHT - 16.0f - candidate.y});
			const bool stationary = direction.x == 0.0f && direction.y == 0.0f;
			const float attackBenefit =
			    attackQuality(candidate) - attackQuality(playerPosition);
			const float reachableDistance =
			    std::min(currentGoalDistance, speed * safeSeconds);
			const float continuity =
			    direction.x * state.debugAiLastDirection.x +
			    direction.y * state.debugAiLastDirection.y;
			const shiki::Vec2 dodgeTarget =
			    bossTarget
			        ? combatTarget
			        : shiki::Vec2{GAME_WIDTH * 0.5f, GAME_HEIGHT - 120.0f};
			const float dodgeProgress =
			    std::hypot(dodgeTarget.x - playerPosition.x,
			               dodgeTarget.y - playerPosition.y) -
			    std::hypot(dodgeTarget.x - candidate.x,
			               dodgeTarget.y - candidate.y);
			float score = 0.0f;
			if (criticalItemTarget) {
				// Life and bomb pickups deliberately outrank survival for this
				// debug AI. Keep safety bounded so it can only break ties
				// between routes that make comparable progress toward the
				// resource.
				score = goalProgress * 12000.0f +
				        std::clamp(safety, -32.0f, 32.0f) * 40.0f +
				        safeSeconds * 250.0f + borderMargin * 0.25f +
				        continuity * 200.0f;
				if (stationary && currentGoalDistance > 8.0f)
					score -= 10000.0f;
			} else if (maximizeOutput) {
				score = attackBenefit * 120.0f + goalProgress * 20.0f +
				        borderMargin * 0.2f + continuity * 120.0f;
				if (stationary && currentGoalDistance > 8.0f)
					score -= 1000.0f;
			} else if (mustDodge) {
				// Survival time and clearance dominate. A lateral first leg can
				// therefore outscore repeatedly squeezing into a bullet wall.
				score = std::clamp(safety, -32.0f, 96.0f) * 700.0f +
				        safeSeconds * 1200.0f + goalProgress * 4.0f +
				        dodgeProgress * 30.0f + borderMargin * 2.0f +
				        continuity * (state.debugAiDirectionCommitFrames > 0
				                          ? 1200.0f
				                          : 300.0f);
				if (focused && safety < 20.0f)
					score += 250.0f;
				if (stationary)
					score -= 500.0f;
			} else {
				const float blockedTime = PLAN_SECONDS - safeSeconds;
				score = goalProgress * 10.0f + attackBenefit * 5.0f +
				        reachableDistance * 0.8f + safeSeconds * 300.0f +
				        std::min(safety, 32.0f) * 1.5f + borderMargin * 0.25f +
				        continuity * 160.0f - blockedTime * 1800.0f +
				        (focused ? 0.0f : 12.0f);
				if (stationary && currentGoalDistance > 12.0f)
					score -= 1000.0f;
			}
			if (focused == state.debugAiLastFocused)
				score += mustDodge ? 60.0f : 30.0f;
			const bool sameCommittedDirection =
			    std::abs(direction.x - state.debugAiLastDirection.x) < 0.05f &&
			    std::abs(direction.y - state.debugAiLastDirection.y) < 0.05f;
			if (sameCommittedDirection && focused == state.debugAiLastFocused) {
				committedCandidateAvailable = true;
				committedDirection = direction;
				committedSafety = safety;
				committedSafeSeconds = safeSeconds;
				committedFocused = focused;
			}
			if (score > bestScore) {
				bestScore = score;
				bestSafety = safety;
				bestSafeSeconds = safeSeconds;
				bestDirection = direction;
				bestFocused = focused;
			}
		}
	}

	// Hold a dodge direction briefly to prevent left/right oscillation. Break
	// the commitment as soon as the old route collides materially sooner or
	// has clearly less clearance than the newly planned route.
	if (mustDodge && !criticalItemTarget &&
	    state.debugAiDirectionCommitFrames > 0 && committedCandidateAvailable) {
		const bool committedRouteViable = committedSafeSeconds >= 0.16f;
		const bool replacementMateriallySafer =
		    bestSafeSeconds > committedSafeSeconds + 0.10f ||
		    bestSafety > committedSafety + 6.0f;
		if (committedRouteViable && !replacementMateriallySafer) {
			bestDirection = committedDirection;
			bestSafety = committedSafety;
			bestSafeSeconds = committedSafeSeconds;
			bestFocused = committedFocused;
		}
	}

	const float selectedContinuity =
	    bestDirection.x * state.debugAiLastDirection.x +
	    bestDirection.y * state.debugAiLastDirection.y;
	if (mustDodge) {
		if (state.debugAiDirectionCommitFrames <= 0 ||
		    selectedContinuity < 0.5f)
			state.debugAiDirectionCommitFrames = 8;
		else
			--state.debugAiDirectionCommitFrames;
	} else if (state.debugAiDirectionCommitFrames > 0) {
		--state.debugAiDirectionCommitFrames;
	}
	state.debugAiLastDirection = bestDirection;
	state.debugAiLastFocused = bestFocused;
	state.keyLeft = bestDirection.x < -0.1f;
	state.keyRight = bestDirection.x > 0.1f;
	state.keyUp = bestDirection.y < -0.1f;
	state.keyDown = bestDirection.y > 0.1f;
	state.keyShoot = true;
	state.keyFocus = bestFocused;
	state.isFocused = bestFocused;
	state.keyBomb = false;
	state.bombRequested = false;
	if (state.deathbombFrame < 0) {
		state.debugAiDeathbombTargetFrame = -1;
		state.debugAiImminentDanger =
		    currentClearance < 6.0f || bestSafety < 1.0f;
	} else if (state.bombs > 0 && state.bombFrame < 0) {
		if (state.debugAiDeathbombTargetFrame < 0) {
			const float randomUnit =
			    static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
			state.debugAiDeathbombTargetFrame =
			    sampleTH06DebugAiDeathbombTarget(state.deathbombFrame,
			                                     state.debugAiImminentDanger,
			                                     randomUnit);
		}
		state.bombRequested =
		    state.deathbombFrame >= state.debugAiDeathbombTargetFrame;
	}
	state.keyBomb = state.bombRequested;
	state.keyDialogueSkip = true;
	if (state.dialogue.active)
		state.dialogue.advanceRequested = true;
}

// Process gameplay input.
void handleInput(GameState &state, float dt) {
	if (state.isGameOver)
		return;
	if (state.debugAiEnabled)
		updateTH06DebugAiInput(state);
	if (state.eclTimeStopped) {
		state.bombRequested = false;
		return;
	}

	const bool longDeathbomb = state.deathbombFrame >= TRUE_DEATHBOMB_FRAMES;
	if (!longDeathbomb && state.playerDeathTimer < 0) {
		// Move the player with directional input.
		shiki::Vec2 direction(0.0f, 0.0f);
		if (state.keyUp)
			direction.y -= 1.0f;
		if (state.keyDown)
			direction.y += 1.0f;
		if (state.keyLeft)
			direction.x -= 1.0f;
		if (state.keyRight)
			direction.x += 1.0f;

		const float length =
		    std::sqrt(direction.x * direction.x + direction.y * direction.y);
		if (length > 0.0f) {
			direction.x /= length;
			direction.y /= length;
		}

		const float speed =
		    state.isFocused ? state.playerSpeed * 0.5f : state.playerSpeed;
		const float bombMovementScale =
		    state.playerCharacter == 1 && state.bombFrame >= 0 ? 0.3f : 1.0f;
		const float horizontalSpeed = direction.x * speed;
		if (horizontalSpeed < 0.0f && state.previousHorizontalSpeed >= 0.0f)
			setPlayerAnmScript(state, 1);
		else if (horizontalSpeed == 0.0f &&
		         state.previousHorizontalSpeed < 0.0f)
			setPlayerAnmScript(state, 2);
		if (horizontalSpeed > 0.0f && state.previousHorizontalSpeed <= 0.0f)
			setPlayerAnmScript(state, 3);
		else if (horizontalSpeed == 0.0f &&
		         state.previousHorizontalSpeed > 0.0f)
			setPlayerAnmScript(state, 4);
		state.previousHorizontalSpeed = horizontalSpeed;
		updatePlayerAnm(state, dt);

		auto pos = state.player.getPosition();
		pos.x += direction.x * speed * bombMovementScale * dt;
		pos.y += direction.y * speed * bombMovementScale * dt;
		pos.x = shiki::clamp(pos.x, 8.0f, GAME_WIDTH - 8.0f);
		pos.y = shiki::clamp(pos.y, 16.0f, GAME_HEIGHT - 16.0f);
		state.player.setPosition(pos);
	}

	// Start a bomb when its request is valid.
	state.bombCooldown -= dt;
	if (state.playerDeathTimer < 0 && state.bombRequested &&
	    state.bombCooldown <= 0.0f && state.bombFrame < 0) {
		const bool inLongDeathbomb =
		    state.deathbombFrame >= TRUE_DEATHBOMB_FRAMES;
		// The IN-style extension normally consumes two, but a final single bomb
		// is still usable and leaves the stock at zero.
		const int bombCost = inLongDeathbomb ? std::min(2, state.bombs) : 1;
		if (bombCost > 0 && state.bombs >= bombCost) {
			if (state.playerCharacter == 0)
				startReimuABomb(state, bombCost);
			else
				startMarisaBBomb(state, bombCost);
		}
	}
	state.bombRequested = false;
}

// Update gameplay logic.
void updateGame(GameState &state, float dt) {
	if (state.isGameOver)
		return;

	const bool longDeathbombAtFrameStart =
	    state.deathbombFrame >= TRUE_DEATHBOMB_FRAMES;
	if (!longDeathbombAtFrameStart && !state.eclTimeStopped)
		state.gameTime += dt;

	// Update invincibility time.
	if (!state.eclTimeStopped && state.invincibleTimer > 0.0f) {
		state.invincibleTimer -= dt;
	}

	static constexpr float PLAYER_LOGIC_DT = 1.0f / 60.0f;
	state.dialogue.eclResumeRequested = false;
	if (!state.eclTimeStopped)
		state.playerLogicAccum += dt;
	while (state.playerLogicAccum >= PLAYER_LOGIC_DT) {
		state.playerLogicAccum -= PLAYER_LOGIC_DT;
		if (state.playerDeathTimer >= 0) {
			++state.playerDeathTimer;
			// Player.cpp keeps the player at the death position for the
			// six-frame respawn wait and 30-frame death animation. Spawning
			// begins at the center-bottom position and accepts input after
			// another 30 frames.
			if (state.playerDeathTimer == 36) {
				state.player.setPosition(GAME_WIDTH * 0.5f,
				                         GAME_HEIGHT - 64.0f);
				state.playerBulletGraceFrames = 90;
			}
			if (state.playerDeathTimer >= 66) {
				state.playerDeathTimer = -1;
				state.invincibleTimer = 4.0f;
			}
		}
		if (state.playerBulletGraceFrames > 0) {
			--state.playerBulletGraceFrames;
			beginEnemyBulletDespawn(state, false);
			for (auto &enemy : state.eclEnemies) {
				for (auto &laser : enemy.lasers) {
					if (!laser.active || laser.state >= 2)
						continue;
					laser.state = 2;
					laser.stateTimer = 0;
					laser.cancelFrames = 0;
					laser.hitboxEndDelay = 0;
				}
			}
		}
		const bool longDeathbomb =
		    state.deathbombFrame >= TRUE_DEATHBOMB_FRAMES;
		if (!longDeathbomb) {
			state.stageBackground.update();
			updateTH06DialogueTick(state, state.resourceManager);
			updateTH06DialogueVisuals(state.dialogue);
			updateReimuATick(state);
			updateTH06ScreenShakesTick(state);
			updateTH06CardUiTick(state);
			state.stageNameVm.tick();
			state.songNameVm.tick();
			if (state.spellResultUi.active &&
			    ++state.spellResultUi.frame >= 280)
				state.spellResultUi.active = false;
			if (state.stageClearUi.active &&
			    ++state.stageClearUi.frame >= 360) {
				state.stageClearUi.active = false;
				if (!state.extraStage && state.stageNumber < 6) {
					++state.stageNumber;
					state.stageAdvanceRequested = true;
					state.stageRestartRequested = true;
					state.gameTime = 0.0f;
					if (state.audioManager)
						state.audioManager->stopMusic();
				} else {
					state.mainMenu.active = true;
					state.mainMenu.screen =
					    TH06MainMenuState::Screen::PressStart;
					state.mainMenu.frame = 0;
					state.stageRestartRequested = true;
					loadTH06TitleMenuVms(state.mainMenu, state.resourceManager);
					if (state.audioManager)
						state.audioManager->stopMusic();
				}
			}
			const int extraLivesAwarded = updateTH06GuiScore(state);
			for (int award = 0; award < extraLivesAwarded; ++award)
				playTH06Sound(state, 28);
			if (state.spellBackgroundActive) {
				++state.spellBackgroundFrame;
				state.spellBackgroundVm.tick();
			}
		} else {
			// ScreenEffect::ShakeScreen restores the unshifted arcade region
			// while game time is stopped.
			state.playfieldRegion = {32.0f, 16.0f, 384.0f, 448.0f};
		}
		if (state.deathbombFrame >= 0) {
			++state.deathbombFrame;
			// With no bombs there is no extended red time-stop window.
			if ((state.deathbombFrame >= TRUE_DEATHBOMB_FRAMES &&
			     state.bombs <= 0) ||
			    state.deathbombFrame >= DEATHBOMB_TOTAL_FRAMES)
				resolveMissedDeathbomb(state);
		}
	}

	// Remove player shots outside the playfield.
	state.playerBullets.erase(
	    std::remove_if(state.playerBullets.begin(), state.playerBullets.end(),
	                   [](const GameState::PlayerBullet &bullet) {
		                   const auto position = bullet.sprite.getPosition();
		                   return !bullet.active || position.y < -50.0f ||
		                          position.y > GAME_HEIGHT + 50.0f ||
		                          position.x < -50.0f ||
		                          position.x > GAME_WIDTH + 50.0f;
	                   }),
	    state.playerBullets.end());

	// Update the player position used by ECL enemy aiming.
	auto playerPos = state.player.getPosition();
	for (auto &e : state.eclEnemies) {
		e.playerX = playerPos.x;
		e.playerY = playerPos.y;
		e.globalTimeStopped = state.eclTimeStopped;
	}

	// TH06 advances enemy movement, ECL and boss timers exactly once per 60 Hz
	// logic tick. Rendering may run substantially faster than that.
	if (state.deathbombFrame < TRUE_DEATHBOMB_FRAMES) {
		state.enemyLogicAccum += dt;
		while (state.enemyLogicAccum >= PLAYER_LOGIC_DT) {
			state.enemyLogicAccum -= PLAYER_LOGIC_DT;
			updateECLEnemies(state.eclEnemies, PLAYER_LOGIC_DT);
			int enemyScore = 0;
			checkPlayerBulletsVsECLEnemies(state.playerBullets,
			                               state.eclEnemies, enemyScore);
			addTH06Score(state, enemyScore);
		}
	}

	if (state.deathbombFrame < TRUE_DEATHBOMB_FRAMES) {
		state.effectLogicAccum += dt;
		while (state.effectLogicAccum >= PLAYER_LOGIC_DT) {
			state.effectLogicAccum -= PLAYER_LOGIC_DT;
			state.effects.updateTick();
		}
	}

	// Update enemy bullets at a fixed 60 Hz step.
	static constexpr float ECL_LOGIC_DT = 1.0f / 60.0f;
	state.bulletLogicAccum += dt;
	while (state.bulletLogicAccum >= ECL_LOGIC_DT) {
		state.bulletLogicAccum -= ECL_LOGIC_DT;
		const bool freezeBullets =
		    state.deathbombFrame >= TRUE_DEATHBOMB_FRAMES ||
		    state.eclTimeStopped;
		if (!freezeBullets)
			for (auto &bullet : state.enemyBullets) {
				if (bullet.despawning) {
					auto position = bullet.sprite.getPosition();
					position.x += bullet.vx * ECL_LOGIC_DT * 0.5f;
					position.y += bullet.vy * ECL_LOGIC_DT * 0.5f;
					bullet.sprite.setPosition(position);
					bullet.despawnSprite.setPosition(position);
					++bullet.despawnFrames;
					const int duration = bullet.bulletType == 9 ? 16 : 12;
					const float progress =
					    static_cast<float>(bullet.despawnFrames) /
					    static_cast<float>(duration);
					const float initialScale = bullet.bulletType == 0   ? 1.0f
					                           : bullet.bulletType <= 5 ? 1.5f
					                           : bullet.bulletType <= 8 ? 3.0f
					                                                    : 1.0f;
					const float scaleVelocity =
					    bullet.bulletType <= 5   ? 1.0f / 12.0f
					    : bullet.bulletType <= 8 ? 1.0f / 6.0f
					                             : -1.0f / 16.0f;
					const float scale =
					    initialScale + scaleVelocity * bullet.despawnFrames;
					bullet.despawnSprite.setScale(scale, scale);
					bullet.despawnSprite.setColor(
					    {1.0f, 1.0f, 1.0f, std::max(0.0f, 1.0f - progress)});
					continue;
				}
				auto pos = bullet.sprite.getPosition();
				if (bullet.fastSpawnFrames > 0) {
					// Renderer-compatible fast path verified by Nobody's
					// opening wave.
					pos.x += bullet.vx * ECL_LOGIC_DT * 0.5f;
					pos.y += bullet.vy * ECL_LOGIC_DT * 0.5f;
					bullet.sprite.setPosition(pos);
					bullet.spawnEffectSprite.setPosition(pos);
					const float progress =
					    static_cast<float>(bullet.fastSpawnAge + 1) /
					    static_cast<float>(bullet.fastSpawnAge +
					                       bullet.fastSpawnFrames);
					const float scale = 2.4f - 1.4f * progress;
					bullet.spawnEffectSprite.setScale(scale, scale);
					bullet.spawnEffectSprite.setColor(
					    {1.0f, 1.0f, 1.0f, std::max(0.0f, 1.0f - progress)});
					++bullet.fastSpawnAge;
					--bullet.fastSpawnFrames;
					continue;
				}
				if (bullet.spawnAnimation.active()) {
					// TH06 keeps spawn VMs non-collidable and pauses
					// fired-bullet EX timers. Fired movement and EX processing
					// begin on the next tick.
					pos.x += bullet.vx * ECL_LOGIC_DT /
					         bullet.spawnAnimation.movementDivisor;
					pos.y += bullet.vy * ECL_LOGIC_DT /
					         bullet.spawnAnimation.movementDivisor;
					bullet.sprite.setPosition(pos);
					bullet.spawnEffectSprite.setPosition(pos);
					++bullet.spawnAnimationAge;
					const float scale = bullet.spawnAnimation.initialScale +
					                    bullet.spawnAnimation.scalePerFrame *
					                        bullet.spawnAnimationAge;
					bullet.spawnEffectSprite.setScale(scale, scale);
					bullet.spawnEffectSprite.setColor(
					    {1.0f, 1.0f, 1.0f,
					     bullet.spawnAnimation.alphaAtAge(
					         bullet.spawnAnimationAge)});
					if (bullet.spawnAnimationAge <
					    bullet.spawnAnimation.durationFrames)
						continue;
					bullet.spawnAnimation.state = TH06BulletSpawnState::Fired;
					bullet.ageFrames = 0;
					continue;
				}
				if (bullet.ageFrames < bullet.effectFrames) {
					if ((bullet.effectFlags & 0x01) != 0 &&
					    bullet.ageFrames <= 16) {
						const float launchSpeed =
						    bullet.speedPerFrame + 5.0f -
						    static_cast<float>(bullet.ageFrames) * 5.0f / 16.0f;
						bullet.vx =
						    std::cos(bullet.angle) * launchSpeed * 60.0f;
						bullet.vy =
						    std::sin(bullet.angle) * launchSpeed * 60.0f;
					} else if ((bullet.effectFlags & 0x10) != 0) {
						bullet.vx += bullet.accelerationX;
						bullet.vy += bullet.accelerationY;
						bullet.angle = std::atan2(bullet.vy, bullet.vx);
					} else if ((bullet.effectFlags & 0x20) != 0) {
						bullet.angle += bullet.angleDelta;
						bullet.speedPerFrame += bullet.speedDelta;
						bullet.vx = std::cos(bullet.angle) *
						            bullet.speedPerFrame * 60.0f;
						bullet.vy = std::sin(bullet.angle) *
						            bullet.speedPerFrame * 60.0f;
					}
				}
				if ((bullet.effectFlags & (0x40 | 0x80 | 0x100)) != 0 &&
				    bullet.directionChangeInterval > 0) {
					float movementSpeed = bullet.speedPerFrame;
					const int threshold = bullet.directionChangeInterval *
					                      (bullet.directionChangeCount + 1);
					if (bullet.ageFrames >= threshold) {
						const bool aimAtPlayer =
						    (bullet.effectFlags & 0x80) != 0;
						const bool absoluteDirection =
						    (bullet.effectFlags & 0x100) != 0;
						++bullet.directionChangeCount;
						if (aimAtPlayer)
							bullet.angle = std::atan2(playerPos.y - pos.y,
							                          playerPos.x - pos.x) +
							               bullet.directionChangeAngle;
						else if (absoluteDirection)
							bullet.angle = bullet.directionChangeAngle;
						else
							bullet.angle += bullet.directionChangeAngle;
						bullet.speedPerFrame = bullet.directionChangeSpeed;
						movementSpeed = bullet.speedPerFrame;
						if (bullet.directionChangeCount >=
						    bullet.directionChangeMax)
							bullet.effectFlags &= ~(0x40 | 0x80 | 0x100);
					} else {
						const int intervalFrame =
						    bullet.ageFrames - bullet.directionChangeInterval *
						                           bullet.directionChangeCount;
						movementSpeed =
						    bullet.speedPerFrame *
						    (1.0f - static_cast<float>(intervalFrame) /
						                bullet.directionChangeInterval);
					}
					bullet.vx = std::cos(bullet.angle) * movementSpeed * 60.0f;
					bullet.vy = std::sin(bullet.angle) * movementSpeed * 60.0f;
				}
				pos.x += bullet.vx * ECL_LOGIC_DT;
				pos.y += bullet.vy * ECL_LOGIC_DT;
				const bool reflectAllEdges = (bullet.effectFlags & 0x400) != 0;
				const bool reflectTopAndSides =
				    (bullet.effectFlags & 0x800) != 0;
				if ((reflectAllEdges || reflectTopAndSides) &&
				    (pos.x < 0.0f || pos.x >= GAME_WIDTH || pos.y < 0.0f ||
				     (reflectAllEdges && pos.y >= GAME_HEIGHT))) {
					if (pos.x < 0.0f || pos.x >= GAME_WIDTH)
						bullet.angle =
						    -bullet.angle - std::numbers::pi_v<float>;
					if (pos.y < 0.0f)
						bullet.angle = -bullet.angle;
					bullet.speedPerFrame = bullet.directionChangeSpeed;
					bullet.vx =
					    std::cos(bullet.angle) * bullet.speedPerFrame * 60.0f;
					bullet.vy =
					    std::sin(bullet.angle) * bullet.speedPerFrame * 60.0f;
					pos.x = std::clamp(pos.x, 0.0f, GAME_WIDTH - 0.01f);
					pos.y = std::clamp(pos.y, 0.0f, GAME_HEIGHT - 0.01f);
					if (++bullet.directionChangeCount >=
					    bullet.directionChangeMax)
						bullet.effectFlags &= ~(0x400 | 0x800);
				}
				bullet.sprite.setPosition(pos);
				if (bullet.bulletType == 2 || bullet.bulletType == 4 ||
				    bullet.bulletType == 5 || bullet.bulletType == 7 ||
				    bullet.bulletType == 8)
					bullet.sprite.setRotation(bullet.angle * 180.0f /
					                              std::numbers::pi_v<float> -
					                          90.0f);
				++bullet.ageFrames;
			}
		if (!freezeBullets) {
			updateTH06ItemsTick(state);
		}
	}

	// Remove enemy bullets outside the playfield.
	state.enemyBullets.erase(
	    std::remove_if(state.enemyBullets.begin(), state.enemyBullets.end(),
	                   [](const GameState::EnemyBullet &b) {
		                   if (b.despawning)
			                   return b.despawnFrames >=
			                          (b.bulletType == 9 ? 16 : 12);
		                   auto p = b.sprite.getPosition();
		                   if ((b.effectFlags & (0x400 | 0x800)) != 0)
			                   return p.y > GAME_HEIGHT + 50.0f;
		                   return p.y > GAME_HEIGHT + 50.0f || p.y < -100.0f ||
		                          p.x < -100.0f || p.x > GAME_WIDTH + 100.0f;
	                   }),
	    state.enemyBullets.end());

	state.playerBullets.erase(
	    std::remove_if(state.playerBullets.begin(), state.playerBullets.end(),
	                   [](const GameState::PlayerBullet &bullet) {
		                   return !bullet.active;
	                   }),
	    state.playerBullets.end());

	// BulletManager calls CheckGraze before its lethal collision test. A
	// normal enemy bullet can only graze once, but remains lethal afterward.
	{
		const auto playerPos = state.player.getPosition();
		constexpr float GRAZE_EXPANSION = 20.0f;
		for (auto &bullet : state.enemyBullets) {
			if ((state.invincibleTimer > 0.0f && state.bombFrame < 0) ||
			    bullet.despawning || bullet.grazed ||
			    bullet.fastSpawnFrames > 0 || bullet.spawnAnimation.active())
				continue;
			const auto bulletPos = bullet.sprite.getPosition();
			const auto bulletSize =
			    getTH06BulletCollisionSize(bullet.bulletType);
			if (std::abs(playerPos.x - bulletPos.x) <=
			        bulletSize.x * 0.5f + GRAZE_EXPANSION +
			            TH06_PLAYER_HITBOX_HALF_SIZE &&
			    std::abs(playerPos.y - bulletPos.y) <=
			        bulletSize.y * 0.5f + GRAZE_EXPANSION +
			            TH06_PLAYER_HITBOX_HALF_SIZE) {
				bullet.grazed = true;
				scoreTH06Graze(state, bulletPos);
			}
		}
	}

	// Resolve player collisions with enemy bullets.
	if (state.playerDeathTimer < 0 && state.invincibleTimer <= 0.0f &&
	    state.deathbombFrame < 0) {
		auto playerPos = state.player.getPosition();
		for (const auto &bullet : state.enemyBullets) {
			if (bullet.despawning || bullet.fastSpawnFrames > 0 ||
			    bullet.spawnAnimation.active())
				continue;
			auto bulletPos = bullet.sprite.getPosition();
			const auto bulletSize =
			    getTH06BulletCollisionSize(bullet.bulletType);
			if (std::abs(playerPos.x - bulletPos.x) <=
			        bulletSize.x * 0.5f + TH06_PLAYER_HITBOX_HALF_SIZE &&
			    std::abs(playerPos.y - bulletPos.y) <=
			        bulletSize.y * 0.5f + TH06_PLAYER_HITBOX_HALF_SIZE) {
				beginDeathbombWindow(state);
				break;
			}
		}
	}

	// Lasers can graze again every 12 frames. CalcLaserHitboxCollision expands
	// the beam-local rectangle by 48 pixels on every side.
	{
		const auto playerPos = state.player.getPosition();
		bool hitByLaser = false;
		for (auto &enemy : state.eclEnemies) {
			if (!enemy.alive)
				continue;
			for (auto &laser : enemy.lasers) {
				if (!laser.active || laser.ageFrames < laser.hitboxStartTime ||
				    laser.cancelFrames >= 0)
					continue;
				const float length =
				    std::max(laser.endOffset - laser.startOffset, 0.0f);
				if (length <= 0.0f)
					continue;
				const float dx = playerPos.x - laser.x;
				const float dy = playerPos.y - laser.y;
				const float along =
				    dx * std::cos(laser.angle) + dy * std::sin(laser.angle);
				const float across =
				    -dx * std::sin(laser.angle) + dy * std::cos(laser.angle);
				constexpr float LASER_GRAZE_EXPANSION = 48.0f;
				const int laserTimer = laser.ageFrames < laser.startTime
				                           ? laser.ageFrames
				                           : laser.ageFrames - laser.startTime;
				const bool canGraze = laserTimer % 12 == 0;
				if ((state.invincibleTimer <= 0.0f || state.bombFrame >= 0) &&
				    canGraze &&
				    along >= laser.startOffset - LASER_GRAZE_EXPANSION -
				                 TH06_PLAYER_HITBOX_HALF_SIZE &&
				    along <= laser.endOffset + LASER_GRAZE_EXPANSION +
				                 TH06_PLAYER_HITBOX_HALF_SIZE &&
				    std::abs(across) <= laser.width * 0.5f +
				                            LASER_GRAZE_EXPANSION +
				                            TH06_PLAYER_HITBOX_HALF_SIZE) {
					scoreTH06Graze(state, playerPos);
				}
				if (along >= laser.startOffset && along <= laser.endOffset &&
				    std::abs(across) <=
				        laser.width * 0.5f + TH06_PLAYER_HITBOX_HALF_SIZE) {
					hitByLaser = state.playerDeathTimer < 0 &&
					             state.invincibleTimer <= 0.0f &&
					             state.deathbombFrame < 0;
					if (hitByLaser)
						break;
				}
			}
			if (hitByLaser)
				break;
		}
		if (hitByLaser) {
			beginDeathbombWindow(state);
		}
	}

	// Remove dead ECL enemies.
	cleanupECLEnemies(state.eclEnemies, GAME_HEIGHT, GAME_WIDTH);
	if (!state.pendingECLEnemies.empty()) {
		state.eclEnemies.reserve(state.eclEnemies.size() +
		                         state.pendingECLEnemies.size());
		state.eclEnemies.insert(
		    state.eclEnemies.end(),
		    std::make_move_iterator(state.pendingECLEnemies.begin()),
		    std::make_move_iterator(state.pendingECLEnemies.end()));
		state.pendingECLEnemies.clear();
	}
	if (!longDeathbombAtFrameStart) {
		const auto activeBoss = std::find_if(
		    state.eclEnemies.begin(), state.eclEnemies.end(),
		    [](const ECLEnemy &enemy) { return enemy.alive && enemy.isBoss; });
		if (activeBoss != state.eclEnemies.end() && !state.dialogue.active) {
			if (state.bossUiOpacity <= 0.0f)
				state.bossUiFrame = 0;
			else
				++state.bossUiFrame;
			const float targetHealth =
			    activeBoss->maxHp > 0
			        ? std::clamp(static_cast<float>(activeBoss->hp) /
			                         static_cast<float>(activeBoss->maxHp),
			                     0.0f, 1.0f)
			        : 0.0f;
			// Gui::UpdateStageElements: fill at +0.01/frame, damage at
			// -0.02/frame.
			if (state.bossHealthDisplayed < targetHealth)
				state.bossHealthDisplayed =
				    std::min(targetHealth, state.bossHealthDisplayed + 0.01f);
			else if (state.bossHealthDisplayed > targetHealth)
				state.bossHealthDisplayed =
				    std::max(targetHealth, state.bossHealthDisplayed - 0.02f);
			state.bossUiOpacity =
			    std::min(1.0f, state.bossUiOpacity + 4.0f / 255.0f);
			state.bossUiLifeCount = activeBoss->bossLifeCount;
			const int remainingFrames =
			    std::max(0, activeBoss->timerCallbackThreshold -
			                    activeBoss->bossTimerFrames);
			state.bossUiSeconds = std::min(99, remainingFrames / 60);
			if (state.bossUiSeconds < 10 &&
			    state.bossUiSeconds != state.lastBossUiSeconds)
				playTH06Sound(state, 29);
			state.lastBossUiSeconds = state.bossUiSeconds;
		} else if (!state.dialogue.active) {
			state.lastBossUiSeconds = -1;
			state.bossUiOpacity =
			    std::max(0.0f, state.bossUiOpacity - 4.0f / 255.0f);
			if (state.bossUiOpacity <= 0.0f) {
				state.bossHealthDisplayed = 0.0f;
				state.bossUiFrame = 0;
			}
		}
	}
}

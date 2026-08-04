#include "th06_application.h"

#include "ecl_enemy.h"
#include "th06_effect_manager.h"
#include "th06_game_state.h"
#include "th06_stage_background.h"
#include "th06_systems.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <nlohmann/json.hpp>
#include <numbers>
#include <shiki/audio/audio_manager.h>
#include <shiki/ecl/ecl_asset.h>
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
int runTH06Example() {
	spdlog::info("ShikigamiSTG - Touhou Koumakyou Extra Stage Demo");

	// Create the engine without frame limiting to match the original game.
	shiki::frontend::RealtimeConfig frontendConfig;
	frontendConfig.vsync = false;
	frontendConfig.targetFps = 0;
	shiki::frontend::Realtime engine(frontendConfig);
	engine.setWindowTitle("Touhou Koumakyou Extra Stage");
	engine.setWindowSize(640, 480);

	// Initialize the real-time frontend.
	if (!engine.initialize()) {
		spdlog::error("Real-time frontend initialization failed!");
		return -1;
	}

	auto *renderer = engine.getRenderer();
	if (!renderer) {
		spdlog::error("Failed to get renderer!");
		return -1;
	}

	// Load textures and initialize runtime state.
	GameState state;
	state.randomItemSpawnIndex = std::rand() % 3;
	state.randomItemTableIndex = std::rand() % 8;

	// Load resources through the JSON sprite-atlas system.
	auto *resourceManager = engine.getResourceManager();
	if (resourceManager) {
		auto *assets = resourceManager->getAssetStore();
		if (!assets) {
			spdlog::error("Generated asset package is not mounted");
			return -1;
		}
		auto eclLoader = shiki::ecl::registerEclFileLoader(
		    *assets,
		    shiki::asset::AssetFormat::fromName("shiki.compat.th06.ecl.v1"), 6);
		if (!eclLoader) {
			spdlog::error(
			    "Failed to register TH06 ECL compatibility loader: {}",
			    eclLoader.error().message);
			return -1;
		}
		// Load bullet and presentation atlases by logical asset name.
		for (const char *atlas :
		     {"etama3",   "etama4",   "player00", "player01", "eff01",
		      "eff02",    "eff03",    "face03a",  "face03b",  "eff04",
		      "eff05",    "eff07",    "face05a",  "face06a",  "face06b",
		      "face08a",  "face08b",  "face09a",  "face09b",  "face10a",
		      "face10b",  "face12a",  "face12b",  "face00a",  "face00b",
		      "face00c",  "face01a",  "face01b",  "face01c",  "face12c",
		      "front",    "ascii",    "title01",  "title02",  "title03",
		      "title04",  "select01", "select02", "select03", "select04",
		      "select05", "slpl00a",  "slpl00b",  "slpl01a",  "slpl01b"})
			resourceManager->loadSpriteAtlas(atlas);
		for (int stage = 1; stage <= 7; ++stage) {
			const std::string prefix = "stg" + std::to_string(stage);
			resourceManager->loadSpriteAtlas(prefix + "enm");
			if (stage != 3 && stage != 4)
				resourceManager->loadSpriteAtlas(prefix + "enm2");
			resourceManager->loadSpriteAtlas(prefix + "bg");
		}
		for (const char *atlas :
		     {"etama3",   "etama4",   "player01", "eff01",    "eff02",
		      "eff03",    "eff04",    "eff05",    "eff07",    "face03a",
		      "face03b",  "face05a",  "face06a",  "face06b",  "face08a",
		      "face08b",  "face09a",  "face09b",  "face10a",  "face10b",
		      "face12a",  "stg7bg",   "face12b",  "face00a",  "face00b",
		      "face00c",  "face01a",  "face01b",  "face01c",  "face12c",
		      "front",    "ascii",    "title01",  "title02",  "title03",
		      "title04",  "select01", "select02", "select03", "select04",
		      "select05", "slpl00a",  "slpl00b",  "slpl01a",  "slpl01b"})
			preloadSpriteAtlas(resourceManager, atlas);
		for (int stage = 1; stage <= 7; ++stage) {
			const std::string prefix = "stg" + std::to_string(stage);
			preloadSpriteAtlas(resourceManager, prefix + "enm");
			if (stage != 3 && stage != 4)
				preloadSpriteAtlas(resourceManager, prefix + "enm2");
			preloadSpriteAtlas(resourceManager, prefix + "bg");
		}
		state.resourceManager = resourceManager;
		state.effects.setResourceManager(resourceManager);
		if (!state.stageBackground.load(*resourceManager, "th06.stage.7"))
			spdlog::warn("Failed to load TH06 stage 7 background JSON");
		state.extraStage = true;
		state.stageTextAnm = loadTH06MenuAnmFile(resourceManager, "text");
		if (!state.stageNameVm.initialize(state.stageTextAnm, 0) ||
		    !state.songNameVm.initialize(state.stageTextAnm, 1)) {
			spdlog::warn("Failed to load original TH06 stage text scripts");
		} else {
			state.stageNameVm.visible = true;
			state.songNameVm.visible = true;
		}

		// Resolve the original ANM sprite indices.
		state.bulletTexture = resourceManager->getSpriteTexture(
		    "player00", 18); // Reimu player shot.
		state.homingBulletTexture = resourceManager->getSpriteTexture(
		    "player00", 19); // Reimu A homing amulet, raw ANM sprite 65.
		// player01.anm raw sprites 64 and 68 map to extracted locals 20 and 24.
		state.marisaBulletTexture =
		    resourceManager->getSpriteTexture("player01", 20);
		state.reimuPlayerAnm = loadTH06MenuAnmFile(resourceManager, "player00");
		state.marisaPlayerAnm =
		    loadTH06MenuAnmFile(resourceManager, "player01");
		state.enemyBulletTexture =
		    resourceManager->getSpriteTexture("etama3", 46); // Enemy bullet.
		state.playerTexture =
		    resourceManager->getSpriteTexture("player00", 0); // Player sprite.
		state.enemyTexture =
		    resourceManager->getSpriteTexture("stg7enm", 0); // Enemy sprite.
		state.patchouliSpellBackgroundTexture =
		    resourceManager->getSpriteTexture("eff04", 0);
		state.flandreSpellBackgroundTexture =
		    resourceManager->getSpriteTexture("eff07", 0);
		state.stageSpellBackgroundTexture =
		    resourceManager->getSpriteTexture("eff01", 0);

		spdlog::info("Loaded JSON sprite atlases");
	}

	if (!loadTH06TitleMenuVms(state.mainMenu, resourceManager))
		spdlog::error("Failed to load original TH06 title ANM scripts");

	// Fall back to direct texture loading when atlas loading fails.
	if (!state.playerTexture) {
		state.playerTexture = loadTexture(
		    renderer, "assets/texture/player/player00/player00.png");
	}
	if (!state.bulletTexture) {
		state.bulletTexture = loadTexture(
		    renderer, "assets/texture/player/player00/sprite_018.png");
	}
	if (!state.homingBulletTexture) {
		state.homingBulletTexture = loadTexture(
		    renderer, "assets/texture/player/player00/sprite_019.png");
	}
	if (!state.enemyBulletTexture) {
		state.enemyBulletTexture = loadTexture(
		    renderer, "assets/texture/bullet/etama3/sprite_046.png");
	}
	if (!state.enemyTexture) {
		state.enemyTexture = loadTexture(
		    renderer, "assets/texture/enemy/stg7enm/sprite_000.png");
	}
	state.titleBackgroundTexture =
	    loadTexture(renderer, "assets/texture/title/title00.jpg");
	state.selectBackgroundTexture =
	    loadTexture(renderer, "assets/texture/title/select00.jpg");

	// Create gameplay objects.
	if (state.playerTexture) {
		state.player = createSprite(state.playerTexture, GAME_WIDTH / 2.0f,
		                            GAME_HEIGHT - 60.0f, 1.0f);
		// Use the texture's native dimensions.
		float texWidth = static_cast<float>(state.playerTexture->getWidth());
		float texHeight = static_cast<float>(state.playerTexture->getHeight());
		state.player.setSourceRect(
		    shiki::Rect(0.0f, 0.0f, texWidth, texHeight));
		state.player.setOrigin({texWidth * 0.5f, texHeight * 0.5f});
		setPlayerAnmScript(state, 0);
	}

	// Initialize audio.
	state.audioManager = engine.getAudio();
	if (state.audioManager) {
		state.audioManager->setMusicVolume(state.musicVolume);
		state.audioManager->setSoundVolume(state.soundVolume);
		if (loadTH06AudioManifest(state))
			spdlog::info("Audio manifest loaded successfully");
		else
			spdlog::warn("Failed to load audio manifest");
	} else {
		spdlog::warn("Audio not available");
	}

	if (!loadTH06Dialogue(state.dialogue, resourceManager))
		spdlog::warn("Failed to load TH06 dialogue JSON");

	// Register keyboard input callbacks.
	engine.setKeyCallback([&state](int key, bool pressed) {
		switch (key) {
		case SDLK_UP:
			if (!state.debugAiEnabled)
				state.keyUp = pressed;
			break;
		case SDLK_DOWN:
			if (!state.debugAiEnabled)
				state.keyDown = pressed;
			break;
		case SDLK_LEFT:
			if (!state.debugAiEnabled)
				state.keyLeft = pressed;
			break;
		case SDLK_RIGHT:
			if (!state.debugAiEnabled)
				state.keyRight = pressed;
			break;
		case SDLK_Z:
			if (!state.debugAiEnabled)
				state.keyShoot = pressed;
			if (!state.debugAiEnabled && pressed && state.dialogue.active)
				state.dialogue.advanceRequested = true;
			break;
		case SDLK_X:
			if (!state.debugAiEnabled)
				state.keyBomb = pressed;
			if (!state.debugAiEnabled && pressed)
				state.bombRequested = true;
			break;
		case SDLK_LSHIFT:
			if (!state.debugAiEnabled) {
				state.keyFocus = pressed;
				state.isFocused = pressed;
			}
			break;
		case SDLK_F1: {
			if (state.debugMode && !state.mainMenu.active && pressed &&
			    !state.debugAiKeyDown) {
				state.debugAiEnabled = !state.debugAiEnabled;
				state.keyUp = false;
				state.keyDown = false;
				state.keyLeft = false;
				state.keyRight = false;
				state.keyShoot = false;
				state.keyBomb = false;
				state.keyFocus = false;
				state.isFocused = false;
				state.bombRequested = false;
				state.debugAiImminentDanger = false;
				state.debugAiDeathbombTargetFrame = -1;
				state.debugAiDodgeOrigin = {};
				state.debugAiDodgeRecoveryFrames = 0;
				state.debugAiLastDirection = {};
				state.debugAiLastFocused = false;
				state.debugAiDirectionCommitFrames = 0;
				spdlog::info("Debug AI: {}",
				             state.debugAiEnabled ? "enabled" : "disabled");
			}
			state.debugAiKeyDown = pressed;
			break;
		}
		case SDLK_F2:
			if (state.debugMode && !state.mainMenu.active && pressed &&
			    !state.debugHitboxKeyDown) {
				state.debugEnemyHitboxes = !state.debugEnemyHitboxes;
				spdlog::info("Hitbox display: {}",
				             state.debugEnemyHitboxes ? "enabled" : "disabled");
			}
			state.debugHitboxKeyDown = pressed;
			break;
		}
	});

	// Track window-size changes.
	engine.setResizeCallback([&state](int width, int height) {
		state.windowWidth = width;
		state.windowHeight = height;
		spdlog::info("Window resized to: {}x{}", width, height);
	});

	// Initialize the ECL runtime.
	std::unique_ptr<shiki::ecl::ECLEngine> eclEngine =
	    std::make_unique<shiki::ecl::ECLEngine>();
	eclEngine->initialize();

	auto eclLoaded =
	    engine.getResourceManager()->getAssetStore()
	        ? eclEngine->loadECLAsset(
	              *engine.getResourceManager()->getAssetStore(),
	              shiki::asset::AssetId::fromName(
	                  "th06.ecl.ecldata" + std::to_string(state.stageNumber)))
	        : shiki::Result<void>{std::unexpected(
	              shiki::Error{shiki::ErrorDomain::Asset, 1,
	                           "TH06 asset package is not mounted"})};
	if (eclLoaded) {
		spdlog::info("ECL StageProgram loaded from AssetStore");

		auto *resMgr = engine.getResourceManager();
		shiki::ecl::EnemySpawnCallback spawnEnemyCallback =
		    [&state, &parser = eclEngine->getParser(),
		     resMgr](const shiki::ecl::EnemySpawnParams &spawn) {
			    // EnemyManager::RunEclTimeline suppresses every timeline spawn
			    // opcode while a boss owns the playfield.
			    if (state.bossPresent)
				    return;
			    const int32_t subId = spawn.subId;
			    // Detect the stage boss and midboss entry points.
			    if (subId == 16) {
				    spdlog::info("=== MIDBOSS Patchouli spawned! subId={}",
				                 subId);
				    // The Extra Stage uses one BGM throughout this section.
			    } else if (subId == 31) {
				    spdlog::info("=== BOSS Flandre spawned! subId={}", subId);
			    }
			    auto enemy =
			        createECLEnemy(parser, 0, subId, spawn.x, spawn.y, 0.0f,
			                       0.0f, state.enemyTexture, resMgr);
			    enemy.invertX = spawn.invertX;
			    enemy.stageNumber = state.stageNumber;
			    enemy.difficulty = state.difficulty;
			    enemy.playerCharacter = state.playerCharacter;
			    if (spawn.life >= 0) {
				    enemy.hp = spawn.life;
				    enemy.maxHp = spawn.life;
			    }
			    enemy.itemDrop = spawn.itemDrop;
			    if (spawn.score >= 0)
				    enemy.scoreValue = spawn.score;
			    // Install enemy-projectile callbacks.
			    enemy.onSpawnBullet = [&state,
			                           resMgr](const ECLBulletSpawn &spawn) {
				    int spriteIdx = getBulletSpriteIndex(spawn.bulletType,
				                                         spawn.bulletColor);
				    const char *bulletAtlas =
				        getBulletSpriteAtlas(spawn.bulletType);
				    auto tex = resMgr ? resMgr->getSpriteTexture(bulletAtlas,
				                                                 spriteIdx)
				                      : nullptr;
				    if (tex && tex->isValid()) {
					    GameState::EnemyBullet eb;
					    eb.sprite = shiki::Sprite(tex);
					    eb.sprite.setPosition(spawn.x, spawn.y);
					    float tw = static_cast<float>(tex->getWidth());
					    float th = static_cast<float>(tex->getHeight());
					    eb.sprite.setSourceRect(shiki::Rect(0, 0, tw, th));
					    eb.sprite.setOrigin({tw / 2.0f, th / 2.0f});
					    eb.sprite.setScale(1.0f, 1.0f);
					    eb.sprite.setBlendMode(
					        getTH06BulletBlendMode(spawn.bulletType));
					    eb.vx = spawn.vx;
					    eb.vy = spawn.vy;
					    eb.speedPerFrame = std::sqrt(spawn.vx * spawn.vx +
					                                 spawn.vy * spawn.vy) /
					                       60.0f;
					    eb.angle = spawn.angle;
					    eb.bulletType = spawn.bulletType;
					    eb.bulletColor = spawn.bulletColor;
					    eb.effectFlags = spawn.flags;
					    eb.effectFrames =
					        spawn.exInts[0] > 0 ? spawn.exInts[0] : 99999;
					    if ((spawn.flags & 0x10) != 0) {
						    const float accelerationAngle =
						        spawn.exFloats[1] <= -999.0f
						            ? eb.angle
						            : spawn.exFloats[1];
						    eb.accelerationX = std::cos(accelerationAngle) *
						                       spawn.exFloats[0] * 60.0f;
						    eb.accelerationY = std::sin(accelerationAngle) *
						                       spawn.exFloats[0] * 60.0f;
					    } else if ((spawn.flags & 0x20) != 0) {
						    eb.speedDelta = spawn.exFloats[0];
						    eb.angleDelta = spawn.exFloats[1];
					    }
					    if ((spawn.flags & (0x40 | 0x80 | 0x100)) != 0) {
						    eb.directionChangeAngle = spawn.exFloats[0];
						    eb.directionChangeSpeed = spawn.exFloats[1] >= 0.0f
						                                  ? spawn.exFloats[1]
						                                  : eb.speedPerFrame;
						    eb.directionChangeInterval = spawn.exInts[0];
						    eb.directionChangeMax = spawn.exInts[1];
					    }
					    if ((spawn.flags & (0x400 | 0x800)) != 0) {
						    eb.directionChangeSpeed = spawn.exFloats[0] >= 0.0f
						                                  ? spawn.exFloats[0]
						                                  : eb.speedPerFrame;
						    eb.directionChangeMax = spawn.exInts[0];
					    }
					    if (spawn.bulletType == 2 || spawn.bulletType == 4 ||
					        spawn.bulletType == 5 || spawn.bulletType == 7 ||
					        spawn.bulletType == 8)
						    eb.sprite.setRotation(
						        eb.angle * 180.0f / std::numbers::pi_v<float> -
						        90.0f);
					    const auto spawnAnimation = getTH06BulletSpawnAnimation(
					        spawn.bulletType, spawn.bulletColor, spawn.flags);
					    if ((spawn.flags & 0x2) != 0) {
						    auto spawnTexture =
						        resMgr ? resMgr->getSpriteTexture(
						                     spawnAnimation.atlas,
						                     spawnAnimation.spriteIndex)
						               : nullptr;
						    if (spawnTexture && spawnTexture->isValid()) {
							    eb.spawnEffectSprite =
							        shiki::Sprite(spawnTexture);
							    const float width = static_cast<float>(
							        spawnTexture->getWidth());
							    const float height = static_cast<float>(
							        spawnTexture->getHeight());
							    eb.spawnEffectSprite.setSourceRect(
							        {0.0f, 0.0f, width, height});
							    eb.spawnEffectSprite.setOrigin(
							        {width * 0.5f, height * 0.5f});
							    eb.spawnEffectSprite.setPosition(spawn.x,
							                                     spawn.y);
							    eb.spawnEffectSprite.setBlendMode(
							        getTH06BulletBlendMode(spawn.bulletType));
							    eb.fastSpawnFrames = 8;
						    }
					    } else {
						    eb.spawnAnimation = spawnAnimation;
						    if (eb.spawnAnimation.active()) {
							    auto spawnTexture =
							        resMgr ? resMgr->getSpriteTexture(
							                     eb.spawnAnimation.atlas,
							                     eb.spawnAnimation.spriteIndex)
							               : nullptr;
							    if (spawnTexture && spawnTexture->isValid()) {
								    eb.spawnEffectSprite =
								        shiki::Sprite(spawnTexture);
								    const float width = static_cast<float>(
								        spawnTexture->getWidth());
								    const float height = static_cast<float>(
								        spawnTexture->getHeight());
								    eb.spawnEffectSprite.setSourceRect(
								        {0.0f, 0.0f, width, height});
								    eb.spawnEffectSprite.setOrigin(
								        {width * 0.5f, height * 0.5f});
								    eb.spawnEffectSprite.setPosition(spawn.x,
								                                     spawn.y);
								    eb.spawnEffectSprite.setBlendMode(
								        getTH06BulletBlendMode(
								            spawn.bulletType));
								    eb.spawnEffectSprite.setScale(
								        eb.spawnAnimation.initialScale,
								        eb.spawnAnimation.initialScale);
								    eb.spawnEffectSprite.setColor(
								        {1.0f, 1.0f, 1.0f, 0.0f});
							    }
						    }
					    }
					    state.enemyBullets.push_back(std::move(eb));
				    } else {
					    static bool warned = false;
					    if (!warned) {
						    warned = true;
						    spdlog::warn("Enemy bullet texture lookup failed: "
						                 "type={}, color={}, "
						                 "{} sprite={}",
						                 spawn.bulletType, spawn.bulletColor,
						                 bulletAtlas, spriteIdx);
					    }
				    }
			    };
			    enemy.onSound = [&state](int soundId) {
				    playTH06Sound(state, soundId);
			    };
			    enemy.onBulletCancel = [&state]() {
				    turnEnemyProjectilesIntoPoints(state);
			    };
			    enemy.onDropItem = [&state](int type, float x, float y,
			                                int itemState) {
				    spawnTH06Item(state, type, x, y, itemState);
			    };
			    enemy.onSpawnEffect = [&state](int effectId, float x, float y,
			                                   int count, uint32_t color) {
				    state.effects.spawn(effectId, x, y, count, color);
			    };
			    enemy.onSpawnMovingEffect =
			        [&state](int effectId, float x, float y, uint32_t color,
			                 float vx, float vy, float ax, float ay) {
				        state.effects.spawnMoving(effectId, x, y, color, vx, vy,
				                                  ax, ay);
			        };
			    enemy.onBulletTransform = [&state, resMgr](
			                                  int instruction, int parameter,
			                                  float enemyX, float enemyY) {
				    auto randomAngle = [] {
					    return static_cast<float>(std::rand()) /
					           static_cast<float>(RAND_MAX) *
					           (2.0f * std::numbers::pi_v<
					                       float>)-std::numbers::pi_v<float>;
				    };
				    auto isLarge = [](const GameState::EnemyBullet &bullet) {
					    const auto &texture = bullet.sprite.getTexture();
					    return texture && texture->isValid() &&
					           texture->getHeight() >= 30;
				    };
				    auto setBulletColor = [&](GameState::EnemyBullet &bullet,
				                              int color) {
					    bullet.bulletColor = color;
					    const int spriteIndex =
					        getBulletSpriteIndex(bullet.bulletType, color);
					    auto texture =
					        resMgr
					            ? resMgr->getSpriteTexture(
					                  getBulletSpriteAtlas(bullet.bulletType),
					                  spriteIndex)
					            : nullptr;
					    if (!texture || !texture->isValid())
						    return;
					    bullet.sprite.setTexture(texture);
					    const float width =
					        static_cast<float>(texture->getWidth());
					    const float height =
					        static_cast<float>(texture->getHeight());
					    bullet.sprite.setSourceRect(
					        {0.0f, 0.0f, width, height});
					    bullet.sprite.setOrigin({width * 0.5f, height * 0.5f});
				    };
				    auto release = [&](GameState::EnemyBullet &bullet,
				                       float angle) {
					    bullet.effectFlags |= 0x10;
					    bullet.speedPerFrame = 0.01f;
					    bullet.angle = angle;
					    bullet.vx = std::cos(angle) * 0.01f * 60.0f;
					    bullet.vy = std::sin(angle) * 0.01f * 60.0f;
					    bullet.accelerationX = std::cos(angle) * 0.01f * 60.0f;
					    bullet.accelerationY = std::sin(angle) * 0.01f * 60.0f;
					    bullet.effectFrames = 120;
					    bullet.ageFrames = 0;
				    };

				    int largeCount = 0;
				    if (instruction == 0) {
					    state.effects.spawn(12, enemyX, enemyY, 1, 0xffffffff);
					    for (auto &bullet : state.enemyBullets) {
						    if (bullet.despawning)
							    continue;
						    setBulletColor(bullet, 15);
						    if (parameter == 0) {
							    bullet.speedPerFrame = 0.0f;
							    bullet.vx = 0.0f;
							    bullet.vy = 0.0f;
						    } else if (parameter == 1) {
							    release(bullet, randomAngle());
							    bullet.effectFrames = 220;
						    }
					    }
					    return 0;
				    }

				    if (instruction == 4) {
					    int remaining = state.difficulty <= 1 ? 14 : 52;
					    const auto playerPosition = state.player.getPosition();
					    for (auto &bullet : state.enemyBullets) {
						    if (remaining <= 0 || bullet.despawning ||
						        !isLarge(bullet) || bullet.bulletColor == 5 ||
						        std::rand() % 4 != 0)
							    continue;
						    setBulletColor(bullet, 5);
						    const auto position = bullet.sprite.getPosition();
						    const float dx = position.x - playerPosition.x;
						    const float dy = position.y - playerPosition.y;
						    float angle;
						    if (dx * dx + dy * dy > 128.0f * 128.0f) {
							    angle =
							        state.difficulty <= 1
							            ? static_cast<float>(std::rand()) /
							                      RAND_MAX *
							                      (3.0f *
							                       std::numbers::pi_v<float> /
							                       4.0f) +
							                  std::numbers::pi_v<float> / 4.0f
							            : randomAngle();
						    } else {
							    angle =
							        std::atan2(playerPosition.y - position.y,
							                   playerPosition.x - position.x) +
							        std::numbers::pi_v<float> / 2.0f +
							        randomAngle();
						    }
						    bullet.angle = angle;
						    bullet.vx =
						        std::cos(angle) * bullet.speedPerFrame * 60.0f;
						    bullet.vy =
						        std::sin(angle) * bullet.speedPerFrame * 60.0f;
						    --remaining;
					    }
					    return 0;
				    }
				    if (instruction == 8) {
					    std::vector<ECLBulletSpawn> additions;
					    additions.reserve(state.enemyBullets.size());
					    for (const auto &bullet : state.enemyBullets) {
						    if (!isLarge(bullet))
							    continue;
						    ++largeCount;
						    const auto position = bullet.sprite.getPosition();
						    ECLBulletSpawn spawn;
						    spawn.x = position.x;
						    spawn.y = position.y;
						    spawn.angle = randomAngle();
						    spawn.bulletType = 3;
						    spawn.bulletColor = 1;
						    additions.push_back(spawn);
					    }
					    // Use the same construction path as ECL bullets without
					    // mutating enemyBullets while it is being scanned.
					    for (const auto &spawn : additions) {
						    const int spriteIdx = getBulletSpriteIndex(
						        spawn.bulletType, spawn.bulletColor);
						    auto tex = resMgr ? resMgr->getSpriteTexture(
						                            getBulletSpriteAtlas(
						                                spawn.bulletType),
						                            spriteIdx)
						                      : nullptr;
						    if (!tex || !tex->isValid())
							    continue;
						    GameState::EnemyBullet bullet;
						    bullet.sprite = shiki::Sprite(tex);
						    bullet.sprite.setSourceRect(
						        {0.0f, 0.0f,
						         static_cast<float>(tex->getWidth()),
						         static_cast<float>(tex->getHeight())});
						    bullet.sprite.setOrigin(
						        {static_cast<float>(tex->getWidth()) * 0.5f,
						         static_cast<float>(tex->getHeight()) * 0.5f});
						    bullet.sprite.setPosition(spawn.x, spawn.y);
						    bullet.sprite.setBlendMode(
						        getTH06BulletBlendMode(spawn.bulletType));
						    bullet.angle = spawn.angle;
						    bullet.bulletType = spawn.bulletType;
						    state.enemyBullets.push_back(std::move(bullet));
					    }
					    return largeCount;
				    }

				    const float sharedRandomAngle = randomAngle();
				    if (instruction == 9 || instruction == 11) {
					    for (auto &bullet : state.enemyBullets) {
						    if (isLarge(bullet) || bullet.speedPerFrame != 0.0f)
							    continue;
						    const auto position = bullet.sprite.getPosition();
						    float angle = randomAngle();
						    if (instruction == 9) {
							    const float dx = enemyX - position.x;
							    const float dy = enemyY - position.y;
							    angle = std::sqrt(dx * dx + dy * dy) *
							                std::numbers::pi_v<float> / 256.0f +
							            sharedRandomAngle;
						    }
						    release(bullet, angle);
					    }
					    return 0;
				    }

				    for (const auto &large : state.enemyBullets) {
					    if (!isLarge(large))
						    continue;
					    ++largeCount;
					    const auto largePosition = large.sprite.getPosition();
					    const float enemyAngle = std::atan2(
					        largePosition.y - enemyY, largePosition.x - enemyX);
					    for (auto &bullet : state.enemyBullets) {
						    if (isLarge(bullet) || bullet.speedPerFrame != 0.0f)
							    continue;
						    const auto position = bullet.sprite.getPosition();
						    const float dx = position.x - largePosition.x;
						    const float dy = position.y - largePosition.y;
						    if (dx * dx + dy * dy >= 64.0f * 64.0f)
							    continue;
						    const float bulletAngle = std::atan2(
						        position.y - enemyY, position.x - enemyX);
						    release(bullet, (bulletAngle - enemyAngle) * 2.2f +
						                        enemyAngle);
					    }
				    }
				    return largeCount;
			    };
			    enemy.onTimeStop = [&state](bool stopped) {
				    state.eclTimeStopped = stopped;
			    };
			    enemy.onKillAllEnemies = [&state]() {
				    for (auto &other : state.eclEnemies) {
					    if (!other.isBoss) {
						    other.hp = 0;
						    if (!other.interactable &&
						        other.deathCallbackSub >= 0)
							    other.handleDamageCallbacks();
					    }
				    }
				    for (auto &other : state.pendingECLEnemies) {
					    other.hp = 0;
					    if (!other.interactable && other.deathCallbackSub >= 0)
						    other.handleDamageCallbacks();
				    }
			    };
			    enemy.onBossChanged = [&state](bool enabled) {
				    state.bossPresent = enabled;
			    };
			    enemy.onSpellStart = [&state, resMgr](int spellId,
			                                          int portraitId,
			                                          const std::string &name) {
				    state.spellActive = true;
				    state.spellCaptureEligible = true;
				    state.spellCardId = spellId;
				    state.spellCaptureScore =
				        spellId >= 0 &&
				                spellId < static_cast<int>(
				                              TH06_SPELL_CARD_SCORES.size())
				            ? TH06_SPELL_CARD_SCORES[static_cast<size_t>(
				                  spellId)]
				            : 0;
				    state.spellName = eclTextToUtf8(name);
				    state.spellBackgroundActive = true;
				    const int stage = state.stageNumber;
				    const bool finalBossSpell =
				        (stage == 6 || stage == 7) && portraitId >= 2;
				    const char *effectAtlas =
				        th06StageEffectAtlas(stage, finalBossSpell);
				    state.spellBackgroundAnm =
				        loadTH06MenuAnmFile(resMgr, effectAtlas);
				    state.spellBackgroundVm = {};
				    state.spellBackgroundVm.initialize(state.spellBackgroundAnm,
				                                       0);
				    state.spellBackgroundScrolls =
				        state.spellBackgroundVm.uvScrollY != 0.0f;
				    state.activeSpellBackgroundTexture =
				        resMgr->getSpriteTexture(
				            effectAtlas, state.spellBackgroundVm.sprite);
				    state.spellBackgroundFrame = 0;
				    turnEnemyProjectilesIntoPoints(state);
				    // ECL SPELLCARDSTART passes the sprite offset used by
				    // ANM_SPRITE_FACE_STAGE_START + spellcardSprite.
				    const auto portraitSpec =
				        resolveTH06StageFaceSprite(stage, portraitId);
				    auto portrait =
				        portraitSpec.atlas
				            ? resMgr->getSpriteTexture(portraitSpec.atlas,
				                                       portraitSpec.localSprite)
				            : nullptr;
				    beginTH06CardUi(state, false, state.spellName,
				                    std::move(portrait));
				    playTH06Sound(state, 14);
			    };
			    enemy.onSpellEnd = [&state](int secondsRemaining) {
				    const bool captured = state.spellCaptureEligible;
				    const int bonus = captured ? state.spellCaptureScore +
				                                     state.spellCaptureScore *
				                                         secondsRemaining / 10
				                               : 0;
				    if (captured)
					    addTH06Score(state, bonus);
				    state.spellResultUi = {bonus, 0, captured, true};
				    state.spellActive = false;
				    state.spellCaptureEligible = false;
				    state.spellCardId = -1;
				    state.spellCaptureScore = 0;
				    state.spellName.clear();
				    state.spellBackgroundActive = false;
				    state.spellBackgroundScrolls = false;
				    state.activeSpellBackgroundTexture.reset();
				    state.spellBackgroundAnm.reset();
				    state.spellBackgroundVm = {};
				    state.spellBackgroundFrame = 0;
				    endTH06CardUi(state, false);
				    turnEnemyProjectilesIntoPoints(state);
			    };
			    enemy.isGlobalSpellActive = [&state]() {
				    return state.spellActive;
			    };
			    enemy.onSpellCaptureFailed = [&state]() {
				    state.spellCaptureEligible = false;
			    };
			    enemy.onDamage = [&state]() { playTH06Sound(state, 20); };
			    enemy.onDeath = [&state](bool boss) {
				    playTH06Sound(state, boss ? 18 : 2);
				    if (boss) {
					    for (auto &other : state.eclEnemies) {
						    if (!other.isBoss)
							    other.hp = 0;
					    }
					    for (auto &other : state.pendingECLEnemies)
						    other.hp = 0;
					    state.bossPresent = false;
					    state.spellActive = false;
					    state.spellBackgroundActive = false;
					    state.spellBackgroundScrolls = false;
					    state.activeSpellBackgroundTexture.reset();
					    state.spellBackgroundAnm.reset();
					    state.spellBackgroundVm = {};
					    state.spellBackgroundFrame = 0;
					    endTH06CardUi(state, false);
					    turnEnemyProjectilesIntoPoints(state);
				    }
			    };
			    // Child enemies share callbacks from the outer enemy.
			    // via captures
			    auto spawnBulletFn = enemy.onSpawnBullet;
			    auto soundFn = enemy.onSound;
			    auto bulletCancelFn = enemy.onBulletCancel;
			    auto dropItemFn = enemy.onDropItem;
			    auto effectFn = enemy.onSpawnEffect;
			    auto movingEffectFn = enemy.onSpawnMovingEffect;
			    auto killAllFn = enemy.onKillAllEnemies;
			    auto bossFn = enemy.onBossChanged;
			    auto spellStartFn = enemy.onSpellStart;
			    auto spellEndFn = enemy.onSpellEnd;
			    auto spellActiveFn = enemy.isGlobalSpellActive;
			    auto spellCaptureFailedFn = enemy.onSpellCaptureFailed;
			    auto damageFn = enemy.onDamage;
			    auto deathFn = enemy.onDeath;
			    auto bulletTransformFn = enemy.onBulletTransform;
			    auto timeStopFn = enemy.onTimeStop;
			    enemy.onSpawnChildEnemy =
			        [&state, &parser, resMgr, spawnBulletFn, soundFn,
			         bulletCancelFn, dropItemFn, effectFn, movingEffectFn,
			         killAllFn, bossFn, spellStartFn, spellEndFn, spellActiveFn,
			         spellCaptureFailedFn, damageFn, deathFn, bulletTransformFn,
			         timeStopFn](int etype, int esub, float ex, float ey,
			                     float ez, float evx, float evy, float eax,
			                     float eay, int ehp) {
				        auto child =
				            createECLEnemy(parser, etype, esub, ex, ey, evx,
				                           evy, state.enemyTexture, resMgr);
				        child.z = ez;
				        child.stageNumber = state.stageNumber;
				        child.playerCharacter = state.playerCharacter;
				        child.difficulty = state.difficulty;
				        child.hp = ehp > 0 ? ehp : 10;
				        child.rowFlag = 0;
				        child.onSpawnBullet = spawnBulletFn;
				        child.onSound = soundFn;
				        child.onBulletCancel = bulletCancelFn;
				        child.onDropItem = dropItemFn;
				        child.onSpawnEffect = effectFn;
				        child.onSpawnMovingEffect = movingEffectFn;
				        child.onKillAllEnemies = killAllFn;
				        child.onBossChanged = bossFn;
				        child.onSpellStart = spellStartFn;
				        child.onSpellEnd = spellEndFn;
				        child.isGlobalSpellActive = spellActiveFn;
				        child.onSpellCaptureFailed = spellCaptureFailedFn;
				        child.onDamage = damageFn;
				        child.onDeath = deathFn;
				        child.onBulletTransform = bulletTransformFn;
				        child.onTimeStop = timeStopFn;
				        child.onSpawnChildEnemy =
				            nullptr; // prevent infinite nesting
				        child.initializeEcl();
				        state.pendingECLEnemies.push_back(std::move(child));
			        };
			    enemy.initializeEcl();
			    state.eclEnemies.push_back(std::move(enemy));
		    };
		eclEngine->setEnemySpawnCallback(spawnEnemyCallback);
		eclEngine->setBossInterruptCallback([&state](int32_t bossId,
		                                             int32_t interruptId) {
			for (auto &enemy : state.eclEnemies) {
				if (enemy.alive && enemy.isBoss && enemy.bossId == bossId) {
					const bool triggered = enemy.triggerInterrupt(interruptId);
					spdlog::info("Boss interrupt: boss={}, interrupt={}, "
					             "sub={}, result={}",
					             bossId, interruptId, enemy.currentSubId,
					             triggered);
					return triggered;
				}
			}
			spdlog::debug("Boss interrupt pending: boss={}, interrupt={}",
			              bossId, interruptId);
			return false;
		});
		eclEngine->setBossAliveCallback([&state](int32_t bossId) {
			return std::any_of(state.eclEnemies.begin(), state.eclEnemies.end(),
			                   [bossId](const ECLEnemy &enemy) {
				                   return enemy.alive && enemy.isBoss &&
				                          enemy.bossId == bossId;
			                   });
		});
		eclEngine->setDialogueStartCallback([&state](int32_t messageId) {
			startTH06Dialogue(state, messageId);
		});
		eclEngine->setDialogueActiveCallback([&state]() {
			return state.dialogue.active && !state.dialogue.eclResumeRequested;
		});

		eclEngine->start(); // Start the ECL runtime.
	} else {
		spdlog::warn("Failed to load ECL StageProgram: {}",
		             eclLoaded.error().message);
	}

	spdlog::info("Game initialized, starting game loop");
	spdlog::info(
	    "Controls: Arrow Keys - Move, Z - Shoot, X - Bomb, LShift - Focus");
	// Run the main loop.
	engine.run(
	    [&state, &eclEngine, &engine](float dt) {
		    updateTH06Fps(state, dt);
		    if (state.stageRestartRequested) {
			    const bool advancingStage = state.stageAdvanceRequested;
			    spdlog::info("Reloading TH06 stage {}", state.stageNumber);
			    state.eclEnemies.clear();
			    state.pendingECLEnemies.clear();
			    state.enemyBullets.clear();
			    state.playerBullets.clear();
			    state.items.clear();
			    state.effects.clear();
			    state.dialogue = {};
			    if (!loadTH06Dialogue(state.dialogue, state.resourceManager,
			                          state.stageNumber))
				    spdlog::warn("Failed to reload TH06 stage {} dialogue",
				                 state.stageNumber);
			    if (state.resourceManager &&
			        !state.stageBackground.load(
			            *state.resourceManager,
			            "th06.stage." + std::to_string(state.stageNumber)))
				    spdlog::warn("Failed to reload TH06 stage {} background",
				                 state.stageNumber);
			    state.cardUi = {};
			    state.spellResultUi = {};
			    state.stageClearUi = {};
			    state.bossPresent = false;
			    state.spellActive = false;
			    state.spellBackgroundActive = false;
			    state.activeSpellBackgroundTexture.reset();
			    state.spellBackgroundAnm.reset();
			    state.spellBackgroundVm = {};
			    state.spellBackgroundScrolls = false;
			    state.spellBackgroundFrame = 0;
			    state.eclTimeStopped = false;
			    if (!advancingStage) {
				    // Returning to the title discards the previous run's stock.
				    state.lives = 2;
				    state.bombs = 3;
				    state.power = state.debugMode ? 128 : 0;
				    state.score = 0;
				    state.guiScore = 0;
			    }
			    state.graze = 0;
			    state.pointItemsCollected = 0;
			    state.powerItemCountForScore = 0;
			    state.bombFrame = -1;
			    state.deathbombFrame = -1;
			    state.playerDeathTimer = -1;
			    state.playerBulletGraceFrames = 0;
			    state.invincibleTimer = 0.0f;
			    state.gameTime = 0.0f;
			    state.enemyLogicAccum = 0.0f;
			    state.bulletLogicAccum = 0.0f;
			    state.playerLogicAccum = 0.0f;
			    configurePlayerCharacter(state, state.playerCharacter);
			    state.currentSong = 0;
			    state.stageNameVm = {};
			    state.songNameVm = {};
			    if (state.stageTextAnm) {
				    state.stageNameVm.initialize(state.stageTextAnm, 0);
				    state.songNameVm.initialize(state.stageTextAnm, 1);
				    state.stageNameVm.visible = true;
				    state.songNameVm.visible = true;
			    }
			    eclEngine->shutdown();
			    auto reloaded =
			        engine.getResourceManager()->getAssetStore()
			            ? eclEngine->loadECLAsset(
			                  *engine.getResourceManager()->getAssetStore(),
			                  shiki::asset::AssetId::fromName(
			                      "th06.ecl.ecldata" +
			                      std::to_string(state.stageNumber)))
			            : shiki::Result<void>{std::unexpected(shiki::Error{
			                  shiki::ErrorDomain::Asset, 1,
			                  "TH06 asset package is not mounted"})};
			    if (!reloaded)
				    spdlog::warn("Failed to restart ECL stage: {}",
				                 reloaded.error().message);
			    else {
				    eclEngine->start();
				    spdlog::info("TH06 stage {} ECL started",
				                 state.stageNumber);
				    if (advancingStage && state.audioManager)
					    state.audioManager->playMusic(
					        th06StageMusicId(state.stageNumber, 0));
			    }
			    state.stageRestartRequested = false;
			    state.stageAdvanceRequested = false;
		    }
		    if (state.mainMenu.active) {
			    updateTH06MainMenu(state, engine, dt);
			    return;
		    }
		    const bool *keyboard = SDL_GetKeyboardState(nullptr);
		    const bool cancelPhysicallyDown = keyboard[SDL_SCANCODE_ESCAPE];
		    const bool quitPhysicallyDown = keyboard[SDL_SCANCODE_Q];
		    updateTH06PauseMenu(state, cancelPhysicallyDown,
		                        quitPhysicallyDown);
		    if (state.pauseMenu.active)
			    return;
		    const bool backspacePhysicallyDown =
		        keyboard[SDL_SCANCODE_BACKSPACE];
		    // Controller.cpp maps both DIK_LCONTROL and DIK_RCONTROL to
		    // TH_BUTTON_SKIP. GuiImpl::RunMsg checks IS_PRESSED every frame.
		    state.keyDialogueSkip =
		        keyboard[SDL_SCANCODE_LCTRL] || keyboard[SDL_SCANCODE_RCTRL];
		    const bool debugSkipPressed =
		        backspacePhysicallyDown && !state.debugSkipKeyDown;
		    state.debugSkipKeyDown = backspacePhysicallyDown;
		    handleInput(state, dt);
		    updateGame(state, dt);
		    // Consume only the physical key-down edge in this frame. There is
		    // no queued request that can survive a phase transition and fire
		    // later.
		    if (state.debugMode && debugSkipPressed) {
			    auto boss = std::find_if(
			        state.eclEnemies.begin(), state.eclEnemies.end(),
			        [](const ECLEnemy &enemy) {
				        return enemy.alive &&
				               (enemy.isBoss || enemy.bossId >= 0 ||
				                enemy.subId == 16 || enemy.subId == 31);
			        });
			    if (boss != state.eclEnemies.end()) {
				    const int previousSub = boss->currentSubId;
				    constexpr int DEBUG_BOSS_DAMAGE = 100000000;
				    boss->applyDebugDamage(DEBUG_BOSS_DAMAGE);
				    spdlog::info("Debug: dealt {} damage to boss phase {}",
				                 DEBUG_BOSS_DAMAGE, previousSub);
			    } else if (eclEngine) {
				    for (int frame = 0; frame < 20 * 60; ++frame)
					    eclEngine->update(1.0f / 60.0f);
				    spdlog::info(
				        "Debug: advanced stage timeline by 20 seconds");
			    }
		    }
		    // Advance the ECL runtime.
		    if (eclEngine && state.deathbombFrame < TRUE_DEATHBOMB_FRAMES) {
			    eclEngine->update(dt);
		    }
	    },
	    [&state, &renderer, &engine](float dt) {
		    if (state.mainMenu.active)
			    renderTH06MainMenu(state, renderer, engine);
		    else {
			    renderGame(state, renderer, engine, dt);
		    }
	    });

	spdlog::info("Game Over - Final Score: {}", state.score);
	return 0;
}

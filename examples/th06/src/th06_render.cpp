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
#include <format>
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

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif

namespace {
float th06RenderedTextWidth(std::string_view text) {
	float width = 0.0f;
	for (size_t index = 0; index < text.size();) {
		const auto lead = static_cast<unsigned char>(text[index]);
		if (lead < 0x80) {
			width += 8.0f;
			++index;
			continue;
		}
		width += 16.0f;
		index += lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
	}
	return width;
}
} // namespace

void renderGame(GameState &state, shiki::Renderer *renderer,
                shiki::frontend::Realtime &engine, float dt) {
	// Read the physical window size.
	int actualWidth, actualHeight;
	engine.getWindowSize(actualWidth, actualHeight);

	// Calculate layout from the physical window size.
	calculateGameLayout(state, actualWidth, actualHeight);

	const float canvasScale =
	    std::min(static_cast<float>(actualWidth) / TH06_CANVAS_WIDTH,
	             static_cast<float>(actualHeight) / TH06_CANVAS_HEIGHT);
	const int canvasWidth = std::max(
	    1, static_cast<int>(std::round(TH06_CANVAS_WIDTH * canvasScale)));
	const int canvasHeight = std::max(
	    1, static_cast<int>(std::round(TH06_CANVAS_HEIGHT * canvasScale)));
	const int canvasX = (actualWidth - canvasWidth) / 2;
	const int canvasY = (actualHeight - canvasHeight) / 2;
	renderer->setOutputSize(actualWidth, actualHeight);
	renderer->setViewport(canvasX, canvasY, canvasWidth, canvasHeight);
	renderer->setProjection(0.0f, TH06_CANVAS_WIDTH, TH06_CANVAS_HEIGHT, 0.0f);
	renderer->setPlayfieldRegion(
	    state.playfieldRegion.x, state.playfieldRegion.y,
	    state.playfieldRegion.width, state.playfieldRegion.height);

	const float logicWidth = TH06_CANVAS_WIDTH;
	const float logicHeight = TH06_CANVAS_HEIGHT;

	// Cover the complete logical canvas with the neutral backing color.
	shiki::Sprite bgSprite;
	renderer->drawRect({0.0f, 0.0f, logicWidth, logicHeight},
	                   {0.2f, 0.2f, 0.2f, 1.0f});

	constexpr float uiLogicX = 416.0f;
	const float uiLogicWidth = logicWidth - uiLogicX;

	// Stage.cpp renders STD z-levels 0-3 through the stage camera before game
	// objects. Keep the flat image only as a missing-data fallback.
	const bool drawStageBackground =
	    !state.spellBackgroundActive || state.spellBackgroundFrame <= 60;
	if (drawStageBackground && state.stageBackground.isLoaded()) {
		state.stageBackground.render(*renderer, state.gameTime * 60.0f);
	} else if (drawStageBackground && state.bgTexture &&
	           state.bgTexture->isValid()) {
		shiki::Sprite bgGameSprite(state.bgTexture);
		float bgW = static_cast<float>(state.bgTexture->getWidth());
		float bgH = static_cast<float>(state.bgTexture->getHeight());
		bgGameSprite.setSourceRect(shiki::Rect(0, 0, bgW, bgH));
		bgGameSprite.setPosition(0.0f, 0.0f);
		bgGameSprite.setScale(GAME_WIDTH / bgW, GAME_HEIGHT / bgH);
		renderer->drawSprite(bgGameSprite, 0.0f, true);
	} else if (drawStageBackground) {
		// Fall back to a black playfield background.
		renderer->drawRect({0.0f, 0.0f, GAME_WIDTH, GAME_HEIGHT},
		                   {0.0f, 0.0f, 0.0f, 1.0f}, true);
	}

	if (state.spellBackgroundActive) {
		const float transition =
		    std::clamp(state.spellBackgroundFrame / 60.0f, 0.0f, 1.0f);
		renderer->drawColoredQuad({{{0.0f, 0.0f},
		                            {GAME_WIDTH, 0.0f},
		                            {0.0f, GAME_HEIGHT},
		                            {GAME_WIDTH, GAME_HEIGHT}}},
		                          {{{0.0f, 0.0f, 0.0f, transition},
		                            {0.0f, 0.0f, 0.0f, transition},
		                            {0.0f, 0.0f, 0.0f, transition},
		                            {0.0f, 0.0f, 0.0f, transition}}},
		                          shiki::BlendMode::Alpha, true);
		const auto &vm = state.spellBackgroundVm;
		if (vm.file && vm.sprite >= 0 && state.resourceManager) {
			auto texture = state.resourceManager->getSpriteTexture(
			    vm.file->atlas, vm.sprite);
			if (texture && texture->isValid()) {
				const float width = static_cast<float>(texture->getWidth());
				const float height = static_cast<float>(texture->getHeight());
				const float vmAlpha =
				    static_cast<float>((vm.color >> 24) & 0xff) / 255.0f;
				const shiki::Color color{
				    static_cast<float>((vm.color >> 16) & 0xff) / 255.0f,
				    static_cast<float>((vm.color >> 8) & 0xff) / 255.0f,
				    static_cast<float>(vm.color & 0xff) / 255.0f,
				    std::min(vmAlpha, transition)};
				const auto drawBackgroundAt = [&](float x, float y,
				                                  float scaleX, float scaleY,
				                                  bool topLeft) {
					shiki::Sprite sprite(texture);
					sprite.setSourceRect({0.0f, 0.0f, width, height});
					sprite.setOrigin(
					    topLeft ? shiki::Vec2{0.0f, 0.0f}
					            : shiki::Vec2{width * 0.5f, height * 0.5f});
					sprite.setPosition(x, y);
					sprite.setScale(scaleX, scaleY);
					sprite.setRotation(-vm.rotation * 180.0f /
					                   std::numbers::pi_v<float>);
					sprite.setColor(color);
					sprite.setBlendMode(vm.additive ? shiki::BlendMode::Add
					                                : shiki::BlendMode::Alpha);
					renderer->drawSprite(sprite, 0.0f, true);
				};

				if (vm.uvScrollY != 0.0f) {
					float scaleX = vm.scaleX;
					float scaleY = vm.scaleY;
					if (vm.file->atlas == "eff04")
						scaleX = scaleY = (GAME_WIDTH * 0.5f) / width;
					const float tileWidth = width * std::abs(scaleX);
					const float tileHeight = height * std::abs(scaleY);
					const float scroll =
					    std::fmod(vm.uvOffsetY * tileHeight, tileHeight);
					for (float x = 0.0f; x < GAME_WIDTH; x += tileWidth)
						for (float y = scroll - tileHeight; y < GAME_HEIGHT;
						     y += tileHeight)
							drawBackgroundAt(x, y, scaleX, scaleY, true);
				} else {
					if (vm.topLeft)
						drawBackgroundAt(0.0f, 0.0f, GAME_WIDTH / width,
						                 GAME_HEIGHT / height, true);
					else
						drawBackgroundAt(GAME_WIDTH * 0.5f, GAME_HEIGHT * 0.5f,
						                 vm.scaleX, vm.scaleY, false);
				}
			}
		}
	}

	// The stage fog affects the 3D scene. TH06 draws the spell background after
	// that scene, so applying this mask to it changes eff06 from red to gray.
	if (!state.spellBackgroundActive)
		renderer->drawColoredQuad({{{0.0f, 0.0f},
		                            {GAME_WIDTH, 0.0f},
		                            {0.0f, GAME_HEIGHT},
		                            {GAME_WIDTH, GAME_HEIGHT}}},
		                          {{{0.0f, 0.0f, 0.0f, 0.18f},
		                            {0.0f, 0.0f, 0.0f, 0.18f},
		                            {0.0f, 0.0f, 0.0f, 0.18f},
		                            {0.0f, 0.0f, 0.0f, 0.18f}}},
		                          shiki::BlendMode::Alpha, true);

	// Player::OnUpdate keeps drawing the player through DEAD and SPAWNING.
	// Apply the original squash/stretch and alpha progression to the render
	// copy so directional animation can continue to own the gameplay sprite.
	const bool invincibilityFlash =
	    state.playerDeathTimer < 0 && state.invincibleTimer > 0.0f &&
	    static_cast<int>(state.invincibleTimer * 60.0f) % 8 < 2;
	if (!invincibilityFlash || state.playerDeathTimer >= 0) {
		// Renderer maps gameplay coordinates to the playfield.
		shiki::Sprite playerSprite = state.player;
		if (state.playerDeathTimer >= 6 && state.playerDeathTimer < 36) {
			const float progress =
			    static_cast<float>(state.playerDeathTimer - 6) / 30.0f;
			playerSprite.setScale(1.0f - progress, 1.0f + 3.0f * progress);
			playerSprite.setColor({1.0f, 1.0f, 1.0f, 1.0f - progress});
			playerSprite.setBlendMode(shiki::BlendMode::Add);
		} else if (state.playerDeathTimer >= 36) {
			const float progress = std::clamp(
			    static_cast<float>(state.playerDeathTimer - 36) / 30.0f, 0.0f,
			    1.0f);
			playerSprite.setScale(progress, 3.0f - 2.0f * progress);
			playerSprite.setColor({1.0f, 1.0f, 1.0f, progress});
			playerSprite.setBlendMode(shiki::BlendMode::Add);
		}
		renderer->drawSprite(playerSprite, 0.0f, true);

		// Draw the player hitbox while focused.
		if (state.playerDeathTimer < 0 && state.debugEnemyHitboxes) {
			auto playerPos = state.player.getPosition();
			renderer->drawRect({playerPos.x - TH06_PLAYER_HITBOX_HALF_SIZE,
			                    playerPos.y - TH06_PLAYER_HITBOX_HALF_SIZE,
			                    TH06_PLAYER_HITBOX_HALF_SIZE * 2.0f,
			                    TH06_PLAYER_HITBOX_HALF_SIZE * 2.0f},
			                   {0.0f, 0.0f, 1.0f, 0.7f}, true);
		}
	}

	const char *optionAtlas =
	    state.playerCharacter == 0 ? "player00" : "player01";
	for (size_t index = 0;
	     state.playerDeathTimer < 0 && index < state.playerOptionVms.size();
	     ++index) {
		const auto &vm = state.playerOptionVms[index];
		if (!vm.visible || vm.sprite < 0 || !state.resourceManager)
			continue;
		auto texture =
		    state.resourceManager->getSpriteTexture(optionAtlas, vm.sprite);
		if (!texture || !texture->isValid())
			continue;
		shiki::Sprite option(texture);
		const float width = static_cast<float>(texture->getWidth());
		const float height = static_cast<float>(texture->getHeight());
		option.setSourceRect({0.0f, 0.0f, width, height});
		option.setOrigin({width * 0.5f, height * 0.5f});
		option.setPosition(state.playerOptionPositions[index]);
		option.setScale(vm.scaleX, vm.scaleY);
		option.setRotation(-vm.rotation * 180.0f / std::numbers::pi_v<float>);
		option.setColor({((vm.color >> 16) & 0xff) / 255.0f,
		                 ((vm.color >> 8) & 0xff) / 255.0f,
		                 (vm.color & 0xff) / 255.0f,
		                 ((vm.color >> 24) & 0xff) / 255.0f});
		if (vm.additive)
			option.setBlendMode(shiki::BlendMode::Add);
		renderer->drawSprite(option, 0.05f, true);
	}

	// Draw player shots in gameplay coordinates.
	for (auto &bullet : state.playerBullets) {
		renderer->drawSprite(bullet.sprite, 0.1f, true);
	}

	if (state.bombFrame >= 0) {
		const float darkness =
		    state.bombFrame < 60
		        ? state.bombFrame * 176.0f / 60.0f
		        : (state.bombFrame >= 240
		               ? (300 - state.bombFrame) * 176.0f / 60.0f
		               : 176.0f);
		renderer->drawRect(
		    {0.0f, 0.0f, GAME_WIDTH, GAME_HEIGHT},
		    {0.0f, 0.0f, 0.0f, std::clamp(darkness, 0.0f, 176.0f) / 255.0f},
		    true);
	}

	for (const auto &orb : state.bombOrbs) {
		if (orb.state == 0)
			continue;
		for (const auto &vm : orb.vms) {
			if (!vm.visible)
				continue;
			shiki::Sprite sprite;
			if (!applyTH06AnmVmSprite(state, vm, sprite))
				continue;
			sprite.setPosition(orb.position.x + vm.offsetX,
			                   orb.position.y + vm.offsetY);
			// BombReimuADraw explicitly uses DrawNoRotation.
			sprite.setRotation(0.0f);
			renderer->drawSprite(sprite, 20.0f, true);
		}
	}
	if (state.playerCharacter == 1 && state.bombFrame >= 0) {
		const auto playerPosition = state.player.getPosition();
		for (size_t index = 0; index < state.marisaBombVms.size(); ++index) {
			const auto &vm = state.marisaBombVms[index];
			if (!vm.visible || vm.sprite < 0 || !state.resourceManager)
				continue;
			shiki::Sprite sprite;
			if (!applyTH06AnmVmSprite(state, vm, sprite))
				continue;
			const float height =
			    static_cast<float>(sprite.getTexture()->getHeight());
			const float angle =
			    -3.0f * std::numbers::pi_v<float> / 5.0f +
			    static_cast<float>(index) * std::numbers::pi_v<float> / 15.0f;
			sprite.setPosition(
			    playerPosition.x + std::cos(angle) * height * vm.scaleY * 0.5f,
			    playerPosition.y + std::sin(angle) * height * vm.scaleY * 0.5f);
			sprite.setRotation(-(std::numbers::pi_v<float> * 1.5f - angle) *
			                   180.0f / std::numbers::pi_v<float>);
			renderer->drawSprite(sprite, 20.0f, true);
		}
	}

	for (const auto &item : state.items)
		renderer->drawSprite(item.sprite, 0.15f, true);

	// Draw ECL enemies.
	renderECLEnemies(state.eclEnemies, renderer,
	                 state.deathbombFrame >= TRUE_DEATHBOMB_FRAMES);
	state.effects.render(*renderer);
	const auto deathbombEffect =
	    sampleTH06DeathbombEffect(state.deathbombFrame);
	if (deathbombEffect.active && state.resourceManager) {
		auto texture = state.resourceManager->getSpriteTexture("etama4", 23);
		if (texture && texture->isValid()) {
			shiki::Sprite ring(texture);
			const float width = static_cast<float>(texture->getWidth());
			const float height = static_cast<float>(texture->getHeight());
			ring.setSourceRect({0.0f, 0.0f, width, height});
			ring.setOrigin({width * 0.5f, height * 0.5f});
			ring.setPosition(state.player.getPosition());
			ring.setScale(deathbombEffect.scale, deathbombEffect.scale);
			const uint32_t displayColor =
			    th06AdditiveEffectDisplayColor(0xff4040ff);
			ring.setColor({((displayColor >> 16) & 0xff) / 255.0f,
			               ((displayColor >> 8) & 0xff) / 255.0f,
			               (displayColor & 0xff) / 255.0f,
			               ((displayColor >> 24) & 0xff) / 255.0f *
			                   deathbombEffect.alpha});
			ring.setBlendMode(shiki::BlendMode::Add);
			renderer->drawSprite(ring, 0.2f, true);
		}
	}

	// Draw enemy bullets in gameplay coordinates.
	for (auto &bullet : state.enemyBullets) {
		shiki::Sprite sprite = bullet.sprite;
		if (bullet.despawning && bullet.despawnSprite.getTexture())
			sprite = bullet.despawnSprite;
		else if ((bullet.fastSpawnFrames > 0 ||
		          bullet.spawnAnimation.active()) &&
		         bullet.spawnEffectSprite.getTexture())
			sprite = bullet.spawnEffectSprite;
		if (state.deathbombFrame >= TRUE_DEATHBOMB_FRAMES) {
			const float alpha = sprite.getColor().w;
			sprite.setColor({1.0f, 0.12f, 0.12f, alpha});
		}
		renderer->drawSprite(sprite, 0.2f, true);
	}

	if (state.debugEnemyHitboxes) {
		const auto drawBox = [&](float left, float top, float width,
		                         float height) {
			renderer->drawRect({left, top, width, height},
			                   {1.0f, 0.0f, 0.0f, 0.65f}, true);
		};
		for (const auto &bullet : state.enemyBullets) {
			if (bullet.despawning || bullet.fastSpawnFrames > 0 ||
			    bullet.spawnAnimation.active())
				continue;
			const auto position = bullet.sprite.getPosition();
			const auto size = getTH06BulletCollisionSize(bullet.bulletType);
			drawBox(position.x - size.x * 0.5f, position.y - size.y * 0.5f,
			        size.x, size.y);
		}
	}

	if (state.bossUiOpacity > 0.0f && !state.dialogue.active) {
		// front.anm script 19 selects sprite 12 and moves it from global x=416
		// to x=56 in 40 frames. VM 21 selects sprite 13; DrawGameScene then
		// overrides its global position to (96, 24) and stretches its 14 px
		// width.
		const float entry = std::clamp(state.bossUiFrame / 40.0f, 0.0f, 1.0f);
		const float frameX = std::lerp(384.0f, 24.0f, entry);
		drawTH06FrontSpriteTransformed(
		    state, renderer, 12, frameX, 8.0f, 1.0f, 1.0f,
		    {1.0f, 1.0f, 1.0f, state.bossUiOpacity}, 80.0f, false, true);
		drawTH06FrontSpriteTransformed(
		    state, renderer, 13, 64.0f, 8.0f,
		    state.bossHealthDisplayed * 288.0f / 14.0f, 0.3f,
		    {1.0f, 1.0f, 1.0f, state.bossUiOpacity}, 81.0f, true, true);

		drawTH06AsciiText(
		    state, renderer, std::to_string(state.bossUiLifeCount), 48.0f, 0.0f,
		    {1.0f, 1.0f, 0.5f, state.bossUiOpacity}, 1.0f, 92.0f, true);
		shiki::Color timerColor{0.63f, 0.82f, 1.0f, state.bossUiOpacity};
		if (state.bossUiSeconds < 5)
			timerColor = {1.0f, 0.25f, 0.25f, state.bossUiOpacity};
		else if (state.bossUiSeconds < 10)
			timerColor = {0.88f, 0.5f, 0.75f, state.bossUiOpacity};
		else if (state.bossUiSeconds < 20)
			timerColor = {0.63f, 0.5f, 1.0f, state.bossUiOpacity};
		drawTH06AsciiText(state, renderer,
		                  std::format("{:02}", state.bossUiSeconds), 352.0f,
		                  0.0f, timerColor, 1.0f, 92.0f, true);
	}

	const auto drawStageVmText = [&](const TH06MenuAnmVm &vm,
	                                 std::string_view text, bool centered) {
		if (!vm.visible || text.empty())
			return;
		const float alpha =
		    static_cast<float>((vm.color >> 24) & 0xff) / 255.0f;
		if (alpha <= 0.0f)
			return;
		// The original text is rasterized into the ANM sprite first.  Its
		// position is the sprite center, while the ASCII stage label occupies
		// the line above it.  Keep the two logical lines separate here.
		const float scaleX = std::abs(vm.scaleX);
		const float scaleY = std::abs(vm.scaleY);
		const float width = th06RenderedTextWidth(text) * scaleX;
		const float x =
		    centered ? vm.x - width * 0.5f : vm.x + 256.0f * scaleX - width;
		const float y = centered ? vm.y + 8.0f * scaleY : vm.y;
		drawTH06Text(renderer, std::string(text), {x, y}, 15.0f,
		             {224.0f / 255.0f, 1.0f, 1.0f, alpha}, scaleX, true);
	};
	if (state.stageNameVm.visible) {
		drawStageVmText(state.stageNameVm, state.stageBackground.name(), true);
		const float alpha =
		    static_cast<float>((state.stageNameVm.color >> 24) & 0xff) / 255.0f;
		const std::string stageLabel =
		    state.extraStage ? "EXTRA STAGE"
		                     : std::format("STAGE {}", state.stageNumber);
		const float stageLabelX =
		    (GAME_WIDTH - static_cast<float>(stageLabel.size()) * 14.0f) * 0.5f;
		drawTH06AsciiText(state, renderer, stageLabel, stageLabelX, 198.0f,
		                  {1.0f, 1.0f, 0.25f, alpha}, 1.0f, 94.0f, true);
	}
	const auto &songNames = state.stageBackground.songNames();
	if (state.currentSong >= 0 &&
	    state.currentSong < static_cast<int>(songNames.size()) &&
	    !songNames[static_cast<size_t>(state.currentSong)].empty()) {
		drawStageVmText(state.songNameVm,
		                std::string("\xe2\x99\xaa") +
		                    songNames[static_cast<size_t>(state.currentSong)],
		                false);
	}

	if (uiLogicWidth > 0.0f) {
		// Draw fullscreen letterbox bars as part of the UI pass. They use the
		// same 32x32 front.anm backing tile as the original 640x480 interface.
		drawTH06WindowUiBacking(state, renderer, actualWidth, actualHeight,
		                        canvasX, canvasY, canvasWidth, canvasHeight,
		                        canvasScale);

		// Gui::DrawGameScene tiles front.anm scripts 6/7/8 around the exact
		// (32,16)-(416,464) playfield rectangle.
		for (float y = 0.0f; y < 464.0f; y += 32.0f) {
			drawTH06FrontSprite(state, renderer, 5, 0.0f, y, 79.0f);
			for (float x = uiLogicX; x < 624.0f; x += 32.0f)
				drawTH06FrontSprite(state, renderer, 5, x, y, 79.0f);
		}
		for (float x = 32.0f; x < 416.0f; x += 32.0f) {
			drawTH06FrontSprite(state, renderer, 6, x, 0.0f, 79.0f);
			drawTH06FrontSprite(state, renderer, 7, x, 464.0f, 79.0f);
		}

		// Gui::DrawGameScene draws VM 5 first, then 0,1,3,4,2. These are the
		// settled front.anm positions after the 120/240-frame entrance.
		drawTH06FrontSpriteTransformed(state, renderer, 14, 528.0f, 376.0f,
		                               1.0f, 1.0f, {1.0f, 1.0f, 1.0f, 1.0f},
		                               80.0f, false);
		const shiki::Color logoTextColor{1.0f, 1.0f, 1.0f, 160.0f / 255.0f};
		drawTH06FrontSpriteTransformed(state, renderer, 0, 472.0f, 320.0f, 1.0f,
		                               1.0f, logoTextColor, 81.0f, false);
		drawTH06FrontSpriteTransformed(state, renderer, 1, 528.0f, 320.0f, 1.0f,
		                               1.0f, logoTextColor, 81.0f, false);
		drawTH06FrontSpriteTransformed(state, renderer, 3, 528.0f, 432.0f, 1.0f,
		                               1.0f, logoTextColor, 81.0f, false);
		drawTH06FrontSpriteTransformed(state, renderer, 4, 584.0f, 432.0f, 1.0f,
		                               1.0f, logoTextColor, 81.0f, false);
		drawTH06FrontSpriteTransformed(state, renderer, 2, 528.0f, 376.0f, 1.0f,
		                               1.0f, logoTextColor, 81.0f, false);

		// front.anm scripts 9-15 define these exact sprite/position pairs.
		drawTH06FrontSprite(state, renderer, 8, 432.0f, 82.0f);
		drawTH06FrontSprite(state, renderer, 9, 432.0f, 58.0f);
		drawTH06FrontSprite(state, renderer, 10, 432.0f, 122.0f);
		drawTH06FrontSprite(state, renderer, 11, 432.0f, 146.0f);
		drawTH06FrontSprite(state, renderer, 15, 432.0f, 186.0f);
		drawTH06FrontSprite(state, renderer, 16, 432.0f, 206.0f);
		drawTH06FrontSprite(state, renderer, 20, 432.0f, 226.0f);

		const auto drawNumber = [&](const std::string &text, float y) {
			drawTH06AsciiText(state, renderer, text, 496.0f, y);
		};
		drawNumber(
		    std::format("{:09}", std::max(state.highScore, state.guiScore)),
		    58.0f);
		drawNumber(std::format("{:09}", state.guiScore), 82.0f);
		drawNumber(std::to_string(state.graze), 206.0f);
		drawNumber(std::to_string(state.pointItemsCollected), 226.0f);

		for (int i = 0; i < state.lives; ++i)
			drawTH06FrontSprite(state, renderer, 17, 496.0f + i * 16.0f,
			                    122.0f);
		for (int i = 0; i < state.bombs; ++i)
			drawTH06FrontSprite(state, renderer, 18, 496.0f + i * 16.0f,
			                    146.0f);

		if (state.power > 0) {
			shiki::Sprite powerBar;
			powerBar.setPosition(496.0f, 186.0f);
			powerBar.setSourceRect({0.0f, 0.0f, 1.0f, 1.0f});
			powerBar.setScale(static_cast<float>(state.power), 16.0f);
			powerBar.setColor({0.45f, 0.75f, 1.0f, 0.8f});
			renderer->drawSprite(powerBar, 80.0f);
		}
		if (state.power >= 128)
			drawTH06FrontSprite(state, renderer, 19, 496.0f, 186.0f, 91.0f);
		else
			drawNumber(std::to_string(state.power), 186.0f);
	}
	if (state.stageClearUi.active) {
		float y = 112.0f;
		const auto drawResultLine = [&](std::string_view text,
		                                shiki::Color color) {
			drawTH06AsciiText(state, renderer, text, 42.0f, y, color, 1.0f,
			                  96.0f, true);
			y += 16.0f;
		};
		drawResultLine("All Clear!", {1.0f, 0.85f, 0.25f, 1.0f});
		y += 16.0f;
		drawResultLine("Stage * 1000 =  7000", {1.0f, 1.0f, 1.0f, 1.0f});
		drawResultLine(std::format("Power *  100 = {:5}", state.power * 100),
		               {0.75f, 0.65f, 1.0f, 1.0f});
		drawResultLine(std::format("Graze *   10 = {:5}", state.graze * 10),
		               {0.5f, 0.85f, 1.0f, 1.0f});
		drawResultLine(
		    std::format("    * Point Item {:3}", state.pointItemsCollected),
		    {1.0f, 0.55f, 0.65f, 1.0f});
		drawResultLine(std::format("Player    = {:8}", state.lives * 3000000),
		               {1.0f, 1.0f, 0.55f, 1.0f});
		drawResultLine(std::format("Bomb      = {:8}", state.bombs * 1000000),
		               {1.0f, 1.0f, 0.55f, 1.0f});
		y += 16.0f;
		drawResultLine("Extra Rank     * 2.0", {1.0f, 0.55f, 0.65f, 1.0f});
		drawResultLine(
		    std::format("Total     = {:8}", state.stageClearUi.score),
		    {1.0f, 1.0f, 1.0f, 1.0f});
	}
	if (state.spellResultUi.active) {
		const std::string_view title = state.spellResultUi.captured
		                                   ? "Spell Card Bonus!"
		                                   : "Spell Card Failed";
		const float titleX =
		    (GAME_WIDTH - static_cast<float>(title.size()) * 16.0f) * 0.5f;
		drawTH06AsciiText(state, renderer, title, titleX, 64.0f,
		                  state.spellResultUi.captured
		                      ? shiki::Color{1.0f, 0.2f, 0.2f, 1.0f}
		                      : shiki::Color{0.6f, 0.6f, 0.6f, 1.0f},
		                  1.0f, 98.0f, true);
		if (state.spellResultUi.captured) {
			const std::string score =
			    std::format("+{}", state.spellResultUi.score);
			const float scoreX =
			    (GAME_WIDTH - static_cast<float>(score.size()) * 32.0f) * 0.5f;
			drawTH06AsciiText(state, renderer, score, scoreX, 80.0f,
			                  {1.0f, 0.55f, 0.65f, 1.0f}, 2.0f, 98.0f, true);
		}
	}
	if (state.dialogue.active) {
		for (size_t i = 0; i < state.dialogue.portraits.size(); ++i) {
			const auto &portraitState = state.dialogue.portraits[i];
			const auto &texture = portraitState.texture;
			if (!portraitState.visible || !texture || !texture->isValid())
				continue;
			shiki::Sprite portrait(texture);
			const float width = static_cast<float>(texture->getWidth());
			const float height = static_cast<float>(texture->getHeight());
			portrait.setSourceRect({0.0f, 0.0f, width, height});
			portrait.setOrigin(
			    {portraitState.originX > 0.0f ? portraitState.originX
			                                  : width * 0.5f,
			     portraitState.originY > 0.0f ? portraitState.originY
			                                  : height * 0.5f});
			portrait.setPosition(portraitState.x, portraitState.y);
			portrait.setColor({1.0f, 1.0f, 1.0f, portraitState.alpha});
			renderer->drawSprite(portrait, 88.0f, true);
		}

		// GuiImpl::DrawDialogue builds a 288x48 gradient quad at global y=384.
		// This example's playfield origin is (32, 16), hence local (48, 368).
		const float boxHeight =
		    std::min(state.dialogue.timerFrames, 60) * 48.0f / 60.0f;
		constexpr int DIALOGUE_BANDS = 12;
		for (int band = 0; band < DIALOGUE_BANDS; ++band) {
			const float top = boxHeight * band / DIALOGUE_BANDS;
			const float bottom = boxHeight * (band + 1) / DIALOGUE_BANDS;
			const float progress = (band + 0.5f) / DIALOGUE_BANDS;
			shiki::Sprite dialogueBand;
			dialogueBand.setPosition(48.0f, 368.0f + top);
			dialogueBand.setSourceRect({0.0f, 0.0f, 1.0f, 1.0f});
			dialogueBand.setScale(288.0f, bottom - top);
			dialogueBand.setColor(
			    {0.0f, 0.0f, 0.0f,
			     std::lerp(208.0f, 144.0f, progress) / 255.0f});
			renderer->drawSprite(dialogueBand, 90.0f, true);
		}
		const float line0Alpha =
		    std::clamp(state.dialogue.lineFrames[0] / 12.0f, 0.0f, 1.0f);
		const float line1Alpha =
		    std::clamp(state.dialogue.lineFrames[1] / 12.0f, 0.0f, 1.0f);
		drawTH06Text(renderer, state.dialogue.lines[0], {64.0f, 372.0f}, 15.0f,
		             {232.0f / 255.0f, 240.0f / 255.0f, 1.0f, line0Alpha}, 1.0f,
		             true);
		drawTH06Text(renderer, state.dialogue.lines[1], {64.0f, 396.0f}, 15.0f,
		             {1.0f, 232.0f / 255.0f, 240.0f / 255.0f, line1Alpha}, 1.0f,
		             true);
	}
	if (state.cardUi.active) {
		const auto decelerate = [](float value) {
			value = std::clamp(value, 0.0f, 1.0f);
			return 1.0f - (1.0f - value) * (1.0f - value);
		};
		const int frame = state.cardUi.frame;

		// face00a.anm scripts 1 and 3: 30-frame decelerating entry, hold until
		// frame 90, then fade while growing for another 30 frames.
		if (frame < 120 && state.cardUi.portrait &&
		    state.cardUi.portrait->isValid()) {
			const float entry = decelerate(frame / 30.0f);
			const float startX = state.cardUi.player ? -64.0f : 448.0f;
			const float targetX = state.cardUi.player ? 128.0f : 288.0f;
			const float y = state.cardUi.player ? 256.0f : 224.0f;
			float alpha = 224.0f / 255.0f;
			float portraitScale = 1.0f;
			if (frame >= 90) {
				const float exit = (frame - 90) / 30.0f;
				alpha *= 1.0f - exit;
				portraitScale += exit * 2.0f;
			}
			shiki::Sprite portrait(state.cardUi.portrait);
			const float width =
			    static_cast<float>(state.cardUi.portrait->getWidth());
			const float height =
			    static_cast<float>(state.cardUi.portrait->getHeight());
			portrait.setSourceRect({0.0f, 0.0f, width, height});
			portrait.setOrigin({width * 0.5f, height * 0.5f});
			portrait.setPosition(std::lerp(startX, targetX, entry), y);
			portrait.setScale(portraitScale, portraitScale);
			portrait.setColor({1.0f, 1.0f, 1.0f, alpha});
			renderer->drawSprite(portrait, 50.0f, true);
		}

		// text.anm scripts 6/7 use a 256x16 text VM. Both shrink from 3x to 1x
		// over 30 frames, then move at frame 100 and wait for interrupt 1.
		const float nameEntry = std::clamp(frame / 30.0f, 0.0f, 1.0f);
		const float nameScale = std::lerp(3.0f, 1.0f, nameEntry);
		const float nameAlpha = nameEntry;
		const float nameX = state.cardUi.player ? 160.0f : 224.0f;
		const float startY = state.cardUi.player ? 328.0f : 296.0f;
		const float stopY = state.cardUi.player ? 424.0f : 24.0f;
		const float move = decelerate((frame - 100) / 30.0f);
		float textX = nameX;
		const float textY = std::lerp(startY, stopY, move);
		if (state.cardUi.endFrame >= 0) {
			const float exit = decelerate(state.cardUi.endFrame / 30.0f);
			textX =
			    std::lerp(nameX, state.cardUi.player ? -128.0f : 512.0f, exit);
		}

		size_t th06NameBytes = 0;
		for (unsigned char ch : state.cardUi.name)
			if ((ch & 0xc0) != 0x80)
				th06NameBytes += ch < 0x80 ? 1 : 2;
		const float barLength = th06NameBytes * 15.0f / 2.0f + 16.0f;
		const float barX =
		    state.cardUi.player
		        ? textX + barLength * 16.0f / 15.0f / 2.0f - 144.0f
		        : textX + 128.0f - barLength * 16.0f / 15.0f / 2.0f;
		drawTH06FrontSpriteTransformed(
		    state, renderer, state.cardUi.player ? 21 : 22, barX, textY,
		    barLength / 14.0f, 1.0f, {1.0f, 1.0f, 1.0f, nameAlpha}, 52.0f,
		    false, true);
		// Bomb names use DrawVmTextFmt (left aligned); enemy spell names use
		// DrawStringFormat, which right-aligns Shift-JIS bytes in the 256 px
		// VM.
		const float nameDrawX =
		    state.cardUi.player
		        ? textX - 128.0f * nameScale
		        : textX + 128.0f * nameScale -
		              static_cast<float>(th06NameBytes) * 8.0f * nameScale;
		drawTH06Text(renderer, state.cardUi.name,
		             {nameDrawX, textY - 8.0f * nameScale}, 15.0f,
		             state.cardUi.player
		                 ? shiki::Color{0.94f, 0.94f, 1.0f, nameAlpha}
		                 : shiki::Color{1.0f, 0.94f, 0.94f, nameAlpha},
		             nameScale, true);
	}

	// Supervisor::DrawFpsCounter refreshes every 500 ms. Keep it anchored to
	// the actual swapchain corner so fullscreen letterboxing cannot move it
	// inside the logical 640x480 canvas.
	const float fpsScale = std::max(canvasScale, 0.75f);
	const float fpsWidth =
	    static_cast<float>(state.fpsText.size()) * 14.0f * fpsScale;
	drawTH06WindowAsciiText(
	    state, renderer, state.fpsText,
	    std::max(0.0f, actualWidth - fpsWidth - 8.0f * fpsScale),
	    std::max(0.0f, actualHeight - 16.0f * fpsScale), fpsScale);
	if (state.debugMode)
		drawTH06WindowAsciiText(
		    state, renderer, "DEBUG",
		    std::max(0.0f,
		             actualWidth - 5.0f * 14.0f * fpsScale - 8.0f * fpsScale),
		    std::max(0.0f, actualHeight - 32.0f * fpsScale), fpsScale,
		    {0.2f, 1.0f, 0.2f, 1.0f});
	if (state.pauseMenu.active)
		renderTH06PauseMenu(state, renderer);
}

void updateTH06Fps(GameState &state, float dt) {
	state.fpsElapsed += std::max(dt, 0.0f);
	++state.fpsFrames;
	if (state.fpsElapsed >= 0.5f) {
		state.fpsText =
		    std::format("{:.2f}fps", state.fpsFrames / state.fpsElapsed);
		state.fpsElapsed = 0.0f;
		state.fpsFrames = 0;
	}
}

void renderTH06PauseMenu(GameState &state, shiki::Renderer *renderer) {
	const auto &menu = state.pauseMenu;
	const float visibility = menu.closing
	                             ? std::max(0.0f, 1.0f - menu.frame / 20.0f)
	                             : std::min(1.0f, menu.backgroundFrame / 30.0f);
	shiki::Sprite shade;
	shade.setSourceRect({0.0f, 0.0f, GAME_WIDTH, GAME_HEIGHT});
	shade.setOrigin({0.0f, 0.0f});
	shade.setPosition(0.0f, 0.0f);
	// capture.anm script 0 fades in a captured, blue-tinted playfield over
	// 30 frames. The frozen frame is already underneath this tint layer.
	shade.setColor({0.04f, 0.14f, 0.18f, 0.72f * visibility});
	renderer->drawSprite(shade, 200.0f, true);

	for (const auto &vm : menu.vms) {
		if (!vm.visible)
			continue;
		shiki::Sprite sprite;
		if (!applyTH06AnmVmSprite(state, vm, sprite))
			continue;
		sprite.setPosition(vm.x + vm.offsetX - 32.0f,
		                   vm.y + vm.offsetY - 16.0f);
		renderer->drawSprite(sprite, 202.0f, true);
	}
}

#include "ecl_enemy.h"
#include "th06_effect_manager.h"
#include "th06_game_state.h"
#include "th06_menu_anm.h"
#include "th06_stage_background.h"
#include "th06_systems.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <format>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <numbers>
#include <optional>
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

bool isTH06MainMenuItemLocked(int item) {
	return item != 0 && item != 1 && item != 7;
}

void playTH06MainMenuSound(GameState &state, int soundId) {
	// Keep title-menu feedback below the in-game mix. The per-sound TH06 dB
	// The example applies the manifest gain before playback.
	playTH06Sound(state, soundId, 0.5f);
}

void updateTH06MainMenu(GameState &state, shiki::frontend::Realtime &engine,
                        float dt) {
	auto advanceLogic = [&]() {
		state.mainMenu.logicAccum += dt;
		while (state.mainMenu.logicAccum >= 1.0f / 60.0f) {
			state.mainMenu.logicAccum -= 1.0f / 60.0f;
			for (auto &vm : state.mainMenu.vms)
				vm.tick();
			++state.mainMenu.frame;
		}
	};
	const bool *keyboard = SDL_GetKeyboardState(nullptr);
	const bool up = keyboard[SDL_SCANCODE_UP];
	const bool down = keyboard[SDL_SCANCODE_DOWN];
	const bool left = keyboard[SDL_SCANCODE_LEFT];
	const bool right = keyboard[SDL_SCANCODE_RIGHT];
	const bool confirm =
	    keyboard[SDL_SCANCODE_Z] || keyboard[SDL_SCANCODE_RETURN];
	const bool cancel =
	    keyboard[SDL_SCANCODE_X] || keyboard[SDL_SCANCODE_ESCAPE];
	const bool backspace = keyboard[SDL_SCANCODE_BACKSPACE];
	if (backspace && !state.mainMenu.backspaceHeld) {
		state.debugMode = !state.debugMode;
		state.debugInfiniteLives = state.debugMode;
		state.debugAiEnabled = false;
		state.debugEnemyHitboxes = false;
		state.debugAiImminentDanger = false;
		state.debugAiDeathbombTargetFrame = -1;
		state.debugAiDodgeOrigin = {};
		state.debugAiDodgeRecoveryFrames = 0;
		state.debugAiLastDirection = {};
		state.debugAiLastFocused = false;
		state.debugAiDirectionCommitFrames = 0;
		state.power = state.debugMode ? 128 : 0;
		spdlog::info("Debug mode: {}",
		             state.debugMode ? "enabled" : "disabled");
	}
	state.mainMenu.backspaceHeld = backspace;

	if (state.mainMenu.screen == TH06MainMenuState::Screen::PressStart) {
		if (confirm && !state.mainMenu.confirmHeld &&
		    state.mainMenu.frame >= 30) {
			state.mainMenu.screen = TH06MainMenuState::Screen::Main;
			state.mainMenu.frame = 0;
			interruptTH06MenuVms(state.mainMenu, 2);
			playTH06MainMenuSound(state, 10);
		}
		state.mainMenu.confirmHeld = confirm;
		advanceLogic();
		return;
	}

	if (state.mainMenu.screen == TH06MainMenuState::Screen::SelectLoading) {
		if (state.mainMenu.frame >= 60) {
			if (loadTH06SelectMenuVms(state.mainMenu, state.resourceManager)) {
				interruptTH06MenuVms(state.mainMenu, state.extraStage ? 18 : 6);
				state.mainMenu.screen =
				    state.extraStage
				        ? TH06MainMenuState::Screen::ExtraConfirm
				        : TH06MainMenuState::Screen::DifficultySelect;
				state.mainMenu.cursor = state.extraStage ? 0 : 1;
				state.mainMenu.frame = 0;
			}
		}
		advanceLogic();
		return;
	}

	if (state.mainMenu.screen == TH06MainMenuState::Screen::DifficultySelect) {
		if ((up && !state.mainMenu.upHeld) ||
		    (down && !state.mainMenu.downHeld)) {
			const int direction = up && !state.mainMenu.upHeld ? -1 : 1;
			state.mainMenu.cursor = (state.mainMenu.cursor + direction + 4) % 4;
			playTH06MainMenuSound(state, 12);
		}
		for (int index = 0; index < 4; ++index) {
			const size_t vmIndex = static_cast<size_t>(1 + index);
			if (vmIndex >= state.mainMenu.vms.size())
				break;
			auto &vm = state.mainMenu.vms[vmIndex];
			vm.color = index == state.mainMenu.cursor ? 0xffffffff : 0x60ffffff;
			vm.offsetX = index == state.mainMenu.cursor ? -6.0f : 0.0f;
			vm.offsetY = index == state.mainMenu.cursor ? -6.0f : 0.0f;
		}
		if (cancel && !state.mainMenu.cancelHeld) {
			loadTH06TitleMenuVms(state.mainMenu, state.resourceManager);
			state.mainMenu.screen = TH06MainMenuState::Screen::Main;
			state.mainMenu.cursor = 0;
			state.mainMenu.frame = 0;
			interruptTH06MenuVms(state.mainMenu, 2);
			playTH06MainMenuSound(state, 11);
		} else if (confirm && !state.mainMenu.confirmHeld &&
		           state.mainMenu.frame >= 30) {
			state.difficulty = state.mainMenu.cursor;
			interruptTH06MenuVms(state.mainMenu, 7);
			const size_t difficultyVm =
			    static_cast<size_t>(1 + state.mainMenu.cursor);
			if (difficultyVm < state.mainMenu.vms.size())
				state.mainMenu.vms[difficultyVm].interrupt(8);
			const size_t unselectedVm =
			    static_cast<size_t>(6 + (1 - state.mainMenu.character) * 2);
			if (unselectedVm + 1 < state.mainMenu.vms.size()) {
				state.mainMenu.vms[unselectedVm].interrupt(0);
				state.mainMenu.vms[unselectedVm + 1].interrupt(0);
			}
			state.mainMenu.screen = TH06MainMenuState::Screen::CharacterSelect;
			state.mainMenu.cursor = state.mainMenu.character;
			state.mainMenu.frame = 0;
			playTH06MainMenuSound(state, 10);
		}
		state.mainMenu.upHeld = up;
		state.mainMenu.downHeld = down;
		state.mainMenu.confirmHeld = confirm;
		state.mainMenu.cancelHeld = cancel;
		advanceLogic();
		return;
	}

	if (state.mainMenu.screen == TH06MainMenuState::Screen::ExtraConfirm) {
		if (cancel && !state.mainMenu.cancelHeld) {
			loadTH06TitleMenuVms(state.mainMenu, state.resourceManager);
			state.mainMenu.screen = TH06MainMenuState::Screen::Main;
			state.mainMenu.frame = 0;
			interruptTH06MenuVms(state.mainMenu, 2);
			playTH06MainMenuSound(state, 11);
		} else if (confirm && !state.mainMenu.confirmHeld &&
		           state.mainMenu.frame >= 30) {
			interruptTH06MenuVms(state.mainMenu, 7);
			if (state.mainMenu.vms.size() > 5)
				state.mainMenu.vms[5].interrupt(8);
			const size_t unselectedVm =
			    static_cast<size_t>(6 + (1 - state.mainMenu.character) * 2);
			if (unselectedVm + 1 < state.mainMenu.vms.size()) {
				state.mainMenu.vms[unselectedVm].interrupt(0);
				state.mainMenu.vms[unselectedVm + 1].interrupt(0);
			}
			state.mainMenu.screen = TH06MainMenuState::Screen::CharacterSelect;
			state.mainMenu.frame = 0;
			playTH06MainMenuSound(state, 10);
		}
		state.mainMenu.confirmHeld = confirm;
		state.mainMenu.cancelHeld = cancel;
		advanceLogic();
		return;
	}

	if (state.mainMenu.screen == TH06MainMenuState::Screen::CharacterSelect) {
		if ((left && !state.mainMenu.leftHeld) ||
		    (right && !state.mainMenu.rightHeld)) {
			const bool movedLeft = left && !state.mainMenu.leftHeld;
			state.mainMenu.character = 1 - state.mainMenu.character;
			for (int character = 0; character < 2; ++character) {
				const int interrupt = character == state.mainMenu.character
				                          ? (movedLeft ? 9 : 10)
				                          : (movedLeft ? 12 : 11);
				state.mainMenu.vms[static_cast<size_t>(6 + character * 2)]
				    .interrupt(interrupt);
				state.mainMenu.vms[static_cast<size_t>(7 + character * 2)]
				    .interrupt(interrupt);
			}
			playTH06MainMenuSound(state, 12);
		}
		if (cancel && !state.mainMenu.cancelHeld) {
			interruptTH06MenuVms(state.mainMenu, state.extraStage ? 18 : 6);
			state.mainMenu.screen =
			    state.extraStage ? TH06MainMenuState::Screen::ExtraConfirm
			                     : TH06MainMenuState::Screen::DifficultySelect;
			state.mainMenu.cursor = state.extraStage ? 0 : state.difficulty;
			state.mainMenu.frame = 0;
			playTH06MainMenuSound(state, 11);
		} else if (confirm && !state.mainMenu.confirmHeld &&
		           state.mainMenu.frame >= 30) {
			// A new run starts from the same stock as the original title flow.
			state.lives = 2;
			state.bombs = 3;
			state.power = state.debugMode ? 128 : 0;
			state.graze = 0;
			state.pointItemsCollected = 0;
			state.powerItemCountForScore = 0;
			state.score = 0;
			state.guiScore = 0;
			state.bombFrame = -1;
			state.deathbombFrame = -1;
			state.playerDeathTimer = -1;
			state.invincibleTimer = 0.0f;
			state.shootingFrame = 0;
			state.gameTime = 0.0f;
			state.highScore = std::max(state.highScore, state.score);
			configurePlayerCharacter(state, state.mainMenu.character);
			state.isGameOver = false;
			state.stageClearUi = {};
			state.spellResultUi = {};
			state.stageRestartRequested = true;
			playTH06MainMenuSound(state, 10);
			state.mainMenu.active = false;
			state.mainMenu.vms.clear();
			state.enemyLogicAccum = 0.0f;
			state.bulletLogicAccum = 0.0f;
			state.playerLogicAccum = 0.0f;
			state.activeSpellBackgroundTexture.reset();
			state.spellBackgroundActive = false;
			state.spellBackgroundScrolls = false;
			state.spellBackgroundFrame = 0;
			state.currentSong = 0;
			state.stageNameVm = {};
			state.songNameVm = {};
			if (state.stageTextAnm) {
				state.stageNameVm.initialize(state.stageTextAnm, 0);
				state.songNameVm.initialize(state.stageTextAnm, 1);
				state.stageNameVm.visible = true;
				state.songNameVm.visible = true;
			}
			if (state.audioManager)
				state.audioManager->playMusic(
				    th06StageMusicId(state.stageNumber, 0));
		}
		state.mainMenu.leftHeld = left;
		state.mainMenu.rightHeld = right;
		state.mainMenu.confirmHeld = confirm;
		state.mainMenu.cancelHeld = cancel;
		advanceLogic();
		return;
	}

	auto moveCursor = [&](int direction) {
		do {
			state.mainMenu.cursor = (state.mainMenu.cursor + direction + 8) % 8;
		} while (isTH06MainMenuItemLocked(state.mainMenu.cursor));
		playTH06MainMenuSound(state, 12);
	};
	if (up && !state.mainMenu.upHeld)
		moveCursor(-1);
	if (down && !state.mainMenu.downHeld)
		moveCursor(1);
	if (confirm && !state.mainMenu.confirmHeld) {
		if (state.mainMenu.cursor == 0 || state.mainMenu.cursor == 1) {
			state.extraStage = state.mainMenu.cursor == 1;
			state.stageNumber = state.extraStage ? 7 : 1;
			state.difficulty = state.extraStage ? 4 : 1;
			state.stageRestartRequested = false;
			playTH06MainMenuSound(state, 10);
			interruptTH06MenuVms(state.mainMenu, 4);
			state.mainMenu.screen = TH06MainMenuState::Screen::SelectLoading;
			state.mainMenu.frame = 0;
		} else if (state.mainMenu.cursor == 7) {
			playTH06MainMenuSound(state, 10);
			engine.stop();
		} else {
			playTH06MainMenuSound(state, 11);
		}
	}
	state.mainMenu.upHeld = up;
	state.mainMenu.downHeld = down;
	state.mainMenu.confirmHeld = confirm;
	state.mainMenu.cancelHeld = cancel;
	for (int item = 0;
	     item < 8 && item < static_cast<int>(state.mainMenu.vms.size());
	     ++item) {
		auto &vm = state.mainMenu.vms[static_cast<size_t>(item)];
		if (item == state.mainMenu.cursor) {
			vm.color = 0xffff0000;
			vm.offsetX = -4.0f;
			vm.offsetY = -4.0f;
		} else {
			vm.color = isTH06MainMenuItemLocked(item) ? 0x80300000 : 0xffffffff;
			vm.offsetX = 0.0f;
			vm.offsetY = 0.0f;
		}
	}
	advanceLogic();
}

void drawTH06MenuSprite(GameState &state, shiki::Renderer *renderer,
                        std::string_view atlas, int spriteId, float x, float y,
                        shiki::Color color, float z = 10.0f) {
	auto texture = state.resourceManager
	                   ? state.resourceManager->getSpriteTexture(
	                         std::string(atlas), spriteId)
	                   : nullptr;
	if (!texture || !texture->isValid())
		return;
	shiki::Sprite sprite(texture);
	const float width = static_cast<float>(texture->getWidth());
	const float height = static_cast<float>(texture->getHeight());
	sprite.setSourceRect({0.0f, 0.0f, width, height});
	sprite.setOrigin({width * 0.5f, height * 0.5f});
	sprite.setPosition(x, y);
	sprite.setColor(color);
	renderer->drawSprite(sprite, z);
}

void drawTH06MenuVm(GameState &state, shiki::Renderer *renderer,
                    const TH06MenuAnmVm &vm) {
	if (!vm.visible || vm.sprite < 0 || !vm.file || !state.resourceManager)
		return;
	auto texture =
	    state.resourceManager->getSpriteTexture(vm.file->atlas, vm.sprite);
	if (!texture || !texture->isValid())
		return;
	shiki::Sprite sprite(texture);
	const float width = static_cast<float>(texture->getWidth());
	const float height = static_cast<float>(texture->getHeight());
	sprite.setSourceRect({0.0f, 0.0f, width, height});
	sprite.setOrigin(vm.topLeft ? shiki::Vec2{0.0f, 0.0f}
	                            : shiki::Vec2{width * 0.5f, height * 0.5f});
	sprite.setPosition(vm.x + vm.offsetX, vm.y + vm.offsetY);
	sprite.setScale(vm.scaleX, vm.scaleY);
	sprite.setRotation(-vm.rotation * 180.0f / std::numbers::pi_v<float>);
	sprite.setColor(
	    {((vm.color >> 16) & 0xff) / 255.0f, ((vm.color >> 8) & 0xff) / 255.0f,
	     (vm.color & 0xff) / 255.0f, ((vm.color >> 24) & 0xff) / 255.0f});
	if (vm.additive)
		sprite.setBlendMode(shiki::BlendMode::Add);
	renderer->drawSprite(sprite, 10.0f);
}

void renderTH06MainMenu(GameState &state, shiki::Renderer *renderer,
                        shiki::frontend::Realtime &engine) {
	int actualWidth = 0;
	int actualHeight = 0;
	engine.getWindowSize(actualWidth, actualHeight);
	const float scale =
	    std::min(static_cast<float>(actualWidth) / TH06_CANVAS_WIDTH,
	             static_cast<float>(actualHeight) / TH06_CANVAS_HEIGHT);
	const int width = static_cast<int>(std::round(TH06_CANVAS_WIDTH * scale));
	const int height = static_cast<int>(std::round(TH06_CANVAS_HEIGHT * scale));
	renderer->setOutputSize(actualWidth, actualHeight);
	renderer->setViewport((actualWidth - width) / 2,
	                      (actualHeight - height) / 2, width, height);
	renderer->setProjection(0.0f, TH06_CANVAS_WIDTH, TH06_CANVAS_HEIGHT, 0.0f);

	const auto backgroundTexture =
	    state.mainMenu.screen == TH06MainMenuState::Screen::DifficultySelect ||
	            state.mainMenu.screen ==
	                TH06MainMenuState::Screen::ExtraConfirm ||
	            state.mainMenu.screen ==
	                TH06MainMenuState::Screen::CharacterSelect
	        ? state.selectBackgroundTexture
	        : state.titleBackgroundTexture;
	if (backgroundTexture && backgroundTexture->isValid()) {
		shiki::Sprite background(backgroundTexture);
		const float textureWidth =
		    static_cast<float>(backgroundTexture->getWidth());
		const float textureHeight =
		    static_cast<float>(backgroundTexture->getHeight());
		background.setSourceRect({0.0f, 0.0f, textureWidth, textureHeight});
		background.setOrigin({0.0f, 0.0f});
		background.setPosition(0.0f, 0.0f);
		background.setScale(TH06_CANVAS_WIDTH / textureWidth,
		                    TH06_CANVAS_HEIGHT / textureHeight);
		renderer->drawSprite(background, 0.0f);
	}

	for (const auto &vm : state.mainMenu.vms)
		drawTH06MenuVm(state, renderer, vm);
	const float fpsScale = std::max(scale, 0.75f);
	const float fpsWidth =
	    static_cast<float>(state.fpsText.size()) * 14.0f * fpsScale;
	drawTH06WindowAsciiText(
	    state, renderer, state.fpsText,
	    std::max(0.0f,
	             static_cast<float>(actualWidth) - fpsWidth - 8.0f * fpsScale),
	    std::max(0.0f, static_cast<float>(actualHeight) - 16.0f * fpsScale),
	    fpsScale);
	if (state.debugMode)
		drawTH06WindowAsciiText(
		    state, renderer, "DEBUG",
		    std::max(0.0f, static_cast<float>(actualWidth) -
		                       5.0f * 14.0f * fpsScale - 8.0f * fpsScale),
		    std::max(0.0f, static_cast<float>(actualHeight) - 32.0f * fpsScale),
		    fpsScale, {0.2f, 1.0f, 0.2f, 1.0f});
}

void updateTH06PauseMenu(GameState &state, bool cancel, bool quit) {
	auto &menu = state.pauseMenu;
	if (!menu.active) {
		if (cancel && !menu.cancelHeld) {
			menu = {};
			menu.anmFile = loadTH06MenuAnmFile(state.resourceManager, "ascii");
			if (!menu.anmFile)
				return;
			for (size_t index = 0; index < menu.vms.size(); ++index)
				menu.vms[index].initialize(menu.anmFile,
				                           2 + static_cast<int>(index));
			for (size_t index = 0; index < 3; ++index)
				menu.vms[index].interrupt(1);
			menu.active = true;
			menu.cancelHeld = true;
		}
		menu.cancelHeld = cancel;
		menu.quitHeld = quit;
		return;
	}

	const bool upPressed = state.keyUp && !menu.upHeld;
	const bool downPressed = state.keyDown && !menu.downHeld;
	const bool confirmPressed = state.keyShoot && !menu.confirmHeld;
	const bool cancelPressed = cancel && !menu.cancelHeld;
	const bool quitPressed = quit && !menu.quitHeld;

	if (!menu.closing && (cancelPressed || quitPressed)) {
		menu.closing = true;
		menu.quitAfterClose = quitPressed;
		menu.frame = 0;
		for (auto &vm : menu.vms)
			if (vm.visible)
				vm.interrupt(2);
	}

	if (!menu.closing && menu.frame >= 4) {
		if (upPressed || downPressed)
			menu.selection = 1 - menu.selection;
		if (confirmPressed) {
			if (menu.page == TH06PauseMenuState::Page::Pause) {
				if (menu.selection == 0) {
					for (size_t index = 0; index < 3; ++index)
						menu.vms[index].interrupt(2);
					menu.closing = true;
					menu.frame = 0;
				} else {
					for (size_t index = 0; index < 3; ++index) {
						menu.vms[index].interrupt(2);
						menu.vms[index + 3].interrupt(1);
					}
					menu.page = TH06PauseMenuState::Page::Quit;
					menu.selection = 1;
					menu.frame = 0;
				}
			} else if (menu.selection == 0) {
				for (size_t index = 3; index < 6; ++index)
					menu.vms[index].interrupt(2);
				menu.closing = true;
				menu.quitAfterClose = true;
				menu.frame = 0;
			} else {
				for (size_t index = 0; index < 3; ++index) {
					menu.vms[index].interrupt(1);
					menu.vms[index + 3].interrupt(2);
				}
				menu.page = TH06PauseMenuState::Page::Pause;
				menu.selection = 1;
				menu.frame = 0;
			}
		}
	}

	if (!menu.closing) {
		const size_t first =
		    menu.page == TH06PauseMenuState::Page::Pause ? 1 : 4;
		const size_t second = first + 1;
		menu.vms[first].color = menu.selection == 0 ? 0xffff8080 : 0x80808080;
		menu.vms[second].color = menu.selection == 1 ? 0xffff8080 : 0x80808080;
		menu.vms[first].scaleX = menu.vms[first].scaleY =
		    menu.selection == 0 ? 1.7f : 1.5f;
		menu.vms[second].scaleX = menu.vms[second].scaleY =
		    menu.selection == 1 ? 1.7f : 1.5f;
		menu.vms[first].offsetX = menu.vms[first].offsetY =
		    menu.selection == 0 ? -4.0f : 0.0f;
		menu.vms[second].offsetX = menu.vms[second].offsetY =
		    menu.selection == 1 ? -4.0f : 0.0f;
	}

	for (auto &vm : menu.vms)
		vm.tick();
	if (!menu.closing && menu.backgroundFrame < 30)
		++menu.backgroundFrame;
	++menu.frame;

	if (menu.closing && menu.frame >= 20) {
		const bool returnToTitle = menu.quitAfterClose;
		menu = {};
		menu.cancelHeld = cancel;
		menu.quitHeld = quit;
		if (returnToTitle) {
			if (state.audioManager)
				state.audioManager->stopMusic();
			state.stageRestartRequested = true;
			state.mainMenu.active = true;
			state.mainMenu.screen = TH06MainMenuState::Screen::PressStart;
			state.mainMenu.frame = 0;
			loadTH06TitleMenuVms(state.mainMenu, state.resourceManager);
		}
		return;
	}

	menu.upHeld = state.keyUp;
	menu.downHeld = state.keyDown;
	menu.confirmHeld = state.keyShoot;
	menu.cancelHeld = cancel;
	menu.quitHeld = quit;
}

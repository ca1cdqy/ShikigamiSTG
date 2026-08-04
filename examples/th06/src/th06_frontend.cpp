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

void preloadSpriteAtlas(shiki::ResourceManager *resourceManager,
                        const std::string &atlasName) {
	if (!resourceManager)
		return;
	const auto *atlas = resourceManager->getSpriteAtlas(atlasName);
	if (!atlas)
		return;
	for (const auto &frame : atlas->sprites)
		resourceManager->getSpriteTexture(atlasName, frame.id);
}

std::shared_ptr<TH06MenuAnmFile>
loadTH06MenuAnmFile(shiki::ResourceManager *resources, std::string_view atlas) {
	auto file = std::make_shared<TH06MenuAnmFile>();
	return resources && file->load(resources->getAssetStore(), atlas) ? file
	                                                                  : nullptr;
}

bool applyTH06AnmVmSprite(GameState &state, const TH06MenuAnmVm &vm,
                          shiki::Sprite &sprite) {
	if (!vm.file || vm.sprite < 0 || !state.resourceManager)
		return false;
	auto texture =
	    state.resourceManager->getSpriteTexture(vm.file->atlas, vm.sprite);
	if (!texture || !texture->isValid())
		return false;
	if (texture != sprite.getTexture()) {
		sprite.setTexture(texture);
		const float width = static_cast<float>(texture->getWidth());
		const float height = static_cast<float>(texture->getHeight());
		sprite.setSourceRect({0.0f, 0.0f, width, height});
		sprite.setOrigin(vm.topLeft ? shiki::Vec2{0.0f, 0.0f}
		                            : shiki::Vec2{width * 0.5f, height * 0.5f});
	}
	sprite.setScale(vm.scaleX, vm.scaleY);
	// TH06 TranslateRotation uses the opposite sign from Sprite's 2D matrix.
	sprite.setRotation(-vm.rotation * 180.0f / std::numbers::pi_v<float>);
	sprite.setColor(
	    {((vm.color >> 16) & 0xff) / 255.0f, ((vm.color >> 8) & 0xff) / 255.0f,
	     (vm.color & 0xff) / 255.0f, ((vm.color >> 24) & 0xff) / 255.0f});
	sprite.setBlendMode(vm.additive ? shiki::BlendMode::Add
	                                : shiki::BlendMode::Alpha);
	return true;
}

void interruptTH06MenuVms(TH06MainMenuState &menu, int interrupt) {
	for (auto &vm : menu.vms)
		vm.interrupt(interrupt);
}

bool loadTH06TitleMenuVms(TH06MainMenuState &menu,
                          shiki::ResourceManager *resources) {
	const auto title01 = loadTH06MenuAnmFile(resources, "title01");
	const auto title02 = loadTH06MenuAnmFile(resources, "title02");
	const auto title03 = loadTH06MenuAnmFile(resources, "title03");
	const auto title04 = loadTH06MenuAnmFile(resources, "title04");
	if (!title01 || !title02 || !title03 || !title04)
		return false;
	menu.vms.clear();
	auto append = [&](const std::shared_ptr<TH06MenuAnmFile> &file, int count) {
		for (int script = 0; script < count; ++script) {
			TH06MenuAnmVm vm;
			vm.initialize(file, script);
			menu.vms.push_back(std::move(vm));
		}
	};
	append(title01, 27);
	append(title02, 4);
	append(title03, 3);
	append(title04, 46);
	interruptTH06MenuVms(menu, 1);
	return menu.vms.size() == 80;
}

bool loadTH06SelectMenuVms(TH06MainMenuState &menu,
                           shiki::ResourceManager *resources) {
	struct FileSpec {
		const char *atlas;
		int scripts;
	};
	static constexpr std::array<FileSpec, 9> FILES = {
	    FileSpec{"select01", 3}, FileSpec{"select02", 2},
	    FileSpec{"select05", 1}, FileSpec{"slpl00a", 1},
	    FileSpec{"slpl00b", 1},  FileSpec{"slpl01a", 1},
	    FileSpec{"slpl01b", 1},  FileSpec{"select03", 2},
	    FileSpec{"select04", 4}};
	menu.vms.clear();
	for (const auto &spec : FILES) {
		const auto file = loadTH06MenuAnmFile(resources, spec.atlas);
		if (!file)
			return false;
		for (int script = 0; script < spec.scripts; ++script) {
			TH06MenuAnmVm vm;
			vm.initialize(file, script);
			menu.vms.push_back(std::move(vm));
		}
	}
	return menu.vms.size() == 16;
}

bool loadTH06Dialogue(TH06DialogueState &dialogue,
                      shiki::ResourceManager *resources, int stage) {
	if (!resources || !resources->getAssetStore())
		return false;
	auto loaded = resources->getAssetStore()->load<shiki::asset::DialogueAsset>(
	    shiki::asset::AssetId::fromName("th06.dialogue." +
	                                    std::to_string(stage)));
	if (!loaded)
		return false;
	dialogue.stageNumber = stage;
	dialogue.messages.clear();
	for (const auto &sourceMessage : (*loaded)->messages) {
		std::vector<TH06DialogueState::Command> message;
		for (const auto &source : sourceMessage) {
			TH06DialogueState::Command command;
			command.time = source.time;
			command.opcode = source.opcode;
			command.arguments.assign(source.arguments.begin(),
			                         source.arguments.end());
			command.text = source.text;
			message.push_back(std::move(command));
		}
		dialogue.messages.push_back(std::move(message));
	}
	return !dialogue.messages.empty();
}

std::string eclTextToUtf8(const std::string &text) {
#ifdef _WIN32
	if (text.empty())
		return {};
	const int wideLength = MultiByteToWideChar(
	    932, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
	if (wideLength <= 0)
		return text;
	std::wstring wide(static_cast<size_t>(wideLength), L'\0');
	MultiByteToWideChar(932, 0, text.data(), static_cast<int>(text.size()),
	                    wide.data(), wideLength);
	const int utf8Length = WideCharToMultiByte(
	    CP_UTF8, 0, wide.data(), wideLength, nullptr, 0, nullptr, nullptr);
	if (utf8Length <= 0)
		return text;
	std::string utf8(static_cast<size_t>(utf8Length), '\0');
	WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideLength, utf8.data(),
	                    utf8Length, nullptr, nullptr);
	return utf8;
#else
	return text;
#endif
}

void startTH06Dialogue(GameState &state, int messageId) {
	// EnemyManager::RunTimeline adds character * 10 before Gui::MsgRead.
	messageId += state.playerCharacter * 10;
	if (messageId < 0 ||
	    messageId >= static_cast<int>(state.dialogue.messages.size())) {
		spdlog::warn("TH06 dialogue {} is unavailable", messageId);
		return;
	}
	state.dialogue.active = true;
	state.dialogue.messageId = messageId;
	state.dialogue.instructionIndex = 0;
	state.dialogue.timerFrames = 0;
	state.dialogue.pauseFrames = 0;
	state.dialogue.lines = {};
	state.dialogue.portraits = {};
	state.dialogue.lineFrames = {-1, -1};
	state.dialogue.advanceRequested = false;
	state.dialogue.eclResumeRequested = false;
	state.dialogue.skippable = true;
	state.playerBullets.clear();
	spdlog::info("Started TH06 dialogue {}", messageId);
}

std::shared_ptr<shiki::Texture> resolveTH06DialoguePortrait(
    const TH06DialogueState &dialogue, shiki::ResourceManager &resourceManager,
    int portraitIndex, int spriteIndex, float &originX, float &originY) {
	const char *atlasName = nullptr;
	int localSprite = 0;
	if (portraitIndex == 0) {
		// Gui::Initialize loads face00a/b/c for Reimu and face01a/b/c for
		// Marisa into the same three player-face ANM slots.
		static constexpr std::array<const char *, 3> REIMU_FACE_ATLASES = {
		    "face00a", "face00b", "face00c"};
		static constexpr std::array<const char *, 3> MARISA_FACE_ATLASES = {
		    "face01a", "face01b", "face01c"};
		if (spriteIndex < 0 || spriteIndex >= 6)
			return nullptr;
		const auto &atlases =
		    dialogue.messageId >= 10 ? MARISA_FACE_ATLASES : REIMU_FACE_ATLASES;
		atlasName = atlases[static_cast<size_t>(spriteIndex / 2)];
		localSprite = spriteIndex % 2;
	} else {
		// Enemy portraits start at ANM_OFFSET_FACE_STAGE_A. MsgRead temporarily
		// replaces that slot with face12c for the Flandre dialogue pair.
		// MsgRead replaces the stage-face A slot for Flandre's entry 0/10 and
		// leaves it loaded for the corresponding post-battle entry 1/11.
		const bool flandreDialogue =
		    dialogue.stageNumber == 7 &&
		    (dialogue.messageId == 0 || dialogue.messageId == 1 ||
		     dialogue.messageId == 10 || dialogue.messageId == 11);
		const auto resolved = resolveTH06StageFaceSprite(
		    dialogue.stageNumber, spriteIndex, flandreDialogue);
		atlasName = resolved.atlas;
		localSprite = resolved.localSprite;
		if (!atlasName)
			return nullptr;
	}

	const auto *atlas = resourceManager.getSpriteAtlas(atlasName);
	if (atlas) {
		const auto frame =
		    std::find_if(atlas->sprites.begin(), atlas->sprites.end(),
		                 [localSprite](const auto &entry) {
			                 return entry.id == localSprite;
		                 });
		if (frame != atlas->sprites.end()) {
			originX = frame->originX;
			originY = frame->originY;
		}
	}
	return resourceManager.getSpriteTexture(atlasName, localSprite);
}

void resetTH06DialoguePortrait(TH06DialoguePortraitState &portrait,
                               size_t index) {
	portrait = {};
	portrait.x = index == 0 ? -64.0f : 448.0f;
	portrait.y = 320.0f;
}

void interruptTH06DialoguePortrait(TH06DialoguePortraitState &portrait,
                                   size_t index, int interrupt) {
	const bool player = index == 0;
	portrait.startX = portrait.x;
	portrait.startY = portrait.y;
	portrait.startAlpha = portrait.alpha;
	portrait.transitionFrame = 0;
	portrait.visible = true;
	switch (interrupt) {
	case 1:
		portrait.targetX = player ? 64.0f : 320.0f;
		portrait.targetY = 320.0f;
		portrait.targetAlpha = 1.0f;
		portrait.transitionDuration = 30;
		portrait.alphaDuration = 30;
		break;
	case 2:
		portrait.targetX = player ? -64.0f : 448.0f;
		portrait.targetY = 320.0f;
		portrait.targetAlpha = 0.0f;
		portrait.transitionDuration = 30;
		portrait.alphaDuration = 30;
		break;
	case 3:
		portrait.targetX = player ? 64.0f : 320.0f;
		portrait.targetY = 320.0f;
		portrait.targetAlpha = 1.0f;
		portrait.transitionDuration = player ? 16 : 30;
		portrait.alphaDuration = 16;
		break;
	case 4:
		portrait.targetX = player ? 48.0f : 336.0f;
		portrait.targetY = 336.0f;
		portrait.targetAlpha = 128.0f / 255.0f;
		portrait.transitionDuration = player ? 16 : 30;
		portrait.alphaDuration = 16;
		break;
	default:
		portrait.transitionFrame = -1;
		break;
	}
}

void updateTH06DialogueVisuals(TH06DialogueState &dialogue) {
	if (!dialogue.active)
		return;
	for (auto &portrait : dialogue.portraits) {
		if (portrait.transitionFrame < 0 || portrait.transitionDuration <= 0)
			continue;
		++portrait.transitionFrame;
		float progress =
		    std::clamp(static_cast<float>(portrait.transitionFrame) /
		                   static_cast<float>(portrait.transitionDuration),
		               0.0f, 1.0f);
		progress = 1.0f - (1.0f - progress) * (1.0f - progress);
		portrait.x = std::lerp(portrait.startX, portrait.targetX, progress);
		portrait.y = std::lerp(portrait.startY, portrait.targetY, progress);
		const float alphaProgress =
		    std::clamp(static_cast<float>(portrait.transitionFrame) /
		                   static_cast<float>(portrait.alphaDuration),
		               0.0f, 1.0f);
		portrait.alpha =
		    std::lerp(portrait.startAlpha, portrait.targetAlpha, alphaProgress);
		if (portrait.transitionFrame >= portrait.transitionDuration) {
			portrait.transitionFrame = -1;
			if (portrait.alpha <= 0.0f)
				portrait.visible = false;
		}
	}
	for (int &frame : dialogue.lineFrames)
		if (frame >= 0)
			++frame;
}

void updateTH06DialogueTick(GameState &state,
                            shiki::ResourceManager *resourceManager) {
	auto &dialogue = state.dialogue;
	if (!dialogue.active)
		return;
	if (dialogue.messageId < 0 ||
	    dialogue.messageId >= static_cast<int>(dialogue.messages.size())) {
		dialogue.active = false;
		return;
	}
	const auto &message =
	    dialogue.messages[static_cast<size_t>(dialogue.messageId)];
	for (int budget = 0; budget < 100; ++budget) {
		if (dialogue.instructionIndex >= message.size()) {
			dialogue.active = false;
			spdlog::warn("TH06 dialogue {} ended without a delete command",
			             dialogue.messageId);
			return;
		}
		const auto &command = message[dialogue.instructionIndex];
		const int time = command.time;
		if (budget == 0 && state.keyDialogueSkip && dialogue.timerFrames < time)
			dialogue.timerFrames = time;
		// Patchouli's entry 2/12 is an original timed sequence without WAIT
		// opcodes. Let a fresh shoot press advance its next timestamp while
		// retaining the authored automatic timing when the player does nothing.
		if (budget == 0 && dialogue.advanceRequested &&
		    dialogue.timerFrames < time) {
			dialogue.timerFrames = time;
			dialogue.advanceRequested = false;
		}
		if (dialogue.timerFrames < time)
			break;

		bool advanceInstruction = true;
		const auto argument = [&](size_t index, int fallback = 0) {
			return index < command.arguments.size() ? command.arguments[index]
			                                        : fallback;
		};
		switch (command.opcode) {
		case 0: // MSGDELETE
			dialogue.active = false;
			dialogue.lines = {};
			return;
		case 1: { // PORTRAITANMSCRIPT
			const int portraitIndex = argument(0, -1);
			if (portraitIndex >= 0 && portraitIndex < 2)
				resetTH06DialoguePortrait(
				    dialogue.portraits[static_cast<size_t>(portraitIndex)],
				    static_cast<size_t>(portraitIndex));
			break;
		}
		case 2: { // PORTRAITANMSPRITE
			const int portraitIndex = argument(0, -1);
			const int imageIndex = argument(1);
			if (portraitIndex >= 0 && portraitIndex < 2 && resourceManager) {
				auto &portrait =
				    dialogue.portraits[static_cast<size_t>(portraitIndex)];
				portrait.texture = resolveTH06DialoguePortrait(
				    dialogue, *resourceManager, portraitIndex, imageIndex,
				    portrait.originX, portrait.originY);
			}
			break;
		}
		case 3: { // TEXTDIALOGUE
			const int textLine = argument(1, -1);
			if (textLine >= 0 && textLine < 2) {
				if (textLine == 0)
					dialogue.lines[1].clear();
				dialogue.lines[static_cast<size_t>(textLine)] = command.text;
				dialogue.lineFrames[static_cast<size_t>(textLine)] = 0;
				dialogue.pauseFrames = 0;
			}
			break;
		}
		case 4: { // WAIT
			const int waitFrames = argument(0);
			const bool shootAdvance =
			    dialogue.advanceRequested && dialogue.pauseFrames >= 8;
			dialogue.advanceRequested = false;
			if (state.keyDialogueSkip || shootAdvance ||
			    dialogue.pauseFrames >= waitFrames) {
				dialogue.pauseFrames = 0;
			} else {
				++dialogue.pauseFrames;
				advanceInstruction = false;
			}
			break;
		}
		case 5: { // ANMINTERRUPT
			const int target = argument(0, -1);
			const int interrupt = argument(1);
			if (target >= 0 && target < 2)
				interruptTH06DialoguePortrait(
				    dialogue.portraits[static_cast<size_t>(target)],
				    static_cast<size_t>(target), interrupt);
			break;
		}
		case 6: // ECLRESUME
			dialogue.eclResumeRequested = true;
			break;
		case 7: { // MUSIC
			const int musicId = argument(0);
			state.currentSong = std::clamp(musicId, 0, 3);
			state.songNameVm = {};
			if (state.songNameVm.initialize(state.stageTextAnm, 1))
				state.songNameVm.visible = true;
			if (state.audioManager) {
				state.audioManager->playMusic(
				    th06StageMusicId(state.stageNumber, musicId));
				spdlog::info("TH06 dialogue switched music to track {}",
				             musicId);
			}
			break;
		}
		case 9: // STAGERESULTS
			beginTH06StageClear(state);
			break;
		case 10: // MSGHALT
			advanceInstruction = false;
			break;
		case 11: // STAGEEND
			break;
		case 12: // MUSICFADEOUT
			if (state.audioManager)
				state.audioManager->stopMusic();
			break;
		case 13: { // WAITSKIPPABLE
			dialogue.skippable = argument(0) != 0;
			break;
		}
		default:
			break;
		}

		if (!advanceInstruction) {
			if (state.keyDialogueSkip && dialogue.timerFrames < 60)
				dialogue.timerFrames = 60;
			return;
		}
		++dialogue.instructionIndex;
	}
	++dialogue.timerFrames;
	if (state.keyDialogueSkip && dialogue.timerFrames < 60)
		dialogue.timerFrames = 60;
}

void playTH06Sound(GameState &state, int soundId, float gainScale) {
	// SoundPlayer.cpp maps ECL sound IDs through g_SoundBufferIdxVol rather
	// than treating them as direct WAV table indices.
	if (!state.audioManager || soundId < 0 ||
	    soundId >= static_cast<int>(state.soundGains.size()))
		return;
	const float gain =
	    state.soundGains[static_cast<size_t>(soundId)] * gainScale;
	state.audioManager->playSound("th06_sfx_" + std::to_string(soundId), gain);
}

void addTH06Score(GameState &state, int amount) {
	if (amount <= 0)
		return;
	state.score = std::min(TH06_MAX_SCORE, state.score + amount);
}

void beginTH06StageClear(GameState &state) {
	if (state.stageClearUi.active)
		return;
	state.stageClearUi.score = calculateTH06ExtraStageClearScore(state);
	state.stageClearUi.frame = 0;
	state.stageClearUi.active = true;
	addTH06Score(state, state.stageClearUi.score);
}

void scoreTH06Graze(GameState &state, const shiki::Vec2 &projectileCenter) {
	// Player::ScoreGraze suppresses the counters during a bomb, but still
	// awards the particle, 500 points, and the graze sound.
	if (state.bombFrame < 0)
		state.graze = std::min(state.graze + 1, 9999);
	addTH06Score(state, 500);
	const auto player = state.player.getPosition();
	state.effects.spawn(8, (player.x + projectileCenter.x) * 0.5f,
	                    (player.y + projectileCenter.y) * 0.5f, 1, 0xffffffff);
	playTH06Sound(state, 30);
}

bool loadTH06AudioManifest(GameState &state) {
	if (!state.audioManager || !state.resourceManager ||
	    !state.resourceManager->getAssetStore())
		return false;
	auto loaded = state.resourceManager->getAssetStore()
	                  ->load<shiki::asset::AudioManifestAsset>(
	                      shiki::asset::AssetId::fromName("th06.audio"));
	if (!loaded)
		return false;
	state.soundGains.clear();
	for (const auto &sound : (*loaded)->sounds) {
		const int id = sound.id;
		if (id < 0)
			return false;
		if (state.soundGains.size() <= static_cast<size_t>(id))
			state.soundGains.resize(static_cast<size_t>(id) + 1, 1.0f);
		state.soundGains[static_cast<size_t>(id)] = sound.gain;
		state.audioManager->loadSound("th06_sfx_" + std::to_string(id),
		                              "assets/" + sound.source);
	}
	for (const auto &music : (*loaded)->music) {
		const auto &id = music.id;
		const auto runtimeName = id;
		if (!state.audioManager->loadMusic(runtimeName,
		                                   "assets/" + music.source))
			return false;
		if (!state.audioManager->setMusicLoopPoints(
		        runtimeName, music.loopStart, music.loopEnd))
			return false;
	}
	return true;
}

// Load a standalone texture.
std::shared_ptr<shiki::Texture> loadTexture(shiki::Renderer *renderer,
                                            const std::string &path) {
	auto texture = std::make_shared<shiki::Texture>();
	texture->setDevice(renderer->getDevice());
	if (!texture->loadFromFile(path)) {
		spdlog::warn("Failed to load texture: {}", path);
		return nullptr;
	}
	return texture;
}

// Create a sprite from a standalone texture.
shiki::Sprite createSprite(std::shared_ptr<shiki::Texture> texture, float x,
                           float y, float scale) {
	shiki::Sprite sprite(texture);
	sprite.setPosition(x, y);
	sprite.setScale(scale, scale);
	sprite.setColor({1.0f, 1.0f, 1.0f, 1.0f});
	if (texture && texture->isValid()) {
		float texWidth = static_cast<float>(texture->getWidth());
		float texHeight = static_cast<float>(texture->getHeight());
		sprite.setSourceRect(shiki::Rect(0.0f, 0.0f, texWidth, texHeight));
		// Center the sprite origin.
		sprite.setOrigin({texWidth / 2.0f, texHeight / 2.0f});
	}
	return sprite;
}

void drawTH06FrontSpriteTransformed(GameState &state, shiki::Renderer *renderer,
                                    int spriteId, float x, float y,
                                    float scaleX, float scaleY,
                                    shiki::Color color, float z, bool topLeft,
                                    bool playfieldSpace) {
	if (!state.resourceManager)
		return;
	auto texture = state.resourceManager->getSpriteTexture("front", spriteId);
	if (!texture || !texture->isValid())
		return;
	shiki::Sprite sprite(texture);
	const float width = static_cast<float>(texture->getWidth());
	const float height = static_cast<float>(texture->getHeight());
	sprite.setSourceRect({0.0f, 0.0f, width, height});
	sprite.setOrigin(topLeft ? shiki::Vec2{0.0f, 0.0f}
	                         : shiki::Vec2{width * 0.5f, height * 0.5f});
	sprite.setPosition(x, y);
	sprite.setScale(scaleX, scaleY);
	sprite.setColor(color);
	renderer->drawSprite(sprite, z, playfieldSpace);
}

void drawTH06FrontSprite(GameState &state, shiki::Renderer *renderer,
                         int spriteId, float x, float y, float z) {
	drawTH06FrontSpriteTransformed(state, renderer, spriteId, x, y, 1.0f, 1.0f,
	                               {1.0f, 1.0f, 1.0f, 1.0f}, z, true);
}

void drawTH06AsciiText(GameState &state, shiki::Renderer *renderer,
                       std::string_view text, float x, float y,
                       shiki::Color color, float scale, float z,
                       bool playfieldSpace) {
	if (!state.resourceManager)
		return;

	float cursorX = x;
	float cursorY = y;
	for (const unsigned char character : text) {
		if (character == '\n') {
			cursorX = x;
			cursorY += 16.0f * scale;
			continue;
		}
		if (character == ' ') {
			cursorX += 14.0f * scale;
			continue;
		}
		if (character < 0x15)
			continue;

		// AsciiManager::DrawStrings indexes normal glyphs with character -
		// 0x15.
		auto texture = state.resourceManager->getSpriteTexture(
		    "ascii", static_cast<int>(character) - 0x15);
		if (texture && texture->isValid()) {
			shiki::Sprite glyph(texture);
			glyph.setSourceRect({0.0f, 0.0f,
			                     static_cast<float>(texture->getWidth()),
			                     static_cast<float>(texture->getHeight())});
			glyph.setOrigin({0.0f, 0.0f});
			glyph.setPosition(cursorX, cursorY);
			glyph.setScale(scale, scale);
			glyph.setColor(color);
			renderer->drawSprite(glyph, z, playfieldSpace);
		}
		cursorX += 14.0f * scale;
	}
}

void drawTH06WindowAsciiText(GameState &state, shiki::Renderer *renderer,
                             std::string_view text, float x, float y,
                             float scale, shiki::Color color) {
	if (!state.resourceManager)
		return;

	float cursorX = x;
	for (const unsigned char character : text) {
		if (character == ' ') {
			cursorX += 14.0f * scale;
			continue;
		}
		if (character < 0x15)
			continue;

		auto texture = state.resourceManager->getSpriteTexture(
		    "ascii", static_cast<int>(character) - 0x15);
		if (texture && texture->isValid()) {
			shiki::Sprite glyph(texture);
			const float width = static_cast<float>(texture->getWidth());
			const float height = static_cast<float>(texture->getHeight());
			glyph.setSourceRect({0.0f, 0.0f, width, height});
			glyph.setOrigin({0.0f, 0.0f});
			glyph.setPosition(cursorX, y);
			glyph.setScale(scale, scale);
			glyph.setColor(color);
			renderer->drawWindowSprite(glyph, 100.0f);
		}
		cursorX += 14.0f * scale;
	}
}

void drawTH06WindowUiBacking(GameState &state, shiki::Renderer *renderer,
                             int windowWidth, int windowHeight, int canvasX,
                             int canvasY, int canvasWidth, int canvasHeight,
                             float canvasScale) {
	if (!state.resourceManager)
		return;
	auto texture = state.resourceManager->getSpriteTexture("front", 5);
	if (!texture || !texture->isValid())
		return;

	const float sourceWidth = static_cast<float>(texture->getWidth());
	const float sourceHeight = static_cast<float>(texture->getHeight());
	const float tileWidth = sourceWidth * canvasScale;
	const float tileHeight = sourceHeight * canvasScale;
	const auto fillRegion = [&](float left, float top, float width,
	                            float height) {
		if (width <= 0.0f || height <= 0.0f)
			return;
		for (float y = top; y < top + height; y += tileHeight) {
			for (float x = left; x < left + width; x += tileWidth) {
				const float visibleWidth =
				    std::min(tileWidth, left + width - x);
				const float visibleHeight =
				    std::min(tileHeight, top + height - y);
				shiki::Sprite tile(texture);
				tile.setSourceRect({0.0f, 0.0f, visibleWidth / canvasScale,
				                    visibleHeight / canvasScale});
				tile.setOrigin({0.0f, 0.0f});
				tile.setPosition(x, y);
				tile.setScale(canvasScale, canvasScale);
				renderer->drawWindowSprite(tile, 78.0f);
			}
		}
	};

	fillRegion(0.0f, 0.0f, static_cast<float>(windowWidth),
	           static_cast<float>(canvasY));
	fillRegion(0.0f, static_cast<float>(canvasY + canvasHeight),
	           static_cast<float>(windowWidth),
	           static_cast<float>(windowHeight - canvasY - canvasHeight));
	fillRegion(0.0f, static_cast<float>(canvasY), static_cast<float>(canvasX),
	           static_cast<float>(canvasHeight));
	fillRegion(static_cast<float>(canvasX + canvasWidth),
	           static_cast<float>(canvasY),
	           static_cast<float>(windowWidth - canvasX - canvasWidth),
	           static_cast<float>(canvasHeight));
}

void drawTH06Text(shiki::Renderer *renderer, const std::string &text,
                  const shiki::Vec2 &position, float size,
                  const shiki::Color &color, float displayScale,
                  bool playfieldSpace) {
	if (text.empty())
		return;
	// TextHelper::RenderTextToTexture renders a black shadow at x * 2 + 3
	// before downsampling the 2x GDI surface into the ANM text texture.
	renderer->drawText(
	    text,
	    {position.x + 1.5f * displayScale, position.y + 1.0f * displayScale},
	    size, {0.0f, 0.0f, 0.0f, color.w}, displayScale, playfieldSpace);
	renderer->drawText(text, position, size, color, displayScale,
	                   playfieldSpace);
}

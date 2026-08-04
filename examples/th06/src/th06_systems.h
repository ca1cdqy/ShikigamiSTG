#pragma once

#include "th06_game_state.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <memory>
#include <numbers>
#include <shiki/frontend/realtime.h>
#include <shiki/render/renderer.h>
#include <string>
#include <string_view>

struct TH06PlayerShotSpec {
	int interval;
	int frame;
	float offsetX;
	float angle;
	float speed;
	int damage;
	int option;
	bool sound;
};

inline constexpr TH06PlayerShotSpec TH06_REIMU_A_ZERO_POWER_SHOT{
    5, 0, 0.0F, -90.0F, 12.0F, 48, 0, true};
inline constexpr TH06PlayerShotSpec TH06_MARISA_B_ZERO_POWER_SHOT{
    5, 0, 0.0F, -90.0F, 12.0F, 48, 0, true};

inline shiki::Vec2 th06PlayerShotVelocity(float angleDegrees,
                                          float speedPerFrame) {
	const float radians = angleDegrees * std::numbers::pi_v<float> / 180.0F;
	return {std::abs(angleDegrees + 90.0F) < 0.001F
	            ? 0.0F
	            : std::cos(radians) * speedPerFrame * 60.0F,
	        std::sin(radians) * speedPerFrame * 60.0F};
}

void preloadSpriteAtlas(shiki::ResourceManager *resources,
                        const std::string &atlasName);
std::shared_ptr<TH06MenuAnmFile>
loadTH06MenuAnmFile(shiki::ResourceManager *resources, std::string_view atlas);
bool applyTH06AnmVmSprite(GameState &state, const TH06MenuAnmVm &vm,
                          shiki::Sprite &sprite);
void interruptTH06MenuVms(TH06MainMenuState &menu, int interrupt);
bool loadTH06TitleMenuVms(TH06MainMenuState &menu,
                          shiki::ResourceManager *resources);
bool loadTH06SelectMenuVms(TH06MainMenuState &menu,
                           shiki::ResourceManager *resources);
bool loadTH06Dialogue(TH06DialogueState &dialogue,
                      shiki::ResourceManager *resources, int stage = 7);
inline std::string th06StageMusicId(int stage, int cue) {
	const int track = stage == 7 ? 14 + cue : stage * 2 + cue;
	return std::format("th06_{:02}", track);
}

struct TH06StageFaceSprite final {
	const char *atlas{};
	int localSprite{};
};

inline TH06StageFaceSprite
resolveTH06StageFaceSprite(int stage, int spriteIndex,
                           bool flandreDialogue = false) {
	if (spriteIndex < 0)
		return {};
	switch (stage) {
	case 1:
		return spriteIndex < 2 ? TH06StageFaceSprite{"face03a", spriteIndex}
		       : spriteIndex < 4
		           ? TH06StageFaceSprite{"face03b", spriteIndex - 2}
		           : TH06StageFaceSprite{};
	case 2:
		return spriteIndex < 2 ? TH06StageFaceSprite{"face05a", spriteIndex}
		                       : TH06StageFaceSprite{};
	case 3:
		return spriteIndex < 2 ? TH06StageFaceSprite{"face06a", spriteIndex}
		       : spriteIndex < 4
		           ? TH06StageFaceSprite{"face06b", spriteIndex - 2}
		           : TH06StageFaceSprite{};
	case 4:
		return spriteIndex < 2 ? TH06StageFaceSprite{"face08a", spriteIndex}
		       : spriteIndex < 4
		           ? TH06StageFaceSprite{"face08b", spriteIndex - 2}
		           : TH06StageFaceSprite{};
	case 5:
		return spriteIndex < 2 ? TH06StageFaceSprite{"face09a", spriteIndex}
		       : spriteIndex < 4
		           ? TH06StageFaceSprite{"face09b", spriteIndex - 2}
		           : TH06StageFaceSprite{};
	case 6:
		if (spriteIndex < 2)
			return {"face09b", spriteIndex};
		if (spriteIndex == 2)
			return {"face10a", 0};
		if (spriteIndex == 3)
			return {"face10b", 0};
		return {};
	case 7:
		if (spriteIndex < 2)
			return {flandreDialogue ? "face12c" : "face08a",
			        flandreDialogue ? 0 : spriteIndex};
		if (spriteIndex == 2)
			return {"face12a", 0};
		if (spriteIndex == 3)
			return {"face12b", 0};
		return {};
	default:
		return {};
	}
}

inline const char *th06StageEffectAtlas(int stage, bool finalBossSpell) {
	static constexpr std::array<const char *, 8> EFFECT_ATLASES = {
	    "eff01", "eff01", "eff02", "eff03", "eff04", "eff05", "eff05", "eff04"};
	if (stage == 6 && finalBossSpell)
		return "eff06";
	if (stage == 7 && finalBossSpell)
		return "eff07";
	return EFFECT_ATLASES[static_cast<size_t>(std::clamp(stage, 0, 7))];
}
std::string eclTextToUtf8(const std::string &text);
void startTH06Dialogue(GameState &state, int messageId);
void updateTH06DialogueTick(GameState &state,
                            shiki::ResourceManager *resources);
void updateTH06DialogueVisuals(TH06DialogueState &dialogue);
void playTH06Sound(GameState &state, int soundId, float gainScale = 1.0F);
void addTH06Score(GameState &state, int amount);
void beginTH06StageClear(GameState &state);
bool loadTH06AudioManifest(GameState &state);

std::shared_ptr<shiki::Texture> loadTexture(shiki::Renderer *renderer,
                                            const std::string &path);
shiki::Sprite createSprite(std::shared_ptr<shiki::Texture> texture, float x,
                           float y, float scale = 1.0F);
void drawTH06FrontSpriteTransformed(GameState &state, shiki::Renderer *renderer,
                                    int spriteId, float x, float y,
                                    float scaleX, float scaleY,
                                    shiki::Color color, float z, bool topLeft,
                                    bool playfieldSpace = false);
void drawTH06FrontSprite(GameState &state, shiki::Renderer *renderer,
                         int spriteId, float x, float y, float z = 90.0F);
void drawTH06AsciiText(GameState &state, shiki::Renderer *renderer,
                       std::string_view text, float x, float y,
                       shiki::Color color = {1.0F, 1.0F, 1.0F, 1.0F},
                       float scale = 1.0F, float z = 92.0F,
                       bool playfieldSpace = false);
void drawTH06WindowAsciiText(GameState &state, shiki::Renderer *renderer,
                             std::string_view text, float x, float y,
                             float scale,
                             shiki::Color color = {1.0F, 1.0F, 1.0F, 1.0F});
void drawTH06WindowUiBacking(GameState &state, shiki::Renderer *renderer,
                             int windowWidth, int windowHeight, int canvasX,
                             int canvasY, int canvasWidth, int canvasHeight,
                             float canvasScale);
void drawTH06Text(shiki::Renderer *renderer, const std::string &text,
                  const shiki::Vec2 &position, float size,
                  const shiki::Color &color, float displayScale = 1.0F,
                  bool playfieldSpace = false);
void scoreTH06Graze(GameState &state, const shiki::Vec2 &projectileCenter);

void spawnTH06Item(GameState &state, int requestedType, float x, float y,
                   int initialState = 1);
void turnEnemyBulletsIntoPoints(GameState &state);
void beginEnemyBulletDespawn(GameState &state, bool createPoints);
void turnEnemyProjectilesIntoPoints(GameState &state);
void calculateGameLayout(GameState &state, int windowWidth, int windowHeight);
shiki::Vec2 gameToScreen(const GameState &state, float x, float y);
void beginTH06CardUi(GameState &state, bool player, std::string name,
                     std::shared_ptr<shiki::Texture> portrait);
void endTH06CardUi(GameState &state, bool player);
void registerTH06ScreenShake(GameState &state, int duration, float start,
                             float end);
void setPlayerAnmScript(GameState &state, int scriptId);
void configurePlayerCharacter(GameState &state, int character);
void handleInput(GameState &state, float dt);
void updateGame(GameState &state, float dt);

void renderGame(GameState &state, shiki::Renderer *renderer,
                shiki::frontend::Realtime &engine, float dt);
void updateTH06MainMenu(GameState &state, shiki::frontend::Realtime &engine,
                        float dt);
void renderTH06MainMenu(GameState &state, shiki::Renderer *renderer,
                        shiki::frontend::Realtime &engine);
void updateTH06Fps(GameState &state, float dt);
void updateTH06PauseMenu(GameState &state, bool cancel, bool quit);
void renderTH06PauseMenu(GameState &state, shiki::Renderer *renderer);

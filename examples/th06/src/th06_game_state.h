#pragma once

#include "ecl_enemy.h"
#include "th06_anm.h"
#include "th06_effect_manager.h"
#include "th06_menu_anm.h"
#include "th06_stage_background.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <shiki/audio/audio_manager.h>
#include <shiki/render/sprite.h>
#include <shiki/render/texture.h>
#include <shiki/resource/resource_manager.h>
#include <string>
#include <vector>

inline constexpr float GAME_WIDTH = 384.0F;
inline constexpr float GAME_HEIGHT = 448.0F;
inline constexpr float TH06_CANVAS_WIDTH = 640.0F;
inline constexpr float TH06_CANVAS_HEIGHT = 480.0F;
inline constexpr float PLAYER_SPEED = 300.0F;
inline constexpr float PLAYER_FOCUS_SPEED = 150.0F;
inline constexpr float BOMB_COOLDOWN = 1.0F;
inline constexpr float INVINCIBLE_TIME = 2.0F;
inline constexpr float DEFAULT_MUSIC_VOLUME = 0.5F;
inline constexpr float DEFAULT_SOUND_VOLUME = 1.0F;
inline constexpr int TRUE_DEATHBOMB_FRAMES = 8;
inline constexpr int DEATHBOMB_TOTAL_FRAMES = 30;
inline constexpr int DEATHBOMB_EFFECT_FRAMES =
    DEATHBOMB_TOTAL_FRAMES - TRUE_DEATHBOMB_FRAMES;
inline constexpr float DEBUG_AI_TRUE_DEATHBOMB_CHANCE = 0.5F;
inline constexpr float DEBUG_AI_IMMINENT_TRUE_DEATHBOMB_CHANCE = 0.9F;
inline constexpr int TH06_MAX_SCORE = 999999990;
inline constexpr int TH06_MAX_LIVES = 8;
inline constexpr std::array<int, 5> TH06_EXTRA_LIFE_SCORES{
    10000000, 20000000, 40000000, 60000000, 1900000000};
inline constexpr std::array<int, 64> TH06_SPELL_CARD_SCORES{
    200000, 200000, 200000, 200000, 200000, 200000, 200000, 250000,
    250000, 250000, 250000, 250000, 250000, 250000, 300000, 300000,
    300000, 300000, 300000, 300000, 300000, 300000, 300000, 300000,
    300000, 300000, 300000, 300000, 300000, 300000, 300000, 300000,
    400000, 400000, 400000, 400000, 400000, 400000, 400000, 400000,
    500000, 500000, 500000, 500000, 500000, 500000, 600000, 600000,
    600000, 600000, 600000, 700000, 700000, 700000, 700000, 700000,
    700000, 700000, 700000, 700000, 700000, 700000, 700000, 700000};

struct TH06DeathbombEffectSample final {
	bool active{};
	float scale{};
	float alpha{};
};

[[nodiscard]] inline TH06DeathbombEffectSample
sampleTH06DeathbombEffect(int deathbombFrame) {
	if (deathbombFrame < TRUE_DEATHBOMB_FRAMES ||
	    deathbombFrame >= DEATHBOMB_TOTAL_FRAMES)
		return {};
	constexpr float DEATH_EFFECT_MAX_SCALE = (0.5f + 0.3f * 40.0f) * 0.5f;
	const float progress =
	    static_cast<float>(deathbombFrame - TRUE_DEATHBOMB_FRAMES) /
	    static_cast<float>(DEATHBOMB_EFFECT_FRAMES);
	return {true, DEATH_EFFECT_MAX_SCALE * (1.0f - progress), 1.0f - progress};
}

[[nodiscard]] inline int sampleTH06DebugAiDeathbombTarget(int currentFrame,
                                                          bool imminentDanger,
                                                          float randomUnit) {
	if (currentFrame >= TRUE_DEATHBOMB_FRAMES)
		return currentFrame;
	const float chance = imminentDanger
	                         ? DEBUG_AI_IMMINENT_TRUE_DEATHBOMB_CHANCE
	                         : DEBUG_AI_TRUE_DEATHBOMB_CHANCE;
	return randomUnit < chance ? currentFrame : TRUE_DEATHBOMB_FRAMES;
}

struct TH06DialoguePortraitState final {
	std::shared_ptr<shiki::Texture> texture;
	float x{};
	float y{};
	float alpha{};
	float startX{};
	float startY{};
	float startAlpha{};
	float targetX{};
	float targetY{};
	float targetAlpha{};
	float originX{};
	float originY{};
	int transitionFrame{-1};
	int transitionDuration{};
	int alphaDuration{};
	bool visible{};
};

struct TH06DialogueState final {
	struct Command final {
		int time{};
		int opcode{};
		std::vector<int> arguments;
		std::string text;
	};

	std::vector<std::vector<Command>> messages;
	std::array<std::string, 2> lines;
	std::array<TH06DialoguePortraitState, 2> portraits;
	std::array<int, 2> lineFrames{-1, -1};
	int stageNumber{};
	std::size_t instructionIndex{};
	int timerFrames{};
	int pauseFrames{};
	int messageId{-1};
	bool active{};
	bool advanceRequested{};
	bool eclResumeRequested{};
	bool skippable{true};
};

struct TH06CardUiState final {
	std::string name;
	std::shared_ptr<shiki::Texture> portrait;
	int frame{};
	int endFrame{-1};
	bool player{};
	bool active{};
};

struct TH06SpellResultUiState final {
	int score{};
	int frame{};
	bool captured{};
	bool active{};
};

struct TH06StageClearUiState final {
	int score{};
	int frame{};
	bool active{};
};

struct TH06MainMenuState final {
	enum class Screen {
		PressStart,
		Main,
		SelectLoading,
		DifficultySelect,
		ExtraConfirm,
		CharacterSelect
	};

	bool active{true};
	Screen screen{Screen::PressStart};
	int cursor{1};
	int character{};
	int frame{};
	float logicAccum{};
	std::vector<TH06MenuAnmVm> vms;
	bool upHeld{};
	bool downHeld{};
	bool leftHeld{};
	bool rightHeld{};
	bool confirmHeld{};
	bool cancelHeld{};
	bool backspaceHeld{};
};

struct TH06PauseMenuState final {
	enum class Page { Pause, Quit };

	bool active{};
	bool closing{};
	bool quitAfterClose{};
	Page page{Page::Pause};
	int selection{};
	int frame{};
	int backgroundFrame{};
	std::shared_ptr<TH06MenuAnmFile> anmFile;
	std::array<TH06MenuAnmVm, 6> vms;
	bool upHeld{};
	bool downHeld{};
	bool confirmHeld{};
	bool cancelHeld{};
	bool quitHeld{};
};

struct GameState final {
	struct PlayerBullet final {
		shiki::Sprite sprite;
		float vx{};
		float vy{};
		float speed{};
		int damage{};
		int ageFrames{};
		bool homing{};
		bool autoRotate{};
		bool laser{};
		bool damageReady{true};
		bool active{true};
		int laserTimerIndex{-1};
		TH06MenuAnmVm anmVm;
	};

	struct ReimuABombOrb final {
		std::array<TH06MenuAnmVm, 4> vms;
		shiki::Vec2 position;
		float vx{};
		float vy{};
		float speed{4.0F};
		int state{};
		int explosionFrames{};
		int accumulatedDamage{};
	};

	struct ScreenShake final {
		int frame{};
		int duration{};
		float start{};
		float end{};
	};

	enum class PlayerOptionState { Unfocused, Focusing, Focused, Unfocusing };

	struct EnemyBullet final {
		shiki::Sprite sprite;
		shiki::Sprite spawnEffectSprite;
		shiki::Sprite despawnSprite;
		float vx{};
		float vy{};
		float accelerationX{};
		float accelerationY{};
		float speedPerFrame{};
		float angle{};
		float speedDelta{};
		float angleDelta{};
		float directionChangeAngle{};
		float directionChangeSpeed{};
		int effectFrames{};
		int ageFrames{};
		int directionChangeInterval{};
		int directionChangeMax{};
		int directionChangeCount{};
		int bulletType{};
		int bulletColor{};
		int effectFlags{};
		int fastSpawnFrames{};
		int fastSpawnAge{};
		TH06BulletSpawnAnimation spawnAnimation;
		int spawnAnimationAge{};
		int despawnFrames{};
		bool grazed{};
		bool despawning{};
	};

	struct Item final {
		shiki::Sprite sprite;
		float x{};
		float y{};
		float startX{};
		float startY{-2.2F};
		float targetX{};
		float targetY{};
		int type{TH06_ITEM_NONE};
		int state{};
		int timerFrames{};
		bool active{true};
	};

	shiki::Sprite player;
	float playerSpeed{PLAYER_SPEED};
	bool isFocused{};
	TH06AnmScript playerAnmScript;
	float playerAnmFrames{};
	float previousHorizontalSpeed{};
	int playerAnmScriptId{-1};
	std::vector<PlayerBullet> playerBullets;
	std::array<ReimuABombOrb, 8> bombOrbs;
	std::array<TH06MenuAnmVm, 4> marisaBombVms;
	std::shared_ptr<TH06MenuAnmFile> reimuPlayerAnm;
	std::shared_ptr<TH06MenuAnmFile> marisaPlayerAnm;
	std::array<TH06MenuAnmVm, 2> playerOptionVms;
	std::array<shiki::Vec2, 2> playerOptionPositions;
	std::array<int, 2> marisaLaserTimers{};
	std::vector<ScreenShake> screenShakes;
	std::uint32_t screenShakeRng{0x1337U};
	shiki::Rect playfieldRegion{32.0F, 16.0F, 384.0F, 448.0F};
	PlayerOptionState playerOptionState{PlayerOptionState::Unfocused};
	int playerOptionTransitionFrame{};
	std::vector<EnemyBullet> enemyBullets;
	std::vector<Item> items;
	int randomItemSpawnIndex{};
	int randomItemTableIndex{};
	std::vector<ECLEnemy> eclEnemies;
	std::vector<ECLEnemy> pendingECLEnemies;
	TH06EffectManager effects;

	std::shared_ptr<shiki::Texture> stg7enmTexture;
	std::shared_ptr<shiki::Texture> etama3Texture;
	shiki::Sprite background;
	TH06StageBackground stageBackground;
	std::shared_ptr<TH06MenuAnmFile> stageTextAnm;
	TH06MenuAnmVm stageNameVm;
	TH06MenuAnmVm songNameVm;
	int currentSong{};
	std::shared_ptr<shiki::Texture> patchouliSpellBackgroundTexture;
	std::shared_ptr<shiki::Texture> flandreSpellBackgroundTexture;
	std::shared_ptr<shiki::Texture> stageSpellBackgroundTexture;
	std::shared_ptr<shiki::Texture> activeSpellBackgroundTexture;
	std::shared_ptr<TH06MenuAnmFile> spellBackgroundAnm;
	TH06MenuAnmVm spellBackgroundVm;
	int spellBackgroundFrame{};
	bool spellBackgroundActive{};
	bool spellBackgroundScrolls{};
	bool eclTimeStopped{};

	int score{};
	int guiScore{};
	int nextScoreIncrement{};
	int highScore{10000000};
	int lives{2};
	int bombs{3};
	int power{};
	int graze{};
	int powerItemCountForScore{};
	int pointItemsCollected{};
	int extraLives{};
	bool extraStage{};
	int stageNumber{7};
	int difficulty{4};
	int playerCharacter{};
	bool keyUp{};
	bool keyDown{};
	bool keyLeft{};
	bool keyRight{};
	bool keyShoot{};
	bool keyBomb{};
	bool bombRequested{};
	bool keyFocus{};
	bool keyDialogueSkip{};

	float shootCooldown{};
	float bombCooldown{};
	float invincibleTimer{};
	float playerLogicAccum{};
	float effectLogicAccum{};
	int shootingFrame{};
	int bombFrame{-1};
	int deathbombFrame{-1};
	int playerDeathTimer{-1};
	int playerBulletGraceFrames{};
	float gameTime{};
	float fpsElapsed{};
	int fpsFrames{};
	std::string fpsText{"0.00fps"};
	bool isGameOver{};
	bool stageRestartRequested{};
	bool stageAdvanceRequested{};
	bool bossPresent{};
	TH06DialogueState dialogue;

	bool spellActive{};
	bool spellCaptureEligible{};
	int spellCardId{-1};
	int spellCaptureScore{};
	std::string spellName;
	TH06CardUiState cardUi;
	TH06SpellResultUiState spellResultUi;
	TH06StageClearUiState stageClearUi;
	float bossHealthDisplayed{};
	float bossUiOpacity{};
	int bossUiFrame{};
	int bossUiLifeCount{};
	int bossUiSeconds{};
	int lastBossUiSeconds{-1};
	float bulletLogicAccum{};
	float enemyLogicAccum{};
	TH06MainMenuState mainMenu;
	TH06PauseMenuState pauseMenu;
	std::shared_ptr<shiki::Texture> titleBackgroundTexture;
	std::shared_ptr<shiki::Texture> selectBackgroundTexture;

	bool debugMode{};
	bool debugInfiniteLives{};
	bool debugAiEnabled{};
	bool debugEnemyHitboxes{};
	bool debugAiKeyDown{};
	bool debugHitboxKeyDown{};
	bool debugSkipKeyDown{};
	bool debugAiImminentDanger{};
	int debugAiDeathbombTargetFrame{-1};
	shiki::Vec2 debugAiDodgeOrigin{};
	int debugAiDodgeRecoveryFrames{};
	shiki::Vec2 debugAiLastDirection{};
	bool debugAiLastFocused{};
	int debugAiDirectionCommitFrames{};
	std::shared_ptr<shiki::Texture> playerTexture;
	std::shared_ptr<shiki::Texture> bulletTexture;
	std::shared_ptr<shiki::Texture> homingBulletTexture;
	std::shared_ptr<shiki::Texture> marisaBulletTexture;
	std::shared_ptr<shiki::Texture> enemyBulletTexture;
	std::shared_ptr<shiki::Texture> enemyTexture;
	std::shared_ptr<shiki::Texture> bgTexture;

	shiki::AudioManager *audioManager{};
	std::vector<float> soundGains;
	shiki::ResourceManager *resourceManager{};
	float musicVolume{DEFAULT_MUSIC_VOLUME};
	float soundVolume{DEFAULT_SOUND_VOLUME};
	int windowWidth{1280};
	int windowHeight{960};
	float gameScreenX{};
	float gameScreenY{};
	float gameScreenWidth{384.0F};
	float gameScreenHeight{448.0F};
};

[[nodiscard]] inline int updateTH06GuiScore(GameState &state) {
	state.score = std::clamp(state.score, 0, TH06_MAX_SCORE);
	if (state.guiScore == state.score)
		return 0;
	if (state.score < state.guiScore)
		state.score = state.guiScore;

	int increment = (state.score - state.guiScore) >> 5;
	increment = std::clamp(increment, 10, 78910);
	increment -= increment % 10;
	state.nextScoreIncrement = std::max(state.nextScoreIncrement, increment);
	state.nextScoreIncrement =
	    std::min(state.nextScoreIncrement, state.score - state.guiScore);
	state.guiScore += state.nextScoreIncrement;
	if (state.guiScore >= state.score) {
		state.guiScore = state.score;
		state.nextScoreIncrement = 0;
	}

	int awards = 0;
	while (state.extraLives >= 0 &&
	       state.extraLives < static_cast<int>(TH06_EXTRA_LIFE_SCORES.size()) &&
	       state.guiScore >=
	           TH06_EXTRA_LIFE_SCORES[static_cast<size_t>(state.extraLives)]) {
		if (!state.extraStage && state.lives < TH06_MAX_LIVES) {
			++state.lives;
			++awards;
		}
		++state.extraLives;
	}
	state.highScore = std::max(state.highScore, state.guiScore);
	return awards;
}

[[nodiscard]] inline int
calculateTH06ExtraStageClearScore(const GameState &state) {
	int score = (7 * 1000 + state.graze * 10 + state.power * 100) *
	            state.pointItemsCollected;
	score += state.lives * 3000000;
	score += state.bombs * 1000000;
	score *= 2;
	return score - score % 10;
}

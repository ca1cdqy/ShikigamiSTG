#include "ecl_enemy.h"
#include "th06_effect_manager.h"
#include "th06_game_state.h"
#include "th06_menu_anm.h"
#include "th06_systems.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <shiki/asset/standard_assets.h>
#include <shiki/asset/structured_assets.h>
#include <shiki/ecl/ecl_asset.h>
#include <shiki/ecl/ecl_executor.h>
#include <shiki/resource/resource_manager.h>

#include <cstdlib>
#include <memory>
#include <string>

namespace {

shiki::ResourceManager &th06Resources() {
	static auto resources = [] {
		auto manager = std::make_unique<shiki::ResourceManager>();
		if (!manager->mountAssetPackage("assets"))
			std::abort();
		if (!shiki::ecl::registerEclFileLoader(
		        *manager->getAssetStore(),
		        shiki::asset::AssetFormat::fromName("shiki.compat.th06.ecl.v1"),
		        6))
			std::abort();
		return manager;
	}();
	return *resources;
}

shiki::asset::AssetStore &th06Assets() {
	return *th06Resources().getAssetStore();
}

bool loadEclStage(shiki::ecl::ECLParser &parser, int stage) {
	auto loaded =
	    th06Assets().load<shiki::ecl::ECLFile>(shiki::asset::AssetId::fromName(
	        "th06.ecl.ecldata" + std::to_string(stage)));
	if (!loaded)
		return false;
	parser.setFile(**loaded);
	return true;
}

bool loadEclStage(shiki::ecl::ECLEngine &engine, int stage) {
	return engine
	    .loadECLAsset(th06Assets(),
	                  shiki::asset::AssetId::fromName("th06.ecl.ecldata" +
	                                                  std::to_string(stage)))
	    .has_value();
}

shiki::ecl::ECLParam intParam(int32_t value) { return {'S', value, false}; }

shiki::ecl::ECLParam floatParam(float value) { return {'f', value, false}; }

shiki::ecl::ECLParam uintParam(uint32_t value) { return {'U', value, false}; }

shiki::ecl::ECLParam stringParam(std::string value) {
	return {'z', std::move(value), false};
}

shiki::ecl::ECLInstruction
instruction(uint32_t time, uint16_t id,
            std::vector<shiki::ecl::ECLParam> params) {
	shiki::ecl::ECLInstruction result;
	result.time = time;
	result.id = id;
	result.params = std::move(params);
	return result;
}

} // namespace

TEST_CASE("IN-style deathbomb ring contracts through the frozen window",
          "[player][deathbomb][effect]") {
	CHECK(TRUE_DEATHBOMB_FRAMES == 8);
	CHECK(DEATHBOMB_TOTAL_FRAMES == 30);
	CHECK(DEATHBOMB_EFFECT_FRAMES == 22);
	CHECK_FALSE(sampleTH06DeathbombEffect(TRUE_DEATHBOMB_FRAMES - 1).active);
	const auto start = sampleTH06DeathbombEffect(TRUE_DEATHBOMB_FRAMES);
	REQUIRE(start.active);
	CHECK(start.scale == Catch::Approx(6.25f));
	CHECK(start.alpha == Catch::Approx(1.0f));
	const auto middle = sampleTH06DeathbombEffect(TRUE_DEATHBOMB_FRAMES +
	                                              DEATHBOMB_EFFECT_FRAMES / 2);
	REQUIRE(middle.active);
	CHECK(middle.scale == Catch::Approx(3.125f));
	CHECK(middle.alpha == Catch::Approx(0.5f));
	const auto end = sampleTH06DeathbombEffect(DEATHBOMB_TOTAL_FRAMES);
	CHECK_FALSE(end.active);
	CHECK(end.scale == Catch::Approx(0.0f));
}

TEST_CASE("Debug AI makes one true-or-long deathbomb decision",
          "[player][deathbomb][ai]") {
	CHECK(sampleTH06DebugAiDeathbombTarget(2, false, 0.49f) == 2);
	CHECK(sampleTH06DebugAiDeathbombTarget(2, false, 0.50f) ==
	      TRUE_DEATHBOMB_FRAMES);
	CHECK(sampleTH06DebugAiDeathbombTarget(5, true, 0.89f) == 5);
	CHECK(sampleTH06DebugAiDeathbombTarget(5, true, 0.90f) ==
	      TRUE_DEATHBOMB_FRAMES);
	CHECK(sampleTH06DebugAiDeathbombTarget(TRUE_DEATHBOMB_FRAMES, false,
	                                       1.0f) == TRUE_DEATHBOMB_FRAMES);
}

TEST_CASE("TH06 score display awards extends at the original thresholds",
          "[score][extend]") {
	GameState state;
	state.score = 10000000;
	int awards = 0;
	for (int frame = 0; frame < 1000 && state.guiScore < state.score; ++frame)
		awards += updateTH06GuiScore(state);

	CHECK(state.guiScore == 10000000);
	CHECK(state.lives == 3);
	CHECK(state.extraLives == 1);
	CHECK(awards == 1);
	CHECK(state.highScore == 10000000);
}

TEST_CASE("TH06 extend threshold advances while the life stock is full",
          "[score][extend]") {
	GameState state;
	state.lives = TH06_MAX_LIVES;
	state.score = 10000000;
	for (int frame = 0; frame < 1000 && state.guiScore < state.score; ++frame)
		CHECK(updateTH06GuiScore(state) == 0);

	CHECK(state.extraLives == 1);
	CHECK(state.lives == TH06_MAX_LIVES);
}

TEST_CASE("TH06 Extra clear score uses the original stage result formula",
          "[score][stage-clear]") {
	GameState state;
	state.power = 128;
	state.graze = 100;
	state.pointItemsCollected = 10;
	state.lives = 2;
	state.bombs = 3;
	CHECK(calculateTH06ExtraStageClearScore(state) == 18416000);
}

TEST_CASE("TH06 stage asset preserves original title and song metadata",
          "[asset][stage]") {
	auto loaded = th06Assets().load<shiki::asset::StageAsset>(
	    shiki::asset::AssetId::fromName("th06.stage.7"));
	REQUIRE(loaded);
	CHECK((*loaded)->name.ends_with("Sister of Scarlet"));
	CHECK_FALSE((*loaded)->songs[0].empty());
	CHECK((*loaded)->songs[1].starts_with("U.N."));
}

TEST_CASE("TH06 timeline preserves spawn fields and starts at frame zero",
          "[ecl]") {
	shiki::ecl::ECLEngine engine;
	REQUIRE(loadEclStage(engine, 7));

	std::vector<shiki::ecl::EnemySpawnParams> spawns;
	engine.setEnemySpawnCallback(
	    [&](const shiki::ecl::EnemySpawnParams &spawn) {
		    spawns.push_back(spawn);
	    });
	engine.start();

	for (int frame = 0; frame <= 440; ++frame) {
		engine.update(1.0f / 60.0f);
	}

	REQUIRE(spawns.size() == 2);
	CHECK(spawns[0].subId == 0);
	CHECK(spawns[0].x == Catch::Approx(32.0f));
	CHECK(spawns[0].y == Catch::Approx(-48.0f));
	CHECK(spawns[0].z == Catch::Approx(0.0f));
	CHECK(spawns[0].life == 20);
	CHECK(spawns[0].itemDrop == 0);
	CHECK(spawns[0].score == 2000);
	CHECK_FALSE(spawns[0].invertX);
}

TEST_CASE("TH06 stage one timeline spawns and updates its first enemies",
          "[ecl][stage1]") {
	shiki::ecl::ECLEngine engine;
	engine.initialize();
	th06Resources().loadSpriteAtlas("stg1enm");
	th06Resources().loadSpriteAtlas("stg1enm2");

	std::vector<ECLEnemy> enemies;
	const auto &parser = engine.getParser();
	engine.setEnemySpawnCallback([&](const auto &spawn) {
		auto enemy = createECLEnemy(parser, 0, spawn.subId, spawn.x, spawn.y,
		                            0.0F, 0.0F, nullptr, &th06Resources());
		enemy.stageNumber = 1;
		enemy.difficulty = 1;
		enemies.push_back(std::move(enemy));
	});
	REQUIRE(engine.loadECLAsset(
	    th06Assets(), shiki::asset::AssetId::fromName("th06.ecl.ecldata7")));
	engine.start();
	engine.shutdown();
	REQUIRE(engine.loadECLAsset(
	    th06Assets(), shiki::asset::AssetId::fromName("th06.ecl.ecldata1")));
	engine.start();
	for (int frame = 0; frame < 900; ++frame) {
		engine.update(1.0F / 60.0F);
		for (auto &enemy : enemies)
			enemy.update(1.0F / 60.0F);
	}
	CHECK_FALSE(enemies.empty());
}

TEST_CASE("TH06 timeline restores mirrored and randomized spawn variants",
          "[ecl][timeline]") {
	shiki::ecl::ECLEngine engine;
	REQUIRE(loadEclStage(engine, 7));

	std::vector<shiki::ecl::EnemySpawnParams> spawns;
	engine.setEnemySpawnCallback(
	    [&](const shiki::ecl::EnemySpawnParams &spawn) {
		    spawns.push_back(spawn);
	    });
	engine.setBossInterruptCallback([](int32_t, int32_t) { return true; });
	engine.setBossAliveCallback([](int32_t) { return false; });
	engine.start();

	for (int frame = 0; frame <= 6000; ++frame)
		engine.update(1.0f / 60.0f);

	const auto mirrored =
	    std::find_if(spawns.begin(), spawns.end(), [](const auto &spawn) {
		    return spawn.subId == 0 && spawn.x == 320.0f;
	    });
	REQUIRE(mirrored != spawns.end());
	CHECK(mirrored->invertX);

	const auto randomized =
	    std::find_if(spawns.begin(), spawns.end(),
	                 [](const auto &spawn) { return spawn.subId == 10; });
	REQUIRE(randomized != spawns.end());
	CHECK(randomized->x >= 0.0f);
	CHECK(randomized->x < 384.0f);
	CHECK(randomized->y == Catch::Approx(32.0f));
	CHECK_FALSE(randomized->invertX);
}

TEST_CASE("TH06 timeline interrupts Patchouli and waits for the boss to end",
          "[ecl][boss]") {
	shiki::ecl::ECLEngine engine;
	REQUIRE(loadEclStage(engine, 7));

	bool patchouliAlive = false;
	bool spawnedPostBossFairy = false;
	int interruptBossId = -1;
	int interruptId = -1;
	engine.setEnemySpawnCallback(
	    [&](const shiki::ecl::EnemySpawnParams &spawn) {
		    if (spawn.subId == 16)
			    patchouliAlive = true;
		    if (spawn.subId == 6)
			    spawnedPostBossFairy = true;
	    });
	engine.setBossInterruptCallback([&](int32_t bossId, int32_t requested) {
		interruptBossId = bossId;
		interruptId = requested;
		return true;
	});
	engine.setBossAliveCallback(
	    [&](int32_t bossId) { return bossId == 0 && patchouliAlive; });
	engine.start();

	for (int frame = 0; frame < 6000; ++frame)
		engine.update(1.0f / 60.0f);

	CHECK(patchouliAlive);
	CHECK(interruptBossId == 0);
	CHECK(interruptId == 0);
	CHECK_FALSE(spawnedPostBossFairy);

	patchouliAlive = false;
	for (int frame = 0; frame < 300; ++frame)
		engine.update(1.0f / 60.0f);
	CHECK(spawnedPostBossFairy);
}

TEST_CASE("TH06 Patchouli interrupt enters the firing subroutine",
          "[ecl][boss][bullet]") {
	shiki::ecl::ECLParser parser;
	REQUIRE(loadEclStage(parser, 7));

	ECLEnemy patchouli;
	patchouli.eclParser = &parser;
	patchouli.sub = parser.getSubroutine(16);
	patchouli.subId = 16;
	patchouli.x = 192.0f;
	patchouli.y = 128.0f;
	patchouli.playerX = 192.0f;
	patchouli.playerY = 400.0f;
	int bulletCount = 0;
	patchouli.onSpawnBullet = [&](const ECLBulletSpawn &) { ++bulletCount; };

	for (int frame = 0; frame < 120; ++frame)
		patchouli.update(1.0f / 60.0f);
	REQUIRE(patchouli.isBoss);
	REQUIRE(patchouli.triggerInterrupt(0));

	for (int frame = 0; frame < 1800; ++frame)
		patchouli.update(1.0f / 60.0f);
	CHECK(bulletCount > 0);
}

TEST_CASE("TH06 main-loop ordering starts Patchouli's attack",
          "[ecl][boss][bullet][integration]") {
	shiki::ecl::ECLEngine engine;
	REQUIRE(loadEclStage(engine, 7));

	std::vector<ECLEnemy> enemies;
	int bulletCount = 0;
	bool interruptTriggered = false;
	int interruptAttempts = 0;
	int requestedBossId = -1;
	int requestedInterruptId = -1;
	engine.setEnemySpawnCallback(
	    [&](const shiki::ecl::EnemySpawnParams &spawn) {
		    if (spawn.subId != 16)
			    return;
		    auto enemy = createECLEnemy(engine.getParser(), 0, spawn.subId,
		                                spawn.x, spawn.y, 0.0f, 0.0f, nullptr);
		    enemy.onSpawnBullet = [&](const ECLBulletSpawn &) {
			    ++bulletCount;
		    };
		    enemies.push_back(std::move(enemy));
	    });
	engine.setBossInterruptCallback([&](int32_t bossId, int32_t interruptId) {
		++interruptAttempts;
		requestedBossId = bossId;
		requestedInterruptId = interruptId;
		for (auto &enemy : enemies) {
			if (enemy.alive && enemy.isBoss && enemy.bossId == bossId) {
				interruptTriggered = enemy.triggerInterrupt(interruptId);
				return interruptTriggered;
			}
		}
		return false;
	});
	engine.setBossAliveCallback([&](int32_t bossId) {
		return std::any_of(
		    enemies.begin(), enemies.end(), [bossId](const ECLEnemy &enemy) {
			    return enemy.alive && enemy.isBoss && enemy.bossId == bossId;
		    });
	});
	engine.start();

	for (int frame = 0; frame < 5700; ++frame) {
		updateECLEnemies(enemies, 1.0f / 60.0f);
		engine.update(1.0f / 60.0f);
	}

	REQUIRE(enemies.size() == 1);
	INFO("sub=" << enemies[0].currentSubId << " isBoss=" << enemies[0].isBoss
	            << " bossId=" << enemies[0].bossId
	            << " interrupts=" << enemies[0].interrupts.size() << " instr="
	            << enemies[0].instrIndex << " elapsed=" << enemies[0].elapsed
	            << " attempts=" << interruptAttempts
	            << " requestedBoss=" << requestedBossId
	            << " requestedInterrupt=" << requestedInterruptId
	            << " hasInterrupt0=" << enemies[0].interrupts.contains(0)
	            << " firstInterrupt="
	            << (enemies[0].interrupts.empty()
	                    ? -1
	                    : enemies[0].interrupts.begin()->first));
	CHECK(interruptTriggered);
	CHECK(enemies[0].currentSubId != 16);
	CHECK(enemies[0].interactable);
	CHECK(enemies[0].damageable);
	CHECK(bulletCount > 0);
}

TEST_CASE("ECL enemy axis velocity is measured per logic frame", "[ecl]") {
	shiki::ecl::ECLSubroutine sub;
	sub.instructions.push_back(instruction(
	    0, 44, {floatParam(2.0f), floatParam(3.0f), floatParam(0.0f)}));

	ECLEnemy enemy;
	enemy.sub = &sub;
	enemy.update(1.0f / 60.0f);

	CHECK(enemy.x == Catch::Approx(2.0f));
	CHECK(enemy.y == Catch::Approx(3.0f));
}

TEST_CASE("ECL timeline mirroring does not flip velocity every frame",
          "[ecl]") {
	shiki::ecl::ECLSubroutine sub;
	sub.instructions.push_back(instruction(
	    0, 44, {floatParam(2.0f), floatParam(0.0f), floatParam(0.0f)}));

	ECLEnemy enemy;
	enemy.sub = &sub;
	enemy.invertX = true;
	enemy.update(2.0f / 60.0f);

	CHECK(enemy.x == Catch::Approx(-4.0f));
	CHECK(enemy.vx == Catch::Approx(2.0f));
}

TEST_CASE("ECL position interpolation uses frame duration and reaches target",
          "[ecl]") {
	shiki::ecl::ECLSubroutine sub;
	sub.instructions.push_back(
	    instruction(0, 56,
	                {intParam(60), floatParam(120.0f), floatParam(60.0f),
	                 floatParam(0.0f)}));

	ECLEnemy enemy;
	enemy.sub = &sub;
	for (int frame = 0; frame < 60; ++frame) {
		enemy.update(1.0f / 60.0f);
	}

	CHECK(enemy.x == Catch::Approx(120.0f));
	CHECK(enemy.y == Catch::Approx(60.0f));
}

TEST_CASE("ECL relative jumps use byte addresses and preserve VM time",
          "[ecl]") {
	shiki::ecl::ECLSubroutine sub;
	sub.instructions.push_back(
	    instruction(0, 4, {intParam(-10001), intParam(2)}));
	sub.instructions.push_back(instruction(
	    0, 44, {floatParam(1.0f), floatParam(0.0f), floatParam(0.0f)}));
	sub.instructions.push_back(
	    instruction(1, 3, {intParam(0), intParam(-20), intParam(-10001)}));
	sub.instructions[0].address = 100;
	sub.instructions[1].address = 120;
	sub.instructions[2].address = 140;

	ECLEnemy enemy;
	enemy.sub = &sub;
	enemy.update(3.0f / 60.0f);

	CHECK(enemy.x == Catch::Approx(3.0f));
	CHECK(enemy.vars.getInt(-10001) == 0);
}

TEST_CASE("ECL enemy executes only the selected difficulty branch", "[ecl]") {
	shiki::ecl::ECLSubroutine sub;
	sub.instructions.push_back(instruction(
	    0, 44, {floatParam(10.0f), floatParam(0.0f), floatParam(0.0f)}));
	sub.instructions.back().rankMask = 0x0100;
	sub.instructions.push_back(instruction(
	    0, 44, {floatParam(2.0f), floatParam(0.0f), floatParam(0.0f)}));
	sub.instructions.back().rankMask = 0x0200;

	ECLEnemy enemy;
	enemy.sub = &sub;
	enemy.difficulty = 1;
	enemy.update(1.0f / 60.0f);

	CHECK(enemy.x == Catch::Approx(2.0f));
}

TEST_CASE("TH06 aimed fan preserves original alternating angle layout",
          "[ecl][bullet]") {
	shiki::ecl::ECLSubroutine sub;
	sub.instructions.push_back(instruction(
	    0, 67,
	    {intParam(2), intParam(3), intParam(3), intParam(1), floatParam(2.0f),
	     floatParam(2.0f), floatParam(0.1f), floatParam(0.2f), intParam(0)}));

	ECLEnemy enemy;
	enemy.sub = &sub;
	enemy.playerX = 100.0f;
	enemy.playerY = 0.0f;
	std::vector<float> angles;
	enemy.onSpawnBullet = [&](const ECLBulletSpawn &spawn) {
		angles.push_back(std::atan2(spawn.vy, spawn.vx));
	};
	enemy.update(1.0f / 60.0f);

	REQUIRE(angles.size() == 3);
	CHECK(angles[0] == Catch::Approx(0.1f));
	CHECK(angles[1] == Catch::Approx(-0.1f));
	CHECK(angles[2] == Catch::Approx(0.3f));
}

TEST_CASE(
    "TH06 circle uses both counts, layer rotation, and speed interpolation",
    "[ecl][bullet]") {
	shiki::ecl::ECLSubroutine sub;
	sub.instructions.push_back(instruction(
	    0, 70,
	    {intParam(1), intParam(4), intParam(4), intParam(2), floatParam(4.0f),
	     floatParam(2.0f), floatParam(0.0f), floatParam(0.25f), intParam(0)}));

	ECLEnemy enemy;
	enemy.sub = &sub;
	struct Shot {
		float speed;
		float angle;
	};
	std::vector<Shot> shots;
	enemy.onSpawnBullet = [&](const ECLBulletSpawn &spawn) {
		shots.push_back({std::hypot(spawn.vx, spawn.vy) / 60.0f,
		                 std::atan2(spawn.vy, spawn.vx)});
	};
	enemy.update(1.0f / 60.0f);

	REQUIRE(shots.size() == 8);
	CHECK(shots[0].speed == Catch::Approx(4.0f));
	CHECK(shots[4].speed == Catch::Approx(3.0f));
	CHECK(shots[0].angle == Catch::Approx(0.0f));
	CHECK(shots[4].angle == Catch::Approx(0.25f));
}

TEST_CASE("TH06 SHOOTNOW reuses the last complete bullet pattern",
          "[ecl][bullet]") {
	shiki::ecl::ECLSubroutine sub;
	sub.instructions.push_back(instruction(
	    0, 68,
	    {intParam(0), intParam(0), intParam(2), intParam(2), floatParam(1.0f),
	     floatParam(1.0f), floatParam(0.0f), floatParam(0.1f), intParam(0)}));
	sub.instructions.push_back(instruction(1, 80, {}));

	ECLEnemy enemy;
	enemy.sub = &sub;
	int shotCount = 0;
	enemy.onSpawnBullet = [&](const ECLBulletSpawn &) { ++shotCount; };
	enemy.update(2.0f / 60.0f);

	CHECK(shotCount == 8);
}

TEST_CASE("TH06 SHOOTNOW fires while interval shooting is disabled",
          "[ecl][bullet]") {
	shiki::ecl::ECLSubroutine sub;
	sub.instructions.push_back(instruction(
	    0, 68,
	    {intParam(0), intParam(0), intParam(1), intParam(1), floatParam(1.0f),
	     floatParam(1.0f), floatParam(0.0f), floatParam(0.0f), intParam(0)}));
	sub.instructions.push_back(instruction(0, 78, {}));
	sub.instructions.push_back(instruction(0, 80, {}));

	ECLEnemy enemy;
	enemy.sub = &sub;
	int shotCount = 0;
	enemy.onSpawnBullet = [&](const ECLBulletSpawn &) { ++shotCount; };
	enemy.update(1.0f / 60.0f);

	CHECK_FALSE(enemy.inFiring);
	CHECK(shotCount == 2);
}

TEST_CASE("TH06 bullet effects are copied into every spawned bullet",
          "[ecl][bullet]") {
	shiki::ecl::ECLSubroutine sub;
	sub.instructions.push_back(
	    instruction(0, 82,
	                {intParam(96), intParam(-1), intParam(-1), intParam(-1),
	                 floatParam(0.015f), floatParam(-999.0f), floatParam(-1.0f),
	                 floatParam(-1.0f)}));
	sub.instructions.push_back(
	    instruction(0, 68,
	                {intParam(3), intParam(2), intParam(1), intParam(1),
	                 floatParam(0.0f), floatParam(0.0f), floatParam(0.0f),
	                 floatParam(0.0f), intParam(0x18)}));

	ECLEnemy enemy;
	enemy.sub = &sub;
	ECLBulletSpawn captured;
	enemy.onSpawnBullet = [&](const ECLBulletSpawn &spawn) {
		captured = spawn;
	};
	enemy.update(1.0f / 60.0f);

	CHECK(captured.flags == 0x18);
	CHECK(captured.exInts[0] == 96);
	CHECK(captured.exFloats[0] == Catch::Approx(0.015f));
	CHECK(captured.exFloats[1] == Catch::Approx(-999.0f));
}

TEST_CASE("TH06 bubble bullets use the etama4 atlas", "[ecl][bullet][anm]") {
	CHECK(std::string(getBulletSpriteAtlas(9)) == "etama4");
	CHECK(getBulletSpriteIndex(9, 0) == 0);
	CHECK(getBulletSpriteIndex(9, 3) == 3);
	CHECK(getTH06BulletBlendMode(9) == shiki::BlendMode::Add);
	CHECK(std::string(getBulletSpriteAtlas(8)) == "etama3");
	CHECK(getTH06BulletBlendMode(6) == shiki::BlendMode::Alpha);
}

TEST_CASE("TH06 Royal Flare EX instruction emits accelerating bullets",
          "[ecl][boss][bullet]") {
	shiki::ecl::ECLParser parser;
	REQUIRE(loadEclStage(parser, 7));

	ECLEnemy patchouli;
	patchouli.eclParser = &parser;
	patchouli.sub = parser.getSubroutine(23);
	patchouli.currentSubId = 23;
	int shotCount = 0;
	ECLBulletSpawn firstShot;
	patchouli.onSpawnBullet = [&](const ECLBulletSpawn &spawn) {
		if (shotCount++ == 0)
			firstShot = spawn;
	};
	for (int frame = 0; frame < 240; ++frame)
		patchouli.update(1.0f / 60.0f);

	CHECK(shotCount > 0);
	CHECK((firstShot.flags & 0x10) != 0);
	CHECK(firstShot.exInts[0] == 100);
	CHECK(firstShot.exFloats[0] > 0.0f);
	CHECK(std::isfinite(firstShot.angle));
}

TEST_CASE("TH06 Silent Selene preserves the angle of zero-speed bullets",
          "[ecl][boss][bullet]") {
	shiki::ecl::ECLParser parser;
	REQUIRE(loadEclStage(parser, 7));

	ECLEnemy patchouli;
	patchouli.eclParser = &parser;
	patchouli.sub = parser.getSubroutine(22);
	patchouli.currentSubId = 22;
	std::vector<ECLBulletSpawn> shots;
	patchouli.onSpawnBullet = [&](const ECLBulletSpawn &spawn) {
		shots.push_back(spawn);
	};
	for (int frame = 0; frame < 240; ++frame)
		patchouli.update(1.0f / 60.0f);

	REQUIRE_FALSE(shots.empty());
	CHECK(std::hypot(shots[0].vx, shots[0].vy) == Catch::Approx(0.0f));
	CHECK(shots[0].angle > 1.0f);
	CHECK(shots[0].angle < 2.2f);
	CHECK((shots[0].flags & 0x10) != 0);
}

TEST_CASE("TH06 boss pose sets follow horizontal movement on their first tick",
          "[ecl][boss][anm]") {
	shiki::ecl::ECLParser parser;
	REQUIRE(loadEclStage(parser, 7));

	auto patchouli = createECLEnemy(parser, 0, 16, 0.0f, 0.0f, 0.0f, 0.0f,
	                                nullptr, &th06Resources());
	auto flandre = createECLEnemy(parser, 0, 31, 0.0f, 0.0f, 0.0f, 0.0f,
	                              nullptr, &th06Resources());
	patchouli.update(1.0f / 60.0f);
	flandre.update(1.0f / 60.0f);

	CHECK(patchouli.animationScript == 68);
	CHECK(flandre.animationScript == 162);
	CHECK(std::any_of(flandre.auxiliaryAnimations.begin(),
	                  flandre.auxiliaryAnimations.end(),
	                  [](const auto &animation) { return animation.active; }));
}

TEST_CASE("TH06 laser slots create rotate offset and cancel lasers",
          "[ecl][laser]") {
	shiki::ecl::ECLSubroutine sub;
	sub.instructions.push_back(instruction(0, 87, {intParam(2)}));
	sub.instructions.push_back(instruction(
	    0, 85,
	    {intParam(1), intParam(6), floatParam(0.5f), floatParam(0.0f),
	     floatParam(32.0f), floatParam(400.0f), floatParam(368.0f),
	     floatParam(24.0f), intParam(60), intParam(800), intParam(20),
	     intParam(60), intParam(18), intParam(0)}));
	sub.instructions.push_back(
	    instruction(0, 88, {intParam(2), floatParam(0.25f)}));
	sub.instructions.push_back(instruction(
	    0, 90,
	    {intParam(2), floatParam(8.0f), floatParam(12.0f), floatParam(0.0f)}));
	sub.instructions.push_back(instruction(1, 92, {intParam(2)}));

	ECLEnemy enemy;
	enemy.sub = &sub;
	enemy.x = 100.0f;
	enemy.y = 50.0f;
	enemy.update(1.0f / 60.0f);

	const int laserIndex = enemy.laserReferences[2];
	REQUIRE(laserIndex >= 0);
	CHECK(enemy.lasers[static_cast<size_t>(laserIndex)].active);
	CHECK(enemy.lasers[static_cast<size_t>(laserIndex)].angle ==
	      Catch::Approx(0.75f));
	CHECK(enemy.lasers[static_cast<size_t>(laserIndex)].x ==
	      Catch::Approx(108.0f));
	CHECK(enemy.lasers[static_cast<size_t>(laserIndex)].y ==
	      Catch::Approx(62.0f));

	enemy.update(1.0f / 60.0f);
	CHECK(enemy.lasers[static_cast<size_t>(laserIndex)].cancelFrames >= 0);
	CHECK(enemy.lasers[static_cast<size_t>(laserIndex)].state == 2);
	CHECK(enemy.lasers[static_cast<size_t>(laserIndex)].stateTimer == 1);
	for (int frame = 0; frame < 20; ++frame)
		enemy.update(1.0f / 60.0f);
	CHECK_FALSE(enemy.lasers[static_cast<size_t>(laserIndex)].active);
}

TEST_CASE("TH06 preserved enemy death modes keep ECL slots alive",
          "[ecl][enemy][death]") {
	ECLEnemy enemy;
	enemy.hp = 0;
	enemy.deathMode = 1;
	int effects = 0;
	enemy.onSpawnEffect = [&](int, float, float, int count, uint32_t) {
		effects += count;
	};

	CHECK(enemy.finishDeath(false));
	CHECK(enemy.alive);
	CHECK_FALSE(enemy.interactable);
	CHECK_FALSE(enemy.damageable);
	CHECK(effects == 5);
	CHECK(enemy.finishDeath(false));
	CHECK(effects == 5);
}

TEST_CASE("TH06 death mode three preserves the boss and emits three effects",
          "[ecl][enemy][death]") {
	ECLEnemy enemy;
	enemy.hp = 0;
	enemy.deathMode = 3;
	int effects = 0;
	enemy.onSpawnEffect = [&](int, float, float, int count, uint32_t) {
		effects += count;
	};

	CHECK(enemy.finishDeath(true));
	CHECK(enemy.alive);
	CHECK(enemy.hp == 1);
	CHECK_FALSE(enemy.damageable);
	CHECK(enemy.deathMode == 0);
	CHECK(effects == 3);
}

TEST_CASE("TH06 final death mode resolves before Flandre's cleanup callback",
          "[ecl][boss][death]") {
	shiki::ecl::ECLParser parser;
	REQUIRE(loadEclStage(parser, 7));
	auto boss =
	    createECLEnemy(parser, 0, 68, 192.0f, 96.0f, 0.0f, 0.0f, nullptr);
	boss.isBoss = true;
	boss.bossId = 0;
	boss.hp = 0;
	boss.deathMode = 3;
	boss.deathCallbackSub = 69;
	int bossHidden = 0;
	boss.onBossChanged = [&](bool enabled) { bossHidden += !enabled; };

	REQUIRE(boss.handleDamageCallbacks());
	CHECK(boss.currentSubId == 69);
	CHECK(boss.deathMode == 0);
	CHECK(bossHidden == 1);
	for (int frame = 0; frame < 120; ++frame)
		boss.update(1.0f / 60.0f);
	CHECK_FALSE(boss.alive);
}

TEST_CASE("TH06 EX instruction 14 emits bullets along active lasers",
          "[ecl][laser][bullet]") {
	shiki::ecl::ECLSubroutine sub;
	sub.instructions.push_back(instruction(0, 87, {intParam(0)}));
	sub.instructions.push_back(instruction(
	    0, 85,
	    {intParam(1), intParam(2), floatParam(0.0f), floatParam(0.0f),
	     floatParam(0.0f), floatParam(144.0f), floatParam(144.0f),
	     floatParam(12.0f), intParam(0), intParam(60), intParam(1), intParam(0),
	     intParam(0), intParam(0)}));
	sub.instructions.push_back(instruction(0, 78, {}));
	sub.instructions.push_back(instruction(
	    0, 68,
	    {intParam(0), intParam(1), intParam(1), intParam(1), floatParam(2.0f),
	     floatParam(2.0f), floatParam(0.0f), floatParam(0.0f), intParam(0)}));
	sub.instructions.push_back(
	    instruction(0, 121, {intParam(14), intParam(0)}));

	ECLEnemy enemy;
	enemy.sub = &sub;
	int shots = 0;
	enemy.onSpawnBullet = [&](const ECLBulletSpawn &) { ++shots; };
	enemy.update(1.0f / 60.0f);

	CHECK(shots == 3);
	CHECK(enemy.vars.getInt(-10004) == 1);
}

TEST_CASE("TH06 EX instruction 16 updates Flandre's final-phase variables",
          "[ecl][boss]") {
	ECLEnemy enemy;
	enemy.hp = 3000;
	enemy.runExInstruction(16, 0);

	CHECK(enemy.vars.getFloat(-10008) == Catch::Approx(1.5f));
	CHECK(enemy.vars.getInt(-10010) == 160);
}

TEST_CASE("TH06 global bullet EX instructions do not require a local pattern",
          "[ecl][bullet]") {
	ECLEnemy enemy;
	int calledInstruction = -1;
	int calledParameter = -1;
	enemy.onBulletTransform = [&](int instruction, int parameter, float,
	                              float) {
		calledInstruction = instruction;
		calledParameter = parameter;
		return 7;
	};

	enemy.runExInstruction(15, 23);

	CHECK(calledInstruction == 15);
	CHECK(calledParameter == 23);
	CHECK(enemy.vars.getInt(-10004) == 7);
}

TEST_CASE("TH06 global time stop is controlled only by EX instruction four",
          "[ecl][time-stop]") {
	ECLEnemy enemy;
	std::vector<bool> changes;
	enemy.onTimeStop = [&](bool stopped) { changes.push_back(stopped); };

	enemy.runExInstruction(0, 1);
	CHECK(changes.empty());
	CHECK_FALSE(enemy.globalTimeStopped);

	enemy.runExInstruction(4, 1);
	REQUIRE(changes.size() == 1);
	CHECK(changes.back());
	CHECK(enemy.globalTimeStopped);

	enemy.runExInstruction(4, 0);
	REQUIRE(changes.size() == 2);
	CHECK_FALSE(changes.back());
	CHECK_FALSE(enemy.globalTimeStopped);
}

TEST_CASE("TH06 global time stop freezes boss timeout but not enemy movement",
          "[ecl][time-stop]") {
	shiki::ecl::ECLSubroutine sub;
	ECLEnemy enemy;
	enemy.sub = &sub;
	enemy.movementMode = 0;
	enemy.vx = 1.0f;
	enemy.globalTimeStopped = true;

	enemy.update(1.0f / 60.0f);

	CHECK(enemy.bossTimerFrames == 0);
	CHECK(enemy.x == Catch::Approx(1.0f));
}

TEST_CASE("TH06 ECL readonly variables match the original GetVar table",
          "[ecl][variables]") {
	shiki::ecl::ECLSubroutine sub;
	sub.instructions.push_back(
	    instruction(0, 4, {intParam(-10001), intParam(-10013)}));
	sub.instructions.push_back(
	    instruction(0, 4, {intParam(-10002), intParam(-10014)}));
	sub.instructions.push_back(
	    instruction(0, 4, {intParam(-10003), intParam(-10022)}));
	sub.instructions.push_back(
	    instruction(0, 4, {intParam(-10004), intParam(-10025)}));

	ECLEnemy enemy;
	enemy.sub = &sub;
	enemy.difficulty = 3;
	enemy.rank = 19;
	enemy.bossTimerFrames = 47;
	enemy.elapsed = 9.0f;
	enemy.playerCharacter = 1;
	enemy.initializeEcl();

	CHECK(enemy.vars.getInt(-10001) == 3);
	CHECK(enemy.vars.getInt(-10002) == 19);
	CHECK(enemy.vars.getInt(-10003) == 47);
	CHECK(enemy.vars.getInt(-10004) == 3);
}

TEST_CASE("TH06 stage six EX instruction creates the original laser grid",
          "[ecl][laser]") {
	ECLEnemy enemy;
	enemy.difficulty = 1;
	enemy.runExInstruction(7, 0);

	const auto active = std::count_if(
	    enemy.lasers.begin(), enemy.lasers.end(),
	    [](const ECLEnemy::LaserState &laser) { return laser.active; });
	CHECK(active == 48);
	CHECK(enemy.lasers[0].width == Catch::Approx(28.0f));
	CHECK(enemy.lasers[0].startTime == 60);
	CHECK(enemy.lasers[16].startTime == 92);
}

TEST_CASE("TH06 Stage 3 ECL firing is independent of render frequency",
          "[ecl][stage3][timing]") {
	shiki::ecl::ECLParser parser;
	REQUIRE(loadEclStage(parser, 3));

	for (size_t subId = 0; subId < parser.getFile().subs.size(); ++subId) {
		DYNAMIC_SECTION("subroutine " << subId) {
			auto simulate = [&](float dt, int updates) {
				std::srand(static_cast<unsigned int>(subId + 1));
				auto enemy =
				    createECLEnemy(parser, 0, static_cast<int32_t>(subId),
				                   192.0f, 96.0f, 0.0f, 0.0f, nullptr);
				enemy.difficulty = 1;
				enemy.playerX = 192.0f;
				enemy.playerY = 400.0f;
				enemy.onSpawnBullet = [](const ECLBulletSpawn &) {};
				for (int update = 0; update < updates; ++update)
					enemy.update(dt);
				return enemy.spawnedBulletCount;
			};

			const auto at60Hz = simulate(1.0f / 60.0f, 600);
			const auto at600Hz = simulate(1.0f / 600.0f, 6000);
			CHECK(at600Hz == at60Hz);
			CHECK(at60Hz < 5000);
		}
	}
}

TEST_CASE("TH06 Meiling flower spell uses the boss timer for red waves",
          "[ecl][stage3][boss][timing]") {
	shiki::ecl::ECLParser parser;
	REQUIRE(loadEclStage(parser, 3));
	auto enemy =
	    createECLEnemy(parser, 0, 13, 192.0f, 96.0f, 0.0f, 0.0f, nullptr);
	enemy.difficulty = 1;
	enemy.playerX = 192.0f;
	enemy.playerY = 400.0f;
	int currentFrame = 0;
	std::vector<int> redWaveFrames;
	enemy.onSpawnBullet = [&](const ECLBulletSpawn &spawn) {
		if (spawn.bulletType == 5 && spawn.bulletColor == 2 &&
		    (redWaveFrames.empty() || redWaveFrames.back() != currentFrame))
			redWaveFrames.push_back(currentFrame);
	};
	for (; currentFrame < 600; ++currentFrame)
		enemy.update(1.0f / 60.0f);

	REQUIRE(redWaveFrames.size() >= 6);
	for (size_t index = 1; index < redWaveFrames.size(); ++index)
		CHECK(redWaveFrames[index] - redWaveFrames[index - 1] == 80);
}

TEST_CASE("TH06 Stage 4 firing subroutines remain reachable below the boss",
          "[ecl][stage4][timing]") {
	shiki::ecl::ECLParser parser;
	REQUIRE(loadEclStage(parser, 4));
	const auto simulate = [&](float playerY) {
		std::srand(53);
		auto enemy =
		    createECLEnemy(parser, 0, 52, 192.0f, 96.0f, 0.0f, 0.0f, nullptr);
		enemy.stageNumber = 4;
		enemy.difficulty = 2;
		enemy.playerX = 192.0f;
		enemy.playerY = playerY;
		enemy.onSpawnBullet = [](const ECLBulletSpawn &) {};
		for (int frame = 0; frame < 600; ++frame)
			enemy.update(1.0f / 60.0f);
		return enemy.spawnedBulletCount;
	};

	CHECK(simulate(32.0f) > 0);
	CHECK(simulate(400.0f) > 0);
}

TEST_CASE("TH06 ANM initialization preserves script-defined visibility",
          "[anm]") {
	auto makeFile = [](uint8_t terminalOpcode) {
		auto file = std::make_shared<TH06MenuAnmFile>();
		file->data = {0, 0, 1, 4, 0, 0, 0, 0, 0, 0, terminalOpcode, 0};
		file->scripts[0] = {0, file->data.size()};
		return file;
	};

	TH06MenuAnmVm stopped;
	REQUIRE(stopped.initialize(makeFile(21), 0));
	CHECK(stopped.visible);
	CHECK(stopped.stopped);

	TH06MenuAnmVm exitWithoutHiding;
	REQUIRE(exitWithoutHiding.initialize(makeFile(15), 0));
	CHECK(exitWithoutHiding.visible);
	CHECK(exitWithoutHiding.stopped);
}

TEST_CASE("TH06 SPELLCARDEND ignores nonspell cleanup instructions", "[ecl]") {
	shiki::ecl::ECLSubroutine sub;
	sub.instructions.push_back(instruction(0, 94, {}));

	ECLEnemy enemy;
	enemy.sub = &sub;
	int spellEndCount = 0;
	enemy.onSpellEnd = [&](int) { ++spellEndCount; };
	enemy.update(1.0f / 60.0f);

	CHECK(spellEndCount == 0);
}

TEST_CASE("TH06 SPELLCARDEND reads the global spell state", "[ecl]") {
	shiki::ecl::ECLSubroutine sub;
	sub.instructions.push_back(instruction(0, 94, {}));

	ECLEnemy enemy;
	enemy.sub = &sub;
	bool globalSpellActive = true;
	int spellEndCount = 0;
	enemy.isGlobalSpellActive = [&]() { return globalSpellActive; };
	enemy.onSpellEnd = [&](int) {
		globalSpellActive = false;
		++spellEndCount;
	};
	enemy.update(1.0f / 60.0f);

	CHECK_FALSE(globalSpellActive);
	CHECK(spellEndCount == 1);
}

TEST_CASE("TH06 Sakuya first nonspell keeps its original timeout", "[ecl]") {
	shiki::ecl::ECLParser parser;
	REQUIRE(loadEclStage(parser, 5));
	auto enemy =
	    createECLEnemy(parser, 0, 41, 192.0f, 96.0f, 0.0f, 0.0f, nullptr);
	enemy.stageNumber = 5;
	enemy.difficulty = 1;
	bool globalSpellActive = false;
	int spellStarts = 0;
	int spellEnds = 0;
	int bulletCancels = 0;
	enemy.isGlobalSpellActive = [&]() { return globalSpellActive; };
	enemy.onSpellStart = [&](int, int, const std::string &) {
		globalSpellActive = true;
		++spellStarts;
	};
	enemy.onSpellEnd = [&](int) {
		globalSpellActive = false;
		++spellEnds;
	};
	enemy.onBulletCancel = [&]() { ++bulletCancels; };
	enemy.initializeEcl();

	for (int frame = 0; frame < 2699; ++frame)
		enemy.update(1.0f / 60.0f);
	CHECK(spellStarts == 0);
	CHECK(spellEnds == 0);
	CHECK(bulletCancels == 0);

	for (int frame = 0; frame < 3; ++frame)
		enemy.update(1.0f / 60.0f);
	CHECK(spellStarts == 1);
}

TEST_CASE("TH06 Flandre EX instruction creates the steam particle wings",
          "[ecl][boss][effect]") {
	ECLEnemy enemy;
	enemy.x = 192.0f;
	enemy.y = 128.0f;
	struct SpawnedEffect {
		int id;
		float x;
		float y;
		uint32_t color;
		float vx;
		float vy;
		float ax;
		float ay;
	};
	std::vector<SpawnedEffect> effects;
	enemy.onSpawnMovingEffect = [&](int id, float x, float y, uint32_t color,
	                                float vx, float vy, float ax, float ay) {
		effects.push_back({id, x, y, color, vx, vy, ax, ay});
	};

	enemy.runExInstruction(6);

	REQUIRE(effects.size() == 2);
	CHECK(effects[0].id == 19);
	CHECK(effects[0].color == 0xff3030ff);
	CHECK(effects[0].x + effects[1].x == Catch::Approx(enemy.x * 2.0f));
	CHECK(effects[0].y == Catch::Approx(effects[1].y));
	CHECK(effects[0].ax == Catch::Approx(-effects[0].vx / 120.0f));
	CHECK(effects[0].ay == Catch::Approx(-effects[0].vy / 120.0f));
}

TEST_CASE("TH06 late Flandre spells execute their real firing subroutines",
          "[ecl][boss][bullet][integration]") {
	shiki::ecl::ECLParser parser;
	REQUIRE(loadEclStage(parser, 7));

	for (const int subId : {39, 44, 48, 50}) {
		DYNAMIC_SECTION("subroutine " << subId) {
			std::vector<ECLEnemy> enemies;
			std::vector<ECLEnemy> pending;
			int bulletCount = 0;
			int transformCount = 0;

			auto configure = [&](ECLEnemy &enemy) {
				enemy.playerX = 192.0f;
				enemy.playerY = 400.0f;
				enemy.onSpawnBullet = [&](const ECLBulletSpawn &) {
					++bulletCount;
				};
				enemy.onBulletTransform = [&](int, int, float, float) {
					++transformCount;
					return 1;
				};
				enemy.onKillAllEnemies = [] {};
				enemy.onSpawnChildEnemy = [&](int type, int childSubId, float x,
				                              float y, float z, float vx,
				                              float vy, float, float, int hp) {
					auto child = createECLEnemy(parser, type, childSubId, x, y,
					                            vx, vy, nullptr);
					child.z = z;
					child.hp = hp;
					child.playerX = 192.0f;
					child.playerY = 400.0f;
					child.onSpawnBullet = [&](const ECLBulletSpawn &) {
						++bulletCount;
					};
					child.onBulletTransform = [&](int, int, float, float) {
						++transformCount;
						return 1;
					};
					pending.push_back(std::move(child));
				};
			};

			auto boss = createECLEnemy(parser, 0, subId, 192.0f, 96.0f, 0.0f,
			                           0.0f, nullptr);
			configure(boss);
			enemies.push_back(std::move(boss));
			for (int frame = 0; frame < 900; ++frame) {
				for (auto &enemy : enemies)
					enemy.update(1.0f / 60.0f);
				enemies.insert(enemies.end(),
				               std::make_move_iterator(pending.begin()),
				               std::make_move_iterator(pending.end()));
				pending.clear();
			}

			CHECK(bulletCount > 0);
			if (subId == 44)
				CHECK(transformCount > 0);
		}
	}
}

TEST_CASE("TH06 Flandre emitter spawns preserve their ECL Z parameter",
          "[ecl][boss][enemy][integration]") {
	shiki::ecl::ECLParser parser;
	REQUIRE(loadEclStage(parser, 7));

	for (const int subId : {44, 50, 57}) {
		DYNAMIC_SECTION("subroutine " << subId) {
			auto boss = createECLEnemy(parser, 0, subId, 192.0f, 96.0f, 0.0f,
			                           0.0f, nullptr);
			std::vector<float> childZ;
			boss.onSpawnBullet = [](const ECLBulletSpawn &) {};
			boss.onKillAllEnemies = [] {};
			boss.onSpawnChildEnemy = [&](int, int, float, float, float z, float,
			                             float, float, float,
			                             int) { childZ.push_back(z); };

			for (int frame = 0; frame < 260; ++frame)
				boss.update(1.0f / 60.0f);

			REQUIRE_FALSE(childZ.empty());
			CHECK(std::any_of(childZ.begin(), childZ.end(),
			                  [](float z) { return std::abs(z) > 0.01f; }));
		}
	}
}

TEST_CASE("TH06 Nobody emitter stops instead of firing forever",
          "[ecl][boss][bullet][integration]") {
	shiki::ecl::ECLParser parser;
	REQUIRE(loadEclStage(parser, 7));
	auto emitter =
	    createECLEnemy(parser, 0, 66, 192.0f, 96.0f, 0.0f, 0.0f, nullptr);
	emitter.hp = 4;
	int bulletCount = 0;
	emitter.onSpawnBullet = [&](const ECLBulletSpawn &) { ++bulletCount; };

	for (int frame = 0; frame < 1200; ++frame)
		emitter.update(1.0f / 60.0f);

	INFO("sub=" << emitter.currentSubId << " instr=" << emitter.instrIndex
	            << " elapsed=" << emitter.elapsed << " hp=" << emitter.hp
	            << " x=" << emitter.x << " y=" << emitter.y);
	CHECK_FALSE(emitter.alive);
	CHECK(bulletCount > 0);
	CHECK(bulletCount < 1000);
}

TEST_CASE("TH06 Nobody emitter disables damage before player-shot collision",
          "[ecl][boss][bullet][integration]") {
	shiki::ecl::ECLParser parser;
	REQUIRE(loadEclStage(parser, 7));

	std::vector<ECLEnemy> enemies;
	auto emitter =
	    createECLEnemy(parser, 0, 66, 192.0f, 96.0f, 0.0f, 0.0f, nullptr);
	emitter.hp = 4;
	emitter.playerX = 192.0f;
	emitter.playerY = 400.0f;
	int bulletCount = 0;
	emitter.onSpawnBullet = [&](const ECLBulletSpawn &) { ++bulletCount; };
	enemies.push_back(std::move(emitter));

	struct PlayerShot {
		shiki::Sprite sprite;
		bool active = true;
		int damage = 10;
	};
	std::vector<PlayerShot> playerShots(1);
	playerShots.front().sprite.setPosition(192.0f, 96.0f);
	int score = 0;

	// SpawnEnemy runs time-0 ECL before the enemy can enter collision checks.
	enemies.front().initializeEcl();
	checkPlayerBulletsVsECLEnemies(playerShots, enemies, score);

	REQUIRE(enemies.size() == 1);
	CHECK(enemies.front().alive);
	CHECK(enemies.front().hp == 4);
	CHECK_FALSE(enemies.front().collidable);
	CHECK_FALSE(enemies.front().damageable);
	CHECK_FALSE(enemies.front().interactable);
	CHECK(playerShots.front().active);

	for (int frame = 0; frame < 10; ++frame)
		updateECLEnemies(enemies, 1.0f / 60.0f);
	CHECK(bulletCount >= 3);
}

TEST_CASE("TH06 Nobody opening emitter spawns its first aimed wave",
          "[ecl][boss][bullet][integration]") {
	shiki::ecl::ECLParser parser;
	REQUIRE(loadEclStage(parser, 7));
	auto boss =
	    createECLEnemy(parser, 0, 60, 192.0f, 96.0f, 0.0f, 0.0f, nullptr);
	boss.playerX = 192.0f;
	boss.playerY = 400.0f;
	std::vector<ECLEnemy> children;
	int openingShots = 0;
	std::vector<float> openingAngles;
	boss.onSpawnBullet = [](const ECLBulletSpawn &) {};
	boss.onKillAllEnemies = [] {};
	boss.onSpawnChildEnemy = [&](int type, int subId, float x, float y, float z,
	                             float vx, float vy, float, float, int hp) {
		auto child = createECLEnemy(parser, type, subId, x, y, vx, vy, nullptr);
		child.z = z;
		child.hp = hp;
		child.playerX = 192.0f;
		child.playerY = 400.0f;
		child.onSpawnBullet = [&](const ECLBulletSpawn &spawn) {
			if (spawn.flags == 0x202)
				++openingShots;
			if (spawn.flags == 0x202)
				openingAngles.push_back(spawn.angle);
		};
		child.initializeEcl();
		children.push_back(std::move(child));
	};

	for (int frame = 0; frame < 180; ++frame) {
		boss.update(1.0f / 60.0f);
		for (auto &child : children)
			child.update(1.0f / 60.0f);
	}

	REQUIRE_FALSE(children.empty());
	CHECK(children.front().currentSubId == 66);
	CHECK(openingShots > 0);
	REQUIRE(openingAngles.size() >= 3);
	const float playerAngle = std::numbers::pi_v<float> * 0.5f;
	CHECK(openingAngles[0] == Catch::Approx(playerAngle));
	CHECK(std::abs(openingAngles[1] - playerAngle) ==
	      Catch::Approx(std::numbers::pi_v<float> / 3.0f));
	CHECK(std::abs(openingAngles[2] - playerAngle) ==
	      Catch::Approx(std::numbers::pi_v<float> / 3.0f));
}

TEST_CASE("TH06 final Flandre subroutine ends the boss and dialogue wait",
          "[ecl][boss][integration]") {
	shiki::ecl::ECLParser parser;
	REQUIRE(loadEclStage(parser, 7));
	auto boss =
	    createECLEnemy(parser, 0, 69, 192.0f, 96.0f, 0.0f, 0.0f, nullptr);
	boss.isBoss = true;
	boss.bossId = 0;
	int bossDeathCount = 0;
	boss.onDeath = [&](bool isBoss) {
		if (isBoss)
			++bossDeathCount;
	};

	for (int frame = 0; frame < 1200; ++frame)
		boss.update(1.0f / 60.0f);

	INFO("sub=" << boss.currentSubId << " instr=" << boss.instrIndex
	            << " elapsed=" << boss.elapsed << " hp=" << boss.hp
	            << " deathSub=" << boss.deathCallbackSub);
	CHECK_FALSE(boss.alive);
	CHECK(bossDeathCount == 1);
}

TEST_CASE("TH06 Laevateinn and final spell keep bounded firing rates",
          "[ecl][boss][bullet][integration]") {
	shiki::ecl::ECLParser parser;
	REQUIRE(loadEclStage(parser, 7));

	for (const int subId : {36, 68}) {
		DYNAMIC_SECTION("subroutine " << subId) {
			auto boss = createECLEnemy(parser, 0, subId, 192.0f, 96.0f, 0.0f,
			                           0.0f, nullptr);
			boss.hp = 3000;
			boss.playerX = 192.0f;
			boss.playerY = 400.0f;
			int bulletCount = 0;
			boss.onSpawnBullet = [&](const ECLBulletSpawn &) { ++bulletCount; };
			boss.onKillAllEnemies = [] {};

			for (int frame = 0; frame < 1200; ++frame)
				boss.update(1.0f / 60.0f);

			CHECK(bulletCount > 0);
			CHECK(bulletCount < 5000);
			if (subId == 68)
				CHECK(boss.vars.getInt(-10010) >= 40);
		}
	}
}

TEST_CASE("TH06 Laevateinn CALL inherits its laser geometry variables",
          "[ecl][boss][laser][bullet]") {
	shiki::ecl::ECLParser parser;
	REQUIRE(loadEclStage(parser, 7));
	auto boss =
	    createECLEnemy(parser, 0, 36, 192.0f, 96.0f, 0.0f, 0.0f, nullptr);
	std::vector<ECLBulletSpawn> shots;
	boss.onSpawnBullet = [&](const ECLBulletSpawn &spawn) {
		shots.push_back(spawn);
	};
	boss.onKillAllEnemies = [] {};

	for (int frame = 0; frame < 122; ++frame)
		boss.update(1.0f / 60.0f);

	REQUIRE(boss.lasers[0].active);
	REQUIRE(boss.lasers[1].active);
	CHECK(boss.lasers[0].angle == Catch::Approx(-2.984513f + 0.039270f));
	CHECK(boss.lasers[1].angle == Catch::Approx(-2.984513f));
	REQUIRE_FALSE(shots.empty());
	CHECK(shots.front().angle == Catch::Approx(-1.413717f + 0.039270f));
}

TEST_CASE("TH06 clock lasers stay anchored and cancel after rotating",
          "[ecl][boss][laser][integration]") {
	shiki::ecl::ECLParser parser;
	REQUIRE(loadEclStage(parser, 7));
	auto emitter =
	    createECLEnemy(parser, 0, 58, 192.0f, 96.0f, 0.0f, 0.0f, nullptr);
	emitter.z = 96.0f;

	for (int frame = 0; frame < 31; ++frame)
		emitter.update(1.0f / 60.0f);
	for (size_t index = 0; index < 4; ++index) {
		INFO("laser=" << index);
		REQUIRE(emitter.lasers[index].active);
		CHECK(emitter.lasers[index].x == Catch::Approx(192.0f));
		CHECK(emitter.lasers[index].y == Catch::Approx(96.0f));
	}

	for (int frame = 0; frame < 720; ++frame)
		emitter.update(1.0f / 60.0f);
	for (size_t index = 0; index < 4; ++index) {
		CHECK(emitter.lasers[index].x == Catch::Approx(192.0f));
		CHECK(emitter.lasers[index].y == Catch::Approx(96.0f));
	}
	for (int frame = 0; frame < 160; ++frame)
		emitter.update(1.0f / 60.0f);
	for (size_t index = 0; index < 4; ++index)
		CHECK_FALSE(emitter.lasers[index].active);
}

TEST_CASE("rotated sprites keep their origin anchored at position",
          "[render][sprite][laser]") {
	shiki::Sprite sprite;
	sprite.setPosition(120.0f, 80.0f);
	sprite.setOrigin({8.0f, 200.0f});
	sprite.setScale(1.5f, 2.0f);
	sprite.setRotation(-90.0f);

	const auto transform = sprite.getTransform();
	const float x = transform.data[0] * 8.0f + transform.data[4] * 200.0f +
	                transform.data[12];
	const float y = transform.data[1] * 8.0f + transform.data[5] * 200.0f +
	                transform.data[13];
	CHECK(x == Catch::Approx(120.0f));
	CHECK(y == Catch::Approx(80.0f));
}

TEST_CASE("debug damage follows boss life and death callbacks",
          "[ecl][boss][debug]") {
	shiki::ecl::ECLParser parser;
	parser.getFile().subs.resize(2);
	ECLEnemy boss;
	boss.eclParser = &parser;
	boss.sub = &parser.getFile().subs[0];
	boss.deathCallbackSub = 1;
	boss.hp = 100;

	boss.lifeCallbackThreshold = 50;
	boss.lifeCallbackSub = 1;
	CHECK(boss.applyDebugDamage(100000000));
	CHECK(boss.sub == &parser.getFile().subs[1]);
	CHECK(boss.hp == 50);

	boss.lifeCallbackThreshold = -1;
	boss.deathCallbackSub = -1;
	CHECK(boss.applyDebugDamage(100000000));
	CHECK_FALSE(boss.alive);
}

TEST_CASE("debug damage cannot bypass a boss phase callback setup window",
          "[ecl][boss][debug]") {
	ECLEnemy boss;
	boss.isBoss = true;
	boss.hp = 1800;

	CHECK_FALSE(boss.applyDebugDamage(100000000));
	CHECK(boss.alive);
	CHECK(boss.hp == 1);
}

TEST_CASE("debug phase defeat can be used once per successive boss phase",
          "[ecl][boss][debug]") {
	shiki::ecl::ECLParser parser;
	parser.getFile().subs.resize(3);
	ECLEnemy boss;
	boss.eclParser = &parser;
	boss.sub = &parser.getFile().subs[0];
	boss.lifeCallbackThreshold = 100;
	boss.lifeCallbackSub = 1;

	REQUIRE(boss.applyDebugDamage(100000000));
	CHECK(boss.sub == &parser.getFile().subs[1]);

	// The next spell installs a fresh life callback after its setup runs.
	boss.lifeCallbackThreshold = 50;
	boss.lifeCallbackSub = 2;
	REQUIRE(boss.applyDebugDamage(100000000));
	CHECK(boss.sub == &parser.getFile().subs[2]);
}

TEST_CASE("debug damage defeats only Flandre's Cranberry Trap phase",
          "[ecl][boss][debug][integration]") {
	shiki::ecl::ECLParser parser;
	REQUIRE(loadEclStage(parser, 7));
	auto boss =
	    createECLEnemy(parser, 0, 31, 192.0f, 96.0f, 0.0f, 0.0f, nullptr);
	boss.onSpawnBullet = [](const ECLBulletSpawn &) {};
	boss.onKillAllEnemies = [] {};

	for (int frame = 0; frame < 180; ++frame)
		boss.update(1.0f / 60.0f);
	REQUIRE(boss.isBoss);
	REQUIRE(boss.triggerInterrupt(0));

	bool cranberryStarted = false;
	boss.onSpellStart = [&](int, int, const std::string &) {
		cranberryStarted = true;
	};
	for (int frame = 0; frame < 3600 && !cranberryStarted; ++frame)
		boss.update(1.0f / 60.0f);

	INFO("sub=" << boss.currentSubId << " hp=" << boss.hp
	            << " lifeThreshold=" << boss.lifeCallbackThreshold
	            << " lifeSub=" << boss.lifeCallbackSub
	            << " deathSub=" << boss.deathCallbackSub
	            << " timerThreshold=" << boss.timerCallbackThreshold
	            << " timerSub=" << boss.timerCallbackSub);
	REQUIRE(cranberryStarted);
	const int cranberrySub = boss.currentSubId;
	REQUIRE(boss.applyDebugDamage(100000000));
	CHECK(boss.alive);
	CHECK(boss.currentSubId != cranberrySub);
	for (int frame = 0; frame < 180; ++frame)
		boss.update(1.0f / 60.0f);
	INFO("after transition sub=" << boss.currentSubId << " hp=" << boss.hp
	                             << " deathSub=" << boss.deathCallbackSub);
	CHECK(boss.alive);
}

TEST_CASE(
    "TH06 boss, spellcard, and sound opcodes reach presentation callbacks",
    "[ecl][boss]") {
	shiki::ecl::ECLSubroutine sub;
	sub.instructions.push_back(instruction(0, 101, {intParam(0)}));
	sub.instructions.push_back(instruction(
	    0, 93, {intParam(7), intParam(12), stringParam("spell name")}));
	sub.instructions.push_back(instruction(0, 106, {intParam(16)}));
	sub.instructions.push_back(instruction(0, 126, {intParam(4)}));
	sub.instructions.push_back(instruction(0, 135, {intParam(1)}));
	sub.instructions.push_back(instruction(0, 83, {}));
	sub.instructions.push_back(instruction(0, 94, {}));

	ECLEnemy enemy;
	enemy.sub = &sub;
	bool bossEnabled = false;
	int spellId = -1;
	int portraitId = -1;
	std::string spellName;
	int soundId = -1;
	int spellEndCount = 0;
	int spellSecondsRemaining = -1;
	int bulletCancelCount = 0;
	enemy.timerCallbackThreshold = 601;
	enemy.onBossChanged = [&](bool enabled) { bossEnabled = enabled; };
	enemy.onSpellStart = [&](int id, int portrait, const std::string &name) {
		spellId = id;
		portraitId = portrait;
		spellName = name;
	};
	enemy.onSound = [&](int id) { soundId = id; };
	enemy.onBulletCancel = [&]() { ++bulletCancelCount; };
	enemy.onSpellEnd = [&](int secondsRemaining) {
		++spellEndCount;
		spellSecondsRemaining = secondsRemaining;
	};
	enemy.update(1.0f / 60.0f);

	CHECK(bossEnabled);
	CHECK(enemy.isBoss);
	CHECK(spellId == 12);
	CHECK(portraitId == 7);
	CHECK(spellName == "spell name");
	CHECK(soundId == 16);
	CHECK(enemy.bossLifeCount == 4);
	CHECK(enemy.timeoutSpell);
	CHECK(bulletCancelCount == 1);
	CHECK(spellEndCount == 1);
	CHECK(spellSecondsRemaining == 10);
}

TEST_CASE("TH06 ordinary spell timeout invalidates capture before callback",
          "[ecl][boss][spell]") {
	shiki::ecl::ECLParser parser;
	parser.getFile().subs.resize(2);
	ECLEnemy enemy;
	enemy.eclParser = &parser;
	enemy.sub = &parser.getFile().subs[0];
	enemy.spellActive = true;
	enemy.timerCallbackThreshold = 1;
	enemy.timerCallbackSub = 1;
	int failures = 0;
	enemy.onSpellCaptureFailed = [&]() { ++failures; };

	enemy.update(1.0f / 60.0f);
	CHECK(failures == 1);
	CHECK(enemy.sub == &parser.getFile().subs[1]);
}

TEST_CASE("TH06 timeout spell preserves capture at its timer callback",
          "[ecl][boss][spell]") {
	shiki::ecl::ECLParser parser;
	parser.getFile().subs.resize(2);
	ECLEnemy enemy;
	enemy.eclParser = &parser;
	enemy.sub = &parser.getFile().subs[0];
	enemy.spellActive = true;
	enemy.timeoutSpell = true;
	enemy.timerCallbackThreshold = 1;
	enemy.timerCallbackSub = 1;
	int failures = 0;
	enemy.onSpellCaptureFailed = [&]() { ++failures; };

	enemy.update(1.0f / 60.0f);
	CHECK(failures == 0);
	CHECK(enemy.sub == &parser.getFile().subs[1]);
}

TEST_CASE("TH06 boss life threshold switches to its callback subroutine",
          "[ecl][boss]") {
	shiki::ecl::ECLParser parser;
	auto &subs = parser.getFile().subs;
	subs.resize(2);
	subs[0].instructions.push_back(instruction(0, 113, {intParam(50)}));
	subs[0].instructions.push_back(instruction(0, 114, {intParam(1)}));
	subs[1].instructions.push_back(instruction(0, 111, {intParam(200)}));

	ECLEnemy enemy;
	enemy.eclParser = &parser;
	enemy.sub = &subs[0];
	enemy.hp = 40;
	enemy.update(3.0f / 60.0f);

	CHECK(enemy.sub == &subs[1]);
	CHECK(enemy.hp == 200);
}

TEST_CASE("Player bullets apply their TH06 damage value", "[ecl][player]") {
	struct PlayerBullet {
		shiki::Sprite sprite;
		int damage = 0;
		bool active = true;
	};

	std::vector<PlayerBullet> bullets(1);
	bullets[0].sprite.setPosition(100.0f, 120.0f);
	bullets[0].damage = 24;

	std::vector<ECLEnemy> enemies(1);
	enemies[0].x = 100.0f;
	enemies[0].y = 120.0f;
	enemies[0].hp = 100;
	int score = 0;

	checkPlayerBulletsVsECLEnemies(bullets, enemies, score);

	CHECK(enemies[0].hp == 76);
	CHECK_FALSE(bullets[0].active);
	CHECK(score == 0);
}

TEST_CASE("Reimu A zero-power shot travels vertically and kills one HP",
          "[th06][player][shot]") {
	const auto &shot = TH06_REIMU_A_ZERO_POWER_SHOT;
	const auto velocity = th06PlayerShotVelocity(shot.angle, shot.speed);
	CHECK(shot.offsetX == Catch::Approx(0.0F));
	CHECK(velocity.x == Catch::Approx(0.0F));
	CHECK(velocity.y == Catch::Approx(-720.0F));

	struct PlayerBullet {
		shiki::Sprite sprite;
		int damage{};
		bool active{true};
	};
	PlayerBullet bullet;
	bullet.sprite.setPosition(100.0F, 200.0F);
	bullet.damage = shot.damage;
	for (int frame = 0; frame < 5; ++frame) {
		auto position = bullet.sprite.getPosition();
		position.x += velocity.x / 60.0F;
		position.y += velocity.y / 60.0F;
		bullet.sprite.setPosition(position);
	}
	CHECK(bullet.sprite.getPosition().x == Catch::Approx(100.0F));

	std::vector<PlayerBullet> bullets{bullet};
	std::vector<ECLEnemy> enemies(1);
	enemies[0].x = 100.0F;
	enemies[0].y = 140.0F;
	enemies[0].hp = 1;
	int score = 0;
	checkPlayerBulletsVsECLEnemies(bullets, enemies, score);

	CHECK(enemies[0].hp <= 0);
	CHECK_FALSE(bullets[0].active);
}

TEST_CASE("Marisa B zero-power shot travels vertically and kills one HP",
          "[th06][player][shot]") {
	const auto &shot = TH06_MARISA_B_ZERO_POWER_SHOT;
	const auto velocity = th06PlayerShotVelocity(shot.angle, shot.speed);
	CHECK(shot.offsetX == Catch::Approx(0.0F));
	CHECK(velocity.x == Catch::Approx(0.0F));

	struct PlayerBullet {
		shiki::Sprite sprite;
		int damage{};
		bool active{true};
	};
	PlayerBullet bullet;
	bullet.sprite.setPosition(120.0F, 200.0F);
	bullet.damage = shot.damage;
	for (int frame = 0; frame < 5; ++frame) {
		auto position = bullet.sprite.getPosition();
		position.x += velocity.x / 60.0F;
		position.y += velocity.y / 60.0F;
		bullet.sprite.setPosition(position);
	}
	CHECK(bullet.sprite.getPosition().x == Catch::Approx(120.0F));

	std::vector<PlayerBullet> bullets{bullet};
	std::vector<ECLEnemy> enemies(1);
	enemies[0].x = 120.0F;
	enemies[0].y = 140.0F;
	enemies[0].hp = 1;
	int score = 0;
	checkPlayerBulletsVsECLEnemies(bullets, enemies, score);
	CHECK(enemies[0].hp <= 0);
	CHECK_FALSE(bullets[0].active);
}

TEST_CASE("Marisa B laser remains active and uses a vertical hit region",
          "[ecl][player][laser]") {
	struct PlayerBullet {
		shiki::Sprite sprite;
		int damage = 6;
		bool laser = true;
		bool damageReady = true;
		bool active = true;
	};

	std::vector<PlayerBullet> bullets(1);
	bullets[0].sprite.setPosition(100.0f, 200.0f);
	std::vector<ECLEnemy> enemies(2);
	enemies[0].x = 108.0f;
	enemies[0].y = 80.0f;
	enemies[0].hp = 200;
	enemies[1].x = 140.0f;
	enemies[1].y = 80.0f;
	enemies[1].hp = 200;
	int score = 0;

	checkPlayerBulletsVsECLEnemies(bullets, enemies, score);

	CHECK(bullets[0].active);
	CHECK_FALSE(bullets[0].damageReady);
	CHECK(enemies[0].hp == 194);
	CHECK(enemies[1].hp == 200);

	checkPlayerBulletsVsECLEnemies(bullets, enemies, score);
	CHECK(enemies[0].hp == 194);
}

TEST_CASE("TH06 title ANM executes the original main-menu interrupt",
          "[th06][anm][menu]") {
	auto file = std::make_shared<TH06MenuAnmFile>();
	REQUIRE(file->load(&th06Assets(), "title01"));
	TH06MenuAnmVm vm;
	REQUIRE(vm.initialize(file, 0));

	vm.interrupt(2);
	for (int frame = 0; frame < 61; ++frame)
		vm.tick();

	CHECK(vm.visible);
	CHECK(vm.sprite == 10);
	CHECK(vm.x == Catch::Approx(448.0f));
	CHECK(vm.y == Catch::Approx(200.0f));
}

TEST_CASE("TH06 character ANM accepts interrupt zero", "[th06][anm][menu]") {
	auto file = std::make_shared<TH06MenuAnmFile>();
	REQUIRE(file->load(&th06Assets(), "slpl01a"));
	TH06MenuAnmVm vm;
	REQUIRE(vm.initialize(file, 0));

	vm.interrupt(7);
	vm.tick();
	REQUIRE(vm.visible);

	vm.interrupt(0);
	vm.tick();
	CHECK(vm.pendingInterrupt == TH06MenuAnmVm::NO_PENDING_INTERRUPT);
	for (int frame = 0; frame < 120; ++frame)
		vm.tick();
}

TEST_CASE("TH06 press-start screen hides configuration menu VMs",
          "[th06][anm][menu]") {
	std::vector<TH06MenuAnmVm> vms;
	const auto append = [&](const char *name, int count) {
		auto file = std::make_shared<TH06MenuAnmFile>();
		REQUIRE(file->load(&th06Assets(), name));
		for (int script = 0; script < count; ++script) {
			TH06MenuAnmVm vm;
			REQUIRE(vm.initialize(file, script));
			vm.interrupt(1);
			vms.push_back(std::move(vm));
		}
	};
	append("title01", 27);
	append("title02", 4);
	append("title03", 3);
	append("title04", 46);
	for (int frame = 0; frame < 120; ++frame)
		for (auto &vm : vms)
			vm.tick();

	const auto visible = std::count_if(
	    vms.begin(), vms.end(), [](const auto &vm) { return vm.visible; });
	CHECK(visible == 7);
	for (const auto &vm : vms)
		if (vm.visible)
			CHECK((vm.file->atlas == "title02" || vm.file->atlas == "title03"));
}

TEST_CASE("TH06 character select keeps the unselected portrait hidden",
          "[th06][anm][menu]") {
	struct FileSpec {
		const char *name;
		int scripts;
	};
	static constexpr std::array<FileSpec, 9> FILES = {
	    FileSpec{"select01", 3}, FileSpec{"select02", 2},
	    FileSpec{"select05", 1}, FileSpec{"slpl00a", 1},
	    FileSpec{"slpl00b", 1},  FileSpec{"slpl01a", 1},
	    FileSpec{"slpl01b", 1},  FileSpec{"select03", 2},
	    FileSpec{"select04", 4}};
	std::vector<TH06MenuAnmVm> vms;
	for (const auto &spec : FILES) {
		auto file = std::make_shared<TH06MenuAnmFile>();
		REQUIRE(file->load(&th06Assets(), spec.name));
		for (int script = 0; script < spec.scripts; ++script) {
			TH06MenuAnmVm vm;
			REQUIRE(vm.initialize(file, script));
			vm.interrupt(18);
			vms.push_back(std::move(vm));
		}
	}
	for (int frame = 0; frame < 60; ++frame)
		for (auto &vm : vms)
			vm.tick();

	for (auto &vm : vms)
		vm.interrupt(7);
	vms[5].interrupt(8);
	vms[8].interrupt(0);
	vms[9].interrupt(0);
	for (int frame = 0; frame < 60; ++frame)
		for (auto &vm : vms)
			vm.tick();

	CHECK(vms[6].visible);
	CHECK(vms[7].visible);
	CHECK_FALSE(vms[8].visible);
	CHECK_FALSE(vms[9].visible);
}

TEST_CASE("TH06 player ANM resolves sparse Marisa shot sprites",
          "[th06][anm][player]") {
	auto file = std::make_shared<TH06MenuAnmFile>();
	REQUIRE(file->load(&th06Assets(), "player01"));

	TH06MenuAnmVm shot;
	REQUIRE(shot.initialize(file, 64));
	CHECK(shot.sprite == 20);
	CHECK(shot.scaleX == Catch::Approx(1.0f));
	CHECK((shot.color >> 24) == 0xc0);
	CHECK(shot.autoRotate);

	TH06MenuAnmVm laser;
	REQUIRE(laser.initialize(file, 71));
	CHECK(laser.sprite == 24);
	CHECK(laser.scaleX == Catch::Approx(2.0f));
	CHECK((laser.color & 0x00ffffff) == 0x00ff8080);
	CHECK((laser.color >> 24) == 0x08);
	for (int frame = 0; frame < 20; ++frame)
		laser.tick();
	CHECK((laser.color >> 24) == 0xa0);

	laser.visible = true;
	laser.interrupt(1);
	bool exited = false;
	for (int frame = 0; frame < 70; ++frame) {
		laser.tick();
		exited = exited || !laser.visible;
	}
	CHECK(exited);
}

TEST_CASE("TH06 player bombs preserve original layered ANM colors",
          "[th06][anm][player][bomb]") {
	auto marisa = std::make_shared<TH06MenuAnmFile>();
	REQUIRE(marisa->load(&th06Assets(), "player01"));
	static constexpr std::array<uint32_t, 4> MARISA_COLORS = {
	    0x00ff8080, 0x0080ff80, 0x008080ff, 0x0080ffff};
	for (int layer = 0; layer < 4; ++layer) {
		TH06MenuAnmVm vm;
		REQUIRE(vm.initialize(marisa, 8 + layer));
		CHECK((vm.color & 0x00ffffff) ==
		      MARISA_COLORS[static_cast<size_t>(layer)]);
		CHECK(vm.additive);
		CHECK(vm.scaleX == Catch::Approx(11.0f));
		CHECK(vm.scaleY == Catch::Approx(7.0f));
	}

	auto reimu = std::make_shared<TH06MenuAnmFile>();
	REQUIRE(reimu->load(&th06Assets(), "player00"));
	static constexpr std::array<uint32_t, 4> REIMU_COLORS = {
	    0x003030ff, 0x0030ffff, 0x0030ff30, 0x00ff3030};
	for (int layer = 0; layer < 4; ++layer) {
		TH06MenuAnmVm vm;
		REQUIRE(vm.initialize(reimu, 133 + layer));
		CHECK((vm.color & 0x00ffffff) ==
		      REIMU_COLORS[static_cast<size_t>(layer)]);
		CHECK(vm.additive);
		CHECK(vm.useOffset);
		CHECK((vm.color >> 24) == 0x80);
	}
}

TEST_CASE("TH06 player ANM provides both character option scripts",
          "[th06][anm][player]") {
	for (const char *character : {"player00", "player01"}) {
		auto file = std::make_shared<TH06MenuAnmFile>();
		REQUIRE(file->load(&th06Assets(), character));
		for (int script : {128, 129}) {
			TH06MenuAnmVm option;
			REQUIRE(option.initialize(file, script));
			CHECK(option.sprite >= 0);
		}
	}
}

TEST_CASE("TH06 Reimu A shot scripts expose their original render state",
          "[th06][anm][player]") {
	auto file = std::make_shared<TH06MenuAnmFile>();
	REQUIRE(file->load(&th06Assets(), "player00"));
	TH06MenuAnmVm mainShot;
	REQUIRE(mainShot.initialize(file, 64));
	CHECK(mainShot.sprite == 18);
	CHECK(mainShot.scaleX == Catch::Approx(1.5f));
	CHECK((mainShot.color >> 24) == 128);
	CHECK_FALSE(mainShot.autoRotate);

	TH06MenuAnmVm homingShot;
	REQUIRE(homingShot.initialize(file, 65));
	CHECK(homingShot.sprite == 19);
	CHECK(homingShot.scaleX == Catch::Approx(1.5f));
	CHECK((homingShot.color >> 24) == 96);
	CHECK_FALSE(homingShot.autoRotate);
}

TEST_CASE("TH06 spell damage is capped per frame and reduced",
          "[ecl][player][boss]") {
	struct PlayerBullet {
		shiki::Sprite sprite;
		int damage = 30;
		bool active = true;
	};

	std::vector<PlayerBullet> bullets(4);
	for (auto &bullet : bullets)
		bullet.sprite.setPosition(100.0f, 120.0f);
	std::vector<ECLEnemy> enemies(1);
	enemies[0].x = 100.0f;
	enemies[0].y = 120.0f;
	enemies[0].hp = 100;
	enemies[0].spellActive = true;
	int score = 0;

	checkPlayerBulletsVsECLEnemies(bullets, enemies, score);

	CHECK(enemies[0].hp == 90);
}

TEST_CASE("TH06 bomb damage uses the shared frame cap and spell reduction",
          "[ecl][player][bomb]") {
	struct PlayerBullet {
		shiki::Sprite sprite;
		int damage = 0;
		bool active = true;
	};

	std::vector<PlayerBullet> bullets;
	std::vector<ECLEnemy> enemies(1);
	enemies[0].hp = 1000;
	enemies[0].spellActive = true;
	enemies[0].pendingPlayerDamage = 400;
	enemies[0].pendingDamageFromBomb = true;
	int score = 0;

	checkPlayerBulletsVsECLEnemies(bullets, enemies, score);

	CHECK(enemies[0].hp == 977);
	CHECK(enemies[0].pendingPlayerDamage == 0);
	CHECK_FALSE(enemies[0].pendingDamageFromBomb);
}

TEST_CASE("TH06 ENEMYKILLALL reaches the enemy manager callback",
          "[ecl][enemy]") {
	shiki::ecl::ECLSubroutine sub;
	sub.instructions.push_back(instruction(0, 96, {}));
	ECLEnemy enemy;
	enemy.sub = &sub;
	bool killedAll = false;
	enemy.onKillAllEnemies = [&]() { killedAll = true; };

	enemy.update(1.0f / 60.0f);

	CHECK(killedAll);
}

TEST_CASE("TH06 timeline starts and waits for original dialogue",
          "[ecl][dialogue]") {
	shiki::ecl::ECLEngine engine;
	REQUIRE(loadEclStage(engine, 7));
	int messageId = -1;
	bool dialogueActive = true;
	engine.setDialogueStartCallback(
	    [&](int32_t requested) { messageId = requested; });
	engine.setDialogueActiveCallback([&]() { return dialogueActive; });
	engine.start();

	for (int frame = 0; frame < 4800; ++frame)
		engine.update(1.0f / 60.0f);

	CHECK(messageId >= 0);
	dialogueActive = false;
	for (int frame = 0; frame < 10; ++frame)
		engine.update(1.0f / 60.0f);
	CHECK(messageId >= 0);
}

TEST_CASE("TH06 bullet spawn states match original ANM timing",
          "[ecl][bullet][anm]") {
	const auto fast = getTH06BulletSpawnAnimation(0, 6, 0x2);
	CHECK(fast.state == TH06BulletSpawnState::Fast);
	CHECK(fast.spriteIndex == 137);
	CHECK(fast.durationFrames == 8);
	CHECK(fast.alphaFrames == 8);
	CHECK(fast.movementDivisor == Catch::Approx(2.0f));
	CHECK(fast.initialScale == Catch::Approx(2.4f));
	CHECK(fast.scalePerFrame == Catch::Approx(-1.4f / 8.0f));
	CHECK(fast.fadeOut);
	CHECK(fast.alphaAtAge(1) == Catch::Approx(0.875f));

	const auto nobodyOpening = getTH06BulletSpawnAnimation(3, 6, 0x202);
	CHECK(nobodyOpening.spriteIndex == 137);
	CHECK(nobodyOpening.durationFrames == 8);
	CHECK(nobodyOpening.alphaAtAge(1) == Catch::Approx(0.875f));

	const auto normal = getTH06BulletSpawnAnimation(3, 10, 0x4);
	CHECK(normal.state == TH06BulletSpawnState::Normal);
	CHECK(normal.spriteIndex == 143);
	CHECK(normal.durationFrames == 16);
	CHECK(normal.movementDivisor == Catch::Approx(2.5f));

	const auto slow = getTH06BulletSpawnAnimation(5, 14, 0x8);
	CHECK(slow.state == TH06BulletSpawnState::Slow);
	CHECK(slow.spriteIndex == 144);
	CHECK(slow.durationFrames == 32);
	CHECK(slow.movementDivisor == Catch::Approx(3.0f));
}

TEST_CASE("TH06 ANM scripts preserve original frames and jump loops",
          "[anm][player]") {
	const auto movingLeft = loadTH06AnmScript(&th06Assets(), "player00", 1);
	REQUIRE_FALSE(movingLeft.empty());
	CHECK(sampleTH06AnmScript(movingLeft, 0.0f) == 4);
	CHECK(sampleTH06AnmScript(movingLeft, 3.0f) == 5);
	CHECK(sampleTH06AnmScript(movingLeft, 14.0f) == 7);
	CHECK(sampleTH06AnmScript(movingLeft, 48.0f) == 7);

	const auto movingRight = loadTH06AnmScript(&th06Assets(), "player00", 3);
	REQUIRE_FALSE(movingRight.empty());
	const auto *rightFrame = sampleTH06AnmFrame(movingRight, 3.0f);
	REQUIRE(rightFrame != nullptr);
	CHECK(rightFrame->flipX);
}

TEST_CASE("TH06 boss frame sampler preserves global raw sprite IDs",
          "[anm][boss]") {
	const auto flandre = loadTH06AnmScript(&th06Assets(), "stg7enm2", 160);
	REQUIRE_FALSE(flandre.empty());
	REQUIRE_FALSE(flandre.frames.empty());
	CHECK(flandre.frames.front().sprite == 160);
}

TEST_CASE("TH06 ANM scripts without jumps hold their final sprite", "[anm]") {
	TH06AnmScript script;
	script.frames = {{12, 0, 4, 0, false}, {13, 4, 8, 8, false}};

	CHECK_FALSE(script.looping);
	CHECK(sampleTH06AnmScript(script, 120.0f) == 13);
}

TEST_CASE("TH06 stage ANM preserves three-axis rotation and UV scrolling",
          "[anm][stage]") {
	auto stage4 = std::make_shared<TH06MenuAnmFile>();
	REQUIRE(stage4->load(th06Resources().getAssetStore(), "stg4bg"));
	TH06MenuAnmVm floor;
	REQUIRE(floor.initialize(stage4, 2));
	CHECK(floor.rotationX == Catch::Approx(-std::numbers::pi_v<float> / 2.0f));
	CHECK(floor.rotationY == Catch::Approx(0.0f));
	CHECK(floor.rotation == Catch::Approx(0.0f));

	auto stage2 = std::make_shared<TH06MenuAnmFile>();
	REQUIRE(stage2->load(th06Resources().getAssetStore(), "stg2bg"));
	TH06MenuAnmVm mist;
	REQUIRE(mist.initialize(stage2, 0));
	const float initialOffset = mist.uvOffsetX;
	CHECK(initialOffset != Catch::Approx(0.0f));
	mist.tick();
	CHECK(mist.uvOffsetX != Catch::Approx(initialOffset));
}

TEST_CASE("TH06 ordinary bullet collision sizes come from bullet templates",
          "[bullet][collision]") {
	CHECK(TH06_PLAYER_HITBOX_HALF_SIZE == Catch::Approx(1.25f));
	CHECK(getTH06BulletCollisionSize(0).x == Catch::Approx(4.0f));
	CHECK(getTH06BulletCollisionSize(3).x == Catch::Approx(6.0f));
	CHECK(getTH06BulletCollisionSize(4).x == Catch::Approx(5.0f));
	CHECK(getTH06BulletCollisionSize(7).x == Catch::Approx(11.0f));
	CHECK(getTH06BulletCollisionSize(8).x == Catch::Approx(9.0f));
	CHECK(getTH06BulletCollisionSize(9).x == Catch::Approx(32.0f));
}

TEST_CASE("TH06 ANMSETPOSES selects moving and stopping direction scripts",
          "[ecl][enemy][anm]") {
	shiki::ecl::ECLSubroutine sub;
	sub.instructions.push_back(instruction(
	    0, 98,
	    {intParam(0), intParam(6), intParam(7), intParam(4), intParam(5)}));
	ECLEnemy enemy;
	enemy.sub = &sub;
	enemy.vx = -1.0f;

	enemy.update(1.0f / 60.0f);
	CHECK(enemy.animationScript == 4);

	enemy.vx = 0.0f;
	enemy.update(1.0f / 60.0f);
	CHECK(enemy.animationScript == 6);
}

TEST_CASE("TH06 huge and bubble bullets use their dedicated spawn scripts",
          "[ecl][bullet][anm]") {
	const auto huge = getTH06BulletSpawnAnimation(8, 6, 0x2);
	CHECK(huge.spriteIndex == 144);
	CHECK(huge.durationFrames == 32);
	CHECK(huge.initialScale == Catch::Approx(6.0f));
	CHECK(huge.scalePerFrame == Catch::Approx(-0.1f));

	const auto bubble = getTH06BulletSpawnAnimation(9, 3, 0x8);
	CHECK(std::string(bubble.atlas) == "etama4");
	CHECK(bubble.spriteIndex == 3);
	CHECK(bubble.durationFrames == 24);
	CHECK(bubble.alphaFrames == 32);
	CHECK(bubble.targetAlpha == Catch::Approx(1.0f));

	const auto immediate = getTH06BulletSpawnAnimation(3, 0, 0);
	CHECK_FALSE(immediate.active());
	CHECK(immediate.durationFrames == 0);
}

TEST_CASE("TH06 Extra point item value follows original collection height",
          "[item][score]") {
	CHECK(getTH06PointItemScore(4, 127.9f) == 300000);
	CHECK(getTH06PointItemScore(4, 128.0f) == 200000);
	CHECK(getTH06PointItemScore(4, 228.9f) == 160000);
}

TEST_CASE("TH06 enemy death preserves timeline score and bomb-attracted drop",
          "[ecl][enemy][item]") {
	struct PlayerBullet {
		shiki::Sprite sprite;
		int damage = 0;
		bool active = true;
	};

	std::vector<PlayerBullet> bullets;
	std::vector<ECLEnemy> enemies(1);
	enemies[0].hp = 40;
	enemies[0].scoreValue = 2000;
	enemies[0].itemDrop = TH06_ITEM_POINT;
	enemies[0].pendingPlayerDamage = 70;
	enemies[0].pendingDamageFromBomb = true;
	int droppedType = TH06_ITEM_NONE;
	int droppedState = -1;
	std::vector<std::pair<int, int>> spawnedEffects;
	enemies[0].onDropItem = [&](int type, float, float, int state) {
		droppedType = type;
		droppedState = state;
	};
	enemies[0].onSpawnEffect = [&](int id, float, float, int count, uint32_t) {
		spawnedEffects.emplace_back(id, count);
	};
	int score = 0;

	checkPlayerBulletsVsECLEnemies(bullets, enemies, score);

	CHECK_FALSE(enemies[0].alive);
	CHECK(score == 2000);
	CHECK(droppedType == TH06_ITEM_POINT);
	CHECK(droppedState == 1);
	REQUIRE(spawnedEffects.size() == 3);
	CHECK(spawnedEffects[0] == std::pair{3, 1});
	CHECK(spawnedEffects[1] == std::pair{0, 1});
	CHECK(spawnedEffects[2] == std::pair{4, 4});
}

TEST_CASE("TH06 ANM death opcode unpacks the three effect indices",
          "[ecl][enemy][effect]") {
	shiki::ecl::ECLSubroutine sub;
	sub.instructions.push_back(instruction(0, 100, {intParam(0x00030201)}));
	ECLEnemy enemy;
	enemy.sub = &sub;

	enemy.update(1.0f / 60.0f);

	CHECK(enemy.deathAnm1 == 1);
	CHECK(enemy.deathAnm2 == 2);
	CHECK(enemy.deathAnm3 == 3);
	CHECK(enemy.scoreValue == 100);
}

TEST_CASE("TH06 stage presentation resources follow original stage slots",
          "[th06][stage][presentation]") {
	CHECK(th06StageMusicId(1, 0) == "th06_02");
	CHECK(th06StageMusicId(1, 1) == "th06_03");
	CHECK(th06StageMusicId(2, 0) == "th06_04");
	CHECK(th06StageMusicId(6, 1) == "th06_13");
	CHECK(th06StageMusicId(7, 0) == "th06_14");

	CHECK(std::string(resolveTH06StageFaceSprite(1, 0).atlas) == "face03a");
	CHECK(std::string(resolveTH06StageFaceSprite(1, 3).atlas) == "face03b");
	CHECK(resolveTH06StageFaceSprite(1, 3).localSprite == 1);
	CHECK(std::string(resolveTH06StageFaceSprite(2, 1).atlas) == "face05a");
	CHECK(std::string(resolveTH06StageFaceSprite(3, 2).atlas) == "face06b");
	CHECK(std::string(resolveTH06StageFaceSprite(4, 2).atlas) == "face08b");
	CHECK(std::string(resolveTH06StageFaceSprite(5, 2).atlas) == "face09b");
	CHECK(std::string(resolveTH06StageFaceSprite(6, 2).atlas) == "face10a");
	CHECK(std::string(resolveTH06StageFaceSprite(6, 3).atlas) == "face10b");
	CHECK(std::string(resolveTH06StageFaceSprite(7, 2).atlas) == "face12a");
	CHECK(std::string(resolveTH06StageFaceSprite(7, 3).atlas) == "face12b");

	CHECK(std::string(th06StageEffectAtlas(1, false)) == "eff01");
	CHECK(std::string(th06StageEffectAtlas(2, false)) == "eff02");
	CHECK(std::string(th06StageEffectAtlas(3, false)) == "eff03");
	CHECK(std::string(th06StageEffectAtlas(4, false)) == "eff04");
	CHECK(std::string(th06StageEffectAtlas(5, false)) == "eff05");
	CHECK(std::string(th06StageEffectAtlas(6, false)) == "eff05");
	CHECK(std::string(th06StageEffectAtlas(6, true)) == "eff06");
	CHECK(std::string(th06StageEffectAtlas(7, false)) == "eff04");
	CHECK(std::string(th06StageEffectAtlas(7, true)) == "eff07");
}

TEST_CASE("TH06 ECL particle opcode preserves count and 32-bit color",
          "[ecl][enemy][effect]") {
	shiki::ecl::ECLSubroutine sub;
	sub.instructions.push_back(instruction(
	    0, 118, {intParam(6), uintParam(8), uintParam(0xff20a0e0)}));
	ECLEnemy enemy;
	enemy.sub = &sub;
	int effectId = -1;
	int count = 0;
	uint32_t color = 0;
	enemy.onSpawnEffect = [&](int id, float, float, int spawnedCount,
	                          uint32_t spawnedColor) {
		effectId = id;
		count = spawnedCount;
		color = spawnedColor;
	};

	enemy.update(1.0f / 60.0f);

	CHECK(effectId == 6);
	CHECK(count == 8);
	CHECK(color == 0xff20a0e0);
}

TEST_CASE("TH06 effect manager follows original effect lifetime and motion",
          "[effect]") {
	TH06EffectManager effects;
	effects.setResourceManager(&th06Resources());
	effects.spawn(3, 100.0f, 120.0f, 2);

	REQUIRE(effects.effects().size() == 2);
	CHECK(effects.effects()[0].duration == 40);
	const float startX = effects.effects()[0].x;
	effects.updateTick();
	CHECK(effects.effects()[0].x != Catch::Approx(startX));
	for (int frame = 1; frame < 40; ++frame)
		effects.updateTick();
	CHECK(effects.effects().empty());
}

TEST_CASE("TH06 effect manager applies supplied steam particle motion",
          "[effect]") {
	TH06EffectManager effects;
	effects.setResourceManager(&th06Resources());
	effects.spawnMoving(19, 100.0f, 120.0f, 0xff3030ff, 2.0f, -1.0f, -0.5f,
	                    0.25f);

	REQUIRE(effects.effects().size() == 1);
	effects.updateTick();
	CHECK(effects.effects()[0].x == Catch::Approx(102.0f));
	CHECK(effects.effects()[0].y == Catch::Approx(119.0f));
	CHECK(effects.effects()[0].vx == Catch::Approx(1.5f));
	CHECK(effects.effects()[0].vy == Catch::Approx(-0.75f));
}

TEST_CASE("TH06 ANM random sprite opcode stays within its source frame group",
          "[anm][effect]") {
	auto file = std::make_shared<TH06MenuAnmFile>();
	REQUIRE(file->load(&th06Assets(), "etama4"));

	for (int sample = 0; sample < 32; ++sample) {
		TH06MenuAnmVm vm;
		REQUIRE(vm.initialize(file, 9));
		CHECK(vm.sprite >= 7);
		CHECK(vm.sprite <= 10);
	}
}

TEST_CASE("TH06 spell effect opcode creates a boss-following orbit",
          "[ecl][enemy][effect]") {
	shiki::ecl::ECLSubroutine sub;
	sub.instructions.push_back(
	    instruction(0, 102,
	                {intParam(8), floatParam(1.0f), floatParam(2.0f),
	                 floatParam(3.0f), floatParam(24.0f)}));
	ECLEnemy enemy;
	enemy.sub = &sub;
	enemy.resourceManager = &th06Resources();

	enemy.update(1.0f / 60.0f);

	REQUIRE(enemy.spellEffectCount == 1);
	CHECK(enemy.spellEffects[0].colorId == 8);
	CHECK(enemy.spellEffects[0].axisX == Catch::Approx(1.0f));
	CHECK(enemy.spellEffects[0].axisY == Catch::Approx(2.0f));
	CHECK(enemy.spellEffects[0].axisZ == Catch::Approx(3.0f));
	CHECK(enemy.spellEffects[0].targetDistance == Catch::Approx(24.0f));
	CHECK(enemy.spellEffects[0].distance == Catch::Approx(0.3f));
	CHECK(enemy.spellEffects[0].ageFrames == 1);
}

TEST_CASE("TH06 spell effects use the default non-color-composition palette",
          "[ecl][enemy][effect]") {
	CHECK(th06SpellEffectColor(0) == 0xfff0f0f0);
	CHECK(th06SpellEffectColor(8) == 0xffffe0ff);
	CHECK(th06SpellEffectColor(11) == 0xffe0e0ff);
	CHECK(th06SpellEffectColor(14) == 0xffe0ffff);
	CHECK(th06SpellEffectColor(27) == 0xffffffff);
}

TEST_CASE("TH06 additive effect compatibility keeps hue near white",
          "[effect][render]") {
	CHECK(th06AdditiveEffectDisplayColor(0xffffffff) == 0xffffffff);
	CHECK(th06AdditiveEffectDisplayColor(0xff3030ff) == 0xfff3f3ff);
	CHECK(th06AdditiveEffectDisplayColor(0xffe0e0ff) == 0xfffdfdff);
	CHECK(th06AdditiveEffectDisplayColor(0x804040ff) == 0x80f4f4ff);
}

TEST_CASE("TH06 ANM slot opcode starts Flandre's auxiliary wing animation",
          "[ecl][enemy][anm]") {
	shiki::ecl::ECLSubroutine sub;
	sub.instructions.push_back(
	    instruction(0, 99, {intParam(0), intParam(165)}));
	ECLEnemy enemy;
	enemy.sub = &sub;
	enemy.resourceManager = &th06Resources();

	enemy.update(1.0f / 60.0f);

	REQUIRE(enemy.auxiliaryAnimations[0].active);
	CHECK(enemy.auxiliaryAnimations[0].scriptId == 165);
	REQUIRE(enemy.auxiliaryAnimations[0].vm.file);
	CHECK(enemy.auxiliaryAnimations[0].vm.sprite == 8);
	for (int frame = 0; frame < 12; ++frame)
		enemy.update(1.0f / 60.0f);
	CHECK(enemy.auxiliaryAnimations[0].vm.sprite == 11);
}

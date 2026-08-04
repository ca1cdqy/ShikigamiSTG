#include <catch2/catch_test_macros.hpp>

#include <shiki/game_definition.h>
#include <shiki/session.h>
#include <shiki/stg/gameplay_context.h>
#include <shiki/stg/item/item.h>
#include <shiki/stg/score/score.h>

TEST_CASE("Items and scores live in deterministic World components",
          "[stg][item][score]") {
	using namespace shiki;
	using namespace shiki::stg;

	GameDefinition definition;
	auto items = ItemComponents::registerWith(definition);
	auto scores = ScoreComponents::registerWith(definition);
	REQUIRE(items);
	REQUIRE(scores);
	REQUIRE(installItemSystems(definition, *items));

	game::EntityHandle scoreEntity;
	REQUIRE(definition.addSystem(
	    {.name = "test.spawn.item_and_score.v1",
	     .phase = game::SystemPhase::Flow},
	    [items = *items, scores = *scores,
	     &scoreEntity](GameplayContext &game) {
		    if (game.time().tick.value != 1)
			    return;
		    ItemApi itemApi(game.commands(), items);
		    static_cast<void>(
		        itemApi.spawn(ItemSpec{.kind = {7},
		                               .style = {3},
		                               .value = 250,
		                               .collisionRadius = 5.0F,
		                               .lifetime = {10}},
		                      ItemSpawn{.position = {{10.0F, 20.0F}},
		                                .velocityPerTick = {2.0F, -1.0F}}));
		    auto entity = game.commands().spawn();
		    if (entity) {
			    scoreEntity = *entity;
			    static_cast<void>(game.commands().set(
			        *entity, scores.score,
			        Score{.points = 100, .highScore = 100}));
		    }
	    }));
	REQUIRE(definition.addSystem(
	    {.name = "test.update.score.v1",
	     .phase = game::SystemPhase::Simulation},
	    [scores = *scores, &scoreEntity](GameplayContext &game) {
		    ScoreApi scoreApi(game.world(), game.commands(), scores);
		    static_cast<void>(scoreApi.add(
		        scoreEntity, ScoreDelta{.points = 50, .graze = 1}));
	    }));

	auto session = Session::create(std::move(definition), {});
	REQUIRE(session);
	REQUIRE((*session)->step(control::InputFrame{.tick = {1}}));

	const Score *score =
	    (*session)->worldView().tryGet(scoreEntity, scores->score);
	REQUIRE(score != nullptr);
	CHECK(score->points == 150);
	CHECK(score->highScore == 150);
	CHECK(score->graze == 1);

	auto itemQuery = (*session)->worldView().query(
	    items->identity, items->transform, items->lifetime, items->value);
	REQUIRE(itemQuery.size() == 1);
	const auto item = *itemQuery.begin();
	CHECK(item.template get<ItemIdentity>().kind.value == 7);
	CHECK(item.template get<ItemValue>().amount == 250);
	CHECK(item.template get<Transform>().position.value.x == 12.0F);
	CHECK(item.template get<Transform>().position.value.y == 19.0F);
	CHECK(item.template get<ItemLifetime>().remaining.value == 9);
}

TEST_CASE("Score deltas are policy-neutral value transformations",
          "[stg][score]") {
	const shiki::stg::Score updated = shiki::stg::applyScoreDelta(
	    {.points = 90, .highScore = 100, .graze = 2, .pointItems = 3},
	    {.points = 20, .graze = 1, .pointItems = 2});
	CHECK(updated.points == 110);
	CHECK(updated.highScore == 110);
	CHECK(updated.graze == 3);
	CHECK(updated.pointItems == 5);
}

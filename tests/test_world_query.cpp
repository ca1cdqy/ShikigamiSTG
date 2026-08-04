#include <catch2/catch_test_macros.hpp>

#include <shiki/game/world.h>

#include <vector>

namespace {

struct Position final {
	int x{};
	int y{};
};

struct Velocity final {
	int x{};
	int y{};
};

constexpr shiki::game::CommandSource queryFlowSource{
    .producer = {20}, .system = 20, .partition = 0, .buffer = 0};

} // namespace

TEST_CASE("Typed queries return active matching entities in storage order",
          "[world][query]") {
	using namespace shiki::game;

	World world;
	const auto position =
	    world.registerComponent<Position>({"game.position.v1"});
	const auto velocity =
	    world.registerComponent<Velocity>({"game.velocity.v1"});
	REQUIRE(position);
	REQUIRE(velocity);
	REQUIRE(world.beginTick());
	auto commands = world.commands(CommitPhase::Flow, queryFlowSource);
	REQUIRE(commands);

	const auto first = commands->spawn();
	const auto second = commands->spawn();
	const auto third = commands->spawn();
	REQUIRE(first);
	REQUIRE(second);
	REQUIRE(third);
	REQUIRE(commands->set(*first, *position, {1, 2}) ==
	        CommandStatus::Accepted);
	REQUIRE(commands->set(*second, *position, {3, 4}) ==
	        CommandStatus::Accepted);
	REQUIRE(commands->set(*second, *velocity, {5, 6}) ==
	        CommandStatus::Accepted);
	REQUIRE(commands->set(*third, *position, {7, 8}) ==
	        CommandStatus::Accepted);
	REQUIRE(world.commit(CommitPhase::Flow));

	const auto positioned = world.view().query(*position);
	REQUIRE(positioned.isValid());
	REQUIRE(positioned.size() == 3);
	std::vector<EntityHandle> order;
	for (const auto row : positioned) {
		order.push_back(row.entity());
	}
	REQUIRE(order.size() == 3);
	CHECK(std::ranges::find(order, *first) != order.end());
	CHECK(std::ranges::find(order, *second) != order.end());
	CHECK(std::ranges::find(order, *third) != order.end());

	const auto byEntity = world.view().query(QueryOrder::EntityId, *position);
	auto canonical = byEntity.begin();
	CHECK((*canonical).entity() == *first);
	++canonical;
	CHECK((*canonical).entity() == *second);
	++canonical;
	CHECK((*canonical).entity() == *third);

	const auto moving = world.view().query(*position, *velocity);
	REQUIRE(moving.isValid());
	REQUIRE(moving.size() == 1);
	const auto row = *moving.begin();
	CHECK(row.entity() == *second);
	CHECK(row.get<Position>().x == 3);
	CHECK(row.get<Velocity>().y == 6);
}

TEST_CASE("Queries expire at structural commit boundaries", "[world][query]") {
	using namespace shiki::game;

	World world;
	const auto position =
	    world.registerComponent<Position>({"game.position.v1"});
	REQUIRE(position);
	REQUIRE(world.beginTick());
	auto commands = world.commands(CommitPhase::Flow, queryFlowSource);
	REQUIRE(commands);
	const auto entity = commands->spawn();
	REQUIRE(entity);
	REQUIRE(commands->set(*entity, *position, {1, 1}) ==
	        CommandStatus::Accepted);
	REQUIRE(world.commit(CommitPhase::Flow));

	const auto query = world.view().query(*position);
	REQUIRE(query.isValid());
	REQUIRE(world.commit(CommitPhase::Simulation));
	CHECK_FALSE(query.isValid());
}

#include <catch2/catch_test_macros.hpp>

#include <shiki/game/world.h>

namespace {

struct Position final {
	int x{};
	int y{};
};

struct Velocity final {
	int x{};
	int y{};
};

constexpr shiki::game::CommandSource componentFlowSource{
    .producer = {10}, .system = 10, .partition = 0, .buffer = 0};
constexpr shiki::game::CommandSource componentSimulationSource{
    .producer = {11}, .system = 11, .partition = 0, .buffer = 0};
constexpr shiki::game::CommandSource componentResolutionSource{
    .producer = {12}, .system = 12, .partition = 0, .buffer = 0};

} // namespace

TEST_CASE("Components activate with their entity at Flow commit",
          "[world][component]") {
	using namespace shiki::game;

	World world;
	const auto position =
	    world.registerComponent<Position>({"game.position.v1"});
	REQUIRE(position);
	REQUIRE(world.beginTick());
	auto commands = world.commands(CommitPhase::Flow, componentFlowSource);
	REQUIRE(commands);
	const auto entity = commands->spawn();
	REQUIRE(entity);
	CHECK(commands->set(*entity, *position, {12, 34}) ==
	      CommandStatus::Accepted);
	CHECK(world.view().tryGet(*entity, *position) == nullptr);

	REQUIRE(world.commit(CommitPhase::Flow));
	const auto *value = world.view().tryGet(*entity, *position);
	REQUIRE(value != nullptr);
	CHECK(value->x == 12);
	CHECK(value->y == 34);
}

TEST_CASE("Component removal and entity destruction clear sparse storage",
          "[world][component]") {
	using namespace shiki::game;

	World world;
	const auto position =
	    world.registerComponent<Position>({"game.position.v1"});
	const auto velocity =
	    world.registerComponent<Velocity>({"game.velocity.v1"});
	REQUIRE(position);
	REQUIRE(velocity);
	REQUIRE(world.beginTick());
	auto flow = world.commands(CommitPhase::Flow, componentFlowSource);
	REQUIRE(flow);
	const auto entity = flow->spawn();
	REQUIRE(entity);
	REQUIRE(flow->set(*entity, *position, {1, 2}) == CommandStatus::Accepted);
	REQUIRE(flow->set(*entity, *velocity, {3, 4}) == CommandStatus::Accepted);
	REQUIRE(world.commit(CommitPhase::Flow));

	auto simulation =
	    world.commands(CommitPhase::Simulation, componentSimulationSource);
	REQUIRE(simulation);
	REQUIRE(simulation->remove(*entity, *position) == CommandStatus::Accepted);
	REQUIRE(world.commit(CommitPhase::Simulation));
	CHECK_FALSE(world.view().contains(*entity, *position));
	CHECK(world.view().contains(*entity, *velocity));

	auto resolution =
	    world.commands(CommitPhase::Resolution, componentResolutionSource);
	REQUIRE(resolution);
	REQUIRE(resolution->destroy(*entity) == CommandStatus::Accepted);
	REQUIRE(world.commit(CommitPhase::Resolution));
	CHECK(world.view().state(*entity) == EntityState::Dead);
	CHECK(world.view().tryGet(*entity, *velocity) == nullptr);
}

TEST_CASE("Resolution component data remains invisible until next tick",
          "[world][component]") {
	using namespace shiki::game;

	World world;
	const auto position =
	    world.registerComponent<Position>({"game.position.v1"});
	REQUIRE(position);
	REQUIRE(world.beginTick());
	REQUIRE(world.commit(CommitPhase::Flow));
	REQUIRE(world.commit(CommitPhase::Simulation));
	auto resolution =
	    world.commands(CommitPhase::Resolution, componentResolutionSource);
	REQUIRE(resolution);
	const auto entity = resolution->spawn();
	REQUIRE(entity);
	REQUIRE(resolution->set(*entity, *position, {99, 100}) ==
	        CommandStatus::Accepted);
	REQUIRE(world.commit(CommitPhase::Resolution));
	CHECK(world.view().tryGet(*entity, *position) == nullptr);

	REQUIRE(world.beginTick());
	const auto *value = world.view().tryGet(*entity, *position);
	REQUIRE(value != nullptr);
	CHECK(value->x == 99);
	CHECK(value->y == 100);
}

TEST_CASE("Component registration is stable and closes before simulation",
          "[world][component]") {
	using namespace shiki::game;

	World world;
	const auto first = world.registerComponent<Position>({"game.position.v1"});
	const auto repeated =
	    world.registerComponent<Position>({"game.position.v1"});
	REQUIRE(first);
	REQUIRE(repeated);
	CHECK(first->key() == repeated->key());

	const auto conflict =
	    world.registerComponent<Velocity>({"game.position.v1"});
	REQUIRE_FALSE(conflict);
	CHECK(conflict.error().code ==
	      static_cast<std::uint32_t>(WorldError::ComponentConflict));

	REQUIRE(world.beginTick());
	const auto late = world.registerComponent<Velocity>({"game.velocity.v1"});
	REQUIRE_FALSE(late);
	CHECK(late.error().code ==
	      static_cast<std::uint32_t>(WorldError::ComponentsLocked));
}

TEST_CASE("Presentation snapshots materialize multi-component queries",
          "[world][component][presentation]") {
	using namespace shiki::game;

	World world;
	constexpr ComponentFlags observable =
	    ComponentFlags::Observable | ComponentFlags::Deterministic;
	const auto position = world.registerComponent<Position>(
	    {.name = "game.position.v1", .flags = observable});
	const auto velocity = world.registerComponent<Velocity>(
	    {.name = "game.velocity.v1", .flags = observable});
	REQUIRE(position);
	REQUIRE(velocity);
	REQUIRE(world.beginTick());
	auto commands = world.commands(CommitPhase::Flow, componentFlowSource);
	REQUIRE(commands);
	const auto entity = commands->spawn();
	REQUIRE(entity);
	REQUIRE(commands->set(*entity, *position, {12, 34}) ==
	        CommandStatus::Accepted);
	REQUIRE(commands->set(*entity, *velocity, {-5, 8}) ==
	        CommandStatus::Accepted);
	REQUIRE(world.commit(CommitPhase::Flow));

	const auto snapshot = world.buildPresentationSnapshot();
	const auto rows =
	    snapshot.query(QueryOrder::EntityId, *position, *velocity);
	REQUIRE(rows.size() == 1);
	const auto &entry = *rows.begin();
	CHECK(entry.entity() == *entity);
	CHECK(entry.get<Position>().x == 12);
	CHECK(entry.get<Position>().y == 34);
	CHECK(entry.get<Velocity>().x == -5);
	CHECK(entry.get<Velocity>().y == 8);
}

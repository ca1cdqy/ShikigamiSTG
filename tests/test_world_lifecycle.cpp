#include <catch2/catch_test_macros.hpp>

#include <shiki/game/world.h>

namespace {

constexpr shiki::game::CommandSource flowSource{
    .producer = {1}, .system = 2, .partition = 0, .buffer = 0};
constexpr shiki::game::CommandSource simulationSource{
    .producer = {2}, .system = 3, .partition = 0, .buffer = 0};
constexpr shiki::game::CommandSource resolutionSource{
    .producer = {3}, .system = 4, .partition = 0, .buffer = 0};

} // namespace

TEST_CASE("Flow spawns activate at the Flow commit", "[world]") {
	using namespace shiki::game;

	World world;
	REQUIRE(world.beginTick());
	auto commands = world.commands(CommitPhase::Flow, flowSource);
	REQUIRE(commands);
	const auto entity = commands->spawn();
	REQUIRE(entity);
	CHECK(world.view().state(*entity) == EntityState::Pending);
	CHECK(world.view().pendingCount() == 1);

	REQUIRE(world.commit(CommitPhase::Flow));
	CHECK(world.view().state(*entity) == EntityState::Alive);
	CHECK(world.view().aliveCount() == 1);
}

TEST_CASE("Simulation spawns activate without replaying an earlier phase",
          "[world]") {
	using namespace shiki::game;

	World world;
	REQUIRE(world.beginTick());
	REQUIRE(world.commit(CommitPhase::Flow));
	auto commands = world.commands(CommitPhase::Simulation, simulationSource);
	REQUIRE(commands);
	const auto entity = commands->spawn();
	REQUIRE(entity);
	CHECK(world.view().state(*entity) == EntityState::Pending);

	REQUIRE(world.commit(CommitPhase::Simulation));
	CHECK(world.view().state(*entity) == EntityState::Alive);
}

TEST_CASE("Resolution spawns activate at the next BeginTick", "[world]") {
	using namespace shiki::game;

	World world;
	REQUIRE(world.beginTick());
	REQUIRE(world.commit(CommitPhase::Flow));
	REQUIRE(world.commit(CommitPhase::Simulation));
	auto commands = world.commands(CommitPhase::Resolution, resolutionSource);
	REQUIRE(commands);
	const auto entity = commands->spawn();
	REQUIRE(entity);

	REQUIRE(world.commit(CommitPhase::Resolution));
	CHECK(world.view().state(*entity) == EntityState::Pending);
	CHECK(world.view().aliveCount() == 0);
	CHECK(world.view().pendingCount() == 1);

	REQUIRE(world.beginTick());
	CHECK(world.view().state(*entity) == EntityState::Alive);
	CHECK(world.view().aliveCount() == 1);
}

TEST_CASE("Destroy remains deferred until its phase commit", "[world]") {
	using namespace shiki::game;

	World world;
	REQUIRE(world.beginTick());
	auto flow = world.commands(CommitPhase::Flow, flowSource);
	REQUIRE(flow);
	const auto entity = flow->spawn();
	REQUIRE(entity);
	REQUIRE(world.commit(CommitPhase::Flow));

	auto simulation = world.commands(CommitPhase::Simulation, simulationSource);
	REQUIRE(simulation);
	CHECK(simulation->destroy(*entity) == CommandStatus::Accepted);
	CHECK(world.view().state(*entity) == EntityState::Alive);
	REQUIRE(world.commit(CommitPhase::Simulation));
	CHECK(world.view().state(*entity) == EntityState::Dead);
}

TEST_CASE("Destroy wins over spawn at the same commit", "[world]") {
	using namespace shiki::game;

	World world;
	REQUIRE(world.beginTick());
	auto commands = world.commands(CommitPhase::Flow, flowSource);
	REQUIRE(commands);
	const auto entity = commands->spawn();
	REQUIRE(entity);
	CHECK(commands->destroy(*entity) == CommandStatus::Accepted);

	REQUIRE(world.commit(CommitPhase::Flow));
	CHECK(world.view().state(*entity) == EntityState::Dead);
	CHECK(world.view().aliveCount() == 0);
	CHECK(world.view().pendingCount() == 0);
}

TEST_CASE("World rejects foreign handles and invalid phase order", "[world]") {
	using namespace shiki::game;

	World first;
	World second;
	REQUIRE(first.beginTick());
	REQUIRE(second.beginTick());
	auto firstCommands = first.commands(CommitPhase::Flow, flowSource);
	auto secondCommands = second.commands(CommitPhase::Flow, flowSource);
	REQUIRE(firstCommands);
	REQUIRE(secondCommands);
	const auto entity = firstCommands->spawn();
	REQUIRE(entity);

	CHECK(second.view().state(*entity) == EntityState::Foreign);
	CHECK(secondCommands->destroy(*entity) == CommandStatus::ForeignWorld);
	REQUIRE_FALSE(first.commit(CommitPhase::Simulation));
	REQUIRE(first.commit(CommitPhase::Flow));
	CHECK(firstCommands->destroy(*entity) == CommandStatus::Expired);
}

TEST_CASE("Command sources and producers have stable ownership", "[world]") {
	using namespace shiki::game;

	World world;
	REQUIRE(world.beginTick());
	REQUIRE(world.commands(CommitPhase::Flow, flowSource));
	REQUIRE_FALSE(world.commands(CommitPhase::Flow, flowSource));

	constexpr CommandSource conflicting{
	    .producer = {1}, .system = 99, .partition = 0, .buffer = 0};
	REQUIRE_FALSE(world.commands(CommitPhase::Flow, conflicting));
}

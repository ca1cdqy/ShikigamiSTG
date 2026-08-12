// Engine micro-benchmarks for ShikigamiSTG.
//
// Build and run in release mode for meaningful numbers:
//
//   xmake f -m release
//   xmake build bench
//   xmake run bench
//
// The harness is dependency-free: every case runs a warmup pass followed by
// several timed passes and reports the median nanoseconds.

#include <shiki/game/component.h>
#include <shiki/game/system.h>
#include <shiki/game/world.h>
#include <shiki/game_definition.h>
#include <shiki/session.h>
#include <shiki/stg/gameplay_context.h>
#include <shiki/stg/projectile/projectile.h>
#include <shiki/stg/projectile/projectile_api.h>
#include <shiki/stg/projectile/projectile_systems.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

namespace {

using namespace shiki;
using namespace shiki::game;
using namespace shiki::stg;

constexpr CommandSource benchFlowSource{
    .producer = {90}, .system = 90, .partition = 0, .buffer = 0};
constexpr CommandSource benchSimulationSource{
    .producer = {91}, .system = 91, .partition = 0, .buffer = 0};

struct BenchPosition final {
	float x{};
	float y{};
};

struct BenchVelocity final {
	float x{};
	float y{};
};

[[noreturn]] void fail(const char *what) {
	std::fprintf(stderr, "benchmark failed: %s\n", what);
	std::exit(1);
}

template <typename Fn> double measureOnce(Fn &&fn) {
	const auto start = std::chrono::steady_clock::now();
	fn();
	const auto end = std::chrono::steady_clock::now();
	return std::chrono::duration<double, std::nano>(end - start).count();
}

template <typename Fn> double medianNs(Fn &&fn, std::size_t passes) {
	std::vector<double> samples;
	samples.reserve(passes);
	fn(); // Warmup pass.
	for (std::size_t pass = 0; pass < passes; ++pass)
		samples.push_back(measureOnce(fn));
	const auto middle =
	    samples.begin() + static_cast<std::ptrdiff_t>(samples.size() / 2);
	std::nth_element(samples.begin(), middle, samples.end());
	return *middle;
}

void report(const char *name, std::size_t count, double ns,
            const char *note = nullptr) {
	std::printf("%-36s %10zu  %12.1f ns/op  %10.3f ns/elem", name, count, ns,
	            count != 0 ? ns / static_cast<double>(count) : 0.0);
	if (note != nullptr)
		std::printf("  (%s)", note);
	std::printf("\n");
}

ProjectileComponents buildProjectileWorld(World &world, std::size_t count,
                                          TickSpan lifetime) {
	auto components = ProjectileComponents::registerWith(world);
	if (!components)
		fail("register projectile components");
	if (!world.beginTick())
		fail("begin tick");
	auto flow = world.commands(CommitPhase::Flow, benchFlowSource);
	if (!flow)
		fail("open flow commands");
	ProjectileApi api(*flow, *components);
	ProjectileSpec spec{.collisionRadius = 3.0F,
	                    .lifetime = lifetime,
	                    .style = {0},
	                    .faction = {1},
	                    .flags = ProjectileFlags::RotateToVelocity,
	                    .damage = 0};
	std::vector<ProjectileSpawn> spawns;
	spawns.reserve(count);
	for (std::size_t index = 0; index < count; ++index) {
		spawns.push_back(ProjectileSpawn{
		    .position = {{static_cast<float>(index % 640),
		                  static_cast<float>((index / 640) % 720)}},
		    .velocityPerTick = {0.5F, 1.0F},
		    .orientation = {0.0F}});
	}
	auto batch = api.spawnBatch(spec, spawns);
	if (!batch)
		fail("spawn projectile batch");
	if (!world.commit(CommitPhase::Flow))
		fail("flow commit");
	if (!world.commit(CommitPhase::Simulation))
		fail("simulation commit");
	if (!world.commit(CommitPhase::Resolution))
		fail("resolution commit");
	return *components;
}

void runProjectileTick(World &world, const ProjectileComponents &components,
                       bool movement) {
	if (!world.beginTick())
		fail("begin tick");
	if (!world.commit(CommitPhase::Flow))
		fail("flow commit");
	auto simulation =
	    world.commands(CommitPhase::Simulation, benchSimulationSource);
	if (!simulation)
		fail("open simulation commands");
	if (movement)
		updateProjectileMovement(world.view(), *simulation, components);
	else
		updateProjectileLifetimes(world.view(), *simulation, components);
	if (!world.commit(CommitPhase::Simulation))
		fail("simulation commit");
	if (!world.commit(CommitPhase::Resolution))
		fail("resolution commit");
}

void benchWorldSpawnCommit() {
	constexpr std::size_t count = 20000;
	World world;
	auto position = world.registerComponent<BenchPosition>(
	    {.name = "bench.position.v1"});
	auto velocity = world.registerComponent<BenchVelocity>(
	    {.name = "bench.velocity.v1"});
	if (!position || !velocity)
		fail("register spawn components");

	const auto fn = [&]() {
		if (!world.beginTick())
			fail("begin tick");
		auto flow = world.commands(CommitPhase::Flow, benchFlowSource);
		if (!flow)
			fail("open flow commands");
		std::vector<std::tuple<BenchPosition, BenchVelocity>> records;
		records.reserve(count);
		for (std::size_t index = 0; index < count; ++index)
			records.emplace_back(BenchPosition{1.0F, 2.0F},
			                     BenchVelocity{0.5F, 0.25F});
		auto spawned =
		    flow->spawnBatch(std::tuple{*position, *velocity}, std::move(records));
		if (!spawned)
			fail("spawn batch");
		if (!world.commit(CommitPhase::Flow))
			fail("flow commit");
		if (!world.commit(CommitPhase::Simulation))
			fail("simulation commit");
		if (!world.commit(CommitPhase::Resolution))
			fail("resolution commit");
	};
	const double ns = medianNs(fn, 9);
	report("world_spawn_commit", count, ns);
}

void benchWorldQuery() {
	constexpr std::size_t count = 20000;
	World world;
	const auto components = buildProjectileWorld(world, count, TickSpan{100000});

	const auto fn = [&]() {
		double sum = 0.0;
		const auto rows = world.view().query(
		    components.transform, components.motion, components.identity);
		for (const auto row : rows)
			sum += row.get<Transform>().position.value.x +
			       row.get<Motion>().velocityPerTick.y;
		volatile double sink = sum;
		static_cast<void>(sink);
	};
	const double ns = medianNs(fn, 31);
	report("world_query_projectiles", count, ns);
}

void benchProjectileMovement() {
	constexpr std::size_t count = 20000;
	World world;
	const auto components = buildProjectileWorld(world, count, TickSpan{100000});
	const auto fn = [&]() { runProjectileTick(world, components, true); };
	const double ns = medianNs(fn, 15);
	report("projectile_movement_tick", count, ns, "incl. commits");
}

void benchProjectileLifetimes() {
	constexpr std::size_t count = 20000;
	World world;
	const auto components = buildProjectileWorld(world, count, TickSpan{100000});
	const auto fn = [&]() { runProjectileTick(world, components, false); };
	const double ns = medianNs(fn, 15);
	report("projectile_lifetimes_tick", count, ns, "incl. commits");
}

void benchPresentationSnapshot() {
	constexpr std::size_t count = 20000;
	World world;
	const auto components = buildProjectileWorld(world, count, TickSpan{100000});
	static_cast<void>(components);
	const auto fn = [&]() {
		const auto snapshot = world.buildPresentationSnapshot();
		volatile std::size_t sink = snapshot.entityCount();
		static_cast<void>(sink);
	};
	const double ns = medianNs(fn, 15);
	report("presentation_snapshot", count, ns);
}

void benchSessionStep() {
	constexpr std::uint64_t bulletsPerTick = 30;
	constexpr TickSpan bulletLifetime{600};

	GameDefinition definition;
	auto installed = definition.addSystem(
	    {.name = "bench.session.emit.v1", .phase = SystemPhase::PreSimulation},
	    [](GameplayContext &game) {
		    ProjectileSpec spec{.collisionRadius = 3.0F,
		                        .lifetime = bulletLifetime,
		                        .style = {0},
		                        .faction = {1},
		                        .damage = 0};
		    std::vector<ProjectileSpawn> spawns;
		    spawns.reserve(bulletsPerTick);
		    for (std::uint64_t index = 0; index < bulletsPerTick; ++index) {
			    spawns.push_back(ProjectileSpawn{
			        .position = {{320.0F + static_cast<float>(index), 310.0F}},
			        .velocityPerTick = {0.0F, 2.0F},
			        .orientation = {0.0F}});
		    }
		    static_cast<void>(game.projectiles().spawnBatch(spec, spawns));
	    });
	if (!installed)
		fail("install session emit system");
	auto session =
	    Session::create(std::move(definition),
	                    SessionConfig{.tickRate = {60},
	                                  .randomSeed = 1,
	                                  .requireSequentialInputTicks = true});
	if (!session) {
		std::fprintf(stderr, "create session failed: %s\n",
		             session.error().message.c_str());
		fail("create session");
	}

	Tick tick{0};
	const auto step = [&]() {
		tick.value += 1;
		auto result = (*session)->step(control::InputFrame{.tick = tick});
		if (!result)
			fail("session step");
	};
	for (std::uint64_t warmup = 0; warmup < 120; ++warmup)
		step();
	const std::size_t alive = (*session)->worldView().aliveCount();
	const double ns = medianNs(step, 25);
	report("session_step_30_per_tick", alive, ns, "steady state");
}

} // namespace

int main() {
	std::printf("ShikigamiSTG engine benchmarks (release build)\n");
	std::printf("%-36s %10s %16s %14s\n", "case", "elements", "median",
	            "per element");
	benchWorldSpawnCommit();
	benchWorldQuery();
	benchProjectileMovement();
	benchProjectileLifetimes();
	benchPresentationSnapshot();
	benchSessionStep();
	return 0;
}
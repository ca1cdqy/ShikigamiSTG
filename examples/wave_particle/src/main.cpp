#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <numbers>
#include <optional>
#include <vector>

#include <shiki/control/input_frame.h>
#include <shiki/game/component.h>
#include <shiki/game/system.h>
#include <shiki/game_definition.h>
#include <shiki/session.h>
#include <shiki/stg/projectile/projectile.h>

#include "wave_particle.h"

namespace {

constexpr int kWindowWidth = 960;
constexpr int kWindowHeight = 720;
constexpr float kCanvasWidth = 960.0F;
constexpr float kCanvasHeight = 720.0F;
constexpr float kPlayfieldLeft = 160.0F;
constexpr float kPlayfieldWidth = 640.0F;
constexpr float kPlayfieldHeight = 720.0F;
constexpr float kEmitterX = 320.0F;
constexpr float kEmitterY = 310.0F;
constexpr std::uint64_t kWarmupTicks = 45;

struct DemoSession final {
	std::unique_ptr<shiki::Session> session;
	shiki::game::ComponentToken<shiki::stg::Transform> transform;
	shiki::game::ComponentToken<shiki::stg::ProjectileVisual> visual;
};

[[nodiscard]] std::optional<DemoSession> createDemoSession() {
	using namespace shiki;
	using namespace shiki::game;
	using namespace shiki::stg;

	GameDefinition definition;
	constexpr ComponentFlags observable =
	    ComponentFlags::Observable | ComponentFlags::Deterministic;
	auto transform = definition.registerComponent<Transform>(
	    {.name = "shiki.transform.v1", .flags = observable});
	auto visual = definition.registerComponent<ProjectileVisual>(
	    {.name = "shiki.projectile.visual.v1", .flags = observable});
	if (!transform || !visual) {
		std::cerr << "Failed to register projectile presentation components\n";
		return std::nullopt;
	}

	auto wave = std::make_shared<WaveParticleState>();
	auto installed =
	    definition.addSystem({.name = "example.wave_particle.emit.v1",
	                          .phase = SystemPhase::PreSimulation},
	                         [wave](GameplayContext &game) {
		                         if (game.time().tick.value > kWarmupTicks)
			                         emitWaveParticle(game, *wave);
	                         });
	if (!installed) {
		std::cerr << "Failed to install wave-particle system\n";
		return std::nullopt;
	}

	auto session =
	    Session::create(std::move(definition),
	                    SessionConfig{.tickRate = {60},
	                                  .randomSeed = 0x09523B,
	                                  .requireSequentialInputTicks = true});
	if (!session) {
		std::cerr << "Failed to create deterministic demo session\n";
		return std::nullopt;
	}
	return DemoSession{std::move(*session), *transform, *visual};
}

[[nodiscard]] SDL_FPoint project(const shiki::Vec2 &position) {
	return {kPlayfieldLeft + position.x, position.y};
}

void appendDiamond(std::vector<SDL_Vertex> &vertices, std::vector<int> &indices,
                   SDL_FPoint center, float radius, SDL_FColor color) {
	const int base = static_cast<int>(vertices.size());
	vertices.push_back({{center.x, center.y - radius}, color, {0.0F, 0.0F}});
	vertices.push_back({{center.x + radius, center.y}, color, {0.0F, 0.0F}});
	vertices.push_back({{center.x, center.y + radius}, color, {0.0F, 0.0F}});
	vertices.push_back({{center.x - radius, center.y}, color, {0.0F, 0.0F}});
	indices.insert(indices.end(),
	               {base, base + 1, base + 2, base, base + 2, base + 3});
}

void appendEmitter(std::vector<SDL_Vertex> &vertices, std::vector<int> &indices,
                   SDL_FPoint center, float radius, SDL_FColor color) {
	constexpr int segments = 32;
	const int base = static_cast<int>(vertices.size());
	vertices.push_back({center, color, {0.0F, 0.0F}});
	for (int segment = 0; segment <= segments; ++segment) {
		const float angle = 2.0F * std::numbers::pi_v<float> *
		                    static_cast<float>(segment) /
		                    static_cast<float>(segments);
		vertices.push_back({{center.x + std::cos(angle) * radius,
		                     center.y + std::sin(angle) * radius},
		                    color,
		                    {0.0F, 0.0F}});
	}
	for (int segment = 0; segment < segments; ++segment) {
		indices.insert(indices.end(),
		               {base, base + segment + 1, base + segment + 2});
	}
}

void drawBackdrop(SDL_Renderer *renderer) {
	SDL_SetRenderDrawColor(renderer, 9, 15, 24, 255);
	SDL_RenderClear(renderer);

	const SDL_FRect canvasRect{0.0F, 0.0F, kCanvasWidth, kCanvasHeight};
	SDL_SetRenderDrawColor(renderer, 16, 25, 38, 255);
	SDL_RenderFillRect(renderer, &canvasRect);

	const SDL_FRect playfield{kPlayfieldLeft, 0.0F, kPlayfieldWidth,
	                          kPlayfieldHeight};
	SDL_SetRenderDrawColor(renderer, 11, 18, 29, 255);
	SDL_RenderFillRect(renderer, &playfield);

	SDL_SetRenderDrawColor(renderer, 34, 53, 73, 150);
	for (int x = 0; x <= 10; ++x) {
		const float lineX =
		    playfield.x + static_cast<float>(x) * playfield.w / 10.0F;
		SDL_RenderLine(renderer, lineX, playfield.y, lineX,
		               playfield.y + playfield.h);
	}
	for (int y = 0; y <= 12; ++y) {
		const float lineY =
		    playfield.y + static_cast<float>(y) * playfield.h / 12.0F;
		SDL_RenderLine(renderer, playfield.x, lineY, playfield.x + playfield.w,
		               lineY);
	}
	SDL_SetRenderDrawColor(renderer, 88, 208, 177, 255);
	SDL_RenderRect(renderer, &playfield);
}

void drawProjectiles(SDL_Renderer *renderer, const DemoSession &demo,
                     float interpolation) {
	std::vector<SDL_Vertex> vertices;
	std::vector<int> indices;
	const auto &current = demo.session->currentPresentation();
	const auto &previous = demo.session->previousPresentation();
	const auto rows = current.query(shiki::game::QueryOrder::EntityId,
	                                demo.transform, demo.visual);
	vertices.reserve(rows.size() * 8 + 34);
	indices.reserve(rows.size() * 12 + 96);

	for (const auto &entry : rows) {
		const auto &currentTransform =
		    entry.template get<shiki::stg::Transform>();
		shiki::Vec2 position = currentTransform.position.value;
		if (const auto *old = previous.tryGet(entry.entity(), demo.transform)) {
			position =
			    shiki::lerp(old->position.value, position, interpolation);
		}
		if (position.x < -24.0F || position.x > kPlayfieldWidth + 24.0F ||
		    position.y < -24.0F || position.y > kPlayfieldHeight + 24.0F)
			continue;
		const auto &visual = entry.template get<shiki::stg::ProjectileVisual>();
		const SDL_FColor core = visual.style.value == 0
		                            ? SDL_FColor{1.0F, 0.33F, 0.38F, 0.95F}
		                            : SDL_FColor{0.32F, 0.86F, 0.73F, 0.95F};
		const SDL_FPoint screen = project(position);
		appendDiamond(vertices, indices, screen, 5.8F,
		              {core.r, core.g, core.b, 0.16F});
		appendDiamond(vertices, indices, screen, 2.7F, core);
	}

	const SDL_FPoint emitter = project({kEmitterX, kEmitterY});
	appendEmitter(vertices, indices, emitter, 15.0F,
	              {0.96F, 0.98F, 0.97F, 0.92F});
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	if (!vertices.empty()) {
		SDL_RenderGeometry(renderer, nullptr, vertices.data(),
		                   static_cast<int>(vertices.size()), indices.data(),
		                   static_cast<int>(indices.size()));
	}
}

} // namespace

int main() {
	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
		std::cerr << "SDL initialization failed: " << SDL_GetError() << '\n';
		return 1;
	}
	SDL_Window *window =
	    SDL_CreateWindow("ShikigamiSTG - Boundary of Wave and Particle",
	                     kWindowWidth, kWindowHeight, SDL_WINDOW_RESIZABLE);
	SDL_Renderer *renderer =
	    window ? SDL_CreateRenderer(window, nullptr) : nullptr;
	if (!window || !renderer) {
		std::cerr << "SDL window creation failed: " << SDL_GetError() << '\n';
		if (renderer)
			SDL_DestroyRenderer(renderer);
		if (window)
			SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}
	SDL_SetRenderLogicalPresentation(renderer, kWindowWidth, kWindowHeight,
	                                 SDL_LOGICAL_PRESENTATION_LETTERBOX);
	SDL_SetRenderVSync(renderer, 1);

	auto demo = createDemoSession();
	if (!demo) {
		SDL_DestroyRenderer(renderer);
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}

	bool running = true;
	bool paused = false;
	double accumulator = 0.0;
	constexpr double fixedStep = 1.0 / 60.0;
	std::uint64_t previousCounter = SDL_GetPerformanceCounter();
	const double frequency = static_cast<double>(SDL_GetPerformanceFrequency());
	while (running) {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_EVENT_QUIT)
				running = false;
			if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
				if (event.key.key == SDLK_ESCAPE)
					running = false;
				else if (event.key.key == SDLK_SPACE)
					paused = !paused;
				else if (event.key.key == SDLK_R) {
					demo = createDemoSession();
					accumulator = 0.0;
				}
			}
		}

		const std::uint64_t counter = SDL_GetPerformanceCounter();
		double elapsed =
		    static_cast<double>(counter - previousCounter) / frequency;
		previousCounter = counter;
		elapsed = std::min(elapsed, 0.1);
		if (!paused)
			accumulator += elapsed;
		while (demo && accumulator >= fixedStep) {
			const shiki::Tick next{demo->session->tick().value + 1};
			if (!demo->session->step({.tick = next})) {
				std::cerr << "Session step failed\n";
				running = false;
				break;
			}
			accumulator -= fixedStep;
		}

		drawBackdrop(renderer);
		if (demo) {
			drawProjectiles(renderer, *demo,
			                static_cast<float>(accumulator / fixedStep));
		}
		SDL_RenderPresent(renderer);
	}

	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}

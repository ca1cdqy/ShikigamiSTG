#include "th06_stage_background.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <shiki/asset/standard_assets.h>
#include <shiki/asset/structured_assets.h>
#include <shiki/render/renderer.h>
#include <shiki/render/texture.h>
#include <shiki/resource/resource_manager.h>

namespace {
shiki::Vec3 add(const shiki::Vec3 &a, const shiki::Vec3 &b) {
	return {a.x + b.x, a.y + b.y, a.z + b.z};
}

shiki::Vec3 subtract(const shiki::Vec3 &a, const shiki::Vec3 &b) {
	return {a.x - b.x, a.y - b.y, a.z - b.z};
}

shiki::Vec3 multiply(const shiki::Vec3 &value, float scalar) {
	return {value.x * scalar, value.y * scalar, value.z * scalar};
}

float dot(const shiki::Vec3 &a, const shiki::Vec3 &b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

shiki::Vec3 cross(const shiki::Vec3 &a, const shiki::Vec3 &b) {
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
	        a.x * b.y - a.y * b.x};
}

shiki::Vec3 normalize(const shiki::Vec3 &value) {
	const float length = std::max(std::sqrt(dot(value, value)), 0.0001f);
	return multiply(value, 1.0f / length);
}

shiki::Color unpackColor(uint32_t argb) {
	return {static_cast<float>((argb >> 16) & 0xff) / 255.0f,
	        static_cast<float>((argb >> 8) & 0xff) / 255.0f,
	        static_cast<float>(argb & 0xff) / 255.0f,
	        static_cast<float>((argb >> 24) & 0xff) / 255.0f};
}

float lerp(float from, float to, float ratio) {
	return from + (to - from) * ratio;
}

shiki::Vec3 lerp(const shiki::Vec3 &from, const shiki::Vec3 &to, float ratio) {
	return {lerp(from.x, to.x, ratio), lerp(from.y, to.y, ratio),
	        lerp(from.z, to.z, ratio)};
}

shiki::Color lerp(const shiki::Color &from, const shiki::Color &to,
                  float ratio) {
	return {lerp(from.x, to.x, ratio), lerp(from.y, to.y, ratio),
	        lerp(from.z, to.z, ratio), lerp(from.w, to.w, ratio)};
}
} // namespace

bool TH06StageBackground::load(shiki::ResourceManager &resources,
                               std::string_view assetName) {
	auto *assets = resources.getAssetStore();
	if (!assets)
		return false;
	auto loaded = assets->load<shiki::asset::StageAsset>(
	    shiki::asset::AssetId::fromName(assetName));
	if (!loaded)
		return false;

	resources_ = &resources;
	scriptVisuals_.clear();
	auto animation = assets->load<shiki::asset::AnimationAsset>(
	    shiki::asset::AssetId::fromName("animation." + (*loaded)->atlas));
	if (!animation)
		return false;
	if (!(*animation)->scripts.empty())
		scriptVisuals_.resize(
		    static_cast<size_t>((*animation)->scripts.rbegin()->first + 1));
	animationFile_ = std::make_shared<TH06MenuAnmFile>();
	if (!animationFile_->load(assets, (*loaded)->atlas))
		animationFile_.reset();
	scriptVms_.clear();
	scriptVms_.resize(scriptVisuals_.size());
	const auto read = [&](size_t offset, auto &value) {
		if (offset + sizeof(value) > (*animation)->instructions.size())
			return false;
		std::memcpy(&value, (*animation)->instructions.data() + offset,
		            sizeof(value));
		return true;
	};
	for (const auto &[scriptId, range] : (*animation)->scripts) {
		if (scriptId < 0 ||
		    static_cast<size_t>(scriptId) >= scriptVisuals_.size())
			continue;
		auto &visual = scriptVisuals_[static_cast<size_t>(scriptId)];
		if (animationFile_ &&
		    scriptVms_[static_cast<size_t>(scriptId)].initialize(animationFile_,
		                                                         scriptId))
			for (size_t cursor = range.begin; cursor + 4 <= range.end;) {
				int16_t instructionTime{};
				uint8_t opcode{};
				uint8_t length{};
				read(cursor, instructionTime);
				read(cursor + 2, opcode);
				read(cursor + 3, length);
				const size_t args = cursor + 4;
				if (args + length > range.end)
					break;
				if (instructionTime != 0) {
					cursor = args + length;
					continue;
				}
				switch (opcode) {
				case 1: {
					int32_t rawSprite{};
					if (read(args, rawSprite))
						visual.texture = resources.getSpriteTexture(
						    (*animation)->atlas,
						    (*animation)->resolveSprite(rawSprite));
					break;
				}
				case 2:
					read(args, visual.scale.x);
					read(args + sizeof(float), visual.scale.y);
					break;
				case 3: {
					uint32_t alpha{};
					if (read(args, alpha))
						visual.alpha =
						    static_cast<float>(alpha & 0xff) / 255.0f;
					break;
				}
				case 12: {
					uint32_t target{};
					uint32_t duration{};
					if (read(args, target) &&
					    read(args + sizeof(uint32_t), duration)) {
						visual.fadeTargetAlpha =
						    static_cast<float>(target & 0xff) / 255.0f;
						visual.fadeStartFrame =
						    static_cast<int>(instructionTime);
						visual.fadeDuration =
						    static_cast<int>(duration & 0xffff);
					}
					break;
				}
				case 7:
					visual.scale.x = -visual.scale.x;
					break;
				case 9:
					read(args, visual.rotation.x);
					read(args + sizeof(float), visual.rotation.y);
					read(args + sizeof(float) * 2, visual.rotation.z);
					break;
				case 13:
					visual.additive = true;
					break;
				case 14:
					visual.additive = false;
					break;
				case 23:
					visual.topLeft = true;
					break;
				default:
					break;
				}
				cursor = args + length;
			}
	}

	objects_.clear();
	instances_.clear();
	instructions_.clear();
	name_ = (*loaded)->name;
	songNames_.fill({});
	std::copy_n((*loaded)->songs.begin(),
	            std::min(songNames_.size(), (*loaded)->songs.size()),
	            songNames_.begin());
	for (const auto &source : (*loaded)->objects) {
		Object object;
		object.zLevel = source.zLevel;
		object.position = {source.position[0], source.position[1],
		                   source.position[2]};
		for (const auto &entry : source.quads) {
			Quad quad;
			quad.script = entry.script;
			quad.position = {entry.position[0], entry.position[1],
			                 entry.position[2]};
			quad.size = {entry.size[0], entry.size[1]};
			if (quad.script >= 0 &&
			    static_cast<size_t>(quad.script) < scriptVisuals_.size()) {
				const auto &visual =
				    scriptVisuals_[static_cast<size_t>(quad.script)];
				if (visual.texture && visual.texture->isValid()) {
					if (quad.size.x <= 0.0f)
						quad.size.x =
						    static_cast<float>(visual.texture->getWidth()) *
						    std::abs(visual.scale.x);
					if (quad.size.y <= 0.0f)
						quad.size.y =
						    static_cast<float>(visual.texture->getHeight()) *
						    std::abs(visual.scale.y);
				}
			}
			object.quads.push_back(quad);
		}
		objects_.push_back(std::move(object));
	}

	for (const auto &entry : (*loaded)->instances) {
		Instance instance;
		instance.objectId = entry.objectId;
		instance.position = {entry.position[0], entry.position[1],
		                     entry.position[2]};
		instances_.push_back(instance);
	}

	for (const auto &entry : (*loaded)->timeline) {
		Instruction instruction;
		instruction.frame = entry.frame;
		instruction.opcode = entry.opcode;
		instruction.arg0 = entry.argument;
		instruction.vector = {entry.vector[0], entry.vector[1],
		                      entry.vector[2]};
		instructions_.push_back(instruction);
	}

	loaded_ = std::any_of(
	    scriptVisuals_.begin(), scriptVisuals_.end(), [](const auto &visual) {
		    return visual.texture && visual.texture->isValid();
	    });
	return loaded_;
}

void TH06StageBackground::update() {
	if (!loaded_ || !resources_)
		return;
	for (size_t index = 0; index < scriptVms_.size(); ++index) {
		auto &vm = scriptVms_[index];
		if (!vm.file)
			continue;
		vm.tick();
		if (index >= scriptVisuals_.size() || vm.sprite < 0)
			continue;
		auto &visual = scriptVisuals_[index];
		visual.texture =
		    resources_->getSpriteTexture(vm.file->atlas, vm.sprite);
		visual.scale = {vm.scaleX, vm.scaleY};
		visual.rotation = {vm.rotationX, vm.rotationY, vm.rotation};
		visual.uvOffset = {vm.uvOffsetX, vm.uvOffsetY};
		visual.alpha = static_cast<float>((vm.color >> 24) & 0xff) / 255.0f;
		visual.fadeDuration = 0;
		visual.additive = vm.additive;
		visual.topLeft = vm.topLeft;
	}
}

TH06StageBackground::CameraState
TH06StageBackground::evaluateCamera(float frame) const {
	CameraState state;

	std::vector<const Instruction *> positions;
	for (const auto &instruction : instructions_)
		if (instruction.opcode == 0 && instruction.frame >= 0)
			positions.push_back(&instruction);
	if (!positions.empty()) {
		state.position = positions.front()->vector;
		for (size_t index = 0; index < positions.size(); ++index) {
			if (frame < positions[index]->frame)
				break;
			state.position = positions[index]->vector;
			if (index + 1 < positions.size() &&
			    frame < positions[index + 1]->frame) {
				const float span = static_cast<float>(
				    positions[index + 1]->frame - positions[index]->frame);
				const float ratio =
				    span > 0.0f ? (frame - positions[index]->frame) / span
				                : 1.0f;
				state.position =
				    lerp(positions[index]->vector, positions[index + 1]->vector,
				         std::clamp(ratio, 0.0f, 1.0f));
				break;
			}
		}
	}

	int facingDuration = 0;
	int fogDuration = 0;
	shiki::Vec3 facingInitial = state.facing;
	Fog fogInitial = state.fog;
	int facingStart = 0;
	int fogStart = 0;
	for (const auto &instruction : instructions_) {
		if (instruction.frame < 0 || instruction.frame > frame)
			continue;
		switch (instruction.opcode) {
		case 1: {
			Fog target{unpackColor(static_cast<uint32_t>(instruction.arg0)),
			           instruction.vector.y, instruction.vector.z};
			if (fogDuration > 0) {
				const float ratio = std::clamp(
				    (frame - fogStart) / static_cast<float>(fogDuration), 0.0f,
				    1.0f);
				state.fog.color = lerp(fogInitial.color, target.color, ratio);
				state.fog.nearPlane =
				    lerp(fogInitial.nearPlane, target.nearPlane, ratio);
				state.fog.farPlane =
				    lerp(fogInitial.farPlane, target.farPlane, ratio);
			} else {
				state.fog = target;
			}
			break;
		}
		case 2: {
			const auto target = instruction.vector;
			if (facingDuration > 0) {
				const float ratio = std::clamp(
				    (frame - facingStart) / static_cast<float>(facingDuration),
				    0.0f, 1.0f);
				state.facing = lerp(facingInitial, target, ratio);
			} else {
				state.facing = target;
			}
			break;
		}
		case 3:
			facingInitial = state.facing;
			facingDuration = instruction.arg0;
			facingStart = instruction.frame;
			break;
		case 4:
			fogInitial = state.fog;
			fogDuration = instruction.arg0;
			fogStart = instruction.frame;
			break;
		default:
			break;
		}
	}
	return state;
}

void TH06StageBackground::render(shiki::Renderer &renderer,
                                 float stageFrame) const {
	if (!loaded_)
		return;
	const auto camera = evaluateCamera(stageFrame);
	constexpr float WIDTH = 384.0f;
	constexpr float HEIGHT = 448.0f;
	constexpr float FOV = 30.0f * 3.14159265358979323846f / 180.0f;
	const float cameraDistance = (HEIGHT * 0.5f) / std::tan(FOV * 0.5f);
	const shiki::Vec3 eye{WIDTH * 0.5f, -HEIGHT * 0.5f,
	                      -cameraDistance * camera.facing.z};
	const shiki::Vec3 at{WIDTH * 0.5f + camera.facing.x,
	                     -HEIGHT * 0.5f + camera.facing.y, 0.0f};
	const auto forward = normalize(subtract(at, eye));
	const auto right = normalize(cross({0.0f, 1.0f, 0.0f}, forward));
	const auto up = cross(forward, right);
	const float tanHalfFov = std::tan(FOV * 0.5f);
	const float aspect = WIDTH / HEIGHT;

	const auto cameraDepth = [&](const shiki::Vec3 &point) {
		return dot(subtract(point, eye), forward);
	};
	const auto project = [&](const shiki::Vec3 &point, shiki::Vec2 &result,
	                         float &depth) {
		const auto relative = subtract(point, eye);
		depth = dot(relative, forward);
		const float ndcX = dot(relative, right) / (depth * tanHalfFov * aspect);
		const float ndcY = dot(relative, up) / (depth * tanHalfFov);
		result = {(ndcX + 1.0f) * WIDTH * 0.5f, (1.0f - ndcY) * HEIGHT * 0.5f};
	};

	struct ClipVertex {
		shiki::Vec3 position;
		shiki::Vec2 uv;
	};
	const auto clipToNearPlane = [&](std::array<ClipVertex, 3> triangle) {
		constexpr float NEAR_PLANE = 100.0f;
		std::vector<ClipVertex> input(triangle.begin(), triangle.end());
		std::vector<ClipVertex> output;
		output.reserve(4);
		for (size_t index = 0; index < input.size(); ++index) {
			const auto &current = input[index];
			const auto &previous =
			    input[(index + input.size() - 1) % input.size()];
			const float currentDepth = cameraDepth(current.position);
			const float previousDepth = cameraDepth(previous.position);
			const bool currentInside = currentDepth >= NEAR_PLANE;
			const bool previousInside = previousDepth >= NEAR_PLANE;
			if (currentInside != previousInside) {
				const float ratio = (NEAR_PLANE - previousDepth) /
				                    (currentDepth - previousDepth);
				output.push_back(
				    {lerp(previous.position, current.position, ratio),
				     {lerp(previous.uv.x, current.uv.x, ratio),
				      lerp(previous.uv.y, current.uv.y, ratio)}});
			}
			if (currentInside)
				output.push_back(current);
		}
		return output;
	};

	struct ProjectedTriangle {
		int zLevel = 0;
		float depth = 0.0f;
		int script = 0;
		bool additive = false;
		std::array<shiki::Vec2, 3> positions;
		std::array<shiki::Color, 3> colors;
		std::array<shiki::Vec2, 3> uvs;
		std::array<shiki::Color, 3> fogColors;
		std::array<float, 3> fogFactors;
	};
	std::vector<ProjectedTriangle> projectedTriangles;

	for (int zLevel = 0; zLevel < 4; ++zLevel) {
		for (const auto &instance : instances_) {
			if (instance.objectId < 0 ||
			    instance.objectId >= static_cast<int>(objects_.size()))
				continue;
			const auto &object =
			    objects_[static_cast<size_t>(instance.objectId)];
			if (object.zLevel != zLevel)
				continue;
			for (const auto &quad : object.quads) {
				if (quad.script < 0 ||
				    static_cast<size_t>(quad.script) >= scriptVisuals_.size())
					continue;
				const auto &visual =
				    scriptVisuals_[static_cast<size_t>(quad.script)];
				if (!visual.texture || !visual.texture->isValid())
					continue;
				// Stage::RenderObjects uses the object's position only for its
				// helper cube visibility test. Actual quad VMs are positioned
				// from the quad and instance coordinates alone.
				const auto base = subtract(
				    add(instance.position, quad.position), camera.position);
				const float width =
				    quad.size.x > 0.0f
				        ? quad.size.x
				        : static_cast<float>(visual.texture->getWidth()) *
				              std::abs(visual.scale.x);
				const float height =
				    quad.size.y > 0.0f
				        ? quad.size.y
				        : static_cast<float>(visual.texture->getHeight()) *
				              std::abs(visual.scale.y);
				std::array<shiki::Vec3, 4> world;
				const float centerX =
				    visual.topLeft ? base.x + width * 0.5f : base.x;
				const float centerY =
				    visual.topLeft ? base.y + height * 0.5f : base.y;
				const float sineX = std::sin(visual.rotation.x);
				const float cosineX = std::cos(visual.rotation.x);
				const float sineY = std::sin(visual.rotation.y);
				const float cosineY = std::cos(visual.rotation.y);
				const float sineZ = std::sin(visual.rotation.z);
				const float cosineZ = std::cos(visual.rotation.z);
				const std::array<shiki::Vec2, 4> local = {
				    shiki::Vec2{-width * 0.5f, height * 0.5f},
				    shiki::Vec2{width * 0.5f, height * 0.5f},
				    shiki::Vec2{-width * 0.5f, -height * 0.5f},
				    shiki::Vec2{width * 0.5f, -height * 0.5f}};
				for (size_t index = 0; index < local.size(); ++index) {
					shiki::Vec3 rotated{local[index].x, local[index].y, 0.0f};
					rotated = {rotated.x,
					           rotated.y * cosineX - rotated.z * sineX,
					           rotated.y * sineX + rotated.z * cosineX};
					rotated = {rotated.x * cosineY + rotated.z * sineY,
					           rotated.y,
					           -rotated.x * sineY + rotated.z * cosineY};
					rotated = {rotated.x * cosineZ - rotated.y * sineZ,
					           rotated.x * sineZ + rotated.y * cosineZ,
					           rotated.z};
					world[index] = {centerX + rotated.x, -(centerY + rotated.y),
					                base.z + rotated.z};
				}

				float alpha = visual.alpha;
				if (visual.fadeDuration > 0 &&
				    stageFrame >= visual.fadeStartFrame) {
					const float ratio =
					    std::clamp((stageFrame -
					                static_cast<float>(visual.fadeStartFrame)) /
					                   static_cast<float>(visual.fadeDuration),
					               0.0f, 1.0f);
					alpha = lerp(alpha, visual.fadeTargetAlpha, ratio);
				}
				const float u0 = visual.uvOffset.x;
				const float v0 = visual.uvOffset.y;
				const bool flipX = visual.scale.x < 0.0f;
				const float quadDepth =
				    (cameraDepth(world[0]) + cameraDepth(world[1]) +
				     cameraDepth(world[2]) + cameraDepth(world[3])) /
				    4.0f;
				const std::array<shiki::Vec2, 4> uvs = {
				    shiki::Vec2{u0 + (flipX ? 1.0f : 0.0f), v0},
				    shiki::Vec2{u0 + (flipX ? 0.0f : 1.0f), v0},
				    shiki::Vec2{u0 + (flipX ? 1.0f : 0.0f), v0 + 1.0f},
				    shiki::Vec2{u0 + (flipX ? 0.0f : 1.0f), v0 + 1.0f}};
				constexpr std::array<std::array<size_t, 3>, 2> TRIANGLES = {
				    std::array<size_t, 3>{0, 1, 2},
				    std::array<size_t, 3>{1, 3, 2}};
				for (const auto &indices : TRIANGLES) {
					const auto clipped = clipToNearPlane(
					    {{{world[indices[0]], uvs[indices[0]]},
					      {world[indices[1]], uvs[indices[1]]},
					      {world[indices[2]], uvs[indices[2]]}}});
					if (clipped.size() < 3)
						continue;
					for (size_t fan = 1; fan + 1 < clipped.size(); ++fan) {
						const std::array<ClipVertex, 3> vertices = {
						    clipped[0], clipped[fan], clipped[fan + 1]};
						ProjectedTriangle triangle;
						triangle.zLevel = zLevel;
						triangle.script = quad.script;
						triangle.additive = visual.additive;
						triangle.depth = quadDepth;
						for (size_t vertex = 0; vertex < vertices.size();
						     ++vertex) {
							float depth{};
							project(vertices[vertex].position,
							        triangle.positions[vertex], depth);
							triangle.uvs[vertex] = vertices[vertex].uv;
							const float fogFactor = std::clamp(
							    (camera.fog.farPlane - depth) /
							        std::max(camera.fog.farPlane -
							                     camera.fog.nearPlane,
							                 1.0f),
							    0.0f, 1.0f);
							triangle.colors[vertex] = {1.0f, 1.0f, 1.0f, alpha};
							triangle.fogColors[vertex] = camera.fog.color;
							triangle.fogFactors[vertex] = fogFactor;
						}
						projectedTriangles.push_back(std::move(triangle));
					}
				}
			}
		}
	}

	// TH06 relies on D3D depth testing. The 2D renderer obtains the same
	// visibility by drawing farther faces first inside each explicit STD layer.
	std::stable_sort(
	    projectedTriangles.begin(), projectedTriangles.end(),
	    [](const ProjectedTriangle &left, const ProjectedTriangle &right) {
		    if (left.zLevel != right.zLevel)
			    return left.zLevel < right.zLevel;
		    return left.depth > right.depth;
	    });
	for (const auto &triangle : projectedTriangles) {
		if (triangle.script < 0 ||
		    static_cast<size_t>(triangle.script) >= scriptVisuals_.size())
			continue;
		const std::array<shiki::Vec2, 4> positions = {
		    triangle.positions[0], triangle.positions[1], triangle.positions[2],
		    triangle.positions[2]};
		const std::array<shiki::Color, 4> colors = {
		    triangle.colors[0], triangle.colors[1], triangle.colors[2],
		    triangle.colors[2]};
		const std::array<shiki::Vec2, 4> uvs = {
		    triangle.uvs[0], triangle.uvs[1], triangle.uvs[2], triangle.uvs[2]};
		const std::array<shiki::Color, 4> fogColors = {
		    triangle.fogColors[0], triangle.fogColors[1], triangle.fogColors[2],
		    triangle.fogColors[2]};
		const std::array<float, 4> fogFactors = {
		    triangle.fogFactors[0], triangle.fogFactors[1],
		    triangle.fogFactors[2], triangle.fogFactors[2]};
		renderer.drawFoggedTexturedQuad(
		    scriptVisuals_[static_cast<size_t>(triangle.script)].texture,
		    positions, colors, uvs, fogColors, fogFactors,
		    triangle.additive ? shiki::BlendMode::Add : shiki::BlendMode::Alpha,
		    true, true);
	}
}

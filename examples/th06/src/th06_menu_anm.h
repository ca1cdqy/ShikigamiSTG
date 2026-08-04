#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <shiki/asset/standard_assets.h>
#include <string>
#include <unordered_map>
#include <vector>

struct TH06MenuAnmFile {
	struct ScriptRange {
		size_t begin = 0;
		size_t end = 0;
	};

	std::string atlas;
	std::vector<uint8_t> data;
	std::unordered_map<int, ScriptRange> scripts;
	std::unordered_map<int, int> spriteLocalIndices;

	template <typename T> bool read(size_t offset, T &value) const {
		if (offset + sizeof(T) > data.size())
			return false;
		std::memcpy(&value, data.data() + offset, sizeof(T));
		return true;
	}

	bool load(shiki::asset::AssetStore *assets, std::string_view atlasName) {
		if (!assets)
			return false;
		auto loaded = assets->load<shiki::asset::AnimationAsset>(
		    shiki::asset::AssetId::fromName("animation." +
		                                    std::string(atlasName)));
		if (!loaded)
			return false;
		atlas = (*loaded)->atlas;
		data.resize((*loaded)->instructions.size());
		std::memcpy(data.data(), (*loaded)->instructions.data(), data.size());
		scripts.clear();
		spriteLocalIndices.clear();
		for (const auto &[id, range] : (*loaded)->scripts)
			scripts[id] = {range.begin, range.end};
		for (const auto &[rawId, localId] : (*loaded)->spriteMap)
			spriteLocalIndices[rawId] = localId;
		return true;
	}

	int resolveSprite(int rawId) const {
		const auto found = spriteLocalIndices.find(rawId);
		return found == spriteLocalIndices.end() ? rawId : found->second;
	}
};

struct TH06MenuAnmVm {
	static constexpr int NO_PENDING_INTERRUPT = std::numeric_limits<int>::min();

	std::shared_ptr<TH06MenuAnmFile> file;
	size_t scriptBegin = 0;
	size_t scriptEnd = 0;
	size_t instruction = 0;
	int time = 0;
	int pendingInterrupt = NO_PENDING_INTERRUPT;
	int sprite = -1;
	float x = 0.0f;
	float y = 0.0f;
	float offsetX = 0.0f;
	float offsetY = 0.0f;
	float scaleX = 1.0f;
	float scaleY = 1.0f;
	float rotationX = 0.0f;
	float rotationY = 0.0f;
	float rotation = 0.0f;
	float angleVelocityX = 0.0f;
	float angleVelocityY = 0.0f;
	float angleVelocity = 0.0f;
	float scaleVelocityX = 0.0f;
	float scaleVelocityY = 0.0f;
	float uvScrollX = 0.0f;
	float uvScrollY = 0.0f;
	float uvOffsetX = 0.0f;
	float uvOffsetY = 0.0f;
	uint32_t color = 0xffffffff;
	bool visible = false;
	bool stopped = false;
	bool useOffset = false;
	bool additive = false;
	bool topLeft = false;
	bool autoRotate = false;

	float positionInitialX = 0.0f;
	float positionInitialY = 0.0f;
	float positionFinalX = 0.0f;
	float positionFinalY = 0.0f;
	int positionDuration = 0;
	int positionFrame = 0;
	int positionMode = 0;
	float scaleInitialX = 1.0f;
	float scaleInitialY = 1.0f;
	float scaleFinalX = 1.0f;
	float scaleFinalY = 1.0f;
	int scaleDuration = 0;
	int scaleFrame = 0;
	uint8_t alphaInitial = 255;
	uint8_t alphaFinal = 255;
	int alphaDuration = 0;
	int alphaFrame = 0;

	bool initialize(std::shared_ptr<TH06MenuAnmFile> source, int scriptId) {
		if (!source)
			return false;
		const auto found = source->scripts.find(scriptId);
		if (found == source->scripts.end())
			return false;
		file = std::move(source);
		scriptBegin = found->second.begin;
		scriptEnd = found->second.end;
		instruction = scriptBegin;
		visible = false;
		tick();
		return true;
	}

	void interrupt(int id) { pendingInterrupt = id; }

	template <typename T> T argument(size_t offset) const {
		T value{};
		file->read(offset, value);
		return value;
	}

	void seekInterrupt() {
		size_t cursor = scriptBegin;
		size_t fallback = 0;
		while (cursor + 4 <= scriptEnd) {
			const uint8_t opcode = file->data[cursor + 2];
			const uint8_t length = file->data[cursor + 3];
			if (cursor + 4 + length > scriptEnd)
				break;
			if (opcode == 22 && length >= 4) {
				const int32_t label = argument<int32_t>(cursor + 4);
				if (label == -1)
					fallback = cursor;
				if (label == pendingInterrupt) {
					instruction = cursor + 4 + length;
					time = argument<int16_t>(instruction);
					pendingInterrupt = NO_PENDING_INTERRUPT;
					stopped = false;
					visible = true;
					return;
				}
			}
			if (opcode == 0 || opcode == 15)
				break;
			cursor += 4 + length;
		}
		if (fallback != 0) {
			const uint8_t length = file->data[fallback + 3];
			instruction = fallback + 4 + length;
			time = argument<int16_t>(instruction);
			stopped = false;
			visible = true;
		}
		pendingInterrupt = NO_PENDING_INTERRUPT;
	}

	void tick() {
		if (!file)
			return;
		if (pendingInterrupt != NO_PENDING_INTERRUPT)
			seekInterrupt();
		while (!stopped && instruction + 4 <= scriptEnd) {
			const int16_t instructionTime = argument<int16_t>(instruction);
			const uint8_t opcode = file->data[instruction + 2];
			const uint8_t length = file->data[instruction + 3];
			const size_t args = instruction + 4;
			if (instructionTime > time || instruction + 4 + length > scriptEnd)
				break;
			switch (opcode) {
			case 0:
				visible = false;
				stopped = true;
				break;
			case 1:
				sprite =
				    file->resolveSprite(length >= 4 ? argument<int32_t>(args)
				                                    : argument<int16_t>(args));
				visible = true;
				break;
			case 2:
				scaleX = argument<float>(args);
				scaleY = argument<float>(args + 4);
				break;
			case 3:
				color = (color & 0x00ffffff) |
				        ((argument<uint32_t>(args) & 0xff) << 24);
				break;
			case 4:
				color = (color & 0xff000000) |
				        (argument<uint32_t>(args) & 0xffffff);
				break;
			case 5: {
				const uint32_t target = length >= 4 ? argument<uint32_t>(args)
				                                    : argument<uint16_t>(args);
				instruction = scriptBegin + target;
				time = argument<int16_t>(instruction);
				continue;
			}
			case 7:
				scaleX = -scaleX;
				break;
			case 8:
				scaleY = -scaleY;
				break;
			case 9:
				rotationX = argument<float>(args);
				rotationY = argument<float>(args + 4);
				rotation = argument<float>(args + 8);
				break;
			case 10:
				angleVelocityX = argument<float>(args);
				angleVelocityY = argument<float>(args + 4);
				angleVelocity = argument<float>(args + 8);
				break;
			case 11:
				scaleVelocityX = argument<float>(args);
				scaleVelocityY = argument<float>(args + 4);
				scaleDuration = 0;
				break;
			case 12:
				alphaInitial = static_cast<uint8_t>(color >> 24);
				alphaFinal = static_cast<uint8_t>(argument<uint32_t>(args));
				alphaDuration = static_cast<int>(argument<uint32_t>(args + 4));
				alphaFrame = 0;
				break;
			case 13:
				additive = true;
				break;
			case 14:
				additive = false;
				break;
			case 15:
				stopped = true;
				break;
			case 16:
				if (length >= 8) {
					const int baseSprite = argument<int32_t>(args);
					const int spriteCount = argument<int32_t>(args + 4);
					const int spriteOffset =
					    spriteCount > 0 ? std::rand() % spriteCount : 0;
					sprite = file->resolveSprite(baseSprite + spriteOffset);
				} else {
					sprite = file->resolveSprite(argument<int32_t>(args));
				}
				visible = true;
				break;
			case 17:
				if (useOffset) {
					offsetX = argument<float>(args);
					offsetY = argument<float>(args + 4);
				} else {
					x = argument<float>(args);
					y = argument<float>(args + 4);
				}
				break;
			case 18:
			case 19:
			case 20:
				positionMode = opcode - 18;
				positionInitialX = useOffset ? offsetX : x;
				positionInitialY = useOffset ? offsetY : y;
				positionFinalX = argument<float>(args);
				positionFinalY = argument<float>(args + 4);
				positionDuration =
				    static_cast<int>(argument<uint32_t>(args + 12));
				positionFrame = 0;
				break;
			case 21:
				stopped = true;
				break;
			case 23:
				topLeft = true;
				break;
			case 24:
				visible = false;
				stopped = true;
				break;
			case 25:
				useOffset = argument<uint32_t>(args) != 0;
				break;
			case 26:
				autoRotate = argument<uint32_t>(args) != 0;
				break;
			case 27:
				uvScrollX = argument<float>(args);
				uvOffsetX = std::remainder(uvOffsetX + uvScrollX, 1.0f);
				break;
			case 28:
				uvScrollY = argument<float>(args);
				uvOffsetY = std::remainder(uvOffsetY + uvScrollY, 1.0f);
				break;
			case 29:
				visible = argument<uint32_t>(args) != 0;
				break;
			case 30:
				scaleInitialX = scaleX;
				scaleInitialY = scaleY;
				scaleFinalX = argument<float>(args);
				scaleFinalY = argument<float>(args + 4);
				scaleDuration = argument<uint16_t>(args + 8);
				scaleFrame = 0;
				break;
			default:
				break;
			}
			instruction += 4 + length;
		}

		rotationX += angleVelocityX;
		rotationY += angleVelocityY;
		rotation += angleVelocity;
		if (scaleDuration > 0) {
			const float progress = std::min(
			    1.0f, ++scaleFrame / static_cast<float>(scaleDuration));
			scaleX = std::lerp(scaleInitialX, scaleFinalX, progress);
			scaleY = std::lerp(scaleInitialY, scaleFinalY, progress);
			if (scaleFrame >= scaleDuration)
				scaleDuration = 0;
		} else {
			scaleX += scaleVelocityX;
			scaleY += scaleVelocityY;
		}
		if (alphaDuration > 0) {
			const float progress = std::min(
			    1.0f, ++alphaFrame / static_cast<float>(alphaDuration));
			const auto alpha = static_cast<uint8_t>(
			    std::lerp(static_cast<float>(alphaInitial),
			              static_cast<float>(alphaFinal), progress));
			color = (color & 0x00ffffff) | (static_cast<uint32_t>(alpha) << 24);
			if (alphaFrame >= alphaDuration)
				alphaDuration = 0;
		}
		if (positionDuration > 0) {
			float progress = std::min(
			    1.0f, positionFrame / static_cast<float>(positionDuration));
			if (positionMode == 1)
				progress = 1.0f - (1.0f - progress) * (1.0f - progress);
			else if (positionMode == 2)
				progress = 1.0f - std::pow(1.0f - progress, 4.0f);
			const float nextX =
			    std::lerp(positionInitialX, positionFinalX, progress);
			const float nextY =
			    std::lerp(positionInitialY, positionFinalY, progress);
			if (useOffset) {
				offsetX = nextX;
				offsetY = nextY;
			} else {
				x = nextX;
				y = nextY;
			}
			if (positionFrame++ >= positionDuration)
				positionDuration = 0;
		}
		if (!stopped)
			++time;
	}
};

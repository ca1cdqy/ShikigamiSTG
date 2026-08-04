#pragma once

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "th06_menu_anm.h"

struct TH06AnmFrame {
	int sprite = -1;
	int startFrame = 0;
	int endFrame = 1;
	size_t instructionOffset = 0;
	bool flipX = false;
};

struct TH06AnmScript {
	std::vector<TH06AnmFrame> frames;
	size_t loopFrame = 0;
	bool looping = false;

	[[nodiscard]] bool empty() const { return frames.empty(); }
};

inline TH06AnmScript loadTH06AnmScript(shiki::asset::AssetStore *assets,
                                       std::string_view atlas, int scriptId) {
	TH06MenuAnmFile source;
	if (!source.load(assets, atlas))
		return {};
	const auto found = source.scripts.find(scriptId);
	if (found == source.scripts.end())
		return {};
	const auto &data = source.data;
	const size_t scriptOffset = found->second.begin;
	const size_t scriptLimit = found->second.end;

	TH06AnmScript result;
	size_t offset = scriptOffset;
	size_t jumpTarget = 0;
	int jumpFrame = -1;
	bool flipX = false;
	while (offset + 4 <= data.size() && offset < scriptLimit) {
		int16_t time = 0;
		source.read(offset, time);
		const uint8_t opcode = data[offset + 2];
		const uint8_t length = data[offset + 3];
		if ((opcode == 0 && time == 0) || offset + 4 + length > data.size() ||
		    offset + 4 + length > scriptLimit)
			break;
		const size_t relativeOffset = offset - scriptOffset;
		if (opcode == 1 && length >= 2) {
			int32_t sprite = 0;
			if (length >= 4)
				source.read(offset + 4, sprite);
			else {
				int16_t shortSprite = 0;
				source.read(offset + 4, shortSprite);
				sprite = shortSprite;
			}
			result.frames.push_back({sprite, static_cast<int>(time),
			                         static_cast<int>(time) + 1, relativeOffset,
			                         flipX});
		} else if (opcode == 5 && length >= 2) {
			uint32_t target = 0;
			if (length >= 4)
				source.read(offset + 4, target);
			else {
				uint16_t shortTarget = 0;
				source.read(offset + 4, shortTarget);
				target = shortTarget;
			}
			jumpTarget = target;
			jumpFrame = time;
			result.looping = true;
			break;
		} else if (opcode == 7) {
			flipX = !flipX;
		} else if (opcode == 0 || opcode == 15 || opcode == 21 ||
		           opcode == 24) {
			jumpFrame = time;
			break;
		}
		offset += 4 + length;
	}

	for (size_t index = 0; index < result.frames.size(); ++index) {
		const int next = index + 1 < result.frames.size()
		                     ? result.frames[index + 1].startFrame
		                     : jumpFrame;
		result.frames[index].endFrame =
		    next > result.frames[index].startFrame
		        ? next
		        : result.frames[index].startFrame + 1;
		if (result.frames[index].instructionOffset == jumpTarget)
			result.loopFrame = index;
	}
	return result;
}

inline int sampleTH06AnmScript(const TH06AnmScript &script,
                               float elapsedFrames) {
	if (script.frames.empty())
		return -1;
	const auto &last = script.frames.back();
	const auto &loop = script.frames[script.loopFrame];
	if (script.looping && elapsedFrames >= last.endFrame &&
	    last.endFrame > loop.startFrame) {
		const float loopLength =
		    static_cast<float>(last.endFrame - loop.startFrame);
		elapsedFrames = loop.startFrame +
		                std::fmod(elapsedFrames - loop.startFrame, loopLength);
	}
	for (auto it = script.frames.rbegin(); it != script.frames.rend(); ++it)
		if (elapsedFrames >= it->startFrame)
			return it->sprite;
	return script.frames.front().sprite;
}

inline const TH06AnmFrame *sampleTH06AnmFrame(const TH06AnmScript &script,
                                              float elapsedFrames) {
	if (script.frames.empty())
		return nullptr;
	const auto &last = script.frames.back();
	const auto &loop = script.frames[script.loopFrame];
	if (script.looping && elapsedFrames >= last.endFrame &&
	    last.endFrame > loop.startFrame) {
		const float loopLength =
		    static_cast<float>(last.endFrame - loop.startFrame);
		elapsedFrames = loop.startFrame +
		                std::fmod(elapsedFrames - loop.startFrame, loopLength);
	}
	for (auto it = script.frames.rbegin(); it != script.frames.rend(); ++it)
		if (elapsedFrames >= it->startFrame)
			return &*it;
	return &script.frames.front();
}

#pragma once

#include "th06_effect_color.h"
#include "th06_menu_anm.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <shiki/render/renderer.h>
#include <shiki/render/sprite.h>
#include <shiki/render/texture.h>
#include <shiki/resource/resource_manager.h>
#include <string>
#include <string_view>
#include <vector>

class TH06EffectManager {
  public:
	struct Effect {
		int id = 0;
		int age = 0;
		int duration = 1;
		float x = 0.0f;
		float y = 0.0f;
		float originX = 0.0f;
		float originY = 0.0f;
		float vx = 0.0f;
		float vy = 0.0f;
		float ax = 0.0f;
		float ay = 0.0f;
		float directionX = 0.0f;
		float directionY = 0.0f;
		TH06MenuAnmVm vm;
	};

	TH06EffectManager() = default;

	void setResourceManager(const shiki::ResourceManager *resources) {
		resources_ = resources;
		loadAnmFiles();
	}

	void clear() { effects_.clear(); }

	void spawn(int effectId, float x, float y, int count = 1,
	           uint32_t packedColor = 0xffffffff) {
		if (effectId < 0 || effectId >= static_cast<int>(SCRIPT_IDS.size()) ||
		    count <= 0)
			return;
		count = std::min(count, 512 - static_cast<int>(effects_.size()));
		for (int index = 0; index < count; ++index) {
			Effect effect;
			effect.id = effectId;
			effect.x = effect.originX = x;
			effect.y = effect.originY = y;
			effect.duration = FALLBACK_DURATIONS[static_cast<size_t>(effectId)];
			const auto file =
			    effectId == 16 ? stageEffectAnm_ : bulletEffectAnm_;
			if (!effect.vm.initialize(
			        file, SCRIPT_IDS[static_cast<size_t>(effectId)]))
				continue;
			// EffectManager::SpawnParticles overwrites the script's current
			// color immediately after SetAndExecuteScriptIdx.
			effect.vm.color = packedColor;
			effect.vm.visible = true;
			configureMotion(effect);
			effects_.push_back(std::move(effect));
		}
	}

	void spawnMoving(int effectId, float x, float y, uint32_t packedColor,
	                 float vx, float vy, float ax, float ay) {
		const size_t previousSize = effects_.size();
		spawn(effectId, x, y, 1, packedColor);
		if (effects_.size() == previousSize)
			return;
		auto &effect = effects_.back();
		effect.vx = vx;
		effect.vy = vy;
		effect.ax = ax;
		effect.ay = ay;
	}

	void updateTick() {
		for (auto &effect : effects_) {
			updateMotion(effect);
			effect.vm.tick();
			++effect.age;
		}
		std::erase_if(effects_, [](const Effect &effect) {
			return !effect.vm.visible && effect.vm.stopped;
		});
	}

	void render(shiki::Renderer &renderer) const {
		if (!resources_)
			return;
		for (const auto &effect : effects_) {
			if (!effect.vm.visible || !effect.vm.file || effect.vm.sprite < 0)
				continue;
			auto texture = resources_->getSpriteTexture(effect.vm.file->atlas,
			                                            effect.vm.sprite);
			if (!texture || !texture->isValid())
				continue;
			shiki::Sprite sprite(texture);
			const float width = static_cast<float>(texture->getWidth());
			const float height = static_cast<float>(texture->getHeight());
			sprite.setSourceRect({0.0f, 0.0f, width, height});
			sprite.setOrigin(effect.vm.topLeft
			                     ? shiki::Vec2{0.0f, 0.0f}
			                     : shiki::Vec2{width * 0.5f, height * 0.5f});
			sprite.setPosition(effect.x + effect.vm.offsetX,
			                   effect.y + effect.vm.offsetY);
			sprite.setScale(effect.vm.scaleX, effect.vm.scaleY);
			sprite.setRotation(-effect.vm.rotation * 180.0f /
			                   std::numbers::pi_v<float>);
			const uint32_t displayColor =
			    effect.vm.additive
			        ? th06AdditiveEffectDisplayColor(effect.vm.color)
			        : effect.vm.color;
			sprite.setColor({((displayColor >> 16) & 0xff) / 255.0f,
			                 ((displayColor >> 8) & 0xff) / 255.0f,
			                 (displayColor & 0xff) / 255.0f,
			                 ((displayColor >> 24) & 0xff) / 255.0f});
			sprite.setBlendMode(effect.vm.additive ? shiki::BlendMode::Add
			                                       : shiki::BlendMode::Alpha);
			renderer.drawSprite(sprite, 0.2f, true);
		}
	}

	[[nodiscard]] const std::vector<Effect> &effects() const {
		return effects_;
	}

  private:
	static constexpr std::array<int, 20> SCRIPT_IDS = {
	    3, 4, 5, 6, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 18, 18, 0, 7, 8, 19};
	static constexpr std::array<int, 20> FALLBACK_DURATIONS = {
	    20, 20, 40, 40,    30,    30,    30,    30, 30,  30,
	    30, 30, 40, 36000, 36000, 36000, 36000, 60, 240, 120};

	static std::shared_ptr<TH06MenuAnmFile>
	loadAnm(const shiki::ResourceManager *resources, std::string_view atlas) {
		auto file = std::make_shared<TH06MenuAnmFile>();
		return resources && file->load(resources->getAssetStore(), atlas)
		           ? file
		           : nullptr;
	}

	void loadAnmFiles() {
		bulletEffectAnm_ = loadAnm(resources_, "etama4");
		// Extra stage follows EffectManager::AddedCallback case 7 and loads
		// eff04.anm for effect 16.
		stageEffectAnm_ = loadAnm(resources_, "eff04");
	}

	[[nodiscard]] float randomUnit() {
		rng_ = rng_ * 0x343fdu + 0x269ec3u;
		return static_cast<float>((rng_ >> 16) & 0x7fff) / 32768.0f;
	}

	void randomDirection(Effect &effect) {
		const float angle = randomUnit() * 2.0f * std::numbers::pi_v<float> -
		                    std::numbers::pi_v<float>;
		effect.directionX = std::cos(angle);
		effect.directionY = std::sin(angle);
	}

	void randomSplash(Effect &effect, bool big) {
		const float factor = big ? 4.0f / 33.0f : 1.0f / 12.0f;
		const float decelerationFrames = big ? 20.0f : 19.0f;
		effect.vx = (randomUnit() * 256.0f - 128.0f) * factor;
		effect.vy = (randomUnit() * 256.0f - 128.0f) * factor;
		effect.ax = -effect.vx / decelerationFrames;
		effect.ay = -effect.vy / decelerationFrames;
	}

	void configureMotion(Effect &effect) {
		if (effect.id == 3)
			randomSplash(effect, true);
		else if (effect.id >= 4 && effect.id <= 11)
			randomSplash(effect, false);
		else if (effect.id == 17 || effect.id == 18)
			randomDirection(effect);
	}

	static void updateMotion(Effect &effect) {
		if (effect.id >= 3 && effect.id <= 11) {
			effect.x += effect.vx;
			effect.y += effect.vy;
			effect.vx += effect.ax;
			effect.vy += effect.ay;
		} else if (effect.id == 17 || effect.id == 18) {
			const float length = effect.id == 17 ? 60.0f : 240.0f;
			const float distance =
			    256.0f - static_cast<float>(effect.age) * 256.0f / length;
			effect.x = effect.originX + distance * effect.directionX;
			effect.y = effect.originY + distance * effect.directionY;
		} else if (effect.id == 19) {
			effect.x += effect.vx;
			effect.y += effect.vy;
			effect.vx += effect.ax;
			effect.vy += effect.ay;
		}
	}

	const shiki::ResourceManager *resources_ = nullptr;
	std::shared_ptr<TH06MenuAnmFile> bulletEffectAnm_;
	std::shared_ptr<TH06MenuAnmFile> stageEffectAnm_;
	std::vector<Effect> effects_;
	uint32_t rng_ = 0x1337u;
};

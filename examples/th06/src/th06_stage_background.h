#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <shiki/core/types.h>
#include <string>
#include <string_view>
#include <vector>

#include "th06_menu_anm.h"

namespace shiki {
class Renderer;
class ResourceManager;
class Texture;
} // namespace shiki

class TH06StageBackground {
  public:
	bool load(shiki::ResourceManager &resources, std::string_view assetName);
	void update();
	void render(shiki::Renderer &renderer, float stageFrame) const;
	[[nodiscard]] bool isLoaded() const { return loaded_; }
	[[nodiscard]] const std::string &name() const { return name_; }
	[[nodiscard]] const std::array<std::string, 4> &songNames() const {
		return songNames_;
	}

  private:
	struct Quad {
		int script = 0;
		shiki::Vec3 position;
		shiki::Vec2 size;
	};
	struct Object {
		int zLevel = 0;
		shiki::Vec3 position;
		std::vector<Quad> quads;
	};
	struct Instance {
		int objectId = -1;
		shiki::Vec3 position;
	};
	struct Instruction {
		int frame = 0;
		int opcode = 0;
		int32_t arg0 = 0;
		shiki::Vec3 vector;
	};
	struct Fog {
		shiki::Color color{0.0f, 0.0f, 0.0f, 1.0f};
		float nearPlane = 200.0f;
		float farPlane = 500.0f;
	};
	struct CameraState {
		shiki::Vec3 position;
		shiki::Vec3 facing{0.0f, 0.0f, 1.0f};
		Fog fog;
	};
	struct ScriptVisual {
		std::shared_ptr<shiki::Texture> texture;
		shiki::Vec2 scale{1.0f, 1.0f};
		shiki::Vec3 rotation;
		shiki::Vec2 uvOffset;
		float alpha{1.0f};
		float fadeTargetAlpha{1.0f};
		int fadeStartFrame{};
		int fadeDuration{};
		bool additive{};
		bool topLeft{};
	};

	[[nodiscard]] CameraState evaluateCamera(float frame) const;

	std::vector<Object> objects_;
	std::vector<Instance> instances_;
	std::vector<Instruction> instructions_;
	std::vector<ScriptVisual> scriptVisuals_;
	std::shared_ptr<TH06MenuAnmFile> animationFile_;
	std::vector<TH06MenuAnmVm> scriptVms_;
	shiki::ResourceManager *resources_{};
	std::string name_;
	std::array<std::string, 4> songNames_;
	bool loaded_ = false;
};

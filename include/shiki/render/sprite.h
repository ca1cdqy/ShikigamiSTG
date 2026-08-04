#pragma once

#include <memory>
#include <optional>
#include <shiki/core/types.h>
#include <string>
#include <vector>

namespace shiki {

class Texture;

/** One texture region and its playback duration. */
struct AnimationFrame {
	Rect sourceRect; ///< Source rectangle in texture pixels.
	float duration;  ///< Playback duration in seconds.
};

/** Time-based sequence of sprite source rectangles. */
class Animation {
  public:
	/** Creates an unnamed animation with no frames. */
	Animation() = default;
	/** Creates an empty animation with a diagnostic name. */
	explicit Animation(const std::string &name);

	/** Appends a source rectangle and duration. */
	void addFrame(const Rect &sourceRect, float duration);
	/** Appends a complete frame value. */
	void addFrame(const AnimationFrame &frame);

	/** Controls whether playback wraps after the final frame. */
	void setLoop(bool loop) { isLoop_ = loop; }
	/** Reports whether playback wraps after the final frame. */
	[[nodiscard]] bool isLoop() const { return isLoop_; }

	/** Returns a copy of the frame at index. */
	[[nodiscard]] AnimationFrame getFrame(size_t index) const;
	/** Returns the number of stored frames. */
	[[nodiscard]] size_t getFrameCount() const { return frames_.size(); }

	/** Starts or resumes playback. */
	void play();
	/** Stops playback and returns to the first frame. */
	void stop();
	/** Suspends playback at the current frame. */
	void pause();
	/** Returns to the first frame without changing loop configuration. */
	void reset();

	/** Advances playback by dt seconds. */
	void update(float dt);

	/** Returns the zero-based frame selected for rendering. */
	[[nodiscard]] size_t getCurrentFrameIndex() const { return currentFrame_; }
	/** Reports whether update() currently advances playback. */
	[[nodiscard]] bool isPlaying() const { return isPlaying_; }

  private:
	std::string name_;
	std::vector<AnimationFrame> frames_;
	size_t currentFrame_ = 0;
	float frameTimer_ = 0.0f;
	bool isPlaying_ = false;
	bool isLoop_ = true;
};

/**
 * Copyable render value containing a texture reference, source rectangle,
 * transform, color, blend mode, and optional animation state.
 *
 * Texture ownership is shared. Copying a sprite copies animation playback
 * state but continues to reference the same Texture object.
 */
class Sprite {
  public:
	/** Creates a visible sprite without a texture. */
	Sprite();
	/** Creates a visible sprite referencing texture. */
	explicit Sprite(std::shared_ptr<Texture> texture);
	/** Releases the shared texture reference. */
	~Sprite() = default;

	/** Moves sprite state and its shared texture reference. */
	Sprite(Sprite &&) = default;
	/** Replaces this sprite with moved state. */
	Sprite &operator=(Sprite &&) = default;
	/** Copies sprite state and shares the texture reference. */
	Sprite(const Sprite &) = default;
	/** Copies sprite state and shares the texture reference. */
	Sprite &operator=(const Sprite &) = default;

	/** Replaces the shared texture reference. */
	void setTexture(std::shared_ptr<Texture> texture);
	/** Returns the shared texture reference, which may be null. */
	[[nodiscard]] std::shared_ptr<Texture> getTexture() const {
		return texture_;
	}

	/** Sets the sampled rectangle in texture pixels. */
	void setSourceRect(const Rect &rect) { sourceRect_ = rect; }
	/** Returns the sampled rectangle in texture pixels. */
	[[nodiscard]] const Rect &getSourceRect() const { return sourceRect_; }

	/** Sets position in the renderer's active coordinate space. */
	void setPosition(const Vec2 &pos) { position_ = pos; }
	/** Sets position in the renderer's active coordinate space. */
	void setPosition(float x, float y) { position_ = Vec2(x, y); }
	/** Returns position in the renderer's active coordinate space. */
	[[nodiscard]] const Vec2 &getPosition() const { return position_; }

	/** Sets clockwise rotation in degrees. */
	void setRotation(float angle) { rotation_ = angle; }
	/** Returns clockwise rotation in degrees. */
	[[nodiscard]] float getRotation() const { return rotation_; }

	/** Sets independent horizontal and vertical scale factors. */
	void setScale(const Vec2 &scale) { scale_ = scale; }
	/** Sets independent horizontal and vertical scale factors. */
	void setScale(float x, float y) { scale_ = Vec2(x, y); }
	/** Returns the horizontal and vertical scale factors. */
	[[nodiscard]] const Vec2 &getScale() const { return scale_; }

	/** Sets the transform origin in source-rectangle pixels. */
	void setOrigin(const Vec2 &origin) { origin_ = origin; }
	/** Returns the transform origin in source-rectangle pixels. */
	[[nodiscard]] const Vec2 &getOrigin() const { return origin_; }

	/** Sets normalized RGBA color modulation. */
	void setColor(const Color &color) { color_ = color; }
	/** Returns normalized RGBA color modulation. */
	[[nodiscard]] const Color &getColor() const { return color_; }

	/** Sets the blend operation used by the renderer. */
	void setBlendMode(BlendMode mode) { blendMode_ = mode; }
	/** Returns the configured blend operation. */
	[[nodiscard]] BlendMode getBlendMode() const { return blendMode_; }

	/** Copies an animation and resets sprite playback to its copied state. */
	void setAnimation(const Animation &anim);
	/** Starts or resumes the assigned animation. */
	void playAnimation();
	/** Stops the assigned animation when present. */
	void stopAnimation();
	/** Advances the assigned animation by dt seconds. */
	void updateAnimation(float dt);
	/** Reports whether an animation is assigned. */
	[[nodiscard]] bool hasAnimation() const { return animation_.has_value(); }

	/** Controls whether render submission should include the sprite. */
	void setVisible(bool visible) { visible_ = visible; }
	/** Reports whether render submission should include the sprite. */
	[[nodiscard]] bool isVisible() const { return visible_; }

	/** Returns the matrix composed from origin, scale, rotation, and position.
	 */
	[[nodiscard]] Mat4 getTransform() const;

	/** Returns the axis-aligned bounds of the transformed source rectangle. */
	[[nodiscard]] Rect getBounds() const;

  private:
	std::shared_ptr<Texture> texture_;
	Rect sourceRect_;

	/// Transform
	Vec2 position_ = Vec2(0.0f, 0.0f);
	float rotation_ = 0.0f;
	Vec2 scale_ = Vec2(1.0f, 1.0f);
	Vec2 origin_ = Vec2(0.0f, 0.0f);

	/// Appearance
	Color color_ = Color(1.0f, 1.0f, 1.0f, 1.0f);
	BlendMode blendMode_ = BlendMode::Alpha;
	bool visible_ = true;

	/// Animation
	std::optional<Animation> animation_;
};

} // namespace shiki

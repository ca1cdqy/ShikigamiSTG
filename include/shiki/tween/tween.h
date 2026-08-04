#pragma once

#include <functional>
#include <memory>
#include <shiki/core/types.h>
#include <string>
#include <vector>

namespace shiki {

/** Selects the easing curve applied to interpolated values. */
enum class EaseType {
	Linear,       ///< Constant rate of change.
	InQuad,       ///< Accelerates using a quadratic curve.
	OutQuad,      ///< Decelerates using a quadratic curve.
	InOutQuad,    ///< Accelerates then decelerates with a quadratic curve.
	InCubic,      ///< Accelerates using a cubic curve.
	OutCubic,     ///< Decelerates using a cubic curve.
	InOutCubic,   ///< Accelerates then decelerates with a cubic curve.
	InQuart,      ///< Accelerates using a quartic curve.
	OutQuart,     ///< Decelerates using a quartic curve.
	InOutQuart,   ///< Accelerates then decelerates with a quartic curve.
	InSine,       ///< Accelerates using a sinusoidal curve.
	OutSine,      ///< Decelerates using a sinusoidal curve.
	InOutSine,    ///< Accelerates then decelerates with a sinusoidal curve.
	InExpo,       ///< Accelerates using an exponential curve.
	OutExpo,      ///< Decelerates using an exponential curve.
	InOutExpo,    ///< Accelerates then decelerates with an exponential curve.
	InCirc,       ///< Accelerates using a circular curve.
	OutCirc,      ///< Decelerates using a circular curve.
	InOutCirc,    ///< Accelerates then decelerates with a circular curve.
	InBack,       ///< Overshoots slightly before settling at the start.
	OutBack,      ///< Overshoots slightly before settling at the end.
	InOutBack,    ///< Overshoots at both ends.
	InElastic,    ///< Oscillates at the start like a spring.
	OutElastic,   ///< Oscillates at the end like a spring.
	InOutElastic, ///< Oscillates at both ends like a spring.
	InBounce,     ///< Bounces at the start.
	OutBounce,    ///< Bounces at the end.
	InOutBounce   ///< Bounces at both ends.
};

/**
 * Animates one float value over a duration using a configurable easing curve.
 *
 * Create an instance with Tween::to() or Tween::fromTo(), configure it with
 * the fluent setters, then call start() and advance it with update() each
 * frame. Completed tweens do not remove themselves; discard or restart them.
 *
 * Tweens are non-copyable because they own callback state.
 */
class Tween {
  public:
	using ValueCallback = std::function<void(float)>;          ///< Called each frame with the current value.
	using CompletionCallback = std::function<void()>;           ///< Called once when the tween finishes.

	/** Creates a default-initialized, inactive tween. */
	Tween() = default;
	~Tween() = default;

	/** Tweens cannot be copied because they own callbacks. */
	Tween(const Tween &) = delete;
	/** Tweens cannot be copy-assigned. */
	Tween &operator=(const Tween &) = delete;

	/** Transfers all tween state and callbacks. */
	Tween(Tween &&) noexcept = default;
	/** Replaces this tween with moved state. */
	Tween &operator=(Tween &&) noexcept = default;

	/** Creates a tween that interpolates from its current value to to over duration seconds. */
	static Tween to(float from, float to, float duration,
	                EaseType ease = EaseType::Linear);
	/** Creates a tween that interpolates from from to to over duration seconds. */
	static Tween fromTo(float from, float to, float duration,
	                    EaseType ease = EaseType::Linear);

	/** Sets the delay in seconds before playback begins. */
	Tween &setDelay(float delay) {
		delay_ = delay;
		return *this;
	}
	/** Sets the easing curve applied during interpolation. */
	Tween &setEase(EaseType ease) {
		easeType_ = ease;
		return *this;
	}
	/** Sets the number of times the tween repeats; zero means play once. */
	Tween &setLoop(int count) {
		loopCount_ = count;
		return *this;
	}
	/** Enables ping-pong mode, reversing direction on each repeat. */
	Tween &setPingPong(bool pingPong) {
		pingPong_ = pingPong;
		return *this;
	}
	/** Replaces the per-frame value callback. */
	Tween &onUpdate(ValueCallback callback) {
		onUpdate_ = std::move(callback);
		return *this;
	}
	/** Replaces the completion callback. */
	Tween &onComplete(CompletionCallback callback) {
		onComplete_ = std::move(callback);
		return *this;
	}

	/** Advances elapsed time and invokes the value callback when running. */
	void update(float dt);

	/** Returns whether update() currently advances playback. */
	[[nodiscard]] bool isRunning() const { return isRunning_; }
	/** Returns whether the tween has reached its end state. */
	[[nodiscard]] bool isCompleted() const { return isCompleted_; }
	/** Returns normalized elapsed progress in the range [0.0, 1.0]. */
	[[nodiscard]] float getProgress() const { return progress_; }
	/** Returns the eased interpolated value for the current progress. */
	[[nodiscard]] float getCurrentValue() const { return currentValue_; }

	/** Starts playback from the beginning. */
	void start();
	/** Suspends playback without resetting elapsed time. */
	void pause() { isRunning_ = false; }
	/** Resumes playback from the current elapsed time. */
	void resume() { isRunning_ = true; }
	/** Stops playback and marks the tween as completed. */
	void stop();
	/** Resets elapsed time and starts playback from the beginning. */
	void restart();

  private:
	float from_ = 0.0f;
	float to_ = 0.0f;
	float duration_ = 1.0f;
	float delay_ = 0.0f;
	float elapsed_ = 0.0f;
	float progress_ = 0.0f;
	float currentValue_ = 0.0f;
	EaseType easeType_ = EaseType::Linear;
	int loopCount_ = 0;
	int currentLoop_ = 0;
	bool pingPong_ = false;
	bool isRunning_ = false;
	bool isCompleted_ = false;
	bool isReversed_ = false;

	ValueCallback onUpdate_;
	CompletionCallback onComplete_;

	/// Easing calculation
	[[nodiscard]] float ease(float t) const;
	[[nodiscard]] float easeInQuad(float t) const;
	[[nodiscard]] float easeOutQuad(float t) const;
	[[nodiscard]] float easeInOutQuad(float t) const;
	[[nodiscard]] float easeInCubic(float t) const;
	[[nodiscard]] float easeOutCubic(float t) const;
	[[nodiscard]] float easeInOutCubic(float t) const;
	[[nodiscard]] float easeInSine(float t) const;
	[[nodiscard]] float easeOutSine(float t) const;
	[[nodiscard]] float easeInOutSine(float t) const;
	[[nodiscard]] float easeInExpo(float t) const;
	[[nodiscard]] float easeOutExpo(float t) const;
	[[nodiscard]] float easeInOutExpo(float t) const;
	[[nodiscard]] float easeInBack(float t) const;
	[[nodiscard]] float easeOutBack(float t) const;
	[[nodiscard]] float easeInOutBack(float t) const;
	[[nodiscard]] float easeInElastic(float t) const;
	[[nodiscard]] float easeOutElastic(float t) const;
	[[nodiscard]] float easeInOutElastic(float t) const;
	[[nodiscard]] float easeInBounce(float t) const;
	[[nodiscard]] float easeOutBounce(float t) const;
	[[nodiscard]] float easeInOutBounce(float t) const;
};

/**
 * Owns a collection of active Tween instances and advances them together.
 *
 * Completed tweens are removed automatically during update(). The manager
 * is non-copyable because it owns tween state.
 */
class TweenManager {
  public:
	/** Creates an empty tween manager. */
	TweenManager() = default;
	~TweenManager() = default;

	/** Tween managers cannot be copied because they own tween state. */
	TweenManager(const TweenManager &) = delete;
	/** Tween managers cannot be copy-assigned. */
	TweenManager &operator=(const TweenManager &) = delete;

	/** Transfers owned tween collection. */
	TweenManager(TweenManager &&) noexcept = default;
	/** Replaces this manager by moving the tween collection. */
	TweenManager &operator=(TweenManager &&) noexcept = default;

	/** Takes ownership of a tween and starts advancing it. */
	void addTween(std::unique_ptr<Tween> tween);

	/** Advances all owned tweens by dt seconds and removes completed ones. */
	void update(float dt);

	/** Stops and destroys all owned tweens. */
	void clear();

	/** Returns the number of tweens currently owned by this manager. */
	[[nodiscard]] size_t getActiveCount() const { return tweens_.size(); }

  private:
	std::vector<std::unique_ptr<Tween>> tweens_;
};

} // namespace shiki

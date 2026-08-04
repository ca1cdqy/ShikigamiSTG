#include <algorithm>
#include <cmath>
#include <shiki/tween/tween.h>

namespace shiki {

Tween Tween::to(float from, float to, float duration, EaseType ease) {
	Tween tween;
	tween.from_ = from;
	tween.to_ = to;
	tween.duration_ = duration;
	tween.easeType_ = ease;
	tween.currentValue_ = from;
	return tween;
}

Tween Tween::fromTo(float from, float to, float duration, EaseType ease) {
	return Tween::to(from, to, duration, ease);
}

void Tween::update(float dt) {
	if (!isRunning_ || isCompleted_)
		return;

	if (delay_ > 0.0f) {
		delay_ -= dt;
		if (delay_ > 0.0f)
			return;
		dt = -delay_;
		delay_ = 0.0f;
	}

	elapsed_ += dt;
	progress_ = std::clamp(elapsed_ / duration_, 0.0f, 1.0f);

	float easedProgress = ease(progress_);
	currentValue_ = from_ + (to_ - from_) * easedProgress;

	if (onUpdate_) {
		onUpdate_(currentValue_);
	}

	if (progress_ >= 1.0f) {
		if (loopCount_ == 0 || currentLoop_ >= loopCount_) {
			isCompleted_ = true;
			isRunning_ = false;
			if (onComplete_) {
				onComplete_();
			}
		} else {
			currentLoop_++;
			if (pingPong_) {
				isReversed_ = !isReversed_;
				std::swap(from_, to_);
			}
			elapsed_ = 0.0f;
			progress_ = 0.0f;
		}
	}
}

void Tween::start() {
	isRunning_ = true;
	isCompleted_ = false;
	elapsed_ = 0.0f;
	progress_ = 0.0f;
	currentLoop_ = 0;
	currentValue_ = from_;
}

void Tween::stop() { isRunning_ = false; }

void Tween::restart() { start(); }

float Tween::ease(float t) const {
	switch (easeType_) {
	case EaseType::Linear:
		return t;
	case EaseType::InQuad:
		return easeInQuad(t);
	case EaseType::OutQuad:
		return easeOutQuad(t);
	case EaseType::InOutQuad:
		return easeInOutQuad(t);
	case EaseType::InCubic:
		return easeInCubic(t);
	case EaseType::OutCubic:
		return easeOutCubic(t);
	case EaseType::InOutCubic:
		return easeInOutCubic(t);
	case EaseType::InSine:
		return easeInSine(t);
	case EaseType::OutSine:
		return easeOutSine(t);
	case EaseType::InOutSine:
		return easeInOutSine(t);
	case EaseType::InExpo:
		return easeInExpo(t);
	case EaseType::OutExpo:
		return easeOutExpo(t);
	case EaseType::InOutExpo:
		return easeInOutExpo(t);
	case EaseType::InBack:
		return easeInBack(t);
	case EaseType::OutBack:
		return easeOutBack(t);
	case EaseType::InOutBack:
		return easeInOutBack(t);
	case EaseType::InElastic:
		return easeInElastic(t);
	case EaseType::OutElastic:
		return easeOutElastic(t);
	case EaseType::InOutElastic:
		return easeInOutElastic(t);
	case EaseType::InBounce:
		return easeInBounce(t);
	case EaseType::OutBounce:
		return easeOutBounce(t);
	case EaseType::InOutBounce:
		return easeInOutBounce(t);
	default:
		return t;
	}
}

float Tween::easeInQuad(float t) const { return t * t; }
float Tween::easeOutQuad(float t) const {
	return 1.0f - (1.0f - t) * (1.0f - t);
}
float Tween::easeInOutQuad(float t) const {
	return t < 0.5f ? 2.0f * t * t
	                : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f;
}

float Tween::easeInCubic(float t) const { return t * t * t; }
float Tween::easeOutCubic(float t) const {
	return 1.0f - std::pow(1.0f - t, 3.0f);
}
float Tween::easeInOutCubic(float t) const {
	return t < 0.5f ? 4.0f * t * t * t
	                : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
}

float Tween::easeInSine(float t) const {
	return 1.0f - std::cos(t * 3.14159265359f / 2.0f);
}
float Tween::easeOutSine(float t) const {
	return std::sin(t * 3.14159265359f / 2.0f);
}
float Tween::easeInOutSine(float t) const {
	return -(std::cos(3.14159265359f * t) - 1.0f) / 2.0f;
}

float Tween::easeInExpo(float t) const {
	return t == 0.0f ? 0.0f : std::pow(2.0f, 10.0f * (t - 1.0f));
}
float Tween::easeOutExpo(float t) const {
	return t == 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
}
float Tween::easeInOutExpo(float t) const {
	if (t == 0.0f)
		return 0.0f;
	if (t == 1.0f)
		return 1.0f;
	return t < 0.5f ? std::pow(2.0f, 20.0f * t - 10.0f) / 2.0f
	                : (2.0f - std::pow(2.0f, -20.0f * t + 10.0f)) / 2.0f;
}

float Tween::easeInBack(float t) const {
	const float c1 = 1.70158f;
	const float c3 = c1 + 1.0f;
	return c3 * t * t * t - c1 * t * t;
}

float Tween::easeOutBack(float t) const {
	const float c1 = 1.70158f;
	const float c3 = c1 + 1.0f;
	return 1.0f + c3 * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f);
}

float Tween::easeInOutBack(float t) const {
	const float c1 = 1.70158f;
	const float c2 = c1 * 1.525f;
	return t < 0.5f
	           ? (std::pow(2.0f * t, 2.0f) * ((c2 + 1.0f) * 2.0f * t - c2)) /
	                 2.0f
	           : (std::pow(2.0f * t - 2.0f, 2.0f) *
	                  ((c2 + 1.0f) * (t * 2.0f - 2.0f) + c2) +
	              2.0f) /
	                 2.0f;
}

float Tween::easeInElastic(float t) const {
	const float c4 = (2.0f * 3.14159265359f) / 3.0f;
	if (t == 0.0f)
		return 0.0f;
	if (t == 1.0f)
		return 1.0f;
	return -std::pow(2.0f, 10.0f * t - 10.0f) *
	       std::sin((t * 10.0f - 10.75f) * c4);
}

float Tween::easeOutElastic(float t) const {
	const float c4 = (2.0f * 3.14159265359f) / 3.0f;
	if (t == 0.0f)
		return 0.0f;
	if (t == 1.0f)
		return 1.0f;
	return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) +
	       1.0f;
}

float Tween::easeInOutElastic(float t) const {
	const float c5 = (2.0f * 3.14159265359f) / 4.5f;
	if (t == 0.0f)
		return 0.0f;
	if (t == 1.0f)
		return 1.0f;
	if (t < 0.5f) {
		return -(std::pow(2.0f, 20.0f * t - 10.0f) *
		         std::sin((20.0f * t - 11.125f) * c5)) /
		       2.0f;
	}
	return (std::pow(2.0f, -20.0f * t + 10.0f) *
	        std::sin((20.0f * t - 11.125f) * c5)) /
	           2.0f +
	       1.0f;
}

float Tween::easeInBounce(float t) const {
	return 1.0f - easeOutBounce(1.0f - t);
}

float Tween::easeOutBounce(float t) const {
	const float n1 = 7.5625f;
	const float d1 = 2.75f;
	if (t < 1.0f / d1) {
		return n1 * t * t;
	} else if (t < 2.0f / d1) {
		t -= 1.5f / d1;
		return n1 * t * t + 0.75f;
	} else if (t < 2.5f / d1) {
		t -= 2.25f / d1;
		return n1 * t * t + 0.9375f;
	} else {
		t -= 2.625f / d1;
		return n1 * t * t + 0.984375f;
	}
}

float Tween::easeInOutBounce(float t) const {
	return t < 0.5f ? (1.0f - easeOutBounce(1.0f - 2.0f * t)) / 2.0f
	                : (1.0f + easeOutBounce(2.0f * t - 1.0f)) / 2.0f;
}


void TweenManager::addTween(std::unique_ptr<Tween> tween) {
	if (tween) {
		tween->start();
		tweens_.push_back(std::move(tween));
	}
}

void TweenManager::update(float dt) {
	for (auto &tween : tweens_) {
		if (tween->isRunning()) {
			tween->update(dt);
		}
	}

	tweens_.erase(std::remove_if(tweens_.begin(), tweens_.end(),
	                             [](const std::unique_ptr<Tween> &tween) {
		                             return tween->isCompleted();
	                             }),
	              tweens_.end());
}

void TweenManager::clear() { tweens_.clear(); }

} // namespace shiki

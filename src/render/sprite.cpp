#include <cmath>
#include <shiki/core/types.h>
#include <shiki/render/sprite.h>
#include <shiki/render/texture.h>

namespace shiki {

Animation::Animation(const std::string &name) : name_(name) {}

void Animation::addFrame(const Rect &sourceRect, float duration) {
  frames_.push_back({sourceRect, duration});
}

void Animation::addFrame(const AnimationFrame &frame) {
  frames_.push_back(frame);
}

AnimationFrame Animation::getFrame(size_t index) const {
  if (index < frames_.size()) {
    return frames_[index];
  }
  return {};
}

void Animation::play() {
  isPlaying_ = true;
  currentFrame_ = 0;
  frameTimer_ = 0.0f;
}

void Animation::stop() {
  isPlaying_ = false;
  currentFrame_ = 0;
  frameTimer_ = 0.0f;
}

void Animation::pause() { isPlaying_ = false; }

void Animation::reset() {
  currentFrame_ = 0;
  frameTimer_ = 0.0f;
}

void Animation::update(float dt) {
  if (!isPlaying_ || frames_.empty()) {
    return;
  }

  frameTimer_ += dt;

  while (currentFrame_ < frames_.size() &&
         frameTimer_ >= frames_[currentFrame_].duration) {
    frameTimer_ -= frames_[currentFrame_].duration;
    currentFrame_++;

    if (currentFrame_ >= frames_.size()) {
      if (isLoop_) {
        currentFrame_ = 0;
      } else {
        currentFrame_ = frames_.size() - 1;
        isPlaying_ = false;
        break;
      }
    }
  }
}

Sprite::Sprite() {
  sourceRect_ = Rect(0.0f, 0.0f, 32.0f, 32.0f);
}

Sprite::Sprite(std::shared_ptr<Texture> texture)
    : texture_(std::move(texture)) {
  if (texture_) {
    sourceRect_ = Rect(0.0f, 0.0f, static_cast<float>(texture_->getWidth()),
                       static_cast<float>(texture_->getHeight()));
  } else {
    sourceRect_ = Rect(0.0f, 0.0f, 32.0f, 32.0f);
  }
}

void Sprite::setTexture(std::shared_ptr<Texture> texture) {
  texture_ = std::move(texture);
}

void Sprite::setAnimation(const Animation &anim) { animation_ = anim; }

void Sprite::playAnimation() {
  if (animation_) {
    animation_->play();
  }
}

void Sprite::stopAnimation() {
  if (animation_) {
    animation_->stop();
  }
}

void Sprite::updateAnimation(float dt) {
  if (animation_) {
    animation_->update(dt);
  }
}

Mat4 Sprite::getTransform() const {
  Mat4 transform = Mat4(1.0f);

  float sx = scale_.x;
  float sy = scale_.y;

  float cosR = std::cos(rotation_ * 3.14159265f / 180.0f);
  float sinR = std::sin(rotation_ * 3.14159265f / 180.0f);

  // Keep the scaled and rotated origin anchored at position_. Subtracting
  // the unrotated origin makes long sprites (notably TH06 lasers) orbit
  // around a displaced point as their angle changes.
  float tx = position_.x - (cosR * sx * origin_.x - sinR * sy * origin_.y);
  float ty = position_.y - (sinR * sx * origin_.x + cosR * sy * origin_.y);

  transform.data[0] = cosR * sx;
  transform.data[1] = sinR * sx;
  transform.data[4] = -sinR * sy;
  transform.data[5] = cosR * sy;
  transform.data[12] = tx;
  transform.data[13] = ty;
  transform.data[15] = 1.0f;

  return transform;
}

Rect Sprite::getBounds() const {
  float halfWidth = sourceRect_.width * scale_.x / 2.0f;
  float halfHeight = sourceRect_.height * scale_.y / 2.0f;

  return Rect(position_.x - halfWidth, position_.y - halfHeight,
              sourceRect_.width * scale_.x, sourceRect_.height * scale_.y);
}

} // namespace shiki

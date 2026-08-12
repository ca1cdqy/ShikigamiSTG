#include <algorithm>
#include <array>
#include <cmath>
#include <shiki/render/renderer.h>
#include <shiki/render/sprite.h>
#include <shiki/render/texture.h>
#include <vector>

namespace shiki {
void Renderer::drawSprite(const Sprite &sprite, float zIndex,
                          bool playfieldSpace) {
	if (!sprite.isVisible() || vertices_.size() + 4 > MAX_VERTICES ||
	    indices_.size() + 6 > MAX_INDICES) {
		return;
	}

	Mat4 transform = sprite.getTransform();

	float w = sprite.getSourceRect().width;
	float h = sprite.getSourceRect().height;

	if (w <= 0.0f)
		w = 32.0f;
	if (h <= 0.0f)
		h = 32.0f;

	float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
	auto texture = sprite.getTexture();
	if (texture && texture->isValid()) {
		Rect sourceRect = sprite.getSourceRect();
		int texWidth = texture->getWidth();
		int texHeight = texture->getHeight();
		if (texWidth > 0 && texHeight > 0) {
			const float fTexWidth = static_cast<float>(texWidth);
			const float fTexHeight = static_cast<float>(texHeight);
			u0 = sourceRect.x / fTexWidth;
			v0 = sourceRect.y / fTexHeight;
			u1 = (sourceRect.x + sourceRect.width) / fTexWidth;
			v1 = (sourceRect.y + sourceRect.height) / fTexHeight;
		}
	}

	Color color = sprite.getColor();

	const float logicalWidth = projectionRight_ - projectionLeft_;
	const float logicalHeight = projectionBottom_ - projectionTop_;
	if (logicalWidth <= 0.0f || logicalHeight <= 0.0f)
		return;

	Vec2 positions[4] = {{0.0f, 0.0f}, {w, 0.0f}, {0.0f, h}, {w, h}};

	for (auto &pos : positions) {
		const float x = pos.x;
		const float y = pos.y;
		const float transformedX =
		    transform.data[0] * x + transform.data[4] * y + transform.data[12];
		const float transformedY =
		    transform.data[1] * x + transform.data[5] * y + transform.data[13];

		// TH06's logical canvas is 640x480. The 384x448 playfield is a viewport
		// inside that canvas, not the coordinate system for the whole window.
		if (playfieldSpace) {
			pos.x = transformedX / 384.0f * 2.0f - 1.0f;
			pos.y = 1.0f - transformedY / 448.0f * 2.0f;
		} else {
			pos.x =
			    (transformedX - projectionLeft_) / logicalWidth * 2.0f - 1.0f;
			pos.y =
			    1.0f - (transformedY - projectionTop_) / logicalHeight * 2.0f;
		}
	}

	uint16_t baseIndex = static_cast<uint16_t>(vertices_.size());

	vertices_.push_back({positions[0], color, {u0, v0}});
	vertices_.push_back({positions[1], color, {u1, v0}});
	vertices_.push_back({positions[2], color, {u0, v1}});
	vertices_.push_back({positions[3], color, {u1, v1}});

	indices_.push_back(baseIndex + 0);
	indices_.push_back(baseIndex + 1);
	indices_.push_back(baseIndex + 2);
	indices_.push_back(baseIndex + 1);
	indices_.push_back(baseIndex + 3);
	indices_.push_back(baseIndex + 2);

	void *gpuTex = nullptr;
	if (texture && texture->isValid()) {
		gpuTex = texture->getHandle();
	}

	SpriteDrawInfo info;
	info.texture = gpuTex;
	info.indexOffset = static_cast<uint32_t>(indices_.size() - 6);
	info.indexCount = 6;
	info.blendMode = sprite.getBlendMode();
	info.playfieldSpace = playfieldSpace;
	spriteDraws_.push_back(info);

	spriteCount_++;
}

void Renderer::drawWindowSprite(const Sprite &sprite, float /*zIndex*/) {
	// Sprite position, scale, and origin are window pixels; convert the
	// transformed corners straight into output NDC without the projection.
	if (!sprite.isVisible() || vertices_.size() + 4 > MAX_VERTICES ||
	    indices_.size() + 6 > MAX_INDICES) {
		return;
	}
	const float outputW = static_cast<float>(std::max(outputWidth_, 1));
	const float outputH = static_cast<float>(std::max(outputHeight_, 1));

	float width = sprite.getSourceRect().width;
	float height = sprite.getSourceRect().height;
	if (width <= 0.0f) {
		width = 32.0f;
	}
	if (height <= 0.0f) {
		height = 32.0f;
	}

	float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
	auto texture = sprite.getTexture();
	if (texture && texture->isValid()) {
		const Rect sourceRect = sprite.getSourceRect();
		const int texWidth = texture->getWidth();
		const int texHeight = texture->getHeight();
		if (texWidth > 0 && texHeight > 0) {
			const float fTexWidth = static_cast<float>(texWidth);
			const float fTexHeight = static_cast<float>(texHeight);
			u0 = sourceRect.x / fTexWidth;
			v0 = sourceRect.y / fTexHeight;
			u1 = (sourceRect.x + sourceRect.width) / fTexWidth;
			v1 = (sourceRect.y + sourceRect.height) / fTexHeight;
		}
	}

	const Mat4 transform = sprite.getTransform();
	const Vec2 p0(transform.data[12], transform.data[13]);
	const Vec2 p1(transform.data[12] + transform.data[0] * width,
	              transform.data[13] + transform.data[1] * width);
	const Vec2 p2(transform.data[12] + transform.data[4] * height,
	              transform.data[13] + transform.data[5] * height);
	const Vec2 p3 = p1 + p2 - p0;

	const Color color = sprite.getColor();
	const auto toNdc = [outputW, outputH](const Vec2 &point) {
		return Vec2(point.x / outputW * 2.0f - 1.0f,
		            1.0f - point.y / outputH * 2.0f);
	};

	const uint16_t baseIndex = static_cast<uint16_t>(vertices_.size());
	vertices_.push_back({toNdc(p0), color, {u0, v0}});
	vertices_.push_back({toNdc(p1), color, {u1, v0}});
	vertices_.push_back({toNdc(p2), color, {u0, v1}});
	vertices_.push_back({toNdc(p3), color, {u1, v1}});
	indices_.insert(indices_.end(), {static_cast<uint16_t>(baseIndex + 0),
	                                 static_cast<uint16_t>(baseIndex + 1),
	                                 static_cast<uint16_t>(baseIndex + 2),
	                                 static_cast<uint16_t>(baseIndex + 1),
	                                 static_cast<uint16_t>(baseIndex + 3),
	                                 static_cast<uint16_t>(baseIndex + 2)});

	void *gpuTex = nullptr;
	if (texture && texture->isValid()) {
		gpuTex = texture->getHandle();
	}
	spriteDraws_.push_back({gpuTex, static_cast<uint32_t>(indices_.size() - 6),
	                        6, sprite.getBlendMode(), false, true, false});
	++spriteCount_;
}

void Renderer::drawTexturedQuad(const std::shared_ptr<Texture> &texture,
                                const std::array<Vec2, 4> &logicalPositions,
                                const Color &color, BlendMode blendMode,
                                bool clipToPlayfield) {
	drawTexturedQuad(texture, logicalPositions,
	                 std::array<Color, 4>{color, color, color, color},
	                 blendMode, clipToPlayfield);
}

void Renderer::drawTexturedQuad(const std::shared_ptr<Texture> &texture,
                                const std::array<Vec2, 4> &logicalPositions,
                                const std::array<Color, 4> &colors,
                                BlendMode blendMode, bool clipToPlayfield) {
	drawTexturedQuad(texture, logicalPositions, colors,
	                 {{{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}}},
	                 blendMode, clipToPlayfield, false);
}

void Renderer::drawTexturedQuad(const std::shared_ptr<Texture> &texture,
                                const std::array<Vec2, 4> &logicalPositions,
                                const std::array<Color, 4> &colors,
                                const std::array<Vec2, 4> &uvs,
                                BlendMode blendMode, bool clipToPlayfield,
                                bool repeatTexture) {
	if (!texture || !texture->isValid() ||
	    vertices_.size() + 4 > MAX_VERTICES ||
	    indices_.size() + 6 > MAX_INDICES)
		return;

	std::array<Vec2, 4> positions = logicalPositions;
	for (auto &position : positions) {
		if (clipToPlayfield) {
			position.x = position.x / 384.0f * 2.0f - 1.0f;
			position.y = 1.0f - position.y / 448.0f * 2.0f;
		} else {
			const float logicalWidth = projectionRight_ - projectionLeft_;
			const float logicalHeight = projectionBottom_ - projectionTop_;
			position.x =
			    (position.x - projectionLeft_) / logicalWidth * 2.0f - 1.0f;
			position.y =
			    1.0f - (position.y - projectionTop_) / logicalHeight * 2.0f;
		}
	}

	const auto baseIndex = static_cast<uint16_t>(vertices_.size());
	for (size_t index = 0; index < positions.size(); ++index)
		vertices_.push_back({positions[index], colors[index], uvs[index]});
	indices_.insert(indices_.end(), {static_cast<uint16_t>(baseIndex + 0),
	                                 static_cast<uint16_t>(baseIndex + 1),
	                                 static_cast<uint16_t>(baseIndex + 2),
	                                 static_cast<uint16_t>(baseIndex + 1),
	                                 static_cast<uint16_t>(baseIndex + 3),
	                                 static_cast<uint16_t>(baseIndex + 2)});
	spriteDraws_.push_back({texture->getHandle(),
	                        static_cast<uint32_t>(indices_.size() - 6), 6,
	                        blendMode, clipToPlayfield, false, repeatTexture});
	++spriteCount_;
}

void Renderer::drawFoggedTexturedQuad(
    const std::shared_ptr<Texture> &texture,
    const std::array<Vec2, 4> &logicalPositions,
    const std::array<Color, 4> &colors, const std::array<Vec2, 4> &uvs,
    const std::array<Color, 4> &fogColors,
    const std::array<float, 4> &fogFactors, BlendMode blendMode,
    bool clipToPlayfield, bool repeatTexture) {
	if (!texture || !texture->isValid() ||
	    vertices_.size() + 4 > MAX_VERTICES ||
	    indices_.size() + 6 > MAX_INDICES)
		return;

	std::array<Vec2, 4> positions = logicalPositions;
	for (auto &position : positions) {
		if (clipToPlayfield) {
			position.x = position.x / 384.0f * 2.0f - 1.0f;
			position.y = 1.0f - position.y / 448.0f * 2.0f;
		} else {
			const float logicalWidth = projectionRight_ - projectionLeft_;
			const float logicalHeight = projectionBottom_ - projectionTop_;
			position.x =
			    (position.x - projectionLeft_) / logicalWidth * 2.0f - 1.0f;
			position.y =
			    1.0f - (position.y - projectionTop_) / logicalHeight * 2.0f;
		}
	}

	const auto baseIndex = static_cast<uint16_t>(vertices_.size());
	for (size_t index = 0; index < positions.size(); ++index) {
		vertices_.push_back({positions[index], colors[index], uvs[index],
		                     fogColors[index], fogFactors[index]});
	}
	indices_.insert(indices_.end(), {static_cast<uint16_t>(baseIndex + 0),
	                                 static_cast<uint16_t>(baseIndex + 1),
	                                 static_cast<uint16_t>(baseIndex + 2),
	                                 static_cast<uint16_t>(baseIndex + 1),
	                                 static_cast<uint16_t>(baseIndex + 3),
	                                 static_cast<uint16_t>(baseIndex + 2)});
	spriteDraws_.push_back({texture->getHandle(),
	                        static_cast<uint32_t>(indices_.size() - 6), 6,
	                        blendMode, clipToPlayfield, false, repeatTexture});
	++spriteCount_;
}

void Renderer::drawColoredQuad(const std::array<Vec2, 4> &logicalPositions,
                               const std::array<Color, 4> &colors,
                               BlendMode blendMode, bool clipToPlayfield) {
	if (vertices_.size() + 4 > MAX_VERTICES ||
	    indices_.size() + 6 > MAX_INDICES)
		return;
	std::array<Vec2, 4> positions = logicalPositions;
	for (auto &position : positions) {
		if (clipToPlayfield) {
			position.x = position.x / 384.0f * 2.0f - 1.0f;
			position.y = 1.0f - position.y / 448.0f * 2.0f;
		} else {
			const float logicalWidth = projectionRight_ - projectionLeft_;
			const float logicalHeight = projectionBottom_ - projectionTop_;
			position.x =
			    (position.x - projectionLeft_) / logicalWidth * 2.0f - 1.0f;
			position.y =
			    1.0f - (position.y - projectionTop_) / logicalHeight * 2.0f;
		}
	}
	const auto baseIndex = static_cast<uint16_t>(vertices_.size());
	for (size_t index = 0; index < positions.size(); ++index)
		vertices_.push_back({positions[index], colors[index], {0.0f, 0.0f}});
	indices_.insert(indices_.end(), {static_cast<uint16_t>(baseIndex + 0),
	                                 static_cast<uint16_t>(baseIndex + 1),
	                                 static_cast<uint16_t>(baseIndex + 2),
	                                 static_cast<uint16_t>(baseIndex + 1),
	                                 static_cast<uint16_t>(baseIndex + 3),
	                                 static_cast<uint16_t>(baseIndex + 2)});
	spriteDraws_.push_back({nullptr, static_cast<uint32_t>(indices_.size() - 6),
	                        6, blendMode, clipToPlayfield, false, false});
	++spriteCount_;
}

void Renderer::drawLine(const Vec2 &start, const Vec2 &end,
                        const Color &color) {
	if (vertices_.size() + 4 > MAX_VERTICES ||
	    indices_.size() + 6 > MAX_INDICES) {
		return;
	}

	// Rasterize a one-pixel-wide quad along the segment. The vertices are
	// built in world space and projected through the same path as
	// drawColoredQuad so lines honor the active projection and viewport.
	const Vec2 delta = end - start;
	const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
	Vec2 normal;
	if (length > 1e-6f) {
		normal = {-delta.y / length, delta.x / length};
	} else {
		normal = {0.0f, 1.0f};
	}
	const Vec2 half = normal * 0.5f;
	const std::array<Vec2, 4> worldPositions = {start - half, end - half,
	                                            start + half, end + half};

	std::array<Vec2, 4> positions = worldPositions;
	for (auto &position : positions) {
		const float logicalWidth = projectionRight_ - projectionLeft_;
		const float logicalHeight = projectionBottom_ - projectionTop_;
		if (logicalWidth <= 0.0f || logicalHeight <= 0.0f) {
			return;
		}
		position.x =
		    (position.x - projectionLeft_) / logicalWidth * 2.0f - 1.0f;
		position.y =
		    1.0f - (position.y - projectionTop_) / logicalHeight * 2.0f;
	}

	const auto baseIndex = static_cast<uint16_t>(vertices_.size());
	for (const auto &position : positions) {
		vertices_.push_back({position, color, {0.0f, 0.0f}});
	}
	indices_.insert(indices_.end(), {static_cast<uint16_t>(baseIndex + 0),
	                                 static_cast<uint16_t>(baseIndex + 1),
	                                 static_cast<uint16_t>(baseIndex + 2),
	                                 static_cast<uint16_t>(baseIndex + 1),
	                                 static_cast<uint16_t>(baseIndex + 3),
	                                 static_cast<uint16_t>(baseIndex + 2)});
	spriteDraws_.push_back({nullptr, static_cast<uint32_t>(indices_.size() - 6),
	                        6, BlendMode::Alpha, false, false, false});
	++spriteCount_;
}

void Renderer::drawRect(const Rect &rect, const Color &color,
                        bool playfieldSpace) {
	drawColoredQuad({{{rect.x, rect.y},
	                  {rect.x + rect.width, rect.y},
	                  {rect.x, rect.y + rect.height},
	                  {rect.x + rect.width, rect.y + rect.height}}},
	                {color, color, color, color}, currentBlendMode_,
	                playfieldSpace);
}

void Renderer::setViewport(int x, int y, int width, int height) {
	viewport_ = Rect(static_cast<float>(x), static_cast<float>(y),
	                 static_cast<float>(width), static_cast<float>(height));
}

void Renderer::setOutputSize(int width, int height) {
	outputWidth_ = std::max(width, 1);
	outputHeight_ = std::max(height, 1);
}

void Renderer::setPlayfieldRegion(float x, float y, float width, float height) {
	playfieldRegion_ = {x, y, std::max(width, 1.0f), std::max(height, 1.0f)};
}

void Renderer::setProjection(float left, float right, float bottom, float top) {
	projectionLeft_ = left;
	projectionRight_ = right;
	projectionBottom_ = bottom;
	projectionTop_ = top;
}

void Renderer::setBlendMode(BlendMode mode) { currentBlendMode_ = mode; }

} // namespace shiki
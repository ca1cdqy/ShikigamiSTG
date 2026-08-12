#include <algorithm>
#include <charconv>
#include <filesystem>
#include <ft2build.h>
#include <iterator>
#include <shiki/render/renderer.h>
#include <shiki/render/sprite.h>
#include <shiki/render/texture.h>
#include <spdlog/spdlog.h>
#include <vector>
#include FT_FREETYPE_H
#include FT_SYNTHESIS_H

namespace shiki {

bool Renderer::initializeDefaultFont() {
	if (defaultFontFace_) {
		return true;
	}

	FT_Library library = nullptr;
	if (FT_Init_FreeType(&library) != 0) {
		return false;
	}

	static constexpr const char *FONT_PATHS[] = {
	    "C:/Windows/Fonts/msgothic.ttc",
	    "C:/Windows/Fonts/msyh.ttc",
	    "C:/Windows/Fonts/arial.ttf",
	    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
	    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
	    "/System/Library/Fonts/ヒラギノ角ゴシック W3.ttc",
	    "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
	    // Emscripten virtual filesystem path used by web builds.
	    "/fonts/msgothic.ttc",
	    "/fonts/NotoSansCJK-Regular.ttc",
	};

	FT_Face face = nullptr;
	for (const char *path : FONT_PATHS) {
		if (std::filesystem::exists(path) &&
		    FT_New_Face(library, path, 0, &face) == 0) {
			break;
		}
	}
	if (!face) {
		FT_Done_FreeType(library);
		spdlog::warn(
		    "No default system font found; text rendering is disabled");
		return false;
	}

	fontLibrary_ = library;
	defaultFontFace_ = face;
	return true;
}

void Renderer::drawText(const std::string &text, const Vec2 &position,
                        float size, const Color &color, float displayScale,
                        bool playfieldSpace) {
	if (text.empty() || size <= 0.0f || displayScale <= 0.0f ||
	    !initializeDefaultFont()) {
		return;
	}

	const int pixelSize = std::max(1, static_cast<int>(std::round(size)));
	textCacheKeyBuffer_.clear();
	textCacheKeyBuffer_.reserve(text.size() + 16);
	char sizeBuffer[16];
	const auto sizeResult =
	    std::to_chars(std::begin(sizeBuffer), std::end(sizeBuffer), pixelSize);
	textCacheKeyBuffer_.append(sizeBuffer, sizeResult.ptr);
	textCacheKeyBuffer_.push_back(':');
	textCacheKeyBuffer_ += text;
	auto cached = textCache_.find(textCacheKeyBuffer_);

	if (cached == textCache_.end()) {
		FT_Face face = static_cast<FT_Face>(defaultFontFace_);
		// TH06 creates a 2x GDI font and downsamples it into the 16-pixel ANM
		// text surface. Rasterize at the same supersampled resolution here.
		const int rasterSize = pixelSize * 2;
		FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(rasterSize));

		std::vector<uint32_t> codepoints;
		for (size_t i = 0; i < text.size();) {
			const auto first = static_cast<uint8_t>(text[i]);
			uint32_t codepoint = 0xfffd;
			size_t length = 1;
			if (first < 0x80) {
				codepoint = first;
			} else if ((first & 0xe0) == 0xc0 && i + 1 < text.size()) {
				codepoint = (first & 0x1f) << 6;
				codepoint |= static_cast<uint8_t>(text[i + 1]) & 0x3f;
				length = 2;
			} else if ((first & 0xf0) == 0xe0 && i + 2 < text.size()) {
				codepoint = (first & 0x0f) << 12;
				codepoint |= (static_cast<uint8_t>(text[i + 1]) & 0x3f) << 6;
				codepoint |= static_cast<uint8_t>(text[i + 2]) & 0x3f;
				length = 3;
			} else if ((first & 0xf8) == 0xf0 && i + 3 < text.size()) {
				codepoint = (first & 0x07) << 18;
				codepoint |= (static_cast<uint8_t>(text[i + 1]) & 0x3f) << 12;
				codepoint |= (static_cast<uint8_t>(text[i + 2]) & 0x3f) << 6;
				codepoint |= static_cast<uint8_t>(text[i + 3]) & 0x3f;
				length = 4;
			}
			codepoints.push_back(codepoint);
			i += length;
		}

		int rasterWidth = 8;
		for (uint32_t codepoint : codepoints) {
			if (FT_Load_Char(face, codepoint, FT_LOAD_DEFAULT) == 0) {
				FT_GlyphSlot_Embolden(face->glyph);
				rasterWidth += static_cast<int>(face->glyph->advance.x >> 6);
			}
		}
		const int ascender =
		    static_cast<int>(face->size->metrics.ascender >> 6);
		const int rasterHeight =
		    std::max(rasterSize + 8,
		             static_cast<int>(face->size->metrics.height >> 6) + 8);
		std::vector<uint8_t> rasterPixels(
		    static_cast<size_t>(rasterWidth * rasterHeight * 4), 0);

		int penX = 4;
		const int baseline = ascender + 4;
		for (uint32_t codepoint : codepoints) {
			if (FT_Load_Char(face, codepoint, FT_LOAD_RENDER) != 0) {
				continue;
			}
			FT_GlyphSlot_Embolden(face->glyph);
			const FT_Bitmap &bitmap = face->glyph->bitmap;
			const int targetX = penX + face->glyph->bitmap_left;
			const int targetY = baseline - face->glyph->bitmap_top;
			for (int row = 0; row < static_cast<int>(bitmap.rows); ++row) {
				for (int col = 0; col < static_cast<int>(bitmap.width); ++col) {
					const int px = targetX + col;
					const int py = targetY + row;
					if (px < 0 || py < 0 || px >= rasterWidth ||
					    py >= rasterHeight) {
						continue;
					}
					const int sourceRow =
					    bitmap.pitch >= 0
					        ? row
					        : static_cast<int>(bitmap.rows) - row - 1;
					const auto source = static_cast<size_t>(
					    sourceRow * std::abs(bitmap.pitch) + col);
					const auto target =
					    static_cast<size_t>((py * rasterWidth + px) * 4);
					rasterPixels[target + 0] = 255;
					rasterPixels[target + 1] = 255;
					rasterPixels[target + 2] = 255;
					rasterPixels[target + 3] = bitmap.buffer[source];
				}
			}
			penX += static_cast<int>(face->glyph->advance.x >> 6);
		}

		const int width = (rasterWidth + 1) / 2;
		const int height = (rasterHeight + 1) / 2;
		std::vector<uint8_t> pixels(static_cast<size_t>(width * height * 4), 0);
		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				unsigned alpha = 0;
				unsigned samples = 0;
				for (int sampleY = 0; sampleY < 2; ++sampleY) {
					for (int sampleX = 0; sampleX < 2; ++sampleX) {
						const int sourceX = x * 2 + sampleX;
						const int sourceY = y * 2 + sampleY;
						if (sourceX >= rasterWidth || sourceY >= rasterHeight) {
							continue;
						}
						const auto source = static_cast<size_t>(
						    (sourceY * rasterWidth + sourceX) * 4 + 3);
						alpha += rasterPixels[source];
						++samples;
					}
				}
				const auto target = static_cast<size_t>((y * width + x) * 4);
				pixels[target + 0] = 255;
				pixels[target + 1] = 255;
				pixels[target + 2] = 255;
				pixels[target + 3] =
				    static_cast<uint8_t>(samples > 0 ? alpha / samples : 0);
			}
		}

		auto texture = std::make_shared<Texture>();
		texture->setDevice(getDevice());
		// Use the same isolated upload path as file textures. Keeping glyph
		// data out of the per-frame vertex transfer removes backend row-pitch
		// and resource-lifetime coupling from cached text textures.
		if (!texture->createFromData(width, height, pixels.data())) {
			return;
		}
		cached =
		    textCache_.emplace(textCacheKeyBuffer_, std::move(texture)).first;
	}

	Sprite sprite(cached->second);
	sprite.setPosition(position);
	sprite.setSourceRect(Rect(0.0f, 0.0f,
	                          static_cast<float>(cached->second->getWidth()),
	                          static_cast<float>(cached->second->getHeight())));
	sprite.setScale(displayScale, displayScale);
	sprite.setColor(color);
	drawSprite(sprite, 100.0f, playfieldSpace);
}

} // namespace shiki
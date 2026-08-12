#if defined(__EMSCRIPTEN__)

#include <GLES3/gl3.h>
#include <SDL3/SDL.h>
#include <cstdint>
#include <shiki/render/texture.h>
#include <spdlog/spdlog.h>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace shiki {

Texture::Texture() = default;

Texture::~Texture() { release(); }

Texture::Texture(Texture &&other) noexcept
    : backend_(other.backend_), handle_(other.handle_), width_(other.width_),
      height_(other.height_), path_(std::move(other.path_)) {
	other.backend_ = nullptr;
	other.handle_ = nullptr;
	other.width_ = 0;
	other.height_ = 0;
}

Texture &Texture::operator=(Texture &&other) noexcept {
	if (this != &other) {
		release();
		backend_ = other.backend_;
		handle_ = other.handle_;
		width_ = other.width_;
		height_ = other.height_;
		path_ = std::move(other.path_);
		other.backend_ = nullptr;
		other.handle_ = nullptr;
		other.width_ = 0;
		other.height_ = 0;
	}
	return *this;
}

bool Texture::loadFromFile(const std::string &path) {
	if (!backend_) {
		spdlog::error("Texture::loadFromFile: backend handle not set");
		return false;
	}

	int width, height, channels;
	stbi_uc *data = stbi_load(path.c_str(), &width, &height, &channels, 4);

	if (data) {
		bool result = createFromData(width, height, data);
		stbi_image_free(data);

		if (result) {
			path_ = path;
			spdlog::info("Texture loaded (stb_image): {} ({}x{}, channels={})",
			             path, width, height, channels);
		}
		return result;
	}

	SDL_Surface *surface = SDL_LoadBMP(path.c_str());
	if (surface) {
		SDL_Surface *rgbaSurface =
		    SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
		SDL_DestroySurface(surface);

		if (rgbaSurface) {
			bool result =
			    createFromData(rgbaSurface->w, rgbaSurface->h,
			                   static_cast<uint8_t *>(rgbaSurface->pixels));
			SDL_DestroySurface(rgbaSurface);

			if (result) {
				path_ = path;
				spdlog::info("Texture loaded (SDL): {} ({}x{})", path, width_,
				             height_);
			}
			return result;
		}
	}

	spdlog::error("Failed to load texture '{}': {}", path,
	              stbi_failure_reason());
	return false;
}

bool Texture::createFromData(int width, int height, const uint8_t *data) {
	if (!backend_) {
		spdlog::error("Texture::createFromData: backend handle not set");
		return false;
	}
	if (width <= 0 || height <= 0 || !data) {
		spdlog::error("Texture::createFromData: invalid parameters");
		return false;
	}

	release();
	GLuint texture = 0;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
	             GL_UNSIGNED_BYTE, data);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);

	handle_ = reinterpret_cast<void *>(static_cast<uintptr_t>(texture));
	width_ = width;
	height_ = height;
	return true;
}

bool Texture::createEmpty(int width, int height) {
	if (!backend_) {
		spdlog::error("Texture::createEmpty: backend handle not set");
		return false;
	}
	if (width <= 0 || height <= 0) {
		spdlog::error("Texture::createEmpty: invalid dimensions");
		return false;
	}

	release();
	GLuint texture = 0;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
	             GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);

	handle_ = reinterpret_cast<void *>(static_cast<uintptr_t>(texture));
	width_ = width;
	height_ = height;
	return true;
}

bool Texture::create(int width, int height) {
	if (!backend_) {
		spdlog::error("Texture::create: backend handle not set");
		return false;
	}

	std::vector<uint8_t> whiteData(static_cast<size_t>(width) * height * 4,
	                               255);
	return createFromData(width, height, whiteData.data());
}

void Texture::release() {
	const GLuint texture =
	    static_cast<GLuint>(reinterpret_cast<uintptr_t>(handle_));
	if (texture) {
		glDeleteTextures(1, &texture);
		handle_ = nullptr;
	}
	width_ = 0;
	height_ = 0;
}

} // namespace shiki

#endif // defined(__EMSCRIPTEN__)
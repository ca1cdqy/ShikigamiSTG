#pragma once

#include <shiki/core/types.h>
#include <string>

// Forward declaration for SDL
struct SDL_GPUTexture;
struct SDL_GPUSampler;
struct SDL_GPUDevice;

namespace shiki {

/**
 * Owns one SDL GPU texture and its associated device state.
 *
 * A Texture is created empty and populated through one of the creation or
 * loading methods. It is non-copyable because it owns a GPU resource. The
 * GPU device must be set with setDevice() before any creation call.
 */
class Texture {
  public:
	/** Creates an empty, invalid texture. */
	Texture();
	/** Releases the GPU texture if one was created. */
	~Texture();

	/** Textures cannot be copied because they own a GPU resource. */
	Texture(const Texture &) = delete;
	/** Textures cannot be copy-assigned. */
	Texture &operator=(const Texture &) = delete;

	/** Transfers GPU resource ownership and device state. */
	Texture(Texture &&other) noexcept;
	/** Releases current GPU resource, then takes ownership from other. */
	Texture &operator=(Texture &&other) noexcept;

	/** Decodes an image file and uploads it to the GPU. Returns true on
	 * success. */
	bool loadFromFile(const std::string &path);

	/** Creates and uploads a texture from RGBA8 pixel data. Returns true on
	 * success. */
	bool createFromData(int width, int height, const uint8_t *data);

	/**
	 * Creates an uninitialized GPU texture whose pixels are uploaded by the
	 * renderer during the current frame. Returns true on success.
	 */
	bool createEmpty(int width, int height);

	/** Creates a blank white GPU texture. Returns true on success. */
	bool create(int width, int height);

	/** Sets the SDL GPU device used by all subsequent creation calls. */
	void setDevice(SDL_GPUDevice *device) { device_ = device; }

	/** Returns the texture width in pixels. */
	[[nodiscard]] int getWidth() const { return width_; }
	/** Returns the texture height in pixels. */
	[[nodiscard]] int getHeight() const { return height_; }

	/** Returns the underlying SDL GPU texture handle for renderer use. */
	[[nodiscard]] SDL_GPUTexture *getGPUTexture() const { return gpuTexture_; }

	/** Returns the filesystem path supplied to loadFromFile(), or empty. */
	[[nodiscard]] const std::string &getPath() const { return path_; }

	/** Returns whether a GPU texture has been successfully created. */
	[[nodiscard]] bool isValid() const { return gpuTexture_ != nullptr; }

  private:
	void release();

	SDL_GPUDevice *device_ = nullptr;
	SDL_GPUTexture *gpuTexture_ = nullptr;

	int width_ = 0;
	int height_ = 0;
	std::string path_;
};

} // namespace shiki

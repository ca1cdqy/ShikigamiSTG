#pragma once

#include <shiki/core/types.h>
#include <string>

namespace shiki {

/**
 * Owns one backend texture and its associated device state.
 *
 * A Texture is created empty and populated through one of the creation or
 * loading methods. It is non-copyable because it owns a backend resource.
 * The backend handle must be set with setDevice() before any creation call.
 *
 * Desktop builds wrap an SDL_GPUTexture created on the SDL_GPUDevice*;
 * web builds wrap a WebGL2 texture id (GLuint) created on the
 * SDL_GLContext. Both handles are passed as opaque void* so the public API
 * stays platform-neutral.
 */
class Texture {
  public:
	/** Creates an empty, invalid texture. */
	Texture();
	/** Releases the backend texture if one was created. */
	~Texture();

	/** Textures cannot be copied because they own a backend resource. */
	Texture(const Texture &) = delete;
	/** Textures cannot be copy-assigned. */
	Texture &operator=(const Texture &) = delete;

	/** Transfers backend resource ownership and device state. */
	Texture(Texture &&other) noexcept;
	/** Releases current backend resource, then takes ownership from other. */
	Texture &operator=(Texture &&other) noexcept;

	/** Decodes an image file and uploads it to the backend. Returns true on
	 * success. */
	bool loadFromFile(const std::string &path);

	/** Creates and uploads a texture from RGBA8 pixel data. Returns true on
	 * success. */
	bool createFromData(int width, int height, const uint8_t *data);

	/**
	 * Creates an uninitialized backend texture whose pixels are uploaded by
	 * the renderer during the current frame. Returns true on success.
	 */
	bool createEmpty(int width, int height);

	/** Creates a blank white backend texture. Returns true on success. */
	bool create(int width, int height);

	/**
	 * Sets the opaque backend handle used by all subsequent creation calls.
	 *
	 * Pass the same handle returned by Renderer::getDevice(): an
	 * SDL_GPUDevice* on desktop or an SDL_GLContext on web builds.
	 */
	void setDevice(void *device) { backend_ = device; }

	/** Returns the texture width in pixels. */
	[[nodiscard]] int getWidth() const { return width_; }
	/** Returns the texture height in pixels. */
	[[nodiscard]] int getHeight() const { return height_; }

	/**
	 * Returns the underlying backend texture handle.
	 *
	 * Desktop builds return the SDL_GPUTexture*; web builds return the
	 * WebGL2 texture id (GLuint). The handle is only valid while this
	 * Texture is alive.
	 */
	[[nodiscard]] void *getHandle() const { return handle_; }

	/** Returns the filesystem path supplied to loadFromFile(), or empty. */
	[[nodiscard]] const std::string &getPath() const { return path_; }

	/** Returns whether a backend texture has been successfully created. */
	[[nodiscard]] bool isValid() const { return handle_ != nullptr; }

  private:
	void release();

	void *backend_ = nullptr; ///< Opaque device/renderer backend handle.
	void *handle_ = nullptr;  ///< Opaque backend texture handle.

	int width_ = 0;
	int height_ = 0;
	std::string path_;
};

} // namespace shiki
#pragma once

#include <array>
#include <memory>
#include <shiki/core/types.h>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declarations for the desktop SDL_GPU backend. These must live at
// global scope so they match the real SDL3 declarations.
struct SDL_GPUDevice;
struct SDL_GPUCommandBuffer;
struct SDL_GPURenderPass;
struct SDL_GPUTexture;
struct SDL_GPUBuffer;
struct SDL_GPUTransferBuffer;
struct SDL_GPUSampler;
struct SDL_GPUShader;
struct SDL_GPUGraphicsPipeline;

namespace shiki {

/// Forward declarations
class Texture;
class Sprite;

/** One transformed, textured, and colored vertex. */
struct Vertex {
	Vec2 position; ///< Position in the renderer's active coordinate space.
	Vec4 color;    ///< Per-vertex RGBA color modulation.
	Vec2 uv;       ///< Normalized texture coordinates.
	Vec4 fogColor{0.0f, 0.0f, 0.0f, 1.0f}; ///< Fixed-function fog color.
	float fogFactor = 1.0f; ///< One keeps the texture; zero selects fogColor.
};

/**
 * Immediate-mode 2D renderer with a compile-time selected backend.
 *
 * Desktop builds use the SDL3 GPU API (Direct3D 12, Metal, or Vulkan).
 * WebAssembly builds use a native WebGL2 backend that mirrors the desktop
 * batching architecture. The public API is identical on every platform and
 * the backend switch is resolved at compile time, so there is no virtual
 * dispatch or runtime indirection.
 *
 * Usage pattern per frame:
 * -# Call beginFrame() to acquire a render target.
 * -# Submit draw calls; they are batched by texture and blend mode.
 * -# Call endFrame() to flush all batches and present.
 *
 * The renderer is non-copyable because it owns backend resources. Call
 * initialize() once after a window and a backend handle are available.
 */
class Renderer {
  public:
	/** Creates a renderer with no allocated backend resources. */
	Renderer();
	/** Calls shutdown() to release any initialized backend resources. */
	~Renderer();

	/** Renderers cannot be copied because they own backend resources. */
	Renderer(const Renderer &) = delete;
	/** Renderers cannot be copy-assigned. */
	Renderer &operator=(const Renderer &) = delete;

	/** Transfers backend resource ownership. */
	Renderer(Renderer &&) noexcept;
	/** Releases current backend resources, then takes ownership from other. */
	Renderer &operator=(Renderer &&) noexcept;

	/**
	 * Creates all pipelines, buffers, shaders, and the default font.
	 *
	 * @param window The SDL_Window* that owns the rendering surface.
	 * @param device Opaque backend handle: an SDL_GPUDevice* on desktop or an
	 *               SDL_GLContext on web builds.
	 * @return True when the renderer is ready for beginFrame().
	 */
	bool initialize(void *window, void *device);
	/** Releases all backend resources and resets internal state. */
	void shutdown();

	/** Begins a new frame and acquires a render target. */
	void beginFrame();
	/** Flushes all batched draw calls and presents the rendered frame. */
	void endFrame();

	/** Issues a full-screen clear with the given RGBA color. */
	void clear(const Color &color = {0.0f, 0.0f, 0.0f, 1.0f});

	/** Queues a Sprite draw in the playfield or window coordinate space. */
	void drawSprite(const Sprite &sprite, float zIndex = 0.0f,
	                bool playfieldSpace = false);
	/**
	 * Queues a Sprite draw in window pixel coordinates regardless of
	 * playfieldSpace.
	 *
	 * Position, scale, and origin are measured in physical window pixels
	 * (0..outputWidth, 0..outputHeight), independent of the projection and
	 * playfield viewport. Used for UI anchored to the actual swapchain.
	 */
	void drawWindowSprite(const Sprite &sprite, float zIndex = 0.0f);

	/**
	 * Queues a textured quad using four pre-projected logical-coordinate
	 * vertices in top-left, top-right, bottom-left, bottom-right order.
	 */
	void drawTexturedQuad(const std::shared_ptr<Texture> &texture,
	                      const std::array<Vec2, 4> &positions,
	                      const Color &color = {1.0f, 1.0f, 1.0f, 1.0f},
	                      BlendMode blendMode = BlendMode::Alpha,
	                      bool clipToPlayfield = false);
	/** Queues a textured quad with a per-vertex color array. */
	void drawTexturedQuad(const std::shared_ptr<Texture> &texture,
	                      const std::array<Vec2, 4> &positions,
	                      const std::array<Color, 4> &colors,
	                      BlendMode blendMode = BlendMode::Alpha,
	                      bool clipToPlayfield = false);
	/** Queues a textured quad with explicit UVs and optional wrap sampling. */
	void drawTexturedQuad(const std::shared_ptr<Texture> &texture,
	                      const std::array<Vec2, 4> &positions,
	                      const std::array<Color, 4> &colors,
	                      const std::array<Vec2, 4> &uvs,
	                      BlendMode blendMode = BlendMode::Alpha,
	                      bool clipToPlayfield = false,
	                      bool repeatTexture = false);
	/** Queues a textured quad with per-vertex fog modulation. */
	void drawFoggedTexturedQuad(const std::shared_ptr<Texture> &texture,
	                            const std::array<Vec2, 4> &positions,
	                            const std::array<Color, 4> &colors,
	                            const std::array<Vec2, 4> &uvs,
	                            const std::array<Color, 4> &fogColors,
	                            const std::array<float, 4> &fogFactors,
	                            BlendMode blendMode = BlendMode::Alpha,
	                            bool clipToPlayfield = false,
	                            bool repeatTexture = false);
	/** Queues a solid quad using the renderer's default white texture. */
	void drawColoredQuad(const std::array<Vec2, 4> &positions,
	                     const std::array<Color, 4> &colors,
	                     BlendMode blendMode = BlendMode::Alpha,
	                     bool clipToPlayfield = false);

	/** Renders a UTF-8 string using the embedded FreeType font. */
	void drawText(const std::string &text, const Vec2 &position,
	              float size = 16.0f,
	              const Color &color = {1.0f, 1.0f, 1.0f, 1.0f},
	              float displayScale = 1.0f, bool playfieldSpace = false);

	/** Queues a single colored line segment. */
	void drawLine(const Vec2 &start, const Vec2 &end,
	              const Color &color = {1.0f, 1.0f, 1.0f, 1.0f});

	/** Queues a filled colored rectangle. */
	void drawRect(const Rect &rect,
	              const Color &color = {1.0f, 1.0f, 1.0f, 1.0f},
	              bool playfieldSpace = false);

	/** Sets the active backend scissor and viewport region in physical pixels.
	 */
	void setViewport(int x, int y, int width, int height);
	/** Updates the physical output dimensions used for projection. */
	void setOutputSize(int width, int height);
	/** Sets the playfield region in logical coordinates for coordinate mapping.
	 */
	void setPlayfieldRegion(float x, float y, float width, float height);

	/** Configures the orthographic projection bounds for subsequent draws. */
	void setProjection(float left, float right, float bottom, float top);

	/** Changes the active blend state. */
	void setBlendMode(BlendMode mode);

	/** Returns the total number of backend draw calls issued in the last frame.
	 */
	[[nodiscard]] int getDrawCallCount() const { return drawCallCount_; }
	/** Returns the total number of sprites batched in the last frame. */
	[[nodiscard]] int getSpriteCount() const { return spriteCount_; }

	/**
	 * Returns the opaque backend handle passed to initialize().
	 *
	 * Desktop builds return the SDL_GPUDevice*; web builds return the
	 * SDL_GLContext. Use it only to hand the same handle to Texture uploads.
	 */
	[[nodiscard]] void *getDevice() const { return backend_; }

  private:
	/// Common state shared by every backend.
	bool initializeDefaultFont();
	void *fontLibrary_ = nullptr;
	void *defaultFontFace_ = nullptr;
	std::unordered_map<std::string, std::shared_ptr<Texture>> textCache_;
	/// Reused key buffer for text cache lookups (avoids per-frame allocs).
	std::string textCacheKeyBuffer_;

	int drawCallCount_ = 0;
	int spriteCount_ = 0;
	BlendMode currentBlendMode_ = BlendMode::Alpha;
	Rect viewport_;
	int outputWidth_ = 640;
	int outputHeight_ = 480;
	Color clearColor_ = {0.0f, 0.0f, 0.0f, 1.0f};
	float projectionLeft_ = 0.0f;
	float projectionRight_ = 640.0f;
	float projectionBottom_ = 480.0f;
	float projectionTop_ = 0.0f;
	Rect playfieldRegion_ = {32.0f, 16.0f, 384.0f, 448.0f};
	void *window_ = nullptr;
	/// Opaque backend handle: SDL_GPUDevice* on desktop, SDL_GLContext on web.
	void *backend_ = nullptr;

	/// Batching state shared by the desktop GPU and web WebGL backends.
	static constexpr size_t MAX_VERTICES = 65536;
	static constexpr size_t MAX_INDICES = 65536;
	std::vector<Vertex> vertices_;
	std::vector<uint16_t> indices_;
	/// Per-sprite draw metadata.
	struct SpriteDrawInfo {
		void *texture = nullptr;  // Backend texture handle, or null for white
		uint32_t indexOffset = 0; // Offset in the index buffer
		uint32_t indexCount = 6;  // Index count, fixed at six
		BlendMode blendMode = BlendMode::Alpha;
		bool playfieldSpace = false;
		bool windowSpace = false;
		bool repeatTexture = false;
	};
	std::vector<SpriteDrawInfo> spriteDraws_;

#if defined(__EMSCRIPTEN__)
	/// WebGL2 backend state (src/render/renderer_webgl.cpp).
	void applyPendingClear();
	void flushBatch();
	bool createShadersAndBuffers();

	unsigned int webglProgram_ = 0;
	unsigned int webglVertexShader_ = 0;
	unsigned int webglFragmentShader_ = 0;
	unsigned int webglVao_ = 0;
	unsigned int webglVbo_ = 0;
	unsigned int webglIbo_ = 0;
	unsigned int webglWhiteTexture_ = 0;
	int webglUniformTexture_ = -1;
	bool webglNeedsClear_ = true;
#else
	/// SDL_GPU backend state (src/render/renderer.cpp).
	SDL_GPUDevice *gpuDevice_ = nullptr;
	void flushBatch();
	bool createPipeline();
	bool createBuffers();

	SDL_GPUCommandBuffer *commandBuffer_ = nullptr;
	SDL_GPURenderPass *renderPass_ = nullptr;
	SDL_GPUTexture *swapchainTexture_ = nullptr;
	SDL_GPUBuffer *vertexBuffer_ = nullptr;
	SDL_GPUBuffer *indexBuffer_ = nullptr;
	SDL_GPUTransferBuffer *frameTransferBuffer_ = nullptr;
	SDL_GPUSampler *sampler_ = nullptr;
	SDL_GPUSampler *repeatSampler_ = nullptr;
	SDL_GPUShader *vertexShader_ = nullptr;
	SDL_GPUShader *fragmentShader_ = nullptr;
	SDL_GPUGraphicsPipeline *pipeline_ = nullptr;
	SDL_GPUGraphicsPipeline *additivePipeline_ = nullptr;
	SDL_GPUTexture *defaultTexture_ = nullptr; // Default white texture
#endif
};

} // namespace shiki
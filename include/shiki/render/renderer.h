#pragma once

#include <array>
#include <memory>
#include <shiki/core/types.h>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declaration for SDL
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

/** One transformed, textured, and colored GPU vertex. */
struct Vertex {
	Vec2 position; ///< Position in the renderer's active coordinate space.
	Vec4 color;    ///< Per-vertex RGBA color modulation.
	Vec2 uv;       ///< Normalized texture coordinates.
	Vec4 fogColor{0.0f, 0.0f, 0.0f, 1.0f}; ///< Fixed-function fog color.
	float fogFactor = 1.0f; ///< One keeps the texture; zero selects fogColor.
};

/** Records a single queued draw operation before submission. */
struct RenderCommand {
	/** Identifies the GPU operation this command encodes. */
	enum class Type {
		DrawSprite,  ///< Batched textured quad for a Sprite.
		DrawText,    ///< FreeType-rendered glyph sequence.
		DrawLine,    ///< Single line segment in screen space.
		DrawRect,    ///< Filled or stroked rectangle in screen space.
		Clear,       ///< Full framebuffer clear.
		SetViewport, ///< Updates the active scissor/viewport region.
		SetBlendMode ///< Changes the active GPU pipeline blend state.
	};

	Type type;           ///< Which GPU operation this command encodes.
	float zIndex = 0.0f; ///< Sort key within the same render pass.
	                     /// Command-specific parameters
};

/**
 * SDL3 GPU-backed immediate-mode 2D renderer.
 *
 * Usage pattern per frame:
 * -# Call beginFrame() to acquire a swapchain texture.
 * -# Submit draw calls; they are batched by texture and blend mode.
 * -# Call endFrame() to flush all batches and present.
 *
 * The renderer is non-copyable because it owns GPU resources. Call
 * initialize() once after a window and GPU device are available.
 */
class Renderer {
  public:
	/** Creates a renderer with no allocated GPU resources. */
	Renderer();
	/** Calls shutdown() to release any initialized GPU resources. */
	~Renderer();

	/** Renderers cannot be copied because they own GPU resources. */
	Renderer(const Renderer &) = delete;
	/** Renderers cannot be copy-assigned. */
	Renderer &operator=(const Renderer &) = delete;

	/** Transfers GPU resource ownership. */
	Renderer(Renderer &&) noexcept;
	/** Releases current GPU resources, then takes ownership from other. */
	Renderer &operator=(Renderer &&) noexcept;

	/** Creates all GPU pipelines, buffers, shaders, and the default font. */
	bool initialize(void *window, void *device);
	/** Releases all GPU resources and resets internal state. */
	void shutdown();

	/** Begins a new frame and acquires a swapchain render target. */
	void beginFrame();
	/** Flushes all batched draw calls and presents the rendered frame. */
	void endFrame();

	/** Issues a full-screen clear with the given RGBA color. */
	void clear(const Color &color = {0.0f, 0.0f, 0.0f, 1.0f});

	/** Queues a Sprite draw in the playfield or window coordinate space. */
	void drawSprite(const Sprite &sprite, float zIndex = 0.0f,
	                bool playfieldSpace = false);
	/** Queues a Sprite draw in window coordinate space regardless of
	 * playfieldSpace. */
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

	/** Sets the active GPU scissor and viewport region in physical pixels. */
	void setViewport(int x, int y, int width, int height);
	/** Updates the physical output dimensions used for projection. */
	void setOutputSize(int width, int height);
	/** Sets the playfield region in logical coordinates for coordinate mapping.
	 */
	void setPlayfieldRegion(float x, float y, float width, float height);

	/** Configures the orthographic projection bounds for subsequent draws. */
	void setProjection(float left, float right, float bottom, float top);

	/** Changes the active GPU blend pipeline state. */
	void setBlendMode(BlendMode mode);

	/** Returns the total number of GPU draw calls issued in the last frame. */
	[[nodiscard]] int getDrawCallCount() const { return drawCallCount_; }
	/** Returns the total number of sprites batched in the last frame. */
	[[nodiscard]] int getSpriteCount() const { return spriteCount_; }

	/** Returns the SDL GPU device used by this renderer. */
	[[nodiscard]] SDL_GPUDevice *getDevice() const { return device_; }

  private:
	void flushBatch();
	bool createPipeline();
	bool createBuffers();
	bool initializeDefaultFont();

	SDL_GPUDevice *device_ = nullptr;
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
	SDL_GPUBuffer *uniformBuffer_ = nullptr;   // Projection uniform buffer
	SDL_GPUTexture *defaultTexture_ = nullptr; // Default white texture

	/// Batching state
	std::vector<RenderCommand> commandQueue_;
	int drawCallCount_ = 0;
	int spriteCount_ = 0;

	/// Current state
	BlendMode currentBlendMode_ = BlendMode::Alpha;
	Rect viewport_;
	int outputWidth_ = 640;
	int outputHeight_ = 480;
	Color clearColor_ = {0.0f, 0.0f, 0.0f, 1.0f};

	/// Projection parameters
	float projectionLeft_ = 0.0f;
	float projectionRight_ = 640.0f;
	float projectionBottom_ = 480.0f;
	float projectionTop_ = 0.0f;
	Rect playfieldRegion_ = {32.0f, 16.0f, 384.0f, 448.0f};

	/// Window state
	void *window_ = nullptr;

	/// Vertex storage
	static constexpr size_t MAX_VERTICES = 65536;
	static constexpr size_t MAX_INDICES = 65536;
	std::vector<Vertex> vertices_;
	std::vector<uint16_t> indices_;

	/// Per-sprite draw metadata
	struct SpriteDrawInfo {
		SDL_GPUTexture *texture =
		    nullptr; // A null pointer selects the default white texture
		uint32_t indexOffset = 0; // Offset in the index buffer
		uint32_t indexCount = 6;  // Index count, fixed at six
		BlendMode blendMode = BlendMode::Alpha;
		bool playfieldSpace = false;
		bool windowSpace = false;
		bool repeatTexture = false;
	};
	std::vector<SpriteDrawInfo> spriteDraws_;

	void *fontLibrary_ = nullptr;
	void *defaultFontFace_ = nullptr;
	std::unordered_map<std::string, std::shared_ptr<Texture>> textCache_;
	/// Reused key buffer for text cache lookups (avoids per-frame allocs).
	std::string textCacheKeyBuffer_;
};

} // namespace shiki

#pragma once

#include <functional>
#include <memory>
#include <shiki/core/types.h>
#include <shiki/render/renderer.h>
#include <string>

namespace shiki {
class AudioManager;
class ResourceManager;
} // namespace shiki

namespace shiki::frontend {

/** Configures the optional SDL real-time frontend. */
struct RealtimeConfig {
	std::string title = "ShikigamiSTG"; ///< Initial UTF-8 window title.
	int width = 640;                    ///< Initial client width in pixels.
	int height = 480;                   ///< Initial client height in pixels.
	bool fullscreen = false; ///< Whether to start in fullscreen mode.
	bool vsync = true;       ///< Whether presentation waits for vblank.
	int targetFps = 60;      ///< Software frame limit, or zero for no limit.
};

/**
 * Platform-facing real-time convenience facade.
 *
 * Deterministic or headless applications should use Runtime and Session;
 * Realtime owns the optional window, renderer, audio, and resource
 * services. It is platform and presentation state, not Session state.
 */
class Realtime final {
  public:
	/** Per-frame gameplay update callback receiving elapsed seconds. */
	using UpdateCallback = std::function<void(float)>;
	/** Per-frame render callback receiving elapsed seconds. */
	using RenderCallback = std::function<void(float)>;

	/** Creates a frontend with RealtimeConfig defaults. */
	Realtime();
	/** Creates a frontend from a copied configuration. */
	explicit Realtime(const RealtimeConfig &config);
	/** Shuts down initialized subsystems. */
	~Realtime();

	/** Frontends cannot be copied because they own platform resources. */
	Realtime(const Realtime &) = delete;
	/** Frontends cannot be copy-assigned. */
	Realtime &operator=(const Realtime &) = delete;

	/** Transfers platform and subsystem ownership. */
	Realtime(Realtime &&) noexcept;
	/** Shuts down current state and takes ownership from another engine. */
	Realtime &operator=(Realtime &&) noexcept;

	/** Initializes SDL, the window, rendering, audio, and resources. */
	[[nodiscard]] bool initialize();
	/** Stops the loop and releases initialized platform resources. */
	void shutdown();

	/** Runs event, update, and render processing until stop() is requested. */
	void run(const UpdateCallback &update, const RenderCallback &render);
	/** Requests termination of the active run() loop. */
	void stop();

	/// Input callback
	/** Keyboard event callback receiving a platform key code and press state.
	 */
	using KeyCallback = std::function<void(int key, bool pressed)>;
	/** Replaces the keyboard event callback. */
	void setKeyCallback(const KeyCallback &callback) {
		keyCallback_ = callback;
	}

	/// Window resize callback
	/** Callback receiving the new physical client dimensions. */
	using ResizeCallback = std::function<void(int width, int height)>;
	/** Replaces the callback invoked after a client-size change. */
	void setResizeCallback(const ResizeCallback &callback) {
		resizeCallback_ = callback;
	}

	/** Updates the UTF-8 window title. */
	void setWindowTitle(const std::string &title);
	/** Updates client size in physical pixels. */
	void setWindowSize(int width, int height);
	/** Enters or leaves fullscreen mode. */
	void setFullscreen(bool fullscreen);
	/** Enables or disables presentation synchronization. */
	void setVSync(bool enabled);
	/** Sets the software frame limit; zero disables it. */
	void setTargetFps(int targetFps);

	/** Returns the owned renderer, or null before initialization. */
	[[nodiscard]] ::shiki::Renderer *getRenderer() const;
	/** Returns the owned audio manager, or null before initialization. */
	[[nodiscard]] ::shiki::AudioManager *getAudio() const;
	/** Returns the owned resource manager, or null before initialization. */
	[[nodiscard]] ::shiki::ResourceManager *getResourceManager() const;

	/** Reports whether run() is active. */
	[[nodiscard]] bool isRunning() const { return isRunning_; }
	/** Returns the most recently measured frame duration in seconds. */
	[[nodiscard]] float getDeltaTime() const { return deltaTime_; }
	/** Returns the rolling measured frame rate. */
	[[nodiscard]] float getFPS() const { return fps_; }
	/** Returns the configured logical window width. */
	[[nodiscard]] int getWidth() const { return config_.width; }
	/** Returns the configured logical window height. */
	[[nodiscard]] int getHeight() const { return config_.height; }

	/// Returns the current physical window size
	void getWindowSize(int &width, int &height) const;

  private:
	void processEvents();
	void calculateDeltaTime();
	void limitFrameRate();
	bool applyPresentMode();

	RealtimeConfig config_;
	bool isRunning_ = false;
	bool isInitialized_ = false;

	/// Timing state
	float deltaTime_ = 0.0f;
	float fps_ = 0.0f;
	uint64_t lastFrameTime_ = 0;
	uint64_t frameCount_ = 0;
	float fpsTimer_ = 0.0f;

	// SDL
	void *window_ = nullptr;
	void *gpuDevice_ = nullptr;

	/// Subsystems
	std::unique_ptr<::shiki::Renderer> renderer_;
	std::unique_ptr<::shiki::AudioManager> audio_;
	std::unique_ptr<::shiki::ResourceManager> resourceManager_;

	/// Input callback
	KeyCallback keyCallback_;

	/// Window resize callback
	ResizeCallback resizeCallback_;
};

} // namespace shiki::frontend

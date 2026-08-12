#include <SDL3/SDL.h>
#include <algorithm>
#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif
#include <shiki/audio/audio_manager.h>
#include <shiki/frontend/realtime.h>
#include <shiki/render/renderer.h>
#include <shiki/render/sprite.h>
#include <shiki/resource/resource_manager.h>
#include <spdlog/spdlog.h>

namespace shiki::frontend {

Realtime::Realtime() : config_() {}

Realtime::Realtime(const RealtimeConfig &config) : config_(config) {}

Realtime::~Realtime() { shutdown(); }

Realtime::Realtime(Realtime &&other) noexcept
    : config_(other.config_), isRunning_(other.isRunning_),
      isInitialized_(other.isInitialized_), deltaTime_(other.deltaTime_),
      fps_(other.fps_), lastFrameTime_(other.lastFrameTime_),
      frameCount_(other.frameCount_), fpsTimer_(other.fpsTimer_),
      window_(other.window_), backendHandle_(other.backendHandle_) {
	other.window_ = nullptr;
	other.backendHandle_ = nullptr;
	other.isRunning_ = false;
	other.isInitialized_ = false;
}

Realtime &Realtime::operator=(Realtime &&other) noexcept {
	if (this != &other) {
		shutdown();
		config_ = other.config_;
		isRunning_ = other.isRunning_;
		isInitialized_ = other.isInitialized_;
		deltaTime_ = other.deltaTime_;
		fps_ = other.fps_;
		lastFrameTime_ = other.lastFrameTime_;
		frameCount_ = other.frameCount_;
		fpsTimer_ = other.fpsTimer_;
		window_ = other.window_;
		backendHandle_ = other.backendHandle_;
		other.window_ = nullptr;
		other.backendHandle_ = nullptr;
		other.isRunning_ = false;
		other.isInitialized_ = false;
	}
	return *this;
}

bool Realtime::initialize() {
	if (isInitialized_) {
		return true;
	}

	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS)) {
		spdlog::error("SDL_Init failed: {}", SDL_GetError());
		return false;
	}

	uint64_t windowFlags = SDL_WINDOW_RESIZABLE;
#if defined(__EMSCRIPTEN__)
	// The WebGL2 backend needs an OpenGL (ES) window.
	windowFlags |= SDL_WINDOW_OPENGL;
#endif
	window_ = SDL_CreateWindow(config_.title.c_str(), config_.width,
	                           config_.height, windowFlags);

	if (!window_) {
		spdlog::error("SDL_CreateWindow failed: {}", SDL_GetError());
		SDL_Quit();
		return false;
	}

	int w, h;
	SDL_GetWindowSize(static_cast<SDL_Window *>(window_), &w, &h);
	config_.width = w;
	config_.height = h;

#if defined(__EMSCRIPTEN__)
	// Create a WebGL2 (OpenGL ES 3.0) context for the native renderer.
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
	SDL_GLContext glContext =
	    SDL_GL_CreateContext(static_cast<SDL_Window *>(window_));
	if (!glContext) {
		spdlog::error("SDL_GL_CreateContext failed: {}", SDL_GetError());
		SDL_DestroyWindow(static_cast<SDL_Window *>(window_));
		window_ = nullptr;
		SDL_Quit();
		return false;
	}
	SDL_GL_MakeCurrent(static_cast<SDL_Window *>(window_), glContext);
	backendHandle_ = glContext;
	SDL_GL_SetSwapInterval(config_.vsync ? 1 : 0);
#else
#if defined(NDEBUG)
	constexpr bool gpuDebugMode = false;
#else
	constexpr bool gpuDebugMode = true;
#endif
#if defined(_WIN32)
	backendHandle_ = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXBC |
	                                         SDL_GPU_SHADERFORMAT_DXIL,
	                                     gpuDebugMode, nullptr);
#elif defined(__APPLE__)
	backendHandle_ = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_METALLIB,
	                                     gpuDebugMode, nullptr);
#else
	backendHandle_ =
	    SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, gpuDebugMode, nullptr);
#endif
	if (!backendHandle_) {
		spdlog::error("SDL_CreateGPUDevice failed: {}", SDL_GetError());
		SDL_DestroyWindow(static_cast<SDL_Window *>(window_));
		window_ = nullptr;
		SDL_Quit();
		return false;
	}
	auto *gpuDevice = static_cast<SDL_GPUDevice *>(backendHandle_);

	if (!SDL_ClaimWindowForGPUDevice(gpuDevice,
	                                 static_cast<SDL_Window *>(window_))) {
		spdlog::error("SDL_ClaimWindowForGPUDevice failed: {}", SDL_GetError());
		SDL_DestroyGPUDevice(gpuDevice);
		backendHandle_ = nullptr;
		SDL_DestroyWindow(static_cast<SDL_Window *>(window_));
		window_ = nullptr;
		SDL_Quit();
		return false;
	}
	if (!applyPresentMode()) {
		spdlog::warn("Failed to configure GPU present mode: {}",
		             SDL_GetError());
	}
	if (!SDL_SetGPUAllowedFramesInFlight(gpuDevice, 3)) {
		spdlog::warn("Failed to set GPU frames in flight: {}", SDL_GetError());
	}
#endif

	renderer_ = std::make_unique<Renderer>();
	if (!renderer_->initialize(window_, backendHandle_)) {
		spdlog::error("Renderer initialization failed!");
		renderer_.reset();
		if (backendHandle_) {
#if defined(__EMSCRIPTEN__)
			SDL_GL_DestroyContext(static_cast<SDL_GLContext>(backendHandle_));
#else
			SDL_DestroyGPUDevice(static_cast<SDL_GPUDevice *>(backendHandle_));
#endif
			backendHandle_ = nullptr;
		}
		SDL_DestroyWindow(static_cast<SDL_Window *>(window_));
		window_ = nullptr;
		SDL_Quit();
		return false;
	}

	audio_ = std::make_unique<AudioManager>();
	if (!audio_->initialize()) {
		spdlog::warn("Audio initialization failed - continuing without audio");
	}

	resourceManager_ = std::make_unique<ResourceManager>();
	resourceManager_->setDevice(backendHandle_);
	if (!resourceManager_->initialize()) {
		spdlog::warn("ResourceManager initialization failed - continuing "
		             "without resource manager");
	}

	renderer_->setViewport(0, 0, config_.width, config_.height);

	spdlog::info("Real-time frontend initialized: window={}x{}", config_.width,
	             config_.height);

	isInitialized_ = true;
	return true;
}

void Realtime::shutdown() {
	if (!isInitialized_) {
		return;
	}

	// GPU-backed resources must be released before their device.
	resourceManager_.reset();
	renderer_.reset();
	audio_.reset();

	if (backendHandle_) {
#if defined(__EMSCRIPTEN__)
		SDL_GL_DestroyContext(static_cast<SDL_GLContext>(backendHandle_));
#else
		SDL_DestroyGPUDevice(static_cast<SDL_GPUDevice *>(backendHandle_));
#endif
		backendHandle_ = nullptr;
	}

	if (window_) {
		SDL_DestroyWindow(static_cast<SDL_Window *>(window_));
		window_ = nullptr;
	}

	SDL_Quit();

	isInitialized_ = false;
}

void Realtime::run(const UpdateCallback &update, const RenderCallback &render) {
	if (!isInitialized_ && !initialize()) {
		return;
	}

	isRunning_ = true;
	lastFrameTime_ = SDL_GetPerformanceCounter();

#if defined(__EMSCRIPTEN__)
	storedUpdate_ = update;
	storedRender_ = render;
	emscripten_set_main_loop_arg(wasmMainLoop, this, 0, 1);
#else
	while (isRunning_) {
		runFrame(update, render);
	}
#endif
}

void Realtime::runFrame(const UpdateCallback &update,
                        const RenderCallback &render) {
	processEvents();

	calculateDeltaTime();

	if (renderer_) {
		renderer_->beginFrame();
		renderer_->clear({0.2f, 0.2f, 0.2f, 1.0f});
	}

	update(deltaTime_);

	render(deltaTime_);

	if (renderer_) {
		renderer_->endFrame();
	}

	limitFrameRate();

	frameCount_++;
	fpsTimer_ += deltaTime_;
	if (fpsTimer_ >= 1.0f) {
		fps_ = static_cast<float>(frameCount_) / fpsTimer_;
		frameCount_ = 0;
		fpsTimer_ = 0.0f;
	}
}

#if defined(__EMSCRIPTEN__)
void Realtime::wasmMainLoop(void *opaque) {
	auto *frontend = static_cast<Realtime *>(opaque);
	frontend->runFrame(frontend->storedUpdate_, frontend->storedRender_);
	if (!frontend->isRunning_) {
		emscripten_cancel_main_loop();
		emscripten_force_exit(0);
	}
}
#endif

void Realtime::stop() { isRunning_ = false; }

void Realtime::setWindowTitle(const std::string &title) {
	config_.title = title;
	if (window_) {
		SDL_SetWindowTitle(static_cast<SDL_Window *>(window_), title.c_str());
	}
}

void Realtime::setWindowSize(int width, int height) {
	config_.width = width;
	config_.height = height;
	if (window_) {
		SDL_SetWindowSize(static_cast<SDL_Window *>(window_), width, height);
	}
}

void Realtime::setFullscreen(bool fullscreen) {
	config_.fullscreen = fullscreen;
	if (window_) {
		SDL_SetWindowFullscreen(static_cast<SDL_Window *>(window_), fullscreen);
	}
}

void Realtime::setVSync(bool enabled) {
	config_.vsync = enabled;
	if (!isInitialized_ || !backendHandle_) {
		return;
	}
#if defined(__EMSCRIPTEN__)
	SDL_GL_SetSwapInterval(enabled ? 1 : 0);
#else
	applyPresentMode();
#endif
}

void Realtime::setTargetFps(int targetFps) {
	config_.targetFps = std::max(0, targetFps);
}

#if !defined(__EMSCRIPTEN__)
bool Realtime::applyPresentMode() {
	if (!backendHandle_ || !window_) {
		return false;
	}
	auto *device = static_cast<SDL_GPUDevice *>(backendHandle_);
	auto *window = static_cast<SDL_Window *>(window_);
	const SDL_GPUPresentMode mode = config_.vsync
	                                    ? SDL_GPU_PRESENTMODE_VSYNC
	                                    : SDL_GPU_PRESENTMODE_IMMEDIATE;
	if (!SDL_WindowSupportsGPUPresentMode(device, window, mode)) {
		spdlog::warn("Requested GPU present mode {} is unsupported",
		             config_.vsync ? "VSYNC" : "IMMEDIATE");
		return false;
	}
	return SDL_SetGPUSwapchainParameters(
	    device, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode);
}
#endif // !defined(__EMSCRIPTEN__)

Renderer *Realtime::getRenderer() const { return renderer_.get(); }

AudioManager *Realtime::getAudio() const { return audio_.get(); }

ResourceManager *Realtime::getResourceManager() const {
	return resourceManager_.get();
}

void Realtime::getWindowSize(int &width, int &height) const {
	if (window_) {
		SDL_GetWindowSize(static_cast<SDL_Window *>(window_), &width, &height);
	} else {
		width = config_.width;
		height = config_.height;
	}
}

void Realtime::processEvents() {
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		if (event.type == SDL_EVENT_QUIT) {
			isRunning_ = false;
		} else if (event.type == SDL_EVENT_WINDOW_RESIZED) {
			int newWidth = event.window.data1;
			int newHeight = event.window.data2;
			config_.width = newWidth;
			config_.height = newHeight;

			if (resizeCallback_) {
				resizeCallback_(newWidth, newHeight);
			}
		} else if (event.type == SDL_EVENT_KEY_DOWN ||
		           event.type == SDL_EVENT_KEY_UP) {
			bool pressed = (event.type == SDL_EVENT_KEY_DOWN);
			int key = static_cast<int>(event.key.key);

			if (keyCallback_) {
				keyCallback_(key, pressed);
			}
		}
	}
}

void Realtime::calculateDeltaTime() {
	uint64_t currentTime = SDL_GetPerformanceCounter();
	if (lastFrameTime_ == 0) {
		lastFrameTime_ = currentTime;
	}
	uint64_t frequency = SDL_GetPerformanceFrequency();
	deltaTime_ = static_cast<float>(currentTime - lastFrameTime_) /
	             static_cast<float>(frequency);
	lastFrameTime_ = currentTime;
}

void Realtime::limitFrameRate() {
	if (config_.targetFps > 0) {
		float frameTime = 1.0f / static_cast<float>(config_.targetFps);
		if (deltaTime_ < frameTime) {
			uint32_t delayMs =
			    static_cast<uint32_t>((frameTime - deltaTime_) * 1000.0f);
			if (delayMs > 0) {
				SDL_Delay(delayMs);
			}
		}
	}
}

} // namespace shiki::frontend

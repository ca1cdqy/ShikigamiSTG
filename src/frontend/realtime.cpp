#include <SDL3/SDL.h>
#include <algorithm>
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
      window_(other.window_), gpuDevice_(other.gpuDevice_) {
	other.window_ = nullptr;
	other.gpuDevice_ = nullptr;
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
		gpuDevice_ = other.gpuDevice_;
		other.window_ = nullptr;
		other.gpuDevice_ = nullptr;
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

	window_ = SDL_CreateWindow(config_.title.c_str(), config_.width,
	                           config_.height, SDL_WINDOW_RESIZABLE);

	if (!window_) {
		spdlog::error("SDL_CreateWindow failed: {}", SDL_GetError());
		SDL_Quit();
		return false;
	}

#if defined(NDEBUG)
	constexpr bool gpuDebugMode = false;
#else
	constexpr bool gpuDebugMode = true;
#endif
#if defined(_WIN32)
	gpuDevice_ = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXBC |
	                                     SDL_GPU_SHADERFORMAT_DXIL,
	                                 gpuDebugMode, nullptr);
#elif defined(__APPLE__)
	gpuDevice_ = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_METALLIB,
	                                 gpuDebugMode, nullptr);
#else
	gpuDevice_ =
	    SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, gpuDebugMode, nullptr);
#endif
	if (!gpuDevice_) {
		spdlog::error("SDL_CreateGPUDevice failed: {}", SDL_GetError());
		SDL_DestroyWindow(static_cast<SDL_Window *>(window_));
		window_ = nullptr;
		SDL_Quit();
		return false;
	}

	int w, h;
	SDL_GetWindowSize(static_cast<SDL_Window *>(window_), &w, &h);
	config_.width = w;
	config_.height = h;

	if (!SDL_ClaimWindowForGPUDevice(static_cast<SDL_GPUDevice *>(gpuDevice_),
	                                 static_cast<SDL_Window *>(window_))) {
		spdlog::error("SDL_ClaimWindowForGPUDevice failed: {}", SDL_GetError());
		SDL_DestroyGPUDevice(static_cast<SDL_GPUDevice *>(gpuDevice_));
		gpuDevice_ = nullptr;
		SDL_DestroyWindow(static_cast<SDL_Window *>(window_));
		window_ = nullptr;
		SDL_Quit();
		return false;
	}
	if (!applyPresentMode()) {
		spdlog::warn("Failed to configure GPU present mode: {}",
		             SDL_GetError());
	}
	if (!SDL_SetGPUAllowedFramesInFlight(
	        static_cast<SDL_GPUDevice *>(gpuDevice_), 3)) {
		spdlog::warn("Failed to set GPU frames in flight: {}", SDL_GetError());
	}

	renderer_ = std::make_unique<Renderer>();
	if (!renderer_->initialize(window_, gpuDevice_)) {
		spdlog::error("Renderer initialization failed!");
		renderer_.reset();
		SDL_DestroyGPUDevice(static_cast<SDL_GPUDevice *>(gpuDevice_));
		gpuDevice_ = nullptr;
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
	resourceManager_->setDevice(gpuDevice_);
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

	if (gpuDevice_) {
		SDL_DestroyGPUDevice(static_cast<SDL_GPUDevice *>(gpuDevice_));
		gpuDevice_ = nullptr;
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

	while (isRunning_) {
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
}

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
	if (isInitialized_) {
		applyPresentMode();
	}
}

void Realtime::setTargetFps(int targetFps) {
	config_.targetFps = std::max(0, targetFps);
}

bool Realtime::applyPresentMode() {
	if (!gpuDevice_ || !window_) {
		return false;
	}
	auto *device = static_cast<SDL_GPUDevice *>(gpuDevice_);
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

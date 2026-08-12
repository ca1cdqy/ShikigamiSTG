#if defined(__EMSCRIPTEN__)

#include <GLES3/gl3.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <ft2build.h>
#include <shiki/render/renderer.h>
#include <shiki/render/sprite.h>
#include <shiki/render/texture.h>
#include <spdlog/spdlog.h>
#include <sstream>
#include <string>
#include <vector>
#include FT_FREETYPE_H

namespace shiki {

namespace {

/** Maps a BlendMode to the equivalent WebGL blend function pair. */
void applyWebGlBlend(BlendMode mode) {
	if (mode == BlendMode::None) {
		glDisable(GL_BLEND);
		return;
	}
	glEnable(GL_BLEND);
	switch (mode) {
	case BlendMode::Add:
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE);
		break;
	case BlendMode::Multiply:
		glBlendFuncSeparate(GL_DST_COLOR, GL_ZERO, GL_ONE,
		                    GL_ONE_MINUS_SRC_ALPHA);
		break;
	case BlendMode::Screen:
	case BlendMode::Alpha:
	default:
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA,
		                    GL_ONE_MINUS_SRC_ALPHA);
		break;
	}
}

/** Reads a shader source file from the Emscripten virtual filesystem. */
bool loadShaderSource(const char *name, std::string &out) {
	const char *searchPaths[] = {"shaders/", "build/shaders/", "/shaders/"};
	for (const char *dir : searchPaths) {
		std::ifstream stream(std::string(dir) + name, std::ios::binary);
		if (!stream) {
			continue;
		}
		std::ostringstream buffer;
		buffer << stream.rdbuf();
		out = buffer.str();
		return !out.empty();
	}
	return false;
}

/** Compiles one GLSL ES shader stage. Returns the GL object id or zero. */
unsigned int compileShader(GLenum stage, const std::string &source) {
	const unsigned int shader = glCreateShader(stage);
	if (!shader) {
		return 0;
	}
	const char *sourcePtr = source.c_str();
	glShaderSource(shader, 1, &sourcePtr, nullptr);
	glCompileShader(shader);
	int status = GL_FALSE;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if (status != GL_TRUE) {
		char log[2048] = {};
		glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
		spdlog::error("WebGL shader compile failed: {}", log);
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

} // namespace

Renderer::Renderer() = default;

Renderer::~Renderer() { shutdown(); }

Renderer::Renderer(Renderer &&other) noexcept
    : fontLibrary_(other.fontLibrary_),
      defaultFontFace_(other.defaultFontFace_),
      textCache_(std::move(other.textCache_)),
      drawCallCount_(other.drawCallCount_), spriteCount_(other.spriteCount_),
      currentBlendMode_(other.currentBlendMode_), viewport_(other.viewport_),
      outputWidth_(other.outputWidth_), outputHeight_(other.outputHeight_),
      clearColor_(other.clearColor_), projectionLeft_(other.projectionLeft_),
      projectionRight_(other.projectionRight_),
      projectionBottom_(other.projectionBottom_),
      projectionTop_(other.projectionTop_),
      playfieldRegion_(other.playfieldRegion_), window_(other.window_),
      backend_(other.backend_), vertices_(std::move(other.vertices_)),
      indices_(std::move(other.indices_)),
      spriteDraws_(std::move(other.spriteDraws_)),
      webglProgram_(other.webglProgram_),
      webglVertexShader_(other.webglVertexShader_),
      webglFragmentShader_(other.webglFragmentShader_),
      webglVao_(other.webglVao_), webglVbo_(other.webglVbo_),
      webglIbo_(other.webglIbo_), webglWhiteTexture_(other.webglWhiteTexture_),
      webglUniformTexture_(other.webglUniformTexture_),
      webglNeedsClear_(other.webglNeedsClear_) {
	other.window_ = nullptr;
	other.backend_ = nullptr;
	other.webglProgram_ = 0;
	other.webglVertexShader_ = 0;
	other.webglFragmentShader_ = 0;
	other.webglVao_ = 0;
	other.webglVbo_ = 0;
	other.webglIbo_ = 0;
	other.webglWhiteTexture_ = 0;
	other.webglUniformTexture_ = -1;
	other.fontLibrary_ = nullptr;
	other.defaultFontFace_ = nullptr;
}

Renderer &Renderer::operator=(Renderer &&other) noexcept {
	if (this != &other) {
		shutdown();
		fontLibrary_ = other.fontLibrary_;
		defaultFontFace_ = other.defaultFontFace_;
		textCache_ = std::move(other.textCache_);
		drawCallCount_ = other.drawCallCount_;
		spriteCount_ = other.spriteCount_;
		currentBlendMode_ = other.currentBlendMode_;
		viewport_ = other.viewport_;
		outputWidth_ = other.outputWidth_;
		outputHeight_ = other.outputHeight_;
		clearColor_ = other.clearColor_;
		projectionLeft_ = other.projectionLeft_;
		projectionRight_ = other.projectionRight_;
		projectionBottom_ = other.projectionBottom_;
		projectionTop_ = other.projectionTop_;
		playfieldRegion_ = other.playfieldRegion_;
		window_ = other.window_;
		backend_ = other.backend_;

		vertices_ = std::move(other.vertices_);
		indices_ = std::move(other.indices_);
		spriteDraws_ = std::move(other.spriteDraws_);
		webglProgram_ = other.webglProgram_;
		webglVertexShader_ = other.webglVertexShader_;
		webglFragmentShader_ = other.webglFragmentShader_;
		webglVao_ = other.webglVao_;
		webglVbo_ = other.webglVbo_;
		webglIbo_ = other.webglIbo_;
		webglWhiteTexture_ = other.webglWhiteTexture_;
		webglUniformTexture_ = other.webglUniformTexture_;
		webglNeedsClear_ = other.webglNeedsClear_;
		other.window_ = nullptr;
		other.backend_ = nullptr;
		other.webglProgram_ = 0;
		other.webglVertexShader_ = 0;
		other.webglFragmentShader_ = 0;
		other.webglVao_ = 0;
		other.webglVbo_ = 0;
		other.webglIbo_ = 0;
		other.webglWhiteTexture_ = 0;
		other.webglUniformTexture_ = -1;
		other.fontLibrary_ = nullptr;
		other.defaultFontFace_ = nullptr;
	}
	return *this;
}

bool Renderer::initialize(void *window, void *device) {
	window_ = window;
	backend_ = device;
	if (!backend_) {
		spdlog::error("Renderer::initialize: GL context is null");
		return false;
	}

	// 1x1 white texture used for untextured colored draws.
	const uint8_t whitePixel[4] = {255, 255, 255, 255};
	glGenTextures(1, &webglWhiteTexture_);
	glBindTexture(GL_TEXTURE_2D, webglWhiteTexture_);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
	             whitePixel);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);

	if (!createShadersAndBuffers()) {
		spdlog::error("Failed to initialize WebGL2 renderer resources");
		return false;
	}

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA,
	                    GL_ONE_MINUS_SRC_ALPHA);

	viewport_ = Rect(0.0f, 0.0f, 640.0f, 480.0f);
	outputWidth_ = 640;
	outputHeight_ = 480;
	webglNeedsClear_ = true;

	spdlog::info("Renderer initialized (WebGL2 backend)");
	return true;
}

bool Renderer::createShadersAndBuffers() {
	std::string vertexSource;
	std::string fragmentSource;
	if (!loadShaderSource("sprite.vert.glsl", vertexSource) ||
	    !loadShaderSource("sprite.frag.glsl", fragmentSource)) {
		spdlog::error("WebGL shader sources not found under /shaders; run a "
		              "desktop build to generate them");
		return false;
	}

	webglVertexShader_ = compileShader(GL_VERTEX_SHADER, vertexSource);
	webglFragmentShader_ = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
	if (!webglVertexShader_ || !webglFragmentShader_) {
		return false;
	}

	webglProgram_ = glCreateProgram();
	glAttachShader(webglProgram_, webglVertexShader_);
	glAttachShader(webglProgram_, webglFragmentShader_);
	glLinkProgram(webglProgram_);
	int status = GL_FALSE;
	glGetProgramiv(webglProgram_, GL_LINK_STATUS, &status);
	if (status != GL_TRUE) {
		char log[2048] = {};
		glGetProgramInfoLog(webglProgram_, sizeof(log), nullptr, log);
		spdlog::error("WebGL program link failed: {}", log);
		return false;
	}
	glDeleteShader(webglVertexShader_);
	glDeleteShader(webglFragmentShader_);
	webglVertexShader_ = 0;
	webglFragmentShader_ = 0;

	// The build pipeline renames the combined sampler to uSprite.
	webglUniformTexture_ = glGetUniformLocation(webglProgram_, "uSprite");
	if (webglUniformTexture_ < 0) {
		webglUniformTexture_ = glGetUniformLocation(
		    webglProgram_, "SPIRV_Cross_CombinedspriteTexturespriteSampler");
	}
	glUseProgram(webglProgram_);
	if (webglUniformTexture_ >= 0) {
		glUniform1i(webglUniformTexture_, 0);
	}
	glUseProgram(0);

	glGenVertexArrays(1, &webglVao_);
	glGenBuffers(1, &webglVbo_);
	glGenBuffers(1, &webglIbo_);

	glBindVertexArray(webglVao_);
	glBindBuffer(GL_ARRAY_BUFFER, webglVbo_);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, webglIbo_);

	constexpr GLsizei stride = static_cast<GLsizei>(sizeof(Vertex));
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride,
	                      reinterpret_cast<const void *>(0));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride,
	                      reinterpret_cast<const void *>(8));
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
	                      reinterpret_cast<const void *>(24));
	glEnableVertexAttribArray(3);
	glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride,
	                      reinterpret_cast<const void *>(32));
	glEnableVertexAttribArray(4);
	glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, stride,
	                      reinterpret_cast<const void *>(48));
	glBindVertexArray(0);

	return true;
}

void Renderer::shutdown() {
	textCache_.clear();
	if (defaultFontFace_) {
		FT_Done_Face(static_cast<FT_Face>(defaultFontFace_));
		defaultFontFace_ = nullptr;
	}
	if (fontLibrary_) {
		FT_Done_FreeType(static_cast<FT_Library>(fontLibrary_));
		fontLibrary_ = nullptr;
	}
	if (webglProgram_) {
		glDeleteProgram(webglProgram_);
		webglProgram_ = 0;
	}
	if (webglVertexShader_) {
		glDeleteShader(webglVertexShader_);
		webglVertexShader_ = 0;
	}
	if (webglFragmentShader_) {
		glDeleteShader(webglFragmentShader_);
		webglFragmentShader_ = 0;
	}
	if (webglVao_) {
		glDeleteVertexArrays(1, &webglVao_);
		webglVao_ = 0;
	}
	if (webglVbo_) {
		glDeleteBuffers(1, &webglVbo_);
		webglVbo_ = 0;
	}
	if (webglIbo_) {
		glDeleteBuffers(1, &webglIbo_);
		webglIbo_ = 0;
	}
	if (webglWhiteTexture_) {
		glDeleteTextures(1, &webglWhiteTexture_);
		webglWhiteTexture_ = 0;
	}
	backend_ = nullptr;
	window_ = nullptr;
}

void Renderer::beginFrame() {
	drawCallCount_ = 0;
	spriteCount_ = 0;
	webglNeedsClear_ = true;
}

void Renderer::endFrame() {
	if (!backend_) {
		return;
	}
	applyPendingClear();
	if (!vertices_.empty()) {
		flushBatch();
	}
	SDL_GL_SwapWindow(static_cast<SDL_Window *>(window_));
}

void Renderer::clear(const Color &color) { clearColor_ = color; }

void Renderer::applyPendingClear() {
	if (!backend_ || !webglNeedsClear_) {
		return;
	}
	glClearColor(clearColor_.x, clearColor_.y, clearColor_.z, clearColor_.w);
	glClear(GL_COLOR_BUFFER_BIT);
	webglNeedsClear_ = false;
}

void Renderer::flushBatch() {
	if (vertices_.empty()) {
		return;
	}

	glBindVertexArray(webglVao_);
	glUseProgram(webglProgram_);

	glBindBuffer(GL_ARRAY_BUFFER, webglVbo_);
	glBufferData(GL_ARRAY_BUFFER,
	             static_cast<GLsizeiptr>(vertices_.size() * sizeof(Vertex)),
	             vertices_.data(), GL_DYNAMIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, webglIbo_);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER,
	             static_cast<GLsizeiptr>(indices_.size() * sizeof(uint16_t)),
	             indices_.data(), GL_DYNAMIC_DRAW);

	const int outputH = std::max(outputHeight_, 1);

	// Preserve submission order while merging adjacent compatible sprites.
	for (size_t drawIndex = 0; drawIndex < spriteDraws_.size();) {
		const auto &info = spriteDraws_[drawIndex];
		uint32_t batchedIndexCount = info.indexCount;
		size_t nextDraw = drawIndex + 1;
		while (nextDraw < spriteDraws_.size()) {
			const auto &next = spriteDraws_[nextDraw];
			if (next.texture != info.texture ||
			    next.blendMode != info.blendMode ||
			    next.playfieldSpace != info.playfieldSpace ||
			    next.windowSpace != info.windowSpace ||
			    next.repeatTexture != info.repeatTexture ||
			    next.indexOffset != info.indexOffset + batchedIndexCount) {
				break;
			}
			batchedIndexCount += next.indexCount;
			++nextDraw;
		}

		// Compute the pixel viewport for this draw category, mirroring the
		// desktop SDL_GPU backend. GL viewports and scissors are bottom-left
		// origin, so flip the Y axis.
		Rect region = viewport_;
		if (info.windowSpace) {
			region = {0.0f, 0.0f, static_cast<float>(outputWidth_),
			          static_cast<float>(outputHeight_)};
		} else if (info.playfieldSpace) {
			const float logicalWidth =
			    std::max(projectionRight_ - projectionLeft_, 1e-6f);
			const float logicalHeight =
			    std::max(projectionBottom_ - projectionTop_, 1e-6f);
			const float playfieldWidth = std::min(
			    viewport_.width,
			    viewport_.width * playfieldRegion_.width / logicalWidth);
			const float playfieldHeight = std::min(
			    viewport_.height,
			    viewport_.height * playfieldRegion_.height / logicalHeight);
			region = {viewport_.x +
			              viewport_.width * playfieldRegion_.x / logicalWidth,
			          viewport_.y +
			              viewport_.height * playfieldRegion_.y / logicalHeight,
			          playfieldWidth, playfieldHeight};
		}

		const int viewportX = static_cast<int>(std::floor(region.x));
		const int viewportY = outputH - static_cast<int>(std::floor(region.y)) -
		                      static_cast<int>(std::ceil(region.height));
		const int viewportW = static_cast<int>(std::ceil(region.width));
		const int viewportH = static_cast<int>(std::ceil(region.height));
		glViewport(viewportX, viewportY, viewportW, viewportH);
		glEnable(GL_SCISSOR_TEST);
		glScissor(viewportX, viewportY, viewportW, viewportH);

		applyWebGlBlend(info.blendMode);

		const GLuint texture =
		    info.texture
		        ? static_cast<GLuint>(reinterpret_cast<uintptr_t>(info.texture))
		        : webglWhiteTexture_;
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
		                info.repeatTexture ? GL_REPEAT : GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
		                info.repeatTexture ? GL_REPEAT : GL_CLAMP_TO_EDGE);

		glDrawElements(
		    GL_TRIANGLES, static_cast<GLsizei>(batchedIndexCount),
		    GL_UNSIGNED_SHORT,
		    reinterpret_cast<const void *>(
		        static_cast<uintptr_t>(info.indexOffset) * sizeof(uint16_t)));
		++drawCallCount_;

		drawIndex = nextDraw;
	}

	glDisable(GL_SCISSOR_TEST);
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindVertexArray(0);
	glUseProgram(0);

	vertices_.clear();
	indices_.clear();
	spriteDraws_.clear();
}

} // namespace shiki

#endif // defined(__EMSCRIPTEN__)
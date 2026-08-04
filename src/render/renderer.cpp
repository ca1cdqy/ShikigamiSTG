#include <SDL3/SDL.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ft2build.h>
#include <shiki/render/renderer.h>
#include <shiki/render/shader_compiler.h>
#include <shiki/render/sprite.h>
#include <shiki/render/texture.h>
#include <spdlog/spdlog.h>
#include FT_FREETYPE_H
#include FT_SYNTHESIS_H
#include <vector>

namespace shiki {

Renderer::Renderer() = default;

Renderer::~Renderer() { shutdown(); }

Renderer::Renderer(Renderer &&other) noexcept
    : device_(other.device_), commandBuffer_(other.commandBuffer_),
      renderPass_(other.renderPass_),
      swapchainTexture_(other.swapchainTexture_),
      vertexBuffer_(other.vertexBuffer_), indexBuffer_(other.indexBuffer_),
      frameTransferBuffer_(other.frameTransferBuffer_),
      sampler_(other.sampler_), repeatSampler_(other.repeatSampler_),
      vertexShader_(other.vertexShader_),
      fragmentShader_(other.fragmentShader_), pipeline_(other.pipeline_),
      additivePipeline_(other.additivePipeline_),
      drawCallCount_(other.drawCallCount_), spriteCount_(other.spriteCount_),
      currentBlendMode_(other.currentBlendMode_), viewport_(other.viewport_),
      outputWidth_(other.outputWidth_), outputHeight_(other.outputHeight_),
      clearColor_(other.clearColor_), projectionLeft_(other.projectionLeft_),
      projectionRight_(other.projectionRight_),
      projectionBottom_(other.projectionBottom_),
      projectionTop_(other.projectionTop_),
      playfieldRegion_(other.playfieldRegion_), window_(other.window_),
      fontLibrary_(other.fontLibrary_),
      defaultFontFace_(other.defaultFontFace_),
      textCache_(std::move(other.textCache_)) {
	other.device_ = nullptr;
	other.commandBuffer_ = nullptr;
	other.renderPass_ = nullptr;
	other.swapchainTexture_ = nullptr;
	other.vertexBuffer_ = nullptr;
	other.indexBuffer_ = nullptr;
	other.frameTransferBuffer_ = nullptr;
	other.sampler_ = nullptr;
	other.repeatSampler_ = nullptr;
	other.vertexShader_ = nullptr;
	other.fragmentShader_ = nullptr;
	other.pipeline_ = nullptr;
	other.additivePipeline_ = nullptr;
	other.window_ = nullptr;
	other.fontLibrary_ = nullptr;
	other.defaultFontFace_ = nullptr;
}

Renderer &Renderer::operator=(Renderer &&other) noexcept {
	if (this != &other) {
		shutdown();
		device_ = other.device_;
		commandBuffer_ = other.commandBuffer_;
		renderPass_ = other.renderPass_;
		swapchainTexture_ = other.swapchainTexture_;
		vertexBuffer_ = other.vertexBuffer_;
		indexBuffer_ = other.indexBuffer_;
		frameTransferBuffer_ = other.frameTransferBuffer_;
		sampler_ = other.sampler_;
		repeatSampler_ = other.repeatSampler_;
		vertexShader_ = other.vertexShader_;
		fragmentShader_ = other.fragmentShader_;
		pipeline_ = other.pipeline_;
		additivePipeline_ = other.additivePipeline_;
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
		fontLibrary_ = other.fontLibrary_;
		defaultFontFace_ = other.defaultFontFace_;
		textCache_ = std::move(other.textCache_);
		other.device_ = nullptr;
		other.commandBuffer_ = nullptr;
		other.renderPass_ = nullptr;
		other.swapchainTexture_ = nullptr;
		other.vertexBuffer_ = nullptr;
		other.indexBuffer_ = nullptr;
		other.frameTransferBuffer_ = nullptr;
		other.sampler_ = nullptr;
		other.repeatSampler_ = nullptr;
		other.vertexShader_ = nullptr;
		other.fragmentShader_ = nullptr;
		other.pipeline_ = nullptr;
		other.additivePipeline_ = nullptr;
		other.window_ = nullptr;
		other.fontLibrary_ = nullptr;
		other.defaultFontFace_ = nullptr;
	}
	return *this;
}

bool Renderer::initialize(void *window, void *device) {
	window_ = window;
	device_ = static_cast<SDL_GPUDevice *>(device);

	if (!device_) {
		spdlog::error("Renderer::initialize: device is null");
		return false;
	}

	SDL_GPUSamplerCreateInfo samplerInfo = {};
	samplerInfo.min_filter = SDL_GPU_FILTER_LINEAR;
	samplerInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
	samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
	samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	sampler_ = SDL_CreateGPUSampler(device_, &samplerInfo);
	if (!sampler_) {
		spdlog::error("Failed to create sampler: {}", SDL_GetError());
		return false;
	}
	samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
	samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
	samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
	repeatSampler_ = SDL_CreateGPUSampler(device_, &samplerInfo);
	if (!repeatSampler_) {
		spdlog::error("Failed to create repeat sampler: {}", SDL_GetError());
		return false;
	}

	SDL_GPUTextureCreateInfo whiteTextureInfo = {};
	whiteTextureInfo.type = SDL_GPU_TEXTURETYPE_2D;
	whiteTextureInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	whiteTextureInfo.width = 1;
	whiteTextureInfo.height = 1;
	whiteTextureInfo.layer_count_or_depth = 1;
	whiteTextureInfo.num_levels = 1;
	whiteTextureInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
	defaultTexture_ = SDL_CreateGPUTexture(device_, &whiteTextureInfo);
	if (!defaultTexture_) {
		spdlog::error("Failed to create default texture: {}", SDL_GetError());
		return false;
	}

	uint8_t whitePixel[4] = {255, 255, 255, 255};
	SDL_GPUTransferBufferCreateInfo whiteTransferInfo = {};
	whiteTransferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
	whiteTransferInfo.size = 4;
	SDL_GPUTransferBuffer *whiteTransfer =
	    SDL_CreateGPUTransferBuffer(device_, &whiteTransferInfo);
	if (whiteTransfer) {
		void *mapped = SDL_MapGPUTransferBuffer(device_, whiteTransfer, false);
		if (mapped) {
			std::memcpy(mapped, whitePixel, 4);
			SDL_UnmapGPUTransferBuffer(device_, whiteTransfer);
		}
		SDL_GPUCommandBuffer *whiteCmd = SDL_AcquireGPUCommandBuffer(device_);
		SDL_GPUCopyPass *whiteCopyPass = SDL_BeginGPUCopyPass(whiteCmd);
		SDL_GPUTextureTransferInfo whiteSrc = {};
		whiteSrc.transfer_buffer = whiteTransfer;
		SDL_GPUTextureRegion whiteDst = {};
		whiteDst.texture = defaultTexture_;
		whiteDst.w = 1;
		whiteDst.h = 1;
		whiteDst.d = 1;
		SDL_UploadToGPUTexture(whiteCopyPass, &whiteSrc, &whiteDst, false);
		SDL_EndGPUCopyPass(whiteCopyPass);
		SDL_SubmitGPUCommandBuffer(whiteCmd);
		SDL_ReleaseGPUTransferBuffer(device_, whiteTransfer);
	}

	if (!createBuffers()) {
		spdlog::error("Failed to create buffers");
		return false;
	}

	if (!createPipeline()) {
		spdlog::warn("Failed to create pipeline - rendering will be limited");
	}

	viewport_ = Rect(0.0f, 0.0f, 640.0f, 480.0f);

	spdlog::info("Renderer initialized successfully");
	return true;
}

bool Renderer::createBuffers() {
	SDL_GPUBufferCreateInfo vertexBufferInfo = {};
	vertexBufferInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
	vertexBufferInfo.size = static_cast<Uint32>(MAX_VERTICES * sizeof(Vertex));
	vertexBuffer_ = SDL_CreateGPUBuffer(device_, &vertexBufferInfo);
	if (!vertexBuffer_) {
		spdlog::error("Failed to create vertex buffer: {}", SDL_GetError());
		return false;
	}

	SDL_GPUBufferCreateInfo indexBufferInfo = {};
	indexBufferInfo.usage = SDL_GPU_BUFFERUSAGE_INDEX;
	indexBufferInfo.size = static_cast<Uint32>(MAX_INDICES * sizeof(uint16_t));
	indexBuffer_ = SDL_CreateGPUBuffer(device_, &indexBufferInfo);
	if (!indexBuffer_) {
		spdlog::error("Failed to create index buffer: {}", SDL_GetError());
		return false;
	}

	SDL_GPUTransferBufferCreateInfo transferInfo = {};
	transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
	transferInfo.size = static_cast<Uint32>(MAX_VERTICES * sizeof(Vertex) +
	                                        MAX_INDICES * sizeof(uint16_t));
	frameTransferBuffer_ = SDL_CreateGPUTransferBuffer(device_, &transferInfo);
	if (!frameTransferBuffer_) {
		spdlog::error("Failed to create persistent frame transfer buffer: {}",
		              SDL_GetError());
		return false;
	}

	return true;
}

bool Renderer::createPipeline() {
	ShaderFormat format = ShaderCompiler::getSupportedFormat();
	std::string ext = ShaderCompiler::getShaderExtension(format);

	std::vector<std::string> possibleShaderDirs = {
	    "shaders",
	    "build/windows/x64/release/shaders",
	    "build/windows/x64/debug/shaders",
	    "build/linux/x86_64/release/shaders",
	    "build/linux/x86_64/debug/shaders",
	    "build/macosx/arm64/release/shaders",
	    "build/macosx/x86_64/release/shaders",
	    "build/shaders",
	    "../shaders",
	    "../../shaders",
	};

	std::string vertexShaderPath;
	std::string fragmentShaderPath;
	bool found = false;

	for (const auto &dir : possibleShaderDirs) {
		vertexShaderPath = dir + "/sprite.vert" + ext;
		fragmentShaderPath = dir + "/sprite.frag" + ext;

		std::ifstream testFile(vertexShaderPath, std::ios::binary);
		if (testFile.good()) {
			testFile.close();
			found = true;
			spdlog::info("Found shaders in: {}", dir);
			break;
		}
	}

	if (!found) {
		spdlog::error("Could not find shader files in any search path");
		return false;
	}

	ShaderData vertexShaderData =
	    ShaderCompiler::loadFromFile(vertexShaderPath, ShaderType::Vertex);
	if (!vertexShaderData.isValid()) {
		spdlog::error("Failed to load vertex shader: {}", vertexShaderPath);
		return false;
	}

	ShaderData fragmentShaderData =
	    ShaderCompiler::loadFromFile(fragmentShaderPath, ShaderType::Fragment);
	if (!fragmentShaderData.isValid()) {
		spdlog::error("Failed to load fragment shader: {}", fragmentShaderPath);
		return false;
	}

	SDL_GPUShaderFormat deviceFormats = SDL_GetGPUShaderFormats(device_);
	SDL_GPUShaderFormat shaderFormat;
	if (deviceFormats & SDL_GPU_SHADERFORMAT_DXIL) {
		shaderFormat = SDL_GPU_SHADERFORMAT_DXIL;
	} else if (deviceFormats & SDL_GPU_SHADERFORMAT_DXBC) {
		shaderFormat = SDL_GPU_SHADERFORMAT_DXBC;
	} else if (deviceFormats & SDL_GPU_SHADERFORMAT_SPIRV) {
		shaderFormat = SDL_GPU_SHADERFORMAT_SPIRV;
	} else if (deviceFormats & SDL_GPU_SHADERFORMAT_METALLIB) {
		shaderFormat = SDL_GPU_SHADERFORMAT_METALLIB;
	} else {
		spdlog::error("No supported shader format found");
		return false;
	}

	SDL_GPUShaderCreateInfo vertexShaderInfo = {};
	vertexShaderInfo.code_size = vertexShaderData.bytecode.size();
	vertexShaderInfo.code = vertexShaderData.bytecode.data();
	vertexShaderInfo.entrypoint = vertexShaderData.entryPoint.c_str();
	vertexShaderInfo.format = shaderFormat;
	vertexShaderInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
	vertexShaderInfo.num_samplers = 0;
	vertexShaderInfo.num_storage_textures = 0;
	vertexShaderInfo.num_storage_buffers = 0;
	vertexShaderInfo.num_uniform_buffers = 0;

	vertexShader_ = SDL_CreateGPUShader(device_, &vertexShaderInfo);
	if (!vertexShader_) {
		spdlog::error("Failed to create vertex shader: {}", SDL_GetError());
		return false;
	}
	spdlog::info(
	    "Vertex shader created successfully, format: {}",
	    (vertexShaderInfo.format == SDL_GPU_SHADERFORMAT_SPIRV)  ? "SPIRV"
	    : (vertexShaderInfo.format == SDL_GPU_SHADERFORMAT_DXBC) ? "DXBC"
	    : (vertexShaderInfo.format == SDL_GPU_SHADERFORMAT_DXIL) ? "DXIL"
	    : (vertexShaderInfo.format == SDL_GPU_SHADERFORMAT_METALLIB)
	        ? "METALLIB"
	                                                             : "Unknown");

	SDL_GPUShaderCreateInfo fragmentShaderInfo = {};
	fragmentShaderInfo.code_size = fragmentShaderData.bytecode.size();
	fragmentShaderInfo.code = fragmentShaderData.bytecode.data();
	fragmentShaderInfo.entrypoint = fragmentShaderData.entryPoint.c_str();
	fragmentShaderInfo.format = shaderFormat;
	fragmentShaderInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
	fragmentShaderInfo.num_samplers = 1;
	fragmentShaderInfo.num_storage_textures = 0;
	fragmentShaderInfo.num_storage_buffers = 0;
	fragmentShaderInfo.num_uniform_buffers = 0;

	fragmentShader_ = SDL_CreateGPUShader(device_, &fragmentShaderInfo);
	if (!fragmentShader_) {
		spdlog::error("Failed to create fragment shader: {}", SDL_GetError());
		return false;
	}

	SDL_GPUColorTargetDescription colorTargetDesc = {};
	colorTargetDesc.format = SDL_GetGPUSwapchainTextureFormat(
	    device_, static_cast<SDL_Window *>(window_));
	colorTargetDesc.blend_state.enable_blend = true;
	colorTargetDesc.blend_state.color_write_mask = 0xF;
	colorTargetDesc.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
	colorTargetDesc.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
	colorTargetDesc.blend_state.src_color_blendfactor =
	    SDL_GPU_BLENDFACTOR_SRC_ALPHA;
	colorTargetDesc.blend_state.dst_color_blendfactor =
	    SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	colorTargetDesc.blend_state.src_alpha_blendfactor =
	    SDL_GPU_BLENDFACTOR_SRC_ALPHA;
	colorTargetDesc.blend_state.dst_alpha_blendfactor =
	    SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;

	SDL_GPUVertexBufferDescription vertexBufferDesc = {};
	vertexBufferDesc.slot = 0;
	vertexBufferDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
	vertexBufferDesc.instance_step_rate = 0;
	vertexBufferDesc.pitch = sizeof(Vertex);

	SDL_GPUVertexAttribute vertexAttributes[5] = {};
	vertexAttributes[0].buffer_slot = 0;
	vertexAttributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
	vertexAttributes[0].location = 0;
	vertexAttributes[0].offset = 0;
	vertexAttributes[1].buffer_slot = 0;
	vertexAttributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
	vertexAttributes[1].location = 1;
	vertexAttributes[1].offset = sizeof(float) * 2;
	vertexAttributes[2].buffer_slot = 0;
	vertexAttributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
	vertexAttributes[2].location = 2;
	vertexAttributes[2].offset = sizeof(float) * 6;
	vertexAttributes[3].buffer_slot = 0;
	vertexAttributes[3].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
	vertexAttributes[3].location = 3;
	vertexAttributes[3].offset = sizeof(float) * 8;
	vertexAttributes[4].buffer_slot = 0;
	vertexAttributes[4].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
	vertexAttributes[4].location = 4;
	vertexAttributes[4].offset = sizeof(float) * 12;

	SDL_GPUGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.vertex_shader = vertexShader_;
	pipelineInfo.fragment_shader = fragmentShader_;
	pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
	pipelineInfo.vertex_input_state.vertex_buffer_descriptions =
	    &vertexBufferDesc;
	pipelineInfo.vertex_input_state.num_vertex_attributes = 5;
	pipelineInfo.vertex_input_state.vertex_attributes = vertexAttributes;
	pipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	pipelineInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
	pipelineInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
	pipelineInfo.rasterizer_state.front_face =
	    SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
	pipelineInfo.target_info.num_color_targets = 1;
	pipelineInfo.target_info.color_target_descriptions = &colorTargetDesc;

	pipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &pipelineInfo);
	if (!pipeline_) {
		spdlog::error("Failed to create graphics pipeline: {}", SDL_GetError());
		return false;
	}

	colorTargetDesc.blend_state.src_color_blendfactor =
	    SDL_GPU_BLENDFACTOR_SRC_ALPHA;
	colorTargetDesc.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
	colorTargetDesc.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
	colorTargetDesc.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
	additivePipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &pipelineInfo);
	if (!additivePipeline_) {
		spdlog::error("Failed to create additive graphics pipeline: {}",
		              SDL_GetError());
		return false;
	}

	spdlog::info("Graphics pipeline created successfully");
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
	if (device_) {
		SDL_WaitForGPUIdle(device_);

		if (pipeline_) {
			SDL_ReleaseGPUGraphicsPipeline(device_, pipeline_);
			pipeline_ = nullptr;
		}
		if (additivePipeline_) {
			SDL_ReleaseGPUGraphicsPipeline(device_, additivePipeline_);
			additivePipeline_ = nullptr;
		}
		if (vertexShader_) {
			SDL_ReleaseGPUShader(device_, vertexShader_);
			vertexShader_ = nullptr;
		}
		if (fragmentShader_) {
			SDL_ReleaseGPUShader(device_, fragmentShader_);
			fragmentShader_ = nullptr;
		}
		if (sampler_) {
			SDL_ReleaseGPUSampler(device_, sampler_);
			sampler_ = nullptr;
		}
		if (repeatSampler_) {
			SDL_ReleaseGPUSampler(device_, repeatSampler_);
			repeatSampler_ = nullptr;
		}
		if (defaultTexture_) {
			SDL_ReleaseGPUTexture(device_, defaultTexture_);
			defaultTexture_ = nullptr;
		}
		if (vertexBuffer_) {
			SDL_ReleaseGPUBuffer(device_, vertexBuffer_);
			vertexBuffer_ = nullptr;
		}
		if (indexBuffer_) {
			SDL_ReleaseGPUBuffer(device_, indexBuffer_);
			indexBuffer_ = nullptr;
		}
		if (frameTransferBuffer_) {
			SDL_ReleaseGPUTransferBuffer(device_, frameTransferBuffer_);
			frameTransferBuffer_ = nullptr;
		}

		device_ = nullptr;
	}

	window_ = nullptr;
}

void Renderer::beginFrame() {
	drawCallCount_ = 0;
	spriteCount_ = 0;
	commandQueue_.clear();
	vertices_.clear();
	indices_.clear();

	commandBuffer_ = SDL_AcquireGPUCommandBuffer(device_);
	if (!commandBuffer_) {
		spdlog::error("Failed to acquire command buffer");
		return;
	}

	if (!SDL_WaitAndAcquireGPUSwapchainTexture(
	        commandBuffer_, static_cast<SDL_Window *>(window_),
	        &swapchainTexture_, nullptr, nullptr)) {
		spdlog::error("Failed to acquire swapchain texture");
		return;
	}
}

void Renderer::endFrame() {
	flushBatch();

	if (commandBuffer_) {
		SDL_SubmitGPUCommandBuffer(commandBuffer_);
		commandBuffer_ = nullptr;
	}
	swapchainTexture_ = nullptr;
}

void Renderer::clear(const Color &color) { clearColor_ = color; }

void Renderer::drawSprite(const Sprite &sprite, float zIndex,
                          bool playfieldSpace) {
	if (!sprite.isVisible() || vertices_.size() + 4 > MAX_VERTICES ||
	    indices_.size() + 6 > MAX_INDICES) {
		return;
	}

	Mat4 transform = sprite.getTransform();

	float w = sprite.getSourceRect().width;
	float h = sprite.getSourceRect().height;

	if (w <= 0.0f)
		w = 32.0f;
	if (h <= 0.0f)
		h = 32.0f;

	float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
	auto texture = sprite.getTexture();
	if (texture && texture->isValid()) {
		Rect sourceRect = sprite.getSourceRect();
		int texWidth = texture->getWidth();
		int texHeight = texture->getHeight();
		if (texWidth > 0 && texHeight > 0) {
			const float fTexWidth = static_cast<float>(texWidth);
			const float fTexHeight = static_cast<float>(texHeight);
			u0 = sourceRect.x / fTexWidth;
			v0 = sourceRect.y / fTexHeight;
			u1 = (sourceRect.x + sourceRect.width) / fTexWidth;
			v1 = (sourceRect.y + sourceRect.height) / fTexHeight;
		}
	}

	Color color = sprite.getColor();

	const float logicalWidth = projectionRight_ - projectionLeft_;
	const float logicalHeight = projectionBottom_ - projectionTop_;
	if (logicalWidth <= 0.0f || logicalHeight <= 0.0f)
		return;

	Vec2 positions[4] = {{0.0f, 0.0f}, {w, 0.0f}, {0.0f, h}, {w, h}};

	for (auto &pos : positions) {
		const float x = pos.x;
		const float y = pos.y;
		const float transformedX =
		    transform.data[0] * x + transform.data[4] * y + transform.data[12];
		const float transformedY =
		    transform.data[1] * x + transform.data[5] * y + transform.data[13];

		// TH06's logical canvas is 640x480. The 384x448 playfield is a viewport
		// inside that canvas, not the coordinate system for the whole window.
		if (playfieldSpace) {
			pos.x = transformedX / 384.0f * 2.0f - 1.0f;
			pos.y = 1.0f - transformedY / 448.0f * 2.0f;
		} else {
			pos.x =
			    (transformedX - projectionLeft_) / logicalWidth * 2.0f - 1.0f;
			pos.y =
			    1.0f - (transformedY - projectionTop_) / logicalHeight * 2.0f;
		}
	}

	uint16_t baseIndex = static_cast<uint16_t>(vertices_.size());

	vertices_.push_back({positions[0], color, {u0, v0}});
	vertices_.push_back({positions[1], color, {u1, v0}});
	vertices_.push_back({positions[2], color, {u0, v1}});
	vertices_.push_back({positions[3], color, {u1, v1}});

	indices_.push_back(baseIndex + 0);
	indices_.push_back(baseIndex + 1);
	indices_.push_back(baseIndex + 2);
	indices_.push_back(baseIndex + 1);
	indices_.push_back(baseIndex + 3);
	indices_.push_back(baseIndex + 2);

	SDL_GPUTexture *gpuTex = nullptr;
	if (texture && texture->isValid()) {
		gpuTex = texture->getGPUTexture();
	}

	SpriteDrawInfo info;
	info.texture = gpuTex;
	info.indexOffset = static_cast<uint32_t>(indices_.size() - 6);
	info.indexCount = 6;
	info.blendMode = sprite.getBlendMode();
	info.playfieldSpace = playfieldSpace;
	spriteDraws_.push_back(info);

	spriteCount_++;
}

void Renderer::drawWindowSprite(const Sprite &sprite, float zIndex) {
	const size_t drawIndex = spriteDraws_.size();
	const size_t vertexIndex = vertices_.size();
	drawSprite(sprite, zIndex, false);
	if (spriteDraws_.size() != drawIndex) {
		spriteDraws_.back().windowSpace = true;
		const float logicalWidth = projectionRight_ - projectionLeft_;
		const float logicalHeight = projectionBottom_ - projectionTop_;
		for (size_t index = vertexIndex; index < vertices_.size(); ++index) {
			const float x =
			    (vertices_[index].position.x + 1.0f) * 0.5f * logicalWidth +
			    projectionLeft_;
			const float y =
			    (1.0f - vertices_[index].position.y) * 0.5f * logicalHeight +
			    projectionTop_;
			vertices_[index].position.x =
			    x / static_cast<float>(outputWidth_) * 2.0f - 1.0f;
			vertices_[index].position.y =
			    1.0f - y / static_cast<float>(outputHeight_) * 2.0f;
		}
	}
}

void Renderer::drawTexturedQuad(const std::shared_ptr<Texture> &texture,
                                const std::array<Vec2, 4> &logicalPositions,
                                const Color &color, BlendMode blendMode,
                                bool clipToPlayfield) {
	drawTexturedQuad(texture, logicalPositions,
	                 std::array<Color, 4>{color, color, color, color},
	                 blendMode, clipToPlayfield);
}

void Renderer::drawTexturedQuad(const std::shared_ptr<Texture> &texture,
                                const std::array<Vec2, 4> &logicalPositions,
                                const std::array<Color, 4> &colors,
                                BlendMode blendMode, bool clipToPlayfield) {
	drawTexturedQuad(texture, logicalPositions, colors,
	                 {{{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}}},
	                 blendMode, clipToPlayfield, false);
}

void Renderer::drawTexturedQuad(const std::shared_ptr<Texture> &texture,
                                const std::array<Vec2, 4> &logicalPositions,
                                const std::array<Color, 4> &colors,
                                const std::array<Vec2, 4> &uvs,
                                BlendMode blendMode, bool clipToPlayfield,
                                bool repeatTexture) {
	if (!texture || !texture->isValid() ||
	    vertices_.size() + 4 > MAX_VERTICES ||
	    indices_.size() + 6 > MAX_INDICES)
		return;

	std::array<Vec2, 4> positions = logicalPositions;
	for (auto &position : positions) {
		if (clipToPlayfield) {
			position.x = position.x / 384.0f * 2.0f - 1.0f;
			position.y = 1.0f - position.y / 448.0f * 2.0f;
		} else {
			const float logicalWidth = projectionRight_ - projectionLeft_;
			const float logicalHeight = projectionBottom_ - projectionTop_;
			position.x =
			    (position.x - projectionLeft_) / logicalWidth * 2.0f - 1.0f;
			position.y =
			    1.0f - (position.y - projectionTop_) / logicalHeight * 2.0f;
		}
	}

	const auto baseIndex = static_cast<uint16_t>(vertices_.size());
	for (size_t index = 0; index < positions.size(); ++index)
		vertices_.push_back({positions[index], colors[index], uvs[index]});
	indices_.insert(indices_.end(), {static_cast<uint16_t>(baseIndex + 0),
	                                 static_cast<uint16_t>(baseIndex + 1),
	                                 static_cast<uint16_t>(baseIndex + 2),
	                                 static_cast<uint16_t>(baseIndex + 1),
	                                 static_cast<uint16_t>(baseIndex + 3),
	                                 static_cast<uint16_t>(baseIndex + 2)});
	spriteDraws_.push_back({texture->getGPUTexture(),
	                        static_cast<uint32_t>(indices_.size() - 6), 6,
	                        blendMode, clipToPlayfield, false, repeatTexture});
	++spriteCount_;
}

void Renderer::drawFoggedTexturedQuad(
    const std::shared_ptr<Texture> &texture,
    const std::array<Vec2, 4> &logicalPositions,
    const std::array<Color, 4> &colors, const std::array<Vec2, 4> &uvs,
    const std::array<Color, 4> &fogColors,
    const std::array<float, 4> &fogFactors, BlendMode blendMode,
    bool clipToPlayfield, bool repeatTexture) {
	if (!texture || !texture->isValid() ||
	    vertices_.size() + 4 > MAX_VERTICES ||
	    indices_.size() + 6 > MAX_INDICES)
		return;

	std::array<Vec2, 4> positions = logicalPositions;
	for (auto &position : positions) {
		if (clipToPlayfield) {
			position.x = position.x / 384.0f * 2.0f - 1.0f;
			position.y = 1.0f - position.y / 448.0f * 2.0f;
		} else {
			const float logicalWidth = projectionRight_ - projectionLeft_;
			const float logicalHeight = projectionBottom_ - projectionTop_;
			position.x =
			    (position.x - projectionLeft_) / logicalWidth * 2.0f - 1.0f;
			position.y =
			    1.0f - (position.y - projectionTop_) / logicalHeight * 2.0f;
		}
	}

	const auto baseIndex = static_cast<uint16_t>(vertices_.size());
	for (size_t index = 0; index < positions.size(); ++index) {
		vertices_.push_back({positions[index], colors[index], uvs[index],
		                     fogColors[index], fogFactors[index]});
	}
	indices_.insert(indices_.end(), {static_cast<uint16_t>(baseIndex + 0),
	                                 static_cast<uint16_t>(baseIndex + 1),
	                                 static_cast<uint16_t>(baseIndex + 2),
	                                 static_cast<uint16_t>(baseIndex + 1),
	                                 static_cast<uint16_t>(baseIndex + 3),
	                                 static_cast<uint16_t>(baseIndex + 2)});
	spriteDraws_.push_back({texture->getGPUTexture(),
	                        static_cast<uint32_t>(indices_.size() - 6), 6,
	                        blendMode, clipToPlayfield, false, repeatTexture});
	++spriteCount_;
}

void Renderer::drawColoredQuad(const std::array<Vec2, 4> &logicalPositions,
                               const std::array<Color, 4> &colors,
                               BlendMode blendMode, bool clipToPlayfield) {
	if (vertices_.size() + 4 > MAX_VERTICES ||
	    indices_.size() + 6 > MAX_INDICES)
		return;
	std::array<Vec2, 4> positions = logicalPositions;
	for (auto &position : positions) {
		if (clipToPlayfield) {
			position.x = position.x / 384.0f * 2.0f - 1.0f;
			position.y = 1.0f - position.y / 448.0f * 2.0f;
		} else {
			const float logicalWidth = projectionRight_ - projectionLeft_;
			const float logicalHeight = projectionBottom_ - projectionTop_;
			position.x =
			    (position.x - projectionLeft_) / logicalWidth * 2.0f - 1.0f;
			position.y =
			    1.0f - (position.y - projectionTop_) / logicalHeight * 2.0f;
		}
	}
	const auto baseIndex = static_cast<uint16_t>(vertices_.size());
	for (size_t index = 0; index < positions.size(); ++index)
		vertices_.push_back({positions[index], colors[index], {0.0f, 0.0f}});
	indices_.insert(indices_.end(), {static_cast<uint16_t>(baseIndex + 0),
	                                 static_cast<uint16_t>(baseIndex + 1),
	                                 static_cast<uint16_t>(baseIndex + 2),
	                                 static_cast<uint16_t>(baseIndex + 1),
	                                 static_cast<uint16_t>(baseIndex + 3),
	                                 static_cast<uint16_t>(baseIndex + 2)});
	spriteDraws_.push_back({nullptr, static_cast<uint32_t>(indices_.size() - 6),
	                        6, blendMode, clipToPlayfield, false, false});
	++spriteCount_;
}

bool Renderer::initializeDefaultFont() {
	if (defaultFontFace_)
		return true;

	FT_Library library = nullptr;
	if (FT_Init_FreeType(&library) != 0)
		return false;

	static constexpr const char *FONT_PATHS[] = {
	    "C:/Windows/Fonts/msgothic.ttc",
	    "C:/Windows/Fonts/msyh.ttc",
	    "C:/Windows/Fonts/arial.ttf",
	    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
	    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
	    "/System/Library/Fonts/ヒラギノ角ゴシック W3.ttc",
	    "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
	};

	FT_Face face = nullptr;
	for (const char *path : FONT_PATHS) {
		if (std::filesystem::exists(path) &&
		    FT_New_Face(library, path, 0, &face) == 0)
			break;
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
	    !initializeDefaultFont())
		return;

	const int pixelSize = std::max(1, static_cast<int>(std::round(size)));
	const std::string cacheKey = std::to_string(pixelSize) + ":" + text;
	auto cached = textCache_.find(cacheKey);

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
			if (FT_Load_Char(face, codepoint, FT_LOAD_RENDER) != 0)
				continue;
			FT_GlyphSlot_Embolden(face->glyph);
			const FT_Bitmap &bitmap = face->glyph->bitmap;
			const int targetX = penX + face->glyph->bitmap_left;
			const int targetY = baseline - face->glyph->bitmap_top;
			for (int row = 0; row < static_cast<int>(bitmap.rows); ++row) {
				for (int col = 0; col < static_cast<int>(bitmap.width); ++col) {
					const int px = targetX + col;
					const int py = targetY + row;
					if (px < 0 || py < 0 || px >= rasterWidth ||
					    py >= rasterHeight)
						continue;
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
						if (sourceX >= rasterWidth || sourceY >= rasterHeight)
							continue;
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
		texture->setDevice(device_);
		// Use the same isolated upload path as file textures. Keeping glyph
		// data out of the per-frame vertex transfer removes backend row-pitch
		// and resource-lifetime coupling from cached text textures.
		if (!texture->createFromData(width, height, pixels.data()))
			return;
		cached = textCache_.emplace(cacheKey, std::move(texture)).first;
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

void Renderer::drawLine(const Vec2 &start, const Vec2 &end,
                        const Color &color) {
	RenderCommand cmd;
	cmd.type = RenderCommand::Type::DrawLine;
	commandQueue_.push_back(cmd);
}

void Renderer::drawRect(const Rect &rect, const Color &color,
                        bool playfieldSpace) {
	drawColoredQuad({{{rect.x, rect.y},
	                  {rect.x + rect.width, rect.y},
	                  {rect.x, rect.y + rect.height},
	                  {rect.x + rect.width, rect.y + rect.height}}},
	                {color, color, color, color}, currentBlendMode_,
	                playfieldSpace);
}

void Renderer::setViewport(int x, int y, int width, int height) {
	viewport_ = Rect(static_cast<float>(x), static_cast<float>(y),
	                 static_cast<float>(width), static_cast<float>(height));
}

void Renderer::setOutputSize(int width, int height) {
	outputWidth_ = std::max(width, 1);
	outputHeight_ = std::max(height, 1);
}

void Renderer::setPlayfieldRegion(float x, float y, float width, float height) {
	playfieldRegion_ = {x, y, std::max(width, 1.0f), std::max(height, 1.0f)};
}

void Renderer::setProjection(float left, float right, float bottom, float top) {
	projectionLeft_ = left;
	projectionRight_ = right;
	projectionBottom_ = bottom;
	projectionTop_ = top;
}

void Renderer::setBlendMode(BlendMode mode) { currentBlendMode_ = mode; }

void Renderer::flushBatch() {
	if (vertices_.empty() || !commandBuffer_ || !pipeline_) {
		return;
	}

	const size_t vertexBytes = vertices_.size() * sizeof(Vertex);
	const size_t indexBytes = indices_.size() * sizeof(uint16_t);

	if (!frameTransferBuffer_) {
		spdlog::error("Persistent frame transfer buffer is unavailable");
		return;
	}

	void *mapped =
	    SDL_MapGPUTransferBuffer(device_, frameTransferBuffer_, true);
	if (mapped) {
		auto *target = static_cast<uint8_t *>(mapped);
		std::memcpy(target, vertices_.data(), vertexBytes);
		std::memcpy(target + vertexBytes, indices_.data(), indexBytes);
		SDL_UnmapGPUTransferBuffer(device_, frameTransferBuffer_);
	} else {
		spdlog::error("Failed to map transfer buffer");
		return;
	}

	SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(commandBuffer_);
	if (!copyPass) {
		spdlog::error("Failed to begin copy pass");
		return;
	}

	SDL_GPUTransferBufferLocation vertexTransfer = {};
	vertexTransfer.transfer_buffer = frameTransferBuffer_;
	vertexTransfer.offset = 0;

	SDL_GPUBufferRegion vertexRegion = {};
	vertexRegion.buffer = vertexBuffer_;
	vertexRegion.offset = 0;
	vertexRegion.size = static_cast<Uint32>(vertexBytes);

	SDL_UploadToGPUBuffer(copyPass, &vertexTransfer, &vertexRegion, true);

	SDL_GPUTransferBufferLocation indexTransfer = {};
	indexTransfer.transfer_buffer = frameTransferBuffer_;
	indexTransfer.offset = static_cast<Uint32>(vertexBytes);

	SDL_GPUBufferRegion indexRegion = {};
	indexRegion.buffer = indexBuffer_;
	indexRegion.offset = 0;
	indexRegion.size = static_cast<Uint32>(indexBytes);

	SDL_UploadToGPUBuffer(copyPass, &indexTransfer, &indexRegion, true);

	SDL_EndGPUCopyPass(copyPass);

	SDL_GPUColorTargetInfo colorTargetInfo = {};
	colorTargetInfo.texture = swapchainTexture_;
	colorTargetInfo.clear_color = {clearColor_.x, clearColor_.y, clearColor_.z,
	                               clearColor_.w};
	colorTargetInfo.load_op = SDL_GPU_LOADOP_CLEAR;
	colorTargetInfo.store_op = SDL_GPU_STOREOP_STORE;

	renderPass_ =
	    SDL_BeginGPURenderPass(commandBuffer_, &colorTargetInfo, 1, nullptr);

	SDL_BindGPUGraphicsPipeline(renderPass_, pipeline_);

	SDL_GPUViewport viewport = {};
	viewport.x = viewport_.x;
	viewport.y = viewport_.y;
	viewport.w = viewport_.width;
	viewport.h = viewport_.height;
	viewport.min_depth = 0.0f;
	viewport.max_depth = 1.0f;
	SDL_SetGPUViewport(renderPass_, &viewport);

	SDL_GPUBufferBinding vertexBinding = {};
	vertexBinding.buffer = vertexBuffer_;
	vertexBinding.offset = 0;
	SDL_BindGPUVertexBuffers(renderPass_, 0, &vertexBinding, 1);

	SDL_GPUBufferBinding indexBinding = {};
	indexBinding.buffer = indexBuffer_;
	indexBinding.offset = 0;
	SDL_BindGPUIndexBuffer(renderPass_, &indexBinding,
	                       SDL_GPU_INDEXELEMENTSIZE_16BIT);

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
			    next.indexOffset != info.indexOffset + batchedIndexCount)
				break;
			batchedIndexCount += next.indexCount;
			++nextDraw;
		}
		SDL_GPUViewport drawViewport = viewport;
		SDL_Rect scissor = {static_cast<int>(viewport_.x),
		                    static_cast<int>(viewport_.y),
		                    static_cast<int>(viewport_.width),
		                    static_cast<int>(viewport_.height)};
		if (info.windowSpace) {
			drawViewport = {0.0f,
			                0.0f,
			                static_cast<float>(outputWidth_),
			                static_cast<float>(outputHeight_),
			                0.0f,
			                1.0f};
			scissor = {0, 0, outputWidth_, outputHeight_};
		} else if (info.playfieldSpace) {
			const float logicalWidth = projectionRight_ - projectionLeft_;
			const float logicalHeight = projectionBottom_ - projectionTop_;
			const float playfieldWidth = std::min(
			    viewport_.width,
			    viewport_.width * playfieldRegion_.width / logicalWidth);
			const float playfieldHeight = std::min(
			    viewport_.height,
			    viewport_.height * playfieldRegion_.height / logicalHeight);
			drawViewport.w = playfieldWidth;
			drawViewport.h = playfieldHeight;
			drawViewport.x +=
			    viewport_.width * playfieldRegion_.x / logicalWidth;
			drawViewport.y +=
			    viewport_.height * playfieldRegion_.y / logicalHeight;
			scissor.x = static_cast<int>(std::floor(drawViewport.x));
			scissor.y = static_cast<int>(std::floor(drawViewport.y));
			scissor.w = static_cast<int>(std::ceil(playfieldWidth));
			scissor.h = static_cast<int>(std::ceil(playfieldHeight));
		}
		SDL_SetGPUViewport(renderPass_, &drawViewport);
		SDL_SetGPUScissor(renderPass_, &scissor);
		SDL_BindGPUGraphicsPipeline(
		    renderPass_, info.blendMode == BlendMode::Add && additivePipeline_
		                     ? additivePipeline_
		                     : pipeline_);
		SDL_GPUTextureSamplerBinding samplerBinding = {};
		samplerBinding.sampler =
		    info.repeatTexture && repeatSampler_ ? repeatSampler_ : sampler_;
		samplerBinding.texture = info.texture ? info.texture : defaultTexture_;
		SDL_BindGPUFragmentSamplers(renderPass_, 0, &samplerBinding, 1);

		SDL_DrawGPUIndexedPrimitives(renderPass_, batchedIndexCount, 1,
		                             info.indexOffset, 0, 0);
		drawCallCount_++;
		drawIndex = nextDraw;
	}

	SDL_EndGPURenderPass(renderPass_);
	renderPass_ = nullptr;

	vertices_.clear();
	indices_.clear();
	commandQueue_.clear();
	spriteDraws_.clear();
}

} // namespace shiki

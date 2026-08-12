#if !defined(__EMSCRIPTEN__)
#include <SDL3/SDL.h>
#include <algorithm>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ft2build.h>
#include <iterator>
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
    : backend_(other.backend_), gpuDevice_(other.gpuDevice_),
      commandBuffer_(other.commandBuffer_),
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
      vertices_(std::move(other.vertices_)), indices_(std::move(other.indices_)),
      spriteDraws_(std::move(other.spriteDraws_)),
      fontLibrary_(other.fontLibrary_),
      defaultFontFace_(other.defaultFontFace_),
      textCache_(std::move(other.textCache_)) {
	other.backend_ = nullptr;
	other.gpuDevice_ = nullptr;
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
		backend_ = other.backend_;
		gpuDevice_ = other.gpuDevice_;
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
		vertices_ = std::move(other.vertices_);
		indices_ = std::move(other.indices_);
		spriteDraws_ = std::move(other.spriteDraws_);
		fontLibrary_ = other.fontLibrary_;
		defaultFontFace_ = other.defaultFontFace_;
		textCache_ = std::move(other.textCache_);
		other.gpuDevice_ = nullptr;
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
		other.backend_ = nullptr;
		other.fontLibrary_ = nullptr;
		other.defaultFontFace_ = nullptr;
	}
	return *this;
}

bool Renderer::initialize(void *window, void *device) {
	window_ = window;
	backend_ = device;
	gpuDevice_ = static_cast<SDL_GPUDevice *>(device);

	if (!gpuDevice_) {
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
	sampler_ = SDL_CreateGPUSampler(gpuDevice_, &samplerInfo);
	if (!sampler_) {
		spdlog::error("Failed to create sampler: {}", SDL_GetError());
		return false;
	}
	samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
	samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
	samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
	repeatSampler_ = SDL_CreateGPUSampler(gpuDevice_, &samplerInfo);
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
	defaultTexture_ = SDL_CreateGPUTexture(gpuDevice_, &whiteTextureInfo);
	if (!defaultTexture_) {
		spdlog::error("Failed to create default texture: {}", SDL_GetError());
		return false;
	}

	uint8_t whitePixel[4] = {255, 255, 255, 255};
	SDL_GPUTransferBufferCreateInfo whiteTransferInfo = {};
	whiteTransferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
	whiteTransferInfo.size = 4;
	SDL_GPUTransferBuffer *whiteTransfer =
	    SDL_CreateGPUTransferBuffer(gpuDevice_, &whiteTransferInfo);
	if (whiteTransfer) {
		void *mapped =
		    SDL_MapGPUTransferBuffer(gpuDevice_, whiteTransfer, false);
		if (mapped) {
			std::memcpy(mapped, whitePixel, 4);
			SDL_UnmapGPUTransferBuffer(gpuDevice_, whiteTransfer);
		}
		SDL_GPUCommandBuffer *whiteCmd =
		    SDL_AcquireGPUCommandBuffer(gpuDevice_);
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
		SDL_ReleaseGPUTransferBuffer(gpuDevice_, whiteTransfer);
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
	vertexBuffer_ = SDL_CreateGPUBuffer(gpuDevice_, &vertexBufferInfo);
	if (!vertexBuffer_) {
		spdlog::error("Failed to create vertex buffer: {}", SDL_GetError());
		return false;
	}

	SDL_GPUBufferCreateInfo indexBufferInfo = {};
	indexBufferInfo.usage = SDL_GPU_BUFFERUSAGE_INDEX;
	indexBufferInfo.size = static_cast<Uint32>(MAX_INDICES * sizeof(uint16_t));
	indexBuffer_ = SDL_CreateGPUBuffer(gpuDevice_, &indexBufferInfo);
	if (!indexBuffer_) {
		spdlog::error("Failed to create index buffer: {}", SDL_GetError());
		return false;
	}

	SDL_GPUTransferBufferCreateInfo transferInfo = {};
	transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
	transferInfo.size = static_cast<Uint32>(MAX_VERTICES * sizeof(Vertex) +
	                                        MAX_INDICES * sizeof(uint16_t));
	frameTransferBuffer_ =
	    SDL_CreateGPUTransferBuffer(gpuDevice_, &transferInfo);
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

	// Shaders are generated into <project>/build/shaders. From the desktop
	// run directory (build/<plat>/<arch>/<mode>) that resolves as
	// "../../shaders"; the wasm filesystem serves them at "/shaders".
	std::vector<std::string> possibleShaderDirs = {
	    "shaders",
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

	SDL_GPUShaderFormat deviceFormats = SDL_GetGPUShaderFormats(gpuDevice_);
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

	vertexShader_ = SDL_CreateGPUShader(gpuDevice_, &vertexShaderInfo);
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

	fragmentShader_ = SDL_CreateGPUShader(gpuDevice_, &fragmentShaderInfo);
	if (!fragmentShader_) {
		spdlog::error("Failed to create fragment shader: {}", SDL_GetError());
		return false;
	}

	SDL_GPUColorTargetDescription colorTargetDesc = {};
	colorTargetDesc.format = SDL_GetGPUSwapchainTextureFormat(
	    gpuDevice_, static_cast<SDL_Window *>(window_));
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

	pipeline_ = SDL_CreateGPUGraphicsPipeline(gpuDevice_, &pipelineInfo);
	if (!pipeline_) {
		spdlog::error("Failed to create graphics pipeline: {}", SDL_GetError());
		return false;
	}

	colorTargetDesc.blend_state.src_color_blendfactor =
	    SDL_GPU_BLENDFACTOR_SRC_ALPHA;
	colorTargetDesc.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
	colorTargetDesc.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
	colorTargetDesc.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
	additivePipeline_ =
	    SDL_CreateGPUGraphicsPipeline(gpuDevice_, &pipelineInfo);
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
	if (gpuDevice_) {
		SDL_WaitForGPUIdle(gpuDevice_);

		if (pipeline_) {
			SDL_ReleaseGPUGraphicsPipeline(gpuDevice_, pipeline_);
			pipeline_ = nullptr;
		}
		if (additivePipeline_) {
			SDL_ReleaseGPUGraphicsPipeline(gpuDevice_, additivePipeline_);
			additivePipeline_ = nullptr;
		}
		if (vertexShader_) {
			SDL_ReleaseGPUShader(gpuDevice_, vertexShader_);
			vertexShader_ = nullptr;
		}
		if (fragmentShader_) {
			SDL_ReleaseGPUShader(gpuDevice_, fragmentShader_);
			fragmentShader_ = nullptr;
		}
		if (sampler_) {
			SDL_ReleaseGPUSampler(gpuDevice_, sampler_);
			sampler_ = nullptr;
		}
		if (repeatSampler_) {
			SDL_ReleaseGPUSampler(gpuDevice_, repeatSampler_);
			repeatSampler_ = nullptr;
		}
		if (defaultTexture_) {
			SDL_ReleaseGPUTexture(gpuDevice_, defaultTexture_);
			defaultTexture_ = nullptr;
		}
		if (vertexBuffer_) {
			SDL_ReleaseGPUBuffer(gpuDevice_, vertexBuffer_);
			vertexBuffer_ = nullptr;
		}
		if (indexBuffer_) {
			SDL_ReleaseGPUBuffer(gpuDevice_, indexBuffer_);
			indexBuffer_ = nullptr;
		}
		if (frameTransferBuffer_) {
			SDL_ReleaseGPUTransferBuffer(gpuDevice_, frameTransferBuffer_);
			frameTransferBuffer_ = nullptr;
		}

		gpuDevice_ = nullptr;
		backend_ = nullptr;
	}

	window_ = nullptr;
}

void Renderer::beginFrame() {
	drawCallCount_ = 0;
	spriteCount_ = 0;
	vertices_.clear();
	indices_.clear();

	commandBuffer_ = SDL_AcquireGPUCommandBuffer(gpuDevice_);
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
	    SDL_MapGPUTransferBuffer(gpuDevice_, frameTransferBuffer_, true);
	if (mapped) {
		auto *target = static_cast<uint8_t *>(mapped);
		std::memcpy(target, vertices_.data(), vertexBytes);
		std::memcpy(target + vertexBytes, indices_.data(), indexBytes);
		SDL_UnmapGPUTransferBuffer(gpuDevice_, frameTransferBuffer_);
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
		samplerBinding.texture =
		    info.texture ? static_cast<SDL_GPUTexture *>(info.texture)
		                 : defaultTexture_;
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
	spriteDraws_.clear();
}

} // namespace shiki
#endif // !defined(__EMSCRIPTEN__)

#include <SDL3/SDL.h>
#include <shiki/render/texture.h>
#include <spdlog/spdlog.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace shiki {

Texture::Texture() = default;

Texture::~Texture() { release(); }

Texture::Texture(Texture &&other) noexcept
    : device_(other.device_), gpuTexture_(other.gpuTexture_),
      width_(other.width_), height_(other.height_),
      path_(std::move(other.path_)) {
  other.device_ = nullptr;
  other.gpuTexture_ = nullptr;
  other.width_ = 0;
  other.height_ = 0;
}

Texture &Texture::operator=(Texture &&other) noexcept {
  if (this != &other) {
    release();
    device_ = other.device_;
    gpuTexture_ = other.gpuTexture_;
    width_ = other.width_;
    height_ = other.height_;
    path_ = std::move(other.path_);
    other.device_ = nullptr;
    other.gpuTexture_ = nullptr;
    other.width_ = 0;
    other.height_ = 0;
  }
  return *this;
}

bool Texture::loadFromFile(const std::string &path) {
  if (!device_) {
    spdlog::error("Texture::loadFromFile: device not set");
    return false;
  }

  int width, height, channels;
  stbi_uc *data =
      stbi_load(path.c_str(), &width, &height, &channels, 4);

  if (data) {
    bool result = createFromData(width, height, data);
    stbi_image_free(data);

    if (result) {
      path_ = path;
      spdlog::info("Texture loaded (stb_image): {} ({}x{}, channels={})", path,
                   width, height, channels);
    }
    return result;
  }

  SDL_Surface *surface = SDL_LoadBMP(path.c_str());
  if (surface) {
    SDL_Surface *rgbaSurface =
        SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(surface);

    if (rgbaSurface) {
      bool result = createFromData(rgbaSurface->w, rgbaSurface->h,
                                   static_cast<uint8_t *>(rgbaSurface->pixels));
      SDL_DestroySurface(rgbaSurface);

      if (result) {
        path_ = path;
        spdlog::info("Texture loaded (SDL): {} ({}x{})", path, width_, height_);
      }
      return result;
    }
  }

  spdlog::error("Failed to load texture '{}': {}", path, stbi_failure_reason());
  return false;
}

bool Texture::createFromData(int width, int height, const uint8_t *data) {
  if (!device_) {
    spdlog::error("Texture::createFromData: device not set");
    return false;
  }

  if (width <= 0 || height <= 0 || !data) {
    spdlog::error("Texture::createFromData: invalid parameters");
    return false;
  }

  if (!createEmpty(width, height)) {
    return false;
  }

  const auto alignUp = [](size_t value, size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
  };
  const size_t sourcePitch = static_cast<size_t>(width) * 4;
  const size_t uploadPitch = alignUp(sourcePitch, 256);

  // D3D12 requires a 256-byte texture row pitch. SDL can realign this
  // internally, but explicitly supplying the aligned layout keeps narrow
  // glyph textures identical across GPU backends.
  SDL_GPUTransferBufferCreateInfo transferInfo = {};
  transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
  transferInfo.size =
      static_cast<Uint32>(uploadPitch * static_cast<size_t>(height));

  SDL_GPUTransferBuffer *transferBuffer =
      SDL_CreateGPUTransferBuffer(device_, &transferInfo);
  if (!transferBuffer) {
    spdlog::error("Failed to create transfer buffer: {}", SDL_GetError());
    SDL_ReleaseGPUTexture(device_, gpuTexture_);
    gpuTexture_ = nullptr;
    return false;
  }

  void *mapped = SDL_MapGPUTransferBuffer(device_, transferBuffer, false);
  if (!mapped) {
    spdlog::error("Failed to map texture transfer buffer: {}", SDL_GetError());
    SDL_ReleaseGPUTransferBuffer(device_, transferBuffer);
    SDL_ReleaseGPUTexture(device_, gpuTexture_);
    gpuTexture_ = nullptr;
    return false;
  }
  auto *uploadData = static_cast<uint8_t *>(mapped);
  std::memset(uploadData, 0, uploadPitch * static_cast<size_t>(height));
  for (int row = 0; row < height; ++row)
    std::memcpy(uploadData + static_cast<size_t>(row) * uploadPitch,
                data + static_cast<size_t>(row) * sourcePitch, sourcePitch);
  SDL_UnmapGPUTransferBuffer(device_, transferBuffer);

  SDL_GPUCommandBuffer *cmdBuffer = SDL_AcquireGPUCommandBuffer(device_);
  if (!cmdBuffer) {
    spdlog::error("Failed to acquire command buffer for texture upload");
    SDL_ReleaseGPUTransferBuffer(device_, transferBuffer);
    SDL_ReleaseGPUTexture(device_, gpuTexture_);
    gpuTexture_ = nullptr;
    return false;
  }

  SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(cmdBuffer);
  if (!copyPass) {
    spdlog::error("Failed to begin copy pass for texture upload");
    SDL_ReleaseGPUTransferBuffer(device_, transferBuffer);
    SDL_ReleaseGPUTexture(device_, gpuTexture_);
    gpuTexture_ = nullptr;
    return false;
  }

  SDL_GPUTextureTransferInfo transferSrc = {};
  transferSrc.transfer_buffer = transferBuffer;
  transferSrc.offset = 0;
  transferSrc.pixels_per_row = static_cast<Uint32>(uploadPitch / 4);
  transferSrc.rows_per_layer = static_cast<Uint32>(height);

  SDL_GPUTextureRegion transferDst = {};
  transferDst.texture = gpuTexture_;
  transferDst.x = 0;
  transferDst.y = 0;
  transferDst.z = 0;
  transferDst.w = static_cast<Uint32>(width);
  transferDst.h = static_cast<Uint32>(height);
  transferDst.d = 1;

  SDL_UploadToGPUTexture(copyPass, &transferSrc, &transferDst, false);

  SDL_EndGPUCopyPass(copyPass);

  SDL_SubmitGPUCommandBuffer(cmdBuffer);

  SDL_ReleaseGPUTransferBuffer(device_, transferBuffer);

  return true;
}

bool Texture::createEmpty(int width, int height) {
  if (!device_) {
    spdlog::error("Texture::createEmpty: device not set");
    return false;
  }
  if (width <= 0 || height <= 0) {
    spdlog::error("Texture::createEmpty: invalid dimensions");
    return false;
  }

  release();
  SDL_GPUTextureCreateInfo textureInfo = {};
  textureInfo.type = SDL_GPU_TEXTURETYPE_2D;
  textureInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
  textureInfo.width = static_cast<Uint32>(width);
  textureInfo.height = static_cast<Uint32>(height);
  textureInfo.layer_count_or_depth = 1;
  textureInfo.num_levels = 1;
  textureInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
  gpuTexture_ = SDL_CreateGPUTexture(device_, &textureInfo);
  if (!gpuTexture_) {
    spdlog::error("Failed to create GPU texture: {}", SDL_GetError());
    return false;
  }
  width_ = width;
  height_ = height;
  return true;
}

bool Texture::create(int width, int height) {
  if (!device_) {
    spdlog::error("Texture::create: device not set");
    return false;
  }

  std::vector<uint8_t> emptyData(width * height * 4, 255);
  return createFromData(width, height, emptyData.data());
}

void Texture::release() {
  if (gpuTexture_ && device_) {
    SDL_ReleaseGPUTexture(device_, gpuTexture_);
    gpuTexture_ = nullptr;
  }
  width_ = 0;
  height_ = 0;
}

} // namespace shiki

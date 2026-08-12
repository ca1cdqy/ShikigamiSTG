#include <algorithm>
#include <filesystem>
#include <shiki/asset/standard_assets.h>
#include <shiki/asset/structured_assets.h>
#include <shiki/render/texture.h>
#include <shiki/resource/resource_manager.h>
#include <spdlog/spdlog.h>

namespace shiki {

ResourceManager::ResourceManager() = default;

ResourceManager::~ResourceManager() { shutdown(); }

ResourceManager::ResourceManager(ResourceManager &&other) noexcept
    : device_(other.device_), textures_(std::move(other.textures_)),
      sounds_(std::move(other.sounds_)), musics_(std::move(other.musics_)),
      textureCache_(std::move(other.textureCache_)),
      soundCache_(std::move(other.soundCache_)),
      musicCache_(std::move(other.musicCache_)),
      spriteAtlases_(std::move(other.spriteAtlases_)),
      assetStore_(std::move(other.assetStore_)),
      assetAtlases_(std::move(other.assetAtlases_)) {
	other.device_ = nullptr;
}

ResourceManager &ResourceManager::operator=(ResourceManager &&other) noexcept {
	if (this != &other) {
		device_ = other.device_;
		textures_ = std::move(other.textures_);
		sounds_ = std::move(other.sounds_);
		musics_ = std::move(other.musics_);
		textureCache_ = std::move(other.textureCache_);
		soundCache_ = std::move(other.soundCache_);
		musicCache_ = std::move(other.musicCache_);
		spriteAtlases_ = std::move(other.spriteAtlases_);
		assetStore_ = std::move(other.assetStore_);
		assetAtlases_ = std::move(other.assetAtlases_);
		other.device_ = nullptr;
	}
	return *this;
}

bool ResourceManager::initialize() {
	if (std::filesystem::is_regular_file("assets/manifest.json")) {
		auto mounted = mountAssetPackage("assets");
		if (!mounted) {
			spdlog::warn("Failed to mount generated asset package: {}",
			             mounted.error().message);
		}
	}
	return true;
}

Result<void> ResourceManager::mountAssetPackage(const std::string &root,
                                                const std::string &manifest) {
	auto sources = std::make_unique<asset::FileSourceProvider>(root);
	auto source = sources->read(manifest);
	if (!source)
		return std::unexpected(source.error());
	return mountAssetSource(std::move(sources), [source = std::move(*source)](
	                                                asset::AssetStore &store) {
		auto standard = asset::registerStandardAssetLoaders(store);
		if (!standard)
			return standard;
		auto structured = asset::registerStructuredAssetLoaders(store);
		if (!structured)
			return structured;
		return asset::addJsonManifest(store, source);
	});
}

Result<void> ResourceManager::mountAssetSource(
    std::unique_ptr<asset::SourceProvider> sources,
    std::function<Result<void>(asset::AssetStore &)> setup) {
	if (!sources)
		return std::unexpected(
		    Error{ErrorDomain::Asset,
		          static_cast<std::uint32_t>(asset::AssetError::NullProvider),
		          "Asset source provider cannot be null"});
	if (!setup)
		return std::unexpected(
		    Error{ErrorDomain::Asset,
		          static_cast<std::uint32_t>(asset::AssetError::UnknownLoader),
		          "Asset store setup callback cannot be empty"});
	auto store = std::make_unique<asset::AssetStore>(std::move(sources));
	auto configured = setup(*store);
	if (!configured)
		return std::unexpected(configured.error());
	assetStore_ = std::move(store);
	assetAtlases_.clear();
	return {};
}

void ResourceManager::shutdown() { clear(); }

std::shared_ptr<Texture> ResourceManager::loadTexture(const std::string &path) {
	auto it = textureCache_.find(path);
	if (it != textureCache_.end()) {
		return it->second;
	}

	auto texture = std::make_shared<Texture>();

	if (device_) {
		texture->setDevice(device_);
	}

	if (!texture->loadFromFile(path)) {
		spdlog::error("Failed to load texture: {}", path);
		return nullptr;
	}

	textureCache_[path] = texture;
	return texture;
}

std::shared_ptr<Sound> ResourceManager::loadSound(const std::string &path) {
	return nullptr;
}

std::shared_ptr<Music> ResourceManager::loadMusic(const std::string &path) {
	return nullptr;
}

std::shared_ptr<Texture>
ResourceManager::getTexture(const std::string &path) const {
	auto it = textureCache_.find(path);
	if (it != textureCache_.end()) {
		return it->second;
	}
	return nullptr;
}

std::shared_ptr<Sound>
ResourceManager::getSound(const std::string &path) const {
	auto it = soundCache_.find(path);
	if (it != soundCache_.end()) {
		return it->second;
	}
	return nullptr;
}

std::shared_ptr<Music>
ResourceManager::getMusic(const std::string &path) const {
	auto it = musicCache_.find(path);
	if (it != musicCache_.end()) {
		return it->second;
	}
	return nullptr;
}

void ResourceManager::unloadTexture(const std::string &path) {
	textureCache_.erase(path);
	textures_.erase(path);
}

void ResourceManager::unloadSound(const std::string &path) {
	soundCache_.erase(path);
	sounds_.erase(path);
}

void ResourceManager::unloadMusic(const std::string &path) {
	musicCache_.erase(path);
	musics_.erase(path);
}

void ResourceManager::clear() {
	textureCache_.clear();
	soundCache_.clear();
	musicCache_.clear();
	textures_.clear();
	sounds_.clear();
	musics_.clear();
	spriteAtlases_.clear();
	assetAtlases_.clear();
	assetStore_.reset();
}

size_t ResourceManager::getTextureCount() const { return textureCache_.size(); }

size_t ResourceManager::getSoundCount() const { return soundCache_.size(); }

size_t ResourceManager::getMusicCount() const { return musicCache_.size(); }

bool ResourceManager::loadSpriteAtlas(const std::string &atlasName) {
	if (!assetStore_) {
		spdlog::error("Cannot load sprite atlas without a mounted AssetStore");
		return false;
	}
	const asset::AssetId id = asset::AssetId::fromName("atlas." + atlasName);
	auto loaded = assetStore_->load<asset::SpriteAtlasAsset>(id);
	if (!loaded) {
		spdlog::error("Failed to load sprite atlas '{}' from AssetStore: {}",
		              atlasName, loaded.error().message);
		return false;
	}
	SpriteAtlas atlas;
	atlas.source = (*loaded)->name;
	atlas.sprites.reserve((*loaded)->sprites.size());
	for (const asset::SpriteFrameAsset &frame : (*loaded)->sprites) {
		atlas.sprites.push_back(SpriteFrame{
		    frame.id, static_cast<int>(frame.width),
		    static_cast<int>(frame.height), frame.originX, frame.originY});
	}
	spriteAtlases_[atlasName] = std::move(atlas);
	assetAtlases_[atlasName] = id;
	return true;
}

const SpriteAtlas *
ResourceManager::getSpriteAtlas(const std::string &name) const {
	auto it = spriteAtlases_.find(name);
	if (it != spriteAtlases_.end()) {
		return &it->second;
	}
	return nullptr;
}

std::shared_ptr<Texture>
ResourceManager::getSpriteTexture(const std::string &atlasName,
                                  int spriteId) const {
	if (assetStore_) {
		auto atlasId = assetAtlases_.find(atlasName);
		if (atlasId == assetAtlases_.end()) {
			const asset::AssetId candidate =
			    asset::AssetId::fromName("atlas." + atlasName);
			auto loaded = assetStore_->load<asset::SpriteAtlasAsset>(candidate);
			if (loaded) {
				assetAtlases_[atlasName] = candidate;
				atlasId = assetAtlases_.find(atlasName);
			}
		}
		if (atlasId != assetAtlases_.end()) {
			auto atlas =
			    assetStore_->find<asset::SpriteAtlasAsset>(atlasId->second);
			if (atlas) {
				const asset::SpriteFrameAsset *frame = (*atlas)->find(spriteId);
				if (frame) {
					const std::uint64_t imageKey = frame->image.key.value;
					if (const auto cached = textureIdCache_.find(imageKey);
					    cached != textureIdCache_.end())
						return cached->second;
					auto image =
					    assetStore_->load<asset::ImageAsset>(frame->image);
					if (image) {
						auto texture =
						    const_cast<ResourceManager *>(this)->realize(
						        frame->image, **image);
						if (texture) {
							textureIdCache_[imageKey] = *texture;
							return *texture;
						}
					}
				}
			}
		}
	}

	return nullptr;
}

Result<std::shared_ptr<Texture>>
ResourceManager::realize(asset::AssetId id, const asset::ImageAsset &source) {
	const std::uint64_t imageKey = id.key.value;
	if (const auto cached = textureIdCache_.find(imageKey);
	    cached != textureIdCache_.end())
		return cached->second;
	const std::string cacheKey = "asset:" + std::to_string(id.key.value);
	if (const auto cached = textureCache_.find(cacheKey);
	    cached != textureCache_.end()) {
		textureIdCache_[imageKey] = cached->second;
		return cached->second;
	}
	if (!device_)
		return std::unexpected(Error{ErrorDomain::Presentation, 1,
		                             "GPU device is not configured"});
	auto texture = std::make_shared<Texture>();
	texture->setDevice(device_);
	if (!texture->createFromData(
	        static_cast<int>(source.width), static_cast<int>(source.height),
	        reinterpret_cast<const std::uint8_t *>(source.rgba.data())))
		return std::unexpected(Error{ErrorDomain::Presentation, 2,
		                             "GPU texture realization failed"});
	textureCache_[cacheKey] = texture;
	textureIdCache_[imageKey] = texture;
	return texture;
}

} // namespace shiki

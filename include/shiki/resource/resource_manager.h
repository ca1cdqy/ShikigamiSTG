#pragma once

/** @file Presentation resource facade and custom asset-store mounting API. */

#include <functional>
#include <memory>
#include <shiki/asset/asset_store.h>
#include <shiki/asset/standard_assets.h>
#include <shiki/core/types.h>
#include <shiki/presentation/asset_realizer.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace shiki {

/// Forward declarations
class Texture;
class Sound;
class Music;
struct SpriteFrame;
struct SpriteAtlas;

/** Metadata for one frame within a sprite atlas texture. */
struct SpriteFrame {
	int id = 0;           ///< Atlas-local frame identifier.
	int width = 0;        ///< Frame width in pixels.
	int height = 0;       ///< Frame height in pixels.
	float originX = 0.0f; ///< Horizontal transform origin in frame pixels.
	float originY = 0.0f; ///< Vertical transform origin in frame pixels.
};

/** Groups multiple textures and their frame metadata into one named atlas. */
struct SpriteAtlas {
	std::string source; ///< Logical source identity of this atlas.
	std::vector<SpriteFrame> sprites; ///< Frame metadata in manifest order.
};

/**
 * Facade that bridges the modern AssetStore with the legacy texture, sound,
 * music, and sprite-atlas caches used by existing frontends.
 *
 * Call initialize() once before any resource loading and shutdown() before
 * destruction. The underlying GPU device must be supplied via setDevice()
 * before loadTexture() or realize() are called.
 */
class ResourceManager
    : public presentation::AssetRealizer<asset::ImageAsset, Texture> {
  public:
	/** Creates an empty resource manager with no loaded resources. */
	ResourceManager();
	/** Calls shutdown() if initialized. */
	~ResourceManager();

	/** Resource managers cannot be copied because they own GPU resources. */
	ResourceManager(const ResourceManager &) = delete;
	/** Resource managers cannot be copy-assigned. */
	ResourceManager &operator=(const ResourceManager &) = delete;

	/** Transfers resource cache and GPU device ownership. */
	ResourceManager(ResourceManager &&) noexcept;
	/** Releases current resources, then takes ownership from other. */
	ResourceManager &operator=(ResourceManager &&) noexcept;

	/** Prepares internal caches and constructs the AssetStore. Returns true on
	 * success. */
	bool initialize();
	/** Unloads all resources and destroys the AssetStore. */
	void shutdown();

	/** Mounts a generated asset package by root directory and manifest
	 * filename. */
	[[nodiscard]] Result<void>
	mountAssetPackage(const std::string &root,
	                  const std::string &manifest = "manifest.json");

	/**
	 * Mounts an application-defined source and configures its
	 * AssetStore.
	 *
	 * The setup callback may register arbitrary formats and
	 * add manifest
	 * entries using the procedural asset API. No JSON
	 * parser, standard loader,
	 * filesystem layout, or compatibility
	 * decoder is installed implicitly.
	 */
	[[nodiscard]] Result<void>
	mountAssetSource(std::unique_ptr<asset::SourceProvider> sources,
	                 std::function<Result<void>(asset::AssetStore &)> setup);

	/** Returns the modern CPU AssetStore owned by this manager. */
	[[nodiscard]] asset::AssetStore *getAssetStore() const {
		return assetStore_.get();
	}

	/** Realizes a decoded image asset as a GPU texture and caches the result.
	 */
	[[nodiscard]] Result<std::shared_ptr<Texture>>
	realize(asset::AssetId id, const asset::ImageAsset &source) override;

	/** Sets the SDL GPU device used when uploading new textures. */
	void setDevice(void *device) { device_ = device; }

	/** Loads or retrieves a cached GPU texture from a filesystem path. */
	std::shared_ptr<Texture> loadTexture(const std::string &path);
	/** Loads or retrieves a cached sound buffer from a filesystem path. */
	std::shared_ptr<Sound> loadSound(const std::string &path);
	/** Loads or retrieves a cached music stream from a filesystem path. */
	std::shared_ptr<Music> loadMusic(const std::string &path);

	/** Returns a cached GPU texture for a path, or null if not loaded. */
	std::shared_ptr<Texture> getTexture(const std::string &path) const;
	/** Returns a cached sound buffer for a path, or null if not loaded. */
	std::shared_ptr<Sound> getSound(const std::string &path) const;
	/** Returns a cached music stream for a path, or null if not loaded. */
	std::shared_ptr<Music> getMusic(const std::string &path) const;

	/** Loads a typed sprite-atlas asset by logical name and caches metadata. */
	bool loadSpriteAtlas(const std::string &atlasName);
	/** Returns the atlas registered under name, or null if absent. */
	const SpriteAtlas *getSpriteAtlas(const std::string &name) const;
	/** Returns the texture for a frame within a named atlas, or null. */
	std::shared_ptr<Texture> getSpriteTexture(const std::string &atlasName,
	                                          int spriteId) const;

	/** Removes the cache entry for a texture path. */
	void unloadTexture(const std::string &path);
	/** Removes the cache entry for a sound path. */
	void unloadSound(const std::string &path);
	/** Removes the cache entry for a music path. */
	void unloadMusic(const std::string &path);

	/** Unloads all textures, sounds, and music resources. */
	void clear();

	/** Returns the number of loaded GPU textures. */
	[[nodiscard]] size_t getTextureCount() const;
	/** Returns the number of loaded sound buffers. */
	[[nodiscard]] size_t getSoundCount() const;
	/** Returns the number of loaded music streams. */
	[[nodiscard]] size_t getMusicCount() const;

  private:
	/// GPU device
	void *device_ = nullptr;

	/// Resource cache
	mutable std::unordered_map<std::string, std::weak_ptr<Texture>> textures_;
	mutable std::unordered_map<std::string, std::weak_ptr<Sound>> sounds_;
	mutable std::unordered_map<std::string, std::weak_ptr<Music>> musics_;

	/// Loaded resources retained by strong references
	mutable std::unordered_map<std::string, std::shared_ptr<Texture>>
	    textureCache_;
	    /// Texture lookup by integer asset key (avoids per-frame string keys).
	    mutable std::unordered_map<std::uint64_t, std::shared_ptr<Texture>>
	        textureIdCache_;
	mutable std::unordered_map<std::string, std::shared_ptr<Sound>> soundCache_;
	mutable std::unordered_map<std::string, std::shared_ptr<Music>> musicCache_;

	/// Sprite-atlas cache
	mutable std::unordered_map<std::string, SpriteAtlas> spriteAtlases_;

	std::unique_ptr<asset::AssetStore> assetStore_;
	mutable std::unordered_map<std::string, asset::AssetId> assetAtlases_;
};

} // namespace shiki

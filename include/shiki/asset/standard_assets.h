#pragma once

#include <shiki/asset/asset_store.h>

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace shiki::asset {

/** Immutable decoded RGBA8 pixels independent from any GPU backend. */
struct ImageAsset final {
	std::uint32_t width{};
	std::uint32_t height{};
	std::vector<std::byte> rgba;
};

/** Describes one sprite and its independent image dependency. */
struct SpriteFrameAsset final {
	std::int32_t id{};
	AssetId image{};
	std::uint32_t width{};
	std::uint32_t height{};
	float originX{};
	float originY{};
};

/** Immutable sprite atlas metadata decoded from compact JSON. */
struct SpriteAtlasAsset final {
	std::string name;
	std::vector<SpriteFrameAsset> sprites;

	/** Finds one local sprite ID without performing source IO. */
	[[nodiscard]] const SpriteFrameAsset *find(std::int32_t id) const noexcept;
};

/** Identifies one byte range containing a complete animation script. */
struct AnimationScriptRange final {
	std::size_t begin{};
	std::size_t end{};
};

/** Immutable animation bytecode and sprite remapping metadata. */
struct AnimationAsset final {
	std::string atlas;
	std::vector<std::byte> instructions;
	std::map<std::int32_t, AnimationScriptRange> scripts;
	std::map<std::int32_t, std::int32_t> spriteMap;

	/** Resolves an animation sprite ID to its atlas-local frame ID. */
	[[nodiscard]] std::int32_t resolveSprite(std::int32_t rawId) const noexcept;
};

/** Returns the canonical standard image decoder format. */
[[nodiscard]] constexpr AssetFormat imageFormat() noexcept {
	return AssetFormat::fromName("shiki.image.rgba8.v1");
}

/** Returns the canonical compact sprite atlas decoder format. */
[[nodiscard]] constexpr AssetFormat spriteAtlasFormat() noexcept {
	return AssetFormat::fromName("shiki.sprite_atlas.json.v1");
}

/** Returns the canonical compact animation decoder format. */
[[nodiscard]] constexpr AssetFormat animationFormat() noexcept {
	return AssetFormat::fromName("shiki.animation.json.v1");
}

/** Registers platform-independent image and sprite atlas CPU decoders. */
[[nodiscard]] Result<void> registerStandardAssetLoaders(AssetStore &assets);

/** Adds entries from a compact generic JSON manifest to an AssetStore. */
[[nodiscard]] Result<void> addJsonManifest(AssetStore &assets,
                                           const Source &source);

} // namespace shiki::asset

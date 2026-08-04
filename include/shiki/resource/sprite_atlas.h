#pragma once

#include <memory>
#include <shiki/core/types.h>
#include <string>
#include <vector>

namespace shiki {

/** Metadata for one frame within a sprite atlas texture. */
struct SpriteFrame {
	int id = 0;          ///< Atlas-local frame identifier.
	std::string file;    ///< Source texture filename for this frame.
	int width = 0;       ///< Frame width in pixels.
	int height = 0;      ///< Frame height in pixels.
	float originX = 0.0f; ///< Horizontal transform origin in frame pixels.
	float originY = 0.0f; ///< Vertical transform origin in frame pixels.
};

/** Groups multiple textures and their frame metadata into one named atlas. */
struct SpriteAtlas {
	std::string source;              ///< JSON manifest path used to load this atlas.
	std::vector<std::string> textures; ///< Texture filenames referenced by frames.
	std::vector<SpriteFrame> sprites;  ///< Frame metadata in manifest order.
};

/**
 * Loads sprite atlas manifests from JSON files or directories.
 *
 * All methods are static; no instance state is maintained.
 */
class SpriteAtlasLoader {
  public:
	/** Parses one JSON atlas manifest and populates atlas. Returns true on success. */
	static bool loadFromJson(const std::string &jsonPath, SpriteAtlas &atlas);

	/** Loads every JSON atlas manifest found in a directory. Returns true if at least one loaded. */
	static bool loadFromDirectory(const std::string &dirPath,
	                              std::vector<SpriteAtlas> &atlases);

  private:
	/** Parses pre-read JSON content and populates atlas. */
	static bool parseJson(const std::string &jsonContent, SpriteAtlas &atlas);
};

} // namespace shiki

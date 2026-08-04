#pragma once

/** @file Portable JSON scene, dialogue, and audio asset payloads. */

#include <shiki/asset/asset_store.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace shiki::asset {

/** One immutable quad within a data-driven stage background. */
struct StageQuadAsset final {
	std::int32_t script{};           ///< Animation script used by the quad.
	std::array<float, 3> position{}; ///< Object-local x, y, and z coordinates.
	std::array<float, 2> size{};     ///< Width and height in stage units.
};

/** One reusable object within a data-driven stage background. */
struct StageObjectAsset final {
	std::int32_t zLevel{};             ///< Renderer-defined depth bucket.
	std::array<float, 3> position{};   ///< Object origin in stage space.
	std::vector<StageQuadAsset> quads; ///< Quads in source order.
};

/** One placed instance of a reusable stage-background object. */
struct StageInstanceAsset final {
	std::int32_t objectId{-1};       ///< Index into StageAsset::objects.
	std::array<float, 3> position{}; ///< Placement offset in stage space.
};

/** One application-defined stage camera or effect timeline instruction. */
struct StageInstructionAsset final {
	std::int32_t frame{};          ///< Execution frame on the stage timeline.
	std::int32_t opcode{};         ///< Application-defined instruction code.
	std::int32_t argument{};       ///< Scalar opcode argument.
	std::array<float, 3> vector{}; ///< Vector opcode argument.
};

/** Immutable decoded stage-background scene data. */
struct StageAsset final {
	std::string atlas; ///< Logical sprite-atlas name used by stage quads.
	std::string name;  ///< Optional display name.
	std::vector<std::string>
	    songs; ///< Optional music metadata in source order.
	std::vector<StageObjectAsset> objects;     ///< Reusable object definitions.
	std::vector<StageInstanceAsset> instances; ///< Placed object instances.
	std::vector<StageInstructionAsset> timeline; ///< Camera and fog commands.
};

/** One immutable application-defined dialogue command. */
struct DialogueCommandAsset final {
	std::int32_t time{};                 ///< Message-local execution frame.
	std::int32_t opcode{};               ///< Application-defined command code.
	std::vector<std::int32_t> arguments; ///< Integer opcode arguments.
	std::string text;                    ///< Decoded command text bytes.
};

/** Immutable decoded dialogue messages. */
struct DialogueAsset final {
	std::vector<std::vector<DialogueCommandAsset>>
	    messages; ///< Commands by message ID.
};

/** Metadata for one sound effect. */
struct SoundAsset final {
	std::int32_t id{};  ///< Application-defined sound-effect ID.
	std::string source; ///< Logical audio asset path.
	float gain{1.0f};   ///< Linear playback gain.
};

/** Metadata for one music track and its optional sample loop. */
struct MusicAsset final {
	std::string id;            ///< Runtime music identifier.
	std::string source;        ///< Logical audio asset path.
	std::uint64_t loopStart{}; ///< Inclusive loop start sample.
	std::uint64_t loopEnd{};   ///< Exclusive loop end sample.
};

/** Immutable decoded audio source manifest. */
struct AudioManifestAsset final {
	std::vector<SoundAsset> sounds; ///< Sound effects indexed by metadata ID.
	std::vector<MusicAsset> music;  ///< Music tracks in manifest order.
};

/** Returns the canonical compact stage-background format. */
[[nodiscard]] constexpr AssetFormat stageFormat() noexcept {
	return AssetFormat::fromName("shiki.stage_background.json.v1");
}

/** Returns the canonical compact dialogue format. */
[[nodiscard]] constexpr AssetFormat dialogueFormat() noexcept {
	return AssetFormat::fromName("shiki.dialogue.json.v1");
}

/** Returns the canonical compact audio-manifest format. */
[[nodiscard]] constexpr AssetFormat audioManifestFormat() noexcept {
	return AssetFormat::fromName("shiki.audio_manifest.json.v1");
}

/** Registers platform-independent structured scene and media decoders. */
[[nodiscard]] Result<void> registerStructuredAssetLoaders(AssetStore &assets);

} // namespace shiki::asset

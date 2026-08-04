#include <shiki/asset/structured_assets.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <memory>

namespace shiki::asset {
namespace {

using Json = nlohmann::json;

[[nodiscard]] Error decodeError(const char *message, const Source &source) {
	Error error{ErrorDomain::Asset,
	            static_cast<std::uint32_t>(AssetError::DecodeFailed), message};
	error.fields.push_back({"source", source.identity});
	return error;
}

[[nodiscard]] Result<Json> parseJson(const Source &source) {
	const auto *begin = reinterpret_cast<const char *>(source.bytes.data());
	const auto *end = begin + source.bytes.size();
	Json value = Json::parse(begin, end, nullptr, false, true);
	if (value.is_discarded())
		return std::unexpected(
		    decodeError("Structured asset JSON is malformed", source));
	return value;
}

[[nodiscard]] bool isVector(const Json &value, std::size_t size) {
	if (!value.is_array() || value.size() != size)
		return false;
	return std::ranges::all_of(
	    value, [](const Json &item) { return item.is_number(); });
}

template <std::size_t Size>
[[nodiscard]] std::array<float, Size> readVector(const Json &value,
                                                 std::size_t offset = 0) {
	std::array<float, Size> result{};
	for (std::size_t index = 0; index < Size; ++index)
		result[index] = value[offset + index].get<float>();
	return result;
}

[[nodiscard]] Result<std::shared_ptr<const StageAsset>>
decodeStage(const Source &source) {
	auto parsed = parseJson(source);
	if (!parsed)
		return std::unexpected(parsed.error());
	const Json &root = *parsed;
	if (!root.is_object() || root.value("version", 0) != 1 ||
	    !root.contains("atlas") || !root["atlas"].is_string() ||
	    !root.contains("objects") || !root["objects"].is_array() ||
	    !root.contains("instances") || !root["instances"].is_array() ||
	    !root.contains("timeline") || !root["timeline"].is_array())
		return std::unexpected(
		    decodeError("Stage JSON has an invalid schema", source));
	auto stage = std::make_shared<StageAsset>();
	stage->atlas = root["atlas"].get<std::string>();
	if (root.contains("name")) {
		if (!root["name"].is_string())
			return std::unexpected(
			    decodeError("Stage name is invalid", source));
		stage->name = root["name"].get<std::string>();
	}
	if (root.contains("songs")) {
		if (!root["songs"].is_array())
			return std::unexpected(
			    decodeError("Stage song names are invalid", source));
		for (const Json &song : root["songs"]) {
			if (!song.is_string())
				return std::unexpected(
				    decodeError("Stage song name is invalid", source));
			stage->songs.push_back(song.get<std::string>());
		}
	}
	for (const Json &item : root["objects"]) {
		if (!item.is_object() || !item.contains("z") ||
		    !item["z"].is_number_integer() || !item.contains("position") ||
		    !isVector(item["position"], 3) || !item.contains("quads") ||
		    !item["quads"].is_array())
			return std::unexpected(
			    decodeError("Stage object is invalid", source));
		StageObjectAsset object{.zLevel = item["z"].get<std::int32_t>(),
		                        .position = readVector<3>(item["position"])};
		for (const Json &quad : item["quads"]) {
			if (!isVector(quad, 6) || !quad[0].is_number_integer())
				return std::unexpected(
				    decodeError("Stage quad is invalid", source));
			object.quads.push_back(
			    StageQuadAsset{.script = quad[0].get<std::int32_t>(),
			                   .position = readVector<3>(quad, 1),
			                   .size = readVector<2>(quad, 4)});
		}
		stage->objects.push_back(std::move(object));
	}
	for (const Json &item : root["instances"]) {
		if (!isVector(item, 4) || !item[0].is_number_integer())
			return std::unexpected(
			    decodeError("Stage instance is invalid", source));
		stage->instances.push_back(
		    StageInstanceAsset{.objectId = item[0].get<std::int32_t>(),
		                       .position = readVector<3>(item, 1)});
	}
	for (const Json &item : root["timeline"]) {
		if (!isVector(item, 6) || !item[0].is_number_integer() ||
		    !item[1].is_number_integer() || !item[2].is_number_integer())
			return std::unexpected(
			    decodeError("Stage timeline instruction is invalid", source));
		stage->timeline.push_back(
		    StageInstructionAsset{.frame = item[0].get<std::int32_t>(),
		                          .opcode = item[1].get<std::int32_t>(),
		                          .argument = item[2].get<std::int32_t>(),
		                          .vector = readVector<3>(item, 3)});
	}
	return stage;
}

[[nodiscard]] Result<std::shared_ptr<const DialogueAsset>>
decodeDialogue(const Source &source) {
	auto parsed = parseJson(source);
	if (!parsed)
		return std::unexpected(parsed.error());
	const Json &root = *parsed;
	if (!root.is_object() || root.value("version", 0) != 1 ||
	    !root.contains("messages") || !root["messages"].is_array())
		return std::unexpected(
		    decodeError("Dialogue JSON has an invalid schema", source));
	auto dialogue = std::make_shared<DialogueAsset>();
	for (const Json &message : root["messages"]) {
		if (!message.is_array())
			return std::unexpected(
			    decodeError("Dialogue message is invalid", source));
		auto &commands = dialogue->messages.emplace_back();
		for (const Json &item : message) {
			if (!item.is_array() || item.size() < 2 ||
			    !item[0].is_number_integer() || !item[1].is_number_integer())
				return std::unexpected(
				    decodeError("Dialogue command is invalid", source));
			DialogueCommandAsset command{.time = item[0].get<std::int32_t>(),
			                             .opcode = item[1].get<std::int32_t>()};
			for (std::size_t index = 2; index < item.size(); ++index) {
				if (item[index].is_string() && index + 1 == item.size())
					command.text = item[index].get<std::string>();
				else if (item[index].is_number_integer())
					command.arguments.push_back(
					    item[index].get<std::int32_t>());
				else
					return std::unexpected(
					    decodeError("Dialogue argument is invalid", source));
			}
			commands.push_back(std::move(command));
		}
	}
	return dialogue;
}

[[nodiscard]] Result<std::shared_ptr<const AudioManifestAsset>>
decodeAudioManifest(const Source &source) {
	auto parsed = parseJson(source);
	if (!parsed)
		return std::unexpected(parsed.error());
	const Json &root = *parsed;
	if (!root.is_object() || root.value("version", 0) != 1 ||
	    !root.contains("sounds") || !root["sounds"].is_array() ||
	    !root.contains("music") || !root["music"].is_array())
		return std::unexpected(
		    decodeError("Audio manifest has an invalid schema", source));
	auto audio = std::make_shared<AudioManifestAsset>();
	for (const Json &item : root["sounds"]) {
		if (!item.is_object() || !item.contains("id") ||
		    !item["id"].is_number_integer() || !item.contains("file") ||
		    !item["file"].is_string() || !item.contains("gain") ||
		    !item["gain"].is_number())
			return std::unexpected(
			    decodeError("Sound entry is invalid", source));
		audio->sounds.push_back(
		    SoundAsset{.id = item["id"].get<std::int32_t>(),
		               .source = item["file"].get<std::string>(),
		               .gain = item["gain"].get<float>()});
	}
	for (const Json &item : root["music"]) {
		if (!item.is_object() || !item.contains("id") ||
		    !item["id"].is_string() || !item.contains("file") ||
		    !item["file"].is_string() || !item.contains("loopStart") ||
		    !item["loopStart"].is_number_unsigned() ||
		    !item.contains("loopEnd") || !item["loopEnd"].is_number_unsigned())
			return std::unexpected(
			    decodeError("Music entry is invalid", source));
		audio->music.push_back(
		    MusicAsset{.id = item["id"].get<std::string>(),
		               .source = item["file"].get<std::string>(),
		               .loopStart = item["loopStart"].get<std::uint64_t>(),
		               .loopEnd = item["loopEnd"].get<std::uint64_t>()});
	}
	return audio;
}

} // namespace

Result<void> registerStructuredAssetLoaders(AssetStore &assets) {
	auto stage =
	    registerAssetDecoder<StageAsset>(assets, stageFormat(), decodeStage);
	if (!stage)
		return std::unexpected(stage.error());
	auto dialogue = registerAssetDecoder<DialogueAsset>(
	    assets, dialogueFormat(), decodeDialogue);
	if (!dialogue)
		return std::unexpected(dialogue.error());
	auto audio = registerAssetDecoder<AudioManifestAsset>(
	    assets, audioManifestFormat(), decodeAudioManifest);
	if (!audio)
		return std::unexpected(audio.error());
	return {};
}

} // namespace shiki::asset

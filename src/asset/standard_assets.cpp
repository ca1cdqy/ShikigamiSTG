#include <shiki/asset/standard_assets.h>

#include <nlohmann/json.hpp>
#include <stb_image.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <string>

namespace shiki::asset {
namespace {

using Json = nlohmann::json;

[[nodiscard]] Error decodeError(const char *message,
                                std::string identity = {}) {
	Error error{ErrorDomain::Asset,
	            static_cast<std::uint32_t>(AssetError::DecodeFailed), message};
	if (!identity.empty())
		error.fields.push_back({"source", std::move(identity)});
	return error;
}

[[nodiscard]] Result<Json> parseJson(const Source &source) {
	const auto *begin = reinterpret_cast<const char *>(source.bytes.data());
	const auto *end = begin + source.bytes.size();
	Json value = Json::parse(begin, end, nullptr, false, true);
	if (value.is_discarded())
		return std::unexpected(
		    decodeError("Asset JSON is malformed", source.identity));
	return value;
}

[[nodiscard]] Result<std::shared_ptr<const ImageAsset>>
decodeImage(const Source &source) {
	if (source.bytes.size() >
	    static_cast<std::size_t>(std::numeric_limits<int>::max()))
		return std::unexpected(decodeError(
		    "Image source exceeds decoder size limits", source.identity));
	int width{};
	int height{};
	int channels{};
	stbi_uc *pixels = stbi_load_from_memory(
	    reinterpret_cast<const stbi_uc *>(source.bytes.data()),
	    static_cast<int>(source.bytes.size()), &width, &height, &channels, 4);
	if (pixels == nullptr || width <= 0 || height <= 0) {
		if (pixels != nullptr)
			stbi_image_free(pixels);
		return std::unexpected(
		    decodeError("Image source cannot be decoded", source.identity));
	}
	const std::size_t byteCount =
	    static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
	auto result = std::make_shared<ImageAsset>();
	result->width = static_cast<std::uint32_t>(width);
	result->height = static_cast<std::uint32_t>(height);
	result->rgba.resize(byteCount);
	std::memcpy(result->rgba.data(), pixels, byteCount);
	stbi_image_free(pixels);
	return result;
}

[[nodiscard]] Result<std::shared_ptr<const SpriteAtlasAsset>>
decodeSpriteAtlas(const Source &source) {
	auto parsed = parseJson(source);
	if (!parsed)
		return std::unexpected(parsed.error());
	const Json &root = *parsed;
	if (!root.is_object() || root.value("version", 0) != 1 ||
	    !root.contains("source") || !root["source"].is_string() ||
	    !root.contains("sprites") || !root["sprites"].is_array())
		return std::unexpected(decodeError(
		    "Sprite atlas JSON has an invalid schema", source.identity));
	auto atlas = std::make_shared<SpriteAtlasAsset>();
	atlas->name = root["source"].get<std::string>();
	for (const Json &item : root["sprites"]) {
		if (!item.is_object() || !item.contains("id") ||
		    !item["id"].is_number_integer() || !item.contains("asset") ||
		    !item["asset"].is_string())
			return std::unexpected(decodeError(
			    "Sprite atlas frame is missing its identity", source.identity));
		SpriteFrameAsset frame;
		frame.id = item["id"].get<std::int32_t>();
		frame.image = AssetId::fromName(item["asset"].get<std::string>());
		frame.width = item.value("width", 0U);
		frame.height = item.value("height", 0U);
		frame.originX = item.value("origin_x", 0.0F);
		frame.originY = item.value("origin_y", 0.0F);
		atlas->sprites.push_back(frame);
	}
	std::ranges::sort(atlas->sprites, {}, &SpriteFrameAsset::id);
	if (std::ranges::adjacent_find(atlas->sprites, {}, &SpriteFrameAsset::id) !=
	    atlas->sprites.end())
		return std::unexpected(decodeError(
		    "Sprite atlas contains duplicate frame IDs", source.identity));
	return atlas;
}

[[nodiscard]] int hexDigit(char value) noexcept {
	if (value >= '0' && value <= '9')
		return value - '0';
	if (value >= 'a' && value <= 'f')
		return value - 'a' + 10;
	if (value >= 'A' && value <= 'F')
		return value - 'A' + 10;
	return -1;
}

template <class T>
void appendScalar(std::vector<std::byte> &output, const T &value) {
	const auto *bytes = reinterpret_cast<const std::byte *>(&value);
	output.insert(output.end(), bytes, bytes + sizeof(T));
}

[[nodiscard]] bool appendHex(std::vector<std::byte> &output,
                             std::string_view value) {
	if ((value.size() & 1U) != 0U)
		return false;
	for (std::size_t index = 0; index < value.size(); index += 2) {
		const int high = hexDigit(value[index]);
		const int low = hexDigit(value[index + 1]);
		if (high < 0 || low < 0)
			return false;
		output.push_back(static_cast<std::byte>((high << 4) | low));
	}
	return true;
}

[[nodiscard]] Result<std::shared_ptr<const AnimationAsset>>
decodeAnimation(const Source &source) {
	auto parsed = parseJson(source);
	if (!parsed)
		return std::unexpected(parsed.error());
	const Json &root = *parsed;
	if (!root.is_object() || root.value("version", 0) != 1 ||
	    !root.contains("atlas") || !root["atlas"].is_string() ||
	    !root.contains("scripts") || !root["scripts"].is_object())
		return std::unexpected(decodeError(
		    "Animation JSON has an invalid schema", source.identity));
	auto animation = std::make_shared<AnimationAsset>();
	animation->atlas = root["atlas"].get<std::string>();
	if (const auto found = root.find("spriteMap"); found != root.end()) {
		if (!found->is_object())
			return std::unexpected(decodeError(
			    "Animation sprite map must be an object", source.identity));
		for (const auto &[rawId, localId] : found->items()) {
			if (!localId.is_number_integer())
				return std::unexpected(
				    decodeError("Animation sprite map value must be an integer",
				                source.identity));
			animation->spriteMap.emplace(std::stoi(rawId),
			                             localId.get<std::int32_t>());
		}
	}
	for (const auto &[idText, instructions] : root["scripts"].items()) {
		if (!instructions.is_array())
			return std::unexpected(decodeError(
			    "Animation script must be an array", source.identity));
		const std::size_t begin = animation->instructions.size();
		for (const Json &instruction : instructions) {
			if (!instruction.is_array() || instruction.size() != 3 ||
			    !instruction[0].is_number_integer() ||
			    !instruction[1].is_number_integer() ||
			    !instruction[2].is_string())
				return std::unexpected(
				    decodeError("Animation instruction has an invalid schema",
				                source.identity));
			const std::int16_t time = instruction[0].get<std::int16_t>();
			const std::uint8_t opcode = instruction[1].get<std::uint8_t>();
			const std::string arguments = instruction[2].get<std::string>();
			const std::size_t argumentSize = arguments.size() / 2;
			if ((arguments.size() & 1U) != 0U || argumentSize > 255U)
				return std::unexpected(decodeError(
				    "Animation argument payload is invalid", source.identity));
			appendScalar(animation->instructions, time);
			appendScalar(animation->instructions, opcode);
			appendScalar(animation->instructions,
			             static_cast<std::uint8_t>(argumentSize));
			if (!appendHex(animation->instructions, arguments))
				return std::unexpected(
				    decodeError("Animation argument payload is not hexadecimal",
				                source.identity));
		}
		animation->scripts.emplace(
		    std::stoi(idText),
		    AnimationScriptRange{begin, animation->instructions.size()});
	}
	return animation;
}

} // namespace

const SpriteFrameAsset *SpriteAtlasAsset::find(std::int32_t id) const noexcept {
	const auto found =
	    std::ranges::lower_bound(sprites, id, {}, &SpriteFrameAsset::id);
	return found != sprites.end() && found->id == id ? &*found : nullptr;
}

std::int32_t AnimationAsset::resolveSprite(std::int32_t rawId) const noexcept {
	const auto found = spriteMap.find(rawId);
	return found == spriteMap.end() ? rawId : found->second;
}

Result<void> registerStandardAssetLoaders(AssetStore &assets) {
	auto image =
	    registerAssetDecoder<ImageAsset>(assets, imageFormat(), decodeImage);
	if (!image)
		return std::unexpected(image.error());
	auto atlas = registerAssetDecoder<SpriteAtlasAsset>(
	    assets, spriteAtlasFormat(), decodeSpriteAtlas);
	if (!atlas)
		return std::unexpected(atlas.error());
	auto animation = registerAssetDecoder<AnimationAsset>(
	    assets, animationFormat(), decodeAnimation);
	if (!animation)
		return std::unexpected(animation.error());
	return {};
}

Result<void> addJsonManifest(AssetStore &assets, const Source &source) {
	auto parsed = parseJson(source);
	if (!parsed)
		return std::unexpected(parsed.error());
	const Json &root = *parsed;
	if (!root.is_object() || root.value("version", 0) != 1 ||
	    !root.contains("assets") || !root["assets"].is_array())
		return std::unexpected(decodeError(
		    "Asset manifest has an invalid schema", source.identity));
	for (const Json &item : root["assets"]) {
		if (!item.is_object() || !item.contains("id") ||
		    !item["id"].is_string() || !item.contains("format") ||
		    !item["format"].is_string() || !item.contains("source") ||
		    !item["source"].is_string())
			return std::unexpected(decodeError(
			    "Manifest entry has an invalid schema", source.identity));
		ManifestEntry entry{
		    .id = AssetId::fromName(item["id"].get<std::string>()),
		    .format = AssetFormat::fromName(item["format"].get<std::string>()),
		    .source = item["source"].get<std::string>()};
		if (item.contains("dependencies")) {
			if (!item["dependencies"].is_array())
				return std::unexpected(decodeError(
				    "Manifest dependencies must be an array", source.identity));
			for (const Json &dependency : item["dependencies"]) {
				if (!dependency.is_string())
					return std::unexpected(
					    decodeError("Manifest dependency must be a string",
					                source.identity));
				entry.dependencies.push_back(
				    AssetId::fromName(dependency.get<std::string>()));
			}
		}
		auto added = addAsset(assets, std::move(entry));
		if (!added)
			return std::unexpected(added.error());
	}
	return {};
}

} // namespace shiki::asset

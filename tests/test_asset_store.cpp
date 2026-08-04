#include <catch2/catch_test_macros.hpp>
#include <shiki/asset/asset_store.h>

#include <charconv>
#include <cstring>
#include <memory>
#include <string>

namespace {

struct PlainTextAsset final {
	int value{};
};

shiki::asset::Source makeSource(std::string identity, std::string value) {
	shiki::asset::Source source;
	source.identity = std::move(identity);
	source.bytes.resize(value.size());
	std::memcpy(source.bytes.data(), value.data(), value.size());
	return source;
}

shiki::Result<std::shared_ptr<const PlainTextAsset>>
decodePlainText(const shiki::asset::Source &source) {
	const auto *begin = reinterpret_cast<const char *>(source.bytes.data());
	const auto *end = begin + source.bytes.size();
	int value{};
	const auto parsed = std::from_chars(begin, end, value);
	if (parsed.ec != std::errc{} || parsed.ptr != end)
		return std::unexpected(shiki::Error{
		    shiki::ErrorDomain::Asset,
		    static_cast<std::uint32_t>(shiki::asset::AssetError::DecodeFailed),
		    "Plain-text integer is invalid"});
	return std::make_shared<const PlainTextAsset>(PlainTextAsset{value});
}

} // namespace

TEST_CASE("Procedural asset API decodes an application-defined format",
          "[asset][procedural]") {
	using namespace shiki::asset;
	int sourceReads = 0;
	auto sources = std::make_unique<CallbackSourceProvider>(
	    [&sourceReads](std::string_view source) -> shiki::Result<Source> {
		    ++sourceReads;
		    return makeSource(std::string(source), "42");
	    });
	AssetStore store(std::move(sources));
	constexpr auto format = AssetFormat::fromName("example.integer.text.v1");
	constexpr auto id = AssetId::fromName("example.answer");

	REQUIRE(
	    registerAssetDecoder<PlainTextAsset>(store, format, decodePlainText));
	auto direct = decodeAsset<PlainTextAsset>(
	    store, format, makeSource("memory:direct", "17"));
	REQUIRE(direct);
	CHECK((*direct)->value == 17);
	CHECK(sourceReads == 0);

	REQUIRE(addAsset(
	    store,
	    ManifestEntry{.id = id, .format = format, .source = "answer.txt"}));
	auto loaded = loadAsset<PlainTextAsset>(store, id);
	REQUIRE(loaded);
	CHECK((*loaded)->value == 42);
	CHECK(sourceReads == 1);

	auto cached = findAsset<PlainTextAsset>(store, id);
	REQUIRE(cached);
	CHECK((*cached)->value == 42);
}

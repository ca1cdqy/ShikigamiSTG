#include <shiki/core/binary_codec.h>

namespace shiki {
namespace {

[[nodiscard]] Error truncatedPayload() {
	return Error{ErrorDomain::Flow, 1, "Binary payload is truncated"};
}

} // namespace

void BinaryWriter::writeU8(std::uint8_t value) {
	bytes_.push_back(static_cast<std::byte>(value));
}

void BinaryWriter::writeU32(std::uint32_t value) {
	for (std::uint32_t shift = 0; shift < 32; shift += 8)
		writeU8(static_cast<std::uint8_t>(value >> shift));
}

void BinaryWriter::writeU64(std::uint64_t value) {
	for (std::uint32_t shift = 0; shift < 64; shift += 8)
		writeU8(static_cast<std::uint8_t>(value >> shift));
}

void BinaryWriter::writeI64(std::int64_t value) {
	writeU64(std::bit_cast<std::uint64_t>(value));
}

void BinaryWriter::writeF32(float value) {
	writeU32(std::bit_cast<std::uint32_t>(value));
}

void BinaryWriter::writeBool(bool value) { writeU8(value ? 1 : 0); }

Result<std::span<const std::byte>> BinaryReader::take(std::size_t count) {
	if (count > bytes_.size() - offset_)
		return std::unexpected(truncatedPayload());
	const auto result = bytes_.subspan(offset_, count);
	offset_ += count;
	return result;
}

Result<std::uint8_t> BinaryReader::readU8() {
	auto bytes = take(1);
	if (!bytes)
		return std::unexpected(bytes.error());
	return std::to_integer<std::uint8_t>((*bytes)[0]);
}

Result<std::uint32_t> BinaryReader::readU32() {
	auto bytes = take(4);
	if (!bytes)
		return std::unexpected(bytes.error());
	std::uint32_t value{};
	for (std::uint32_t index = 0; index < 4; ++index)
		value |= std::uint32_t{std::to_integer<std::uint8_t>((*bytes)[index])}
		         << (index * 8);
	return value;
}

Result<std::uint64_t> BinaryReader::readU64() {
	auto bytes = take(8);
	if (!bytes)
		return std::unexpected(bytes.error());
	std::uint64_t value{};
	for (std::uint32_t index = 0; index < 8; ++index)
		value |= std::uint64_t{std::to_integer<std::uint8_t>((*bytes)[index])}
		         << (index * 8);
	return value;
}

Result<std::int64_t> BinaryReader::readI64() {
	auto value = readU64();
	if (!value)
		return std::unexpected(value.error());
	return std::bit_cast<std::int64_t>(*value);
}

Result<float> BinaryReader::readF32() {
	auto value = readU32();
	if (!value)
		return std::unexpected(value.error());
	return std::bit_cast<float>(*value);
}

Result<bool> BinaryReader::readBool() {
	auto value = readU8();
	if (!value)
		return std::unexpected(value.error());
	if (*value > 1)
		return std::unexpected(
		    Error{ErrorDomain::Flow, 2, "Binary boolean is not canonical"});
	return *value != 0;
}

} // namespace shiki

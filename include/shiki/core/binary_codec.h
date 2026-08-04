#pragma once

#include <shiki/core/result.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace shiki {

/** Writes canonical little-endian values for immutable engine payloads. */
class BinaryWriter final {
  public:
	void writeU8(std::uint8_t value);
	void writeU32(std::uint32_t value);
	void writeU64(std::uint64_t value);
	void writeI64(std::int64_t value);
	void writeF32(float value);
	void writeBool(bool value);
	/** Moves the completed canonical byte sequence out of this writer. */
	[[nodiscard]] std::vector<std::byte> finish() && noexcept {
		return std::move(bytes_);
	}

  private:
	std::vector<std::byte> bytes_;
};

/** Reads bounds-checked canonical little-endian engine payloads. */
class BinaryReader final {
  public:
	explicit BinaryReader(std::span<const std::byte> bytes) noexcept
	    : bytes_(bytes) {}
	[[nodiscard]] Result<std::uint8_t> readU8();
	[[nodiscard]] Result<std::uint32_t> readU32();
	[[nodiscard]] Result<std::uint64_t> readU64();
	[[nodiscard]] Result<std::int64_t> readI64();
	[[nodiscard]] Result<float> readF32();
	[[nodiscard]] Result<bool> readBool();
	/** Returns whether the entire payload was consumed exactly. */
	[[nodiscard]] bool empty() const noexcept {
		return offset_ == bytes_.size();
	}

  private:
	[[nodiscard]] Result<std::span<const std::byte>> take(std::size_t count);
	std::span<const std::byte> bytes_;
	std::size_t offset_{};
};

} // namespace shiki

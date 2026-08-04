#pragma once

#include <shiki/core/result.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace shiki {
namespace ecl {

/// A dynamically typed ECL instruction parameter value.
using ECLValue = std::variant<int32_t,    ///< Signed 32-bit integer.
                              uint32_t,   ///< Unsigned 32-bit integer.
                              float,      ///< Single-precision floating-point value.
                              std::string ///< Null-terminated string.
                              >;

/** One typed parameter parsed from an ECL instruction argument list. */
struct ECLParam {
	char type = '\0'; ///< Type tag: 'S'=int16, 's'=int8, 'U'=uint16, 'u'=uint8, 'f'=float, 'z'=string, etc.
	ECLValue value;   ///< Parsed value matching the type tag.
	bool isStack = false; ///< Whether the value references the ECL operand stack.
};

/** One decoded ECL instruction with its timing and difficulty metadata. */
struct ECLInstruction {
	uint32_t time = 0;      ///< Execution time in fixed-rate ticks.
	uint16_t id = 0;        ///< Instruction opcode identifier.
	uint16_t size = 0;      ///< Total encoded instruction size in bytes.
	uint16_t rankMask = 0;  ///< Bit mask selecting which difficulty ranks execute this instruction.
	uint16_t paramMask = 0; ///< Bit mask indicating which parameters use stack references.
	std::vector<ECLParam> params; ///< Decoded instruction parameters in format order.
	uint32_t address = 0;   ///< Source-file byte offset used for diagnostics and timeline linking.
};

/** A named collection of ECL instructions representing one enemy subroutine. */
struct ECLSubroutine {
	std::string name;                        ///< Subroutine name from the ECL header.
	std::vector<ECLInstruction> instructions; ///< Instructions in source order.
};

/** A named collection of ECL timeline instructions for stage-flow control. */
struct ECLTimeline {
	std::string name;                        ///< Timeline name from the ECL header.
	std::vector<ECLInstruction> instructions; ///< Instructions in time-ascending order.
};

/** The complete decoded content of one ECL binary file. */
struct ECLFile {
	uint32_t version = 0;              ///< Game version; 6 identifies Touhou 6.
	std::vector<ECLSubroutine> subs;   ///< Enemy subroutines in header order.
	std::vector<ECLTimeline> timelines; ///< Stage timelines in header order.
};

/**
 * Decodes a TH06-format ECL binary into an ECLFile.
 *
 * Call parse() with caller-owned bytes, or parseFile() with a filesystem path.
 * The parsed result is retained internally and accessible via getFile() until
 * the next parse call or clear().
 *
 * ECLParser is non-copyable; it is movable to allow ownership transfer.
 */
class ECLParser {
  public:
	/** Creates an empty parser with no decoded content. */
	ECLParser() = default;
	~ECLParser() = default;

	/** ECL parsers cannot be copied. */
	ECLParser(const ECLParser &) = delete;
	/** ECL parsers cannot be copy-assigned. */
	ECLParser &operator=(const ECLParser &) = delete;

	/** Transfers decoded content and parser state. */
	ECLParser(ECLParser &&) noexcept = default;
	/** Replaces this parser with moved state. */
	ECLParser &operator=(ECLParser &&) noexcept = default;

	/** Decodes ECL bytes already owned by the caller. Returns an error on failure. */
	[[nodiscard]] Result<void> parse(std::span<const std::byte> bytes,
	                                 uint32_t version = 6);

	/** Reads and decodes an ECL file from the filesystem. Returns true on success. */
	bool parseFile(const std::string &filePath, uint32_t version = 6);

	/** Returns a reference to the internally retained decoded ECL data. */
	[[nodiscard]] const ECLFile &getFile() const { return file_; }
	/** Returns a mutable reference to the internally retained decoded ECL data. */
	[[nodiscard]] ECLFile &getFile() { return file_; }

	/** Moves the decoded ECL data out of the parser, leaving it empty. */
	[[nodiscard]] ECLFile takeFile() && noexcept { return std::move(file_); }

	/** Replaces internally retained data with a pre-decoded immutable asset copy. */
	void setFile(ECLFile file) noexcept { file_ = std::move(file); }

	/** Returns a pointer to the named subroutine, or null if not found. */
	[[nodiscard]] const ECLSubroutine *
	getSubroutine(const std::string &name) const;
	/** Returns a pointer to the subroutine at index, or null if out of range. */
	[[nodiscard]] const ECLSubroutine *getSubroutine(size_t index) const;

	/** Returns a pointer to the named timeline, or null if not found. */
	[[nodiscard]] const ECLTimeline *getTimeline(const std::string &name) const;
	/** Returns a pointer to the timeline at index, or null if out of range. */
	[[nodiscard]] const ECLTimeline *getTimeline(size_t index) const;

	/** Discards all decoded content and resets parser state. */
	void clear();

  private:
	ECLFile file_;

	/// Internal parsing helpers
	bool parseHeader(const uint8_t *data, size_t size);
	bool parseSubroutines(const uint8_t *data, size_t size,
	                      const std::vector<uint32_t> &offsets,
	                      size_t timelineCountMax, size_t subCount);
	bool parseTimelines(const uint8_t *data, size_t size,
	                    const std::vector<uint32_t> &offsets,
	                    size_t timelineCount);

	/// Instruction parsing
	bool parseInstruction(const uint8_t *data, size_t size, size_t &offset,
	                      ECLInstruction &instr, bool isTimeline);
	bool parseTimelineInstruction(const uint8_t *data, size_t size,
	                              size_t &offset, ECLInstruction &instr);

	/// Parameter parsing
	bool parseParams(const uint8_t *data, size_t dataSize, uint16_t id,
	                 const std::string &format, std::vector<ECLParam> &params);

	/// Returns the instruction format string
	std::string getInstructionFormat(uint16_t id, bool isTimeline) const;
};

} // namespace ecl
} // namespace shiki

#pragma once

#include <shiki/core/result.h>
#include <shiki/flow/action.h>
#include <shiki/flow/stage_program.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace shiki::flow {

/** A non-owning immutable source supplied to one parser invocation. */
struct StageSource final {
	std::string_view identity;
	std::span<const std::byte> bytes;
};

/** Read-only registered capabilities available while parsing a stage. */
struct StageParseContext final {
	const ActionRegistry &actions;
	const ConditionRegistry &conditions;
	game::SchemaDigest schema{};
};

/** Converts one external stage format into a validated StageProgram. */
class StageParser {
  public:
	virtual ~StageParser() = default;
	/** Parses source without retaining its non-owning byte span. */
	[[nodiscard]] virtual Result<StageProgram>
	parse(const StageSource &source, const StageParseContext &context) = 0;
};

/** Stable failures produced by stage parser registration and lookup. */
enum class StageParserError : std::uint32_t {
	EmptyFormat = 1,
	DuplicateFormat,
	UnknownFormat,
	NullParser,
	RegistryFrozen
};

/** Owns named parser implementations at the external asset boundary. */
class StageParserRegistry final {
  public:
	/** Registers one parser for a stable manifest format name. */
	[[nodiscard]] Result<void> add(std::string format,
	                               std::unique_ptr<StageParser> parser);

	/** Parses one source with a registered format implementation. */
	[[nodiscard]] Result<StageProgram>
	parse(std::string_view format, const StageSource &source,
	      const StageParseContext &context) const;

	/** Prevents parser mutation after definition freeze. */
	void freeze() noexcept { frozen_ = true; }

  private:
	struct Entry final {
		std::string format;
		std::unique_ptr<StageParser> parser;
	};
	std::vector<Entry> entries_;
	bool frozen_{};
};

} // namespace shiki::flow

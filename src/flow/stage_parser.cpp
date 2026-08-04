#include <shiki/flow/stage_parser.h>

#include <algorithm>

namespace shiki::flow {
namespace {

[[nodiscard]] Error makeParserError(StageParserError code,
                                    const char *message) {
	return Error{ErrorDomain::Flow, static_cast<std::uint32_t>(code), message};
}

} // namespace

Result<void> StageParserRegistry::add(std::string format,
                                      std::unique_ptr<StageParser> parser) {
	if (frozen_)
		return std::unexpected(makeParserError(
		    StageParserError::RegistryFrozen,
		    "Stage parser registry cannot change after definition freeze"));
	if (format.empty())
		return std::unexpected(
		    makeParserError(StageParserError::EmptyFormat,
		                    "Stage parser format cannot be empty"));
	if (parser == nullptr)
		return std::unexpected(makeParserError(StageParserError::NullParser,
		                                       "Stage parser cannot be null"));
	if (std::ranges::any_of(entries_, [&](const Entry &entry) {
		    return entry.format == format;
	    }))
		return std::unexpected(
		    makeParserError(StageParserError::DuplicateFormat,
		                    "Stage parser format is already registered"));
	entries_.push_back(Entry{std::move(format), std::move(parser)});
	return {};
}

Result<StageProgram>
StageParserRegistry::parse(std::string_view format, const StageSource &source,
                           const StageParseContext &context) const {
	const auto entry = std::ranges::find(entries_, format, &Entry::format);
	if (entry == entries_.end())
		return std::unexpected(makeParserError(
		    StageParserError::UnknownFormat,
		    "No stage parser is registered for the requested format"));
	return entry->parser->parse(source, context);
}

} // namespace shiki::flow

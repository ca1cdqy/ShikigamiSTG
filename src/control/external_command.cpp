#include <shiki/control/external_command.h>

#include <algorithm>

namespace shiki::control {

Result<ExternalCommand>
ExternalCommandRegistry::load(game::TypeKey type, std::uint32_t version,
                              std::span<const std::byte> payload) const {
	const auto entry = std::ranges::find(entries_, type, &Entry::type);
	if (entry == entries_.end())
		return std::unexpected(
		    Error{ErrorDomain::Core,
		          static_cast<std::uint32_t>(ExternalCommandError::UnknownType),
		          "External command type is not registered"});
	if (entry->version != version)
		return std::unexpected(Error{
		    ErrorDomain::Core,
		    static_cast<std::uint32_t>(ExternalCommandError::VersionMismatch),
		    "External command version is not supported"});
	auto validated = entry->validate(payload);
	if (!validated)
		return std::unexpected(validated.error());
	return ExternalCommand{type, version, {payload.begin(), payload.end()}};
}

Result<void>
ExternalCommandRegistry::execute(const ExternalCommand &command,
                                 stg::GameplayContext &game) const {
	const auto entry = std::ranges::find(entries_, command.type_, &Entry::type);
	if (entry == entries_.end())
		return std::unexpected(
		    Error{ErrorDomain::Core,
		          static_cast<std::uint32_t>(ExternalCommandError::UnknownType),
		          "External command type is not registered"});
	if (entry->version != command.version_)
		return std::unexpected(Error{
		    ErrorDomain::Core,
		    static_cast<std::uint32_t>(ExternalCommandError::VersionMismatch),
		    "External command version is not supported"});
	return entry->execute(game, command.payload_);
}

std::vector<ExternalCommandRegistration>
ExternalCommandRegistry::registrations() const {
	std::vector<ExternalCommandRegistration> result;
	result.reserve(entries_.size());
	for (const Entry &entry : entries_)
		result.push_back({entry.name, entry.version});
	return result;
}

} // namespace shiki::control

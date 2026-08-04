#include <shiki/flow/action.h>

#include <algorithm>

namespace shiki::flow {

Result<void> ActionRegistry::execute(game::TypeKey type, std::uint32_t version,
                                     std::span<const std::byte> payload,
                                     stg::GameplayContext &game) const {
	const auto entry = std::ranges::find(entries_, type, &Entry::type);
	if (entry == entries_.end())
		return std::unexpected(
		    Error{ErrorDomain::Flow,
		          static_cast<std::uint32_t>(ActionError::UnknownType),
		          "Stage action type is not registered"});
	if (entry->version != version)
		return std::unexpected(
		    Error{ErrorDomain::Flow,
		          static_cast<std::uint32_t>(ActionError::VersionMismatch),
		          "Stage action version is not supported"});
	return entry->execute(game, payload);
}

std::vector<std::string> ActionRegistry::names() const {
	std::vector<std::string> result;
	result.reserve(entries_.size());
	for (const Entry &entry : entries_)
		result.push_back(entry.name);
	return result;
}

std::vector<ActionRegistration> ActionRegistry::registrations() const {
	std::vector<ActionRegistration> result;
	result.reserve(entries_.size());
	for (const Entry &entry : entries_)
		result.push_back(ActionRegistration{entry.name, entry.version});
	return result;
}

} // namespace shiki::flow

#include <shiki/flow/condition.h>

#include <algorithm>

namespace shiki::flow {

Result<bool>
ConditionRegistry::evaluate(game::TypeKey type, std::uint32_t version,
                            std::span<const std::byte> payload,
                            const stg::GameplayContext &game) const {
	const auto entry = std::ranges::find(entries_, type, &Entry::type);
	if (entry == entries_.end())
		return std::unexpected(
		    Error{ErrorDomain::Flow,
		          static_cast<std::uint32_t>(ConditionError::UnknownType),
		          "Stage condition type is not registered"});
	if (entry->version != version)
		return std::unexpected(
		    Error{ErrorDomain::Flow,
		          static_cast<std::uint32_t>(ConditionError::VersionMismatch),
		          "Stage condition version is not supported"});
	return entry->evaluate(game, payload);
}

std::vector<ConditionRegistration> ConditionRegistry::registrations() const {
	std::vector<ConditionRegistration> result;
	result.reserve(entries_.size());
	for (const Entry &entry : entries_)
		result.push_back(ConditionRegistration{entry.name, entry.version});
	return result;
}

} // namespace shiki::flow

#include <shiki/stg/pattern/pattern.h>

#include <algorithm>

namespace shiki::stg {

Result<std::unique_ptr<Pattern>>
PatternRegistry::create(const PatternInvocation &invocation) const {
	const auto entry =
	    std::ranges::find(entries_, invocation.type_, &Entry::type);
	if (entry == entries_.end())
		return std::unexpected(
		    Error{ErrorDomain::Definition,
		          static_cast<std::uint32_t>(PatternError::UnknownType),
		          "Pattern type is not registered"});
	if (entry->typeTag != invocation.typeTag_ ||
	    entry->version != invocation.version_ ||
	    invocation.arguments_ == nullptr)
		return std::unexpected(
		    Error{ErrorDomain::Definition,
		          static_cast<std::uint32_t>(PatternError::ArgumentMismatch),
		          "Pattern arguments do not match the registered type"});
	auto pattern = entry->create(invocation.arguments_.get());
	if (!pattern)
		return std::unexpected(pattern.error());
	if (*pattern == nullptr)
		return std::unexpected(
		    Error{ErrorDomain::Definition,
		          static_cast<std::uint32_t>(PatternError::FactoryFailed),
		          "Pattern factory returned a null instance"});
	return pattern;
}

std::vector<std::string> PatternRegistry::names() const {
	std::vector<std::string> result;
	result.reserve(entries_.size());
	for (const Entry &entry : entries_)
		result.push_back(entry.name);
	return result;
}

std::vector<PatternRegistration> PatternRegistry::registrations() const {
	std::vector<PatternRegistration> result;
	result.reserve(entries_.size());
	for (const Entry &entry : entries_)
		result.push_back(PatternRegistration{entry.name, entry.version});
	return result;
}

} // namespace shiki::stg

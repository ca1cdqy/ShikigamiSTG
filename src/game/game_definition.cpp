#include <shiki/game_definition.h>

#include <algorithm>
#include <limits>
#include <map>
#include <string>

namespace shiki {
namespace {

[[nodiscard]] Error makeDefinitionError(DefinitionError code,
                                        std::string message) {
	return Error{ErrorDomain::Definition, static_cast<std::uint32_t>(code),
	             std::move(message)};
}

[[nodiscard]] std::uint8_t phaseValue(game::SystemPhase phase) noexcept {
	return static_cast<std::uint8_t>(phase);
}

} // namespace

Result<void> GameDefinition::addSystem(game::SystemDescriptor descriptor,
                                       GameplaySystem system) {
	if (frozen_) {
		return std::unexpected(makeDefinitionError(
		    DefinitionError::DefinitionFrozen,
		    "Cannot add a system after the definition is frozen"));
	}
	if (descriptor.name.empty()) {
		return std::unexpected(makeDefinitionError(
		    DefinitionError::EmptySystemName, "System name cannot be empty"));
	}
	if (!system) {
		return std::unexpected(
		    makeDefinitionError(DefinitionError::InvalidSystemCallback,
		                        "System callback cannot be empty"));
	}
	const game::TypeKey key = game::typeKeyFromName(descriptor.name);
	if (std::ranges::any_of(systems_, [&](const SystemEntry &entry) {
		    return entry.key == key;
	    })) {
		return std::unexpected(makeDefinitionError(
		    DefinitionError::DuplicateSystem,
		    "System name or persistent key is already registered"));
	}
	auto type = types_.add(
	    {.domain = game::TypeDomain::System, .name = descriptor.name});
	if (!type)
		return std::unexpected(type.error());
	systems_.push_back(
	    SystemEntry{std::move(descriptor), key, std::move(system)});
	return {};
}

Result<void> GameDefinition::freeze() {
	if (frozen_)
		return {};
	if (systems_.size() > static_cast<std::size_t>(
	                          std::numeric_limits<std::uint16_t>::max() - 6)) {
		return std::unexpected(makeDefinitionError(
		    DefinitionError::TooManySystems,
		    "Definition exceeds the deterministic system index capacity"));
	}
	for (const std::string &name : actors_.names()) {
		auto type =
		    types_.add({.domain = game::TypeDomain::Actor, .name = name});
		if (!type)
			return std::unexpected(type.error());
	}
	for (const std::string &name : controllers_.names()) {
		auto type =
		    types_.add({.domain = game::TypeDomain::Controller, .name = name});
		if (!type)
			return std::unexpected(type.error());
	}
	for (const control::ExternalCommandRegistration &command :
	     externalCommands_.registrations()) {
		auto type = types_.add({.domain = game::TypeDomain::ExternalCommand,
		                        .name = command.name,
		                        .version = command.version});
		if (!type)
			return std::unexpected(type.error());
	}
	for (const flow::ActionRegistration &action : actions_.registrations()) {
		auto type = types_.add({.domain = game::TypeDomain::Action,
		                        .name = action.name,
		                        .version = action.version});
		if (!type)
			return std::unexpected(type.error());
	}
	for (const flow::ConditionRegistration &condition :
	     conditions_.registrations()) {
		auto type = types_.add({.domain = game::TypeDomain::Condition,
		                        .name = condition.name,
		                        .version = condition.version});
		if (!type)
			return std::unexpected(type.error());
	}
	for (const stg::PatternRegistration &pattern : patterns_.registrations()) {
		auto type = types_.add({.domain = game::TypeDomain::Pattern,
		                        .name = pattern.name,
		                        .version = pattern.version});
		if (!type)
			return std::unexpected(type.error());
	}

	std::map<game::TypeKey, std::size_t> indices;
	for (std::size_t index = 0; index < systems_.size(); ++index)
		indices.emplace(systems_[index].key, index);

	std::vector<std::vector<std::size_t>> edges(systems_.size());
	std::vector<std::size_t> indegrees(systems_.size());
	const auto addEdge = [&](std::size_t before, std::size_t after) {
		auto &outgoing = edges[before];
		if (std::ranges::find(outgoing, after) == outgoing.end()) {
			outgoing.push_back(after);
			++indegrees[after];
		}
	};
	for (std::size_t index = 0; index < systems_.size(); ++index) {
		const auto &entry = systems_[index];
		for (const game::TypeKey dependency : entry.descriptor.before) {
			const auto found = indices.find(dependency);
			if (found == indices.end()) {
				return std::unexpected(makeDefinitionError(
				    DefinitionError::MissingDependency,
				    "System before dependency is not registered"));
			}
			if (phaseValue(entry.descriptor.phase) >
			    phaseValue(systems_[found->second].descriptor.phase)) {
				return std::unexpected(makeDefinitionError(
				    DefinitionError::ReversePhaseDependency,
				    "System dependency reverses fixed phase order"));
			}
			addEdge(index, found->second);
		}
		for (const game::TypeKey dependency : entry.descriptor.after) {
			const auto found = indices.find(dependency);
			if (found == indices.end()) {
				return std::unexpected(makeDefinitionError(
				    DefinitionError::MissingDependency,
				    "System after dependency is not registered"));
			}
			if (phaseValue(entry.descriptor.phase) <
			    phaseValue(systems_[found->second].descriptor.phase)) {
				return std::unexpected(makeDefinitionError(
				    DefinitionError::ReversePhaseDependency,
				    "System dependency reverses fixed phase order"));
			}
			addEdge(found->second, index);
		}
	}

	schedule_.clear();
	schedule_.reserve(systems_.size());
	std::vector<bool> scheduled(systems_.size());
	while (schedule_.size() != systems_.size()) {
		std::size_t selected = systems_.size();
		for (std::size_t index = 0; index < systems_.size(); ++index) {
			if (scheduled[index] || indegrees[index] != 0)
				continue;
			if (selected == systems_.size() ||
			    std::tuple{phaseValue(systems_[index].descriptor.phase),
			               systems_[index].key} <
			        std::tuple{phaseValue(systems_[selected].descriptor.phase),
			                   systems_[selected].key}) {
				selected = index;
			}
		}
		if (selected == systems_.size()) {
			return std::unexpected(makeDefinitionError(
			    DefinitionError::ScheduleCycle,
			    "System dependency graph contains a cycle"));
		}
		scheduled[selected] = true;
		schedule_.push_back(selected);
		for (const std::size_t dependent : edges[selected])
			--indegrees[dependent];
	}

	auto schema = types_.freeze();
	if (!schema)
		return std::unexpected(schema.error());
	actors_.freeze();
	controllers_.freeze();
	externalCommands_.freeze();
	actions_.freeze();
	conditions_.freeze();
	stages_.freeze();
	patterns_.freeze();
	frozen_ = true;
	return {};
}

} // namespace shiki

#include <shiki/control/controller.h>
#include <shiki/game/world.h>

#include <algorithm>
#include <string>

namespace shiki::control {
namespace {

[[nodiscard]] Error makeControllerError(ControllerError code,
                                        std::string message) {
	return Error{ErrorDomain::Definition, static_cast<std::uint32_t>(code),
	             std::move(message)};
}

} // namespace

void IntentWriter::action(std::uint8_t bit, bool active) noexcept {
	if (bit >= 64)
		return;
	const std::uint64_t mask = std::uint64_t{1} << bit;
	if (active)
		intent_.actions |= mask;
	else
		intent_.actions &= ~mask;
}

Result<ControllerTypeId> ControllerRegistry::add(std::string name,
                                                 Factory factory) {
	if (frozen_) {
		return std::unexpected(makeControllerError(
		    ControllerError::RegistryFrozen,
		    "Controller registry cannot change after definition freeze"));
	}
	if (name.empty()) {
		return std::unexpected(
		    makeControllerError(ControllerError::EmptyTypeName,
		                        "Controller type name cannot be empty"));
	}
	if (!factory) {
		return std::unexpected(
		    makeControllerError(ControllerError::FactoryFailed,
		                        "Controller factory cannot be empty"));
	}
	const ControllerTypeId type{game::typeKeyFromName(name)};
	if (std::ranges::any_of(
	        entries_, [&](const Entry &entry) { return entry.type == type; })) {
		return std::unexpected(makeControllerError(
		    ControllerError::DuplicateType,
		    "Controller type name or persistent key is already registered"));
	}
	entries_.push_back(Entry{std::move(name), type, std::move(factory)});
	return type;
}

Result<std::unique_ptr<Controller>>
ControllerRegistry::create(ControllerTypeId type) const {
	const auto entry = std::ranges::find(entries_, type, &Entry::type);
	if (entry == entries_.end()) {
		return std::unexpected(makeControllerError(
		    ControllerError::UnknownType, "Controller type is not registered"));
	}
	auto controller = entry->factory();
	if (!controller) {
		return std::unexpected(
		    makeControllerError(ControllerError::FactoryFailed,
		                        "Controller factory returned a null instance"));
	}
	return controller;
}

std::vector<std::string> ControllerRegistry::names() const {
	std::vector<std::string> result;
	result.reserve(entries_.size());
	for (const Entry &entry : entries_)
		result.push_back(entry.name);
	return result;
}

void ControllerPool::attach(game::EntityHandle actor,
                            std::unique_ptr<Controller> controller) {
	entries_.push_back(Entry{actor, std::move(controller), {}, false});
}

void ControllerPool::dispatch(const game::WorldView &world,
                              game::Commands &commands,
                              game::ComponentToken<ActorIntent> intentToken,
                              const InputFrame &input) {
	for (auto &entry : entries_) {
		entry.hasIntent = false;
		if (world.state(entry.actor) != game::EntityState::Alive)
			continue;
		IntentWriter writer;
		entry.controller->update(ControlView{entry.actor, input}, writer);
		entry.currentIntent = writer.finish();
		entry.hasIntent = true;
		static_cast<void>(
		    commands.set(entry.actor, intentToken, entry.currentIntent));
	}
	std::erase_if(entries_, [&](const Entry &entry) {
		return world.state(entry.actor) == game::EntityState::Dead;
	});
}

const ActorIntent *
ControllerPool::intent(game::EntityHandle actor) const noexcept {
	const auto entry = std::ranges::find(entries_, actor, &Entry::actor);
	if (entry == entries_.end() || !entry->hasIntent)
		return nullptr;
	return &entry->currentIntent;
}

} // namespace shiki::control

#include <shiki/game/world.h>

#include <algorithm>
#include <atomic>
#include <limits>
#include <string>

namespace shiki::game {
namespace {

std::atomic_uint32_t nextWorldId{1};

[[nodiscard]] Error makeWorldError(WorldError code, std::string message) {
	return Error{ErrorDomain::World, static_cast<std::uint32_t>(code),
	             std::move(message)};
}

[[nodiscard]] std::uint8_t phaseValue(CommitPhase phase) noexcept {
	return static_cast<std::uint8_t>(phase);
}

} // namespace

Commands::Commands(World &world, CommitPhase phase, CommandSource source,
                   std::uint64_t epoch) noexcept
    : world_(&world), phase_(phase), source_(source), epoch_(epoch) {}

Result<EntityHandle> Commands::spawn() {
	if (sequence_ == std::numeric_limits<std::uint32_t>::max()) {
		return std::unexpected(
		    makeWorldError(WorldError::ProducerExhausted,
		                   "Command buffer exhausted its sequence space"));
	}
	return world_->recordSpawn(phase_, source_, epoch_, sequence_++);
}

CommandStatus Commands::destroy(EntityHandle entity) {
	if (sequence_ == std::numeric_limits<std::uint32_t>::max()) {
		return CommandStatus::SequenceLimit;
	}
	return world_->recordDestroy(phase_, source_, epoch_, sequence_++, entity);
}

World::World() : id_{nextWorldId.fetch_add(1, std::memory_order_relaxed)} {
	if (id_.value == 0) {
		id_.value = nextWorldId.fetch_add(1, std::memory_order_relaxed);
	}
}

Result<void> World::beginTick() {
	if (tickOpen_) {
		return std::unexpected(makeWorldError(
		    WorldError::TickAlreadyOpen,
		    "Cannot begin a tick before the current tick is complete"));
	}

	++tick_.value;
	++epoch_;
	currentPhase_ = CommitPhase::Flow;
	tickOpen_ = true;
	openSources_.clear();
	for (auto &buffer : commands_) {
		buffer.clear();
	}
	for (const EntityHandle entity : deferredResolutionSpawns_) {
		const auto entry = entities_.find(key(entity));
		if (entry != entities_.end() && entry->second == EntityState::Pending) {
			entry->second = EntityState::Alive;
		}
	}
	if (!deferredResolutionSpawns_.empty()) {
		++structuralVersion_;
	}
	deferredResolutionSpawns_.clear();
	eventSequence_ = 0;
	events_.beginTick(tick_);
	return {};
}

Result<Commands> World::commands(CommitPhase phase, CommandSource source) {
	if (!tickOpen_) {
		return std::unexpected(
		    makeWorldError(WorldError::TickNotOpen,
		                   "Cannot open commands outside an active tick"));
	}
	if (phase != currentPhase_) {
		return std::unexpected(makeWorldError(
		    WorldError::WrongPhase,
		    "Command phase does not match the current commit phase"));
	}

	const SourceKey sourceKey{source.system, source.partition, source.buffer};
	auto &producer = producers_[source.producer.value];
	if (producer.bound && producer.source != sourceKey) {
		return std::unexpected(makeWorldError(
		    WorldError::ProducerConflict,
		    "ProducerId is already bound to another command source"));
	}
	producer.source = sourceKey;
	producer.bound = true;

	const auto openKey = std::tuple{phaseValue(phase), sourceKey};
	if (!openSources_.insert(openKey).second) {
		return std::unexpected(
		    makeWorldError(WorldError::DuplicateSource,
		                   "Command source is already open for this phase"));
	}
	return Commands{*this, phase, source, epoch_};
}

Result<void> World::commit(CommitPhase phase) {
	if (!tickOpen_) {
		return std::unexpected(makeWorldError(
		    WorldError::TickNotOpen, "Cannot commit outside an active tick"));
	}
	if (phase != currentPhase_) {
		return std::unexpected(makeWorldError(
		    WorldError::WrongPhase,
		    "Commit phase does not match the current World phase"));
	}

	publishEvents(phase);
	auto &buffer = commands_[phaseIndex(phase)];
	std::ranges::sort(buffer, {}, [](const RecordedCommand &command) {
		return std::tuple{command.system, command.partition, command.buffer,
		                  command.sequence};
	});

	std::set<EntityKey> destroyed;
	for (const auto &command : buffer) {
		if (command.kind == CommandKind::Destroy) {
			destroyed.insert(key(command.entity));
		}
	}
	for (const EntityKey &entityKey : destroyed) {
		entities_.erase(entityKey);
		eraseEntityStorage(entityKey);
	}
	for (std::size_t index = 0; index < buffer.size(); ++index) {
		auto &command = buffer[index];
		if (command.state != nullptr) {
			command.state->apply(*this);
			continue;
		}
		if (command.batch != nullptr) {
			command.batch->apply(*this, phase);
			continue;
		}
		const EntityKey entityKey = key(command.entity);
		auto entry = entities_.find(entityKey);
		if (entry == entities_.end()) {
			continue;
		}
		if (command.kind == CommandKind::Spawn) {
			std::size_t mutationEnd = index + 1;
			while (mutationEnd < buffer.size() &&
			       buffer[mutationEnd].mutation != nullptr &&
			       key(buffer[mutationEnd].entity) == entityKey) {
				++mutationEnd;
			}
			std::vector<ComponentMutation *> initialComponents;
			initialComponents.reserve(mutationEnd - index - 1);
			for (std::size_t mutation = index + 1; mutation < mutationEnd;
			     ++mutation) {
				initialComponents.push_back(buffer[mutation].mutation.get());
			}
			insertEntityStorage(entityKey, initialComponents);
			index = mutationEnd - 1;
			if (phase == CommitPhase::Resolution) {
				deferredResolutionSpawns_.push_back(command.entity);
			} else {
				entry->second = EntityState::Alive;
			}
		} else if (command.mutation != nullptr) {
			command.mutation->apply(*this, entityKey);
		}
	}
	buffer.clear();
	++structuralVersion_;

	if (phase == CommitPhase::Flow) {
		currentPhase_ = CommitPhase::Simulation;
	} else if (phase == CommitPhase::Simulation) {
		currentPhase_ = CommitPhase::Resolution;
	} else {
		tickOpen_ = false;
	}
	return {};
}

void World::publishEvents(CommitPhase phase) {
	auto &buffer = commands_[phaseIndex(phase)];
	std::ranges::sort(buffer, {}, [](const RecordedCommand &command) {
		return std::tuple{command.system, command.partition, command.buffer,
		                  command.sequence};
	});
	for (auto &command : buffer) {
		if (command.event != nullptr) {
			command.event->publish(events_,
			                       EventHeader{tick_, eventSequence_++, {}});
		}
	}
	std::erase_if(buffer, [](const RecordedCommand &command) {
		return command.event != nullptr;
	});
}

Result<EntityHandle> World::recordSpawn(CommitPhase phase, CommandSource source,
                                        std::uint64_t epoch,
                                        std::uint32_t sequence) {
	if (!accepts(phase, epoch)) {
		return std::unexpected(makeWorldError(WorldError::WrongPhase,
		                                      "Command buffer has expired"));
	}

	auto entity = reserveEntity(source);
	if (!entity)
		return std::unexpected(entity.error());
	commands_[phaseIndex(phase)].push_back(
	    RecordedCommand{CommandKind::Spawn, *entity, source.system,
	                    source.partition, source.buffer, sequence});
	return entity;
}

Result<EntityHandle> World::reserveEntity(CommandSource source) {
	auto &producer = producers_[source.producer.value];
	if (producer.slot == 0) {
		if (producer.generation == std::numeric_limits<std::uint32_t>::max()) {
			return std::unexpected(
			    makeWorldError(WorldError::ProducerExhausted,
			                   "Producer exhausted its entity identity space"));
		}
		++producer.generation;
		producer.slot = 1;
	}

	const EntityHandle entity{producer.slot++, producer.generation,
	                          source.producer, id_};
	entities_.emplace(key(entity), EntityState::Pending);
	return entity;
}

CommandStatus World::recordDestroy(CommitPhase phase, CommandSource source,
                                   std::uint64_t epoch, std::uint32_t sequence,
                                   EntityHandle entity) {
	if (!accepts(phase, epoch)) {
		return CommandStatus::Expired;
	}
	if (!entity) {
		return CommandStatus::InvalidHandle;
	}
	if (entity.world_ != id_) {
		return CommandStatus::ForeignWorld;
	}
	if (!entities_.contains(key(entity))) {
		return CommandStatus::EntityDead;
	}
	commands_[phaseIndex(phase)].push_back(
	    RecordedCommand{CommandKind::Destroy, entity, source.system,
	                    source.partition, source.buffer, sequence});
	return CommandStatus::Accepted;
}

EntityState World::state(EntityHandle entity) const noexcept {
	if (!entity) {
		return EntityState::Dead;
	}
	if (entity.world_ != id_) {
		return EntityState::Foreign;
	}
	const auto entry = entities_.find(key(entity));
	return entry == entities_.end() ? EntityState::Dead : entry->second;
}

World::EntityKey World::key(EntityHandle entity) noexcept {
	return {entity.producer_, entity.generation_, entity.slot_};
}

std::size_t World::phaseIndex(CommitPhase phase) noexcept {
	return static_cast<std::size_t>(phase);
}

bool World::accepts(CommitPhase phase, std::uint64_t epoch) const noexcept {
	return tickOpen_ && phase == currentPhase_ && epoch == epoch_;
}

presentation::PresentationSnapshot World::buildPresentationSnapshot() const {
	presentation::PresentationSnapshot snapshot;
	snapshot.tick_ = tick_;
	for (const auto &[archetypeKey, archetype] : archetypes_) {
		static_cast<void>(archetypeKey);
		for (const auto &chunk : archetype->chunks) {
			for (std::size_t row = 0; row < chunk->entities.size(); ++row) {
				const EntityKey entityKey = chunk->entities[row];
				const auto state = entities_.find(entityKey);
				if (state == entities_.end() ||
				    state->second != EntityState::Alive)
					continue;
				presentation::PresentationSnapshot::Entity output{
				    EntityHandle{std::get<2>(entityKey), std::get<1>(entityKey),
				                 ProducerId{std::get<0>(entityKey)}, id_},
				    entityKey,
				    {}};
				for (const auto &[key, column] : chunk->columns) {
					const auto &type = *components_.at(key);
					if (!hasFlag(type.flags(), ComponentFlags::Observable))
						continue;
					auto value = type.copyValue(*column, row);
					if (value != nullptr) {
						output.components.emplace_back(presentation::PresentationSnapshot::Value{
						             type.typeTag(), std::move(value)});
					}
				}
				if (!output.components.empty())
					snapshot.entities_.push_back(std::move(output));
			}
		}
	}
	return snapshot;
}

World::Archetype &World::getOrCreateArchetype(ArchetypeKey key) {
	const auto existing = archetypes_.find(key);
	if (existing != archetypes_.end())
		return *existing->second;

	auto archetype = std::make_unique<Archetype>();
	archetype->key = std::move(key);
	auto *result = archetype.get();
	archetypes_.emplace(result->key, std::move(archetype));
	return *result;
}

World::Chunk &World::getWritableChunk(Archetype &archetype) {
	for (const auto &chunk : archetype.chunks) {
		if (chunk->entities.size() < chunkCapacity)
			return *chunk;
	}

	auto chunk = std::make_unique<Chunk>();
	for (const TypeKey component : archetype.key)
		chunk->columns.emplace(component,
		                       components_.at(component)->createColumn());
	auto *result = chunk.get();
	archetype.chunks.push_back(std::move(chunk));
	return *result;
}

void World::insertEntityStorage(
    EntityKey entity, std::span<ComponentMutation *const> initialComponents) {
	if (locations_.contains(entity))
		return;

	std::vector<std::pair<TypeKey, ComponentMutation *>> finalComponents;
	finalComponents.reserve(initialComponents.size());
	for (ComponentMutation *mutation : initialComponents) {
		const auto position = std::ranges::lower_bound(
		    finalComponents, mutation->componentKey(), {},
		    [](const auto &entry) { return entry.first; });
		if (mutation->isRemoval()) {
			if (position != finalComponents.end() &&
			    position->first == mutation->componentKey())
				finalComponents.erase(position);
			continue;
		}
		if (position != finalComponents.end() &&
		    position->first == mutation->componentKey()) {
			position->second = mutation;
		} else {
			finalComponents.insert(position,
			                       {mutation->componentKey(), mutation});
		}
	}

	ArchetypeKey key;
	key.reserve(finalComponents.size());
	for (const auto &[component, mutation] : finalComponents) {
		static_cast<void>(mutation);
		key.push_back(component);
	}
	Archetype &archetype = getOrCreateArchetype(std::move(key));
	Chunk &chunk = getWritableChunk(archetype);
	const std::size_t row = chunk.entities.size();
	chunk.entities.push_back(entity);
	for (const auto &[component, mutation] : finalComponents)
		mutation->appendTo(*chunk.columns.at(component));
	locations_.emplace(entity, EntityLocation{&archetype, &chunk, row});
}

void World::eraseEntityStorage(EntityKey entity) {
	const auto location = locations_.find(entity);
	if (location == locations_.end())
		return;
	Chunk *chunk = location->second.chunk;
	const std::size_t row = location->second.row;
	eraseChunkRow(*chunk, row);
	locations_.erase(entity);
}

void World::eraseChunkRow(Chunk &chunk, std::size_t row) {
	const std::size_t lastRow = chunk.entities.size() - 1;
	if (row != lastRow) {
		const EntityKey movedEntity = chunk.entities[lastRow];
		chunk.entities[row] = movedEntity;
		locations_.at(movedEntity).row = row;
	}
	for (auto &[key, column] : chunk.columns) {
		static_cast<void>(key);
		column->eraseSwap(row);
	}
	chunk.entities.pop_back();
}

void World::removeComponentStorage(EntityKey entity, TypeKey component) {
	const auto locationEntry = locations_.find(entity);
	if (locationEntry == locations_.end())
		return;

	EntityLocation location = locationEntry->second;
	if (!location.chunk->columns.contains(component))
		return;

	ArchetypeKey targetKey = location.archetype->key;
	const auto removed = std::ranges::lower_bound(targetKey, component);
	targetKey.erase(removed);
	Archetype &targetArchetype = getOrCreateArchetype(std::move(targetKey));
	Chunk &targetChunk = getWritableChunk(targetArchetype);
	const std::size_t targetRow = targetChunk.entities.size();
	targetChunk.entities.push_back(entity);
	for (const TypeKey retained : targetArchetype.key) {
		targetChunk.columns.at(retained)->moveAppendFrom(
		    *location.chunk->columns.at(retained), location.row);
	}
	locations_.insert_or_assign(
	    entity, EntityLocation{&targetArchetype, &targetChunk, targetRow});
	eraseChunkRow(*location.chunk, location.row);
}

EntityState WorldView::state(EntityHandle entity) const noexcept {
	return world_->state(entity);
}

std::size_t WorldView::aliveCount() const noexcept {
	return static_cast<std::size_t>(
	    std::ranges::count_if(world_->entities_, [](const auto &entry) {
		    return entry.second == EntityState::Alive;
	    }));
}

std::size_t WorldView::pendingCount() const noexcept {
	return static_cast<std::size_t>(
	    std::ranges::count_if(world_->entities_, [](const auto &entry) {
		    return entry.second == EntityState::Pending;
	    }));
}

} // namespace shiki::game

#pragma once

#include <shiki/core/time.h>
#include <shiki/game/commands.h>
#include <shiki/game/event_stream.h>
#include <shiki/game/query.h>
#include <shiki/game/world_view.h>
#include <shiki/presentation/presentation_snapshot.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace shiki {
class Session;
}

namespace shiki::game {

/** Stable errors produced by the initial World lifecycle implementation. */
enum class WorldError : std::uint32_t {
	TickAlreadyOpen = 1, ///< beginTick was called before Resolution commit.
	TickNotOpen,         ///< Commands or commit were requested outside a tick.
	WrongPhase,          ///< A phase was opened or committed out of order.
	DuplicateSource,     ///< A command source was opened twice in one phase.
	ProducerConflict,    ///< One ProducerId was bound to different source data.
	ProducerExhausted,   ///< A producer exhausted its stable identity space.
	ComponentsLocked,    ///< Component registration was attempted after a tick
	                     ///< began.
	ComponentConflict    ///< A component key belongs to another C++ type.
};

/** Owns entity identities and deterministic structural command buffers. */
class World final {
  public:
	World();
	~World() = default;

	World(const World &) = delete;
	World &operator=(const World &) = delete;
	World(World &&) = delete;
	World &operator=(World &&) = delete;

	/** Starts the next tick and activates deferred Resolution spawns. */
	[[nodiscard]] Result<void> beginTick();

	/** Opens one deterministic command buffer for the current phase. */
	[[nodiscard]] Result<Commands> commands(CommitPhase phase,
	                                        CommandSource source);

	/** Applies the current phase and advances to the next commit point. */
	[[nodiscard]] Result<void> commit(CommitPhase phase);

	/** Registers a component before the first tick begins. */
	template <Component T>
	[[nodiscard]] Result<ComponentToken<T>>
	registerComponent(ComponentDescriptor<T> descriptor);

	/** Registers an event type before the first tick begins. */
	template <Event T>
	[[nodiscard]] Result<EventToken<T>>
	registerEvent(EventDescriptor<T> descriptor) {
		return events_.registerEvent<T>(std::move(descriptor));
	}

	/** Registers one typed Session State entry before the first tick begins. */
	template <State T>
	[[nodiscard]] Result<StateKey<T>>
	registerState(StateDescriptor<T> descriptor);

	/** Returns immutable retained events published by this World. */
	[[nodiscard]] const EventStream &events() const noexcept { return events_; }

	/** Configures retained event history before the first tick begins. */
	[[nodiscard]] Result<void> setEventRetentionTicks(std::size_t ticks) {
		return events_.setRetentionTicks(ticks);
	}

	/** Returns a read-only lifecycle view. */
	[[nodiscard]] WorldView view() const noexcept { return WorldView{*this}; }

	/** Copies observable state for presentation after a completed tick. */
	[[nodiscard]] presentation::PresentationSnapshot
	buildPresentationSnapshot() const;

	/** Returns the process-local identity of this World. */
	[[nodiscard]] WorldId id() const noexcept { return id_; }

	/** Returns the currently open simulation tick. */
	[[nodiscard]] Tick tick() const noexcept { return tick_; }

  private:
	using EntityKey = std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>;
	using SourceKey = std::tuple<std::uint16_t, std::uint16_t, std::uint16_t>;
	struct EntityKeyHash final {
		[[nodiscard]] std::size_t operator()(const EntityKey &key) const noexcept {
			std::size_t seed = std::hash<std::uint32_t>{}(std::get<0>(key));
			seed ^= std::hash<std::uint32_t>{}(std::get<1>(key)) +
			        0x9e3779b9U + (seed << 6U) + (seed >> 2U);
			seed ^= std::hash<std::uint32_t>{}(std::get<2>(key)) +
			        0x9e3779b9U + (seed << 6U) + (seed >> 2U);
			return seed;
		}
	};
	using ArchetypeKey = std::vector<TypeKey>;
	static constexpr std::size_t chunkCapacity = 256;

	struct ProducerState final {
		std::uint32_t slot{1};
		std::uint32_t generation{1};
		SourceKey source{};
		bool bound{};
	};

	enum class CommandKind : std::uint8_t {
		Spawn,
		SpawnBatch,
		Destroy,
		Mutation,
		Event,
		State
	};

	class ComponentColumn;
	class SpawnBatchCommand;
	class EventEmission;
	class StateMutation;

	class EventEmission {
	  public:
		virtual ~EventEmission() = default;
		virtual void publish(EventStream &stream, EventHeader header) = 0;
	};

	template <Event T> class TypedEventEmission final : public EventEmission {
	  public:
		TypedEventEmission(EventToken<T> token, T value)
		    : token_(token), value_(std::move(value)) {}
		void publish(EventStream &stream, EventHeader header) override {
			stream.publish(token_, header, std::move(value_));
		}

	  private:
		EventToken<T> token_;
		T value_;
	};

	class ComponentMutation {
	  public:
		virtual ~ComponentMutation() = default;
		virtual void apply(World &world, EntityKey entity) = 0;
		[[nodiscard]] virtual TypeKey componentKey() const noexcept = 0;
		[[nodiscard]] virtual bool isRemoval() const noexcept = 0;
		virtual void appendTo(ComponentColumn &column) = 0;
	};

	class StateMutation {
	  public:
		virtual ~StateMutation() = default;
		virtual void apply(World &world) = 0;
	};

	class StateValue {
	  public:
		StateValue(std::string name, std::uint32_t version, StateFlags flags,
		           std::string codec)
		    : name_(std::move(name)), version_(version), flags_(flags),
		      codec_(std::move(codec)) {}
		virtual ~StateValue() = default;
		[[nodiscard]] virtual const void *typeTag() const noexcept = 0;
		[[nodiscard]] virtual const void *value() const noexcept = 0;
		[[nodiscard]] bool matches(std::string_view name, std::uint32_t version,
		                           StateFlags flags,
		                           std::string_view codec) const noexcept {
			return name_ == name && version_ == version && flags_ == flags &&
			       codec_ == codec;
		}

	  private:
		std::string name_;
		std::uint32_t version_{};
		StateFlags flags_{};
		std::string codec_;
	};

	template <State T> class TypedStateValue final : public StateValue {
	  public:
		explicit TypedStateValue(StateDescriptor<T> descriptor)
		    : StateValue(std::move(descriptor.name), descriptor.version,
		                 descriptor.flags, descriptor.codec.identity),
		      value_(std::move(descriptor.initialValue)),
		      codec_(std::move(descriptor.codec)) {}
		[[nodiscard]] const void *typeTag() const noexcept override {
			return &stateTypeTag<T>;
		}
		[[nodiscard]] const void *value() const noexcept override {
			return &value_;
		}
		void set(T value) { value_ = std::move(value); }

	  private:
		T value_;
		StateCodec<T> codec_;
	};

	template <State T> class SetStateMutation final : public StateMutation {
	  public:
		SetStateMutation(StateKey<T> key, T value)
		    : key_(key), value_(std::move(value)) {}
		void apply(World &world) override {
			auto *state = static_cast<TypedStateValue<T> *>(
			    world.states_.at(key_.key()).get());
			state->set(std::move(value_));
		}

	  private:
		StateKey<T> key_;
		T value_;
	};

	class SpawnBatchCommand {
	  public:
		virtual ~SpawnBatchCommand() = default;
		virtual void apply(World &world, CommitPhase phase) = 0;
	};

	class ComponentColumn {
	  public:
		virtual ~ComponentColumn() = default;
		virtual void moveAppendFrom(ComponentColumn &source,
		                            std::size_t row) = 0;
		virtual void eraseSwap(std::size_t row) = 0;
		[[nodiscard]] virtual const void *
		value(std::size_t row) const noexcept = 0;
	};

	template <Component T>
	class TypedComponentColumn final : public ComponentColumn {
	  public:
		void moveAppendFrom(ComponentColumn &source, std::size_t row) override {
			auto &typedSource = static_cast<TypedComponentColumn<T> &>(source);
			values_.push_back(std::move(typedSource.values_[row]));
		}

		void eraseSwap(std::size_t row) override {
			if (row + 1 != values_.size())
				values_[row] = std::move(values_.back());
			values_.pop_back();
		}

		[[nodiscard]] const void *
		value(std::size_t row) const noexcept override {
			return &values_[row];
		}

		void append(T value) { values_.push_back(std::move(value)); }

		void set(std::size_t row, T value) { values_[row] = std::move(value); }

	  private:
		std::vector<T> values_;
	};

	class ComponentType {
	  public:
		ComponentType(std::string name, std::uint32_t version,
		              ComponentFlags flags)
		    : name_(std::move(name)), version_(version), flags_(flags) {}
		virtual ~ComponentType() = default;
		[[nodiscard]] virtual const void *typeTag() const noexcept = 0;
		[[nodiscard]] virtual std::unique_ptr<ComponentColumn>
		createColumn() const = 0;
		[[nodiscard]] virtual std::shared_ptr<const void>
		copyValue(const ComponentColumn &column, std::size_t row) const = 0;

		[[nodiscard]] bool matches(std::string_view name, std::uint32_t version,
		                           ComponentFlags flags) const noexcept {
			return name_ == name && version_ == version && flags_ == flags;
		}
		[[nodiscard]] ComponentFlags flags() const noexcept { return flags_; }

	  private:
		std::string name_;
		std::uint32_t version_{};
		ComponentFlags flags_{};
	};

	template <Component T>
	class TypedComponentType final : public ComponentType {
	  public:
		explicit TypedComponentType(ComponentDescriptor<T> descriptor)
		    : ComponentType(std::move(descriptor.name), descriptor.version,
		                    descriptor.flags) {}

		[[nodiscard]] const void *typeTag() const noexcept override {
			return &componentTypeTag<T>;
		}

		[[nodiscard]] std::unique_ptr<ComponentColumn>
		createColumn() const override {
			return std::make_unique<TypedComponentColumn<T>>();
		}

		[[nodiscard]] std::shared_ptr<const void>
		copyValue(const ComponentColumn &column,
		          std::size_t row) const override {
			if constexpr (std::copy_constructible<T>) {
				const auto &typed =
				    static_cast<const TypedComponentColumn<T> &>(column);
				return std::make_shared<const T>(
				    *static_cast<const T *>(typed.value(row)));
			} else {
				return {};
			}
		}
	};

	struct Chunk final {
		std::vector<EntityKey> entities;
		std::map<TypeKey, std::unique_ptr<ComponentColumn>> columns;
	};

	struct Archetype final {
		ArchetypeKey key;
		std::vector<std::unique_ptr<Chunk>> chunks;
	};

	struct EntityLocation final {
		Archetype *archetype{};
		Chunk *chunk{};
		std::size_t row{};
	};

	template <Component T>
	class SetComponentMutation final : public ComponentMutation {
	  public:
		SetComponentMutation(ComponentToken<T> token, T value)
		    : token_(token), value_(std::move(value)) {}

		void apply(World &world, EntityKey entity) override {
			world.setComponent(entity, token_, std::move(value_));
		}

		[[nodiscard]] TypeKey componentKey() const noexcept override {
			return token_.key();
		}

		[[nodiscard]] bool isRemoval() const noexcept override { return false; }

		void appendTo(ComponentColumn &column) override {
			auto &typedColumn = static_cast<TypedComponentColumn<T> &>(column);
			typedColumn.append(std::move(value_));
		}

	  private:
		ComponentToken<T> token_;
		T value_;
	};

	template <Component T>
	class RemoveComponentMutation final : public ComponentMutation {
	  public:
		explicit RemoveComponentMutation(ComponentToken<T> token)
		    : token_(token) {}

		void apply(World &world, EntityKey entity) override {
			world.removeComponent(entity, token_);
		}

		[[nodiscard]] TypeKey componentKey() const noexcept override {
			return token_.key();
		}

		[[nodiscard]] bool isRemoval() const noexcept override { return true; }

		void appendTo(ComponentColumn &column) override {
			static_cast<void>(column);
		}

	  private:
		ComponentToken<T> token_;
	};

	template <Component... Components>
	class TypedSpawnBatchCommand final : public SpawnBatchCommand {
	  public:
		TypedSpawnBatchCommand(std::tuple<ComponentToken<Components>...> tokens,
		                       std::vector<std::tuple<Components...>> records,
		                       std::vector<EntityHandle> entities)
		    : tokens_(std::move(tokens)), records_(std::move(records)),
		      entities_(std::move(entities)) {}

		void apply(World &world, CommitPhase phase) override;

	  private:
		std::tuple<ComponentToken<Components>...> tokens_;
		std::vector<std::tuple<Components...>> records_;
		std::vector<EntityHandle> entities_;
	};

	struct RecordedCommand final {
		CommandKind kind{};
		EntityHandle entity{};
		std::uint16_t system{};
		std::uint16_t partition{};
		std::uint16_t buffer{};
		std::uint32_t sequence{};
		std::unique_ptr<ComponentMutation> mutation;
		std::unique_ptr<SpawnBatchCommand> batch;
		std::unique_ptr<EventEmission> event;
		std::unique_ptr<StateMutation> state;
	};

	[[nodiscard]] Result<EntityHandle> recordSpawn(CommitPhase phase,
	                                               CommandSource source,
	                                               std::uint64_t epoch,
	                                               std::uint32_t sequence);
	template <Component... Components>
	[[nodiscard]] Result<std::size_t>
	recordSpawnBatch(CommitPhase phase, CommandSource source,
	                 std::uint64_t epoch, std::uint32_t sequence,
	                 std::tuple<ComponentToken<Components>...> tokens,
	                 std::vector<std::tuple<Components...>> records);
	[[nodiscard]] Result<EntityHandle> reserveEntity(CommandSource source);
	[[nodiscard]] CommandStatus
	recordDestroy(CommitPhase phase, CommandSource source, std::uint64_t epoch,
	              std::uint32_t sequence, EntityHandle entity);
	template <State T>
	[[nodiscard]] CommandStatus
	recordState(CommitPhase phase, CommandSource source, std::uint64_t epoch,
	            std::uint32_t sequence, StateKey<T> key, T value);
	[[nodiscard]] EntityState state(EntityHandle entity) const noexcept;
	[[nodiscard]] static EntityKey key(EntityHandle entity) noexcept;
	[[nodiscard]] static std::size_t phaseIndex(CommitPhase phase) noexcept;
	[[nodiscard]] bool accepts(CommitPhase phase,
	                           std::uint64_t epoch) const noexcept;
	void publishEvents(CommitPhase phase);

	template <Component T>
	[[nodiscard]] CommandStatus
	recordSet(CommitPhase phase, CommandSource source, std::uint64_t epoch,
	          std::uint32_t sequence, EntityHandle entity,
	          ComponentToken<T> token, T value);

	template <Component T>
	[[nodiscard]] CommandStatus
	recordRemove(CommitPhase phase, CommandSource source, std::uint64_t epoch,
	             std::uint32_t sequence, EntityHandle entity,
	             ComponentToken<T> token);

	template <Event T>
	[[nodiscard]] CommandStatus
	recordEvent(CommitPhase phase, CommandSource source, std::uint64_t epoch,
	            std::uint32_t sequence, EventToken<T> token, T value);

	template <Component T>
	[[nodiscard]] bool validateToken(ComponentToken<T> token) const noexcept;
	template <State T>
	[[nodiscard]] bool validateState(StateKey<T> key) const noexcept;
	template <State T>
	[[nodiscard]] const T *tryGetState(StateKey<T> key) const noexcept;

	template <Component T>
	void setComponent(EntityKey entity, ComponentToken<T> token, T value);

	template <Component T>
	void removeComponent(EntityKey entity, ComponentToken<T> token);

	template <Component T>
	[[nodiscard]] const T *
	tryGetComponent(EntityHandle entity,
	                ComponentToken<T> token) const noexcept;

	[[nodiscard]] Archetype &getOrCreateArchetype(ArchetypeKey key);
	[[nodiscard]] Chunk &getWritableChunk(Archetype &archetype);
	void insertEntityStorage(
	    EntityKey entity,
	    std::span<ComponentMutation *const> initialComponents = {});
	template <Component... Components>
	void
	insertBatchEntity(EntityKey entity, Archetype &archetype,
	                  const std::tuple<ComponentToken<Components>...> &tokens,
	                  std::tuple<Components...> values);
	void eraseEntityStorage(EntityKey entity);
	void eraseChunkRow(Chunk &chunk, std::size_t row);
	void removeComponentStorage(EntityKey entity, TypeKey component);

	WorldId id_{};
	Tick tick_{};
	CommitPhase currentPhase_{CommitPhase::Flow};
	std::uint64_t epoch_{};
	bool tickOpen_{};
	std::uint64_t structuralVersion_{};
	std::unordered_map<EntityKey, EntityState, EntityKeyHash> entities_;
	std::map<TypeKey, std::unique_ptr<ComponentType>> components_;
	std::map<TypeKey, std::unique_ptr<StateValue>> states_;
	std::map<ArchetypeKey, std::unique_ptr<Archetype>> archetypes_;
	std::unordered_map<EntityKey, EntityLocation, EntityKeyHash> locations_;
	std::map<std::uint32_t, ProducerState> producers_;
	std::array<std::vector<RecordedCommand>, 3> commands_;
	std::set<std::tuple<std::uint8_t, SourceKey>> openSources_;
	std::vector<EntityHandle> deferredResolutionSpawns_;
	EventStream events_;
	std::uint32_t eventSequence_{};

	friend class Commands;
	friend class WorldView;
	friend class ::shiki::Session;
	template <Component... Components> friend class QueryView;
};

template <Component T>
Result<ComponentToken<T>>
World::registerComponent(ComponentDescriptor<T> descriptor) {
	if (tickOpen_ || tick_.value != 0) {
		return std::unexpected(
		    Error{ErrorDomain::World,
		          static_cast<std::uint32_t>(WorldError::ComponentsLocked),
		          "Components must be registered before the first tick"});
	}
	if (descriptor.name.empty()) {
		return std::unexpected(
		    Error{ErrorDomain::World,
		          static_cast<std::uint32_t>(WorldError::ComponentConflict),
		          "Component name cannot be empty"});
	}
	if constexpr (!std::copy_constructible<T>) {
		if (hasFlag(descriptor.flags, ComponentFlags::Observable)) {
			return std::unexpected(
			    Error{ErrorDomain::Definition,
			          static_cast<std::uint32_t>(WorldError::ComponentConflict),
			          "Observable components must be copy constructible"});
		}
	}

	const TypeKey key = typeKeyFromName(descriptor.name);
	const auto existing = components_.find(key);
	if (existing != components_.end()) {
		if (existing->second->typeTag() == &componentTypeTag<T> &&
		    existing->second->matches(descriptor.name, descriptor.version,
		                              descriptor.flags)) {
			return ComponentToken<T>{key, &componentTypeTag<T>};
		}
		return std::unexpected(
		    Error{ErrorDomain::World,
		          static_cast<std::uint32_t>(WorldError::ComponentConflict),
		          "Component key is already registered with another C++ type"});
	}
	components_.emplace(
	    key, std::make_unique<TypedComponentType<T>>(std::move(descriptor)));
	return ComponentToken<T>{key, &componentTypeTag<T>};
}

template <State T>
Result<StateKey<T>> World::registerState(StateDescriptor<T> descriptor) {
	if (tickOpen_ || tick_.value != 0)
		return std::unexpected(
		    Error{ErrorDomain::World,
		          static_cast<std::uint32_t>(StateError::RegistryLocked),
		          "State must be registered before the first tick"});
	if (descriptor.name.empty())
		return std::unexpected(
		    Error{ErrorDomain::Definition,
		          static_cast<std::uint32_t>(StateError::EmptyName),
		          "State name cannot be empty"});
	if (descriptor.codec.identity.empty() || !descriptor.codec.encode ||
	    !descriptor.codec.decode)
		return std::unexpected(
		    Error{ErrorDomain::Definition,
		          static_cast<std::uint32_t>(StateError::InvalidCodec),
		          "State codec must have an identity, encoder, and decoder"});
	const TypeKey key = typeKeyFromName(descriptor.name);
	const auto existing = states_.find(key);
	if (existing != states_.end()) {
		if (existing->second->typeTag() == &stateTypeTag<T> &&
		    existing->second->matches(descriptor.name, descriptor.version,
		                              descriptor.flags,
		                              descriptor.codec.identity))
			return StateKey<T>{key, &stateTypeTag<T>};
		return std::unexpected(
		    Error{ErrorDomain::Definition,
		          static_cast<std::uint32_t>(StateError::DuplicateType),
		          "State key has incompatible type or metadata"});
	}
	states_.emplace(
	    key, std::make_unique<TypedStateValue<T>>(std::move(descriptor)));
	return StateKey<T>{key, &stateTypeTag<T>};
}

template <Component T>
CommandStatus Commands::set(EntityHandle entity, ComponentToken<T> token,
                            T value) {
	if (sequence_ == std::numeric_limits<std::uint32_t>::max()) {
		return CommandStatus::SequenceLimit;
	}
	return world_->recordSet(phase_, source_, epoch_, sequence_++, entity,
	                         token, std::move(value));
}

template <Component... Components>
Result<std::size_t>
Commands::spawnBatch(std::tuple<ComponentToken<Components>...> tokens,
                     std::vector<std::tuple<Components...>> records) {
	if (sequence_ == std::numeric_limits<std::uint32_t>::max()) {
		return std::unexpected(
		    Error{ErrorDomain::World,
		          static_cast<std::uint32_t>(WorldError::ProducerExhausted),
		          "Command buffer exhausted its sequence space"});
	}
	return world_->recordSpawnBatch(phase_, source_, epoch_, sequence_++,
	                                std::move(tokens), std::move(records));
}

template <Component T>
CommandStatus Commands::remove(EntityHandle entity, ComponentToken<T> token) {
	if (sequence_ == std::numeric_limits<std::uint32_t>::max()) {
		return CommandStatus::SequenceLimit;
	}
	return world_->recordRemove(phase_, source_, epoch_, sequence_++, entity,
	                            token);
}

template <Event T> CommandStatus Commands::emit(EventToken<T> token, T value) {
	if (sequence_ == std::numeric_limits<std::uint32_t>::max())
		return CommandStatus::SequenceLimit;
	return world_->recordEvent(phase_, source_, epoch_, sequence_++, token,
	                           std::move(value));
}

template <State T> CommandStatus Commands::set(StateKey<T> key, T value) {
	if (sequence_ == std::numeric_limits<std::uint32_t>::max())
		return CommandStatus::SequenceLimit;
	return world_->recordState(phase_, source_, epoch_, sequence_++, key,
	                           std::move(value));
}

template <State T>
CommandStatus World::recordState(CommitPhase phase, CommandSource source,
                                 std::uint64_t epoch, std::uint32_t sequence,
                                 StateKey<T> key, T value) {
	if (!accepts(phase, epoch))
		return CommandStatus::Expired;
	if (!validateState(key))
		return CommandStatus::InvalidState;
	RecordedCommand command{CommandKind::State, {},
	                        source.system,      source.partition,
	                        source.buffer,      sequence};
	command.state =
	    std::make_unique<SetStateMutation<T>>(key, std::move(value));
	commands_[phaseIndex(phase)].push_back(std::move(command));
	return CommandStatus::Accepted;
}

template <Event T>
CommandStatus World::recordEvent(CommitPhase phase, CommandSource source,
                                 std::uint64_t epoch, std::uint32_t sequence,
                                 EventToken<T> token, T value) {
	if (!accepts(phase, epoch))
		return CommandStatus::Expired;
	const auto *type = events_.find(token.key_);
	if (!token || type == nullptr || type->typeTag != token.typeTag_)
		return CommandStatus::InvalidComponent;
	commands_[phaseIndex(phase)].push_back(RecordedCommand{
	    CommandKind::Event,
	    {},
	    source.system,
	    source.partition,
	    source.buffer,
	    sequence,
	    nullptr,
	    nullptr,
	    std::make_unique<TypedEventEmission<T>>(token, std::move(value))});
	return CommandStatus::Accepted;
}

template <Component T>
CommandStatus World::recordSet(CommitPhase phase, CommandSource source,
                               std::uint64_t epoch, std::uint32_t sequence,
                               EntityHandle entity, ComponentToken<T> token,
                               T value) {
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
	if (!validateToken(token)) {
		return CommandStatus::InvalidComponent;
	}
	commands_[phaseIndex(phase)].push_back(RecordedCommand{
	    CommandKind::Mutation, entity, source.system, source.partition,
	    source.buffer, sequence,
	    std::make_unique<SetComponentMutation<T>>(token, std::move(value))});
	return CommandStatus::Accepted;
}

template <Component T>
CommandStatus World::recordRemove(CommitPhase phase, CommandSource source,
                                  std::uint64_t epoch, std::uint32_t sequence,
                                  EntityHandle entity,
                                  ComponentToken<T> token) {
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
	if (!validateToken(token)) {
		return CommandStatus::InvalidComponent;
	}
	commands_[phaseIndex(phase)].push_back(
	    RecordedCommand{CommandKind::Mutation, entity, source.system,
	                    source.partition, source.buffer, sequence,
	                    std::make_unique<RemoveComponentMutation<T>>(token)});
	return CommandStatus::Accepted;
}

template <Component... Components>
Result<std::size_t>
World::recordSpawnBatch(CommitPhase phase, CommandSource source,
                        std::uint64_t epoch, std::uint32_t sequence,
                        std::tuple<ComponentToken<Components>...> tokens,
                        std::vector<std::tuple<Components...>> records) {
	if (!accepts(phase, epoch)) {
		return std::unexpected(
		    Error{ErrorDomain::World,
		          static_cast<std::uint32_t>(WorldError::WrongPhase),
		          "Command buffer has expired"});
	}
	if (!(validateToken(std::get<ComponentToken<Components>>(tokens)) && ...)) {
		return std::unexpected(
		    Error{ErrorDomain::World,
		          static_cast<std::uint32_t>(WorldError::ComponentConflict),
		          "Spawn batch contains an invalid component token"});
	}
	std::array<TypeKey, sizeof...(Components)> keys{
	    std::get<ComponentToken<Components>>(tokens).key()...};
	std::ranges::sort(keys);
	if (std::ranges::adjacent_find(keys) != keys.end()) {
		return std::unexpected(
		    Error{ErrorDomain::World,
		          static_cast<std::uint32_t>(WorldError::ComponentConflict),
		          "Spawn batch contains duplicate component tokens"});
	}

	std::vector<EntityHandle> entities;
	entities.reserve(records.size());
	for (std::size_t index = 0; index < records.size(); ++index) {
		auto entity = reserveEntity(source);
		if (!entity) {
			for (const EntityHandle reserved : entities)
				entities_.erase(key(reserved));
			return std::unexpected(entity.error());
		}
		entities.push_back(*entity);
	}
	const std::size_t count = records.size();
	commands_[phaseIndex(phase)].push_back(RecordedCommand{
	    CommandKind::SpawnBatch,
	    {},
	    source.system,
	    source.partition,
	    source.buffer,
	    sequence,
	    nullptr,
	    std::make_unique<TypedSpawnBatchCommand<Components...>>(
	        std::move(tokens), std::move(records), std::move(entities))});
	return count;
}

template <Component T>
bool World::validateToken(ComponentToken<T> token) const noexcept {
	const auto component = components_.find(token.key_);
	return token && component != components_.end() &&
	       component->second->typeTag() == token.typeTag_;
}

template <State T> bool World::validateState(StateKey<T> key) const noexcept {
	const auto state = states_.find(key.key_);
	return key && state != states_.end() &&
	       state->second->typeTag() == key.typeTag_;
}

template <State T> const T *World::tryGetState(StateKey<T> key) const noexcept {
	if (!validateState(key))
		return nullptr;
	return static_cast<const T *>(states_.at(key.key_)->value());
}

template <State T> const T *WorldView::tryGet(StateKey<T> key) const noexcept {
	return world_->tryGetState(key);
}

template <Component... Components>
void World::TypedSpawnBatchCommand<Components...>::apply(World &world,
                                                         CommitPhase phase) {
	ArchetypeKey key{std::get<ComponentToken<Components>>(tokens_).key()...};
	std::ranges::sort(key);
	Archetype &archetype = world.getOrCreateArchetype(std::move(key));
	for (std::size_t index = 0; index < entities_.size(); ++index) {
		const EntityKey entityKey = World::key(entities_[index]);
		auto state = world.entities_.find(entityKey);
		if (state == world.entities_.end())
			continue;
		world.insertBatchEntity(entityKey, archetype, tokens_,
		                        std::move(records_[index]));
		if (phase == CommitPhase::Resolution) {
			world.deferredResolutionSpawns_.push_back(entities_[index]);
		} else {
			state->second = EntityState::Alive;
		}
	}
}

template <Component... Components>
void World::insertBatchEntity(
    EntityKey entity, Archetype &archetype,
    const std::tuple<ComponentToken<Components>...> &tokens,
    std::tuple<Components...> values) {
	Chunk &chunk = getWritableChunk(archetype);
	const std::size_t row = chunk.entities.size();
	chunk.entities.push_back(entity);
	[&]<std::size_t... Indices>(std::index_sequence<Indices...>) {
		(static_cast<TypedComponentColumn<
		     std::tuple_element_t<Indices, std::tuple<Components...>>> *>(
		     chunk.columns.at(std::get<Indices>(tokens).key()).get())
		     ->append(std::move(std::get<Indices>(values))),
		 ...);
	}(std::index_sequence_for<Components...>{});
	locations_.emplace(entity, EntityLocation{&archetype, &chunk, row});
}

template <Component T>
void World::setComponent(EntityKey entity, ComponentToken<T> token, T value) {
	const auto locationEntry = locations_.find(entity);
	if (locationEntry == locations_.end())
		return;

	EntityLocation location = locationEntry->second;
	const auto existing = location.chunk->columns.find(token.key_);
	if (existing != location.chunk->columns.end()) {
		auto *column =
		    static_cast<TypedComponentColumn<T> *>(existing->second.get());
		column->set(location.row, std::move(value));
		return;
	}

	ArchetypeKey targetKey = location.archetype->key;
	targetKey.insert(std::ranges::lower_bound(targetKey, token.key_),
	                 token.key_);
	Archetype &targetArchetype = getOrCreateArchetype(std::move(targetKey));
	Chunk &targetChunk = getWritableChunk(targetArchetype);
	const std::size_t targetRow = targetChunk.entities.size();
	targetChunk.entities.push_back(entity);
	for (const TypeKey component : targetArchetype.key) {
		auto &targetColumn = *targetChunk.columns.at(component);
		if (component == token.key_) {
			auto &typedColumn =
			    static_cast<TypedComponentColumn<T> &>(targetColumn);
			typedColumn.append(std::move(value));
		} else {
			targetColumn.moveAppendFrom(*location.chunk->columns.at(component),
			                            location.row);
		}
	}
	locations_.insert_or_assign(
	    entity, EntityLocation{&targetArchetype, &targetChunk, targetRow});
	eraseChunkRow(*location.chunk, location.row);
}

template <Component T>
void World::removeComponent(EntityKey entity, ComponentToken<T> token) {
	removeComponentStorage(entity, token.key_);
}

template <Component T>
const T *World::tryGetComponent(EntityHandle entity,
                                ComponentToken<T> token) const noexcept {
	if (state(entity) != EntityState::Alive || !validateToken(token)) {
		return nullptr;
	}
	const auto location = locations_.find(key(entity));
	if (location == locations_.end())
		return nullptr;
	const auto column = location->second.chunk->columns.find(token.key_);
	if (column == location->second.chunk->columns.end())
		return nullptr;
	return static_cast<const T *>(column->second->value(location->second.row));
}

template <Component T>
const T *WorldView::tryGet(EntityHandle entity,
                           ComponentToken<T> token) const noexcept {
	return world_->tryGetComponent(entity, token);
}

template <Component T>
bool WorldView::contains(EntityHandle entity,
                         ComponentToken<T> token) const noexcept {
	return tryGet(entity, token) != nullptr;
}

template <Component... Components>
QueryView<Components...>
WorldView::query(ComponentToken<Components>... tokens) const {
	return query(QueryOrder::Storage, tokens...);
}

template <Component... Components>
QueryView<Components...>
WorldView::query(QueryOrder order, ComponentToken<Components>... tokens) const {
	std::vector<EntityHandle> entities;
	const std::array<TypeKey, sizeof...(Components)> requested{tokens.key()...};
	for (const auto &[archetypeKey, archetype] : world_->archetypes_) {
		const bool matches = std::ranges::all_of(requested, [&](TypeKey key) {
			return std::ranges::binary_search(archetypeKey, key);
		});
		if (!matches)
			continue;
		for (const auto &chunk : archetype->chunks) {
			for (const auto &entityKey : chunk->entities) {
				const auto state = world_->entities_.find(entityKey);
				if (state == world_->entities_.end() ||
				    state->second != EntityState::Alive)
					continue;
				entities.push_back(EntityHandle{
				    std::get<2>(entityKey), std::get<1>(entityKey),
				    ProducerId{std::get<0>(entityKey)}, world_->id_});
			}
		}
	}
	if (order == QueryOrder::EntityId) {
		std::ranges::sort(entities, {}, [](EntityHandle entity) {
			return World::key(entity);
		});
	}
	return QueryView<Components...>{*world_, world_->structuralVersion_,
	                                std::tuple{tokens...}, std::move(entities)};
}

inline const EventStream &WorldView::events() const noexcept {
	return world_->events();
}

template <Component... Components>
bool QueryView<Components...>::isValid() const noexcept {
	return world_->structuralVersion_ == structuralVersion_;
}

template <Component... Components>
QueryEntry<Components...>
QueryView<Components...>::entryAt(std::size_t index) const {
	const EntityHandle entity = entities_[index];
	return QueryEntry<Components...>{
	    entity, std::tuple{world_->tryGetComponent(
	                entity, std::get<ComponentToken<Components>>(tokens_))...}};
}

} // namespace shiki::game

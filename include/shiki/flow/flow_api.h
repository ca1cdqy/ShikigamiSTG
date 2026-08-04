#pragma once

#include <shiki/core/result.h>
#include <shiki/flow/phase.h>
#include <shiki/flow/stage_program.h>
#include <shiki/stg/actor/actor.h>

#include <memory>
#include <span>
#include <vector>

namespace shiki::stg {
class GameplayContext;
}

namespace shiki::flow {

/** Queues stage programs without exposing Session-owned instance state. */
class FlowApi final {
  public:
	/** Queues a validated immutable program for the next Flow dispatch. */
	[[nodiscard]] Result<void>
	start(std::shared_ptr<const StageProgram> program);

	/** Queues one generic phase and returns its stable Session-local handle. */
	[[nodiscard]] Result<PhaseHandle>
	startPhase(const PhaseSpec &spec,
	           std::span<const game::EntityHandle> members);

	/** Requests deterministic completion of one active or pending phase. */
	[[nodiscard]] Result<void> finishPhase(PhaseHandle phase,
	                                       PhaseResult result);

  private:
	explicit FlowApi(class FlowPool &pool) noexcept : pool_(&pool) {}
	class FlowPool *pool_{};
	friend class FlowPool;
	friend class shiki::stg::GameplayContext;
};

/** Owns active StageProgram instances for one Session. */
class FlowPool final {
  public:
	/** Creates a phase-scoped public API view. */
	[[nodiscard]] FlowApi api() noexcept { return FlowApi{*this}; }

	/** Advances every active instance once in deterministic start order. */
	[[nodiscard]] Result<void>
	dispatch(const ActionRegistry &actions, const PhaseEvents &phaseEvents,
	         const ConditionRegistry &conditions,
	         game::ComponentToken<stg::Health> healthToken,
	         stg::GameplayContext &game);

  private:
	struct Entry final {
		std::shared_ptr<const StageProgram> program;
		StageProgramInstance instance;
	};
	struct PendingPhase final {
		PhaseHandle handle{};
		PhaseSpec spec;
		std::vector<game::EntityHandle> members;
	};
	struct ActivePhase final {
		PhaseHandle handle{};
		PhaseSpec spec;
		std::vector<game::EntityHandle> members;
		Tick enteredAt{};
	};
	struct FinishRequest final {
		PhaseHandle handle{};
		PhaseResult result{PhaseResult::Forced};
	};
	std::vector<std::shared_ptr<const StageProgram>> pending_;
	std::vector<Entry> active_;
	std::vector<PendingPhase> pendingPhases_;
	std::vector<ActivePhase> phases_;
	std::vector<FinishRequest> phaseFinishes_;
	std::uint64_t nextPhase_{1};
	friend class FlowApi;
};

} // namespace shiki::flow

#include <shiki/flow/flow_api.h>

#include <shiki/stg/gameplay_context.h>

#include <algorithm>

namespace shiki::flow {

Result<void> FlowApi::start(std::shared_ptr<const StageProgram> program) {
	if (program == nullptr) {
		return std::unexpected(
		    Error{ErrorDomain::Flow,
		          static_cast<std::uint32_t>(StageProgramError::InvalidNode),
		          "Cannot start a null StageProgram"});
	}
	pool_->pending_.push_back(std::move(program));
	return {};
}

Result<PhaseHandle>
FlowApi::startPhase(const PhaseSpec &spec,
                    std::span<const game::EntityHandle> members) {
	if (pool_->nextPhase_ == 0) {
		return std::unexpected(Error{
		    ErrorDomain::Flow,
		    static_cast<std::uint32_t>(StageProgramError::ExecutionFailed),
		    "Phase handle space is exhausted"});
	}
	const PhaseHandle handle{pool_->nextPhase_++};
	pool_->pendingPhases_.push_back(
	    FlowPool::PendingPhase{handle, spec, {members.begin(), members.end()}});
	return handle;
}

Result<void> FlowApi::finishPhase(PhaseHandle phase, PhaseResult result) {
	if (!phase) {
		return std::unexpected(Error{
		    ErrorDomain::Flow,
		    static_cast<std::uint32_t>(StageProgramError::ExecutionFailed),
		    "Cannot finish a null phase handle"});
	}
	pool_->phaseFinishes_.push_back(FlowPool::FinishRequest{phase, result});
	return {};
}

Result<void> FlowPool::dispatch(const ActionRegistry &actions,
                                const PhaseEvents &phaseEvents,
                                const ConditionRegistry &conditions,
                                game::ComponentToken<stg::Health> healthToken,
                                stg::GameplayContext &game) {
	for (auto &pending : pendingPhases_) {
		phases_.push_back(ActivePhase{pending.handle, pending.spec,
		                              std::move(pending.members),
		                              game.time().tick});
		static_cast<void>(game.commands().emit(
		    phaseEvents.started,
		    PhaseStarted{pending.handle, pending.spec.id, game.time().tick}));
	}
	pendingPhases_.clear();

	for (const FinishRequest &request : phaseFinishes_) {
		const auto phase =
		    std::ranges::find(phases_, request.handle, &ActivePhase::handle);
		if (phase == phases_.end())
			continue;
		static_cast<void>(game.commands().emit(
		    phaseEvents.finished,
		    PhaseFinished{phase->handle, phase->spec.id, request.result,
		                  game.time().tick}));
		phase->handle = {};
	}
	phaseFinishes_.clear();

	for (auto &phase : phases_) {
		if (!phase.handle)
			continue;
		bool cleared =
		    phase.spec.clearWhenAllMembersDefeated && !phase.members.empty();
		if (cleared) {
			for (const game::EntityHandle member : phase.members) {
				const stg::Health *health =
				    game.world().tryGet(member, healthToken);
				if (health != nullptr && health->current > 0) {
					cleared = false;
					break;
				}
			}
		}
		const bool timedOut = phase.spec.timeout.value != 0 &&
		                      game.time().tick.value - phase.enteredAt.value >=
		                          phase.spec.timeout.value;
		if (!cleared && !timedOut)
			continue;
		const PhaseResult result =
		    cleared ? PhaseResult::Cleared : PhaseResult::Timeout;
		static_cast<void>(game.commands().emit(
		    phaseEvents.finished, PhaseFinished{phase.handle, phase.spec.id,
		                                        result, game.time().tick}));
		phase.handle = {};
	}
	std::erase_if(phases_,
	              [](const ActivePhase &phase) { return !phase.handle; });

	for (auto &program : pending_)
		active_.push_back(Entry{program, StageProgramInstance{*program}});
	pending_.clear();
	for (auto &entry : active_) {
		auto result = entry.instance.tick(actions, conditions, game);
		if (!result)
			return std::unexpected(result.error());
	}
	std::erase_if(active_,
	              [](const Entry &entry) { return entry.instance.complete(); });
	return {};
}

} // namespace shiki::flow

#include <shiki/flow/stage_program.h>

#include <shiki/stg/gameplay_context.h>

#include <algorithm>
#include <limits>

namespace shiki::flow {
namespace {

[[nodiscard]] Error makeProgramError(StageProgramError code,
                                     const char *message) {
	return Error{ErrorDomain::Flow, static_cast<std::uint32_t>(code), message};
}

[[nodiscard]] bool fitsU32(std::size_t value) noexcept {
	return value <= std::numeric_limits<std::uint32_t>::max();
}

} // namespace

NodeId StageProgramBuilder::action(GameplayAction actionValue) {
	const NodeId id{static_cast<std::uint32_t>(nodes_.size())};
	nodes_.push_back(
	    PendingNode{NodeKind::Action, std::move(actionValue), {}, {}, {}});
	return id;
}

NodeId StageProgramBuilder::wait(TickSpan duration) {
	const NodeId id{static_cast<std::uint32_t>(nodes_.size())};
	nodes_.push_back(PendingNode{NodeKind::Wait, {}, {}, duration, {}});
	return id;
}

NodeId StageProgramBuilder::wait(GameplayCondition condition) {
	const NodeId id{static_cast<std::uint32_t>(nodes_.size())};
	nodes_.push_back(
	    PendingNode{NodeKind::WaitCondition, {}, std::move(condition), {}, {}});
	return id;
}

Result<NodeId> StageProgramBuilder::sequence(std::span<const NodeId> children) {
	if (children.empty())
		return std::unexpected(
		    makeProgramError(StageProgramError::EmptyComposite,
		                     "Stage sequence must contain at least one child"));
	if (!std::ranges::all_of(children,
	                         [this](NodeId node) { return valid(node); }))
		return std::unexpected(
		    makeProgramError(StageProgramError::InvalidNode,
		                     "Stage sequence contains an invalid child node"));
	const NodeId id{static_cast<std::uint32_t>(nodes_.size())};
	nodes_.push_back(PendingNode{
	    NodeKind::Sequence, {}, {}, {}, {children.begin(), children.end()}});
	return id;
}

Result<NodeId> StageProgramBuilder::parallel(std::span<const NodeId> children) {
	if (children.empty())
		return std::unexpected(makeProgramError(
		    StageProgramError::EmptyComposite,
		    "Stage parallel node must contain at least one child"));
	if (!std::ranges::all_of(children,
	                         [this](NodeId node) { return valid(node); }))
		return std::unexpected(makeProgramError(
		    StageProgramError::InvalidNode,
		    "Stage parallel node contains an invalid child node"));
	const NodeId id{static_cast<std::uint32_t>(nodes_.size())};
	nodes_.push_back(PendingNode{
	    NodeKind::Parallel, {}, {}, {}, {children.begin(), children.end()}});
	return id;
}

Result<StageProgram> StageProgramBuilder::build(NodeId root) && {
	if (!valid(root))
		return std::unexpected(makeProgramError(StageProgramError::InvalidNode,
		                                        "Stage root node is invalid"));
	std::vector<std::uint32_t> parents(nodes_.size());
	for (const PendingNode &node : nodes_) {
		for (const NodeId child : node.children) {
			if (++parents[child.value] > 1)
				return std::unexpected(makeProgramError(
				    StageProgramError::MultipleParents,
				    "Stage node is owned by more than one composite"));
		}
	}
	if (parents[root.value] != 0)
		return std::unexpected(
		    makeProgramError(StageProgramError::MultipleParents,
		                     "Stage root node is owned by another composite"));
	std::vector<bool> reachable(nodes_.size());
	std::vector<NodeId> pending{root};
	while (!pending.empty()) {
		const NodeId node = pending.back();
		pending.pop_back();
		if (reachable[node.value])
			continue;
		reachable[node.value] = true;
		for (const NodeId child : nodes_[node.value].children)
			pending.push_back(child);
	}
	if (std::ranges::find(reachable, false) != reachable.end())
		return std::unexpected(
		    makeProgramError(StageProgramError::UnreachableNode,
		                     "Stage program contains an unreachable node"));

	StageProgram program;
	program.root_ = root;
	program.nodes_.reserve(nodes_.size());
	for (const PendingNode &pendingNode : nodes_) {
		const bool isCondition = pendingNode.kind == NodeKind::WaitCondition;
		const auto encodedPayload = isCondition
		                                ? pendingNode.condition.payload()
		                                : pendingNode.action.payload();
		if (!fitsU32(program.children_.size()) ||
		    !fitsU32(program.payload_.size()) ||
		    !fitsU32(pendingNode.children.size()) ||
		    !fitsU32(encodedPayload.size()))
			return std::unexpected(makeProgramError(
			    StageProgramError::ProgramTooLarge,
			    "Stage program exceeds 32-bit contiguous storage limits"));
		ProgramNode node{
		    .kind = pendingNode.kind,
		    .firstChild = static_cast<std::uint32_t>(program.children_.size()),
		    .childCount =
		        static_cast<std::uint32_t>(pendingNode.children.size()),
		    .payloadOffset =
		        static_cast<std::uint32_t>(program.payload_.size()),
		    .payloadSize = static_cast<std::uint32_t>(encodedPayload.size()),
		    .actionType = isCondition ? pendingNode.condition.type()
		                              : pendingNode.action.type(),
		    .actionVersion = isCondition ? pendingNode.condition.version()
		                                 : pendingNode.action.version(),
		    .wait = pendingNode.wait};
		for (const NodeId child : pendingNode.children)
			program.children_.push_back(child.value);
		program.payload_.insert(program.payload_.end(), encodedPayload.begin(),
		                        encodedPayload.end());
		program.nodes_.push_back(node);
	}
	return program;
}

bool StageProgramBuilder::valid(NodeId node) const noexcept {
	return node.value < nodes_.size();
}

StageProgramInstance::StageProgramInstance(const StageProgram &program)
    : program_(&program), states_(program.nodeCount()) {}

Result<void> StageProgramInstance::tick(const ActionRegistry &actions,
                                        const ConditionRegistry &conditions,
                                        stg::GameplayContext &game) {
	if (complete())
		return {};
	auto result = advance(program_->root_, actions, conditions, game);
	if (!result)
		return std::unexpected(result.error());
	return {};
}

bool StageProgramInstance::complete() const noexcept {
	return states_[program_->root_.value].complete;
}

Result<bool> StageProgramInstance::advance(NodeId id,
                                           const ActionRegistry &actions,
                                           const ConditionRegistry &conditions,
                                           stg::GameplayContext &game) {
	NodeState &state = states_[id.value];
	if (state.complete)
		return true;
	const ProgramNode &node = program_->nodes_[id.value];
	if (node.kind == NodeKind::Action) {
		auto result = actions.execute(
		    node.actionType, node.actionVersion,
		    std::span<const std::byte>{program_->payload_}.subspan(
		        node.payloadOffset, node.payloadSize),
		    game);
		if (!result)
			return std::unexpected(result.error());
		state.complete = true;
		return true;
	}
	if (node.kind == NodeKind::Wait) {
		if (state.elapsed.value >= node.wait.value) {
			state.complete = true;
			return true;
		}
		++state.elapsed.value;
		return false;
	}
	if (node.kind == NodeKind::WaitCondition) {
		auto result = conditions.evaluate(
		    node.actionType, node.actionVersion,
		    std::span<const std::byte>{program_->payload_}.subspan(
		        node.payloadOffset, node.payloadSize),
		    game);
		if (!result)
			return std::unexpected(result.error());
		state.complete = *result;
		return *result;
	}
	const auto children =
	    std::span<const std::uint32_t>{program_->children_}.subspan(
	        node.firstChild, node.childCount);
	if (node.kind == NodeKind::Sequence) {
		while (state.child < children.size()) {
			auto child = advance(NodeId{children[state.child]}, actions,
			                     conditions, game);
			if (!child)
				return std::unexpected(child.error());
			if (!*child)
				return false;
			++state.child;
		}
		state.complete = true;
		return true;
	}
	bool finished = true;
	for (const std::uint32_t childId : children) {
		auto child = advance(NodeId{childId}, actions, conditions, game);
		if (!child)
			return std::unexpected(child.error());
		finished = finished && *child;
	}
	state.complete = finished;
	return finished;
}

} // namespace shiki::flow

#pragma once

#include <shiki/core/result.h>
#include <shiki/core/time.h>
#include <shiki/flow/action.h>
#include <shiki/flow/condition.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace shiki::stg {
class GameplayContext;
}

namespace shiki::flow {

/** Stable index into the node array of a StageProgram. */
struct NodeId final {
	std::uint32_t value{}; ///< Zero-based node index; the builder assigns valid values.
	auto operator<=>(const NodeId &) const = default;
};

/** Selects the execution semantics for one ProgramNode. */
enum class NodeKind : std::uint8_t {
	Action,       ///< Executes one registered action payload immediately.
	Wait,         ///< Pauses execution for a fixed tick count.
	WaitCondition, ///< Pauses execution until a registered condition is true.
	Sequence,      ///< Executes children in order; advances when each completes.
	Parallel       ///< Executes all children concurrently; advances when all complete.
};

/** Compact immutable node stored by a StageProgram. */
struct ProgramNode final {
	NodeKind kind{};                  ///< Execution semantics for this node.
	std::uint32_t firstChild{};       ///< Index of the first child in the children array.
	std::uint32_t childCount{};       ///< Number of direct children.
	std::uint32_t payloadOffset{};    ///< Byte offset into the program payload buffer.
	std::uint32_t payloadSize{};      ///< Byte length of the encoded payload.
	game::TypeKey actionType{};       ///< Persistent action or condition type identity.
	std::uint32_t actionVersion{};    ///< Codec version matched at execution time.
	TickSpan wait{};                  ///< Duration for Wait nodes.
};

/** Stable failures produced while building or executing a StageProgram. */
enum class StageProgramError : std::uint32_t {
	InvalidNode = 1, ///< A NodeId does not correspond to any builder node.
	EmptyComposite,  ///< A Sequence or Parallel node was built with no children.
	MultipleParents, ///< A node was referenced by more than one parent.
	UnreachableNode, ///< A node is not reachable from the declared root.
	ExecutionFailed, ///< A registered action or condition returned an error.
	ProgramTooLarge  ///< The compiled node or payload buffer exceeded its limit.
};

/** An immutable contiguous stage-flow graph shareable across Sessions. */
class StageProgram final {
  public:
	[[nodiscard]] std::size_t nodeCount() const noexcept {
		return nodes_.size();
	}
	[[nodiscard]] std::span<const ProgramNode> nodes() const noexcept {
		return nodes_;
	}
	[[nodiscard]] std::span<const std::uint32_t> children() const noexcept {
		return children_;
	}
	[[nodiscard]] std::span<const std::byte> payload() const noexcept {
		return payload_;
	}

  private:
	std::vector<ProgramNode> nodes_;
	std::vector<std::uint32_t> children_;
	std::vector<std::byte> payload_;
	NodeId root_{};
	friend class StageProgramBuilder;
	friend class StageProgramInstance;
};

/** Builds and validates one format-independent immutable stage program. */
class StageProgramBuilder final {
  public:
	[[nodiscard]] NodeId action(GameplayAction action);
	[[nodiscard]] NodeId wait(TickSpan duration);
	[[nodiscard]] NodeId wait(GameplayCondition condition);
	[[nodiscard]] Result<NodeId> sequence(std::span<const NodeId> children);
	[[nodiscard]] Result<NodeId> parallel(std::span<const NodeId> children);
	[[nodiscard]] Result<StageProgram> build(NodeId root) &&;

  private:
	struct PendingNode final {
		NodeKind kind{};
		GameplayAction action;
		GameplayCondition condition;
		TickSpan wait{};
		std::vector<NodeId> children;
	};
	[[nodiscard]] bool valid(NodeId node) const noexcept;
	std::vector<PendingNode> nodes_;
};

/** Owns deterministic execution state for one Session program instance. */
class StageProgramInstance final {
  public:
	explicit StageProgramInstance(const StageProgram &program);
	[[nodiscard]] Result<void> tick(const ActionRegistry &actions,
	                                const ConditionRegistry &conditions,
	                                stg::GameplayContext &game);
	[[nodiscard]] bool complete() const noexcept;

  private:
	struct NodeState final {
		TickSpan elapsed{};
		std::size_t child{};
		bool complete{};
	};
	[[nodiscard]] Result<bool> advance(NodeId node,
	                                   const ActionRegistry &actions,
	                                   const ConditionRegistry &conditions,
	                                   stg::GameplayContext &game);
	const StageProgram *program_{};
	std::vector<NodeState> states_;
};

} // namespace shiki::flow

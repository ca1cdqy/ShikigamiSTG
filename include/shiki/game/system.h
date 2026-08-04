#pragma once

#include <shiki/game/type_id.h>

#include <cstdint>
#include <string>
#include <vector>

namespace shiki::game {

/** Identifies the fixed simulation stage in which a system executes. */
enum class SystemPhase : std::uint8_t {
	External,       ///< Processes external input and platform events.
	Control,        ///< Reads and normalizes raw controller input.
	Intent,         ///< Translates normalized input into actor intents.
	Flow,           ///< Advances stage programs and phase lifecycles.
	PreSimulation,  ///< Runs user logic before the main simulation pass.
	Actor,          ///< Dispatches actor behavior callbacks.
	Simulation,     ///< Applies movement, physics, and custom gameplay logic.
	Contact,        ///< Detects geometric overlaps between entities.
	Combat,         ///< Evaluates damage, cancellation, and reward rules.
	Resolution,     ///< Finalizes spawn, destroy, and structural commands.
	Finalize        ///< Runs post-resolution cleanup and state snapshots.
};

/** Selects whether a system can be partitioned by a future scheduler. */
enum class ParallelPolicy : std::uint8_t {
	Serial,              ///< Executes exactly once on the simulation thread.
	ParallelForEachChunk ///< Can execute over deterministic chunk partitions.
};

/** Declares stable scheduling identity and explicit dependency edges. */
struct SystemDescriptor final {
	std::string name;                        ///< Stable UTF-8 system name used for dependency resolution.
	SystemPhase phase{SystemPhase::Simulation}; ///< Execution stage within one fixed tick.
	std::vector<TypeKey> before;             ///< Systems that must execute after this one in the same phase.
	std::vector<TypeKey> after;              ///< Systems that must execute before this one in the same phase.
	ParallelPolicy parallel{ParallelPolicy::Serial}; ///< Parallelism policy applied by the scheduler.
};

} // namespace shiki::game

#include <shiki/flow/phase.h>

namespace shiki::flow {

Result<PhaseEvents> PhaseEvents::registerWith(game::World &world) {
	using game::EventVisibility;
	auto started = world.registerEvent<PhaseStarted>(
	    {.name = "shiki.phase_started.v1",
	     .visibility = EventVisibility::SimulationAndPresentation});
	if (!started)
		return std::unexpected(started.error());
	auto finished = world.registerEvent<PhaseFinished>(
	    {.name = "shiki.phase_finished.v1",
	     .visibility = EventVisibility::SimulationAndPresentation});
	if (!finished)
		return std::unexpected(finished.error());
	return PhaseEvents{*started, *finished};
}

} // namespace shiki::flow

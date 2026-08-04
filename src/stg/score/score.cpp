#include <shiki/stg/score/score.h>

#include <shiki/game_definition.h>

#include <algorithm>

namespace shiki::stg {

Result<ScoreComponents>
ScoreComponents::registerWith(GameDefinition &definition) {
	using game::ComponentFlags;
	auto score = definition.registerComponent<Score>(
	    {.name = "shiki.score.v1",
	     .flags = ComponentFlags::Observable | ComponentFlags::Deterministic});
	if (!score)
		return std::unexpected(score.error());
	return ScoreComponents{*score};
}

Score applyScoreDelta(Score current, ScoreDelta delta) noexcept {
	current.points += delta.points;
	current.highScore = std::max(current.highScore, current.points);
	current.graze += delta.graze;
	current.pointItems += delta.pointItems;
	return current;
}

ScoreApi::ScoreApi(game::WorldView world, game::Commands &commands,
                   ScoreComponents components) noexcept
    : world_(world), commands_(&commands), components_(components) {}

game::CommandStatus ScoreApi::set(game::EntityHandle entity, Score score) {
	return commands_->set(entity, components_.score, score);
}

Result<Score> ScoreApi::add(game::EntityHandle entity, ScoreDelta delta) {
	const Score *current = world_.tryGet(entity, components_.score);
	if (!current)
		return std::unexpected(Error{
		    ErrorDomain::World,
		    static_cast<std::uint32_t>(game::CommandStatus::InvalidComponent),
		    "Entity does not have a score component"});
	Score updated = applyScoreDelta(*current, delta);
	const game::CommandStatus status = set(entity, updated);
	if (status != game::CommandStatus::Accepted)
		return std::unexpected(Error{ErrorDomain::World,
		                             static_cast<std::uint32_t>(status),
		                             "Score command was rejected"});
	return updated;
}

} // namespace shiki::stg

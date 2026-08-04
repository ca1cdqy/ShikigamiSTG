#include <shiki/stg/gameplay_context.h>

namespace shiki::stg {

GameplayContext::GameplayContext(
    game::WorldView world, game::Commands &commands,
    const ProjectileComponents &projectileComponents,
    const ActorComponents &actorComponents, TimeView time,
    const control::InputFrame &input, const ActorRegistry *actorRegistry,
    ActorBehaviorPool *actorBehaviors,
    const control::ControllerRegistry *controllerRegistry,
    control::ControllerPool *controllers, flow::FlowPool *flow,
    const PatternRegistry *patternRegistry, PatternPool *patterns) noexcept
    : world_(world), commands_(&commands),
      projectiles_(commands, projectileComponents),
      actors_(commands, actorComponents, actorRegistry, actorBehaviors,
              controllerRegistry, controllers),
      patterns_(patternRegistry, patterns, *this), time_(time), input_(&input),
      controllers_(controllers), flow_(flow) {}

const control::ActorIntent *
GameplayContext::intent(game::EntityHandle actor) const noexcept {
	return controllers_ != nullptr ? controllers_->intent(actor) : nullptr;
}

} // namespace shiki::stg

#include <shiki/stg/item/item.h>

#include <shiki/game_definition.h>
#include <shiki/stg/gameplay_context.h>

#include <string>
#include <utility>

namespace shiki::stg {
namespace {

[[nodiscard]] Error itemCommandError(game::CommandStatus status) {
	return Error{ErrorDomain::World,
	             static_cast<std::uint32_t>(status),
	             "Item command was rejected",
	             {{"command_status",
	               std::to_string(static_cast<std::uint32_t>(status))}}};
}

} // namespace

Result<ItemComponents>
ItemComponents::registerWith(GameDefinition &definition) {
	using game::ComponentFlags;
	constexpr ComponentFlags observable =
	    ComponentFlags::Observable | ComponentFlags::Deterministic;

	auto transform = definition.registerComponent<Transform>(
	    {.name = "shiki.transform.v1", .flags = observable});
	if (!transform)
		return std::unexpected(transform.error());
	auto motion = definition.registerComponent<Motion>(
	    {.name = "shiki.motion.v1", .flags = observable});
	if (!motion)
		return std::unexpected(motion.error());
	auto collision = definition.registerComponent<CircleCollision>(
	    {.name = "shiki.circle_collision.v1", .flags = observable});
	if (!collision)
		return std::unexpected(collision.error());
	auto identity = definition.registerComponent<ItemIdentity>(
	    {.name = "shiki.item.identity.v1",
	     .flags = ComponentFlags::Deterministic});
	if (!identity)
		return std::unexpected(identity.error());
	auto value = definition.registerComponent<ItemValue>(
	    {.name = "shiki.item.value.v1",
	     .flags = ComponentFlags::Deterministic});
	if (!value)
		return std::unexpected(value.error());
	auto lifetime = definition.registerComponent<ItemLifetime>(
	    {.name = "shiki.item.lifetime.v1",
	     .flags = ComponentFlags::Deterministic});
	if (!lifetime)
		return std::unexpected(lifetime.error());
	auto visual = definition.registerComponent<ItemVisual>(
	    {.name = "shiki.item.visual.v1", .flags = observable});
	if (!visual)
		return std::unexpected(visual.error());

	return ItemComponents{*transform, *motion,   *collision, *identity,
	                      *value,     *lifetime, *visual};
}

ItemApi::ItemApi(game::Commands &commands,
                 const ItemComponents &components) noexcept
    : commands_(&commands), components_(&components) {}

Result<game::EntityHandle> ItemApi::spawn(const ItemSpec &spec,
                                          const ItemSpawn &spawn) {
	auto entity = commands_->spawn();
	if (!entity)
		return std::unexpected(entity.error());
	const auto write = [&](game::CommandStatus status) -> Result<void> {
		if (status == game::CommandStatus::Accepted)
			return {};
		return std::unexpected(itemCommandError(status));
	};
	const auto abort = [&] { static_cast<void>(commands_->destroy(*entity)); };
	const auto set = [&](auto token, auto value) -> Result<void> {
		return write(commands_->set(*entity, token, std::move(value)));
	};
	const auto setOrAbort = [&](Result<void> result) -> Result<void> {
		if (!result)
			abort();
		return result;
	};
	if (auto result = setOrAbort(
	        set(components_->transform, Transform{spawn.position, {}}));
	    !result)
		return std::unexpected(result.error());
	if (auto result =
	        setOrAbort(set(components_->motion, Motion{spawn.velocityPerTick}));
	    !result)
		return std::unexpected(result.error());
	if (auto result = setOrAbort(
	        set(components_->collision, CircleCollision{spec.collisionRadius}));
	    !result)
		return std::unexpected(result.error());
	if (auto result =
	        setOrAbort(set(components_->identity, ItemIdentity{spec.kind}));
	    !result)
		return std::unexpected(result.error());
	if (auto result =
	        setOrAbort(set(components_->value, ItemValue{spec.value}));
	    !result)
		return std::unexpected(result.error());
	if (auto result =
	        setOrAbort(set(components_->lifetime, ItemLifetime{spec.lifetime}));
	    !result)
		return std::unexpected(result.error());
	if (auto result =
	        setOrAbort(set(components_->visual, ItemVisual{spec.style}));
	    !result)
		return std::unexpected(result.error());
	return *entity;
}

Result<void> installItemSystems(GameDefinition &definition,
                                ItemComponents components) {
	return definition.addSystem(
	    {.name = "shiki.item.motion.v1",
	     .phase = game::SystemPhase::Simulation},
	    [components](GameplayContext &game) {
		    auto items = game.world().query(
		        game::QueryOrder::EntityId, components.transform,
		        components.motion, components.lifetime, components.identity);
		    for (const auto row : items) {
			    auto lifetime = row.template get<ItemLifetime>();
			    if (lifetime.remaining.value == 0) {
				    static_cast<void>(game.commands().destroy(row.entity()));
				    continue;
			    }
			    auto transform = row.template get<Transform>();
			    const auto &motion = row.template get<Motion>();
			    transform.position.value += motion.velocityPerTick;
			    --lifetime.remaining.value;
			    static_cast<void>(game.commands().set(
			        row.entity(), components.transform, transform));
			    static_cast<void>(game.commands().set(
			        row.entity(), components.lifetime, lifetime));
		    }
	    });
}

} // namespace shiki::stg

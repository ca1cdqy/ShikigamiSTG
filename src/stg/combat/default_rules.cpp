#include <shiki/stg/combat/default_rules.h>

#include <shiki/game_definition.h>
#include <shiki/stg/gameplay_context.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace shiki::stg {
namespace {

struct ContactBody final {
	game::EntityHandle entity{};
	WorldPosition position{};
	float radius{};
};

struct PendingDamage final {
	game::EntityHandle target{};
	game::EntityHandle source{};
	std::int64_t amount{};
};

[[nodiscard]] float distanceSquared(WorldPosition first,
                                    WorldPosition second) noexcept {
	const float x = first.value.x - second.value.x;
	const float y = first.value.y - second.value.y;
	return x * x + y * y;
}

void detectContacts(GameplayContext &game, const DefaultRuleSchema &schema) {
	std::vector<ContactBody> bodies;
	auto query = game.world().query(schema.transform, schema.collision);
	bodies.reserve(query.size());
	for (const auto row : query) {
		bodies.push_back(
		    ContactBody{row.entity(), row.template get<Transform>().position,
		                row.template get<CircleCollision>().radius});
	}
	for (std::size_t first = 0; first < bodies.size(); ++first) {
		for (std::size_t second = first + 1; second < bodies.size(); ++second) {
			const float separation = distanceSquared(bodies[first].position,
			                                         bodies[second].position);
			const float radius = bodies[first].radius + bodies[second].radius;
			if (separation <= radius * radius) {
				static_cast<void>(game.commands().emit(
				    schema.contact,
				    ContactEvent{bodies[first].entity, bodies[second].entity,
				                 separation}));
			}
		}
	}
}

void collectDamage(GameplayContext &game, const DefaultRuleSchema &schema,
                   game::EntityHandle source, game::EntityHandle target,
                   std::vector<PendingDamage> &pending) {
	const DamageSource *damage =
	    game.world().tryGet(source, schema.damageSource);
	const Health *health = game.world().tryGet(target, schema.health);
	const Relation *sourceRelation =
	    game.world().tryGet(source, schema.relation);
	const Relation *targetRelation =
	    game.world().tryGet(target, schema.relation);
	if (damage == nullptr || health == nullptr || sourceRelation == nullptr ||
	    targetRelation == nullptr ||
	    sourceRelation->faction == targetRelation->faction ||
	    sourceRelation->owner == target)
		return;
	static_cast<void>(game.commands().emit(
	    schema.damageRequest,
	    DamageRequest{source, target,
	                  std::max<std::int64_t>(0, damage->amount)}));
	auto entry = std::ranges::find(pending, target, &PendingDamage::target);
	if (entry == pending.end()) {
		pending.push_back(PendingDamage{
		    target, source, std::max<std::int64_t>(0, damage->amount)});
	} else {
		entry->amount += std::max<std::int64_t>(0, damage->amount);
	}
	if (damage->consumeOnHit)
		static_cast<void>(game.commands().destroy(source));
}

void applyDamage(GameplayContext &game, const DefaultRuleSchema &schema) {
	std::vector<PendingDamage> pending;
	auto contacts = game.events().current(schema.contact);
	for (const auto &event : contacts) {
		collectDamage(game, schema, event.value.first, event.value.second,
		              pending);
		collectDamage(game, schema, event.value.second, event.value.first,
		              pending);
	}
	for (const PendingDamage &damage : pending) {
		const Health *current =
		    game.world().tryGet(damage.target, schema.health);
		if (current == nullptr)
			continue;
		const std::int64_t applied = std::min(current->current, damage.amount);
		const std::int64_t remaining = current->current - applied;
		static_cast<void>(game.commands().set(
		    damage.target, schema.health, Health{remaining, current->maximum}));
		static_cast<void>(game.commands().emit(
		    schema.damageResult,
		    DamageResult{damage.source, damage.target, damage.amount, applied,
		                 remaining, remaining == 0}));
		if (remaining == 0)
			static_cast<void>(game.commands().destroy(damage.target));
	}
}

void applyCancellation(GameplayContext &game, const DefaultRuleSchema &schema) {
	auto requests = game.events().current(schema.cancelRequest);
	if (requests.empty())
		return;
	std::vector<game::EntityHandle> cancelled;
	auto projectiles = game.world().query(schema.transform, schema.relation,
	                                      schema.projectile);
	for (const auto projectile : projectiles) {
		const ProjectileIdentity &identity =
		    projectile.template get<ProjectileIdentity>();
		if (hasFlag(identity.flags, ProjectileFlags::CancelImmune))
			continue;
		for (const auto &request : requests) {
			if (request.value.filterFaction &&
			    projectile.template get<Relation>().faction !=
			        request.value.faction)
				continue;
			const WorldPosition position =
			    projectile.template get<Transform>().position;
			if (distanceSquared(position, request.value.center) >
			    request.value.radius * request.value.radius)
				continue;
			if (std::ranges::find(cancelled, projectile.entity()) !=
			    cancelled.end())
				break;
			cancelled.push_back(projectile.entity());
			static_cast<void>(game.commands().destroy(projectile.entity()));
			static_cast<void>(game.commands().emit(
			    schema.cancelResult,
			    CancelResult{projectile.entity(), request.value.source,
			                 position, request.value.cause,
			                 request.value.convertToReward}));
			break;
		}
	}
}

void publishRewards(GameplayContext &game, const DefaultRuleSchema &schema) {
	for (const auto &damage : game.events().current(schema.damageResult)) {
		if (!damage.value.killed)
			continue;
		WorldPosition position{};
		if (const Transform *transform =
		        game.world().tryGet(damage.value.target, schema.transform))
			position = transform->position;
		static_cast<void>(game.commands().emit(
		    schema.reward, RewardEvent{RewardKind::Kill, damage.value.source,
		                               damage.value.target, position, 1}));
	}
	for (const auto &cancel : game.events().current(schema.cancelResult)) {
		if (!cancel.value.convertToReward)
			continue;
		static_cast<void>(game.commands().emit(
		    schema.reward,
		    RewardEvent{RewardKind::ProjectileCancel, cancel.value.source,
		                cancel.value.projectile, cancel.value.position, 1}));
	}
}

} // namespace

Result<DefaultRuleSchema> DefaultRules::install(GameDefinition &definition) {
	using game::ComponentFlags;
	constexpr ComponentFlags deterministic = ComponentFlags::Deterministic;
	constexpr ComponentFlags observable =
	    ComponentFlags::Observable | ComponentFlags::Deterministic;
	auto transform = definition.registerComponent<Transform>(
	    {.name = "shiki.transform.v1", .flags = observable});
	if (!transform)
		return std::unexpected(transform.error());
	auto collision = definition.registerComponent<CircleCollision>(
	    {.name = "shiki.circle_collision.v1", .flags = observable});
	if (!collision)
		return std::unexpected(collision.error());
	auto relation = definition.registerComponent<Relation>(
	    {.name = "shiki.relation.v1", .flags = deterministic});
	if (!relation)
		return std::unexpected(relation.error());
	auto projectile = definition.registerComponent<ProjectileIdentity>(
	    {.name = "shiki.projectile.identity.v1", .flags = deterministic});
	if (!projectile)
		return std::unexpected(projectile.error());
	auto damageSource = definition.registerComponent<DamageSource>(
	    {.name = "shiki.damage_source.v1", .flags = deterministic});
	if (!damageSource)
		return std::unexpected(damageSource.error());
	auto health = definition.registerComponent<Health>(
	    {.name = "shiki.health.v1", .flags = observable});
	if (!health)
		return std::unexpected(health.error());

	using game::EventVisibility;
	auto contact = definition.registerEvent<ContactEvent>(
	    {.name = "shiki.contact.v1",
	     .visibility = EventVisibility::Simulation});
	if (!contact)
		return std::unexpected(contact.error());
	auto damageRequest = definition.registerEvent<DamageRequest>(
	    {.name = "shiki.damage_request.v1",
	     .visibility = EventVisibility::Simulation});
	if (!damageRequest)
		return std::unexpected(damageRequest.error());
	auto damageResult = definition.registerEvent<DamageResult>(
	    {.name = "shiki.damage_result.v1",
	     .visibility = EventVisibility::SimulationAndPresentation});
	if (!damageResult)
		return std::unexpected(damageResult.error());
	auto cancelRequest = definition.registerEvent<CancelRequest>(
	    {.name = "shiki.cancel_request.v1",
	     .visibility = EventVisibility::Simulation});
	if (!cancelRequest)
		return std::unexpected(cancelRequest.error());
	auto cancelResult = definition.registerEvent<CancelResult>(
	    {.name = "shiki.cancel_result.v1",
	     .visibility = EventVisibility::SimulationAndPresentation});
	if (!cancelResult)
		return std::unexpected(cancelResult.error());
	auto reward = definition.registerEvent<RewardEvent>(
	    {.name = "shiki.reward.v1",
	     .visibility = EventVisibility::SimulationAndPresentation});
	if (!reward)
		return std::unexpected(reward.error());

	const DefaultRuleSchema schema{
	    *transform,    *collision,     *relation,     *projectile,
	    *damageSource, *health,        *contact,      *damageRequest,
	    *damageResult, *cancelRequest, *cancelResult, *reward};
	auto contactSystem = definition.addSystem(
	    {.name = "shiki.rules.contact.v1", .phase = game::SystemPhase::Contact},
	    [schema](GameplayContext &game) { detectContacts(game, schema); });
	if (!contactSystem)
		return std::unexpected(contactSystem.error());
	auto damageSystem = definition.addSystem(
	    {.name = "shiki.rules.damage.v1", .phase = game::SystemPhase::Combat},
	    [schema](GameplayContext &game) { applyDamage(game, schema); });
	if (!damageSystem)
		return std::unexpected(damageSystem.error());
	auto cancelSystem = definition.addSystem(
	    {.name = "shiki.rules.cancel.v1", .phase = game::SystemPhase::Combat},
	    [schema](GameplayContext &game) { applyCancellation(game, schema); });
	if (!cancelSystem)
		return std::unexpected(cancelSystem.error());
	auto rewardSystem = definition.addSystem(
	    {.name = "shiki.rules.reward.v1",
	     .phase = game::SystemPhase::Resolution},
	    [schema](GameplayContext &game) { publishRewards(game, schema); });
	if (!rewardSystem)
		return std::unexpected(rewardSystem.error());
	return schema;
}

} // namespace shiki::stg

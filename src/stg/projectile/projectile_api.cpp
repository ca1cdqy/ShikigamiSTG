#include <shiki/stg/projectile/projectile_api.h>

#include <string>
#include <utility>
#include <vector>

namespace shiki::stg {
namespace {

[[nodiscard]] Error projectileCommandError(game::CommandStatus status) {
	return Error{ErrorDomain::World,
	             static_cast<std::uint32_t>(status),
	             "Projectile command was rejected",
	             {{"command_status",
	               std::to_string(static_cast<std::uint32_t>(status))}}};
}

} // namespace

Result<ProjectileComponents>
ProjectileComponents::registerWith(game::World &world) {
	using game::ComponentFlags;
	constexpr ComponentFlags observable =
	    ComponentFlags::Observable | ComponentFlags::Deterministic;

	auto transform = world.registerComponent<Transform>(
	    {.name = "shiki.transform.v1", .flags = observable});
	if (!transform)
		return std::unexpected(transform.error());
	auto motion = world.registerComponent<Motion>(
	    {.name = "shiki.motion.v1", .flags = observable});
	if (!motion)
		return std::unexpected(motion.error());
	auto collision = world.registerComponent<CircleCollision>(
	    {.name = "shiki.circle_collision.v1", .flags = observable});
	if (!collision)
		return std::unexpected(collision.error());
	auto relation = world.registerComponent<Relation>(
	    {.name = "shiki.relation.v1", .flags = ComponentFlags::Deterministic});
	if (!relation)
		return std::unexpected(relation.error());
	auto lifetime = world.registerComponent<ProjectileLifetime>(
	    {.name = "shiki.projectile.lifetime.v1",
	     .flags = ComponentFlags::Deterministic});
	if (!lifetime)
		return std::unexpected(lifetime.error());
	auto visual = world.registerComponent<ProjectileVisual>(
	    {.name = "shiki.projectile.visual.v1", .flags = observable});
	if (!visual)
		return std::unexpected(visual.error());
	auto identity = world.registerComponent<ProjectileIdentity>(
	    {.name = "shiki.projectile.identity.v1",
	     .flags = ComponentFlags::Deterministic});
	if (!identity)
		return std::unexpected(identity.error());
	auto damage = world.registerComponent<DamageSource>(
	    {.name = "shiki.damage_source.v1",
	     .flags = ComponentFlags::Deterministic});
	if (!damage)
		return std::unexpected(damage.error());

	return ProjectileComponents{*transform, *motion, *collision, *relation,
	                            *lifetime,  *visual, *identity,  *damage};
}

ProjectileApi::ProjectileApi(game::Commands &commands,
                             const ProjectileComponents &components) noexcept
    : commands_(&commands), components_(&components) {}

Result<game::EntityHandle> ProjectileApi::spawn(const ProjectileSpec &spec,
                                                const ProjectileSpawn &spawn) {
	return record(spec, spawn);
}

Result<ProjectileBatch>
ProjectileApi::spawnBatch(const ProjectileSpec &spec,
                          std::span<const ProjectileSpawn> spawns) {
	using Record = std::tuple<Transform, Motion, CircleCollision, Relation,
	                          ProjectileLifetime, ProjectileVisual,
	                          ProjectileIdentity, DamageSource>;
	std::vector<Record> records;
	records.reserve(spawns.size());
	for (const ProjectileSpawn &spawn : spawns) {
		records.emplace_back(
		    Transform{spawn.position, spawn.orientation},
		    Motion{spawn.velocityPerTick},
		    CircleCollision{spec.collisionRadius},
		    Relation{spawn.owner, spec.faction},
		    ProjectileLifetime{spec.lifetime}, ProjectileVisual{spec.style},
		    ProjectileIdentity{spec.flags}, DamageSource{spec.damage, true});
	}
	if (records.empty())
		return ProjectileBatch{0};
	auto result = commands_->spawnBatch(
	    std::tuple{components_->transform, components_->motion,
	               components_->collision, components_->relation,
	               components_->lifetime, components_->visual,
	               components_->identity, components_->damage},
	    std::move(records));
	if (!result)
		return std::unexpected(result.error());
	return ProjectileBatch{*result};
}

Result<game::EntityHandle> ProjectileApi::record(const ProjectileSpec &spec,
                                                 const ProjectileSpawn &spawn) {
	auto entity = commands_->spawn();
	if (!entity)
		return std::unexpected(entity.error());

	const auto write = [&](game::CommandStatus status) -> Result<void> {
		if (status == game::CommandStatus::Accepted)
			return {};
		return std::unexpected(projectileCommandError(status));
	};
	const auto abort = [&]() {
		static_cast<void>(commands_->destroy(*entity));
	};

	if (auto result =
	        write(commands_->set(*entity, components_->transform,
	                             Transform{spawn.position, spawn.orientation}));
	    !result) {
		abort();
		return std::unexpected(result.error());
	}
	if (auto result = write(commands_->set(*entity, components_->motion,
	                                       Motion{spawn.velocityPerTick}));
	    !result) {
		abort();
		return std::unexpected(result.error());
	}
	if (auto result =
	        write(commands_->set(*entity, components_->collision,
	                             CircleCollision{spec.collisionRadius}));
	    !result) {
		abort();
		return std::unexpected(result.error());
	}
	if (auto result =
	        write(commands_->set(*entity, components_->relation,
	                             Relation{spawn.owner, spec.faction}));
	    !result) {
		abort();
		return std::unexpected(result.error());
	}
	if (auto result = write(commands_->set(*entity, components_->lifetime,
	                                       ProjectileLifetime{spec.lifetime}));
	    !result) {
		abort();
		return std::unexpected(result.error());
	}
	if (auto result = write(commands_->set(*entity, components_->visual,
	                                       ProjectileVisual{spec.style}));
	    !result) {
		abort();
		return std::unexpected(result.error());
	}
	if (auto result = write(commands_->set(*entity, components_->identity,
	                                       ProjectileIdentity{spec.flags}));
	    !result) {
		abort();
		return std::unexpected(result.error());
	}
	if (auto result = write(commands_->set(*entity, components_->damage,
	                                       DamageSource{spec.damage, true}));
	    !result) {
		abort();
		return std::unexpected(result.error());
	}
	return *entity;
}

} // namespace shiki::stg

#include <shiki/stg/stage/standard_actions.h>

#include <shiki/core/binary_codec.h>
#include <shiki/game_definition.h>
#include <shiki/stg/gameplay_context.h>

#include <limits>

namespace shiki::stg {
namespace {

void writeSpawn(BinaryWriter &writer, const StageSpawn &spawn) {
	writer.writeF32(spawn.position.value.x);
	writer.writeF32(spawn.position.value.y);
	writer.writeF32(spawn.velocityPerTick.x);
	writer.writeF32(spawn.velocityPerTick.y);
	writer.writeF32(spawn.orientation.radians);
}

Result<StageSpawn> readSpawn(BinaryReader &reader) {
	auto x = reader.readF32();
	if (!x)
		return std::unexpected(x.error());
	auto y = reader.readF32();
	if (!y)
		return std::unexpected(y.error());
	auto velocityX = reader.readF32();
	if (!velocityX)
		return std::unexpected(velocityX.error());
	auto velocityY = reader.readF32();
	if (!velocityY)
		return std::unexpected(velocityY.error());
	auto angle = reader.readF32();
	if (!angle)
		return std::unexpected(angle.error());
	return StageSpawn{
	    WorldPosition{{*x, *y}}, {*velocityX, *velocityY}, Angle{*angle}};
}

void writeActorSpec(BinaryWriter &writer, const ActorSpec &spec) {
	writer.writeU64(spec.type.value);
	writer.writeU32(spec.faction.value);
	writer.writeF32(spec.collisionRadius);
	writer.writeI64(spec.health);
	writer.writeBool(spec.controller.has_value());
	if (spec.controller)
		writer.writeU64(spec.controller->key.value);
}

Result<ActorSpec> readActorSpec(BinaryReader &reader) {
	auto type = reader.readU64();
	if (!type)
		return std::unexpected(type.error());
	auto faction = reader.readU32();
	if (!faction)
		return std::unexpected(faction.error());
	auto radius = reader.readF32();
	if (!radius)
		return std::unexpected(radius.error());
	auto health = reader.readI64();
	if (!health)
		return std::unexpected(health.error());
	auto hasController = reader.readBool();
	if (!hasController)
		return std::unexpected(hasController.error());
	std::optional<control::ControllerTypeId> controller;
	if (*hasController) {
		auto controllerType = reader.readU64();
		if (!controllerType)
			return std::unexpected(controllerType.error());
		controller = control::ControllerTypeId{game::TypeKey{*controllerType}};
	}
	return ActorSpec{game::TypeKey{*type}, FactionId{*faction}, *radius,
	                 *health, controller};
}

flow::ActionCodec<SpawnActorAction> spawnActorCodec() {
	return {.encode = [](const SpawnActorAction &action)
	            -> Result<std::vector<std::byte>> {
		        BinaryWriter writer;
		        writeActorSpec(writer, action.spec);
		        writeSpawn(writer, action.spawn);
		        return std::move(writer).finish();
	        },
	        .decode = [](std::span<const std::byte> bytes)
	            -> Result<SpawnActorAction> {
		        BinaryReader reader{bytes};
		        auto spec = readActorSpec(reader);
		        if (!spec)
			        return std::unexpected(spec.error());
		        auto spawn = readSpawn(reader);
		        if (!spawn)
			        return std::unexpected(spawn.error());
		        if (!reader.empty())
			        return std::unexpected(
			            Error{ErrorDomain::Flow, 3,
			                  "SpawnActor payload has trailing bytes"});
		        return SpawnActorAction{*spec, *spawn};
	        }};
}

flow::ActionCodec<SpawnRegisteredActorAction> registeredActorCodec() {
	return {.encode = [](const SpawnRegisteredActorAction &action)
	            -> Result<std::vector<std::byte>> {
		        BinaryWriter writer;
		        writer.writeU64(action.type.key.value);
		        writeSpawn(writer, action.spawn);
		        return std::move(writer).finish();
	        },
	        .decode = [](std::span<const std::byte> bytes)
	            -> Result<SpawnRegisteredActorAction> {
		        BinaryReader reader{bytes};
		        auto type = reader.readU64();
		        if (!type)
			        return std::unexpected(type.error());
		        auto spawn = readSpawn(reader);
		        if (!spawn)
			        return std::unexpected(spawn.error());
		        if (!reader.empty())
			        return std::unexpected(Error{
			            ErrorDomain::Flow, 3,
			            "SpawnRegisteredActor payload has trailing bytes"});
		        return SpawnRegisteredActorAction{
		            ActorTypeId{game::TypeKey{*type}}, *spawn};
	        }};
}

void writeProjectileSpec(BinaryWriter &writer, const ProjectileSpec &spec) {
	writer.writeF32(spec.collisionRadius);
	writer.writeU32(spec.lifetime.value);
	writer.writeU32(spec.style.value);
	writer.writeU32(spec.faction.value);
	writer.writeU32(static_cast<std::uint32_t>(spec.flags));
	writer.writeI64(spec.damage);
}

Result<ProjectileSpec> readProjectileSpec(BinaryReader &reader) {
	auto radius = reader.readF32();
	if (!radius)
		return std::unexpected(radius.error());
	auto lifetime = reader.readU32();
	if (!lifetime)
		return std::unexpected(lifetime.error());
	auto style = reader.readU32();
	if (!style)
		return std::unexpected(style.error());
	auto faction = reader.readU32();
	if (!faction)
		return std::unexpected(faction.error());
	auto flags = reader.readU32();
	if (!flags)
		return std::unexpected(flags.error());
	auto damage = reader.readI64();
	if (!damage)
		return std::unexpected(damage.error());
	return ProjectileSpec{*radius,
	                      TickSpan{*lifetime},
	                      ProjectileStyleId{*style},
	                      FactionId{*faction},
	                      static_cast<ProjectileFlags>(*flags),
	                      *damage};
}

flow::ActionCodec<SpawnProjectileBatchAction> projectileBatchCodec() {
	return {
	    .encode = [](const SpawnProjectileBatchAction &action)
	        -> Result<std::vector<std::byte>> {
		    if (action.projectiles.size() >
		        std::numeric_limits<std::uint32_t>::max())
			    return std::unexpected(Error{ErrorDomain::Flow, 4,
			                                 "Projectile batch is too large"});
		    BinaryWriter writer;
		    writeProjectileSpec(writer, action.spec);
		    writer.writeU32(
		        static_cast<std::uint32_t>(action.projectiles.size()));
		    for (const StageProjectileSpawn &spawn : action.projectiles)
			    writeSpawn(writer, spawn);
		    return std::move(writer).finish();
	    },
	    .decode = [](std::span<const std::byte> bytes)
	        -> Result<SpawnProjectileBatchAction> {
		    BinaryReader reader{bytes};
		    auto spec = readProjectileSpec(reader);
		    if (!spec)
			    return std::unexpected(spec.error());
		    auto count = reader.readU32();
		    if (!count)
			    return std::unexpected(count.error());
		    std::vector<StageProjectileSpawn> projectiles;
		    projectiles.reserve(*count);
		    for (std::uint32_t index = 0; index < *count; ++index) {
			    auto spawn = readSpawn(reader);
			    if (!spawn)
				    return std::unexpected(spawn.error());
			    projectiles.push_back(*spawn);
		    }
		    if (!reader.empty())
			    return std::unexpected(
			        Error{ErrorDomain::Flow, 3,
			              "SpawnProjectileBatch payload has trailing bytes"});
		    return SpawnProjectileBatchAction{*spec, std::move(projectiles)};
	    }};
}

ActorSpawn runtimeSpawn(const StageSpawn &spawn) {
	return ActorSpawn{spawn.position, spawn.velocityPerTick, spawn.orientation,
	                  game::nullEntity};
}

} // namespace

Result<StandardActionSchema>
StandardActions::install(GameDefinition &definition) {
	auto spawnActor = definition.actions().add<SpawnActorAction>(
	    "shiki.action.spawn_actor.v1", 1, spawnActorCodec(),
	    [](GameplayContext &game,
	       const SpawnActorAction &action) -> Result<void> {
		    auto result =
		        game.actors().spawn(action.spec, runtimeSpawn(action.spawn));
		    if (!result)
			    return std::unexpected(result.error());
		    return {};
	    });
	if (!spawnActor)
		return std::unexpected(spawnActor.error());
	auto spawnRegisteredActor =
	    definition.actions().add<SpawnRegisteredActorAction>(
	        "shiki.action.spawn_registered_actor.v1", 1, registeredActorCodec(),
	        [](GameplayContext &game,
	           const SpawnRegisteredActorAction &action) -> Result<void> {
		        auto result = game.actors().spawn(action.type,
		                                          runtimeSpawn(action.spawn));
		        if (!result)
			        return std::unexpected(result.error());
		        return {};
	        });
	if (!spawnRegisteredActor)
		return std::unexpected(spawnRegisteredActor.error());
	auto spawnProjectileBatch =
	    definition.actions().add<SpawnProjectileBatchAction>(
	        "shiki.action.spawn_projectile_batch.v1", 1, projectileBatchCodec(),
	        [](GameplayContext &game,
	           const SpawnProjectileBatchAction &action) -> Result<void> {
		        std::vector<ProjectileSpawn> spawns;
		        spawns.reserve(action.projectiles.size());
		        for (const StageProjectileSpawn &spawn : action.projectiles) {
			        spawns.push_back(
			            ProjectileSpawn{spawn.position, spawn.velocityPerTick,
			                            spawn.orientation, game::nullEntity});
		        }
		        auto result =
		            game.projectiles().spawnBatch(action.spec, spawns);
		        if (!result)
			        return std::unexpected(result.error());
		        return {};
	        });
	if (!spawnProjectileBatch)
		return std::unexpected(spawnProjectileBatch.error());
	return StandardActionSchema{*spawnActor, *spawnRegisteredActor,
	                            *spawnProjectileBatch};
}

} // namespace shiki::stg

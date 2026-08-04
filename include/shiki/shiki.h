#pragma once

/**
 * @file
 * @brief Convenience header for the complete public ShikigamiSTG API.
 *
 * @mainpage ShikigamiSTG API
 *
 * ShikigamiSTG is a C++23 framework for deterministic, Touhou-style shooting
 * games. `shiki::GameDefinition` describes a game, `shiki::Runtime` owns shared
 * services, and `shiki::Session` advances one deterministic game session.
 * Lower-level world, flow, actor, projectile, presentation, and resource APIs
 * remain directly available to applications that need custom behavior.
 *
 * Include `<shiki/shiki.h>` for the complete API or include individual module
 * headers to keep dependencies narrow.
 */

#include <shiki/ecl/ecl_asset.h>
#include <shiki/ecl/ecl_stage_parser.h>

// Core systems
#include <shiki/asset/asset_store.h>
#include <shiki/asset/standard_assets.h>
#include <shiki/asset/structured_assets.h>
#include <shiki/control/controller.h>
#include <shiki/control/external_command.h>
#include <shiki/control/input_frame.h>
#include <shiki/core/binary_codec.h>
#include <shiki/frontend/realtime.h>
#include <shiki/core/result.h>
#include <shiki/core/time.h>
#include <shiki/core/types.h>
#include <shiki/core/units.h>
#include <shiki/flow/action.h>
#include <shiki/flow/condition.h>
#include <shiki/flow/flow_api.h>
#include <shiki/flow/phase.h>
#include <shiki/flow/stage_parser.h>
#include <shiki/flow/stage_program.h>
#include <shiki/game/commands.h>
#include <shiki/game/component.h>
#include <shiki/game/entity.h>
#include <shiki/game/event.h>
#include <shiki/game/event_stream.h>
#include <shiki/game/query.h>
#include <shiki/game/state.h>
#include <shiki/game/system.h>
#include <shiki/game/type_id.h>
#include <shiki/game/type_registry.h>
#include <shiki/game/world.h>
#include <shiki/game/world_view.h>
#include <shiki/game_definition.h>
#include <shiki/presentation/asset_realizer.h>
#include <shiki/presentation/presentation_events.h>
#include <shiki/presentation/presentation_snapshot.h>
#include <shiki/runtime.h>
#include <shiki/session.h>

// Rendering systems
#include <shiki/render/renderer.h>
#include <shiki/render/sprite.h>

// STG systems
#include <shiki/stg/actor/actor.h>
#include <shiki/stg/actor/actor_api.h>
#include <shiki/stg/actor/actor_behavior.h>
#include <shiki/stg/combat/combat.h>
#include <shiki/stg/combat/default_rules.h>
#include <shiki/stg/enemy/enemy.h>
#include <shiki/stg/gameplay_context.h>
#include <shiki/stg/item/item.h>
#include <shiki/stg/pattern/pattern.h>
#include <shiki/stg/pattern/pattern_api.h>
#include <shiki/stg/player/player.h>
#include <shiki/stg/projectile/projectile.h>
#include <shiki/stg/projectile/projectile_api.h>
#include <shiki/stg/projectile/projectile_systems.h>
#include <shiki/stg/score/score.h>
#include <shiki/stg/stage/stage.h>
#include <shiki/stg/stage/standard_actions.h>

// Effect systems
#include <shiki/effect/particle.h>

// UI systems
#include <shiki/ui/ui.h>

// Script systems
#include <shiki/script/script.h>

// Tween systems
#include <shiki/tween/tween.h>

#pragma once

/** @file Component schema and procedural APIs for collectible entities. */

#include <shiki/core/result.h>
#include <shiki/core/time.h>
#include <shiki/game/commands.h>
#include <shiki/game/world_view.h>
#include <shiki/stg/projectile/projectile.h>

#include <compare>
#include <cstdint>
#include <string>

namespace shiki {
class GameDefinition;
}

namespace shiki::stg {

/** Identifies an application-defined collectible kind. */
struct ItemKindId final {
	std::uint32_t value{}; ///< Game-definition-local kind value.
	auto operator<=>(const ItemKindId &) const = default;
};

/** Identifies an application-defined collectible presentation style. */
struct ItemStyleId final {
	std::uint32_t value{}; ///< Presentation style resolved by the frontend.
	auto operator<=>(const ItemStyleId &) const = default;
};

/** Marks an entity as a collectible and selects its game-defined behavior. */
struct ItemIdentity final {
	ItemKindId kind{}; ///< Kind interpreted by collection rules.
};

/** Stores the quantity delivered to game-defined collection rules. */
struct ItemValue final {
	std::int64_t amount{}; ///< Signed application-defined reward quantity.
};

/** Stores the remaining deterministic lifetime of a collectible. */
struct ItemLifetime final {
	TickSpan remaining{}; ///< Fixed ticks before automatic destruction.
};

/** Stores presentation identity without owning renderer resources. */
struct ItemVisual final {
	ItemStyleId style{}; ///< Presentation style resolved by the client.
};

/** Defines data shared by one collectible spawn. */
struct ItemSpec final {
	ItemKindId kind{};           ///< Game-defined collection behavior.
	ItemStyleId style{};         ///< Presentation style.
	std::int64_t value{};        ///< Quantity delivered on collection.
	float collisionRadius{4.0F}; ///< Collection radius in world units.
	TickSpan lifetime{1'800};    ///< Lifetime in fixed simulation ticks.
};

/** Defines placement and motion for one collectible spawn. */
struct ItemSpawn final {
	WorldPosition position{}; ///< Initial world position.
	Vec2 velocityPerTick{};   ///< Initial displacement per fixed tick.
};

/** Holds World registrations used by the standard collectible API. */
struct ItemComponents final {
	game::ComponentToken<Transform> transform;
	game::ComponentToken<Motion> motion;
	game::ComponentToken<CircleCollision> collision;
	game::ComponentToken<ItemIdentity> identity;
	game::ComponentToken<ItemValue> value;
	game::ComponentToken<ItemLifetime> lifetime;
	game::ComponentToken<ItemVisual> visual;

	/** Registers the collectible schema before a Session is created. */
	[[nodiscard]] static Result<ItemComponents>
	registerWith(GameDefinition &definition);
};

/** Records collectible entity creation through a phase-scoped command buffer.
 */
class ItemApi final {
  public:
	/** Creates a non-owning API valid for the current system callback. */
	ItemApi(game::Commands &commands,
	        const ItemComponents &components) noexcept;

	/** Records one collectible and returns its stable pending handle. */
	[[nodiscard]] Result<game::EntityHandle> spawn(const ItemSpec &spec,
	                                               const ItemSpawn &spawn);

  private:
	game::Commands *commands_{};
	const ItemComponents *components_{};
};

/** Installs deterministic movement and lifetime handling for collectibles. */
[[nodiscard]] Result<void> installItemSystems(GameDefinition &definition,
                                              ItemComponents components);

} // namespace shiki::stg

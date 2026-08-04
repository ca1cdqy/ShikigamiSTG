#pragma once

#include <shiki/control/input_frame.h>
#include <shiki/core/result.h>
#include <shiki/core/types.h>
#include <shiki/game/commands.h>
#include <shiki/game/type_id.h>
#include <shiki/game/world_view.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace shiki::control {

/** Identifies a registered source of normalized actor intent. */
struct ControllerTypeId final {
	game::TypeKey key{};
	auto operator<=>(const ControllerTypeId &) const = default;
};

/** Stores normalized movement and game-defined action bits for one actor. */
struct ActorIntent final {
	Vec2 movement{};
	std::uint64_t actions{};

	/** Returns whether one game-defined action bit is active. */
	[[nodiscard]] constexpr bool action(std::uint8_t bit) const noexcept {
		return bit < 64 && (actions & (std::uint64_t{1} << bit)) != 0;
	}
};

/** Gives a Controller read-only input and ownership identity for one tick. */
struct ControlView final {
	game::EntityHandle actor{};
	const InputFrame &input;
};

/** Builds one normalized intent without exposing gameplay mutation APIs. */
class IntentWriter final {
  public:
	/** Replaces the normalized movement vector for this tick. */
	void movement(Vec2 value) noexcept { intent_.movement = value; }

	/** Sets or clears one game-defined action bit. */
	void action(std::uint8_t bit, bool active) noexcept;

	/** Returns the completed value after the Controller callback. */
	[[nodiscard]] ActorIntent finish() const noexcept { return intent_; }

  private:
	ActorIntent intent_{};
};

/** Converts one input source into normalized intent for an arbitrary actor. */
class Controller {
  public:
	virtual ~Controller() = default;
	virtual void update(const ControlView &control, IntentWriter &intent) = 0;
};

/** Stable failures produced by controller registration and construction. */
enum class ControllerError : std::uint32_t {
	EmptyTypeName = 1,
	DuplicateType,
	UnknownType,
	FactoryFailed,
	RegistryFrozen
};

/** Stores Controller factories without owning live controller instances. */
class ControllerRegistry final {
  public:
	using Factory = std::function<std::unique_ptr<Controller>()>;

	/** Registers one stable controller type before Session creation. */
	[[nodiscard]] Result<ControllerTypeId> add(std::string name,
	                                           Factory factory);

	/** Constructs one controller instance from a registered type. */
	[[nodiscard]] Result<std::unique_ptr<Controller>>
	create(ControllerTypeId type) const;

	/** Returns registered persistent names for schema freezing. */
	[[nodiscard]] std::vector<std::string> names() const;
	/** Prevents factory mutation after the owning definition is frozen. */
	void freeze() noexcept { frozen_ = true; }

  private:
	struct Entry final {
		std::string name;
		ControllerTypeId type{};
		Factory factory;
	};
	std::vector<Entry> entries_;
	bool frozen_{};
};

/** Owns live controllers independently from Actor behavior objects. */
class ControllerPool final {
  public:
	/** Binds a controller instance to an actor handle. */
	void attach(game::EntityHandle actor,
	            std::unique_ptr<Controller> controller);

	/** Writes one deterministic ActorIntent component for every active binding.
	 */
	void dispatch(const game::WorldView &world, game::Commands &commands,
	              game::ComponentToken<ActorIntent> intentToken,
	              const InputFrame &input);

	/** Returns the current tick intent for an actor, if one was produced. */
	[[nodiscard]] const ActorIntent *
	intent(game::EntityHandle actor) const noexcept;

  private:
	struct Entry final {
		game::EntityHandle actor{};
		std::unique_ptr<Controller> controller;
		ActorIntent currentIntent{};
		bool hasIntent{};
	};
	std::vector<Entry> entries_;
};

} // namespace shiki::control

#pragma once

#include <shiki/flow/stage_parser.h>
#include <shiki/stg/gameplay_context.h>

#include <cstdint>
#include <functional>

namespace shiki::ecl {

struct ECLFile;

/** Serializable TH06 timeline request for enemy subroutine creation. */
struct EclSpawnEnemyAction final {
	std::uint32_t opcode{};
	std::int32_t subroutine{};
	float x{};
	float y{};
	float z{};
	std::int32_t life{-1};
	std::int32_t itemDrop{-1};
	std::int32_t score{-1};
};

/** Serializable TH06 timeline request for dialogue startup. */
struct EclStartDialogueAction final {
	std::int32_t messageId{};
};

/** Serializable TH06 timeline request for player power mutation. */
struct EclSetPlayerPowerAction final {
	std::uint32_t value{};
};

/** Waits until the game reports that no stage dialogue is active. */
struct EclDialogueCompleteCondition final {};

/** Waits until a TH06 boss interrupt has been accepted. */
struct EclBossInterruptCondition final {
	std::int32_t bossId{};
	std::int32_t interruptId{};
};

/** Waits until the selected TH06 boss slot is no longer alive. */
struct EclBossDefeatedCondition final {
	std::int32_t bossId{};
};

/** Game-owned behavior attached to portable TH06 timeline payloads. */
struct EclStageHandlers final {
	std::function<Result<void>(stg::GameplayContext &,
	                           const EclSpawnEnemyAction &)>
	    spawnEnemy;
	std::function<Result<void>(stg::GameplayContext &,
	                           const EclStartDialogueAction &)>
	    startDialogue;
	std::function<Result<void>(stg::GameplayContext &,
	                           const EclSetPlayerPowerAction &)>
	    setPlayerPower;
	std::function<Result<bool>(const stg::GameplayContext &,
	                           const EclDialogueCompleteCondition &)>
	    dialogueComplete;
	std::function<Result<bool>(const stg::GameplayContext &,
	                           const EclBossInterruptCondition &)>
	    bossInterruptAccepted;
	std::function<Result<bool>(const stg::GameplayContext &,
	                           const EclBossDefeatedCondition &)>
	    bossDefeated;
};

/** Typed registry capabilities required to compile a TH06 ECL timeline. */
struct EclStageTokens final {
	flow::ActionToken<EclSpawnEnemyAction> spawnEnemy;
	flow::ActionToken<EclStartDialogueAction> startDialogue;
	flow::ActionToken<EclSetPlayerPowerAction> setPlayerPower;
	flow::ConditionToken<EclDialogueCompleteCondition> dialogueComplete;
	flow::ConditionToken<EclBossInterruptCondition> bossInterruptAccepted;
	flow::ConditionToken<EclBossDefeatedCondition> bossDefeated;
};

/** Stable failures produced by the TH06 ECL compatibility boundary. */
enum class EclStageError : std::uint32_t {
	InvalidHandlers = 1,
	MissingTimeline,
	UnsupportedOpcode,
	InvalidParameters,
	NonMonotonicTime,
	EmptyTimeline
};

/** Registers portable TH06 ECL actions and conditions with game handlers. */
[[nodiscard]] Result<EclStageTokens>
registerEclStageCompatibility(flow::ActionRegistry &actions,
                              flow::ConditionRegistry &conditions,
                              EclStageHandlers handlers);

/** Compiles TH06 timeline zero from memory into a StageProgram. */
class EclStageParser final : public flow::StageParser {
  public:
	/** Creates a parser bound to tokens from the same game definition. */
	explicit EclStageParser(EclStageTokens tokens) noexcept : tokens_(tokens) {}

	[[nodiscard]] Result<flow::StageProgram>
	parse(const flow::StageSource &source,
	      const flow::StageParseContext &context) override;

  private:
	EclStageTokens tokens_;
};

/** Compiles a previously decoded TH06 ECL syntax asset into stage flow. */
[[nodiscard]] Result<flow::StageProgram>
compileEclStage(const ECLFile &file, EclStageTokens tokens,
                const flow::StageParseContext &context);

} // namespace shiki::ecl

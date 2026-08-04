#include <shiki/ecl/ecl_executor.h>

#include <shiki/ecl/ecl_stage_parser.h>
#include <shiki/game_definition.h>
#include <shiki/session.h>

#include <spdlog/spdlog.h>

#include <memory>
#include <random>
#include <utility>

namespace shiki::ecl {

ECLEngine::ECLEngine() = default;
ECLEngine::~ECLEngine() = default;
ECLEngine::ECLEngine(ECLEngine &&) noexcept = default;
ECLEngine &ECLEngine::operator=(ECLEngine &&) noexcept = default;

void ECLEngine::initialize() {
	accumulator_ = 0.0;
	nextTick_ = 0;
	random_.seed(0);
	running_ = false;
}

void ECLEngine::shutdown() {
	stop();
	session_.reset();
	parser_.clear();
}

void ECLEngine::start() {
	random_.seed(0);
	running_ = session_ != nullptr;
}
void ECLEngine::stop() { running_ = false; }

Result<void> ECLEngine::loadECLAsset(asset::AssetStore &assets,
                                     asset::AssetId id) {
	auto loaded = assets.load<ECLFile>(id);
	if (!loaded)
		return std::unexpected(loaded.error());
	parser_.setFile(**loaded);
	return buildRuntime();
}

bool ECLEngine::loadECLFile(const std::string &filePath,
                            std::uint32_t version) {
	if (!parser_.parseFile(filePath, version))
		return false;
	auto built = buildRuntime();
	if (!built) {
		spdlog::error("Failed to compile ECL StageProgram: {}",
		              built.error().message);
		return false;
	}
	return true;
}

Result<void> ECLEngine::buildRuntime() {
	GameDefinition definition;
	auto tokens = registerEclStageCompatibility(
	    definition.actions(), definition.conditions(),
	    EclStageHandlers{
	        .spawnEnemy =
	            [this](stg::GameplayContext &,
	                   const EclSpawnEnemyAction &action) -> Result<void> {
		        if (enemySpawnCallback_) {
			        EnemySpawnParams spawn{
			            action.subroutine,
			            action.x,
			            action.y,
			            action.z,
			            action.life,
			            action.itemDrop,
			            action.score,
			            action.opcode == 2 || action.opcode == 3 ||
			                action.opcode == 6 || action.opcode == 7};
			        if (action.opcode >= 4) {
				        const auto randomRange = [this](float range) {
					        return std::generate_canonical<float, 24>(random_) *
					               range;
				        };
				        if (spawn.x <= -990.0F)
					        spawn.x = randomRange(384.0F);
				        if (spawn.y <= -990.0F)
					        spawn.y = randomRange(448.0F);
				        if (spawn.z <= -990.0F)
					        spawn.z = randomRange(800.0F);
			        }
			        enemySpawnCallback_(spawn);
		        }
		        return {};
	        },
	        .startDialogue =
	            [this](stg::GameplayContext &,
	                   const EclStartDialogueAction &action) -> Result<void> {
		        if (dialogueStartCallback_)
			        dialogueStartCallback_(action.messageId);
		        return {};
	        },
	        .setPlayerPower = [](stg::GameplayContext &,
	                             const EclSetPlayerPowerAction &)
	            -> Result<void> { return {}; },
	        .dialogueComplete =
	            [this](const stg::GameplayContext &,
	                   const EclDialogueCompleteCondition &) -> Result<bool> {
		        return !dialogueActiveCallback_ || !dialogueActiveCallback_();
	        },
	        .bossInterruptAccepted = [this](const stg::GameplayContext &,
	                                        const EclBossInterruptCondition
	                                            &condition) -> Result<bool> {
		        return bossInterruptCallback_ &&
		               bossInterruptCallback_(condition.bossId,
		                                      condition.interruptId);
	        },
	        .bossDefeated = [this](const stg::GameplayContext &,
	                               const EclBossDefeatedCondition &condition)
	            -> Result<bool> {
		        return !bossAliveCallback_ ||
		               !bossAliveCallback_(condition.bossId);
	        }});
	if (!tokens)
		return std::unexpected(tokens.error());

	auto program = compileEclStage(
	    parser_.getFile(), *tokens,
	    flow::StageParseContext{definition.actions(), definition.conditions()});
	if (!program)
		return std::unexpected(program.error());
	auto sharedProgram =
	    std::make_shared<const flow::StageProgram>(std::move(*program));
	auto session = Session::create(
	    std::move(definition),
	    SessionConfig{.tickRate = TickRate{60}, .randomSeed = 0});
	if (!session)
		return std::unexpected(session.error());
	auto started = (*session)->startStage(std::move(sharedProgram));
	if (!started)
		return std::unexpected(started.error());
	session_ = std::move(*session);
	accumulator_ = 0.0;
	nextTick_ = 1;
	return {};
}

void ECLEngine::update(float dt) {
	if (!running_ || !session_ || dt <= 0.0F)
		return;
	accumulator_ += static_cast<double>(dt);
	constexpr double tickDuration = 1.0 / 60.0;
	while (accumulator_ + 1.0e-9 >= tickDuration) {
		accumulator_ -= tickDuration;
		auto stepped =
		    session_->step(control::InputFrame{.tick = Tick{nextTick_}});
		if (!stepped) {
			spdlog::error("ECL StageProgram step failed: {}",
			              stepped.error().message);
			running_ = false;
			return;
		}
		++nextTick_;
		if (stepped->state != SessionState::Running) {
			running_ = false;
			return;
		}
	}
}

} // namespace shiki::ecl

#pragma once

#include <shiki/asset/asset_store.h>
#include <shiki/core/result.h>
#include <shiki/ecl/ecl_parser.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <random>

namespace shiki {
class Session;
}

namespace shiki::ecl {

/** Parameters exported by a TH06 timeline enemy-create action. */
struct EnemySpawnParams final {
	std::int32_t subId{};
	float x{};
	float y{};
	float z{};
	std::int32_t life{-1};
	std::int32_t itemDrop{-1};
	std::int32_t score{-1};
	bool invertX{};
};

using EnemySpawnCallback = std::function<void(const EnemySpawnParams &)>;
using BossInterruptCallback = std::function<bool(std::int32_t, std::int32_t)>;
using BossAliveCallback = std::function<bool(std::int32_t)>;
using DialogueStartCallback = std::function<void(std::int32_t)>;
using DialogueActiveCallback = std::function<bool()>;

/**
 * Temporary TH06 frontend adapter backed by StageProgram and Session.
 *
 * New games should register ECL stage handlers directly and own Session. This
 * facade exists only while the TH06 example callbacks are migrated to Actor
 * and presentation events.
 */
class ECLEngine final {
  public:
	ECLEngine();
	~ECLEngine();
	ECLEngine(const ECLEngine &) = delete;
	ECLEngine &operator=(const ECLEngine &) = delete;
	ECLEngine(ECLEngine &&) noexcept;
	ECLEngine &operator=(ECLEngine &&) noexcept;

	/** Resets transient timing state. */
	void initialize();
	/** Releases the headless Session and decoded syntax. */
	void shutdown();
	/** Enables fixed-tick stage execution. */
	void start();
	/** Suspends fixed-tick stage execution. */
	void stop();

	/** Loads the retained proprietary ECL format through AssetStore. */
	[[nodiscard]] Result<void> loadECLAsset(asset::AssetStore &assets,
	                                        asset::AssetId id);
	/** Legacy filesystem adapter retained for behavior tests. */
	[[nodiscard]] bool loadECLFile(const std::string &filePath,
	                               std::uint32_t version = 6);

	/** Advances the headless stage Session using a real-time delta. */
	void update(float dt);

	[[nodiscard]] const ECLParser &getParser() const noexcept {
		return parser_;
	}

	void setEnemySpawnCallback(EnemySpawnCallback callback) {
		enemySpawnCallback_ = std::move(callback);
	}
	void setBossInterruptCallback(BossInterruptCallback callback) {
		bossInterruptCallback_ = std::move(callback);
	}
	void setBossAliveCallback(BossAliveCallback callback) {
		bossAliveCallback_ = std::move(callback);
	}
	void setDialogueStartCallback(DialogueStartCallback callback) {
		dialogueStartCallback_ = std::move(callback);
	}
	void setDialogueActiveCallback(DialogueActiveCallback callback) {
		dialogueActiveCallback_ = std::move(callback);
	}

  private:
	[[nodiscard]] Result<void> buildRuntime();

	ECLParser parser_;
	std::unique_ptr<Session> session_;
	EnemySpawnCallback enemySpawnCallback_;
	BossInterruptCallback bossInterruptCallback_;
	BossAliveCallback bossAliveCallback_;
	DialogueStartCallback dialogueStartCallback_;
	DialogueActiveCallback dialogueActiveCallback_;
	std::mt19937 random_{0};
	double accumulator_{};
	std::uint64_t nextTick_{};
	bool running_{};
};

} // namespace shiki::ecl

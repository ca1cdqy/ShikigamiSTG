#pragma once

#include <functional>
#include <memory>
#include <shiki/core/types.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace shiki {

/** Runtime type tag for ScriptValue. */
enum class ScriptValueType {
	Null,
	Integer,
	Float,
	String,
	Boolean,
	Vec2,
	Color
};

/** Dynamically typed value accepted by the lightweight command parser. */
struct ScriptValue {
	ScriptValueType type =
	    ScriptValueType::Null; ///< Active value representation.
	union {
		int intValue;     ///< Integer storage when type is Integer.
		float floatValue; ///< Floating-point storage when type is Float.
		bool boolValue;   ///< Boolean storage when type is Boolean.
	};
	std::string stringValue; ///< String storage when type is String.
	Vec2 vec2Value;          ///< Vector storage when type is Vec2.
	uint32_t colorValue = 0; ///< Packed RGBA storage when type is Color.

	/** Creates a null value. */
	ScriptValue() : intValue(0) {}
	/** Destroys the value and owned string storage. */
	~ScriptValue() {}

	/** Creates an integer value. */
	explicit ScriptValue(int value)
	    : type(ScriptValueType::Integer), intValue(value) {}
	/** Creates a floating-point value. */
	explicit ScriptValue(float value)
	    : type(ScriptValueType::Float), floatValue(value) {}
	/** Creates a string value by copying value. */
	explicit ScriptValue(const std::string &value)
	    : type(ScriptValueType::String), intValue(0), stringValue(value) {}
	/** Creates a Boolean value. */
	explicit ScriptValue(bool value)
	    : type(ScriptValueType::Boolean), boolValue(value) {}
	/** Creates a two-dimensional vector value. */
	explicit ScriptValue(const Vec2 &value)
	    : type(ScriptValueType::Vec2), intValue(0), vec2Value(value) {}
	/** Creates a packed RGBA color value. */
	explicit ScriptValue(uint32_t value)
	    : type(ScriptValueType::Color), intValue(0), colorValue(value) {}
};

/** Parsed command invocation and its timing metadata. */
struct ScriptCommand {
	std::string name; ///< Command identifier passed to the executor callback.
	std::unordered_map<std::string, ScriptValue>
	    parameters;        ///< Named arguments.
	float delay = 0.0f;    ///< Delay from the previous command in seconds.
	float duration = 0.0f; ///< Optional command duration in seconds.
};

/** Named, time-bounded sequence of parsed commands. */
struct ScriptSection {
	std::string name;                    ///< Section identifier.
	std::vector<ScriptCommand> commands; ///< Commands in source order.
	float startTime = 0.0f;              ///< Inclusive start time in seconds.
	float endTime = 0.0f;                ///< Calculated end time in seconds.
};

/** Parses the legacy text command format into sections and variables. */
class ScriptParser {
  public:
	/** Creates an empty parser. */
	ScriptParser() = default;
	/** Releases parsed sections and variables. */
	~ScriptParser() = default;

	/** Parsers cannot be copied. */
	ScriptParser(const ScriptParser &) = delete;
	/** Parsers cannot be copy-assigned. */
	ScriptParser &operator=(const ScriptParser &) = delete;

	/** Transfers parsed data. */
	ScriptParser(ScriptParser &&) noexcept = default;
	/** Replaces parsed data with moved state. */
	ScriptParser &operator=(ScriptParser &&) noexcept = default;

	/** Parses script text, replacing previous parser state on success. */
	bool parse(const std::string &script);
	/** Reads and parses a UTF-8 script file. */
	bool parseFile(const std::string &filePath);

	/** Returns parsed sections in source order. */
	[[nodiscard]] const std::vector<ScriptSection> &getSections() const {
		return sections_;
	}
	/** Returns the current variable table. */
	[[nodiscard]] const std::unordered_map<std::string, ScriptValue> &
	getVariables() const {
		return variables_;
	}

	/** Returns a variable value or a null ScriptValue when absent. */
	[[nodiscard]] ScriptValue getVariable(const std::string &name) const;
	/** Inserts or replaces a variable. */
	void setVariable(const std::string &name, const ScriptValue &value);

	/** Removes all parsed sections and variables. */
	void clear();

  private:
	std::vector<ScriptSection> sections_;
	std::unordered_map<std::string, ScriptValue> variables_;

	/// Internal helpers
	bool parseLine(const std::string &line, ScriptCommand &command);
	ScriptValue parseValue(const std::string &valueStr);
	std::string trim(const std::string &str);
	std::vector<std::string> split(const std::string &str, char delimiter);
};

/**
 * Advances one parsed legacy command script and dispatches ready commands.
 *
 * @determinism Determinism depends on caller-supplied dt and callback behavior;
 * this convenience executor does not enforce fixed ticks.
 * @thread_safety Not thread-safe.
 */
class ScriptExecutor {
  public:
	/** Creates an executor without a loaded script. */
	ScriptExecutor() = default;
	/** Releases parsed script state. */
	~ScriptExecutor() = default;

	/** Executors cannot be copied. */
	ScriptExecutor(const ScriptExecutor &) = delete;
	/** Executors cannot be copy-assigned. */
	ScriptExecutor &operator=(const ScriptExecutor &) = delete;

	/** Transfers parser, playback state, and callback. */
	ScriptExecutor(ScriptExecutor &&) noexcept = default;
	/** Replaces this executor with moved state. */
	ScriptExecutor &operator=(ScriptExecutor &&) noexcept = default;

	/** Parses and loads script text, resetting playback state. */
	bool loadScript(const std::string &script);
	/** Reads and loads a UTF-8 script file, resetting playback state. */
	bool loadScriptFile(const std::string &filePath);

	/** Advances playback by dt seconds and dispatches ready commands. */
	void update(float dt);

	/** Starts playback from the current time. */
	void start();
	/** Suspends playback without changing the current time. */
	void pause() { isRunning_ = false; }
	/** Resumes playback from the current time. */
	void resume() { isRunning_ = true; }
	/** Stops playback and marks the executor inactive. */
	void stop();
	/** Resets time and command cursors, then starts playback. */
	void restart();

	/** Reports whether update() dispatches commands. */
	[[nodiscard]] bool isRunning() const { return isRunning_; }
	/** Reports whether every loaded command has completed. */
	[[nodiscard]] bool isCompleted() const { return isCompleted_; }
	/** Returns elapsed playback time in seconds. */
	[[nodiscard]] float getCurrentTime() const { return currentTime_; }

	/** Callback invoked with a borrowed command during update(). */
	using CommandCallback = std::function<void(const ScriptCommand &)>;
	/** Replaces the command dispatch callback. */
	void setCommandCallback(CommandCallback callback) {
		commandCallback_ = std::move(callback);
	}

  private:
	ScriptParser parser_;
	float currentTime_ = 0.0f;
	bool isRunning_ = false;
	bool isCompleted_ = false;
	size_t currentSection_ = 0;
	size_t currentCommand_ = 0;

	CommandCallback commandCallback_;

	/// Internal helpers
	void executeCommand(const ScriptCommand &command);
};

/** Owns named legacy ScriptExecutor instances. */
class ScriptSystem {
  public:
	/** Creates an empty script registry. */
	ScriptSystem() = default;
	/** Destroys every registered executor. */
	~ScriptSystem() = default;

	/** Script systems cannot be copied because they own executors. */
	ScriptSystem(const ScriptSystem &) = delete;
	/** Script systems cannot be copy-assigned. */
	ScriptSystem &operator=(const ScriptSystem &) = delete;

	/** Transfers executor ownership. */
	ScriptSystem(ScriptSystem &&) noexcept = default;
	/** Replaces this system by moving executor ownership. */
	ScriptSystem &operator=(ScriptSystem &&) noexcept = default;

	/** Initializes registry state. */
	void initialize();
	/** Stops and removes every executor. */
	void shutdown();

	/** Parses text and stores an executor under name. */
	bool loadScript(const std::string &name, const std::string &script);
	/** Loads a UTF-8 file and stores an executor under name. */
	bool loadScriptFile(const std::string &name, const std::string &filePath);

	/** Starts the named executor when it exists. */
	void execute(const std::string &name);
	/** Advances every registered executor by dt seconds. */
	void update(float dt);

	/** Returns a non-owning executor pointer, or null when name is absent. */
	[[nodiscard]] ScriptExecutor *getExecutor(const std::string &name);

	/** Removes and destroys every executor. */
	void clear();

  private:
	std::unordered_map<std::string, std::unique_ptr<ScriptExecutor>> executors_;
};

} // namespace shiki

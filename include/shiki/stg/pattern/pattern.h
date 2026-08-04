#pragma once

#include <shiki/core/result.h>
#include <shiki/game/type_id.h>

#include <concepts>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace shiki::stg {

class GameplayContext;

/** Reports whether a stateful Pattern needs another simulation tick. */
enum class PatternStatus : std::uint8_t { Running, Complete };

/** Stateful producer advanced by the Session pattern pool. */
class Pattern {
  public:
	virtual ~Pattern() = default;
	/** Advances the pattern once using phase-scoped gameplay APIs. */
	[[nodiscard]] virtual Result<PatternStatus> tick(GameplayContext &game) = 0;
};

template <class T>
concept PatternArguments = std::movable<T> && std::destructible<T>;

template <class T> inline const char patternArgumentTypeTag = 0;

/** A typed persistent reference to one registered Pattern factory. */
template <PatternArguments T> class PatternHandle final {
  public:
	/** Returns the persistent pattern identity. */
	[[nodiscard]] constexpr game::TypeKey key() const noexcept { return key_; }
	[[nodiscard]] constexpr std::uint32_t version() const noexcept {
		return version_;
	}
	[[nodiscard]] constexpr explicit operator bool() const noexcept {
		return typeTag_ != nullptr;
	}

  private:
	constexpr PatternHandle(game::TypeKey key, std::uint32_t version,
	                        const void *typeTag) noexcept
	    : key_(key), version_(version), typeTag_(typeTag) {}
	game::TypeKey key_{};
	std::uint32_t version_{1};
	const void *typeTag_{};
	friend class PatternRegistry;
	friend class PatternInvocation;
};

/** Stores one immutable typed Pattern construction request. */
class PatternInvocation final {
  public:
	/** Creates a request from one registered handle and argument value. */
	template <PatternArguments T>
	[[nodiscard]] static PatternInvocation create(PatternHandle<T> handle,
	                                              T arguments) {
		return PatternInvocation{
		    handle.key_, handle.version_, &patternArgumentTypeTag<T>,
		    std::make_shared<const T>(std::move(arguments))};
	}

  private:
	PatternInvocation(game::TypeKey type, std::uint32_t version,
	                  const void *typeTag,
	                  std::shared_ptr<const void> arguments) noexcept
	    : type_(type), version_(version), typeTag_(typeTag),
	      arguments_(std::move(arguments)) {}
	game::TypeKey type_{};
	std::uint32_t version_{1};
	const void *typeTag_{};
	std::shared_ptr<const void> arguments_;
	friend class PatternRegistry;
};

/** Stable failures produced by Pattern registration and construction. */
enum class PatternError : std::uint32_t {
	EmptyName = 1,
	DuplicateType,
	UnknownType,
	ArgumentMismatch,
	InvalidFactory,
	FactoryFailed,
	RegistryFrozen,
	InstanceExhausted
};

/** Persistent Pattern metadata included in the frozen game schema. */
struct PatternRegistration final {
	std::string name;
	std::uint32_t version{1};
};

/** Stores typed Pattern factories without owning live instances. */
class PatternRegistry final {
  public:
	/** Registers one typed Pattern factory before definition freeze. */
	template <PatternArguments T>
	[[nodiscard]] Result<PatternHandle<T>>
	add(std::string name, std::uint32_t version,
	    std::function<Result<std::unique_ptr<Pattern>>(const T &)> factory);

	/** Constructs one live instance from a validated invocation. */
	[[nodiscard]] Result<std::unique_ptr<Pattern>>
	create(const PatternInvocation &invocation) const;

	/** Returns persistent names for schema freezing. */
	[[nodiscard]] std::vector<std::string> names() const;
	[[nodiscard]] std::vector<PatternRegistration> registrations() const;

	/** Prevents factory mutation after definition freeze. */
	void freeze() noexcept { frozen_ = true; }

  private:
	struct Entry final {
		std::string name;
		game::TypeKey type{};
		std::uint32_t version{};
		const void *typeTag{};
		std::function<Result<std::unique_ptr<Pattern>>(const void *)> create;
	};
	std::vector<Entry> entries_;
	bool frozen_{};
};

template <PatternArguments T>
Result<PatternHandle<T>> PatternRegistry::add(
    std::string name, std::uint32_t version,
    std::function<Result<std::unique_ptr<Pattern>>(const T &)> factory) {
	if (frozen_)
		return std::unexpected(
		    Error{ErrorDomain::Definition,
		          static_cast<std::uint32_t>(PatternError::RegistryFrozen),
		          "Pattern registry cannot change after definition freeze"});
	if (name.empty())
		return std::unexpected(
		    Error{ErrorDomain::Definition,
		          static_cast<std::uint32_t>(PatternError::EmptyName),
		          "Pattern name cannot be empty"});
	if (!factory)
		return std::unexpected(
		    Error{ErrorDomain::Definition,
		          static_cast<std::uint32_t>(PatternError::InvalidFactory),
		          "Pattern factory cannot be empty"});
	const game::TypeKey key = game::typeKeyFromName(name);
	for (const Entry &entry : entries_) {
		if (entry.type == key)
			return std::unexpected(
			    Error{ErrorDomain::Definition,
			          static_cast<std::uint32_t>(PatternError::DuplicateType),
			          "Pattern name or persistent key is already registered"});
	}
	entries_.push_back(
	    Entry{std::move(name), key, version, &patternArgumentTypeTag<T>,
	          [factory = std::move(factory)](const void *arguments) {
		          return factory(*static_cast<const T *>(arguments));
	          }});
	return PatternHandle<T>{key, version, &patternArgumentTypeTag<T>};
}

} // namespace shiki::stg

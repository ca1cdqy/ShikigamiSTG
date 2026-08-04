#pragma once

#include <shiki/core/result.h>
#include <shiki/flow/action.h>
#include <shiki/game/type_id.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace shiki::stg {
class GameplayContext;
}

namespace shiki::flow {

template <class T> inline const char conditionPayloadTypeTag = 0;

/** A typed persistent reference to one registered Flow condition. */
template <ActionPayload T> class ConditionToken final {
  public:
	[[nodiscard]] constexpr game::TypeKey key() const noexcept { return key_; }
	[[nodiscard]] constexpr std::uint32_t version() const noexcept {
		return version_;
	}
	[[nodiscard]] constexpr explicit operator bool() const noexcept {
		return typeTag_ != nullptr;
	}

  private:
	constexpr ConditionToken(game::TypeKey key, std::uint32_t version,
	                         const void *typeTag) noexcept
	    : key_(key), version_(version), typeTag_(typeTag) {}
	game::TypeKey key_{};
	std::uint32_t version_{1};
	const void *typeTag_{};
	friend class ConditionRegistry;
};

/** Owns one encoded immutable condition invocation during Program building. */
class GameplayCondition final {
  public:
	GameplayCondition() = default;
	[[nodiscard]] game::TypeKey type() const noexcept { return type_; }
	[[nodiscard]] std::uint32_t version() const noexcept { return version_; }
	[[nodiscard]] std::span<const std::byte> payload() const noexcept {
		return payload_;
	}

  private:
	GameplayCondition(game::TypeKey type, std::uint32_t version,
	                  std::vector<std::byte> payload) noexcept
	    : type_(type), version_(version), payload_(std::move(payload)) {}
	game::TypeKey type_{};
	std::uint32_t version_{1};
	std::vector<std::byte> payload_;
	friend class ConditionRegistry;
};

/** Persistent condition metadata included in the frozen game schema. */
struct ConditionRegistration final {
	std::string name;
	std::uint32_t version{1};
};

/** Stable failures produced by condition registration and evaluation. */
enum class ConditionError : std::uint32_t {
	EmptyName = 1,    ///< The registration name was empty.
	DuplicateType,    ///< The name or key was already registered.
	UnknownType,      ///< No condition was found for the given key.
	PayloadMismatch,  ///< The token type tag did not match the registered entry.
	InvalidHandler,   ///< The evaluation callback was null.
	RegistryFrozen,   ///< Mutation was attempted after definition freeze.
	InvalidCodec,     ///< The codec was missing an encoder or decoder.
	VersionMismatch   ///< The payload version differed from the registered version.
};

/** Stores typed deterministic Flow condition codecs and handlers. */
class ConditionRegistry final {
  public:
	template <ActionPayload T>
	[[nodiscard]] Result<ConditionToken<T>>
	add(std::string name, std::uint32_t version, ActionCodec<T> codec,
	    std::function<Result<bool>(const stg::GameplayContext &, const T &)>
	        handler);

	template <ActionPayload T>
	[[nodiscard]] Result<GameplayCondition> make(ConditionToken<T> token,
	                                             const T &payload) const;

	/** Evaluates one immutable Program condition without gameplay mutation. */
	[[nodiscard]] Result<bool> evaluate(game::TypeKey type,
	                                    std::uint32_t version,
	                                    std::span<const std::byte> payload,
	                                    const stg::GameplayContext &game) const;

	[[nodiscard]] std::vector<ConditionRegistration> registrations() const;
	void freeze() noexcept { frozen_ = true; }

  private:
	struct Entry final {
		std::string name;
		game::TypeKey type{};
		std::uint32_t version{};
		const void *typeTag{};
		std::function<Result<std::vector<std::byte>>(const void *)> encode;
		std::function<Result<bool>(const stg::GameplayContext &,
		                           std::span<const std::byte>)>
		    evaluate;
	};
	std::vector<Entry> entries_;
	bool frozen_{};
};

template <ActionPayload T>
Result<ConditionToken<T>> ConditionRegistry::add(
    std::string name, std::uint32_t version, ActionCodec<T> codec,
    std::function<Result<bool>(const stg::GameplayContext &, const T &)>
        handler) {
	if (frozen_)
		return std::unexpected(
		    Error{ErrorDomain::Definition,
		          static_cast<std::uint32_t>(ConditionError::RegistryFrozen),
		          "Condition registry is frozen"});
	if (name.empty())
		return std::unexpected(
		    Error{ErrorDomain::Definition,
		          static_cast<std::uint32_t>(ConditionError::EmptyName),
		          "Condition name cannot be empty"});
	if (!codec.encode || !codec.decode)
		return std::unexpected(
		    Error{ErrorDomain::Definition,
		          static_cast<std::uint32_t>(ConditionError::InvalidCodec),
		          "Condition codec must encode and decode"});
	if (!handler)
		return std::unexpected(
		    Error{ErrorDomain::Definition,
		          static_cast<std::uint32_t>(ConditionError::InvalidHandler),
		          "Condition handler cannot be empty"});
	const game::TypeKey key = game::typeKeyFromName(name);
	for (const Entry &entry : entries_)
		if (entry.type == key)
			return std::unexpected(
			    Error{ErrorDomain::Definition,
			          static_cast<std::uint32_t>(ConditionError::DuplicateType),
			          "Condition identity is already registered"});
	entries_.push_back(
	    Entry{std::move(name), key, version, &conditionPayloadTypeTag<T>,
	          [encode = codec.encode](const void *payload) {
		          return encode(*static_cast<const T *>(payload));
	          },
	          [decode = std::move(codec.decode), handler = std::move(handler)](
	              const stg::GameplayContext &game,
	              std::span<const std::byte> payload) -> Result<bool> {
		          auto value = decode(payload);
		          if (!value)
			          return std::unexpected(value.error());
		          return handler(game, *value);
	          }});
	return ConditionToken<T>{key, version, &conditionPayloadTypeTag<T>};
}

template <ActionPayload T>
Result<GameplayCondition> ConditionRegistry::make(ConditionToken<T> token,
                                                  const T &payload) const {
	for (const Entry &entry : entries_) {
		if (entry.type != token.key_)
			continue;
		if (entry.version != token.version_ ||
		    entry.typeTag != &conditionPayloadTypeTag<T>)
			return std::unexpected(Error{
			    ErrorDomain::Definition,
			    static_cast<std::uint32_t>(ConditionError::PayloadMismatch),
			    "Condition token does not match its registered payload"});
		auto bytes = entry.encode(&payload);
		if (!bytes)
			return std::unexpected(bytes.error());
		return GameplayCondition{entry.type, entry.version, std::move(*bytes)};
	}
	return std::unexpected(
	    Error{ErrorDomain::Definition,
	          static_cast<std::uint32_t>(ConditionError::UnknownType),
	          "Condition type is not registered"});
}

} // namespace shiki::flow

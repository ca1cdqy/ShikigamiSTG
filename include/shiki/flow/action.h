#pragma once

#include <shiki/core/result.h>
#include <shiki/game/type_id.h>

#include <concepts>
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

template <class T>
concept ActionPayload = std::movable<T> && std::destructible<T>;

template <class T> inline const char actionPayloadTypeTag = 0;

/** Encodes and decodes one stable StageProgram action payload version. */
template <ActionPayload T> struct ActionCodec final {
	std::function<Result<std::vector<std::byte>>(const T &)> encode;
	std::function<Result<T>(std::span<const std::byte>)> decode;
};

/** Persistent action metadata included in the frozen game schema. */
struct ActionRegistration final {
	std::string name;
	std::uint32_t version{1};
};

/** A typed persistent reference to one registered stage action. */
template <ActionPayload T> class ActionToken final {
  public:
	[[nodiscard]] constexpr game::TypeKey key() const noexcept { return key_; }
	[[nodiscard]] constexpr std::uint32_t version() const noexcept {
		return version_;
	}
	[[nodiscard]] constexpr explicit operator bool() const noexcept {
		return typeTag_ != nullptr;
	}

  private:
	constexpr ActionToken(game::TypeKey key, std::uint32_t version,
	                      const void *typeTag) noexcept
	    : key_(key), version_(version), typeTag_(typeTag) {}
	game::TypeKey key_{};
	std::uint32_t version_{1};
	const void *typeTag_{};
	friend class ActionRegistry;
};

/** Owns one encoded immutable action invocation during Program building. */
class GameplayAction final {
  public:
	GameplayAction() = default;
	[[nodiscard]] game::TypeKey type() const noexcept { return type_; }
	[[nodiscard]] std::uint32_t version() const noexcept { return version_; }
	[[nodiscard]] std::span<const std::byte> payload() const noexcept {
		return payload_;
	}

  private:
	GameplayAction(game::TypeKey type, std::uint32_t version,
	               std::vector<std::byte> payload) noexcept
	    : type_(type), version_(version), payload_(std::move(payload)) {}
	game::TypeKey type_{};
	std::uint32_t version_{1};
	std::vector<std::byte> payload_;
	friend class ActionRegistry;
};

/** Stable failures produced by action registration, encoding, and dispatch. */
enum class ActionError : std::uint32_t {
	EmptyName = 1,    ///< The registration name was empty.
	DuplicateType,    ///< The name or key was already registered.
	UnknownType,      ///< No action was found for the given key.
	PayloadMismatch,  ///< The token type tag did not match the registered entry.
	InvalidHandler,   ///< The execution callback was null.
	RegistryFrozen,   ///< Mutation was attempted after definition freeze.
	InvalidCodec,     ///< The codec was missing an encoder or decoder.
	VersionMismatch   ///< The payload version differed from the registered version.
};

/** Stores typed codecs and handlers shared by all Sessions of a definition. */
class ActionRegistry final {
  public:
	template <ActionPayload T>
	[[nodiscard]] Result<ActionToken<T>>
	add(std::string name, std::uint32_t version, ActionCodec<T> codec,
	    std::function<Result<void>(stg::GameplayContext &, const T &)> handler);

	/** Encodes one typed invocation during StageProgram build or asset load. */
	template <ActionPayload T>
	[[nodiscard]] Result<GameplayAction> make(ActionToken<T> token,
	                                          const T &payload) const;

	/** Decodes and executes one immutable Program payload. */
	[[nodiscard]] Result<void> execute(game::TypeKey type,
	                                   std::uint32_t version,
	                                   std::span<const std::byte> payload,
	                                   stg::GameplayContext &game) const;

	[[nodiscard]] std::vector<std::string> names() const;
	[[nodiscard]] std::vector<ActionRegistration> registrations() const;
	void freeze() noexcept { frozen_ = true; }

  private:
	struct Entry final {
		std::string name;
		game::TypeKey type{};
		std::uint32_t version{};
		const void *typeTag{};
		std::function<Result<std::vector<std::byte>>(const void *)> encode;
		std::function<Result<void>(stg::GameplayContext &,
		                           std::span<const std::byte>)>
		    execute;
	};
	std::vector<Entry> entries_;
	bool frozen_{};
};

template <ActionPayload T>
Result<ActionToken<T>> ActionRegistry::add(
    std::string name, std::uint32_t version, ActionCodec<T> codec,
    std::function<Result<void>(stg::GameplayContext &, const T &)> handler) {
	if (frozen_)
		return std::unexpected(
		    Error{ErrorDomain::Definition,
		          static_cast<std::uint32_t>(ActionError::RegistryFrozen),
		          "Action registry is frozen"});
	if (name.empty())
		return std::unexpected(
		    Error{ErrorDomain::Definition,
		          static_cast<std::uint32_t>(ActionError::EmptyName),
		          "Action name cannot be empty"});
	if (!codec.encode || !codec.decode)
		return std::unexpected(
		    Error{ErrorDomain::Definition,
		          static_cast<std::uint32_t>(ActionError::InvalidCodec),
		          "Action codec must encode and decode"});
	if (!handler)
		return std::unexpected(
		    Error{ErrorDomain::Definition,
		          static_cast<std::uint32_t>(ActionError::InvalidHandler),
		          "Action handler cannot be empty"});
	const game::TypeKey key = game::typeKeyFromName(name);
	for (const Entry &entry : entries_)
		if (entry.type == key)
			return std::unexpected(
			    Error{ErrorDomain::Definition,
			          static_cast<std::uint32_t>(ActionError::DuplicateType),
			          "Action identity is already registered"});
	entries_.push_back(
	    Entry{std::move(name), key, version, &actionPayloadTypeTag<T>,
	          [encode = codec.encode](const void *payload) {
		          return encode(*static_cast<const T *>(payload));
	          },
	          [decode = std::move(codec.decode), handler = std::move(handler)](
	              stg::GameplayContext &game,
	              std::span<const std::byte> payload) -> Result<void> {
		          auto value = decode(payload);
		          if (!value)
			          return std::unexpected(value.error());
		          return handler(game, *value);
	          }});
	return ActionToken<T>{key, version, &actionPayloadTypeTag<T>};
}

template <ActionPayload T>
Result<GameplayAction> ActionRegistry::make(ActionToken<T> token,
                                            const T &payload) const {
	for (const Entry &entry : entries_) {
		if (entry.type != token.key_)
			continue;
		if (entry.version != token.version_ ||
		    entry.typeTag != &actionPayloadTypeTag<T>)
			return std::unexpected(
			    Error{ErrorDomain::Definition,
			          static_cast<std::uint32_t>(ActionError::PayloadMismatch),
			          "Action token does not match its registered payload"});
		auto bytes = entry.encode(&payload);
		if (!bytes)
			return std::unexpected(bytes.error());
		return GameplayAction{entry.type, entry.version, std::move(*bytes)};
	}
	return std::unexpected(
	    Error{ErrorDomain::Definition,
	          static_cast<std::uint32_t>(ActionError::UnknownType),
	          "Action type is not registered"});
}

} // namespace shiki::flow

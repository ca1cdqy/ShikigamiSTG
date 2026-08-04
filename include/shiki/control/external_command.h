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

namespace shiki::control {

template <class T>
concept ExternalCommandPayload = std::movable<T> && std::destructible<T>;

template <class T> inline const char externalCommandPayloadTypeTag = 0;

/** Encodes one stable user-defined external command payload. */
template <ExternalCommandPayload T> struct ExternalCommandCodec final {
	std::function<Result<std::vector<std::byte>>(const T &)> encode;
	std::function<Result<T>(std::span<const std::byte>)> decode;
};

/** A typed persistent reference to an external command definition. */
template <ExternalCommandPayload T> class ExternalCommandToken final {
  public:
	[[nodiscard]] constexpr game::TypeKey key() const noexcept { return key_; }
	[[nodiscard]] constexpr std::uint32_t version() const noexcept {
		return version_;
	}
	[[nodiscard]] constexpr explicit operator bool() const noexcept {
		return typeTag_ != nullptr;
	}

  private:
	constexpr ExternalCommandToken(game::TypeKey key, std::uint32_t version,
	                               const void *typeTag) noexcept
	    : key_(key), version_(version), typeTag_(typeTag) {}
	game::TypeKey key_{};
	std::uint32_t version_{1};
	const void *typeTag_{};
	friend class ExternalCommandRegistry;
};

/** Owns one canonical external command stored in an InputFrame. */
class ExternalCommand final {
  public:
	[[nodiscard]] game::TypeKey type() const noexcept { return type_; }
	[[nodiscard]] std::uint32_t version() const noexcept { return version_; }
	[[nodiscard]] std::span<const std::byte> payload() const noexcept {
		return payload_;
	}

  private:
	ExternalCommand(game::TypeKey type, std::uint32_t version,
	                std::vector<std::byte> payload) noexcept
	    : type_(type), version_(version), payload_(std::move(payload)) {}
	game::TypeKey type_{};
	std::uint32_t version_{1};
	std::vector<std::byte> payload_;
	friend class ExternalCommandRegistry;
};

/** Persistent metadata included in the frozen game schema. */
struct ExternalCommandRegistration final {
	std::string name;
	std::uint32_t version{1};
};

/** Stable failures produced by external command registration and decoding. */
enum class ExternalCommandError : std::uint32_t {
	EmptyName = 1,
	DuplicateType,
	UnknownType,
	PayloadMismatch,
	InvalidHandler,
	RegistryFrozen,
	InvalidCodec,
	VersionMismatch
};

/** Registers deterministic commands accepted at the InputFrame boundary. */
class ExternalCommandRegistry final {
  public:
	template <ExternalCommandPayload T>
	[[nodiscard]] Result<ExternalCommandToken<T>>
	add(std::string name, std::uint32_t version, ExternalCommandCodec<T> codec,
	    std::function<Result<void>(stg::GameplayContext &, const T &)> handler);

	/** Encodes one typed command for an InputFrame or user replay stream. */
	template <ExternalCommandPayload T>
	[[nodiscard]] Result<ExternalCommand> make(ExternalCommandToken<T> token,
	                                           const T &payload) const;

	/** Validates and owns encoded bytes read from a user-defined replay. */
	[[nodiscard]] Result<ExternalCommand>
	load(game::TypeKey type, std::uint32_t version,
	     std::span<const std::byte> payload) const;

	/** Decodes and executes one command through phase-scoped gameplay APIs. */
	[[nodiscard]] Result<void> execute(const ExternalCommand &command,
	                                   stg::GameplayContext &game) const;

	[[nodiscard]] std::vector<ExternalCommandRegistration>
	registrations() const;
	void freeze() noexcept { frozen_ = true; }

  private:
	struct Entry final {
		std::string name;
		game::TypeKey type{};
		std::uint32_t version{};
		const void *typeTag{};
		std::function<Result<std::vector<std::byte>>(const void *)> encode;
		std::function<Result<void>(std::span<const std::byte>)> validate;
		std::function<Result<void>(stg::GameplayContext &,
		                           std::span<const std::byte>)>
		    execute;
	};
	std::vector<Entry> entries_;
	bool frozen_{};
};

template <ExternalCommandPayload T>
Result<ExternalCommandToken<T>> ExternalCommandRegistry::add(
    std::string name, std::uint32_t version, ExternalCommandCodec<T> codec,
    std::function<Result<void>(stg::GameplayContext &, const T &)> handler) {
	if (frozen_)
		return std::unexpected(Error{
		    ErrorDomain::Definition,
		    static_cast<std::uint32_t>(ExternalCommandError::RegistryFrozen),
		    "External command registry is frozen"});
	if (name.empty())
		return std::unexpected(
		    Error{ErrorDomain::Definition,
		          static_cast<std::uint32_t>(ExternalCommandError::EmptyName),
		          "External command name cannot be empty"});
	if (!codec.encode || !codec.decode)
		return std::unexpected(Error{
		    ErrorDomain::Definition,
		    static_cast<std::uint32_t>(ExternalCommandError::InvalidCodec),
		    "External command codec must encode and decode"});
	if (!handler)
		return std::unexpected(Error{
		    ErrorDomain::Definition,
		    static_cast<std::uint32_t>(ExternalCommandError::InvalidHandler),
		    "External command handler cannot be empty"});
	const game::TypeKey key = game::typeKeyFromName(name);
	for (const Entry &entry : entries_)
		if (entry.type == key)
			return std::unexpected(Error{
			    ErrorDomain::Definition,
			    static_cast<std::uint32_t>(ExternalCommandError::DuplicateType),
			    "External command identity is already registered"});
	entries_.push_back(
	    Entry{std::move(name), key, version, &externalCommandPayloadTypeTag<T>,
	          [encode = codec.encode](const void *payload) {
		          return encode(*static_cast<const T *>(payload));
	          },
	          [decode = codec.decode](
	              std::span<const std::byte> payload) -> Result<void> {
		          auto decoded = decode(payload);
		          if (!decoded)
			          return std::unexpected(decoded.error());
		          return {};
	          },
	          [decode = std::move(codec.decode), handler = std::move(handler)](
	              stg::GameplayContext &game,
	              std::span<const std::byte> payload) -> Result<void> {
		          auto decoded = decode(payload);
		          if (!decoded)
			          return std::unexpected(decoded.error());
		          return handler(game, *decoded);
	          }});
	return ExternalCommandToken<T>{key, version,
	                               &externalCommandPayloadTypeTag<T>};
}

template <ExternalCommandPayload T>
Result<ExternalCommand>
ExternalCommandRegistry::make(ExternalCommandToken<T> token,
                              const T &payload) const {
	for (const Entry &entry : entries_) {
		if (entry.type != token.key_)
			continue;
		if (entry.version != token.version_ ||
		    entry.typeTag != &externalCommandPayloadTypeTag<T>)
			return std::unexpected(
			    Error{ErrorDomain::Definition,
			          static_cast<std::uint32_t>(
			              ExternalCommandError::PayloadMismatch),
			          "External command token does not match its payload"});
		auto bytes = entry.encode(&payload);
		if (!bytes)
			return std::unexpected(bytes.error());
		return ExternalCommand{entry.type, entry.version, std::move(*bytes)};
	}
	return std::unexpected(
	    Error{ErrorDomain::Definition,
	          static_cast<std::uint32_t>(ExternalCommandError::UnknownType),
	          "External command type is not registered"});
}

} // namespace shiki::control

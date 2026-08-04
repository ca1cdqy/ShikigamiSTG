#pragma once

#include <shiki/core/result.h>
#include <shiki/game/type_id.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace shiki::game {

/** Stable error codes returned by TypeRegistry. */
enum class TypeRegistryError : std::uint32_t {
	EmptyName = 1,  ///< A registration name was empty.
	DuplicateType,  ///< The qualified name was registered twice.
	KeyCollision,   ///< Different names produced one persistent key.
	RegistryFrozen, ///< Mutation was attempted after freeze.
	AlreadyFrozen,  ///< Freeze was requested more than once.
	TooManyTypes    ///< The registry exceeded TypeIndex capacity.
};

/** Describes one type before it is added to a registry. */
struct TypeRegistration final {
	TypeDomain domain{};
	std::string name;
	std::uint32_t version{1};
	std::uint64_t flags{};
	std::string codec;
};

/** Stores the persistent and runtime metadata of a registered type. */
struct RegisteredType final {
	QualifiedTypeKey identity{};
	TypeIndex index{};
	std::string name;
	std::uint32_t version{1};
	std::uint64_t flags{};
	std::string codec;
};

/** Builds and freezes the type schema owned by one game definition. */
class TypeRegistry final {
  public:
	/** Adds a type while preserving ownership of all input strings. */
	[[nodiscard]] Result<TypeKey> add(TypeRegistration registration);

	/** Sorts registrations, assigns dense indices, and computes the digest. */
	[[nodiscard]] Result<SchemaDigest> freeze();

	/** Returns whether the registry can no longer be modified. */
	[[nodiscard]] bool isFrozen() const noexcept { return frozen_; }

	/** Returns the number of registered types. */
	[[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

	/** Returns the frozen schema digest. */
	[[nodiscard]] SchemaDigest digest() const noexcept { return digest_; }

	/** Returns all entries in dense index order after freeze. */
	[[nodiscard]] std::span<const RegisteredType> entries() const noexcept {
		return entries_;
	}

	/** Finds an entry by its qualified persistent identity. */
	[[nodiscard]] const RegisteredType *
	find(QualifiedTypeKey identity) const noexcept;

	/** Finds an entry by its dense identity after freeze. */
	[[nodiscard]] const RegisteredType *find(TypeIndex index) const noexcept;

	/** Finds an entry by its domain and canonical name. */
	[[nodiscard]] const RegisteredType *
	find(TypeDomain domain, std::string_view name) const noexcept;

  private:
	std::vector<RegisteredType> entries_;
	SchemaDigest digest_{};
	bool frozen_{};
};

} // namespace shiki::game

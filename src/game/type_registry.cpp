#include <shiki/game/type_registry.h>

#include <algorithm>
#include <limits>

namespace shiki::game {
namespace {

[[nodiscard]] Error makeRegistryError(TypeRegistryError code,
                                      std::string message) {
	return Error{ErrorDomain::Definition, static_cast<std::uint32_t>(code),
	             std::move(message)};
}

void hashByte(std::uint64_t &hash, std::uint8_t value) noexcept {
	hash ^= value;
	hash *= fnv1a64Prime;
}

template <class Integer>
void hashInteger(std::uint64_t &hash, Integer value) noexcept {
	for (std::size_t index = 0; index < sizeof(Integer); ++index) {
		hashByte(hash, static_cast<std::uint8_t>(value & 0xffU));
		value >>= 8U;
	}
}

void hashString(std::uint64_t &hash, std::string_view value) noexcept {
	hashInteger(hash, static_cast<std::uint64_t>(value.size()));
	for (const char character : value) {
		hashByte(hash, static_cast<std::uint8_t>(character));
	}
}

} // namespace

Result<TypeKey> TypeRegistry::add(TypeRegistration registration) {
	if (frozen_) {
		return std::unexpected(makeRegistryError(
		    TypeRegistryError::RegistryFrozen,
		    "Cannot add a type after the registry is frozen"));
	}
	if (registration.name.empty()) {
		return std::unexpected(makeRegistryError(TypeRegistryError::EmptyName,
		                                         "Type name cannot be empty"));
	}

	const TypeKey key = typeKeyFromName(registration.name);
	const auto existing =
	    std::ranges::find_if(entries_, [&](const auto &entry) {
		    return entry.identity.domain == registration.domain &&
		           entry.identity.key == key;
	    });
	if (existing != entries_.end()) {
		const auto code = existing->name == registration.name
		                      ? TypeRegistryError::DuplicateType
		                      : TypeRegistryError::KeyCollision;
		return std::unexpected(
		    makeRegistryError(code, code == TypeRegistryError::DuplicateType
		                                ? "Type is already registered"
		                                : "Type key collision detected"));
	}

	entries_.push_back(RegisteredType{
	    .identity = {registration.domain, key},
	    .name = std::move(registration.name),
	    .version = registration.version,
	    .flags = registration.flags,
	    .codec = std::move(registration.codec),
	});
	return key;
}

Result<SchemaDigest> TypeRegistry::freeze() {
	if (frozen_) {
		return std::unexpected(
		    makeRegistryError(TypeRegistryError::AlreadyFrozen,
		                      "Type registry has already been frozen"));
	}
	if (entries_.size() > std::numeric_limits<std::uint32_t>::max()) {
		return std::unexpected(
		    makeRegistryError(TypeRegistryError::TooManyTypes,
		                      "Type registry exceeds the TypeIndex capacity"));
	}

	std::ranges::sort(entries_, {}, [](const RegisteredType &entry) {
		return entry.identity;
	});

	std::uint64_t hash = fnv1a64OffsetBasis;
	for (std::size_t index = 0; index < entries_.size(); ++index) {
		auto &entry = entries_[index];
		entry.index = TypeIndex{static_cast<std::uint32_t>(index)};
		hashByte(hash, static_cast<std::uint8_t>(entry.identity.domain));
		hashInteger(hash, entry.identity.key.value);
		hashString(hash, entry.name);
		hashInteger(hash, entry.version);
		hashInteger(hash, entry.flags);
		hashString(hash, entry.codec);
	}

	digest_ = SchemaDigest{hash};
	frozen_ = true;
	return digest_;
}

const RegisteredType *
TypeRegistry::find(QualifiedTypeKey identity) const noexcept {
	const auto entry =
	    std::ranges::find_if(entries_, [&](const auto &candidate) {
		    return candidate.identity == identity;
	    });
	return entry == entries_.end() ? nullptr : &*entry;
}

const RegisteredType *TypeRegistry::find(TypeIndex index) const noexcept {
	if (!frozen_ || !index.isValid() || index.value >= entries_.size()) {
		return nullptr;
	}
	return &entries_[index.value];
}

const RegisteredType *TypeRegistry::find(TypeDomain domain,
                                         std::string_view name) const noexcept {
	const auto *entry = find(QualifiedTypeKey{domain, typeKeyFromName(name)});
	return entry != nullptr && entry->name == name ? entry : nullptr;
}

} // namespace shiki::game

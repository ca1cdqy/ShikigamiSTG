#pragma once

/** @file
 * Procedural and facade APIs for typed CPU asset loading.
 *
 * The free functions in this header are the lowest-level public resource
 * contract. AssetStore member functions and ResourceManager convenience
 * paths are built on the same decoder and source-provider operations.
 */

#include <shiki/core/result.h>
#include <shiki/game/type_id.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace shiki::asset {

/** Identifies one logical asset independently from its source path. */
struct AssetId final {
	game::TypeKey key{};
	auto operator<=>(const AssetId &) const = default;

	/** Creates an identity from one canonical manifest name. */
	[[nodiscard]] static constexpr AssetId
	fromName(std::string_view name) noexcept {
		return AssetId{game::typeKeyFromName(name)};
	}
};

/** Identifies one decoder format registered with an AssetStore. */
struct AssetFormat final {
	game::TypeKey key{};
	auto operator<=>(const AssetFormat &) const = default;

	/** Creates a format identity from one canonical format name. */
	[[nodiscard]] static constexpr AssetFormat
	fromName(std::string_view name) noexcept {
		return AssetFormat{game::typeKeyFromName(name)};
	}
};

/** Describes one immutable asset source resolved by a manifest. */
struct ManifestEntry final {
	AssetId id{};                      ///< Stable logical asset identity.
	AssetFormat format{};              ///< Registered decoder format identity.
	std::string source;                ///< Provider-specific source identifier.
	std::vector<AssetId> dependencies; ///< Assets decoded before this asset.
};

/** Owns bytes read from one manifest source for a decode invocation. */
struct Source final {
	std::string identity; ///< Diagnostic source identity.
	std::vector<std::byte>
	    bytes; ///< Owned immutable-by-convention source bytes.
};

/** Reads manifest source identifiers without coupling loaders to files. */
class SourceProvider {
  public:
	virtual ~SourceProvider() = default;
	/** Reads and owns all source bytes required by one decoder call. */
	[[nodiscard]] virtual Result<Source> read(std::string_view source) = 0;
};

/** Reads sources from a configured filesystem root. */
class FileSourceProvider final : public SourceProvider {
  public:
	explicit FileSourceProvider(std::string root);
	[[nodiscard]] Result<Source> read(std::string_view source) override;

  private:
	std::string root_;
};

/**
 * Adapts a callable to the SourceProvider interface.
 *
 * The callback owns source resolution policy and may read from memory,
 * archives, databases, or application-defined transports. It is invoked
 * synchronously by AssetStore and must return owned bytes.
 *
 * @note The provider is not thread-safe unless the supplied callback is.
 */
class CallbackSourceProvider final : public SourceProvider {
  public:
	using Reader = std::function<Result<Source>(std::string_view)>;

	/** Takes ownership of a source reader callback. */
	explicit CallbackSourceProvider(Reader reader);

	/** Invokes the configured callback for one logical source. */
	[[nodiscard]] Result<Source> read(std::string_view source) override;

  private:
	Reader reader_;
};

/** Stable failures produced by manifest, source, and loader operations. */
enum class AssetError : std::uint32_t {
	DuplicateAsset = 1, ///< The manifest already contains the asset ID.
	UnknownAsset,       ///< The requested asset ID is not registered.
	DuplicateLoader,    ///< The format already has a decoder.
	UnknownLoader,      ///< No decoder is registered for the format.
	TypeMismatch,       ///< The requested C++ payload type is incompatible.
	NullProvider,       ///< The store has no source provider.
	InvalidSource,      ///< The source identifier is invalid for the provider.
	ReadFailed,         ///< The provider could not read the source bytes.
	DecodeFailed,       ///< The decoder rejected the source bytes.
	DependencyCycle     ///< Manifest dependencies contain a cycle.
};

/** A typed immutable reference to a decoded CPU asset. */
template <class T> class AssetHandle final {
  public:
	/** Returns the logical manifest identity. */
	[[nodiscard]] AssetId id() const noexcept { return id_; }
	/** Returns whether a decoded value is available. */
	[[nodiscard]] bool ready() const noexcept { return value_ != nullptr; }
	/** Returns the decoded immutable value or null when not ready. */
	[[nodiscard]] const T *get() const noexcept { return value_.get(); }
	/** Returns the decoded immutable value. Calling before ready is invalid. */
	[[nodiscard]] const T &operator*() const noexcept { return *value_; }
	/** Provides immutable pointer access to the decoded value. */
	[[nodiscard]] const T *operator->() const noexcept { return value_.get(); }

  private:
	AssetHandle(AssetId id, std::shared_ptr<const T> value) noexcept
	    : id_(id), value_(std::move(value)) {}
	AssetId id_{};
	std::shared_ptr<const T> value_;
	friend class AssetStore;
};

/** Internal type identity used to validate typed handles and decoders. */
template <class T> inline const char assetTypeTag = 0;

/** Typed synchronous decoder used by both procedural and facade APIs. */
template <class T>
using AssetDecoder =
    std::function<Result<std::shared_ptr<const T>>(const Source &)>;

/** Resolves manifests and dispatches typed CPU decoders. */
class AssetStore final {
  public:
	explicit AssetStore(std::unique_ptr<SourceProvider> sources);
	AssetStore(const AssetStore &) = delete;
	AssetStore &operator=(const AssetStore &) = delete;
	AssetStore(AssetStore &&) noexcept = default;
	AssetStore &operator=(AssetStore &&) noexcept = default;

	/** Adds one immutable manifest entry. */
	[[nodiscard]] Result<void> add(ManifestEntry entry);

	/** Registers one typed CPU decoder for a manifest format. */
	template <class T>
	[[nodiscard]] Result<void> registerLoader(AssetFormat format,
	                                          AssetDecoder<T> loader);

	/**
	 * Decodes owned source bytes with a registered typed format.
	 *
	 * This low-level operation bypasses the manifest, source provider,
	 * dependency graph, and cache. It is suitable for custom manifests,
	 * editor imports, generated data, and procedural resource pipelines.
	 */
	template <class T>
	[[nodiscard]] Result<std::shared_ptr<const T>>
	decode(AssetFormat format, const Source &source) const;

	/** Validates dependencies and synchronously decodes one typed asset. */
	template <class T> [[nodiscard]] Result<AssetHandle<T>> load(AssetId id);

	/** Returns a cached typed handle without performing source IO. */
	template <class T>
	[[nodiscard]] Result<AssetHandle<T>> find(AssetId id) const;

  private:
	struct Loader final {
		const void *typeTag{};
		std::function<Result<std::shared_ptr<const void>>(const Source &)>
		    decode;
	};
	struct Cached final {
		const void *typeTag{};
		std::shared_ptr<const void> value;
	};

	[[nodiscard]] Result<void> loadDependencies(const ManifestEntry &entry);
	[[nodiscard]] Result<void> loadUntyped(AssetId id,
	                                       std::vector<AssetId> &stack);
	std::unique_ptr<SourceProvider> sources_;
	std::map<AssetId, ManifestEntry> manifest_;
	std::map<AssetFormat, Loader> loaders_;
	std::map<AssetId, Cached> cache_;
};

template <class T>
Result<void> AssetStore::registerLoader(AssetFormat format,
                                        AssetDecoder<T> loader) {
	if (!loader)
		return std::unexpected(
		    Error{ErrorDomain::Asset,
		          static_cast<std::uint32_t>(AssetError::UnknownLoader),
		          "Asset loader cannot be empty"});
	if (loaders_.contains(format))
		return std::unexpected(
		    Error{ErrorDomain::Asset,
		          static_cast<std::uint32_t>(AssetError::DuplicateLoader),
		          "Asset format already has a registered loader"});
	loaders_.emplace(
	    format,
	    Loader{&assetTypeTag<T>,
	           [loader = std::move(loader)](const Source &source)
	               -> Result<std::shared_ptr<const void>> {
		           auto decoded = loader(source);
		           if (!decoded)
			           return std::unexpected(decoded.error());
		           if (*decoded == nullptr)
			           return std::unexpected(Error{
			               ErrorDomain::Asset,
			               static_cast<std::uint32_t>(AssetError::DecodeFailed),
			               "Asset loader returned a null value"});
		           return std::static_pointer_cast<const void>(*decoded);
	           }});
	return {};
}

template <class T>
Result<std::shared_ptr<const T>>
AssetStore::decode(AssetFormat format, const Source &source) const {
	const auto loader = loaders_.find(format);
	if (loader == loaders_.end())
		return std::unexpected(
		    Error{ErrorDomain::Asset,
		          static_cast<std::uint32_t>(AssetError::UnknownLoader),
		          "No loader is registered for the asset format"});
	if (loader->second.typeTag != &assetTypeTag<T>)
		return std::unexpected(Error{
		    ErrorDomain::Asset,
		    static_cast<std::uint32_t>(AssetError::TypeMismatch),
		    "Asset loader payload type does not match the requested type"});
	auto decoded = loader->second.decode(source);
	if (!decoded)
		return std::unexpected(decoded.error());
	return std::static_pointer_cast<const T>(*decoded);
}

template <class T> Result<AssetHandle<T>> AssetStore::load(AssetId id) {
	if (sources_ == nullptr)
		return std::unexpected(
		    Error{ErrorDomain::Asset,
		          static_cast<std::uint32_t>(AssetError::NullProvider),
		          "AssetStore has no source provider"});
	if (const auto cached = cache_.find(id); cached != cache_.end()) {
		if (cached->second.typeTag != &assetTypeTag<T>)
			return std::unexpected(
			    Error{ErrorDomain::Asset,
			          static_cast<std::uint32_t>(AssetError::TypeMismatch),
			          "Cached asset has a different payload type"});
		return AssetHandle<T>{
		    id, std::static_pointer_cast<const T>(cached->second.value)};
	}
	const auto entry = manifest_.find(id);
	if (entry == manifest_.end())
		return std::unexpected(
		    Error{ErrorDomain::Asset,
		          static_cast<std::uint32_t>(AssetError::UnknownAsset),
		          "Asset is not present in the manifest"});
	const auto loader = loaders_.find(entry->second.format);
	if (loader == loaders_.end())
		return std::unexpected(
		    Error{ErrorDomain::Asset,
		          static_cast<std::uint32_t>(AssetError::UnknownLoader),
		          "No loader is registered for the asset format"});
	if (loader->second.typeTag != &assetTypeTag<T>)
		return std::unexpected(Error{
		    ErrorDomain::Asset,
		    static_cast<std::uint32_t>(AssetError::TypeMismatch),
		    "Asset loader payload type does not match the requested type"});
	auto dependencies = loadDependencies(entry->second);
	if (!dependencies)
		return std::unexpected(dependencies.error());
	auto source = sources_->read(entry->second.source);
	if (!source)
		return std::unexpected(source.error());
	auto decoded = loader->second.decode(*source);
	if (!decoded)
		return std::unexpected(decoded.error());
	cache_.emplace(id, Cached{&assetTypeTag<T>, *decoded});
	return AssetHandle<T>{id, std::static_pointer_cast<const T>(*decoded)};
}

template <class T> Result<AssetHandle<T>> AssetStore::find(AssetId id) const {
	const auto cached = cache_.find(id);
	if (cached == cache_.end())
		return std::unexpected(
		    Error{ErrorDomain::Asset,
		          static_cast<std::uint32_t>(AssetError::UnknownAsset),
		          "Asset is not loaded"});
	if (cached->second.typeTag != &assetTypeTag<T>)
		return std::unexpected(
		    Error{ErrorDomain::Asset,
		          static_cast<std::uint32_t>(AssetError::TypeMismatch),
		          "Cached asset has a different payload type"});
	return AssetHandle<T>{
	    id, std::static_pointer_cast<const T>(cached->second.value)};
}

/** Adds one source mapping through the procedural asset API. */
[[nodiscard]] inline Result<void> addAsset(AssetStore &store,
                                           ManifestEntry entry) {
	return store.add(std::move(entry));
}

/** Registers an application-defined typed decoder through the procedural API.
 */
template <class T>
[[nodiscard]] Result<void> registerAssetDecoder(AssetStore &store,
                                                AssetFormat format,
                                                AssetDecoder<T> decoder) {
	return store.registerLoader<T>(format, std::move(decoder));
}

/** Decodes one source directly without a manifest lookup or cache mutation. */
template <class T>
[[nodiscard]] Result<std::shared_ptr<const T>>
decodeAsset(const AssetStore &store, AssetFormat format, const Source &source) {
	return store.decode<T>(format, source);
}

/** Resolves, decodes, and caches one manifest asset through the procedural API.
 */
template <class T>
[[nodiscard]] Result<AssetHandle<T>> loadAsset(AssetStore &store, AssetId id) {
	return store.load<T>(id);
}

/** Finds one previously loaded asset through the procedural API. */
template <class T>
[[nodiscard]] Result<AssetHandle<T>> findAsset(const AssetStore &store,
                                               AssetId id) {
	return store.find<T>(id);
}

} // namespace shiki::asset

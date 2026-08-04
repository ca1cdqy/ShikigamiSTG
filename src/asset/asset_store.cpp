#include <shiki/asset/asset_store.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace shiki::asset {

FileSourceProvider::FileSourceProvider(std::string root)
    : root_(std::move(root)) {}

CallbackSourceProvider::CallbackSourceProvider(Reader reader)
    : reader_(std::move(reader)) {}

Result<Source> CallbackSourceProvider::read(std::string_view source) {
	if (!reader_)
		return std::unexpected(
		    Error{ErrorDomain::Asset,
		          static_cast<std::uint32_t>(AssetError::NullProvider),
		          "Source reader callback is empty"});
	return reader_(source);
}

Result<Source> FileSourceProvider::read(std::string_view source) {
	if (source.empty())
		return std::unexpected(
		    Error{ErrorDomain::Asset,
		          static_cast<std::uint32_t>(AssetError::InvalidSource),
		          "Asset source cannot be empty"});
	const std::filesystem::path path =
	    std::filesystem::path(root_) / std::filesystem::path(source);
	std::ifstream stream(path, std::ios::binary | std::ios::ate);
	if (!stream)
		return std::unexpected(
		    Error{ErrorDomain::Asset,
		          static_cast<std::uint32_t>(AssetError::ReadFailed),
		          "Failed to open asset source",
		          {{"source", path.string()}}});
	const std::streamsize size = stream.tellg();
	if (size < 0)
		return std::unexpected(
		    Error{ErrorDomain::Asset,
		          static_cast<std::uint32_t>(AssetError::ReadFailed),
		          "Failed to determine asset source size"});
	stream.seekg(0, std::ios::beg);
	Source result{path.string(),
	              std::vector<std::byte>(static_cast<std::size_t>(size))};
	if (size != 0 &&
	    !stream.read(reinterpret_cast<char *>(result.bytes.data()), size))
		return std::unexpected(
		    Error{ErrorDomain::Asset,
		          static_cast<std::uint32_t>(AssetError::ReadFailed),
		          "Failed to read complete asset source"});
	return result;
}

AssetStore::AssetStore(std::unique_ptr<SourceProvider> sources)
    : sources_(std::move(sources)) {}

Result<void> AssetStore::add(ManifestEntry entry) {
	if (entry.source.empty())
		return std::unexpected(
		    Error{ErrorDomain::Asset,
		          static_cast<std::uint32_t>(AssetError::InvalidSource),
		          "Manifest source cannot be empty"});
	if (manifest_.contains(entry.id))
		return std::unexpected(
		    Error{ErrorDomain::Asset,
		          static_cast<std::uint32_t>(AssetError::DuplicateAsset),
		          "Manifest asset identity is already registered"});
	manifest_.emplace(entry.id, std::move(entry));
	return {};
}

Result<void> AssetStore::loadDependencies(const ManifestEntry &entry) {
	std::vector<AssetId> stack{entry.id};
	for (const AssetId dependency : entry.dependencies) {
		auto loaded = loadUntyped(dependency, stack);
		if (!loaded)
			return std::unexpected(loaded.error());
	}
	return {};
}

Result<void> AssetStore::loadUntyped(AssetId id, std::vector<AssetId> &stack) {
	if (cache_.contains(id))
		return {};
	if (std::ranges::find(stack, id) != stack.end())
		return std::unexpected(
		    Error{ErrorDomain::Asset,
		          static_cast<std::uint32_t>(AssetError::DependencyCycle),
		          "Asset dependency graph contains a cycle"});
	const auto entry = manifest_.find(id);
	if (entry == manifest_.end())
		return std::unexpected(
		    Error{ErrorDomain::Asset,
		          static_cast<std::uint32_t>(AssetError::UnknownAsset),
		          "Asset dependency is not present in the manifest"});
	const auto loader = loaders_.find(entry->second.format);
	if (loader == loaders_.end())
		return std::unexpected(
		    Error{ErrorDomain::Asset,
		          static_cast<std::uint32_t>(AssetError::UnknownLoader),
		          "No loader is registered for an asset dependency"});
	stack.push_back(id);
	for (const AssetId dependency : entry->second.dependencies) {
		auto loaded = loadUntyped(dependency, stack);
		if (!loaded) {
			stack.pop_back();
			return std::unexpected(loaded.error());
		}
	}
	stack.pop_back();
	auto source = sources_->read(entry->second.source);
	if (!source)
		return std::unexpected(source.error());
	auto decoded = loader->second.decode(*source);
	if (!decoded)
		return std::unexpected(decoded.error());
	cache_.emplace(id, Cached{loader->second.typeTag, std::move(*decoded)});
	return {};
}

} // namespace shiki::asset

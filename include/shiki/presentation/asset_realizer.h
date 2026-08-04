#pragma once

#include <shiki/asset/asset_store.h>
#include <shiki/core/result.h>

#include <memory>

namespace shiki::presentation {

/** Converts one immutable CPU asset into a frontend-specific resource. */
template <class CpuAsset, class FrontendAsset> class AssetRealizer {
  public:
	virtual ~AssetRealizer() = default;

	/** Realizes or retrieves one frontend resource for a logical asset. */
	[[nodiscard]] virtual Result<std::shared_ptr<FrontendAsset>>
	realize(asset::AssetId id, const CpuAsset &source) = 0;
};

} // namespace shiki::presentation

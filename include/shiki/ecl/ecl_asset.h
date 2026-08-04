#pragma once

#include <shiki/asset/asset_store.h>
#include <shiki/ecl/ecl_parser.h>

#include <cstdint>

namespace shiki::ecl {

/** Registers an in-memory ECL syntax decoder for one asset format. */
[[nodiscard]] Result<void> registerEclFileLoader(asset::AssetStore &assets,
                                                 asset::AssetFormat format,
                                                 std::uint32_t version = 6);

} // namespace shiki::ecl

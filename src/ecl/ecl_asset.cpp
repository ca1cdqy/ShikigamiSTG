#include <shiki/ecl/ecl_asset.h>

#include <memory>
#include <utility>

namespace shiki::ecl {

Result<void> registerEclFileLoader(asset::AssetStore &assets,
                                   asset::AssetFormat format,
                                   std::uint32_t version) {
	return asset::registerAssetDecoder<ECLFile>(
	    assets, format,
	    [version](const asset::Source &source)
	        -> Result<std::shared_ptr<const ECLFile>> {
		    ECLParser parser;
		    auto parsed = parser.parse(source.bytes, version);
		    if (!parsed) {
			    Error error = parsed.error();
			    error.fields.push_back({"source", source.identity});
			    return std::unexpected(std::move(error));
		    }
		    return std::make_shared<const ECLFile>(
		        std::move(parser).takeFile());
	    });
}

} // namespace shiki::ecl

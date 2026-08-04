# Reference

This section contains stable specifications that do not belong in Doxygen comments.

Planned topics:

- Tick phases and visibility rules
- JSON resource and atlas formats
- Procedural resource APIs: `CallbackSourceProvider`, `registerAssetDecoder`, `decodeAsset`, and `loadAsset`
- ECL compatibility boundaries
- Error domains and codes
- Versioning and migration policy
- Supported platforms and toolchains

Resources have two layers. The free functions and `AssetStore` in `shiki::asset` are the procedural foundation: applications can provide any source backend, register any decoder format, and decode or resolve their own manifest. `ResourceManager::mountAssetPackage` is the convenience path for the compact JSON, PNG, and audio package. Custom formats should use `mountAssetSource`, whose setup callback registers formats and entries without imposing the engine JSON schema.

Function signatures and call contracts live in the [C++ API reference](../../api/index.md).

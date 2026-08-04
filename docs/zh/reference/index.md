# 参考资料

本节保存需要稳定查阅、但不适合放进 Doxygen 注释的规范。

计划内容：

- 每 Tick 更新阶段与可见性规则
- JSON 资源与图集格式
- 资源底层 API：`CallbackSourceProvider`、`registerAssetDecoder`、`decodeAsset`、`loadAsset`
- ECL 兼容范围
- 错误域和错误码
- 版本与迁移策略
- 平台和工具链支持矩阵

资源系统分为两层。`shiki::asset` 中的自由函数和 `AssetStore` 组成过程式底层：用户可以提供任意来源、注册任意格式的解码器，并直接解码或建立自己的 manifest。`ResourceManager::mountAssetPackage` 是默认的紧凑 JSON、PNG 和音频包便利层；需要自定义格式时使用 `mountAssetSource`，由回调注册格式和资源，不会强制使用引擎的 JSON schema。

函数签名和调用契约位于 [C++ API 参考](../../api/index.md)。

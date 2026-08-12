# 快速开始

本节将带领没有引擎使用经验的开发者完成第一个可运行项目。

计划内容：

- 安装编译器、xmake 和运行依赖
- 创建最小 `GameDefinition`、`Runtime` 与 `Session`
- 注册玩家、敌机和关卡
- 加载贴图与音频资源
- 构建、运行和调试

在正式教程完成前，可参考 `examples/th06` 的入口与应用装配代码。

## 网页构建（WASM）

引擎与示例支持编译为 WebAssembly，在浏览器中运行。

1. 安装并激活 [Emscripten SDK](https://emscripten.org/)（`emsdk install latest && emsdk activate latest`）
2. 配置 wasm 平台：`xmake f -p wasm -y`
3. 构建无资源示例：`xmake build wave_particle`（产物位于 `build/wasm/wasm32/release/`）
4. th06 需要先用脚本把提取的素材打包成 Emscripten 数据文件（不属于构建流程），再构建：

   ```powershell
   python tools/package_wasm_data.py --assets build/windows/x64/release/assets
   xmake build th06
   ```
5. 用 HTTP 服务 `build/wasm/wasm32/release/`（如 `python -m http.server 8000`），打开生成的 `.html` 页面。

网页版使用原生 WebGL2 后端，合批架构与桌面一致，不需要 WebGPU。桌面版保持 SDL3 GPU API；渲染器、纹理与实时前端对外只暴露一套平台无关 API，编译期自动切换后端。着色器只维护一份 HLSL 源，构建时自动翻译为各平台格式。
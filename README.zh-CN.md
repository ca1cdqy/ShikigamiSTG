# ShikigamiSTG

[English](README.md) | [简体中文](README.zh-CN.md)

ShikigamiSTG 是一个使用 C++23 编写的确定性弹幕射击游戏框架。框架同时提供过程式底层 API、组合式能力层，以及适合传统 STG 开发的面向对象便利层。

## 设计目标

- 使用 `Runtime`、`Session` 和 `World` 构建可重复、可测试的固定 Tick 模拟。
- 使用稳定实体句柄、组件、查询和命令缓冲实现过程式玩法。
- 为 Actor、Projectile、Pattern、Stage、Player、Enemy 和 Boss 提供可组合封装。
- 允许替换控制器、战斗规则、资源格式、关卡解析器和表现层。
- 在实时游戏、无窗口测试、回放工具和 编辑器环境中复用同一套模拟代码。

## 主要模块

| 模块 | 职责 |
| --- | --- |
| `core` | 时间、错误、二进制编解码和基础类型 |
| `game` | World、组件、事件、状态、查询和系统调度 |
| `stg` | Actor、弹体、战斗、道具、得分和弹幕模式 |
| `flow` | 关卡程序、阶段、条件和动作 |
| `control` | 玩家输入、AI、脚本和外部命令 |
| `asset` | 自定义来源、格式解码、依赖和缓存 |
| `presentation` | 模拟快照和表现事件 |
| `frontend` | SDL 实时窗口、渲染和音频服务 |
| `ecl` | ECL 兼容适配器 |

## 环境要求

- 支持 C++23 的编译器
- [xmake](https://xmake.io/)
- Windows、Linux 或 macOS
- macOS 构建需要完整 Xcode 命令行工具

xmake 会获取 SDL3、OpenAL Soft、FreeType、GLM、spdlog、stb、nlohmann/json 和 Catch2。

## 构建

构建框架和 TH06 示例：

```powershell
xmake build shiki
xmake build th06
```

运行示例：

```powershell
xmake run th06
```

着色器会根据构建平台生成：

- Windows：DXIL 和 SPIR-V
- Linux：SPIR-V
- macOS：metallib

Windows 和 Linux 会在缺少 DXC 时下载对应版本。可以使用 `SHIKI_DXC_URL` 指定镜像或自定义下载地址。macOS 使用 `xcrun metal` 和 `xcrun metallib`。

## 运行时资源

`th06` 示例和测试程序从可执行文件旁的 `assets/` 文件夹加载运行时资源。使用 `xmake` 的默认配置时，目录位置为 `build/<platform>/<architecture>/<mode>/assets`。该目录必须在运行示例或测试前准备好，并且不应提交到仓库。

## 测试

```powershell
xmake build tests
xmake run tests
```

测试程序和 `th06` 位于同一目标输出目录，并共享该目录下的 `assets/`。

## 文档

```powershell
cd docs
npm install
cd ..
xmake docs
```

Docsify 文档输出到 `build/docs/`，Doxygen API 参考输出到 `build/docs/api/`。

## Release 包

每个 Release 都提供对应平台的 SDK ZIP。`bin/` 目录包含动态库、适用平台的链接库、示例程序和已编译着色器；压缩包同时包含公共头文件、框架与示例源码、资源工具和离线文档。

## 贡献

提交问题或代码前请阅读 [CONTRIBUTING.zh-CN.md](CONTRIBUTING.zh-CN.md)。提交信息必须遵循 [Conventional Commits](https://www.conventionalcommits.org/zh-hans/v1.0.0/) 规范。

## 支持项目

如果这个项目对你有帮助，可以通过[爱发电](https://ifdian.net/a/ca1cdqy)支持后续开发。

## 许可证

本项目使用 Apache-2.0 许可证。第三方依赖继续适用各自的许可证与权利要求。

# 贡献指南

[English](CONTRIBUTING.md) | [简体中文](CONTRIBUTING.zh-CN.md)

感谢参与 ShikigamiSTG。贡献可以包括缺陷修复、测试、文档、性能改进、新的通用能力和示例改进。

## 开始之前

1. 先确认修改属于通用框架、兼容层还是示例，避免把具体游戏规则放进核心模块。
2. 对较大的公共 API 或执行语义变更，先创建 Issue 说明目标、边界和迁移方式。
3. 不要提交原作资源、反编译产物、构建输出或本地工具缓存。
4. 源代码、标识符、注释、日志和 Doxygen API 文档使用英文；用户文档可以提供中文和英文版本。

## 开发流程

从最新主分支创建主题分支：

```text
feat/custom-stage-parser
fix/projectile-collision
docs/resource-api
```

保持修改聚焦。行为修复应包含回归测试；公共 API 应包含 Doxygen 契约说明和至少一个可编译用例。

提交前运行：

```powershell
xmake build shiki
xmake build tests
xmake run tests
xmake docs
```

修改 TH06 示例或表现层时还应运行：

```powershell
xmake build th06
xmake run th06
```

## 提交信息

所有提交必须遵循 [Conventional Commits 1.0.0](https://www.conventionalcommits.org/zh-hans/v1.0.0/) 规范：

```text
<类型>[可选作用域][可选 !]: <简短说明>

[可选正文]

[可选脚注]
```

常用类型：

| 类型 | 用途 |
| --- | --- |
| `feat` | 新增用户可见能力 |
| `fix` | 修复错误行为 |
| `refactor` | 不改变预期行为的代码重构 |
| `perf` | 性能改进 |
| `test` | 新增或修正测试 |
| `docs` | 仅修改文档 |
| `build` | 构建系统或依赖修改 |
| `ci` | 持续集成修改 |
| `style` | 不影响语义的格式修改 |
| `chore` | 其他维护工作 |
| `revert` | 撤销已有提交 |

作用域应使用稳定模块名，例如 `asset`、`world`、`render`、`flow`、`ecl` 或 `th06`。

示例：

```text
feat(asset): add callback-backed archive source
fix(render): select metallib shaders on macOS
test(world): cover resolution-phase activation
docs(api): document custom stage parser ownership
```

破坏性变更必须在类型或作用域后添加 `!`，或者在脚注中添加 `BREAKING CHANGE:`：

```text
refactor(world)!: replace raw entity ids with stable handles

BREAKING CHANGE: EntityId has been removed; use EntityHandle instead.
```

提交标题使用祈使语气，保持简短，结尾不加句号。一个提交只表达一个逻辑变更；格式修改、生成文件和行为修改尽量分开。

## Pull Request

Pull Request 应说明：

- 修改解决的问题；
- 用户可观察到的行为变化；
- 公共 API 和迁移影响；
- 已执行的测试；
- 未验证的平台或剩余风险。

请保持分支可构建，不要包含无关格式化、资源文件或个人环境配置。评审意见解决后，使用符合 Conventional Commits 的提交继续更新，或在合并前整理提交历史。

## 代码要求

- 使用 C++23 和仓库中的 `.clang-format`。
- 使用四列宽度的制表符缩进。
- 可恢复错误使用 `Result`/`std::expected`，公共 API 不依赖异常作为常规错误模型。
- Gameplay 状态进入 `Session`/`World`；GPU、音频和窗口状态留在表现层或 frontend。
- 高层封装必须建立在公开底层 API 上，不能依赖只有框架内部可调用的捷径。
- 不确定性来源、更新顺序、单位、所有权和生命周期必须在 API 中明确。

## 资源与版权

不要提交 TH06 原始资源、受版权保护的游戏数据或未经授权的反编译产物。
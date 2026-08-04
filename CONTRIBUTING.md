# Contributing Guide

[English](CONTRIBUTING.md) | [简体中文](CONTRIBUTING.zh-CN.md)

Thank you for contributing to ShikigamiSTG. Contributions may include bug
fixes, tests, documentation, performance improvements, general framework
features, and example improvements.

## Before You Start

1. Decide whether a change belongs to the general framework, a compatibility
   adapter, or an example. Game-specific rules must not leak into core modules.
2. Open an issue before making a large public API or execution-semantics
   change. Describe the objective, scope, and expected migration path.
3. Do not commit original game assets, reverse-engineering output, build
   products, or local tool caches.
4. Source code, identifiers, comments, logs, and Doxygen API documentation must
   be in English. User documentation may be maintained in English and Chinese.

## Development Workflow

Create a focused branch from the latest main branch:

```text
feat/custom-stage-parser
fix/projectile-collision
docs/resource-api
```

Keep changes focused. Behavioral fixes should include regression tests. Public
APIs should include Doxygen contract documentation and at least one compiling
example.

Run the following checks before submitting a change:

```powershell
xmake build shiki
xmake build tests
xmake run tests
xmake docs
```

Changes to the TH06 example or presentation layer should also be checked with:

```powershell
xmake build th06
xmake run th06
```

## Commit Messages

All commits must follow
[Conventional Commits 1.0.0](https://www.conventionalcommits.org/en/v1.0.0/):

```text
<type>[optional scope][optional !]: <short description>

[optional body]

[optional footer]
```

Common types:

| Type | Purpose |
| --- | --- |
| `feat` | Add a user-visible capability |
| `fix` | Correct erroneous behavior |
| `refactor` | Restructure code without changing expected behavior |
| `perf` | Improve performance |
| `test` | Add or correct tests |
| `docs` | Change documentation only |
| `build` | Change the build system or dependencies |
| `ci` | Change continuous integration |
| `style` | Change formatting without changing semantics |
| `chore` | Perform other maintenance work |
| `revert` | Revert an earlier commit |

Use stable module names as scopes, such as `asset`, `world`, `render`, `flow`,
`ecl`, or `th06`.

Examples:

```text
feat(asset): add callback-backed archive source
fix(render): select metallib shaders on macOS
test(world): cover resolution-phase activation
docs(api): document custom stage parser ownership
```

Mark a breaking change with `!` after the type or scope, or add a
`BREAKING CHANGE:` footer:

```text
refactor(world)!: replace raw entity ids with stable handles

BREAKING CHANGE: EntityId has been removed; use EntityHandle instead.
```

Use the imperative mood, keep the subject concise, and do not end it with a
period. Each commit should represent one logical change. Keep formatting,
generated files, and behavioral changes in separate commits where practical.

## Pull Requests

A pull request should explain:

- The problem being solved
- User-visible behavioral changes
- Public API and migration impact
- Tests that were run
- Unverified platforms and remaining risks

Keep the branch buildable and avoid unrelated formatting, resource files, or
personal environment configuration. Address review feedback with Conventional
Commits-compliant updates, or clean up the history before merging.

## Code Requirements

- Use C++23 and the repository's `.clang-format` configuration.
- Indent with tabs displayed at four columns.
- Use `Result`/`std::expected` for recoverable errors. Public APIs must not use
  exceptions as their routine error model.
- Gameplay state belongs in `Session`/`World`; GPU, audio, and window state
  belongs in the presentation layer or frontend.
- High-level wrappers must be implemented on public low-level APIs, without
  private framework shortcuts.
- APIs must make uncertainty sources, update ordering, units, ownership, and
  lifetimes explicit.

## Assets and Copyright

Do not commit TH06 assets, copyrighted game data, or unauthorized
reverse-engineering output.

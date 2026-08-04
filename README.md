# ShikigamiSTG

[English](README.md) | [简体中文](README.zh-CN.md)

ShikigamiSTG is a deterministic bullet hell game framework written in C++23.
It combines low-level procedural APIs, composable capabilities, and convenient
object-oriented interfaces for traditional shooting game development.

## Design Goals

- Build reproducible, testable fixed-tick simulations with `Runtime`, `Session`,
  and `World`.
- Implement procedural gameplay with stable entity handles, components,
  queries, and command buffers.
- Provide composable wrappers for actors, projectiles, patterns, stages,
  players, enemies, and bosses.
- Allow applications to replace controllers, combat rules, asset formats,
  stage parsers, and presentation layers.
- Reuse the same simulation in real-time games, headless tests, replay tools,
  and editor environments.

## Modules

| Module | Responsibility |
| --- | --- |
| `core` | Time, errors, binary codecs, and fundamental types |
| `game` | World, components, events, state, queries, and system scheduling |
| `stg` | Actors, projectiles, combat, items, scoring, and patterns |
| `flow` | Stage programs, phases, conditions, and actions |
| `control` | Player input, AI, scripts, and external commands |
| `asset` | Custom sources, format decoders, dependencies, and caching |
| `presentation` | Simulation snapshots and presentation events |
| `frontend` | SDL windowing, rendering, and audio services |
| `ecl` | ECL compatibility adapter |

## Requirements

- A compiler with C++23 support
- [xmake](https://xmake.io/)
- Windows, Linux, or macOS
- Full Xcode command-line tools when building on macOS

xmake fetches SDL3, OpenAL Soft, FreeType, GLM, spdlog, stb, nlohmann/json,
and Catch2.

## Build

Build the framework and the TH06 example:

```powershell
xmake build shiki
xmake build th06
```

Run the example:

```powershell
xmake run th06
```

Shaders are generated for the host platform:

- Windows: DXIL and SPIR-V
- Linux: SPIR-V
- macOS: metallib

Windows and Linux download DXC when it is unavailable. Set `SHIKI_DXC_URL` to
use a mirror or custom download URL. macOS uses `xcrun metal` and
`xcrun metallib`.

## Runtime Assets

The `th06` example and test executable load runtime resources from the
`assets/` directory beside the executable. With the default xmake
configuration, this is
`build/<platform>/<architecture>/<mode>/assets`. Prepare this directory before
running the example or complete test suite, and do not commit it to the
repository.

## Tests

```powershell
xmake build tests
xmake run tests
```

The test executable and `th06` share the same target directory and `assets/`
package.

## Documentation

```powershell
cd docs
npm install
cd ..
xmake docs
```

Docsify output is written to `build/docs/`, with the Doxygen API reference at
`build/docs/api/`.

## Release Packages

Each release provides a platform-specific SDK ZIP. Its `bin/` directory
contains the shared library, platform linker library where applicable, the
example executable, and compiled shaders. The package also includes public
headers, framework and example source code, resource tools, and offline
documentation.

## Contributing

Read [CONTRIBUTING.md](CONTRIBUTING.md) before opening an issue or pull
request. Commit messages must follow the
[Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/)
specification.

## Support

If ShikigamiSTG is useful to you, you can support its continued development on
[Afdian](https://ifdian.net/a/ca1cdqy).

## License

ShikigamiSTG is licensed under Apache-2.0. Third-party dependencies remain
subject to their respective licenses and terms.

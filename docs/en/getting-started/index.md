# Getting Started

This section will guide developers with no prior engine experience through a first runnable project.

Planned topics:

- Install the compiler, xmake, and runtime dependencies
- Create a minimal `GameDefinition`, `Runtime`, and `Session`
- Register a player, enemies, and a stage
- Load texture and audio assets
- Build, run, and debug the project

Until the complete tutorial is available, use the application wiring in `examples/th06` as the working reference.

## Web Build (WASM)

The engine and examples can be compiled to WebAssembly and run in a browser.

1. Install and activate the [Emscripten SDK](https://emscripten.org/)
   (`emsdk install latest && emsdk activate latest`)
2. Configure the wasm platform: `xmake f -p wasm -y`
3. Build the examples: `xmake build wave_particle` and `xmake build th06`
   (outputs land in `build/wasm/wasm32/release/`)
4. Serve the repository root over HTTP (e.g. `python -m http.server 8000`)
   and open `http://localhost:8000/web/`

Web builds use a native WebGL2 backend that mirrors the desktop batching
architecture, so no WebGPU is required. Desktop builds keep the SDL3 GPU API;
the renderer, texture, and real-time frontend expose one platform-neutral
API and switch backends at compile time. Shaders are authored once in HLSL
and translated at build time. See `web/README.md` for the full workflow.
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
3. Build the resource-free demo: `xmake build wave_particle`
   (outputs land in `build/wasm/wasm32/release/`)
4. For the TH06 example, package the extracted assets into an Emscripten
   data file once (not part of the build), then build:

   ```powershell
   python tools/package_wasm_data.py --assets build/windows/x64/release/assets
   xmake build th06
   ```
5. Serve `build/wasm/wasm32/release/` over HTTP (e.g.
   `python -m http.server 8000`) and open the generated `.html` page.

Web builds use a native WebGL2 backend that mirrors the desktop batching
architecture, so no WebGPU is required. Desktop builds keep the SDL3 GPU API;
the renderer, texture, and real-time frontend expose one platform-neutral
API and switch backends at compile time. Shaders are authored once in HLSL
and translated at build time.
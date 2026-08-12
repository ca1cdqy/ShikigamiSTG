# Web Build (WASM)

The engine and examples compile to WebAssembly. Build outputs land in the
normal xmake target directory (`build/wasm/wasm32/release`), and `web/index.html`
is a launcher that opens the generated pages with relative links.

The web backend is a native WebGL2 renderer that mirrors the desktop
batching architecture (vertex buffer, texture/blend batching, viewport and
scissor), so draw-call counts stay in the same range as the desktop GPU
backend. No WebGPU is required. Desktop builds keep the SDL3 GPU API
(Direct3D 12 / Metal / Vulkan); wasm builds compile the WebGL2 backend at
compile time.

## Build

1. Install and activate the [Emscripten SDK](https://emscripten.org/).
2. Configure and build:

   ```powershell
   xmake f -p wasm -y
   xmake build wave_particle
   xmake build th06
   ```

   The TH06 web build preloads the same `assets` folder used by desktop
   builds (from `build/windows/x64/release/assets`) into the virtual
   filesystem. Shaders are generated from the single HLSL source into
   `build/shaders` (DXC + SPIRV-Cross) and preloaded to `/shaders`; run a
   desktop build once so the wasm build can reuse them. On Windows it also
   preloads `C:/Windows/Fonts/msgothic.ttc` as `/fonts/msgothic.ttc` so
   in-game text renders; other hosts can drop a CJK font at that path inside
   the wasm filesystem.

## Run

Serve the repository root over HTTP and open the launcher:

```powershell
python -m http.server 8000
# open http://localhost:8000/web/
```

Direct pages (served from `build/wasm/wasm32/release`):

- `wave_particle.html` — wave/particle demo
- `th06.html` — TH06 example (requires the preloaded assets)

Wave/particle controls: Space pauses, R restarts the deterministic
session. Esc quits on desktop builds; the browser build ignores it.

## Notes

- Audio goes through Emscripten's OpenAL implementation backed by WebAudio;
  it initializes lazily after the first user gesture.
- The engine renderer, texture, and real-time frontend expose one
  platform-neutral API (`shiki::Renderer`, `shiki::Texture`,
  `shiki::frontend::Realtime`); the backend switch is a compile-time
  `#if defined(__EMSCRIPTEN__)` choice, so there is no virtual dispatch.
- Shaders live as a single HLSL source (`assets/shaders/sprite.*.hlsl`);
  the build translates them with DXC and SPIRV-Cross to DXIL, SPIR-V,
  GLSL ES, and MSL. No hand-maintained GLSL or Metal sources are kept.
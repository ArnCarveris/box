# Engine core: completion report

## What was built
- bake3 package `box` with flecs (cmake, local repo), wgpu-native v29 (cargo), glfw 3.4 and stb bundles; `apps/box_app` executable; `test/core` test project.
- `box.types`: reflected `vec2/3/4`, `mat4`, `rgb`.
- `box.core`: `App` singleton, command line (`--headless`, `--frames`, `--timeout`, `--cpu-budget`, `--fps`, scene paths), `/tmp/box/<pid>` process folder, CPU budget pacing, load-average oversubscription check, frame loop (emscripten main loop on web).
- `box.surface`: `Surface` component whose on_set opens a GLFW window or an offscreen target and starts the renderer; per-frame poll and submit systems; `write_to_file`/`write_nth` PNG capture.
- `src/renderer` (no flecs): three-packet queue with free and ready lists, renderer thread, WebGPU device setup, placeholder triangle, texture readback to PNG.
- `src/platform`: GLFW + CAMetalLayer on macOS, HTML canvas on web.
- Wasm build with shell page, hash arguments and first-frame overlay.

## Verification
- `test/core`: 25 unit tests pass (`tests.json`).
- Headless capture: `surface/deliverable/headless_capture.png` rendered offscreen and written by the engine.
- Windowed run: 120 frames at vsync in 2.1 s wall clock.
- CPU budget: `--cpu-budget 0.25 --timeout 2` used about a third of a core versus a full core uncapped.
- Wasm: `wasm/deliverable/chrome_webgpu.png` captured from headless Chrome loading `deliverable/wasm/box_app.html`.

## Notes for later epics
- wgpu-native v29 does not implement `wgpuInstanceWaitAny`; its request callbacks fire synchronously, so the native path checks results directly.
- `wgpuSurfacePresent` is unsupported on web; presentation is implicit per animation frame.
- bake runs apps from their project directory, so app scenes live in `apps/box_app/etc`.
- Shaders are still inline strings; file-based assets need an embed strategy shared by native and wasm.

## Running the deliverable
```
python3 -m http.server 8080 -d project/db/engine_core/deliverable/wasm
open http://localhost:8080/box_app.html
```

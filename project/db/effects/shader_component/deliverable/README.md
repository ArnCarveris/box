# Shader component

- `box.shader` holds `Shader {path, macros}` and `ShaderStatus {ok, error}`. Setting one
  preprocesses the file and sends `box_shader_cmd_t` to the renderer, which compiles a
  module and rebuilds the pipelines of that slot, keeping the previous ones when it fails.
- Paths resolve against the search path: the working directory, `apps/box_app` under it or
  the nearest ancestor that has one, then the directory of the executable. That covers the
  repository root, a test project, an installed game and the embedded `etc` on wasm.
- The preprocessor does `#include "file"` relative to the including file with cycle
  detection, `#define` with identifier substitution, `#if`/`#ifdef`/`#ifndef`/`#else`/
  `#endif` (an undefined macro is 0), `#pragma once` and the engine constants such as
  `SHADOW_CASCADE_MAX`. A `#define` never replaces a component or engine macro.
- Every WGSL string left `src/renderer/shader.c` for `apps/box_app/etc/shaders/`, twelve
  shaders over thirteen `include/` files. `#include "generated/layers.wgsl"` is answered by
  the material layer registry, so the runtime recomposition of the mesh shader is now an
  ordinary preprocessor run and `box_layer_cmd_t` is gone from the packet.
- The renderer now builds its pipelines from the first packet, which carries every builtin
  slot, instead of from C string literals at device creation.
- 24 `shader` tests, 223 `render` and 90 `view` green; `bench.json` moves only within noise
  against the culling epic. `shader_macro.png` is `DEBUG_NORMALS=1` on the builtin mesh
  shader; the 25 existing scenes render bit identical.

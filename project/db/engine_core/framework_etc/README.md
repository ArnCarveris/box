# Framework etc folder and shader include path

Engine shaders move from apps/box_app/etc to the framework's own etc folder, installed by bake3, and the shader search path adds <BAKE_HOME>/etc/box/shaders at boot.

## Details

Core engine shaders under `apps/box_app/etc/shaders` do not scale to many applications; only application specific shaders belong in an app. The framework gets a top level `etc/` with `shaders/` (the mesh shader, its includes, the effect and renderer passes) and bake3 installs it to `<BAKE_HOME>/etc/box` for public projects. At import the shader module reads `BAKE_HOME` from the environment, falls back to the global `~/bake` and to the source tree for tests, and prepends `<BAKE_HOME>/etc/box/shaders` to the shader search path, so applications never reference engine shader paths; an app's own `etc/shaders` stays searchable after it for its own shaders and for overrides. On wasm the framework `etc` is embedded next to the app's.

Verification: shader tests for the search order with a fake `BAKE_HOME`, the fallbacks and an app override of an engine include; every visual scene bit identical after the move; the wasm build renders with the embedded framework shaders; the README documents where engine and app assets live.

## References

- `engine_core/etc_install`, `effects/shader_component`
- `src/shader/`, `apps/box_app/etc/shaders`, `apps/box_app/project.json`

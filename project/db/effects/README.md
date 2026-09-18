# Post process effects

A WGSL shader component with includes and macros, a per view post process graph, and the gtao, bloom, exposure, fog, tone mapping and ssr effects.

## Details

Shaders move out of C string literals into WGSL files loaded by a Shader component that supports includes and macros, which becomes the single shader abstraction for the whole engine. On top of that a post process graph runs per view: effects are entities with parameters and a shader, the graph orders them, allocates and reuses intermediate targets and resolves inputs such as color, depth, normals and motion vectors. Toggling an effect entity reconfigures the graph at runtime, which keeps the feature data driven, and a view can disable the whole stack.

The effects themselves are gtao, wide kernel bloom with firefly reduction, histogram based auto exposure, height fog, the tony tone mapper with dithering and screen space reflections. Verification is unit tests for the graph and each parameter component, visual captures per effect with an on and off pair, and benchmarks that report the pass cost at 1080p and the cost of an empty graph against a full one. The epic ships a wasm build of the demo scenes in its deliverable folder next to the test, benchmark and screenshot reports.

Components introduced: Shader, EffectGraph and the per view effect list, Gtao, Bloom, AutoExposure, ToneMapping, Fog and Ssr.

## References

- design/DESIGN.md, sections Renderer (shaders) and Graphics (effects)
- design/datamodel/effects.flecs, which this epic fills in

# Light modulator plugins and cloud shadows

A pluggable modulator on a light, a project authored WGSL function that scales the light per shaded point, with cloud shadows as the first plugin and caustics as the second.

## Details

Not a cloud shadow feature but a plugin point. `LightModulator {shader, enabled}` on a light (directional first, point and spot where the term is cheap) names a `Shader` entity whose WGSL file defines `fn box_light_modulate(p: vec3<f32>, n: vec3<f32>, l: vec3<f32>, time: f32, params: LightModulatorParams) -> vec3<f32>`, a color multiplier of the direct light at the shaded world position; the renderer splices that file into the mesh shader through a generated include the way material layers compose today, one function per modulated light selected by the light index, and multiplies the sun term after the shadow so the modulator never touches indirect light. Parameters travel in a 16 float block per light: `LightModulatorParams` is engine written from any reflected component the plugin registers with `box_light_modulator_params` (the same pattern as `Transitionable`), so a project's `CloudShadows {scale, speed, density, softness}` or `Caustics {scale, speed, intensity, depth}` is an ordinary component on the light and a script line. A light with no modulator costs nothing and the shader variant is compiled only when one exists; changing the referenced shader reloads like any `Shader` write; several modulators on one light multiply. Ship two plugins under the framework etc: `etc/shaders/light/clouds.wgsl` with `CloudShadows` (a scrolling tiled noise darkening the sun, density driven by the weather's overcast unless set by hand, switched off by a quality tier) and `etc/shaders/light/caustics.wgsl` with `Caustics` (a sum of sines caustic pattern brightening the light below a water plane), the second proving the plugin is generic and being the example a project copies for its own.

Verification: unit tests that the include is generated per light, that params reach the packet, that a light without a modulator compiles the plain variant and that a broken plugin file reports through `ShaderStatus` and keeps the previous pipeline; visual scenes `cloud_shadows` (partial overcast over a plain) and `caustics` (a pool floor), every other scene bit identical; a bench case for the sun term with and without a modulator.

## References

- `project/db/effects/deliverable/README.md` (Shader entities, generated includes, reload), `project/db/lighting/deliverable/README.md`, `project/db/weather/deliverable/README.md`
- `etc/shaders/include/pbr.wgsl` (`shade`, the sun term), `etc/shaders/include/light.wgsl`
- `/Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else/etc/engine.flecs` (the old `cloud_shadow` block this replaces)

# Image based lighting

`EnvironmentLight` in `box.light` is a singleton with a sky and a ground `rgb`,
an `intensity` and a `source` reserved for the cubemap or sky entity a later
epic supplies. Extract copies it into `box_view_t` as linear colors plus a
generation that only increments when a field changes; without the component the
defaults (white sky, 128 grey ground, 0.25) reproduce the old ambient term.

On a generation change `src/renderer/environment.c` renders the gradient into a
64 px RGBA16F cubemap, prefilters it into a 5 mip specular cubemap with 64
sample GGX importance sampling (one mip per roughness step) and convolves a
16 px irradiance cubemap with 128 cosine samples; the 64 px split sum BRDF table
is integrated once at startup. The mesh shader replaces the hardcoded ambient
with irradiance * albedo * (1 - metallic) + prefiltered * (F0 * A + B).

Numbers: 8 tests in `render.Environment` pass, `Environment.extract` is 343 ns
and no case regressed over the clustered forward baseline. `pbr_spheres` runs
300 headless frames in 0.19 s (0.26 ms per frame); forcing a prefilter every
frame in a local build adds 1.6 ms per frame, the cost of its 42 small passes.

Limitations: scripts cannot change a component every frame and the renderer has
no GPU timers, so the every frame prefilter number is that local build and not a
checked in scene. The environment is one singleton for all views, is never drawn
as a background, and `source` is ignored until textures land.

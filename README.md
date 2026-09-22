# Box

A data-driven 2D/3D game framework built on [Flecs](https://github.com/SanderMertens/flecs)
and WebGPU. Scenes, assets and configuration are entities and components; the
engine reacts to component changes. See `design/DESIGN.md` for the full brief.

## Prerequisites

- [bake3](https://github.com/SanderMertens/bake3) on the path
- The flecs repository checked out at `../../flecs`
- A Rust toolchain (wgpu-native is built with cargo)
- For wasm builds: emsdk at `~/GitHub/emsdk` or `$EMSDK`

## Build and run

```sh
bake3 --local-env                                          # build the library, app and tests
bake3 run box_app --local-env --cfg release                # open a window with the default scene
bake3 run box_app --local-env -- etc/scenes/capture.flecs  # run a scene (paths are relative to apps/box_app)
bake3 run box_app --local-env -- --headless --frames 10    # headless run, exits after 10 frames
```

## Using the framework

An application creates a world and imports the `Box` module and nothing else:

```c
ecs_world_t *world = ecs_init();
ECS_IMPORT(world, Box);
```

`Box` imports every module of the framework in one fixed order. Component ids are
process globals that every world forces, so a world that imported the modules in
another order would collide with the ids of the first; importing only `Box`, in
applications, tests and benchmarks alike, keeps every world identical.

## Views and effects

Rendering requires an explicit `View`. The built-in effects and material layers
are prefab definitions: an application instantiates the effects it needs and
registers them, in execution order, with the view. Shadows, effects, and
material layers are configured independently for every view. The first rendered
view supplies the light-baking settings for the shared scene bake.

`View.culling.min_dimension_px` sets a per-view minimum projected size for static
instances. An instance is culled when either its projected width or height is
smaller than the threshold, measured in the pixels of the view's render target.
The default is zero, which disables this size check. It applies to static
batches selected for GPU culling by `View.culling.gpu_threshold`.

```flecs
using box.*

post {
  bloom : box.effects.bloom {
    Bloom: {intensity: 0.3}
  }
  tonemap : box.effects.tonemap {}
}

view {
  View: {
    camera: camera,
    culling: {enabled: true, gpu_threshold: 2048, occlusion: true},
    shadows: {enabled: true, resolution: 2048, max_distance: 200},
    bake: {enabled: true, budget: 16384, probe_spacing: 1},
    effects: [
      {effect: post.bloom, enabled: true},
      {effect: post.tonemap, enabled: true}
    ],
    material_layers: [
      {layer: box.material.layers.dust, enabled: true}
    ]
  }
}
```

Bloom uses the flecs-engine additive pyramid and defaults to an intensity of 0.3,
a threshold of 1, and a low-frequency boost of 0.7. Tonemapping maps the resulting
HDR highlights to the display.

`Bloom.prefilter` contains `threshold` and `threshold_softness`. Set the threshold
to zero for thresholdless bloom. `low_frequency_boost` and
`low_frequency_boost_curvature` control the broad halo; `high_pass_frequency`
limits the coarsest contributions. These replace the former `radius` and `knee`
settings. Existing scenes need to remove those two fields.

`karis` enables highlight suppression and `firefly_clamp` limits sampled
luminance (zero disables the clamp). `full_res_first_mip` defaults to true and
starts at half the scene resolution. When false, `max_mip_dimension` caps that
level's longest dimension, defaulting to 512. The pyramid retains Box's RGBA16F
format and extends down to approximately four pixels on its shortest axis.

`temporal` blends neighborhood-clamped history from the previous frame, with
values limited to 0–0.98. It defaults to zero. History belongs to each view and
is discarded on resize or when temporal filtering is disabled; frame gaps
restart accumulation. Like flecs-engine, this filter does not reproject motion.

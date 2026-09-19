# Visual scenes

Every scene in this directory is rendered by `python3 tools/integration.py visual`
and compared against the baseline image of the same name under `baseline/`. A
scene is a regression test with a picture instead of an assertion: a change
that alters what the engine draws shows up as a mismatch instead of going
unnoticed, without anyone looking at screenshots.

```
python3 tools/integration.py visual                          # render and compare every scene
python3 tools/integration.py visual test/integration/visual/tint.flecs   # only this one
python3 tools/integration.py visual --update-baseline test/integration/visual/tint.flecs
                                                             # regenerate one baseline, on purpose
```

Comparison tolerates GPU tie-break speckle: a scene fails only when the largest
per channel delta exceeds 24 and more than 200 pixels differ by more than 16.
The runner prints `max`, `mean` and the pixel count for every scene.

Rules for adding one:

- The scene is fully defined in script; anything it needs is reachable from
  `.flecs`, or the engine gains the component that exposes it.
- Keep it small and fast, a few seconds per render at 960 by 540.
- The captured frame is an early one, so anything that needs history across
  frames cannot be guarded by a baseline; cover that with a C test.
- The scene is deterministic run to run; every scene below renders within the
  tolerance on repeat runs. Headless frames step with a fixed delta and the
  runner's harness sets `ScriptSettings: {enabled: false}` so templates expand
  in one frame; `budget_ms: 0` with a `templates_per_frame` limit is the
  deterministic way to test incremental loading itself.
- A task only inspects and updates the baseline of the scene it works on; every
  other scene must keep matching.

## Feature coverage

| Feature | Scene | What to look for |
| --- | --- | --- |
| Every primitive shape with materials, a camera and a sun | `primitives` | Eighteen shapes on a floor, lit and shadowed. |
| Clustered point and spot lights | `lights_city` | 324 colored point lights and 6 spots over a box city with distinct falloff. |
| Image based lighting | `pbr_spheres` | Roughness by metallic sphere grid: smooth metals mirror the gradient, rough dielectrics go diffuse. |
| Baked lighting and emissive | `baked_room`, `dynamic_room` | The same room with static and dynamic lights; the pillar shadows the wall, emissive boxes light their surroundings. |
| Textures and normal maps | `textured`, `normal_bump` | Checker box and sphere, a bump mapped box lit like the sphere beside it, a roughness ramp. |
| SVG textures | `svg_icons` | Three icons at 64, 256 and 1024 px, crisp at the large size. |
| Texture tiling and wrap modes | `tiling` | Identical world size checkers over 1, 2 and 6 m boxes, a cylinder and a sphere, plus offset, mirror, clamp and fit. |
| Blend modes and back faces | `blending`, `back_faces` | Overlapping panels in several modes sorted correctly; the three back face modes. |
| Milk glass transmission | `milk_glass`, `glass_sweep` | A frosted and a clear panel; six panels from sharp to fully frosted with no blockiness. |
| Material layers | `layers` | A crate and sphere plain, with dust, edge wear, grime, wetness and all four. |
| Views and render targets | `two_views` | The world from a front camera and from a 3/4 camera into a texture target. |
| Render layers | `layers_views` | Two views showing only their own layer, lights and the shared object. |
| Frustum culling | `culling` | Identical with culling on and off; the bench reports culled counts. |
| Cascaded shadows | `shadows`, `shadows_low_sun`, `shadows_dynamic` | Shadows attached to casters, sharp near and softer far, no acne, no seams, moving casters. |
| Parent and ChildOf hierarchies | `parent_tree` | A spinning Parent tree carrying arms and hands, a mixed tower. |
| Tint composition | `tint` | Primary tints on single objects and on every hierarchy flavor, parent color plus child darkening on the grandchild. |
| Every effect at once | `showcase`, `showcase_off` | The whole chain over transparency, glass, layers and PBR textures; the pair shows what the chain does to the same content. |

## The showcase scene

`showcase` runs gtao, ssr, height fog, bloom, auto exposure and tone mapping
together over one scene, and `showcase_off` is the same content with every
effect off. Left to right, each region carries a check:

- The checker wall and the three layered crates on the left: occlusion in the
  corner, between the stacked crates and under their edges, with the dust, edge
  wear and grime layers still reading through it.
- The polished slab in the middle: reflections of the metal ball, the spinning
  box and the emissive bar, which fade into the fog with distance instead of
  reaching the fogged floor at the back.
- The emissive bar and the spark: bloom from the tone mapped highlights, so
  halving the strength of the bar halves its halo instead of leaving it clipped.
- The blue alpha panel, the green additive panel and the frosted glass panel:
  neither the occlusion nor the reflections treat them as the surface behind
  them, and the frosted panel blurs the same HDR image the chain reads, so the
  lamp behind it rolls off instead of clipping.
- The colonnade at the back: height fog over everything the chain did before it,
  and a sky gradient the tone map dithers instead of banding.

The two images differ over the blended panels beyond the chain itself: a view
with a chain blends them in linear light in its float target, a view without one
blends them in the space its 8 bit target is shown in.

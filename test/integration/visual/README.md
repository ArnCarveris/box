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
- A scene full of blended particles is only reproducible when its sprites are
  faint: the slot a particle lands in comes from an atomic pop of the dead
  list, so the order the sprites blend in changes between runs and only a low
  opacity makes that order stop mattering.
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
| Light modulator plugins | `cloud_shadows`, `caustics` | A drifting cloud deck darkening the sun over a plain, driven by the overcast of the weather; caustic bands brightening a pool floor and fading out up the walls above the water plane. |
| Views and render targets | `two_views` | The world from a front camera and from a 3/4 camera into a texture target. |
| Render layers | `layers_views` | Two views showing only their own layer, lights and the shared object. |
| Frustum culling | `culling` | Identical with culling on and off; the bench reports culled counts. |
| The Hidden switch | `hidden` | Half the columns of a grid gone, a hidden sphere group and a hidden sphere inside a group gone, the post of the hidden lamp unlit. |
| Cascaded shadows | `shadows`, `shadows_low_sun`, `shadows_dynamic` | Shadows attached to casters, sharp near and softer far, no acne, no seams, moving casters. |
| Parent and ChildOf hierarchies | `parent_tree` | A spinning Parent tree carrying arms and hands, a mixed tower. |
| Tint composition | `tint` | Primary tints on single objects and on every hierarchy flavor, parent color plus child darkening on the grandchild. |
| Every effect at once | `showcase`, `showcase_off` | The whole chain over transparency, glass, layers and PBR textures; the pair shows what the chain does to the same content. |
| Tone map operators | `tonemap`, `tonemap_tony` | The same shapes under the neutral curve and under the Tony McMapface lookup, which lifts the midtones and desaturates the bright ones. |
| Post process effects | `tonemap`, `auto_exposure`, `bloom`, `gtao`, `gtao_off`, `ssr`, `ssr_off`, `height_fog`, `effect_toggle`, `shader_macro` | Each effect on its own content against its off pair; the toggle scene flips one effect, the macro scene compiles a shader variant. |
| Occlusion culling | `occlusion`, `occlusion_off`, `occlusion_sweep` | Identical pictures with Hi-Z on and off; the sweep shows a camera passing occluders. |
| Sky and atmosphere | `atmosphere_noon`, `atmosphere_sunrise`, `atmosphere_sunset`, `time_of_day_morning`, `time_of_day_noon`, `time_of_day_night`, `celestial`, `celestial_moon`, `sky_fog` | Hillaire sky at three sun heights, the day cycle, sun and moon discs, aerial fog. |
| Weather | `weather_fog`, `weather_overcast` | Ground fog and an overcast sky over the same scene. |
| Rain | `weather_rain`, `weather_rain_heavy` | The same scene under a light and a full rain: slanted streaks filling the column around the camera, thicker, larger and more windblown at full rain, with the sky greyer, the air thicker and the ground, post and sphere wetter. |
| SVG features | `svg_text`, `svg_gradient`, `svg_mask`, `svg_clip`, `svg_filter` | One resvg feature per scene, rasterized crisp at its size. |
| Custom meshes and glTF | `mesh3`, `custom_shape`, `gltf` | Script authored meshes with vertex alpha, three stars and an ngon from the shape registry, an authored crate and the Khronos vertex colored box. |
| 2D shapes | `shapes_2d` | Discs, lines, borders with radii and fitted images on one sheet. |
| Splines | `spline_path`, `spline_road` | Boxes at even arc length with followers and a light on a loop; a road loop with a centre line and a smooth pipe. |
| Controllers | `first_person`, `controller_collision` | The first person controller in a small level; the same controller walked into walls and up steps. |
| Follow camera | `follow_camera` | A third person camera behind a moving box at its far preset, with a second camera at the near preset rendering the same target into a texture. |
| Collision debug | `collision_debug` | Collider wireframes per shape kind and root bounds in white, the cone in its bounds box. |
| Picking | `picking_hover` | The middle of five boxes tinted by its `on.hover` handler after the cursor sweeps onto it. |
| FPS overlay | `fps_overlay` | The frame rate panel with its bar chart in a corner of the view. |
| Transitions | `transitions` | Color and position transitions captured halfway under the fixed timestep. |
| UI | `ui_panels`, `ui_panels_small`, `ui_layout`, `ui_text`, `ui_shadow`, `ui_group_alpha`, `ui_group_alpha_direct`, `ui_hover`, `console` | Nested panels at two surface sizes, the layout gallery, paragraphs and a labelled button, box shadows, a transparent group through the texture path against the ghosting direct path, a hovered button, the open console over a scene. |
| Primitive LOD | `lod_spheres` | A field of spheres at several distances with the segment ladder selected per projected error. |
| Hierarchical LOD traversal | `lod_city` | 400 tower instances where the near ones keep their base, roof and windows and the far ones are single proxy boxes. |
| The same city without LOD | `lod_city_off` | The same silhouette with every part of every tower drawn, which is what `lod_city` is compared against by eye. |
| Detail pruning | `lod_pruning` | Three open frames at 70, 150 and 290 units: the near one keeps its bolts and railings, the far ones lose them and keep their columns, floors and the pinned sign. |
| The same frames with `LodPin` | `lod_pruning_off` | Every frame pinned, so the bolts and railings of the distant ones are drawn, which is what `lod_pruning` is compared against by eye. |
| Generated HLOD proxies | `lod_proxies` | Two rows of three props: the near one of each row keeps its parts, the crates behind it become the handful of primitives the generator clusters them into and the rigs, which carry too many materials for that, become one merged and simplified mesh. |
| Particle emitter shapes | `particles_shapes` | Six streams a few frames old: point, sphere, box, disc, the surface of a mesh and a spline, one color each. |
| Particle forces and collision | `particles_forces` | A jet splashing off a plate, a plume blown over by wind with turbulence and a burst of sparks with a speed and a color variance flattened on the floor. |
| Particle appearances and blending | `particles_appearance` | Five emitters: soft sprite smoke fading into a wall, additive fire down its gradient, sparks, a sprite sheet whose frames grow with the age, and star billboards of a mesh. |
| Particle playback and bursts | `particles_control` | A burst caught two frames into its flight, a schedule of three pulses as nested shells, a running stream and the empty ground where a stopped emitter stands. |
| Impostors | `lod_impostors` | Four mast yards fanning away: the near one keeps its masts, the two behind it are billboards of the eight views captured into their atlas, and the farthest is small enough for the box over its bounds again. |

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

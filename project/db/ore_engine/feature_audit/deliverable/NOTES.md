# Ore Else feature audit: flecs-engine to box

Every flecs-engine component, `flecsEngine_*` call and script construct the Ore Else sources under
`/Users/sandermertens/GitHub/SanderMertens/flecs-projects/flecs-engine/projects/ore_else` use, with
the box equivalent and the rule to port it by. The survey is the whole `etc/**/*.flecs` tree (the 48
engine names that appear in it), the 13 `flecsEngine_*` calls in `src/*.c`, and the script constructs
the game is written in. The `gap` column names the `ore_engine` task that closes the row; `-` means
box already has it.

## Reading the table

Engine script names drop the `Flecs` prefix (`FlecsPosition3` is `Position3`), and the UI module
drops `FlecsUi` (`FlecsUiRect` is `Rect`). Every game script opens with `using flecs.engine.*`; the
box equivalent is a `using` per module (`box.transform`, `box.geometry`, `box.material`, `box.ui`,
...), so the first line of every ported script changes.

## Transform and scene

| flecs-engine | used as | box | mapping rule | gap |
| --- | --- | --- | --- | --- |
| `FlecsPosition3` | `Position3: {x, y, z}`, 516 uses | `Position` | rename; same axes, left handed, Y up | - |
| `FlecsRotation3` | `Rotation3: {x, y, z}`, 149 uses | `Rotation` | rename; both compose Rx * Ry * Rz | - |
| `FlecsScale3` | `Scale3: {x, y, z}`, 116 uses | `Scale` | rename | - |
| `FlecsLookAt` | `LookAt: {0, 1.2, 0}` | `LookAt` | same | - |
| `FlecsDynamicTransform` | tag on the camera and moving parts | `DynamicTransform` | same tag, same meaning, applies to descendants | - |
| `FlecsWorldTransform3` | read in C (`health_bars.c`) | `WorldTransform` | `mat4 m` instead of a packed row set | - |
| `FlecsAABB` | read in C | `AABB`, `RootBounds` | `RootBounds` on a root already aggregates its subtree, which is what the game unions by hand | - |

## Geometry

| flecs-engine | used as | box | mapping rule | gap |
| --- | --- | --- | --- | --- |
| `FlecsBox` | `Box: {w, h, d}`, 309 uses | `Box` | same | - |
| `FlecsCylinder` | `Cylinder: {segments, smooth, length}` | `Cylinder` | same; radius stays in `Scale` | - |
| `FlecsCone`, `FlecsSphere`, `FlecsHemiSphere`, `FlecsIcoSphere`, `FlecsQuad` | shape components | same names | same | - |
| `FlecsBezierCurve` | `BezierCurve` on props | `Spline` with `SplineType` bezier, `SplineMesh` | box splines are one component with a type plus a mesh profile | - |
| `FlecsTerrain` | not used (only a file name) | `-` | the game generates its terrain textures with `tools/gen_terrain.py` and lays a `Box` ground; nothing to port | - |

## Material and surfaces

| flecs-engine | used as | box | mapping rule | gap |
| --- | --- | --- | --- | --- |
| `FlecsRgba` | `Rgba: {r, g, b, a}`, 485 uses | `Rgb` + `Opacity` | split: rgb bytes into `Rgb`, `a / 255` into `Opacity`, and drop `Opacity` when a is 255. A `const c: Rgba` shared by widgets becomes a `const` of the `rgba` type only where a border color wants one | - |
| `FlecsPbrMaterial` | `PbrMaterial: {metallic, roughness}` | `PbrMaterial` | same | - |
| `FlecsPbrTextures` | `PbrTextures: {albedo, normal}` | `PbrTextures` | same slots | - |
| `FlecsEmissive` | `Emissive: {strength, color}` | `Emissive` | same | - |
| `FlecsTexture` | `Texture: {"x.svg", 256, 256}` | `Texture` for raster files, `Svg` for vectors | the engine rasterizes SVG inside `Texture`; box splits them, so every `Texture` of an `.svg` becomes `Svg: {file, width, height}` | - |
| `FlecsSvg` | `Svg: {file, width, height}` | `Svg` | same | - |
| `FlecsTextureTransform` | `TextureTransform` on tiled materials | `TextureTransform`, `TextureTiling`, `TextureWrap` | box splits the offset/scale from the wrap mode and the tiling shape | - |
| `FlecsAlphaBlend` | tag on glass and plumes | `BlendMode` | a component with nine modes instead of a tag; `BlendAlpha` is the same glass model | - |
| `FlecsSurfaceEffects` | per material `{dust, grime, scratch, grain, streak, edge, burn}`, 174 uses | `(MaterialLayerOn, layer)` | one inherited pair per layer instead of one struct of switches; box ships dust, edge wear, grime and wetness, so scratch, grain, streak and burn are project layers (a WGSL body) or come with the wear task | `instance_wear` |
| `FlecsSurfaceWear` | per entity `{dust, grime, scale, seed, burn, edge, scratch}`, 209 uses | none | box parameterizes a layer with `LayerParams`, which is one value for the whole scene; the per entity channel does not exist | `instance_wear` |

## Lighting and sky

| flecs-engine | used as | box | mapping rule | gap |
| --- | --- | --- | --- | --- |
| `FlecsDirectionalLight` | sun and moon | `DirectionalLight` | same | - |
| `FlecsCelestialLight` | `{toa_intensity, toa_color}` | `CelestialLight` | same idea, check the member names | - |
| `FlecsPointLight` | `PointLight: {intensity, range}`, muzzle and vent lights | `PointLight` | same; the game's fading one shot flash light is the effects task | `one_shot_effects` |
| `FlecsStars` | `{density, cells, size, color_variation}` | `Stars` | `density` and `size` carry over, `cells` and `color_variation` have no box member; box adds `twinkle`, `fade_start`, `fade_end` | - |
| `FlecsAtmosphere` | `{sun, moon, stars}` | `Atmosphere` | same three entity members, many more knobs | - |

## Rendering, views and effects

| flecs-engine | used as | box | mapping rule | gap |
| --- | --- | --- | --- | --- |
| `FlecsSurface` | `{title, width, height, resolution_scale, vsync, target_fps}` | `Surface` + `App` | title, size and vsync map; `target_fps` moves to `App.target_fps`; `resolution_scale` has no box member | `resolution_scale` |
| `FlecsCamera` | `{fov: math.PI / 3.6, near_, far_}` | `Camera` | **fov is degrees in box and radians in the engine**; `near_`, `far_` map | - |
| `FlecsRenderView` | one `RenderView` with camera, atmosphere, shadow, effects | `View` | the view names its camera, target, layers and `effect_list`; the atmosphere is the world's, `ambient_intensity` becomes `EnvironmentLight.intensity`, `screen_px_threshold` becomes `LodSettings.error_px` | - |
| `RenderView.shadow` | `{enabled, map_size, max_range, bias}` | `ShadowSettings` | `map_size` is `resolution`, `max_range` is `max_distance`, `bias` is `depth_bias` in texels, and box cascades four times by default | - |
| `RenderView.effects` | ordered list of effect entities | `View.effect_list`, `Effect` | each effect is an entity under `box.effects.stack` with an `Effect` and its parameter component; the view lists them or runs the stack | - |
| `RenderView.surface_effects` | scene wide dust, grime, edge, burn | `LayerParams` on the layer entities | the scene wide amounts become the params of each built in layer | `instance_wear` (per entity half) |
| `RenderView.cloud_shadow` | `{strength, scale, coverage, softness, speed, direction}` | none | the cloud shadow layer over the sun | `cloud_shadows` |
| `FlecsSSAO` | `SSAO: {radius: 0.5}` | `Gtao` | horizon based instead of hemisphere sampled; `radius` maps, the rest retunes | - |
| `FlecsBloom` | `Bloom: {}` | `Bloom` | same effect, different members | - |
| `FlecsAutoExposure` | `{min_log_luma, low_percentile, min_brightness, max_brightness}` | `AutoExposure` | box exposes stops (`min_ev`, `max_ev`, `target`, `compensation`, `speed_up`, `speed_down`); the values retune, they do not convert | - |
| `FlecsTony` | `Tony: {auto_exposure: ...}` | `ToneMapping` | box has one neutral curve; Tony McMapface becomes an operator on `ToneMapping` | `tonemap_operators` |
| `FlecsRenderBatchSet`, `GeometryBatch`, `SkyBoxBatch` | batch entities listed on the view | none | box batches by itself; these rows disappear from the scene | - |

## Particles

| flecs-engine | used as | box | mapping rule | gap |
| --- | --- | --- | --- | --- |
| `FlecsParticles` | pool entities (`fx`, `glow`) shared by emitters | `ParticleSettings` + a pool per emitter | box gives every emitter its own pool under one world budget, so the shared pool entity disappears | - |
| `FlecsParticleEmitter` | emitter templates | `ParticleEmitter`, `ParticleForces`, `ParticleShape` | box splits the emitter into shape, forces and appearance components | - |
| `FlecsParticleCurves` | `ParticleCurves` in `curves.flecs` | `ParticleCurves`, `ParticleGradient` | four keys over life, same idea | - |
| `FlecsParticleBurst` | `ParticleBurst`, 29 uses, and a burst descriptor passed to C | `ParticleBurst` | box's burst is a schedule on an emitter (`{count, at, repeat}`), not a value fired at a point | `one_shot_effects` |

## Input

| flecs-engine | used as | box | mapping rule | gap |
| --- | --- | --- | --- | --- |
| `FlecsInputAction` | `Forward { InputAction: {"..."} }` | `InputAction` | same, description included; `box_input_bindings` generates the controls screen | - |
| `FlecsInputMap` | `InputMap: [keys.W: {actions.Forward}, ...]` | `InputMap` + `Binding` | a map entity with binding pairs or `Binding` children; a key constant is a u64 mask a script can combine with `\|` | - |
| `FlecsInputPriority` | `InputPriority: {0}` game, `{1}` inventory | `InputMap.priority` | one member on the map instead of a component beside it | `input_layers` |
| `FlecsInput` | read in C for the wheel and for any key | `Input` | same singleton; scroll is `mouse.scroll_delta`, "any key" has no binding | `any_key_binding` |
| `FlecsBehaviors` | `Behaviors: [script { await pressed(X) ... }]` | native `async { }` blocks | flecs script has async blocks and `await` of its own; the component and its `script { }` wrapper disappear | - |

## UI

The engine's UI is screen pixels with the origin at the top left and y down; a box UI view is
logical pixels with the origin in the middle and y up. Every authored offset changes, which is the
reason the screens are redesigned on `Layout` and `Anchor` rather than transliterated.

| flecs-engine | used as | box | mapping rule | gap |
| --- | --- | --- | --- | --- |
| `FlecsUiRect` | `Rect: {w, h}`, 40 uses | `Rect` (geometry) | same members | - |
| `FlecsUiBorder` | `Border: {radius, width, color}` | `Border` (geometry) | same members down to the per corner radii and the zero alpha rule | - |
| `FlecsUiLine` | `Line` | `Line` (geometry) | same members | - |
| `FlecsUiText` | `Text: {text, size, align}`, 45 uses | `Text` + `Font` | the string is `value`, the size moves to the `Font` the element inherits, `align` stays | - |
| `FlecsUiImage` | `Image: {texture, fit}` | `Image` + `PbrTextures` | box binds the texture through the material and `Image` only says the region and the fit; `ImageCover` and `ImageContain` are the same fits | - |
| `FlecsUiPosition` | `Position: {x, y}`, 80 uses | `Position` | top left and y down becomes centre and y up, so most of these become a `Layout` or an `Anchor` instead of a number | - |
| `FlecsUiLayout` | `Layout: {direction, padding, spacing, width, height}` | `Layout` | box adds alignment per axis and size from content | - |
| `FlecsUiAnchor` | `Anchor` | `Anchor` | same pinning idea | - |
| `FlecsUiAction` | `UiAction: {action: ...}` on buttons | `Action` (picking) | a click on the element fires the input action through the same layer | - |
| `FlecsUiHotkey` | `Hotkey: {key: "1"}` on slot buttons | `Binding` in a map | the widget takes the key mask as a prop and writes a `Binding` for its own action | - |
| `FlecsUiWidgetState` | `match this[WidgetState].hover` in every widget | `Hovered` + `on.hover` / `on.press` | box raises events; a widget keeps its own `mut` state and its look follows from it | `ui_canonical` |
| `FlecsUiSlider` | not used by the game | `-` | the slider is a script widget | `ui_canonical` |
| the 15 widget templates | `etc/ui/widgets/*.flecs` | `box.ui.widgets` script library | game widgets become thin templates over the canonical library; no widget in C | `ui_canonical` |
| the game's `Icon` | `OreIcon {texture}` on items | `Image` | an icon is an `Image` on a `Rect` with the SVG as its texture | - |

## flecsEngine_* calls

| call | where | box | mapping rule | gap |
| --- | --- | --- | --- | --- |
| `flecsEngine_cameraScreenRay` | `game.c:183` | `box_picking_ray(world, view, cursor, &origin, &dir)` | the cursor is `Input.mouse.normalized`; a view of 0 takes the first camera | - |
| `flecsEngine_rayPlaneY` | `game.c:185` | `box_ray_plane_y(origin, dir, y, &point)` | added to `include/box/math.h` by this task, with the general `box_ray_plane` beside it | - |
| `flecsEngine_cameraViewBasis` | `player.c:72`, `health_bars.c:38` | `WorldTransform` of the camera | the forward, right and up of the camera are columns 2, 0 and 1 of its matrix; `mat4_transform_dir` reads them | - |
| `flecsEngine_objectWorldAABB` | `health_bars.c:157` | `RootBounds` (or `AABB`) | box aggregates the subtree of a root itself, so the hand union over children goes away | - |
| `flecsEngine_ui2dScreenSize` | `health_bars.c:33` | `Surface.point_width/height`, `Bounds` of the UI root | the UI tree works in its own logical pixels, so the bar reads the bounds of its root | - |
| `flecsEngine_ui2dRect`, `flecsEngine_ui2dBorder` | `health_bars.c:192,329` | a UI tree with `WorldAnchor` | the bar is a template anchored to its owner, not two immediate mode rectangles and a hand projection | `world_anchor` |
| `flecsEngine_uiMouseCaptured` | `placement.c:757`, `player.c:203,307` | `box_ui_mouse_captured(world)` | one query from the UI hit test of the frame | `input_layers` |
| `flecsEngine_inputDrag` | `placement.c:201,834` | `box_input_drag(world, action, &start)` | returns whether the action is held past the threshold and where it began | `input_layers` |
| `flecsEngine_inputReject` | `placement.c:840` | `box_input_reject(world, action)` | drops the action for the frame so lower layers never see it | `input_layers` |
| `flecsEngine_inputEmit` | `ui.c:87` | `box_input_emit(world, action, phase)` | already in box | - |
| `flecsEngine_particlesBurst` | `game.c:52,108`, `combat.c:1558` | none | an effect template played once at a point from a pool | `one_shot_effects` |
| `flecsEngine_draw` | `placement.c` (ghosts, markers), `combat.c:1759` (beams), `player.c:227` | a pool of entities shown and hidden | the immediate mode API is the old engine's structure; the box way is real entities under a `ghosts` parent that are placed and hidden, which needs a hide switch | `visibility` |

## Script constructs

| construct | used as | box / flecs | gap |
| --- | --- | --- | --- |
| `module`, `using X.*`, `include` | 85, 308, 109 uses | same, with one `using` per box module | - |
| `template` + `prop` | 40 templates | same | - |
| `prefab` | 75 prefabs | same | - |
| `const`, `export const`, `export mut` | 688 and 91 uses, `cfg.*` read from C | same; `ecs_const_var_get_t` and `ecs_mut_var_get` are flecs | - |
| `if`, `for`, `match`, `enum` | 52, 41 and many uses | same | - |
| `@tree Parent`, `@name`, `@detail` | 30 and 44 uses | flecs script annotations and the doc addon | - |
| `$ { }` singleton scope, `(Rel, Tgt)` pairs | scene setup | same | - |
| `this[Component]`, `Singleton[Component]` | reactive widget styling | same | - |
| map and array literals (`StorageMap`) | the inventory | same | - |
| `script { await ... }` in `Behaviors` | 11 blocks, `try`/`catch(Cancelled)` | native `async { }` with `await`, one await per block (see the picking lessons) | - |
| `await pressed/released/dragged(action)` | input behaviours | `on.pressed`, `on.down`, `on.up`; drag comes with the input task | `input_layers` |
| `pressedAny([A, B, C])` | the inventory toggle | several `async` blocks racing on one `mut` | - |
| async functions registered from C | `inventorySet`, `placeRegion`, `cancel`, `restart` | `ecs_async_function`, which box already uses for its own events | - |
| `platform.mobile` | the quality default | flecs script builtin | - |

## Application

| flecs-engine | used as | box | mapping rule | gap |
| --- | --- | --- | --- | --- |
| `ecs_app_run` with REST and stats | `main.c` | `box_run` + `HttpServer` | the command endpoints and the inspector replace the REST addon | - |
| `--stresstest`, `--stresstest=45` | parsed before `ecs_init` | `box_parse_args` | box rejects every option it does not know, so an application cannot add one | `app_args` |
| `ORE_FIXED_DT` | env var for deterministic runs | `--fps` with `--frames` headless | box headless runs already step at a fixed delta | - |
| `OreCameraFollow` | a C system following the player | none | a follow camera belongs beside the fly and first person controllers | `follow_camera` |

## Which game C files become script

The porting rule: C stays for simulation, everything that is a look, a screen or a reaction to an
event becomes script.

| file | lines | verdict |
| --- | --- | --- |
| `health_bars.c` | 352 | **all script.** A bar template with `WorldAnchor`, a `Layout` and a fill from health. The hand projection, the AABB union and the immediate mode rectangles all go. |
| `quality.c` | 37 | **all script.** A `mut renderQuality` cycled from an `async` block on the quality button; `quality.flecs` already holds the tiers as `match`. |
| `ui.c` | 408 | **mostly script.** The hover look, the afford dimming, the tab switch and the toast fade are widget state and `match`; what stays C is the recipe cost walk that prices an order in raw ore (`oreCraftRawCost`) and the stock slot assignment, both of which are economy queries the widgets read as components. |
| `placement.c` | 1139 | **split.** The input behaviours (press, drag, rectangle on Run, cancel, pipette) become `async` blocks over the actions, and the ghosts become a pool of entities shown and hidden; the cell mapping, the reach and occupancy rules and the paint shapes stay C. |
| `player.c` | 487 | **split.** Movement, reach and mining progress stay C; the limb walk cycle and the jet plume are a template, and the camera follow is the `follow_camera` controller. |
| `buildings.c` | 820 | **split.** `OreSpinners` is `AngularVelocity`, `OreStatusLamps` and `OreWorkLights` are a `match` on the power state, `OreVents` and `OrePistons` are emitters and transitions in the building templates; the registry, the cost, the construction progress and the removal stay C. |
| `game.c` | 725 | **split.** `OreTitleAnyKey` becomes an action and an `async` block, the blast helpers become effect templates played once; the grid, the state machine and the cell helpers stay C. |
| `rules.c` | 237 | **script data, C evaluation.** The 2 by 1 neighbour patterns are a component authored in `rules.flecs`; the evaluation on a cell change stays C. |
| `combat.c`, `craft.c`, `drones.c`, `rocket.c`, `power.c`, `resources.c` | 2340, 1402, 565, 831, 186, 171 | **C.** Targeting, the order book, the drone and rocket state machines, the allocator and the resource table are simulation. Their visuals (muzzle flash, trail, beam, plume, clamps) are templates and effects. |
| `stresstest.c` | 302 | **C**, but it builds the base through the same module API the playthrough uses. |
| `main.c` | 54 | **C**, shrinks to the box imports, `box_parse_args` and `box_run`. |

## Open rows

Thirteen `ore_engine` tasks close every gap above: `input_layers`, `tonemap_operators`,
`cloud_shadows`, `world_anchor`, `ui_canonical` and `component_id_drift` were already filed; this
audit adds
`instance_wear`, `one_shot_effects`, `visibility`, `resolution_scale`, `follow_camera`,
`any_key_binding` and `app_args`. No row of the survey is left without either a box equivalent or a
task.

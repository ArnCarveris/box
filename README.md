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

Options: `--headless`, `--frames <n>`, `--timeout <s|infinite>`, `--cpu-budget <0..1>`,
`--fps <n>`, `--port <n>` (command endpoints, default 27750, 0 disables the server).
An application adds options of its own as `AppOption` entities before it calls
`box_parse_args`: the name of the entity is the flag, its value lands in the member of a
singleton component the scene then reads, and `--help` lists it with the engine's. The
value comes from `--flag=value`, from `AppOption.fallback` when the flag stands alone, from
`true` when the member is a bool, and from the next argument otherwise; a flag no option
claims is still an error.
Headless runs step with a fixed delta of one over `--fps` (60 by default) so captures are
deterministic, require `--frames` or `--timeout`, write their temporary
files to `/tmp/box/<pid>` and refuse to start when the machine's load exceeds its
core count. A scene that sets `Surface.write_to_file` writes each frame as PNG:

```
using box.surface
$ { Surface: {width: 640, height: 360, write_to_file: "/tmp/frame.png"} }
```

## Scenes

Scenes are flecs scripts. `apps/box_app/etc/scenes/primitives.flecs` shows every
primitive shape with materials, a camera and a directional light:

```
using box.transform
using box.geometry
using box.material
using box.camera

camera { Position: {0, 4, -12}  LookAt: {0, 0.5, 0}  Camera: {fov: 55} }
sphere { Position: {-3, 1, 0}  Sphere: {segments: 32, smooth: true, radius: 1}  Rgb: {80, 160, 240} }
```

`apps/box_app/etc/scenes/capture_camera.flecs` adds `Capture: {path:
"/tmp/box/capture_camera.png"}` to its camera, which renders that camera to the
PNG once and removes the component again.

`test/integration/visual/lights_city.flecs` lights a grid of boxes with a few
hundred colored point lights and a handful of spot lights, which the clustered
forward path shades in one pass.

`test/integration/visual/pbr_spheres.flecs` is the image based lighting check: a
7 by 7 grid of spheres with roughness across the columns and metallic up the
rows under an `EnvironmentLight` with a blue sky and a warm ground.

`test/integration/visual/textured.flecs` is the texture check: a checkered box,
sphere and floor, a normal mapped box and a panel with a roughness ramp, bound
through `PbrTextures`. `apps/box_app/etc/textures/` holds the PNGs it loads, which
`python3 tools/gen_textures.py` regenerates.

`test/integration/visual/svg_icons.flecs` is the SVG check: the three hand written
icons in `apps/box_app/etc/svg/` rasterized by `Svg` at 64, 256 and 1024 pixels and
shown side by side on quads, so the same vector art is blocky on the left and crisp
on the right. `svg_text`, `svg_mask`, `svg_clip`, `svg_gradient` and `svg_filter` are
the resvg feature checks, one per SVG in `apps/box_app/etc/svg/`, with text set in the
fallback font `apps/box_app/etc/fonts/B612-Regular.ttf` that the engine loads next to
the system fonts.

`test/integration/visual/tiling.flecs` is the tiling check: one checker texture
at one meter over boxes of one, two and six meters, a cylinder and a sphere, with a
second row that offsets the texture transform, mirrors and clamps the wrap mode and
tiles the same material at three sizes.

`test/integration/visual/blending.flecs` is the blend mode check: alpha blended,
additive, multiply, screen and subtractive panels overlapping each other and the
primitives behind them, drawn back to front. `milk_glass.flecs` holds a frosted
panel with opacity 0.5 and roughness 0.8 next to a clear one over the same
textured geometry, `glass_sweep.flecs` rows six panels of opacity 0.5 at
roughness 0 to 1 over checker boxes and spheres, and `back_faces.flecs` shows
three open boxes of quads, one per `BackFaceMode`, with the camera looking into
them.

`test/integration/visual/layers.flecs` is the material layer check: the same
textured crate and sphere six times, without layers and with dust, edge wear,
grime, wetness and all four at once. A layer is an entity under
`box.material.layers` with a `MaterialLayer` WGSL body and a `LayerParams`
holding its two scene global vectors; a material enables one with the pair
`(MaterialLayerOn, box.material.layers.dust)`. Dust takes its color and amount
from `a`, and noise frequency, roughness and up facing sharpness from `b`. Edge
wear takes the metal color and amount from `a` and the band width, roughness and
noise frequency from `b`. Grime takes its albedo multiplier and amount from `a`
and noise frequency and roughness from `b`. Wetness takes the wet roughness in
`a.x` and the amount in `a.w`, and noise frequency, pooling and noise weight from
`b`. `MaterialLayerSettings` turns every layer off at once.

`test/integration/visual/baked_room.flecs` is the baked lighting check: a tiled
room with static point lights, emissive boxes and a pillar whose shadow the bake
resolves, next to `dynamic_room.flecs` which lights the same room with the lights
marked `DynamicTransform`.

`test/integration/visual/layers_views.flecs` is the render layer check: red
objects in layer `a`, blue objects in layer `b`, a green pillar in both and a
light per layer, drawn by two views of the same camera. The main view lists
layer `a` and the second view lists layer `b` and renders into a `RenderTarget`
its camera captures. A layer is any entity an entity or a light joins with the
pair `(box.view.Layer, layer)`; `View.layers` and `View.shadow_layers` hold up to
eight layers each and show every layer when they are empty.

`test/integration/visual/parent_tree.flecs` is the hierarchy check: a spinning
node whose children use the non fragmenting `Parent` storage (`@tree Parent` in
the script) carries two arms with spheres and a tumbling box, a tower mixes a
`Parent` child with a `ChildOf` subtree under it, and a `Tint` on the spinner
resolves into its children and grandchildren next to an untinted white box.

`test/integration/visual/culling.flecs` is the frustum culling check: a ring of
boxes and spheres around the camera with a row behind it, which `CullSettings`
on a view or as a singleton switches off for a bit identical comparison.

`test/integration/visual/resolution_scale.flecs` is the resolution scale check:
the same checkered floor, crate, ball, post and UI panel rendered at
`Surface: {resolution_scale: 2}`, which is half the pixels on each axis
magnified back to the window. `resolution_scale` divides the framebuffer the
scene is drawn into; one and anything below it is the full resolution, two is a
quarter of the pixels and the knob a quality tier turns on a weak GPU. The swap
chain keeps the size of the window, every view that draws into the surface
renders at the scaled size with its depth target, its effect chain and its
overlay, and one linear pass magnifies the image before the frame is presented.
Everything authored stays in points: a UI tree keeps its logical size,
`box_picking_ray` keeps taking a normalized cursor and a `Surface.write_to_file`
capture writes the window size. `Surface.scaled_width` and
`Surface.scaled_height` report what the scene got.

`test/integration/visual/hidden.flecs` is the visibility check: half the columns
of a grid carry `box.render.Hidden`, one group of spheres is hidden through its
parent, one sphere of a second group is hidden on its own, and one of two lamps
is hidden so its post stays unlit. `Hidden` takes an entity and everything below
it out of every view, every shadow, every pick and every collision query while
it stays in the world with its components and its children, which is how a pool
of ghosts, a pooled effect or a panel is shown and hidden instead of created and
destroyed. The tag is inherited two ways: a hidden prefab hands it to its
instances and a hidden entity hides its subtree. A renderable that is hidden
gives its static slot back and takes one again when it is shown, a light leaves
the packet and the bake, a directional light hands the sun of the view to the
next one, a collider leaves the spatial index and the bounds of its root shrink
around what is left. A particle emitter keeps running: `box.particle` does not
read the switch yet, so a hidden emitter still spawns and still draws.

`test/integration/visual/lod_spheres.flecs` is the primitive reduction check:
four rows of spheres authored at 32 flat shaded segments, which the level
ladder draws at 32, 16, 8 and 4 segments as their projected size falls. A scene
opts in with `LodSettings` on a view or as a singleton, and `enabled: false`
draws every shape at its authored count again. `test/integration/bench/lod_field.flecs`
is the same idea at scale: 20000 spheres and 2000 cylinders over 700 units of
depth, where the ladder is what keeps the triangle count of the frame down.

`test/integration/visual/lod_city.flecs` is the hierarchical LOD check: 400
tower instances where the near ones keep their base, roof and window strips and
the far ones are drawn as one proxy box each, next to `lod_city_off.flecs`,
which draws every part of every tower. The traversal treats every template or
prefab instance as a node, stacks renderer only clusters over those roots and
walks the result once per view instead of visiting the renderables of the scene;
`LodStats` on a view or as a singleton reports the nodes one walk visited, drew
and replaced. `test/integration/bench/lod_city.flecs` is the same at scale: four
million leaves under 50000 tower roots with a flying camera, which draws about
36000 instances a frame and only fits in the instance buffer because the
traversal replaces the distant towers.

`test/integration/visual/lod_pruning.flecs` is the detail pruning check: three
open frames at 70, 150 and 290 units, too sparse for a proxy, whose bolts and
railings the traversal drops as their projected contribution falls while the
columns, the floors and a `LodPin` sign stay. The contribution of a node is the
edge of the square with the area of the widest face of its bounds, scaled by the
color contrast against its parent, so a part that carries the color of what it
sits on counts for a quarter of its size. `LodPin` on an entity holds it and
everything below it out of the test, which is what `lod_pruning_off.flecs` uses
to draw every part.

`test/integration/visual/occlusion.flecs` is the Hi-Z occlusion check: a wall in
front of 2400 boxes whose capture is bit identical to `occlusion_off.flecs`,
next to `occlusion_sweep.flecs`, which `python3 tools/occlusion_sweep.py` sweeps
past walls at 60 units per second and compares frame by frame with occlusion on
and off; `CullSettings.occlusion` switches the test off and `hiz_levels` caps the
depth pyramid.

`test/integration/bench/instances_1m.flecs` is the instance volume check: a
million small cubes in 50000 template instances over a tiled ground, seen from
above. The renderer asks the adapter for its own limits instead of the WebGPU
defaults, grows every instance buffer by doubling up to what those limits allow
and writes the static slots of a packet one run of neighbouring slots at a time,
so a scene of this size renders instead of failing on an oversized buffer.

Entities are static unless they or an ancestor have `DynamicTransform` or
`DynamicMaterial`; static entities are only re-uploaded when a component changes.
`Velocity` and `AngularVelocity` from `box.movement` mark entities dynamic.

`test/integration/visual/tint.flecs` checks tint on single objects and on ChildOf, Parent and mixed hierarchies with tints set at different levels.

The `Input` singleton of `box.input` holds the key, mouse and text state of the frame,
filled in OnLoad from the OS events the platform layer queues and from injected events,
which are indistinguishable from real input; key codes are ASCII where possible and are
exported as script constants under `keys` (`keys.W`, `keys.MouseLeft`).

`apps/box_app/etc/scenes/input_map.flecs` is the action layer map: actions are entities
under the `actions` scope with an `InputAction`, an entity with `InputMap` binds them with
`(keys.W, actions.Forward)` pairs or with `Binding` children that carry a full key mask
like `keys.Ctrl|keys.S`, and a UI button or a command fires the same action without a key.
`keys.Any`, `keys.AnyKey` and `keys.AnyMouse` are masks that resolve on whatever key,
keyboard key or mouse button is down and report the one that fired through the mask of the
action event, so a title screen binds one action instead of every key.
A script task awaits an action with `await on.pressed(actions.Forward)` inside an `async`
block, next to `on.down`, `on.up` and `on.dragged`; the await returns the source that fired
the action, and `on.dragged` returns where the drag began and where the cursor is now, in
every frame the action is held past three window points.

Maps are layers. `InputMap.priority` orders them and the highest resolves first, and a
binding that declares `consume: true` (or a map that declares it for all of its bindings)
keeps its key and its action from every map below it, which is how a UI map sits over the
map of a game. Nothing else is swallowed: a map that does not say so leaves the layers below
alone and the handler decides. A handler that runs inside a raise takes it with
`event.stop()`, and a task that resumes after the delivery takes a whole action for the
frame with `input.consume(actions.Close)`; either clears the actions the maps below bound to
the same key. `box_ui_mouse_captured` and the `UiCapture` singleton say whether the cursor is
on a UI element, so a game stands back while the pointer is on a panel.

A terminal drives both layers with the `key.*`, `mouse.*`, `input.*` and `action.*` commands,
which queue injected events over frames; `test/integration/command/input.flecs` types, clicks
and drags through them and `input_layers.flecs` holds two keys while a map above keeps one of
them.

`CameraController` from `box.controller` flies a camera with the `Forward`, `Back`, `Left`,
`Right`, `Up`, `Down`, `Fast` and `Look` actions the module binds to WASD, QE, shift and the
right mouse button under `box.input.map`; it writes `Position`, `Orientation` and `LookAt`,
turns with the cursor while the surface captures it, and pushes its sphere of `radius` out of
the scene through `box_collision_move` (`no_collision` disables it, `terrain` stays a fallback
floor). `FirstPersonController` walks the same actions in the XZ plane with a walk and a run
speed, falls with gravity, jumps with `Jump`, lowers its eye with `Crouch` and toggles free
flight with `Fly`; its capsule of `radius` slides along walls, climbs ledges up to `step` and
stands on any collider (`foot` reports the ground). `test/integration/visual/first_person.flecs`
stands it in a small level and `controller_collision.flecs` walks it into walls and steps;
`test/integration/bench/first_person_1m.flecs` walks it through a million colliders.

`FollowCamera` is the third person one: it writes `Position` and `LookAt` every frame from the
`Position` of its `target` plus an offset, gliding toward that place at `glide` per second so
it lags behind a target that runs, and it travels there through the scene the way the fly
camera does. `levels` holds up to four zoom presets, each an offset and a look offset, and the
`ZoomIn` and `ZoomOut` actions the module binds to `=` and `-` step `level` through them
together with the wheel, which stops at the presets that are authored and is ignored while
`UiCapture.mouse` says the cursor is on a UI element.
`test/integration/visual/follow_camera.flecs` follows a moving box at two presets.

`JobSettings` sizes the worker pool that runs large tasks off the main thread and
caps the milliseconds their completions may take in PreUpdate, which is how SVG
rasterization and light baking stay off the frame; `JobStats` reports the queue.
Setting an `Svg` queues one job per horizontal band of the image, nearest to the
camera first, and the texture keeps its previous image until the last band lands;
`apps/box_app/etc/scenes/svg_storm.flecs` rasterizes a wall of them at once.

`apps/box_app/etc/scenes/templates.flecs` is the incremental loading demo: a ground
that appears at once and 4096 instances of a `Pillar` template that `box.script`
expands over frames, nearest to the camera first, inside the `ScriptSettings`
budget (`templates_per_frame`, `budget_ms`, `nearest_first`, `enabled`) while
`ScriptStats` reports the queue and the worst frame.

`CpuMonitor`, `EcsStats` and `SystemMonitor` from `box.monitor` publish process
and machine CPU usage, the flecs statistics addon and the per system frame
breakdown as components; a source is only sampled while its singleton exists,
and the CPU budget of a headless run shares the CpuMonitor sampling.

`GpuTimings` is the same for the GPU: while the singleton exists the renderer
writes a timestamp query around every pass it records, resolves them into a
buffer it reads back one or two frames later and the component lists the
milliseconds per pass name (`shadow`, `depth`, `opaque`, `sorted`, `sky`,
`glass`, `lines`, `overlay`, the compute passes and `effect.0` and up for the
chain of a view), with `frame_ms` as their total and `frame` as the frame they
were measured in. Passes that share a name, such as the cascades of a shadow
map or the same pass of two views, report what they spent together. Nothing is
created and no pass is timed while the component is absent, and a device
without the `timestamp-query` feature reports `supported: false` and no passes
instead of failing. `test/integration/command/gpu_timings.flecs` prints the
table over the terminal with `get box.monitor.GpuTimings box.monitor.GpuTimings`.

`Mesh3` holds script authored vertices, normals, uvs and indices and joins the instanced path
through the same mesh cache as the primitives (`test/integration/visual/mesh3.flecs`), and a
`Shape` registration lets a project add its own parametric shape with the same sharing; the
engine ships a small `Star` as the example (`custom_shape.flecs`).

`box.sky` renders a physically based atmosphere from precomputed transmittance, multiple
scattering and sky view tables as the view background and as the IBL source when the
environment names it (`atmosphere_sunrise.flecs`, `atmosphere_noon.flecs`, `atmosphere_sunset.flecs`),
draws the sun disk, a phase correct moon and a star field (`celestial.flecs`), attenuates the sun
light by the atmosphere, drives fog from the same tables (`sky_fog.flecs`), and `TimeOfDay` moves the
sun, moon and stars by hour, date and latitude (`time_of_day_morning.flecs` and siblings).
`box.spline` stores a `Spline` (a vector of points, catmull rom, linear or bezier, with tension
and loop) that rebuilds an arc length table on every set, evaluates position, tangent and normal by
parameter or distance, exposes `spline.position/tangent/normal/length` to scripts and moves any
entity along a path with `FollowPath {spline, speed, offset, mode, orient}` (loop, ping pong, once).
`SplineMesh {spline, profile, segments, width, height, closed, smooth}` sweeps a cross section
along a curve and writes the result as a `Mesh3`, so a road is drawn, lit, shadowed, textured and
shared by the mesh cache like any other mesh and the entity stays a static instance; the mesh is
rebuilt when the curve or the sweep changes, never per frame. The profile is a polyline in the
plane of the cross section (x across the curve, y up) that faces a quarter turn counter clockwise
from the direction it runs, which makes a road out of a flat strip and a pipe or a cable out of a
closed one, and the sweep follows the curve of its own entity when it names none
(`test/integration/visual/spline_road.flecs`).

`box.transition` moves the value a view shows while the world keeps the value the author
assigned. A `Transition` is a map from a component, or one of its member entities, to
`TransitionParams {duration, delay, timing_function}` and is inherited by instances, so a prefab
sets the feel of a whole family:

```
using box.material
using box.transition

prefab Button {
  Rgb: {60, 60, 70}
  Transition: [Rgb: {duration: 0.2, timing_function: EaseOut}]
}
```

Assigning a component the map covers starts a transition from the value the entity currently
shows toward the one that was assigned and replaces whatever was in flight, so a value assigned
twice in a frame restarts from what is displayed instead of jumping. `Position`, `Rotation`,
`Scale`, `Rgb`, `Tint`, `Emissive` and `Opacity` are covered out of the box, and adding
`Transitionable` to any reflected component of up to four floats or bytes registers that one as
well; with member entities the map keys a single member, as in
`Transition: [Position.x: {duration: 0.4}]`. The timing functions are `Linear`, `Ease`, `EaseIn`,
`EaseOut` and `EaseInOut`. Only entities that have a transition carry the private record that
drives it, so a scene without transitions pays nothing. A static entity uploads its transition
once, on the frame it starts, and the renderer thread computes the value of every later frame from
that record and reports the transition back when it ends, so the main thread does nothing per
frame while a transition runs and a transitioning entity never becomes dynamic. The renderer
holds the records that are still moving in one array per channel instead of next to their
instance, so a frame interpolates them as seven passes over contiguous memory with no branch per
record, and a scene in its steady state allocates on neither side. Adding
`(TransitionValue, Component)` to an entity asks the engine to keep the interpolated value of that
component there for scripts to read. `test/integration/visual/transitions.flecs` captures a
position, a color, a scale and an emission halfway through their transitions, and
`test/integration/bench/transitions_10k.flecs` runs ten thousand entities with ten transitions
each, staggered over a second by their delay and restarted by the churn of the benchmark app.

`box.collision` keeps incremental bounds for prefab and template roots, derives a cheap collider
per shape (`SphereCollider`, `BoxCollider`, `CapsuleCollider` override, `NoCollider` opts out) and
answers ray, sphere, box and frustum queries from a bounding volume hierarchy in microseconds over
a million colliders.
`CollisionSettings: {debug: DebugAll}` draws the colliders and the root bounds a view sees as
wireframes over the image (`DebugColliders`, `DebugBounds` or `DebugNone`, which costs a frame
nothing), see `test/integration/visual/collision_debug.flecs`.

`box.picking` turns the cursor into a ray through the camera of every enabled view that renders
into the surface and resolves the nearest collider to the entity the author cares about: the
prefab or template root, unless that collider or an entity between it and the root carries
`Pickable`. The entity under the cursor carries `Hovered`, and a pick raises `on.hover`,
`on.press`, `on.release`, `on.click` and `on.drag` as script events that carry the hit entity,
position and normal:

```
template Button {
  async {
    const hit = await on.click(this)
  }
}
```

An event starts at the entity the pick resolved to and bubbles up the parent chain until a
handler consumes it, so a parent only hears what its children leave alone. A frame in which the
cursor stands still costs no ray query at all, and `PickSettings` holds the reach, the drag
threshold and the switch. An entity that carries an `Action` fires it through the input action
layer when a click resolves on it, and the click walks up the parent chain to the nearest one, so
a widget and a key binding share one action. A view whose contents have no collider answers its
own pick before the ray query runs, which is how `box.ui` resolves the cursor against the
rectangles of its trees; the element that hit is written to the `UiCapture` singleton every
frame, so `box_ui_mouse_captured(world)` and a script alike know the UI holds the pointer. `test/integration/command/picking.flecs` hovers and clicks a crate
through the `mouse.*` commands and keeps the log.
2D shapes: `Circle`, `Line`, `Image` (a texture region on a `Rect` with fit and rotation) and
`Border` (rounded corners and per edge widths evaluated in the fragment shader, so a radius change
rebuilds no mesh), see `shapes_2d.flecs`.
`Weather` blends overcast, fog and rain amounts over the atmosphere with smooth transitions
(`weather_overcast.flecs`, `weather_fog.flecs`, `weather_rain.flecs`, `weather_rain_heavy.flecs`).
Rain is the state that reaches the most: it drives the amount of the wetness material layer, it is
added to the overcast and the fog the sky and the air are drawn with, and it creates one particle
emitter that follows the camera, a column of streaked drops that fall at their terminal speed,
lean with the wind and die on the ground. `Rain` is the shape of that column, and every member has
a default, so `Weather: {rain: 1}` already rains:

```
using box.sky

$ { Weather: {rain: 0.6, transition: 3} }
$ { Rain: {area: 20, height: 26, rate: 14000, wind: {10, 0, 10}} }
```

The rate, the size of a drop and the wind follow the blended rain, so a weather written from a
script or a command arrives over its transition instead of between two frames, and a rain that
starts fills its column at once instead of letting the first drops fall in from the top. A world
that never imported `box.particle` keeps the wet surfaces and the grey sky and misses only the
drops.

`box.ui` roots a retained UI tree with a `Ui` component: the entity and everything below it is
drawn by a view with an orthographic camera in logical pixels, composited over the views of the
scene, with the origin in the middle of the view, x to the right and y up. `Ui.height` is the
logical height the view shows and a height of zero follows the points of the surface divided by
`Ui.scale`, so a tree keeps its size when the window is resized or moves to another resolution
scale. A tree reuses `Position`, `Rotation`, `Scale` and the 2D shapes: depth comes from the walk
of the tree instead of from positions, so a node is drawn over its parent and over the subtrees of
its earlier siblings without an authored z and without z fighting, and reordering children with
`ecs_set_child_order` reorders the draw once the tree is marked by setting `Ui` again. Every node
joins the layer of its tree, which every other view hides, so a tree never shows up in the scene
and the scene never shows up in the tree; a world whose only views are UI views still draws its
active camera. `Ui.view` binds a view that the scene authored instead of creating one. See
`test/integration/visual/ui_panels.flecs` and `ui_panels_small.flecs`, the same tree at two
logical sizes.

A layout pass gives every node of a tree a `Bounds`, the rectangle it covers in the logical
pixels of its view, with the centre in `x, y` like a `Position` and a `Rect`; `Bounds` is written
by the engine and only read by a scene. `Layout` arranges the children of a node in a row or a
column in child order, with `padding` around them, `spacing` between them and `h` and `v`
aligning them: along the axis of the layout the alignment moves the whole group, across it each
child on its own. The box of the node is `Layout.width` by `Layout.height`, a side that is zero
takes that side of its `Rect` and a side that is still zero takes the size of its content, so a
row or a column grows to fit its children; a node that has a `Rect` has it resized to the box, so
a panel with a `Border` needs no authored size. `Anchor` pins a node to an edge or the centre of
its parent with an offset that runs toward the inside, and an anchored node is left out of the
row or the column of its parent and does not add to its content. The pass writes the `Position`
of every node it places and runs for a tree that changed and for no other, so a UI that stands
still costs nothing per frame. See `test/integration/visual/ui_layout.flecs`:

```
ui {
  Ui: {height: 540}
  Layout: {direction: Column, width: 900, height: 500, padding: 16, spacing: 12, h: Center}
  Border: {radius: 20, width: 3}
  Rgb: {30, 34, 46}

  header {
    Layout: {direction: Row, width: 868, height: 56, padding: 10, spacing: 10, v: Middle}
    Border: {radius: 12}
    Rgb: {52, 66, 100}

    badge {
      Anchor: {h: Right, v: Middle, x: 6}
      Rect: {90, 30}
    }
  }
}
```

`Text` draws a string in the tree. The same pass measures it, wraps it to the width its `Layout`
gives it and sizes the element around the lines it holds, so a row or a column fits itself around
a label without an authored size; `Text.align` moves the lines inside that box, `Text.line_height`
sets the distance between two baselines and a newline in the value always breaks. `Font {path,
size}` says which font file at which em size the element rasterizes with and is taken from the
element, else from the nearest one above it in the tree, else from
`etc/fonts/B612-Regular.ttf` at 16 px, which the framework installs with its `etc` folder. One
atlas is rasterized per file and size with stb_truetype, shared by every element that asks for it
and doubled in place when a glyph no longer fits, and the glyphs are emitted as ordinary children
of the element, one textured quad each through the same 2D shape path as an `Image`, so text
needs no pass of its own. `text.measure(element, value)` returns the size a string takes,
`text.wrap(element, value, width)` returns it with a newline wherever it no longer fits and
`text.label(parent, name, value)` makes or finds the named text element of a parent, so a widget
is built from script alone. See `test/integration/visual/ui_text.flecs` and
`test/integration/command/ui_text_script.flecs`:

```
panel {
  Ui: {height: 540}
  Position: {0, 0, 0}
  Font: {size: 18}
  Layout: {direction: Column, width: 600, padding: 16, spacing: 12, h: Center}

  button {
    Layout: {direction: Row, padding: 12, h: Center, v: Middle}
    Border: {radius: 18}
    Rgb: {110, 160, 235}

    caption { Rgb: {16, 24, 40} }
  }
}

const label = text.label(panel.button, "caption", "Save changes")
```

A tree answers the pick of its own view, because a UI element has no collider:
`box_ui_hit(world, view, x, y)` resolves a point in the logical pixels of a view against the
`Bounds` of the nodes it draws and the node drawn over every other node that holds the point
wins, which is the deepest node of the topmost subtree. The test runs in the frame of each node,
so a rotated or a scaled element is hit where it is drawn, a node without a rectangle holds
nothing and a child that a transparent subtree clips away is not hit either. The view of a tree
renders over the scene and picks before it, so an element under the cursor consumes the pick and
the ray of the scene never runs, while a cursor beside the tree reaches the scene as before. A
hit raises the script events a collider raises, `on.hover`, `on.press`, `on.release`, `on.click`
and `on.drag`, bubbling up the tree until a handler consumes them, with `Hovered` on the element
under the cursor and the `Action` of an element firing on a click. Every widget, hover styling
included, is written in script; the module holds no widget of its own. See
`test/integration/visual/ui_hover.flecs` and `test/integration/command/ui_events.flecs`:

```
template Button {
  mut hover = 0
  Rect: {200, 64}
  Border: {radius: 12}
  Action: {action: actions.Save}
  async {
    await on.hover(this)
    hover = 1
  }

  if hover == 1 {
    Rgb: {245, 205, 95}
  } else {
    Rgb: {52, 66, 100}
  }
}
```

The in game console is the proof that the framework carries a real widget. Adding the
`Console` singleton installs the binding: an `InputAction` under `actions` that the grave key
fires through the action layer, and the first open loads the script the framework installs at
`etc/ui/console.flecs`, which builds the overlay out of the elements above and marks the two
parts the engine fills, the element with a `ConsoleLog` that holds the scrollback and the
element with a `TextInput` that holds the line. An application that wants another console
ships its own script at that path; nothing about the way the overlay looks is in C.

```
$ {
  Console: {}
}
```

A `TextInput` is an editable line with a caret of its own, a thin quad pinned to the left edge
of its box at the width of the text before the caret. A focused input takes the characters the
frame collected and the edit keys that go with them: backspace and delete around the caret, the
arrows and home and end over the line, and enter, which moves the line to `submitted` and
empties the input. `history` walks the submitted lines with the up and down arrows and keeps the
line that was being typed until the walk comes back to it, and `complete` asks `box.command` for
the command name or the entity path the tab key completes, replacing the word the caret is in
with the longest prefix every candidate shares and following a single candidate with a space.

An open console holds the keyboard: every input map but its own is disabled while it is open
and enabled again when it closes, so the keys that drive a game do not drive it while a line is
being typed, and the same key still closes it. The line it submits is set as `TerminalInput` on
`terminals.console`, an ordinary session with a folder and a log that an agent reaches over
HTTP with every command it holds, and the scrollback is the log of that session: every line it
ran and the `TerminalOutput` that came back, the engine log included through the `logs`
command. Submitting also fires `actions.ConsoleLine`, so a script can `await on.pressed` for it.
A closed console keeps its tree and its session and only turns the view of the overlay off.
`test/integration/visual/console.flecs` shows the overlay over a scene and
`test/integration/command/console.flecs` opens it with `key.press`, types a line with
`key.type` and captures the screen.

`box.checkpoint` writes the state of a running instance back into the scripts that loaded it.
The `checkpoint [name]` command walks every managed script, substitutes the current values of
the components of the entities that script owns into a copy of its source and writes it as
`<script>.flecs` in `<process folder>/checkpoints/<name>`, the folder it prints; the folder
goes when the process folder goes, and the `Checkpoint` singleton points `dir` somewhere else,
keeps `last` and counts the checkpoints an instance wrote. Only the edited statements change,
so comments, layout and the expressions of untouched statements survive byte for byte.
A component whose value the engine derives, such as `WorldTransform` or `RenderMesh`, carries
the `NoCheckpoint` tag and keeps the text the author wrote; an entity that no script statement
declares cannot be written back at all and the command lists it as `not checkpointed: <path>`
instead of dropping it silently, which covers entities created by a command, by C code or by a
`for` loop of a script. `test/integration/command/checkpoint.flecs` checkpoints a scene, moves
a crate, checkpoints again and adds an entity that no script owns.

The `restore [name]` command puts an instance back, the last checkpoint when no name is given.
It reloads every script whose file the folder holds from the text of that file, so the entities
a script owns are deleted and created again: their handles change and anything that holds one
has to look it up by name again, while renderer slots, colliders and collision roots are
rebuilt by the observers that watch the new entities. Entities that no script owns survive a
restore untouched and the command lists them as `not restored: <path>`, and the script that
declares the session running the command is left alone so a restore never deletes the terminal
under it. Every file of the folder is read and parsed before the first script is reloaded, so a
checkpoint that does not parse leaves the world as it was; a file that parses but fails to
evaluate stops the restore at that file, which the output names after `restore failed: `.
`test/integration/command/checkpoint_restore.flecs` checkpoints a scene, moves a crate, deletes
an entity, adds one that no script owns and restores.

`box.particle` adds particles as a component and nothing else: a `ParticleEmitter`
holds the maximum number of particles alive at once, the emission rate, the spawn
shape with its parameters, the ranges a particle starts between and a seed, and the
transform of the emitter entity places and orients the spawn volume like it places a
mesh. No particle ever reaches the ECS. Extract writes one block per emitter into the
frame packet and the renderer keeps the buffers: a spawn and an integrate compute pass
run over them once per frame and a point sprite pass draws them over the image of every
view, so a million particles cost the main thread one block.

```
using box.particle

fountain {
  Position: {0, 0.5, 0}
  ParticleEmitter: {
    max: 20000, rate: 4000, shape: EmitterDisc, radius: 2,
    direction: {0, 1, 0}, spread: 20,
    speed: {min: 3, max: 9}, lifetime: {min: 1, max: 2},
    size: {min: 0.1, max: 0.2}, opacity: {min: 0.6, max: 1},
    acceleration: {0, -2, 0}, color: {235, 180, 120}, seed: 3
  }
}
```

The shape is `EmitterPoint`, `EmitterSphere`, `EmitterBox`, `EmitterDisc`,
`EmitterMesh` or `EmitterSpline`; the last two draw their spawn points from the
`Mesh3` surface or the `Spline` of the `source` entity, which the module samples into
a table of 64 points and resamples when the shape, the source or the seed changes. A
particle leaves along `direction` inside the cone of `spread`; an emitter that names no
direction sends it outward, which is the surface normal of a mesh, the radius of a
sphere or a disc and the normal of a curve. Every random draw is a hash of the seed and
the index of the particle in the stream of the emitter, so a fixed timestep produces the
same particles on every run and on every device, and the rate is accumulated on that
clock, so a rate that is not a multiple of the frame rate still spawns its particles per
second. The pool of the renderer is the maximum: a spawn takes a slot from the dead list
that the integrate pass pushes the expired particles onto, and an emitter that asks for
more particles than it has slots simply does not fill them.

`ParticleForces` adds gravity, wind, drag and turbulence. An emitter adds the component
of the world, the one of the entity its `forces` member names and the one it carries
itself, so a scene can blow one wind over every emitter while an emitter keeps a drag of
its own. Wind reaches a particle through the drag: the drag pulls the velocity of a
particle towards the wind, which is solved in closed form and is stable at any timestep,
so a particle without drag keeps its course whatever the wind is. Turbulence is a field
of three sines over the position, so a particle that passes the same place is always
pushed the same way and a wander is as reproducible as the rest of the simulation.

`ParticleCollision` gives the particles a ground plane and the colliders of the scene.
The extract takes the colliders within `reach` of the emitter from the collision index,
at most sixteen, and writes them into the block behind its spawn points; a sphere is
tested as a sphere, every other shape as the box of its world bounds. A particle that
hits keeps `bounce` of the speed it had into the surface, loses `friction` of the speed
it had along it, or dies on the spot with `die`.

```
using box.particle

$ { ParticleForces: {wind: {6, 0, 0}, drag: 1.5} }

splash {
  Position: {0, 3, 0}
  ParticleEmitter: {
    max: 20000, rate: 4000, direction: {0, -1, 0}, spread: 20,
    speed: {min: 4, max: 8}, color: {110, 195, 245}, color_variance: {20, 10, 0}, seed: 7
  }
  ParticleForces: {}
  ParticleCollision: {ground: true, bounce: 0.4, friction: 0.2}
}
```

Every property a particle starts with is drawn from a range: the direction from the cone
of `spread`, the speed, the lifetime, the size, the rotation, the opacity and the color,
which is drawn between `color` minus and `color` plus `color_variance` per channel.

`ParticleCurves` changes a particle over its life. Every curve has four keys, at the
start, a third in, two thirds in and at the end, and the value between two keys is the
line between them. The size and the opacity curves multiply what the particle was spawned
with, the rotation curve adds turns and the gradient multiplies its color, so an emitter
that names no curve is left alone. The curves are read by the sprite pass from the age and
the lifetime the buffers already hold, so they cost no per particle state.

`ParticleAppearance` says how a particle is drawn: a soft `ParticleCircle`, a
`ParticleSprite` of a texture, which `columns` and `rows` split into a sheet of frames
that `fps` plays at or that plays once over the life, or a `ParticleBillboard` of the mesh
of an entity, kept facing the camera. The texture is a texture entity, an entity with an
`Image` or one with `PbrTextures`, whose albedo is taken. `blend` is `ParticleAlpha` for
smoke and debris or `ParticleAdditive` for fire, sparks and magic; the blended pools are
drawn back to front after the glass pass and the added ones follow them. A `softness`
fades a particle out over that many world units as it approaches the geometry behind it,
which is what keeps a puff of smoke from cutting a line into a wall. `stretch` lays the
sprite over the direction the particle moves in on the screen and grows it along that direction
by that many world units per unit of speed, which draws a rain drop, a spark or a piece of debris
as the line it travels.

```
smoke {
  ParticleEmitter: {max: 20000, rate: 4000, speed: {min: 1, max: 2}, seed: 8}
  ParticleCurves: {
    size: {start: 0.4, early: 1.2, late: 1.9, end: 2.4}
    opacity: {start: 0, early: 1, late: 0.7, end: 0}
  }
  ParticleAppearance: {shape: ParticleSprite, texture: puff, softness: 0.6}
}
```

`ParticlePlayback` starts, pauses, stops and resets an effect, and `ParticleBurst`
spawns a lump of particles at a time of its playback alongside the continuous rate.
Stopping ends the emission and leaves the particles that are in the air to finish,
pausing freezes the whole pool where it is and drawing carries on, and `reset` is a
trigger the frame that sees it acts on and writes back to false. The clock of the
playback is written by the engine and only runs while the emitter plays, so a schedule
keeps its place over a pause and starts again when a stopped emitter is played. A burst
sits on the emitter or on any of its descendants, which is how one emitter takes several
schedules.

```
using box.particle

firework {
  Position: {0, 4, 0}
  ParticleEmitter: {max: 20000, rate: 0, spread: 180, speed: {min: 6, max: 14}, seed: 9}
  ParticlePlayback: {state: ParticlePlaying}

  shells {
    ParticleBurst: {count: 600, at: 0.5, repeat: 3}
  }
}
```

Every control is a component write, so a script, the REST API and a terminal command
drive it without an endpoint of its own: `set fireworks.firework ParticlePlayback
{state: ParticlePaused}` freezes it from a session. The module also registers the same
controls as script functions, `particle.start`, `particle.pause`, `particle.stop`,
`particle.reset` and `particle.time`, which each take the emitter entity.

An effect that plays once at a point of the world is a template of those components: a
prefab holding the emitter, its bursts, its curves, its appearance and an optional
`PointLight` with an `EffectFlash` that fades it. `box_particle_burst_at(world, effect,
position, scale)` and `particle.burst_at(effect, position, scale)` take a free instance
of that template from its pool or make one, place it, scale its sizes, speeds and spawn
volume and play it from the top; the engine ends the emission once the bursts have gone
off and hands the instance back to the pool a lifetime later, when its last particle has
died, so a hundred hits a second cost the entities of one pool instead of one per hit.
`ParticleEffect` on the instance reports the template, the scale and the times it is
stopped and freed at. `test/integration/visual/one_shot_effects.flecs` plays one template
at three scales.

```
using box.particle

effects {
  prefab Spark {
    ParticleEmitter: {max: 600, rate: 0, shape: EmitterSphere, radius: 0.7}
    ParticleBurst: {count: 300, at: 0}
    PointLight: {intensity: 8, range: 7}
    EffectFlash: {seconds: 0.6}
  }
}

const hit = box.particle.burst_at(effects.Spark, {0, 1, 0}, 1.5)
```

`ParticleSettings` is the singleton that holds what every emitter of a world shares.
`max_total` caps what the pools hold together and `spawn_per_frame` caps what a frame
spawns into them; an emitter that asks for more than its share of either gets its share,
divided in proportion to the asks, so a scene over its budget thins every stream instead
of stalling or losing one. The particles of an emitter stay inside a sphere its speed,
its lifetime and its acceleration bound: `cull` drops the emitters no view can see,
`cull_distance` drops the ones too far from every camera and `step_distance` steps the
ones beyond it every other frame with both frames in one step. `ParticleStats` reports
what the last frame cost: the emitters, how many were simulated, culled or halved, the
particles the pools hold and the bytes they take on the device.

```
using box.particle

$ { ParticleSettings: {max_total: 10000000, spawn_per_frame: 200000, step_distance: 150} }
```

A particle costs 52 bytes: sixteen for its place with its age, sixteen for the velocity
with its lifetime, two words for its color and opacity and one for its rotation and size
as half floats, and a word each in the dead list and in the compacted list of what is
alive. The integrate pass is dispatched over the slots a pool has used rather than over
its ceiling, and an added pool is drawn indirectly over that compacted list, so neither
the simulation nor the draw touches a slot that holds nothing. A blended pool is drawn
over its slots in order instead, because the order two sprites blend in has to be the
same from frame to frame.

`test/integration/visual/particles_shapes.flecs` shows a stream from each of the six
shapes, `test/integration/visual/particles_forces.flecs` a jet splashing off a plate, a
plume blown over by wind and a burst of sparks,
`test/integration/visual/particles_appearance.flecs` smoke, fire, sparks, a sprite sheet
and mesh billboards, `test/integration/visual/particles_control.flecs` a burst caught in
flight next to a repeating schedule, a running stream and a stopped one,
`test/integration/bench/particles_1m.flecs` runs a million of them,
`test/integration/bench/particles_10m.flecs` ten million over ten emitters and
`test/integration/bench/particles_fill.flecs` eleven thousand screen filling soft sprites.

## Tests

```sh
bake3 run test/core --local-env -- -j 12                     # all unit tests
bake3 run test/core --local-env -- Args -j 12                # one suite
bake3 run test/core --local-env -- Args.fps                  # one case
bake3 run test/core --local-env -- -j 12 --json report.json  # with JSON report
bake3 run test/transform --local-env -- -j 12                # other projects: geometry, render, view, shader, particle, command, checkpoint, job, input, controller, script, transition, lod
```

Integration scenes live under `test/integration`: `visual/` scenes are rendered
headless to screenshots, `bench/` scenes are run by the benchmark app and
`command/` scenes run a command file on a terminal session:

```sh
python3 tools/integration.py visual            # writes test/integration/out/<scene>.png
python3 tools/integration.py bench             # writes test/integration/out/<scene>.json
python3 tools/integration.py command           # writes test/integration/out/<scene>.log
bake3 run box_bench --local-env --cfg release -- ../../test/integration/bench/static_100k.flecs --churn 1-1000
bake3 run box_bench --local-env --cfg release -- ../../test/integration/bench/spikes.flecs --spike 4 --fps 60 --attribute
```
The bench app reports mean, p50, p99 and max frame times, fps, drawn, culled and occluded counts
and shadow rebuilds. `--spike <factor>` fails the run when a frame exceeds that multiple of the
mean (4 by default, 0 disables), `--attribute` names the slowest system of every spike through
the system monitor, `--fps` fixes the step, and `--light-churn <n>` moves static lights per
frame. A `<scene>.args` file next to a bench scene holds extra options the runner passes along.


A command scene holds a terminal named `terminals.agent0` with a `CommandScript`
that points at the `<scene>.cmd` next to it. The runner sets `Terminal.log` from a
generated harness scene so the session log survives the process folder, strips the
path of the repository from it and compares it with the `<scene>.expected` next to
the scene, printing a diff when they differ. A `<scene>_content.flecs` is loaded
before the scene, which keeps the scripts that commands patch out of the scene
that holds the terminal. `command/render.flecs` runs `render` on an entity, a
prefab and a template; the runner copies the PNGs of a session to
`test/integration/out/<scene>_<image>.png` while the scene runs, because the
process removes its folder when it exits.

Integration harnesses disable incremental template loading (`ScriptSettings: {enabled: false}`)
so captures and command logs are deterministic; `templates_per_frame` with `budget_ms: 0` gives a
deterministic incremental run when that is what a test needs. The visual harness also sets
`JobSettings: {workers: -1, budget_ms: 0}`, which runs every queued job on the main thread before
the captured frame, so a scene with SVG textures renders the same every run.

## Shaders

Every shader of the engine is a WGSL file under `etc/shaders/` of the framework,
loaded by a `Shader` component that holds a path and a comma separated `NAME=value`
macro list. Setting it preprocesses the file and hands the source to the renderer, which
compiles a module and rebuilds the pipelines that use it; a file that does not
preprocess leaves the previous shader drawing and reports the file and line through
`ShaderStatus`. The builtin shaders are entities under `box.shader.builtin`, so
reloading the mesh shader is a component write:

```
box { shader { builtin { mesh {
  Shader: {path: "etc/shaders/mesh.wgsl", macros: "DEBUG_NORMALS=1"}
} } } }
```

A path names the `etc` folder of a project and is resolved against the shader
search path: the working directory, then `<BAKE_HOME>/etc/box`, then the nearest
ancestor of the working directory that has an `etc/shaders`, then `apps/box_app` at
or above it, then the directory of the executable. That makes an application find
its own files first and override an engine file, an installed game work from any
working directory, a checkout work from the repository root and from a test
project, and the wasm build work from the embedded `etc`.

Engine assets live in `etc/` of the framework and application assets in
`apps/box_app/etc/`: every build of the framework installs `etc/` into the bake
environment as `<BAKE_HOME>/etc/box` (`~/bake3` globally, `.bake/local_env/<name>`
for a local environment), which is what `etc/shaders/mesh.wgsl` resolves to in an
installed game. An application ships its own scenes, textures, SVGs and fonts, plus
the shaders that belong to it alone; `BAKE_HOME` and `BAKE_ENVIRONMENT` are exported
by `bake3 run`, `test` and `bench`, and `bake3 info box` prints the install path. The
wasm build embeds both folders under `etc`, so the same paths work in the browser.

The preprocessor understands `#include "file"` relative to the including file with
cycle detection, `#define NAME value` with identifier substitution, `#if NAME`,
`#ifdef`, `#ifndef`, `#else`, `#endif` and `#pragma once`; an undefined macro is 0
in a condition and a `#define` never replaces a macro the component or the engine
set. The constants the renderer and the shaders share, such as `SHADOW_CASCADE_MAX`
and `ENV_MIPS`, are predefined. `#include "generated/layers.wgsl"` is answered by
the material layer registry instead of from a file, which is how the mesh shader is
recomposed when a scene registers or removes a layer.

`test/integration/visual/shader_macro.flecs` is the macro check: the same scene with
`DEBUG_NORMALS=1` on the builtin mesh shader, which outputs world normals as color.

### Light modulators

A `LightModulator` names a `Shader` whose WGSL file defines one function, and the
renderer multiplies the direct light of that light by what the function returns at
every shaded point:

```wgsl
fn box_light_modulate(p: vec3<f32>, n: vec3<f32>, l: vec3<f32>, time: f32,
    params: LightModulatorParams) -> vec3<f32>
```

`#include "generated/light_modulators.wgsl"` is answered by the modulator registry
the same way the layer include is: every registration is spliced in as
`box_light_modulate_<index>` and a light multiplies the ones its mask names. The
multiply happens after the shadow of the light, so a modulator never touches
indirect light, a light without one costs nothing and the variant is only compiled
once a modulator exists. Several modulators on one light multiply, which is what a
child entity with a modulator of its own is for.

The parameters are an ordinary reflected component next to the modulator that the
plugin registered with `box_light_modulator_params`, written into a sixteen float
block in member order, so a plugin is a WGSL file plus a component and needs no
engine change. The framework ships two under `etc/shaders/light/`:

```
using box.light
using box.shader
using box.sky

plugins { clouds { Shader: {path: "etc/shaders/light/clouds.wgsl"} } }

$ { Weather: {overcast: 0.7} }

sun {
  DirectionalLight: {intensity: 3.2}
  LightModulator: {shader: plugins.clouds}
  CloudShadows: {scale: 24, speed: 2, softness: 0.5}
}
```

`CloudShadows` is a scrolling deck of tiled noise that darkens the sun; a density of
zero follows the overcast of the world's `Weather`, and `enabled: false` switches the
deck off without touching the scene. `Caustics {scale, speed, intensity, depth}` in
`etc/shaders/light/caustics.wgsl` brightens a light below the water plane its depth
names and is the example a project copies for a plugin of its own.
`test/integration/visual/cloud_shadows.flecs` and `caustics.flecs` are the checks.

## Post process effects

An effect is an entity with an `Effect` component that names a `Shader`, an order, a
resolution `scale` and the inputs its pass reads, plus a parameter component the
engine copies into the uniform block the shader sees as `fx.params`:

```
using box.effects

grade {
  Effect: {shader: box.effects.shaders.color_grade, order: 100, enabled: true, color: true}
  ColorGrade: {exposure: 0.7, saturation: 1.8}
}
```

A view runs the effects its `effect_list` names, or every enabled effect of the world
in `order` when the list is empty, and `View.effects: false` turns the chain off.
Enabling or disabling an effect, changing its order or changing the list of a view
reconfigures the chain on the next frame, because the chain is rebuilt during extract.

The renderer runs the chain after the transparent pass and before the capture. A view
with a chain draws its mesh, transparent and glass passes into a pooled RGBA16F target
instead of its own 8 bit one, so highlights past white reach the chain; the gamma encode
moves to the last pass, which writes the image of the view. The passes ping pong
between pooled targets of the size their scale asks for; targets are handed back after the chain, so the passes of one view
and the views of a frame share the same textures. The declared inputs decide what the
frame produces: the depth target is bound as it is, the world normals come from a
second color attachment the mesh pass writes through its `fs_normal` stage, and the
motion vectors come from a pass that reprojects the depth with the view projection of
the previous frame into an RG16F target. An input no enabled pass asks for costs
nothing and binds a one texel dummy. `include/effect.wgsl` gives a pass its bindings,
`fx_sample`, `fx_view_depth`, `fx_world_normal` and `fx_motion_vector`.

`test/integration/visual/effect_toggle.flecs` is the effect check: the same scene with
the color grade enabled, which is visibly brighter and more saturated.

The engine registers its own effects as entities under `box.effects.stack`, all
disabled, so a scene turns one on and gives it parameters:

```
box { effects { stack { bloom {
  Effect: {enabled: true}
  Bloom: {intensity: 0.22, radius: 1, knee: 0, firefly_clamp: 8}
} } } }
```

`gtao` (order 600) traces horizon based ambient occlusion over the depth at half
resolution and darkens the indirect diffuse light of a pixel with it, `ssr` (650)
marches the depth for a reflection and replaces the indirect specular light of a
pixel with what it found, falling back to the prefiltered environment on a miss,
`fog` (700)
integrates a height fog along the view ray it rebuilds from the
depth, `bloom` (800) mixes in a pyramid of the image, `auto_exposure` (850) scales
it by the exposure its histogram adapted to and `tonemap` (900) maps the result
onto the display and dithers it. `ToneMapping.curve` picks the display transform
of that last pass: `ToneMapNeutral` is the hue preserving curve every scene is
baselined with and `ToneMapTony` reads the Tony McMapface lookup, a 48 cube of
RGB9E5 texels under `etc/shaders/lut/` that the engine loads and uploads as a 3D
texture the first time a pass selects it, so a scene that stays on the curve pays
neither the file nor the texture:

```
box { effects { stack { tonemap {
  Effect: {enabled: true}
  ToneMapping: {white: 1, dither: true, curve: ToneMapTony}
} } } }
```

The pass folds the exposed stimulus into the cube with `x / (x + 1)` and samples
half a texel inside it, which is the mapping the cube was baked with;
`test/integration/visual/tonemap_tony.flecs` is the same content as
`tonemap.flecs` with the operator switched over. Two of them ask the frame for work through their
own inputs: `pyramid` builds the mip chain of the image the pass reads,
`exposure` runs the histogram and adaptation compute passes over it and
`occlusion` traces, denoises and accumulates the ambient occlusion of the view.
The second attachment of the mesh pass carries the world normal octahedrally,
the indirect diffuse fraction of a pixel and its indirect specular fraction
packed with the roughness, which is how a pass changes the indirect light of a
pixel and leaves the direct light alone. Targets
between two passes are RGBA16F, so what an earlier pass pushes past white reaches
the tone map. `height_fog.flecs`, `bloom.flecs`, `auto_exposure.flecs` and
`tonemap.flecs` under `test/integration/visual` show one effect each, and
`gtao.flecs` shows a room corner with the occlusion on next to `gtao_off.flecs`
and `ssr.flecs` a glossy floor reflecting colored boxes next to `ssr_off.flecs`;
`python3 tools/ssr_check.py` renders that scene and fails when the floor keeps
more than a handful of isolated pixels where a ray missed.

## Benchmarks

`bench/<module>` holds one bake3 bench project per engine module, mirroring the
test projects. Cases always run sequentially in one process, so `-j` has no
effect on them.

```sh
bake3 run bench/transform --local-env -- -j 1 --json report.json  # one project
bake3 run bench/render --local-env --cfg release -- Extract.dynamic_10k
bake3 run bench/geometry --local-env -- --time 2 --samples 50     # longer budget
python3 tools/bench.py --env bench                                # every project
```

`tools/bench.py` builds and runs every project under `bench/` in release and
writes `<out>/<project>.json` (`bench/out` by default). With `--baseline <dir>`
it compares each report against the same file in that directory and prints the
cases that changed by more than `--threshold` (5% by default):

```sh
python3 tools/bench.py --env bench --out bench/out \
    --baseline project/db/scene_core/deliverable
```

A benchcase is a `void <Suite>_<case>(bench_t *b)` function in
`bench/<module>/src/<Suite>.c`, listed in the `bench` section of the project's
`project.json`; bake generates the harness and stubs for cases that are missing.
Build the world before the loop, measure inside it, exclude per iteration setup
that cannot be hoisted with `bench_pause`/`bench_resume`, keep results with
`bench_keep` and declare how many items an iteration processes with
`bench_set_items`:

```c
void Extract_static_100_changed(bench_t *b)
{
    // epic/task
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxGeometry);
    /* build the scene and the packet */
    bench_set_items(b, 100);

    while (bench_iter(b)) {
        bench_pause(b);
        box_packet_reset(&packet);
        /* touch 100 entities */
        bench_resume(b);
        box_render_extract(world, &packet);
        bench_keep(packet.static_updates.count);
    }

    ecs_fini(world);
}
```

A report holds median, mean, standard deviation, a 95% confidence interval and
the raw samples per case. Reports attached under `project/db` show up in the
**Microbenchmarks** table of the Benchmarks tab of the status page.

## WebAssembly

```sh
bake3 build box_app --target em --local-env --cfg release
bake3 run box_app --target em --local-env --cfg release   # serves the page on http://localhost:8080
```

The page accepts engine arguments in the URL hash, e.g. `box_app.html#--frames 600`.
Every completed epic ships a wasm build under `project/db/<epic>/deliverable/wasm/`;
serve that folder with any static file server and open `box_app.html` in a
WebGPU-capable browser.

## Coding style

`design/coding_style.c` is the reference and `tools/lint.py` enforces it. Run
`python3 tools/lint.py` before committing (`--fix` reformats signatures and blank
lines). See `CLAUDE.md` for the full rule list.

## Project status page

```sh
python3 project/pm.py serve            # http://127.0.0.1:8090
python3 project/pm.py status           # print the tree in the terminal
```

Work is tracked as epics, tasks and subtasks under `project/db`. Each item has
a `README.md` with its goal and a `progress.json` with its status and timing;
tasks and epics keep test reports, screenshots and notes in `deliverable/`. An
epic also has a `priority` in its `progress.json`, an integer that is 100 when
missing and where lower runs first.

The tool derives the execution order from that: a task is ready when every
dependency is complete (an epic as a dependency counts as complete when that
epic is), and among the ready tasks the one from the epic with the lowest
priority goes first, then the oldest one. That order sorts unstarted epics and
tasks everywhere the page lists them and is served as `next_up` in `/api/state`.

The **Plan** tab draws one node per task, colored by status and grouped into a
horizontal band per epic in priority order, with the columns left to right by
dependency depth and the dependency edges between tasks, stronger when they
cross epics. Hovering a task lights up what it waits for and what waits for it;
the filter bar shows one epic or only unfinished tasks, and the diagram pans and
zooms. The priority box in an epic's detail panel writes the new value with
`POST /api/priority` and reorders the page.

Besides the board, timeline and plan, the **Tests** and **Benchmarks** tabs open
with a summary panel of totals and sparklines and then one sortable, searchable
project to suite to case tree, where an open row carries its own chart: time per
suite and per case for tests, median per suite and per case with its confidence
interval for bake3 benchmarks, plus a scene section with frame timing. Both read
the aggregated `/api/tests` and `/api/benchmarks` endpoints served by `pm.py
serve`, which summarize every level of that tree.

The **Build** tab reads the `build*.json` reports of `tools/epic.py` from
`/api/builds` in the same layout: tiles per kind (debug, release, wasm), files, link
time and commit, then a report to project to step tree, slowest files first.

The server also serves `apps/box_inspector/index.html` at `/inspector`. An epic
with a `deliverable/wasm/` build gets an **Inspect** link on its card and in its
detail panel that opens the inspector on that image; the header link **Inspect
local instance** opens it on `http://localhost:27750`.

```sh
python3 project/pm.py new task <epic>/<task> "Title" --brief "One sentence." --deps <epic>/<other>
python3 project/pm.py new epic <epic> "Title" --priority 65   # lower runs first, 100 by default
python3 project/pm.py next 5           # the ready unstarted tasks in the derived order
python3 project/pm.py start <epic>/<task>
python3 project/pm.py brief <epic>/<task> --files src/sky/sky.c,include/box/sky.h
python3 project/pm.py deliver <epic>/<task> --tests render:Atmosphere,core --bench render
python3 project/pm.py done <epic>/<task>
python3 project/pm.py check            # READMEs, statuses, dependencies, priorities and dependency cycles
```

`brief` prints the whole agent prompt for a task: its README, the README of its
epic, the deliverable reports of its dependencies and the house rules, with the
private environment named after the task (`--env`), the files the agent owns
(`--files`) and a `--worktree` note when another agent is editing the tree.

`deliver` runs `bake3 run test/<project> -- [<suite>] -j 8 --json` once per
project into the task's `deliverable/`, merges the reports into `tests.json`,
copies the `tools/bench.py` report to `bench.json` and exits non zero when a test
fails. `done` then refuses a task without `tests.json` and `bench.json` (unless
its `progress.json` says `"bench": false`) and an epic without its report in the
four section format, the merged tests, the benchmarks, the integration outputs
and the wasm image, printing every missing item; `--force` overrides with a
warning. `docs/PROCESS.md` describes the loop these commands encode.

`python3 tools/epic.py <epic> --env <name>` produces everything `done <epic>`
wants except the README: it builds `box_app` in debug, release and wasm and keeps
the bake3 `--build-json` reports as `build_debug.json`, `build_native.json` and
`build_wasm.json`, runs every project under `test/` into `tests_<project>.json`
and the merged `tests.json`, runs the visual, bench and command scenes into
`integration/`, runs `tools/bench.py` into `bench/` against the `bench/` folder of
the previous epic (`--baseline <epic>` picks another) and copies the web image
into `wasm/`. It prints a line per step, exits non zero when a build, a test or a
scene fails and ends with what is still missing; `--steps a,b` and `--skip a,b`
rerun part of the pipeline.

## Layout

```
apps/box_app/   the engine executable, its scenes under etc/ and the wasm shell page
bench/          bake3 bench projects, one per engine module
design/         design brief and datamodel sketches (documentation only)
docs/           architecture notes and the coordination process
etc/            engine assets (WGSL shaders), installed as <BAKE_HOME>/etc/box
include/box/    public headers, one per module
project/        project management tool and its database
src/            engine sources, one directory per flecs module (see docs/ARCHITECTURE.md)
test/           unit test projects
tools/          lint, integration, benchmark and texture generation scripts
```

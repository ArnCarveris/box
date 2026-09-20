# Box architecture

Box is a data-driven framework: every feature is a set of flecs components plus
hooks, observers and systems that make the component values true. There are no
public native functions besides bootstrap. Scripts, the REST API and the command
terminal all work by changing component values.

## Layers

```
apps/            executables (box_app), thin: init world, import modules, run
include/box/     one public header per module, reflected types via ECS_STRUCT
src/<module>/    one directory per flecs module, C11, may include flecs.h
src/renderer/    render thread, wgpu only, MUST NOT include flecs.h
src/platform/    window, surface and OS events (GLFW native, HTML5 on wasm)
etc/             engine assets (WGSL shaders, the default font), installed as <BAKE_HOME>/etc/box
apps/box_app/etc app assets (scenes, textures, svg, fonts), both embedded on wasm
test/            bake test projects, one suite per module
```

Two hard rules keep the concerns apart:

- Nothing under `src/renderer` includes `flecs.h`. It only sees frame packets.
- Nothing outside `src/renderer` and `src/platform` includes `webgpu.h`.

## Modules

| Module | Owns |
|---|---|
| `box.types` | `vec2/3/4`, `mat4`, `rgb` and math helpers |
| `box.core` | `App` singleton, command line with the `AppOption` entities of an application, process folder, frame loop, CPU budget |
| `box.surface` | `Surface` component; setting it brings up a window or offscreen target; `resolution_scale` divides the pixels the scene is drawn at and reports them as `scaled_*`, and the renderer magnifies that image onto the window |
| `box.transform` | `Position/Rotation/Scale/WorldTransform`, static vs dynamic propagation, AABB |
| `box.geometry` | primitive shape components, mesh cache, shape registry; the level ladder of a parametric shape, whose mesh the reduction picks per frame from the projected error of the instance |
| `box.material` | `Rgb`, `PbrMaterial`, `Emissive`, `Tint`, textures, material layers; `Wear` and `WearDetail`, the per entity amounts, noise seed and feature scale the layers of an instance read out of two packed instance words |
| `box.camera`, `box.light`, `box.view` | cameras, lights, views and layers; `LightModulator` with the reflected parameter component a plugin registers, and the shipped `CloudShadows` and `Caustics`; `LodSettings` holds the LOD quality, the error threshold in pixels, the more aggressive shadow threshold and the hysteresis band of a view, with the singleton as its default |
| `box.render` | Extract system, frame packets, handoff queue to the renderer thread; the LOD node hierarchy, which it builds from the collision roots and walks once per view at the head of every extract; the `Hidden` switch, which takes an entity and its subtree out of every view, pick and collision query while it stays in the world |
| `box.shader` | `Shader` component, WGSL preprocessor, builtin shaders under `box.shader.builtin`, the generated includes that compose the material layers and the light modulators into the mesh shader |
| `box.effects` | `Effect` entities, the per view post process chain and its parameters, the display transform lookup cube the tone map pass loads at first use |
| `box.input` | device layer (`Input` singleton, event queues, `keys` codes) and action layer (`InputAction`, `InputMap` layers with `priority` and `consume`, `Binding`, key masks including the `keys.Any` families, emitted actions, the `Raise` record with `event.stop` and `input.consume`, `on.pressed` and `on.dragged` script events) |
| `box.controller` | `CameraController`, `FirstPersonController`, `FollowCamera`, their default actions and the systems that move them |
| `box.command` | terminal sessions, HTTP endpoints, script functions for commands |
| `box.checkpoint` | script state capture and restore |
| `box.monitor` | `CpuMonitor`, `EcsStats`, `SystemMonitor` and `GpuTimings` singletons, sampled only while present |
| `box.script` | scene loading, incremental template instantiation |
| `box.collision` | root bounds, per shape colliders, a bounding volume hierarchy with ray, sphere, box and frustum queries |
| `box.picking` | cursor rays through the views, hit resolution to the root or a `Pickable`, bubbling `on.click` script events, the `Action` a click fires and the source a view without colliders answers its pick with |
| `box.job` | worker pool, prioritised jobs, completion on the main thread in a budget |
| `box.spline` | `Spline` with its cached arc length table, `FollowPath`, `SplineMesh` sweeps |
| `box.ui` | `Ui` roots a UI tree, its layer and the orthographic view that draws it in logical pixels; `Layout`, `Anchor` and the read only `Bounds` of the layout pass; `Font` and `Text` with the glyph atlases, the quads they emit and the `text.*` script helpers; `BoxShadow` as a soft quad behind an element and the view, target and composite quad of a transparent subtree; the hit test that resolves the cursor against the `Bounds` of a view, answers its pick before the scene and writes the `UiCapture` singleton `box_ui_mouse_captured` reads; the `TextInput` line with its caret, history and completion, and the `Console` singleton that binds the grave key, loads `etc/ui/console.flecs` on demand, holds the keyboard while it is open and runs its lines on `terminals.console` |
| `box.transition` | `Transition` maps a component or member to `TransitionParams`; the private per entity record of what is displayed, the timing functions and the `(TransitionValue, Component)` pair |
| `box.lod` | the proxy generator `box_lod_proxy_build`: a reusable builder clusters the primitives of a subtree by material key and proximity and emits a few `Box`, `Sphere` or `Capsule` primitives with the volume the proxy misses as an error in world units; the screen space metric `box_lod_error_px`, which projects such an error at the nearest point of the bounds of a node for a perspective or an orthographic camera, and `box_lod_select`, which holds a node at its level until the projected error leaves the band around the threshold of the view; `box_lod_merge` collapses the meshes of a subtree into one world space mesh with an index range per material key, welding the vertices that share a position, and `box_lod_simplify` collapses that mesh per group with meshoptimizer under an error bound, reporting the error it cost in world units; `box_lod_ladder` turns the authored segment count of a parametric shape into the levels it reduces through, `box_lod_chord_error` is the error one level costs at a radius and `box_lod_reduce` projects those errors and picks the level of the frame; the node hierarchy `box_lod_tree_t`, which the renderer builds from the authored tree and walks once per view: a node the frustum rejects or the depth grid hides stops its descendants, a node whose projected error is under the threshold of the view is drawn as one proxy box and its subtree is released and hidden, and `box_lod_tree_cluster` stacks renderer only clusters over loose roots without touching `Parent` or `ChildOf`; `LodPin` marks a subtree pruning keeps and `LodStats` reports what a walk visited, drew and replaced |
| `box.particle` | `ParticleEmitter` with its shape, ranges and seed, `ParticleForces`, `ParticleCollision`, `ParticleCurves`, `ParticleAppearance`, `ParticlePlayback`, `ParticleBurst`, `ParticleEffect` and `EffectFlash` for the one shot effects of a template pool, `ParticleSettings` and `ParticleStats`; the spawn clock, the playback clock, the shape tables, the colliders and the budget and culling decisions the packet carries, while the particles themselves live in renderer buffers |
| later | `box.atmosphere` |

Each module has `include/box/<module>.h` with its components and a single
`Box<Module>Import` function, and `src/<module>/` with one file per concern
(`<module>.c` registers the module, other files hold systems and hooks).

## Frame

```
OnLoad      platform poll -> Input singleton; injected events
PreUpdate   script async tasks, incremental scene loading
OnUpdate    game systems, controllers
PostUpdate  dynamic transforms (static ones already updated by observers)
PreStore    Extract: build the frame packet from render-relevant components
OnStore     push packet to the renderer queue; block when 2 packets are in flight
```

The renderer thread pops packets, applies deltas to its own persistent state,
records and submits GPU work, presents, and returns the packet to the pool. On
wasm the renderer runs on the main thread and drains the queue after OnStore.

## Frame packets

A packet is not a copy of the scene. It carries:

- per-frame data: cameras, views, lights, time
- full arrays for dynamic instances (entities with `DynamicTransform` or
  `DynamicMaterial`)
- deltas for static instances: index and new data for every static entity whose
  components changed since the last packet, collected by observers
- resource commands: create/destroy mesh, texture, material, in creation order

Packets come from a pool of three and reuse their vectors, so steady state does
not allocate. Render handles (`uint32_t`) identify meshes, materials and
instances on both sides; the game side allocates them from free lists.

## GPU timings

`src/renderer/timing.c` names every pass the renderer records and, while the
world carries the `GpuTimings` singleton, writes a timestamp query around it.
A frame resolves its query set into a buffer at the end of its command encoder
and maps that buffer, which the renderer reads back one or two frames later; no
other frame is recorded until those timestamps arrive, so one query set and one
buffer serve the renderer. `box_timing_render` and `box_timing_compute` are how
a pass is recorded, so a pass that is not timed begins exactly as it did
before. `box_renderer_gpu_timings` hands the resolved passes to the monitor
module, which publishes them per name on the component; a device without the
`timestamp-query` feature creates nothing and reports no passes.

## Transitions

A `Transition` names the components whose displayed value moves toward the value
the author assigns instead of jumping to it. The author facing component always
holds the assigned value; the module records the value that is displayed, the
target, the start time and the parameters in a private `TransitionState` that
only entities with a transition carry. Extract writes that record onto the frame
packet once, on the frame the transition starts: a `box_transition_cmd_t` names
the mesh and static slot of the instance, the values the author assigned, the
world transform of the parent and the scale and bounds of the mesh, followed by
one `box_transition_rec_t` per channel of the instance the transition moves
(position, rotation, scale, rgb, tint, opacity and emission). The renderer keeps
the record next to the slot, computes the transform, the bounds, the color and
the emission of every later frame from it in `box_transition_fill` and writes the
slot again, so the main thread does nothing per frame while a transition runs and
a transitioning static entity never becomes dynamic. The instances the renderer
moved are appended to the static updates of the packet, which is how the shadow
cascade cache and the probe descriptors of the bake see them. When the last
record of an instance ends the renderer keeps the final value, retires the record
and reports the slot back in `transitions_done` of the packet it returns, which
the next extract turns back into the entity and clears its bookkeeping without
polling. The interpolation itself is `box_transition_lerp`, an inline of the
renderer header that reads nothing but its arguments, so the game side, which
still resolves the displayed value of dynamic and sorted instances and of the
`(TransitionValue, Component)` pair, runs the same code.

## Hierarchical LOD

The LOD hierarchy is renderer state, not the entity tree. Every `RootBounds`
root of `box.collision` is a node, `box_lod_tree_cluster` stacks renderer only
cluster nodes over those roots until a handful are left, and the subtree of a
root is materialised from the authored children only while a walk descends into
it. A root that appears is walked once: the same pass hides its renderables,
sums the volume, color and material its proxy is drawn with and measures the
shell its bounding box gets wrong, which is the error the screen space metric of
`box.lod` projects.

`box_render_lod_update` runs at the head of the extract, before the dirty list is
drained. It walks the tree once per view: a node outside the frustum stops there,
a node the coarse depth grid of the renderer hides resolves to its proxy, a node
whose projected error is under the threshold of the view resolves to its proxy
and releases its children, and every other node descends. The decisions of the
views are votes on the node, and descending wins, so a leaf one view draws is
drawn. Between the proxy and the descent sits the contribution test: a node whose
widest face, scaled by the color contrast against its parent, projects under the
same threshold is dropped with its whole subtree, unless it or an ancestor
carries `LodPin`. A node that has a proxy keeps it, so pruning only ever drops
what would otherwise be drawn in full. A node that resolves to a proxy hides the renderables of its subtree once,
which releases their static slots, and leaves one box instance in the dynamic
batches of the frame; a leaf the walk reaches is shown again, which is a dirty
mark and an ordinary static extraction. Nothing else changes: the entities, their
components, their parents, their colliders and every pick keep working on the
authored tree, and `LodSettings` without `enabled` leaves the whole pass out.

The depth grid is the cheap half of the Hi-Z integration. The renderer copies one
small level of the pyramid of the first view into a readback buffer, maps it and
hands it back on the packet with the matrix it was drawn with; the extract reduces
it to a chain and tests the bounds of a node against it. Nothing is ever dropped
on it: a hidden node still leaves its proxy, which the culling of the renderer
tests again, so a stale grid costs detail for a frame and never an object.

## Particles

Particles are renderer state. `box.particle` keeps nothing per particle: the
extract writes one `box_particle_emitter_t` per emitter into the packet with the
world transform of the emitter, its shape and parameters, the ranges a particle
starts between, the seed and how many particles the frame spawns, and the mesh
and spline shapes add a table of 64 sampled points that only changes when the
shape, the source or the seed does. The spawn count comes from an accumulator on
the fixed clock, so a frame of a fixed timestep always asks for the same number.

The renderer owns a pool per emitter, created the first time the emitter appears
in a packet and released when it stops appearing. A pool is a structure of
arrays. The place of a particle and the velocity that integrates it keep their
full precision, sixteen bytes each with the age and the lifetime in their w, and
what only reaches the image is packed into half floats: the color with its
opacity into two words and the rotation with the size into one. A word of the
slot buffer holds the dead list of the free slots and a second holds the
compacted list of what is alive this frame, which makes a particle 52 bytes, so
ten million of them are half a gigabyte of device memory.

Four compute passes run over a pool once per frame, before the views are
recorded. The integrate pass ages every live particle, moves it by its velocity
and acceleration and pushes the slots that expire onto the dead list; it is
dispatched over the slots the pool has ever used, which the packet counts, so a
pool that is filling costs the part of a pass it is using. A pass of one thread
then takes the slots of this frame off the top of the dead list in one step and
the spawn pass fills them, so the particle a frame spawns first always lands in
the slot the list has on top instead of in whichever slot its thread won a race
for: a pool that has not recycled a slot yet draws the same image on every run.
A last pass of one thread writes the length of the compacted list into the
arguments the sprite pass of an added pool is drawn from. Every random value is a
hash of the seed and the index of the particle in the stream of the emitter, not
of its slot, so the particles of a frame are the same on every run and on every
device.

`ParticleSettings` is what the emitters of a world share. `max_total` caps what
the pools hold together and `spawn_per_frame` what a frame spawns into them, and
an emitter over its share gets the share: the ceiling is divided in proportion to
the asks, which do not change while a scene stands still, so a pool is never
rebuilt because a frame was busy. The extract bounds the particles of an emitter
with a sphere its speed, its lifetime, its acceleration and its spawn volume
give, tests that sphere against the frustum and the eye of every view of the
packet and either steps the emitter, steps it every other frame with both frames
in one step or freezes it where it is with the pause bit the playback uses.
`ParticleStats` reports what the frame cost.

The forces of an emitter are the sum of the `ParticleForces` of the world, of
the entity the emitter names and of the one it carries itself, and they live in
the block, not in the particles, so a wind that changes bends the particles that
are already in the air. The integrate pass is a semi implicit Euler step with the
drag solved in closed form: the acceleration and the turbulence are added to the
velocity, the drag pulls that velocity towards the wind over `exp(-drag * delta)`,
which is stable at any timestep, and the position follows the new velocity.
Turbulence is a field of three sines over the position rather than a per particle
wander, because the slot a particle lands in is not reproducible while its place
is.

The ground plane of an emitter and the colliders the extract takes from the
collision index within its reach ride in the same table as its spawn points,
behind them, because the simulation pass already binds the eight storage buffers
a device guarantees. A particle that ends its step behind a surface is lifted
back onto it and keeps the bounce of the speed it had into it and loses the
friction of the speed it had along it, or is killed and handed back to the dead
list.

`ParticlePlayback` is the only state the effect itself has. Its clock runs in the
bookkeeping of the emitter and is published into the component every frame; it
only advances while the emitter plays, a stop leaves the particles in the air to
finish their life while nothing more is spawned, a pause raises a bit in the
block that makes the renderer record neither compute pass so the pool is drawn
exactly as it was, and a reset raises the bit that empties the pool and puts the
clock back to zero. The trigger is written back to false by the frame that acts
on it, so every control is an ordinary component write that a script, the REST
API or a terminal command makes; the module registers the same four as script
functions under its own name.

A `ParticleBurst` is evaluated from that clock instead of from a countdown: the
number of times a schedule went off by a time is the time over its period capped
by its repeat count, and the bursts of a frame are the difference between that
count at the two ends of the frame, so nothing is carried between frames and a
burst lands on the same frame on every run. Its query reaches the emitter over
`ChildOf`, so a schedule sits on the emitter or on any of its descendants and one
emitter takes as many as the scene hangs under it. What the bursts ask for is
added to what the rate asked for and the two are capped by the pool together.

Each view then draws every pool with one instanced sprite pass after its sorted
draws: a particle becomes a camera facing quad of four vertices as a triangle
strip, turned by its rotation and sized by its size, or the mesh of its emitter
laid over the camera axes. Both draws are indirect. An added pool takes its
instances from the compacted list the simulation built, so no slot that holds
nothing is expanded; a blended pool is drawn over its slots in slot order with as
many instances as the pool has used, because the order two sprites blend in has
to be the same from frame to frame and the order a compaction produces is not,
and a slot whose lifetime is zero is pushed outside the depth range. The pass binds the block of
the emitter next to the channels, so the curves over the normalised age of a
particle are evaluated there instead of being written into the buffers: the age
and the lifetime are already in the channels, so a size, opacity, rotation or
color that changes over the life costs no state and leaves the spawn values
intact. The pass reads the depth of the scene without writing it, and reads it a
second time as a sampled texture when an emitter asks for a soft fade, which is
why it takes the depth as a read only attachment. The pools are drawn with the
blended or the additive pipeline of the target format, blended first and back to
front because blending is the only one of the two whose result depends on the
order.

An emitter with a `stretch` is drawn as a streak instead: the quad is laid over
the direction the velocity of the particle points in on the screen and grown
along it with the speed of the particle, so a rain drop or a spark is the line it
travels rather than a dot. The stretch is a field of the emitter block, so it
costs nothing when it is zero and the quad of every other pool is placed by the
expression it was placed by before. `box.sky` is what drives it for rain: the
rain amount of a `Weather` creates one emitter under the sky module, a box volume
that follows the camera of the first view, and drives its rate, drop size, wind
and playback from the blended amount every frame, while the same amount is added
to the overcast and the fog the sky is extracted with and to the amount of the
wetness material layer.

## Static and dynamic content

Entities without `DynamicTransform` are static: their world transform is computed
by an observer when a local component changes and uploaded once as a delta.
Entities with `DynamicTransform` are re-transformed and re-uploaded every frame
in a separate instance buffer. Materials follow the same split with
`DynamicMaterial`. Shadows and culling structures are built on the static buffer
and only patched for changed entries.

## Command interface

Agents drive a running instance through terminal sessions. A session is an entity
with a `Terminal` component that owns a folder `/tmp/box/<pid>/<session>` with a
`resources` subfolder and a `commands.log` holding every command and its result.
Setting `TerminalInput` runs a command line: the dispatcher splits it, looks the
name up as a script function under `box.command.commands`, converts the arguments
and writes the text result to `TerminalOutput`. Commands are grouped per file:
shell, search, logs, script, capture, render and input.

The HTTP layer is glue over those components. `/terminal/new`, `/terminal/close`,
`/terminal/exec` and `/resource/<path>` are served by an `HttpServer` singleton
whose port comes from `--port`; requests are dequeued in OnLoad so handlers run on
the main thread.

`CommandScript` runs a file of command lines on a session when it is set, one
command per line, skipping blank lines and `#` comments; a quoted argument that a
line leaves open continues on the next line. `Terminal.log` copies the session log
to a file outside the process folder, which is how the command integration tests
collect it.

The in game console is the same interface with a UI in front of it. `box.ui` gives
the `Console` singleton a session named `terminals.console`, sets the submitted
line as its `TerminalInput` and shows that session's log as the scrollback, so the
overlay and an agent over HTTP talk to one terminal. `box_command_complete`
answers the tab key of a `TextInput`: the first word of a line completes against
the command names, every later word against the children of the scope its path
names. `box.ui` imports `box.command` the first time a console opens, so a world
that never opens one never pays for it.

## Checkpoints

`box.checkpoint` turns the state of an instance back into script source. The
`checkpoint` command walks every managed script, records the value of every
component of every entity the script owns in an edit set of the script addon and
writes the source that the edit set produces to
`<process folder>/checkpoints/<name>/<script>.flecs`. Only edited statements
change, so the rest of the file survives byte for byte. The `Checkpoint`
singleton points the folder elsewhere and keeps the last one and the count.

What a checkpoint leaves out is as important as what it writes. A component the
engine derives, such as `WorldTransform` or the handles in `RenderMesh`, carries
the `NoCheckpoint` tag, which keeps the statement the author wrote; the module
tags the components of the box modules when a checkpoint runs. An entity that no
statement of a script declares cannot be written back at all: entities created
by C code, by a command or inside a `for` loop have no source location, so they
are reported as `not checkpointed: <path>` instead of being dropped. Entities of
a template body are shared by every instance and are counted, not written.

The `restore` command reads that folder back. It looks up the file of every
managed script, reads and parses them all and only then hands each one to
`ecs_script_reload`, because a script that parses but fails to evaluate empties
itself: parsing first means a corrupt checkpoint leaves the world as it was, and
a file that fails later stops the restore and is named in the output. A reload
deletes the entities of a script and creates them again, so entity handles do
not survive a restore and derived state is rebuilt by the observers that watch
the new entities; anything that caches a handle across a restore, such as a
camera target, has to resolve it by name again. Entities that no script owns are
not part of any reload and are reported as `not restored: <path>`, and the
script that declares the session running the command is kept, which is what
stops a restore from deleting the terminal that asked for it. The `Checkpoint`
singleton is instance state rather than scene state, so the folder and the count
are put back after the reloads, which a `$` statement of a restored script would
otherwise reset.

## Headless and agents

Every process creates `/tmp/box/<pid>` and removes it on exit. `--headless`
requires `--frames` or `--timeout`, caps the whole process at `--cpu-budget`
cores (one by default) by sleeping when it exceeds its share, starts at most
`ceil(budget)` job workers, prints the budget and the measured share at exit,
and refuses to start when the CPU is oversubscribed. Headless rendering uses an
offscreen target so `capture` and `render` commands work without a window.

## Coding rules

See `design/coding_style.c`. In short: file header with purpose, one comment per
function and type, no other comments, asserts over defensive ifs, files under
2000 lines, functions under 200 lines, single exit point per function.

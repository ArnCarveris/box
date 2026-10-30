# Transitions

`box.transition` animates the rendered value of a component when the application changes it,
like CSS transitions. The component itself holds the new value at once: transitions exist only
in the rendered pixels.

The feature is complete: transitions of `Position`, `Rotation` and `Scale` render for the
entity and its descendants (see [Transform transitions](#transform-transitions)), transitions of
`Rgb`, `AlphaCutoff`, `PbrMaterial`, `Emissive`, `TextureTransform` and `Transmission` for mesh
shapes, 2D shapes and texts (see [Material transitions](#material-transitions)), and transitions of `Tint`
and `Opacity` composed down the hierarchy (see
[Tint and opacity transitions](#tint-and-opacity-transitions)). It works on the render thread,
in main thread mode (the browser) and with the null backend. What it does not do is listed in
[Limitations](#limitations), and what it costs in [The fast path](#the-fast-path).

## What the application sees

```flecs
using box.transform
using box.material
using box.transition

e {
  Position: {0, 1, 0}
  Rgb: {255, 0, 0}
  Transition: [
    Position: {duration: 0.5, timing_function: EaseInOut},
    Rgb: {duration: 1.0, delay: 0.25, timing_function: Linear}
  ]
}
```

`Transition` is a map from a component to a `TransitionSpec`:

| Field             | Type             | Default  | Meaning                                       |
|-------------------|------------------|----------|-----------------------------------------------|
| `duration`        | `float`          | 0        | Seconds from the start to the target value    |
| `delay`           | `float`          | 0        | Seconds the rendered value holds still first  |
| `timing_function` | `TimingFunction` | `Linear` | `Linear`, `EaseIn`, `EaseOut` or `EaseInOut`  |

`EaseIn`, `EaseOut` and `EaseInOut` are the CSS cubic Beziers (0.42, 0, 1, 1), (0, 0, 0.58, 1)
and (0.42, 0, 0.58, 1). `box_transition_ease(fn, t)` evaluates them; it is pure and allocates
nothing, so the render thread can call it.

### Authoring from script

`Transition` is written like any component, as a map of component names to specs, after
`using box.transition`. A prefab can declare it for all its instances:

```flecs
using box.transform
using box.material
using box.transition

prefab Crate {
  Box: {1, 1, 1}
  Rgb: {200, 150, 80}
  Transition: [Rgb: {duration: 0.3}, Position: {duration: 0.2, timing_function: EaseOut}]
}

crate : Crate {
  Position: {0, 0.5, 0}
}
```

Then any later write animates, from script, the shell or C, however it is written:

```flecs
crate { Position: {3, 0.5, 0} }   // moves over 0.2 s
Crate { Rgb: {40, 80, 230} }       // every instance that inherits Rgb fades to blue over 0.3 s
```

From the shell, `set crate box.transform.Position {x: 3, y: 0.5, z: 0}` does the same, and the
next `get crate box.transform.Position` returns `{x: 3, y: 0.5, z: 0}`, the target.

### Authoring from C

```c
#include <box.h>

ecs_entity_t e = ecs_entity(world, { .name = "e" });
ecs_set(world, e, Position, {0, 1, 0});
ecs_set(world, e, Rgb, {255, 0, 0});
box_transition_set(world, e, ecs_id(Position), &(TransitionSpec){
    .duration = 0.5f, .timing_function = EaseInOut });
box_transition_set(world, e, ecs_id(Rgb), &(TransitionSpec){
    .duration = 1.0f, .delay = 0.25f });

// Later: the component holds the target at once, the pixels follow
ecs_set(world, e, Position, {4, 1, 0});
```

`box_transition_set(world, e, ecs_id(Position), &spec)` adds or replaces one entry (an instance
gets its own copy of an inherited map first), `box_transition_remove(world, e, ecs_id(Position))`
removes one (the same way, leaving the prefab unchanged), and `box_transition_get()` reads one,
owned or inherited. `box_transition_remove` frees the entry and emits OnSet. Removing the last
entry leaves `Transition` with an empty map; use `ecs_remove(world, e, Transition)` to remove the
component. Removing an entry the entity does not have does nothing: it does not add `Transition`
or copy an inherited map. Both work deferred, on a stage (`it->stage` in a system, or
`ecs_get_stage(world, 0)` until `ecs_merge`); several calls on one entity before the merge edit
the same map. Deferred, `box_transition_remove` looks the entry up in the merged state, so it does
not see an entry that `box_transition_set` added on the stage before the same merge. The storage
is a flecs map (`ecs_map_t`) keyed by the component entity whose values are allocated
`TransitionSpec`s, so `ecs_map_get_deref(map, TransitionSpec, ecs_id(Position))` also reads one.

### Rules

- The application sets a component as always (`ecs_set`, script, a query writing a field). The
  component holds that value immediately and keeps holding it. `ecs_get`, the shell `get`, REST,
  queries, picking, `box.spatial`, collision, `WorldTransform` and every other game thread reader
  see the target only.
- There is no API, component, stat or query that returns the in-progress value. It exists only
  on the render thread. Only rendered pixels show it.
- Transitionable: `Position`, `Rotation`, `Scale`, `Rgb`, `Opacity`, `AlphaCutoff`, `Tint`,
  `PbrMaterial`, `Emissive`, `TextureTransform` and `Transmission` (`box_transition_supported()`). Others are
  rejected: `PbrTextures` holds handles that are part of the batch key, `WorldTransform` and
  `MaterialId` are computed by the engine, and enums and tags have nothing to interpolate.
- A key that is not transitionable, or an entry with a negative `duration` or `delay`, logs an
  error naming the component when `Transition` is set, and the entry is ignored
  (`box_transition_valid()`). The rest of the map works; nothing asserts.

## Semantics

- The clock is `packet->time`, the world time. Pausing or scaling world time pauses or scales
  transitions, and the game and render threads agree on when a transition ends.
- The first value of an entity does not animate. Spawning an entity, or adding `Transition` to
  an existing one, records the current value as the start without a transition. Only later
  changes animate.
- A value change starts a transition from the value currently rendered to the new target, with
  the full `delay` and `duration`. Interrupting a transition therefore never jumps. During
  `delay` the rendered value holds still.
- A change is any change of the bytes of the component, however it was written. A value that
  changes every frame (for example through `Velocity`) restarts the transition every frame,
  which renders as smoothing. Writing an equal value does nothing.
- Removing `Transition`, or removing the transitioned component, snaps to the target. Deleting
  the entity drops its transition.
- Disabling an entity (`ecs_enable(world, e, false)`, which adds `EcsDisabled`) drops its
  transition state like CSS `display: none`: a running transition is cancelled and the entity
  renders nothing while disabled. Enabling it again is a first value that does not animate, so
  it shows its current target, including a value written while it was disabled. Later changes
  animate again from that target with the full `delay` and `duration`.
- An entry removed from the `Transition` map (with `box_transition_remove`, or by a new map
  without it: a script reload, or an instance that sets its own map over the prefab's) snaps
  that component to its target, like removing `Transition`. So does an entry that becomes
  invalid. An entry whose spec changes keeps a running transition until the next value change,
  which uses the new spec.
- `duration: 0` with `delay: 0` is no transition. `duration: 0` with a delay switches after the
  delay.
- `Rotation` interpolates per Euler axis without wrapping. `Rgb`, `Tint` and emissive color
  interpolate per channel in the authored (display) values, and the existing encoding runs on
  the result. All other fields are linear per field after the timing function.
- `Transition` is inherited over `IsA` (`(OnInstantiate, Inherit)`), so a prefab can declare it
  for its instances. An instance that sets its own `Transition` replaces the whole map. A
  transition of an inherited material value belongs to the entity that owns the value, so one
  prefab change animates its one material table slot.
- Draw order from `Position` z, batch membership by target value, picking and bakes use
  targets.

## Limitations

In short, what a transition does not animate, each explained below or in its section:

- An entity with `ManualTransform` (`SurfaceAttachment`) under a transitioning ancestor, and its
  descendants, render at their targets ([manual transforms](#limitation-manual-transforms)).
- Lights and cameras stay at their target place, and a light at its target color
  ([lights and cameras](#limitation-lights-and-cameras), [light color](#limitation-light-color)).
- Light and reflection bakes and probe captures that run during a transition see the rendered
  value of their frame ([bakes and captures](#limitation-bakes-and-captures)).
- Draw order of blended shapes, picking, `box.spatial`, collision and bakes use targets.
- `Rotation` takes the short or the long way as its Euler angles say: no wrapping.
- A glyph in a colored `TextSpans` span keeps the color of the span through an `Rgb`
  transition, and `SurfaceMaterial`, `Border`, `AmbientLight` and other colors outside
  `src/material` do not transition ([material transitions](#material-transitions)).
- A value written every frame restarts its transition every frame: it renders smoothed, never
  reaching a moving target before it stops.

### Limitation: manual transforms

An entity with `ManualTransform` (`SurfaceAttachment`) is not corrected by a transition of an
ancestor and renders at its target: its world matrix is not derived from its parent's, so the
render thread has no chain to recompute it through. Its descendants render at their targets
too, and so does an entity with `ManualTransform` that has a transform transition of its own.

### Limitation: lights and cameras

Lights and cameras are not drawn from instance streams: the light extract
(`src/render/lights.c`) and the views (`src/render/render.c`) read `WorldTransform`, which is
the target. A transition of `Position` or `Rotation` on an entity that is only a light or a
camera does not show, and neither does a light or camera below a transitioning entity: they
stay at their target. A shape on the same entity or below it moves; the light does not.

### Limitation: light color

Lights take their color from `Rgb` (`src/render/lights.c`), which does not go through the
material table or a shading record. A light renders with its target color at once; a shape on
the same entity shows the transition, the light does not.

### Limitation: bakes and captures

Light and reflection bakes, and probe captures, use the material table and shading as the
frame they run in has them. A change of a material component invalidates bakes on the game
thread as without a transition, so a capture that runs while the transition does sees the
value it renders at then, not the target.

## Architecture

The render thread has no access to the world, and a packet holds only encoded GPU bytes
(`box_render_packet_t`, `src/render/render_packet.h`): the world matrix of an instance is
computed on the game thread, in `transformSingle`, `src/transform/transform.c`. So the game
thread tells the render thread what changed, and the render thread computes the values and
patches them into the packet before the backend sees it.

### Game thread, in `RenderExtract`

- One cached query over entities that have `Transition`, matching prefabs too, compares each
  transitioned component to the last target it sent, kept in private per entity state.
  Comparing catches writes that emit no OnSet (`Velocity`, projection). Nothing is added to the
  observers, queries or extraction of entities without `Transition`.
- It allocates a node id per entity with `Transition` and adds records to the packet: node
  created or destroyed, and a transition started (node, component, target value, `delay`,
  `duration`, timing function, start time). Never a `from` value.
- It knows when a transition ends, from the start time and the packet clock. While a node is
  active, it sends one record per affected instance or material slot with the byte offset of the
  record in the packet data, so the resolver can patch it. After the last active frame the
  normal stream holds the target, so the render thread needs no end logic.

Implemented in `src/render/transitions.c`, a system of `RenderExtract`:

- The query is `Transition(self|up IsA)` with `EcsQueryMatchPrefab`, so a prefab and each of its
  instances get their own node and state. Only owned values are compared: an inherited
  material value transitions on the prefab that owns it.
- Values are compared and sent field by field in the layout of the component, with the padding
  zeroed, so an equal write never starts a transition.
- Node ids start at 1 and are reused, after the frame that destroyed them, without carrying any
  state over: the create record resets the node on the render thread.
- Records (`box_render_transition_record_t`, in `box_render_packet_t.transitions`):
  `CREATE` and `DESTROY` of a node, and `START` of a component with its target, `delay`,
  `duration`, timing function and start time (`packet->time`). A first value, and a snap
  (an entry or a component removed), is a `START` with a `delay` and `duration` of 0, which sets
  the value without a transition. After a snap the component is not tracked until it has a
  valid entry and a value again; that value is then a first value and does not animate.
  Removing `Transition` and deleting the entity both send
  `DESTROY`: the normal stream already holds the target.
- `box_render_transitions_active()` says whether a node and component is active at the packet
  time. It and the resolver both decide with `box_render_transition_running()`, from the same
  start time, `delay` and `duration`, so they agree on the last active packet.

### Render thread, `src/render/transition.c`

Called from `box_render_consume` in `src/render/ring.c` before `backend.render`, so it works on
the render thread, in main thread mode (`RenderSettings.main_thread`, the browser), and with the
null backend.

- It keeps a table indexed by node id with the from, current and target value and the curve per
  component. It applies the packet's records, evaluates each active node, and patches the packet
  bytes.
- The table is not backend state, so it survives device loss. The records of a packet that
  the ring drops because it was built for an older backend generation still apply, as they are
  not sent again.
- Values are stored as raw bytes in the layout of their component, with a table of fields per
  component (offset and kind, `float` or `uint8_t`), so a new transitionable component is a
  table entry. `uint8_t` fields interpolate in float and round.
- A start transitions from the value of the running curve at the start time of the new record,
  so an interrupt never jumps, whatever the number of packets in flight.
- It allocates only when a create record grows the table; the active list is sized with it.
- Tests read the value of a node with `box_render_transition_test_value()`, declared in
  `src/render/render_packet.h` next to the null backend, after `box_render_sync`. It exists so
  that tests can check values without a GPU and is not reachable from a public header, a
  component, a stat or the shell.

### Hierarchy

Without any matrix inverse, because a target scale of 0 must work: a transitioning entity's
rendered world matrix is `W_r(E) = W_r(parent node) * rel(E) * L_r(E)`, where `rel(E)` is the
chain of target locals from the nearest ancestor node (the target world matrix of the parent, if
there is none), and `L_r(E)` is built from the rendered position, rotation and scale. Each
affected slot gets `W_r(slot) = W_r(node) * rel(slot)`, with `rel(slot)` being the target chain
from the node including the instance scale. The game thread computes the `rel` matrices each
active frame, walking both `ChildOf` and `Parent`.

### Batches

An entity under an active transition renders from a batch keyed as dynamic for the stream
involved (`dynamic_transform`, `dynamic_material` in `box_draw_key_t`, `src/render/draw.c`).
That reuses per frame run uploads, keeps it out of bakes (`key->bake`) and static shadow
caching, and gives each slot a single byte location to patch. A private tag on the entity and
its affected descendants drives this and is removed at the end, which rekeys the slots back. The
tag is checked only where the key is computed, which is not per frame.

## Transform transitions

Implemented in `src/render/transitions.c` (game thread) and `src/render/transition.c` (render
thread).

- A node *moves* in a frame when `Position`, `Rotation` or `Scale` is active at the packet time
  (`box_render_transitions_active`), and it is not a prefab, has a `Position` and no
  `ManualTransform`.
- The extract walks from every moving node without a moving ancestor down its subtree, through
  `ChildOf` and `Parent` (`ecs_get_parent`, `ecs_children`). Each descendant belongs to its
  nearest moving ancestor; a moving descendant is a node of its own whose parent node is that
  ancestor, so nodes come parent first. The walk skips prefabs and stops at an entity whose
  world matrix is not derived from its parent's (`ManualTransform`, or a `WorldTransform`
  without `Position`). The walk is made again every frame, so a new parent, slot or batch
  during a transition is followed.
- The drawn entities of the walk (with a `DrawSlot` or `TextSlots`) carry the private tag
  `TransitionTransform` (`box.render.TransitionTransform`, `(OnInstantiate, DontInherit)`).
  `box_draws_key` and the glyph key make it `dynamic_transform`, and not `bake`; the observer
  `DrawsOnTransitionTransform` rekeys the slot when the tag is added or removed. The extract
  system runs `immediate`, so the tag is added before `RenderExtractDraws` of the same frame
  and removed in the first frame the node no longer moves. `box_transform_dynamic`, the
  transform systems and `DynamicTransform` are unchanged: the entities keep their target
  `WorldTransform`, and their slots hold the target, written on change as for a static shape.
- A dynamic_transform batch uploads its transform run every frame; the batch remembers the
  packet data offset of that upload (`transform_frame`, `transform_data`).
- A second system, `RenderExtractTransitionSlots`, runs after `RenderExtractDraws` and before
  the lights. For each moving node it sends a `POSE` record, and for each transform it moves
  (its own shape, the shapes and glyphs of its descendants) a `SLOT` record with the byte
  offset of the `box_render_transform_t` in the packet data. Both point at matrices in the
  packet data:
  - `POSE`: `box_render_transition_pose_t`, `rel` being the target locals from the parent node
    down to the parent of the node, or the target world matrix of the parent without a parent
    node, and the target `Position`, `Rotation` and `Scale` of the node.
  - `SLOT`: the target locals from the node down to the entity, times the instance matrix of
    the slot: the instance scale `box_draws_transform` folds in for 3D, 2D and surface shapes,
    or the glyph quad (width, height, x, y) for text.
- The resolver, after it evaluated the values of the packet, computes
  `W_r(node) = W_r(parent node) * rel * L_r` per pose record in order, where `L_r` is
  `box_transform_local` (shared with `transformSingle`, `Ry * Rx * Rz`, scale, translate) of the
  rendered value of each component with an active transition and the target of the others,
  and writes the three rows of `W_r(node) * rel(slot)` into each slot. No matrix is inverted,
  so a scale of 0 works.
- The frame after the last active frame sends no records: the tag is removed, the slot rekeys
  to its static batch and holds its target, byte for byte as without a transition.
- Cull, shadows and the forward passes read the patched transform stream. Draw order of
  blended instances, picking, `box.spatial` and bakes use the target.

Refinement of the design: the pose record carries the target values of the transform
components of the node, so the resolver builds `L_r` also from components that have no
transition of their own (an entity that transitions only `Position` and has a `Rotation`).

### Shadows and Hi-Z

- The shadow atlas caches spot and point light tiles and renders a tile again when a caster
  region reaches it (`box_render_shadow_touched`). The game thread does not know where the
  resolver puts a moving caster, but it knows a range of every value it can render: values
  interpolate per field with timing functions that do not overshoot (`box_transition_ease`),
  and a transition starts from the value rendered at that time, so each rendered field lies
  between the minimum and maximum of the targets the component had since it was last idle.
  The private per entity state keeps that range per field for `Position`, `Rotation` and
  `Scale` (`lo` and `hi` of `box_transition_slot_t`): a start of a component whose transition
  no longer runs resets it to the last target, any other start widens it with the new target.
- Each frame a caster slot moves, the slot system records a region of its scene that holds
  every pose the resolver can render it at in that frame, instead of the whole scene, so the
  tiles of the other lights stay cached. It carries the world matrix of each node as a range,
  element by element (`box_draw_transition_range_t`, `src/render/transition_bounds.c`), through
  the products the resolver computes: the range of the parent node, or the target world matrix
  of the parent, times the target chain, times the range of the local matrix, which
  `box_draws_range_local` builds from the ranges of the fields as `box_transform_local` does
  (Ry * Rx * Rz from the ranges of the sines and cosines, every rotation element kept within
  -1 to 1, the columns scaled, the position). A child under a moving parent gets the range of
  its node, so it sweeps with its parent, through `ChildOf` and `Parent` alike. A translation
  alone gives the bounds at the start and the target pose and every pose between them; a
  rotation or scale gives a box around its sweep.
- The region of a slot is `box_draws_shadow_bounds` (`src/render/batch.c`) of the range times
  the target chain of the slot: the region math of `box_draws_shadow_region`, shared, with the
  local bounds of the mesh or, for a surface instance, of its surface record, through every
  transform between the minimum and maximum of each element. Without the mesh of the batch the
  slot records a region over the whole scene.
- The regions of a frame are recorded again in the next one, so the tiles a caster was in
  during its last moving frame render again when it stops and no shadow stays behind. A slot
  that rekeys at the start or the end records the region of the slot it leaves
  (`box_draws_slot_remove`) and of the slot it gets. None of this runs while no scene keeps
  shadow tiles (`shadow_record`), and an entity without `Transition` records what it did
  before.
- A slot that rekeys moves to another page slot, whose Hi-Z history word is that of the
  instance that had it before, as for any key change. The history only decides what the early
  phase draws (`etc/shaders/cull.wgsl`): the late phase tests every instance in the frustum
  against the pyramid of this frame at its patched transform and draws it when the early phase
  did not, then writes its history. A stale word costs at most one extra early draw and never
  leaves the instance out. A moving instance writes its history at its rendered position.

## Material transitions

Implemented in `src/render/transitions.c` (game thread), `src/render/transition.c` (render
thread) and the encoding in `src/render/batch.c` and `src/render/materials.c`.

- A node runs a *material transition* in a frame when `Rgb`, `AlphaCutoff`, `PbrMaterial`,
  `Emissive`, `TextureTransform` or `Transmission` is active at the packet time
  (`box_render_transitions_active`).
- The value of a component that a record reads comes from the entity that owns it: the entity
  itself or the `IsA` base that has it (`ecs_get_target_for_id(world, e, EcsIsA, id)`). The
  record reads the rendered value of the node of that owner, so one prefab change animates its
  material table slot and every instance that inherits it, and an instance that owns the value
  has a transition of its own. A record keeps the node of each of its components
  (`box_draw_transition_material_t.nodes`), so a slot can read a transition of its own `Rgb`
  and one of an inherited `Emissive`.
- **Mesh shapes** read the shared material table (`box_render_material_t`). In a frame with a
  material transition, before the batches are sent, the extract walks the entity of each node
  with an active material transition and its `IsA` instances, down every level. A slot reads
  its value from its owner or an `IsA` base of it, so this reaches every affected slot, and
  since `Transition` is inherited over `IsA` it visits only entities with `Transition`. The
  instances are checked per table: a table whose entities own no slot (no `MaterialId` of their
  own) and have no 2D or text shading is skipped, except for those of its entities that are
  `IsA` bases themselves, so a prefab with 10,000 mesh instances that inherit its `Rgb` visits
  the prefab alone. Each visited entity that owns a slot (`box_draw_materials_t.owned`) and
  reads an active node marks its slot to upload in the frame; the slots of other owners are not
  visited. The send records where each run of the table went in the packet data
  (`box_draw_materials_t.uploaded`); after the batches, one `MATERIAL` record per slot carries
  the byte offset of the record and its target material. Mesh slots are not rekeyed: their
  instance streams do not change, only the table record does. A slot of the last frame uploads
  once more, so the frame after the end holds the target.
- **2D shapes and texts** carry their material inline (`box_render_shading_t`). The same
  walk gives each drawn 2D shape or text that reads an active value the private tag
  `TransitionMaterial` (`(OnInstantiate, DontInherit)`). `box_draws_key` and the glyph key
  make it `dynamic_material`; `DrawsOnTransitionMaterial` rekeys the slot when the tag is
  added or removed. A dynamic_material batch uploads its shading run every frame and
  remembers its packet data offset (`shading_frame`, `shading_data`); one `SHADING` record per
  shape slot and per glyph carries the offset of its `box_render_shading_t`. The tag is
  removed in the first frame without an active value, which rekeys the slot to its static
  batch with its target, byte for byte.
- **Shared encoding.** The game thread encodes records from component values with
  `box_draws_material_pack` (table) and `box_draws_material` (2D shading), over a
  `box_draw_material_t` of component pointers. The record data
  (`box_draw_transition_material_t`) holds the target value of every component the record is
  encoded from, the tints and fade of a 2D shading and the `Shape2D`. The resolver copies it,
  replaces the value of each component with a node by the value the node renders and calls
  `box_draws_transition_encode`, which runs the same functions: display bytes for the table
  color, linear emission times strength, clamped metallic, roughness and cutoff, tints mixed in
  display encoding before linearization. A component at its target gives the bytes of the
  record without a transition.
- A 2D shading record writes the color, emission and UV words; a glyph record the color and
  emission. The other words (the shape in `material` and `emissive.w`, the clip, the rank, the
  atlas area of a glyph) do not depend on a transitioned component and stay as written.
  A glyph in a colored `TextSpans` span renders with the color of the span, which an `Rgb`
  transition does not change.
- `AlphaCutoff` is part of the batch key only through `box_draws_cutout`, whether a cutoff is
  set, so a transition of its value keeps the batch: the shape has its cutout batch as long as
  its target has a cutoff, and removing `AlphaCutoff` rekeys it at once.
- `Transmission` patches the material table only: 2D shapes and texts do not render it. Its four
  floats interpolate linearly and `attenuation_color` per channel, like `Rgb` and
  `Emissive.color`. The batch key holds only whether a shape has `Transmission`
  (`transmissive`), so a factor that transitions to or from 0 keeps the shape in its
  transmissive batch; adding or removing `Transmission` rekeys it at once and does not animate.
  Its value is 20 bytes and 7 fields, which set `BOX_RENDER_TRANSITION_VALUE` to 20 and
  `BOX_RENDER_TRANSITION_FIELDS` to 7: on 64 bit targets `box_render_transition_record_t` grew
  from 64 to 72 bytes and a `box_render_transition_value_t` of a node from 72 to 88, a cost only
  entities with `Transition` pay.
- While recording shadow regions, a slot whose `AlphaCutoff` or `TextureTransform` runs (and
  the frame after) goes to `shadow_materials`, so alpha tested casters render their tiles again.
- Anything that reads `Rgb` into a material or shading record transitions: mesh shapes, 2D
  shapes and the glyphs of texts. `SurfaceMaterial`, `Border`, `AmbientLight` and other color
  fields outside `src/material`, and light color, do not.

Refinements of the design:

- For a 2D shape, `PbrMaterial` and `AlphaCutoff` change nothing in its shading (its material
  words hold the shape, and 2D shapes are not alpha tested), so they do not tag or patch it.
- `box_render_transitions_get` reads the state from the `RenderTransitionState` singleton, set in
  `box_render_transitions_init`, instead of a lookup of the system by name. The extract keeps
  the batch state it found the first time (`box_draws_state` still looks it up by name).

## Tint and opacity transitions

Implemented in `src/render/transitions.c` (game thread), `src/render/transition.c` (render
thread) and the encoding in `src/render/batch.c`.

- `Tint` composes down `ChildOf` and `Parent`, and `Opacity` multiplies down the hierarchy
  (`box_draws_fade`). A node *fades* in a frame when its `Tint` or `Opacity` is active at the
  packet time and it is not a prefab. Neither is inherited over `IsA`, so the node is the entity
  itself.
- Before the batches are sent, the extract walks from every fading node without a fading
  ancestor down its subtree, through `ChildOf` and `Parent`, skipping prefabs. It does not stop
  at `ManualTransform`: tints and opacity do not depend on the transform. Each descendant
  belongs to its nearest fading ancestor; a fading descendant is a node of its own whose parent
  is that ancestor, so nodes come parent first.
- Every drawn entity of the walk (mesh, 2D shape or text) carries `TransitionMaterial`, so its
  slot is `dynamic_material` and out of the bakes, its shading run uploads every frame and its
  shading has one byte location to patch. A 2D shape or text that a material transition did not
  list joins the shadings to patch.
- Every drawn entity at or below a node with an active `Opacity` transition also carries the
  private tag `TransitionOpacity` (`(OnInstantiate, DontInherit)`). `box_draws_key` and the
  glyph key compute the blend of a tagged shape as if its alpha were below 1, unless its
  `BlendMode` says otherwise (`box_draws_blend`): a shape from opacity 0 to 1 has target 1 but
  renders below 1, so it draws blended during the transition and casts no shadow. The tag is
  removed in the first frame the transition no longer runs, `DrawsOnTransitionOpacity` rekeys the
  slot, and it is back in the batch of its target, casting shadows again when it is opaque. The
  tag is only checked where the key is computed; the per instance alpha check of the dynamic
  shading path (`box_draws_fill_material`) is unchanged.
- **No division.** A composed tint is an affine map of the display encoded color, `c * keep +
  add`, and composing two is ordered, so the value of a node cannot be taken out of a composed
  value (and an opacity of 0 cannot be divided out). After the batches, the extract sends per
  fading node, parent first, a `FADE` record (`box_render_transition_fade_t`): the composed tint
  and alpha of the entities strictly between its parent node and it (of every ancestor without
  a parent node, `box_draws_fade`), and its target `Tint` and `Opacity`. The resolver composes
  the rendered value of the parent node, that chain and the rendered value of the node, and
  keeps both what is above the node and what it passes down.
- Per drawn entity the shading record (`SHADING`) carries the nearest fading node
  (`box_draw_transition_material_t.fade_node`) and the composed tint and alpha of the entities
  strictly between the node and the entity (`fade_rel`, `fade_alpha`). The resolver replaces the
  inherited tints and fade of the record by the node's composed value followed by that chain;
  when the entity is the node itself (`fade_self`) it inherits what the node renders above it,
  and its own `Tint` and `Opacity` are the rendered values of the node. The record then encodes
  with the functions of the game thread: `box_draws_mesh_shading` for the tint and alpha of a
  mesh shading (`BOX_DRAW_TRANSITION_MESH`, the material slot word is kept), and
  `box_draws_material` for a 2D shape (color, emission, material and UV words, so the tinted
  border color follows) and a glyph (color and emission).
- A glyph of a colored `TextSpans` span renders with the alpha of the span times the rendered
  `Opacity` of its text (`opacity_scale`).
- A record whose node did not get a `FADE` record in the packet leaves the bytes as written: the
  target.
- `Opacity` and `Tint` on a light or another entity without a shape have no render effect
  except through descendants: the entity gets no tag and no shading record, only its `FADE`
  record, which its descendants read.
- An alpha tested shadow caster (a mesh with `AlphaCutoff`, which keeps its opaque batch) under
  a fading node records its region every frame of the transition, as its shadow reads the alpha.
- The frame after the last active frame sends no records: the tags are removed, the slot rekeys
  to its static batch and holds its target, byte for byte as without a transition.

Refinements of the design:

- The design asked for one record per slot with every ancestor node's own value left out, and
  the resolver multiplying in the rendered values while walking the parent nodes. Tints compose
  in order, not by multiplication, so the values left out must be put back at their place in the
  chain: the game thread sends the chain between two nodes once per node (`FADE`, like `POSE`
  for transforms) and the chain between the node and the entity per slot, and the resolver
  composes them parent node first. Opacity, which commutes, goes the same way.
- A `DynamicMaterial` shape under an active `Opacity` transition has its blend checked against
  its target alpha by the dynamic shading path every frame, which marks it to rekey; the key it
  gets is the blended one it has, so it stays in its slot. This costs a rekey check per frame
  for such shapes only, and keeps the per instance branch out of that path.

## The fast path

An application without transitions, and every entity without `Transition` in an application
that has them, pays nothing per entity:

- No per instance branch in extraction, and no new field in `box_render_transform_t`,
  `box_render_mesh_shading_t`, `box_render_shading_t` or `box_render_material_t`. No shader
  changes. No new OnSet observers on the transform or material components.
- The resolver starts with one check on the packet and the active count and returns. The packet
  gains one empty vector.
- The extract query has no matched tables when `Transition` is not used.

The only observers the module adds are the OnSet validation of `Transition` itself,
`DrawsOnTransitionTransform`, `DrawsOnTransitionMaterial` and `DrawsOnTransitionOpacity`, on the
private tags, which only entities a transition patches ever get. A frame without an active
`Tint` or `Opacity` transition walks no entity for them. The material table records the runs it
uploads, one entry per run. A frame without an active material transition walks no slot or
entity, and a frame with one visits only the entities of its nodes and those of their `IsA`
instances that own a slot, have 2D or text shading or are bases themselves, never the other
owners of the material table.

The slot system (`RenderExtractTransitionSlots`) stores the state of the batches when it is
registered, so no frame looks it up by name, and a world without `Transition` returns from both
transition systems after checking a few counts. The sizes of the four packet records are
`_Static_assert`ed in `src/render/batch.c` and checked by `Transition_record_sizes` of
`test/cpu/render`.

### Measuring it

`bench/render` (`Transition.c`) has the cases that measure it, each against the same scene of
100k static and 10k dynamic shapes:

- `Transition_none_static` and `Transition_none_dynamic`: no `Transition`, without and with 1%
  of the static shapes moved per frame.
- `Transition_one_idle` and `Transition_one_active`: the same scene with `Transition` on one
  shape, idle or restarted every frame, to show that the other shapes don't pay.
- `Transition_idle_1000` and `Transition_active_1000`: 1000 shapes with `Transition`, idle and
  active, the cost to users of the feature.
- `Transition_prefab_rgb_idle` and `Transition_prefab_rgb_active`: one prefab with `Rgb` and
  `Transition` and 10,000 mesh instances, the cost of the walk of `IsA` instances, with the
  `material_visited` counter.

Each scene case reports `transitions_ms`, the time per frame in the two transition systems of
the extract, which a world without `Transition` keeps at the noise level, and
`Transition_resolve_empty` times the resolver on a packet without records. The instances of the
prefab cases inherit `Transition` over `IsA`, so each of them is a node that the extract compares
every frame, idle or not: that is the cost of declaring `Transition` on a prefab, not of the walk.

### The cost

The numbers of the cases above, from `bench/render` in a release build on an Apple M6 (arm64
macOS), per frame. Run them with
`bake3 run --cfg release bench/render --local-env -- Transition` and update this table when the
extract or the resolver change. The frame is the `ns/iter` of the case; `transitions_ms` is the
counter of the same name (its total, a time per frame measured over separate frames with system
time measurement on, so it is not a part of the frame column and can even exceed it).

| Case                          | Frame (ms) | `transitions_ms` | Note                          |
|-------------------------------|-----------:|-----------------:|-------------------------------|
| `Transition_none_static`      |      0.069 |          0.00008 | baseline, no `Transition`     |
| `Transition_none_dynamic`     |      0.266 |          0.00008 | baseline, 1% moved per frame  |
| `Transition_one_idle`         |      0.070 |          0.00016 | one shape with `Transition`   |
| `Transition_one_active`       |      0.070 |          0.00038 | one shape restarted per frame |
| `Transition_idle_1000`        |      0.107 |            0.079 | 1000 idle nodes               |
| `Transition_active_1000`      |      0.474 |            0.143 | 1000 active nodes             |
| `Transition_prefab_rgb_idle`  |      0.349 |            0.467 | 10,000 inheriting instances   |
| `Transition_prefab_rgb_active`|      0.348 |            0.347 | the same, prefab `Rgb` active |
| `Transition_resolve_empty`    |   0.000002 |                - | resolver on an empty packet   |

What to read from them: the frame of `Transition_one_*` equals that of `Transition_none_static`
within noise (0.070 and 0.069 ms), so the 110,000 other shapes do not pay for one shape with
`Transition`, and a world without it spends 0.08 µs per frame in the two transition systems. An
idle node costs one compare per transitioned component per frame, about 80 ns (0.079 ms for
1000). An active node adds its records and its patches, and an active transform node the walk of
its subtree: 1000 active nodes take 0.143 ms in the extract and rekey their slots to dynamic
batches, which makes their frame 0.474 ms. Declaring `Transition` on a prefab makes every
instance a node that is compared every frame, 0.35 to 0.47 ms for 10,000 instances whether the
prefab's `Rgb` runs or not; the walk itself visits the prefab alone (`material_visited` 1). The
resolver returns from a packet without records in 2 ns.

## Tests

- `test/cpu/core` (`Transition`): the component, its validation and the timing functions.
- `test/cpu/render` (`Transition`, `TransitionMaterial`, `TransitionTintOpacity`,
  `TransitionShadow`): the records of the game thread and the values of the resolver with the
  null backend, and the shadow regions of moving casters, interrupted and nested.
- `test/gpu/render`: the rendered transforms, materials, tints and opacity on a GPU, and in
  `ShadowCache` (`transition_*`) that a caster that transitions renders the tile of its light
  only, also on the frame it ends in, and the tiles of the other lights stay cached.
- `test/integration/visual/transitions_*.flecs`: renders frozen in the middle of a transition,
  compared with their baselines: a transform transition with children
  (`transitions_transform`), a shadow caster mid move (`transitions_shadow`), material color and
  emissive transitions with a delayed one and a prefab (`transitions_material`), tint and opacity
  on parents with children (`transitions_tint_opacity`) and a 2D shape and UI elements
  (`transitions_2d_ui`). A scene cannot change a value after its first frame by itself, and the
  first value never animates, so each changes its values with `SetAt` (`box.app`) at frame 1,
  at the start of the second frame: an entity with `SetAt: {frame: 1, entity: ball}` and the
  new values, which it copies to `ball` then. The harness runs three frames at the fixed delta of
  1/60 s, and every transition is linear over 0.05 s, so the rendered frame is one frame, a third
  of the way, into every transition, at a world time of 3/60 s. No scene reads the wall clock.
- `test/integration/e2e/transitions.py`: through the shell of `apps/boxrun`, a `set` of
  `Position` or `Rgb` is returned by `get` in the next command, while a `capture` of the same
  frames shows the square in between, and the target after the transition. The app runs capped
  at 60 fps (`--fps 60`), since uncapped a headless app runs past a transition of 1 s between
  two commands.
- `test/integration/web/transitions.mjs`: the browser build, which renders on the main thread,
  with a scene that moves, recolors, fades and lights at once; the square passes in between and
  ends at its target, with no error.

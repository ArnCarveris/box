# Transitions

`box.transition` animates the rendered value of a component when the application changes it,
like CSS transitions. The component itself holds the new value at once: transitions exist only
in the rendered pixels.

Status: the `Transition` component, its timing functions and its validation exist
(`include/box/transition.h`), and so do the change detection of the game thread and the render
thread resolver, which evaluates every transitioned value on the packet clock. Transitions of
`Position`, `Rotation` and `Scale` render, for the entity and its descendants (see
[Transform transitions](#transform-transitions)), and so do transitions of `Rgb`,
`AlphaCutoff`, `PbrMaterial`, `Emissive` and `TextureTransform` (see
[Material transitions](#material-transitions)). Tint and opacity transitions do not patch
anything yet and are the next step of the epic.

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

From C, `box_transition_set(world, e, ecs_id(Position), &spec)` adds or replaces one entry (an
instance gets its own copy of an inherited map first), `box_transition_remove()` removes one
(the same way), and `box_transition_get()` reads one. The
storage is a flecs map (`ecs_map_t`) keyed by the component entity whose values are allocated
`TransitionSpec`s, so `ecs_map_get_deref(map, TransitionSpec, ecs_id(Position))` also reads one.

- The application sets a component as always (`ecs_set`, script, a query writing a field). The
  component holds that value immediately and keeps holding it. `ecs_get`, the shell `get`, REST,
  queries, picking, `box.spatial`, collision, `WorldTransform` and every other game thread reader
  see the target only.
- There is no API, component, stat or query that returns the in-progress value. It exists only
  on the render thread. Only rendered pixels show it.
- Transitionable: `Position`, `Rotation`, `Scale`, `Rgb`, `Opacity`, `AlphaCutoff`, `Tint`,
  `PbrMaterial`, `Emissive` and `TextureTransform` (`box_transition_supported()`). Others are
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
  resolver puts a moving caster, so in every frame a scene has a moving caster, and in the
  frame after, the extract records a region over the whole scene: every tile of the scene
  renders again and no shadow stays behind. A slot that rekeys at the start or the end records
  the region of the slot it leaves (`box_draws_slot_remove`) and of the slot it gets.
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
  `Emissive` or `TextureTransform` is active at the packet time
  (`box_render_transitions_active`).
- The value of a component that a record reads comes from the entity that owns it: the entity
  itself or the `IsA` base that has it (`ecs_get_target_for_id(world, e, EcsIsA, id)`). The
  record reads the rendered value of the node of that owner, so one prefab change animates its
  material table slot and every instance that inherits it, and an instance that owns the value
  has a transition of its own. A record keeps the node of each of its components
  (`box_draw_transition_material_t.nodes`), so a slot can read a transition of its own `Rgb`
  and one of an inherited `Emissive`.
- **Mesh shapes** read the shared material table (`box_render_material_t`). In a frame with a
  material transition, before the batches are sent, the extract walks the slots of the table
  (one per prefab or owner of a `MaterialId`), finds the slots whose components have an active
  node, and marks them to upload in the frame. The send records where each run of the table
  went in the packet data (`box_draw_materials_t.uploaded`); after the batches, one `MATERIAL`
  record per slot carries the byte offset of the record and its target material. Mesh slots
  are not rekeyed: their instance streams do not change, only the table record does. A slot
  of the last frame uploads once more, so the frame after the end holds the target.
- **2D shapes and texts** carry their material inline (`box_render_shading_t`). The extract
  walks the entity of each node with an active material transition and its `IsA` instances,
  down every level, and gives each drawn 2D shape or text that reads an active value the
  private tag `TransitionMaterial` (`(OnInstantiate, DontInherit)`). `box_draws_key` and the
  glyph key make it `dynamic_material`; `DrawsOnTransitionMaterial` rekeys the slot when the tag
  is added or removed. A dynamic_material batch uploads its shading run every frame and
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
- While recording shadow regions, a slot whose `AlphaCutoff` or `TextureTransform` runs (and
  the frame after) goes to `shadow_materials`, so alpha tested casters render their tiles again.
- Anything that reads `Rgb` into a material or shading record transitions: mesh shapes, 2D
  shapes and the glyphs of texts. `SurfaceMaterial`, `Border`, `AmbientLight` and other color
  fields outside `src/material`, and light color, do not.

Refinements of the design:

- For a 2D shape, `PbrMaterial` and `AlphaCutoff` change nothing in its shading (its material
  words hold the shape, and 2D shapes are not alpha tested), so they do not tag or patch it.
- `box_render_transitions_get` reads the state from the `RenderTransitions` singleton, set in
  `box_render_transitions_init`, instead of a lookup of the system by name. The extract keeps
  the batch state it found the first time (`box_draws_state` still looks it up by name).

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
`DrawsOnTransitionTransform` and `DrawsOnTransitionMaterial`, on the private tags, which only
entities a transition patches ever get. The material table records the runs it uploads, one
entry per run, and a frame without an active material transition walks no slot or entity.

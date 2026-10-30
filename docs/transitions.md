# Transitions

`box.transition` animates the rendered value of a component when the application changes it,
like CSS transitions. The component itself holds the new value at once: transitions exist only
in the rendered pixels.

Status: the `Transition` component, its timing functions and its validation exist
(`include/box/transition.h`), and so do the change detection of the game thread and the render
thread resolver, which evaluates every transitioned value on the packet clock. Nothing patches
instance or material data yet, so rendered output does not change; patching the transforms,
materials, tint and opacity are the next steps of the epic.

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
render thread has no chain to recompute it through.

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

Implemented in `src/render/transitions.c`, the first system of `RenderExtract`:

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
  the value without a transition. Removing `Transition` and deleting the entity both send
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

## The fast path

An application without transitions, and every entity without `Transition` in an application
that has them, pays nothing per entity:

- No per instance branch in extraction, and no new field in `box_render_transform_t`,
  `box_render_mesh_shading_t`, `box_render_shading_t` or `box_render_material_t`. No shader
  changes. No new OnSet observers on the transform or material components.
- The resolver starts with one check on the packet and the active count and returns. The packet
  gains one empty vector.
- The extract query has no matched tables when `Transition` is not used.

The only observer the module adds is the OnSet validation of `Transition` itself.

# Transitions

`box.transition` animates the rendered value of a component when the application
changes it, like CSS transitions. The component holds its target immediately.
Game logic, queries, picking, collision and `WorldTransform` all see that target;
the intermediate value exists only on the render thread.

Use [keyframe animation](animation.md) when the application needs to read the
animated value or animate an arbitrary reflected numeric member.

## Authoring

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

`Transition` maps component names to `TransitionSpec` values:

| Field | Default | Meaning |
| --- | --- | --- |
| `duration` | 0 | Seconds to reach the target after the delay |
| `delay` | 0 | Seconds to hold the rendered value before moving |
| `timing_function` | `Linear` | `Linear`, `EaseIn`, `EaseOut`, `EaseInOut` or `CubicBezier` |
| `bezier` | 0 | `{x1, y1, x2, y2}` handles, used only by `CubicBezier` |
| `on_add` | false | Adding the component animates from its neutral value |
| `on_remove` | false | Removing the component animates to its neutral value |

The first value does not animate. A later script, shell or C write starts the
transition. For example, after the entity above has rendered, setting its position
with `set e box.transform.Position {x: 4, y: 1, z: 0}` moves its pixels over
half a second; `get` returns the new position immediately.

`Transition` is inherited over `IsA`. A prefab can declare the map for its instances;
an instance that sets its own map replaces the whole inherited map.

### Adding and removing

Adding or removing a transitioned component snaps unless its entry opts in. With
`on_add`, a component added to an entity that had no value, owned or inherited,
animates from its neutral value, the value that renders like not having it. With
`on_remove`, a removed component without an inherited value left animates to its
neutral value, then the entity renders without it. Both use the entry's delay,
duration and curve.

```flecs
glow {
  Rect: {40, 40}
  Transition: [
    Emissive: {duration: 0.3, on_add: true, on_remove: true},
    Tint: {duration: 0.15, timing_function: EaseOut, on_add: true, on_remove: true}
  ]
}
```

A scene no longer needs to keep a `Tint` at alpha 0 or an `Emissive` at strength 0
on an entity just so it can fade. Game logic sees the component as added or removed
at once; only the pixels animate. Adding the component again while it animates out,
or removing it while it animates in, starts from the rendered value without a jump.

| Component | Neutral value |
| --- | --- |
| `Rotation` | `{0, 0, 0}` |
| `Scale` | `{1, 1, 1}` |
| `Opacity` | `{1}`, blended for the whole animation |
| `Tint` | The target color with alpha 0 |
| `Emissive` | Strength 0, the target color |
| `PbrMaterial` | The metallic and roughness used without one |
| `TextureTransform` | `{1, 1, 0, 0}` |
| `ClearCoat` | Strength 0, the target roughness |
| `SpecularAnisotropy` | Strength 0, the target rotation |
| `Sheen` | Black, the target roughness |
| `DiffuseTransmission` | Factor, thickness and blur 0, the target color |
| `Fresnel` | Strength and opacity 0, the target color, power and bias |
| `Distortion` | Strength 0, the target scale and speed |
| `BoxShadow` (add only) | The target offsets, blur and spread, color alpha 0 |
| `PointLight` | Intensity 0, the target range, bake mode and shadows |
| `SpotLight` | Intensity 0, the target range, angles, bake mode and shadows |

`Position`, `Rgb`, `AlphaCutoff` and `Transmission` have no neutral value: an entity
without a `Position` is not placed by its parent, a shape without `Rgb` renders a
default grey, and the other two change the batch of a shape. `DirectionalLight` is
not transitionable and has none either; the `Rgb` of a light still transitions. Removing `BoxShadow`
frees its shadow at once. `on_add` or `on_remove` on one of them logs an error and is
ignored; the rest of the entry still works.

Deleting an entity, removing its shape (`Rect`, `Box`, ...) or disabling it is not
animated: the renderer keeps no instances of its own and only patches what the game
thread draws in that frame, so a shape that is no longer drawn has nothing left to
animate. Spawning an entity, or adding `Transition`, does not animate its first
values either.

An instance that inherits a `Transition` with `on_add` or `on_remove` entries gets
transition state of its own, so it can animate a component it adds or removes.

### From C

```c
ecs_set(world, e, Position, {0, 1, 0});
box_transition_set(world, e, ecs_id(Position), &(TransitionSpec){
    .duration = 0.5f, .timing_function = EaseInOut });
```

`box_transition_set` adds or replaces one entry; `box_transition_remove` removes
one; `box_transition_get` reads an owned or inherited entry. Editing an inherited
map first copies it to the instance. Removing an absent entry does nothing;
removing the last entry leaves an empty map. Use `ecs_remove(world, e, Transition)`
to remove the component itself.

These helpers also work on a deferred stage: multiple edits before the merge edit
the same pending map. One edge case: setting an entry on an entity without
`Transition`, removing the component (or clearing/deleting the entity), then removing
an entry on the same stage can leave an empty `Transition` after the merge.
See [transition.h](../include/box/transition.h) for the API.

### Easing

The presets use the CSS cubic Beziers:

| Preset | Handles |
| --- | --- |
| `EaseIn` | `{0.42, 0, 1, 1}` |
| `EaseOut` | `{0, 0, 0.58, 1}` |
| `EaseInOut` | `{0.42, 0, 0.58, 1}` |

For custom easing:

```flecs
panel {
  Opacity: {1}
  Transition: [Opacity: {
    duration: 0.3,
    timing_function: CubicBezier,
    bezier: {0.2, 0, 0, 1}
  }]
}
```

The curve maps linear progress on x to eased progress on y. All handles must be
finite; x handles must be in 0..1. Y handles may leave that range to overshoot.
Float fields extrapolate; byte colors round and clamp to 0..255. Encoded material
values retain their normal range clamps. Transforms remain free, so scale can
overshoot through zero and mirror.

`box_transition_ease(fn, t)` evaluates presets;
`box_transition_ease_bezier(fn, &bezier, t)` also handles custom curves. Both are
pure and allocate nothing. Keyed curves use a different format; see [Curves](curves.md).

## Playback rules

- Transitions use world time (`packet->time`), so pausing or scaling world time
  pauses or scales them.
- Spawning an entity, adding `Transition`, or first owning a formerly inherited
  value records the current value without animating. Only subsequent changes animate.
- A change starts from the value currently rendered, with the full delay and
  duration. Interruptions do not jump. Equal writes do nothing.
- Changes are detected by comparing component values, including writes that emit
  no `OnSet`. A component changed every frame restarts every frame and appears smoothed.
- Zero duration and delay means no transition. Zero duration with a delay switches
  after that delay.
- Removing the component snaps unless its entry has `on_remove` (see
  [Adding and removing](#adding-and-removing)); a removed value that leaves an
  inherited one always snaps to it. Adding a component snaps unless its entry has
  `on_add`. Removing the map entry or `Transition` snaps to target, also during an
  `on_remove` animation. Invalidating an entry also snaps. Changing a spec leaves a
  running transition alone until the next value change.
- Disabling an entity cancels its transition and hides it. Re-enabling shows its
  current target as a first value; later changes animate again. Deletion drops state.
- Rotation interpolates Euler axes without wrapping or choosing a shortest path.
  Byte colors interpolate per channel in authored display encoding. Other fields
  interpolate linearly after easing.

Unsupported components, negative duration/delay and invalid Bezier handles log an
error naming the entity/component and ignore that entry. Other entries still work.

## Supported components

| Category | Components | Scope |
| --- | --- | --- |
| Transform | `Position`, `Rotation`, `Scale` | Shape, text and shadow slots on the entity and descendants |
| Material | `Rgb`, `Emissive`, `TextureTransform` | Meshes, 2D shapes and text where the field is rendered |
| Mesh material | `AlphaCutoff`, `PbrMaterial`, `Transmission`, `ClearCoat`, `SpecularAnisotropy`, `Sheen`, `DiffuseTransmission`, `Fresnel`, `Distortion` | Mesh material table |
| Hierarchy | `Tint`, `Opacity` | Composed down `ChildOf` and `Parent` |
| 2D shadow | `BoxShadow` | Shadow offset, blur, spread and color |
| 2D shape | `Border` | Border width, radius and color of a `Rect`, `Circle` or `Polygon2D` |
| Light | `PointLight`, `SpotLight` | Intensity, range and cone angles; bake mode and shadows switch at once |
| Light color | `Rgb` | Color of a `PointLight`, `SpotLight` or `DirectionalLight` on the entity |

Material values transition on the entity that owns them. A prefab change therefore
animates its material and the instances inheriting it; an overridden value has its
own state. A slot can combine its own transitioning color with inherited emission.

`Tint` composes in hierarchy order and `Opacity` multiplies. During an opacity
transition, affected shapes are treated as blended unless `BlendMode` overrides
that choice. They cast no opaque shadow until they return to an opaque target;
alpha-tested casters update their shadow tiles as alpha changes.

`SpecularAnisotropy.rotation` also interpolates without wrapping; it wraps only when
packed for rendering. Adding or removing material components snaps, unless the
entry has `on_add` or `on_remove` and the component has a neutral value; adding or
removing `AlphaCutoff` or `Transmission` changes the batch and always snaps. Texture
handles (`PbrTextures`), computed components (`WorldTransform`, `MaterialId`), enums
and tags cannot transition.

### Box shadows

```flecs
using box.geometry
using box.material
using box.transition

card {
  Rect: {200, 120}
  Rgb: {255, 255, 255}
  BoxShadow: {offset_y: -2, blur: 4, color: {0, 0, 0, 120}}
  Transition: [BoxShadow: {duration: 0.15, timing_function: EaseOut}]
}
```

A later change to `{offset_y: -12, blur: 24, spread: 2, color: {0, 0, 0, 160}}`
lifts and softens the shadow. Shadows follow transform, tint and opacity transitions
of their shape or ancestors at the same time. A shadow that shrinks to no area can
animate through that boundary. Adding `BoxShadow` fades it in from alpha 0 with
`on_add`, else it snaps; removing it always snaps.
`BoxShadow` copies into prefab instances, so each instance lifts independently.

### Borders

```flecs
using box.geometry
using box.material
using box.transition

field {
  Rect: {240, 32}
  Rgb: {255, 255, 255}
  Border: {width: 1, radius: 4, color: {160, 160, 160}}
  Transition: [Border: {duration: 0.12, timing_function: EaseOut}]
}
```

Setting `Border: {width: 2, radius: 4, color: {40, 110, 240}}` on focus grows the
ring and changes its color. Width and radius interpolate linearly by the timing
function, the color per channel in display encoding; a width or radius that an
overshooting curve takes below 0 draws as 0. A `BoxShadow` of the shape follows
the rendered border: its cutout radius and the alpha it is covered with. Tint and
opacity transitions of the shape or its ancestors apply to the border color at the
same time. A `Line` draws no border, so a `Border` transition on it does nothing.
`Border` copies into prefab instances, so each instance animates independently.
Picking, layout and `ClipChildren` use the target border: clipped children keep
the target corner radius and inset during a radius or width transition. Adding or
removing `Border` snaps.

## Limitations

- `ManualTransform`, including `SurfaceAttachment`, breaks transform propagation.
  Such an entity and its descendants render at their target transforms, even under
  a transitioning ancestor. Tint and opacity still propagate through them.
- Lights and cameras read target `WorldTransform`. Their positions, rotations and
  light colors do not transition. A shape on the same entity can still animate.
- Colored `TextSpans` retain their span colors during an `Rgb` transition, though
  text opacity still affects their alpha. `SurfaceMaterial`, `AmbientLight`
  and other colors outside `src/material` do not transition.
- Draw order and batch membership follow targets. Picking and collision can therefore
  disagree with the visible intermediate position.
- Bake invalidation uses game-thread changes. Light/reflection bakes and probe captures
  use the rendered material and shading values of the frame when they run. Tint is
  ignored by captures and never invalidates a bake.

## How rendering works

The render thread cannot access the ECS world. The extract sends node lifecycle
and transition-start records, plus offsets of GPU records to patch. A start carries
a target and timing, never a `from`: the resolver evaluates the previous curve at
the start time to preserve continuity across packets in flight.

| File | Responsibility |
| --- | --- |
| [src/render/transitions.c](../src/render/transitions.c) | Compare targets, track nodes, walk affected hierarchies and emit records |
| [src/render/transition.c](../src/render/transition.c) | Evaluate curves and patch packet bytes before the backend renders |
| [src/render/transition_bounds.c](../src/render/transition_bounds.c) | Bound moving shadow casters conservatively |
| [src/render/render_packet.h](../src/render/render_packet.h) | Packet record formats |
| [src/render/batch.c](../src/render/batch.c), [materials.c](../src/render/materials.c) | Shared material/shading encoders |

The resolver runs before `backend.render`, on the render thread or in main-thread
mode, and also works with the null backend. Node state survives device loss;
transition records still apply when a packet's backend generation is stale.

### Transforms and shadows

The extract sends target-local chains between transition nodes and from nodes to
slots. The resolver builds rendered matrices parent first:

`W_render(node) = W_render(parent_node) * relative_chain * L_render(node)`

`W_render(slot) = W_render(node) * relative_chain(slot)`

No matrix inverse is needed, so zero scale works. Hierarchy walks follow both
`ChildOf` and `Parent`, stop at manual transforms and skip prefabs. Active slots
use dynamic-transform batches; after completion they rekey to their target batches.
Cull, shadow and forward passes all read the patched transform stream.

Shadow-cache invalidation uses a conservative range of possible rendered poses,
including interrupted transitions, nested transforms and Bezier overshoot. Only
light tiles touched by caster regions are refreshed, including the frame after
movement ends. Range math runs only for caster subtrees while shadow tiles are cached.
Hi-Z's late pass retests instances at their rendered positions, so stale slot
history after a rekey cannot hide a moving instance.

### Materials and hierarchy fades

Mesh material transitions patch table records; 2D shapes and glyphs patch inline
shading in dynamic-material batches. Encoders are shared with normal extraction,
so completed transitions produce the same bytes as static targets. Hierarchical
tints send the chains between nodes in order; opacity uses the same traversal.
Neither requires dividing out a target value, so zero opacity works.

Box shadows have separate slots. A `BoxShadow` transition patches the whole shadow
transform and shading; combined transforms and fades are resolved in that same
record so they remain aligned with the shape. A `Border` transition patches the
material words of the 2D shading of its shape, and the shadow of the shape gets a
box shadow record that applies the rendered border to its cutout.

## Cost and verification

Entities without `Transition` have no per-entity transition work or extra GPU fields.
An unused extract query matches no tables, and an empty resolver returns after
checking counts. Idle nodes compare their listed components each frame. Instances
inheriting a map but owning none of its values are skipped per table.

Active nodes add records, uploads and patches. Transform and fade transitions walk
affected descendants; material transitions visit affected owners and `IsA` instances,
skipping tables that need no patch. Work grows with the affected slots and glyphs.
No shader transition logic is added.

Run `bake3 run --cfg release bench/render --local-env -- Transition` to compare
unused, idle, active, prefab and shadow-caster cases. Historical release measurements
on an arm64 Linux VM put 1,000 idle nodes at about 0.053 ms of extract time and
1,000 active nodes at 0.198 ms; these are workload-specific, not GPU timings.

Tests cover validation/easing in `test/cpu/core`, packet/resolver behavior in
`test/cpu/render`, and pixels/shadow caching in `test/gpu/render`.
The `transitions_*.flecs` visual scenes, end-to-end `transitions.py` and browser
`transitions.mjs` exercise the same behavior through apps. Visual examples use
`SetAt` to change values after the first frame; declare `SetAt` before the values
it copies so implicitly constructed components are distinguished from authored ones.

# Transition

`box.transition` (`include/box/transition.h`) animates the *rendered* value of a component when it
changes, like CSS transitions. The component holds the new value at once for every game-thread
reader (queries, picking, collision, `WorldTransform`); only the pixels move, on the render thread.
For animating values the game reads, or arbitrary numeric members, use `docs/animation.md`.

## Components

| C name | Script name | Fields |
| --- | --- | --- |
| `Transition` | `Transition` | map component id → `TransitionSpec`: `Transition: [Position: {...}]`; inherited over `IsA` |
| `TransitionSpec` | `Spec` | `duration` (s, 0), `delay` (s, 0), `timing_function` (`Linear`), `bezier`, `on_add` (false), `on_remove` (false) |
| `TransitionBezier` | `Bezier` | `x1, y1, x2, y2` of a CSS `cubic-bezier()`, script `{0.2, 0, 0, 1}` |
| `TimingFunction` | `TimingFunction` | `Linear`, `EaseIn` (0.42,0,1,1), `EaseOut` (0,0,0.58,1), `EaseInOut` (0.42,0,0.58,1), `CubicBezier` |

## Script

```flecs
using box.transform
using box.material
using box.transition

e {
  Position: {0, 1, 0}
  Rgb: {255, 0, 0}
  Transition: [
    Position: {duration: 0.5, timing_function: EaseInOut},
    Rgb: {duration: 1.0, delay: 0.25}
  ]
}

glow {
  Rect: {40, 40}
  Transition: [
    Emissive: {duration: 0.3, on_add: true, on_remove: true},
    Tint: {duration: 0.15, timing_function: EaseOut, on_add: true, on_remove: true}
  ]
}

panel {
  Opacity: {1}
  Transition: [Opacity: {duration: 0.3, timing_function: CubicBezier, bezier: {0.2, 0, 0, 1}}]
}
```

Shared specs as constants (`etc/scenes/ui/theme.flecs`):

```flecs
export const WidgetFade: Spec = {duration: 0.12, timing_function: EaseOut}
Transition: [Rgb: WidgetFade, Tint: WidgetFade]
```

Shell: `set e box.transform.Position {x: 4, y: 1, z: 0}` animates the pixels; `get` returns the
target at once.

## Rules of Transition entries

An entry is used only when `box_transition_valid()` holds; otherwise it logs an error naming the
entity and component and is ignored (other entries still work):
- The component is transitionable (`box_transition_supported`, table below), else
  `component '<c>' is not transitionable, ignored`.
- `duration >= 0` and `delay >= 0`.
- With `CubicBezier`: handles valid (`box_transition_bezier_valid`): `x1`, `x2` in 0..1, `y1`, `y2`
  finite. y outside 0..1 overshoots (floats extrapolate; byte colors clamp to 0..255; transforms
  are free, scale can pass through 0 and mirror).
- `on_add`/`on_remove` on a component without a neutral value in that direction
  (`box_transition_neutral_supported`) logs an error; the flag is ignored, the rest of the entry works.
- Duration 0 and delay 0 = no transition; duration 0 with a delay switches after the delay.
- `bezier` is ignored by the presets.
- An instance that sets its own `Transition` replaces the whole inherited map.
- Changing a spec leaves a running transition alone until the next value change. Removing the
  entry or `Transition`, or invalidating the entry, snaps to target (also mid `on_remove`).

## Supported components and neutral values

| Component | Transitions | Neutral (for `on_add`/`on_remove`) |
| --- | --- | --- |
| `Position` | yes | none |
| `Rotation` / `Scale` | yes | `{0, 0, 0}` / `{1, 1, 1}` |
| `Rgb` | yes (also light color of Point/Spot/DirectionalLight) | none |
| `Opacity` | yes, multiplies down hierarchy | `{1}`, blended during the animation |
| `Tint` | yes, composes down hierarchy | target color, alpha 0 |
| `AlphaCutoff`, `Transmission` | yes | none (change the batch) |
| `Emissive` | yes | strength 0 |
| `PbrMaterial` | yes | metallic/roughness used without one |
| `TextureTransform` (`texture.Transform`) | yes | `{1, 1, 0, 0}` |
| `ClearCoat`, `SpecularAnisotropy` | yes | strength 0 |
| `Sheen` | yes | black |
| `DiffuseTransmission` | yes | factor, thickness, blur 0 |
| `Fresnel` | yes | strength and opacity 0 |
| `Distortion` | yes | strength 0 |
| `BoxShadow` | yes | add only: color alpha 0; removing frees the shadow at once |
| `Border` | yes (width, radius, color of `Rect`/`Circle`/`Polygon2D`) | none |
| `PointLight`, `SpotLight` | intensity, range, cone; bake mode/shadows switch at once | intensity 0 |

Everything else (`DirectionalLight`, `PbrTextures`, `WorldTransform`, `MaterialId`, enums, tags) cannot.

## Playback rules

- Uses world time (`packet->time`): pausing or scaling world time pauses/scales transitions.
- First value never animates: spawning, adding `Transition`, or first owning a formerly inherited
  value records the value. Only later changes animate.
- A change starts from the currently rendered value, with the full delay and duration; no jumps on
  interrupt. Equal writes do nothing.
- Changes are detected through `OnSet`: a write without one (a raw query field without
  `ecs_modified`) does not start a transition. A value changed every frame restarts every frame
  (looks smoothed/laggy).
- Add/remove snaps unless the entry has `on_add`/`on_remove`. `on_add` only when the entity had no
  value (owned or inherited); `on_remove` only when no inherited value remains (else snaps to it).
  Re-adding during an animate-out (or removing during animate-in) continues from the rendered value.
- Deleting the entity, removing its shape, or disabling it is not animated. Re-enabling shows the
  current target as a first value.
- Rotation interpolates Euler axes without wrapping. Byte colors interpolate per channel in display
  encoding. `Border` width/radius below 0 draw as 0.
- Material transitions run on the entity that owns the value: a prefab change animates the prefab
  and its inheriting instances; overridden values have their own state.
- During an `Opacity` transition shapes draw blended (unless `BlendMode` overrides) and cast no
  opaque shadow until back at an opaque target.

## C API

```c
ecs_set(world, e, Position, {0, 1, 0});
box_transition_set(world, e, ecs_id(Position), &(TransitionSpec){
    .duration = 0.5f, .timing_function = EaseInOut });
```

| Function | Does |
| --- | --- |
| `box_transition_set(world, e, comp, &spec)` | add/replace one entry; adds `Transition`; copies an inherited map first; emits OnSet |
| `box_transition_remove(world, e, comp)` | remove one entry (no-op without one); last entry leaves an empty map |
| `box_transition_get(world, e, comp)` | owned or inherited entry, or NULL |
| `box_transition_valid / _supported / _neutral_supported / _bezier_valid` | validation predicates |
| `box_transition_ease(fn, t)`, `box_transition_ease_bezier(fn, &bezier, t)` | pure, no allocation, render-thread safe |

Raw map access: `ecs_map_get_deref(map, TransitionSpec, component)`. Remove the component itself
with `ecs_remove(world, e, Transition)`. On a stage the helpers are deferred and edit the same
pending map. Edge case: set, `ecs_remove(Transition)`, then remove on one stage leaves an empty
`Transition` after the merge.

## Limitations

- `ManualTransform` (incl. `SurfaceAttachment`) breaks transform propagation: it and its
  descendants render at target transforms; tint and opacity still propagate.
- Lights and cameras read the target `WorldTransform`: their position/rotation don't transition.
- Picking, collision, layout, draw order, batching and `ClipChildren` use targets.
- Colored `text.Spans` keep their span colors during an `Rgb` transition. `SurfaceMaterial`,
  `AmbientLight` and colors outside `src/material` don't transition.
- A `Line` has no border: a `Border` transition on it does nothing.

## Cost and implementation

- Entities without `Transition` cost nothing, and neither do idle nodes. Only changed entities
  and running nodes are compared; active ones add packet records and patches. No shader
  transition logic.
- Extract: `src/render/transitions.c` (+ `transition_nodes.c` for the observers and the visit of
  changed entities); render-thread resolver: `src/render/transition.c`
  (+ `transition_material.c`, `transition_light.c`, `transition_box_shadow.c`,
  `transition_bounds.c` for conservative shadow-caster bounds).
- Benchmark: `bake3 run --cfg release bench/render --local-env -- Transition`.
- Tests: `test/cpu/core` (`Transition`), `test/cpu/render`, `test/gpu/render`, visual
  `test/integration/visual/transitions_*.flecs` (use `SetAt` to change values after frame 1).

# Animation

`box.animation` (`include/box/animation.h`) plays keyframe clips as data. A clip entity has child
tracks; each track keys one numeric member of one component with a `Curve` (see `docs/curve.md`).
An `Animator` plays a clip on its entity and descendants, writing the real components on the game
thread, so game code reads the animated values. For render-only smoothing of value changes use
`docs/transition.md` instead.

## Components

| C name | Script name | Fields | Notes |
| --- | --- | --- | --- |
| `AnimationClip` | `Clip` | `duration` (s; 0 = time of last key of any valid track) | parent of tracks |
| `AnimationTrack` | `Track` | `component`, `member` (`"y"`, `"color.r"`), `target` (path relative to animated entity, NULL = itself), `curve` | child of clip |
| `AnimationMode` | `Mode` | `Once`, `Loop`, `PingPong` | |
| `Animator` | `Animator` | `clip`, `time`, `speed` (default 1), `mode` (default `Loop`), `paused`, `finished` | on the animated entity |

## Script

```flecs
using box.transform
using box.geometry
using box.material
using box.animation
using box.curve

bounce {
  Clip: {duration: 2}
  height {
    Track: {component: Position, member: "y", curve: {
      kind: CurveCatmullRom, keys: [{0, 0}, {1, 2}, {2, 0}]
    }}
  }
  red { Track: {component: Rgb, member: "r", curve: {keys: [{0, 255}, {2, 0}]}} }
}

ball {
  Position: {0, 0, 0}
  Sphere: {radius: 0.5}
  Rgb: {255, 0, 0}
  DynamicMaterial
  Animator: {clip: bounce}
}
```

Targets reach descendants, also of prefab instances (`test/assets/animation/animator_prefab.flecs`):

```flecs
walk {
  Clip: {}
  lift { Track: {component: Position, member: "y", target: "arm", curve: {keys: [{0, 0}, {2, 4}]}} }
}

prefab Rig {
  Animator: {clip: walk, time: 0.5, mode: PingPong}
  arm { Position: {0, 0, 0} }
}

first : Rig {}
second : Rig { Animator: {clip: walk, time: 1.5, mode: PingPong} }
```

## C API

```c
Animator *a = ecs_ensure(world, ball, Animator);   /* keeps ctor defaults speed 1, Loop */
a->clip = bounce;
ecs_modified(world, ball, Animator);
```

| Function | Does |
| --- | --- |
| `box_animation_sample(world, track, time, &value)` | sample a track, no allocation; false for invalid/deleted/keyless track |
| `box_animation_clip_duration(world, clip)` | explicit duration, or last key of its valid tracks |

`ecs_set(world, e, Animator, {...})` zeroes omitted fields (speed 0 = frozen). Setting a track
copies its strings and keys; the caller keeps ownership of its input.

## Tracks

- One track = one number. Separate axes/channels need separate tracks.
- Float, double and integer members work. Struct, enum, bool, string, entity and array members do
  not. Integer results round to nearest and clamp to the type range; floats may overshoot.
- Rotation is Euler radians interpolated as plain numbers, per axis, no shortest path: 3 → -3
  takes the long route. Key intermediate angles.
- Keys are seconds from clip start; before the first/after the last key the curve holds.
- Reflection resolves when the track is set (`TrackResolve` observer). Invalid component, missing
  or non-numeric member, no keys or a curve that fails `box_curve_validate` logs
  `AnimationTrack '<track>' of clip '<clip>': <reason>`; the track is skipped. Edits apply on the
  next sample.

## Playback

| Mode | Behavior |
| --- | --- |
| `Once` | clamp to 0..duration, hold the final value, set `finished` |
| `Loop` | wrap into 0..duration |
| `PingPong` | forward, backward, repeat (direction kept privately, also across seeks) |

- `AnimatorPlay` (`EcsOnUpdate`) advances `time` by `delta_time * speed`, samples, writes with
  `ecs_ensure` + `ecs_modified` (OnSet observers run). Transforms update in `EcsPreStore`, so the
  value is on screen the same frame.
- `speed` 2 = double, 0 = hold, negative = backward. A new animator with negative speed at time 0
  starts at the clip end. First frame samples initial time + one delta.
- `Once` finishes at the end it plays toward; seeking back or reversing clears `finished`.
- Write `time` to seek. A playing animator then advances from there.
- `paused` holds time and writes nothing, except once when time is seeked or the clip/tracks
  change. A finished `Once` or speed 0 animator writes only when its time changes.
- There are no clip events: read `time` / `finished`.
- Zero-duration clip holds time at 0. Missing/deleted/invalid clip does nothing.
- Disabled animators hold time and write nothing.

## Rules and gotchas

- One `Animator` per entity. To play two clips, give each its own (child) entity with an `Animator`.
  Two animators writing the same member: the later iteration wins, order not guaranteed.
- `Animator` is copied into prefab instances (`EcsCopy`): each instance owns time/speed/pause;
  later prefab edits do not reach existing instances. Prefabs themselves do not play.
- Writing an inherited or missing component creates an owned copy (`ecs_ensure`); from then on it
  no longer follows the prefab, including members the clip does not write.
- An entity a track writes `Position`, `Rotation` or `Scale` of gets its own `DynamicTransform`.
- Targets resolve once and refresh when the clip/tracks change or descendants are added, removed,
  renamed or reparented. Unnamed instance children are found through their `IsA` to the named
  prefab child. A missing target skips the track and warns once
  (`Animator '<e>': target '<path>' of track '<track>' not found`).
- Add `DynamicMaterial` to shapes whose material animates every frame.
- A `Transition` on an animated component restarts on every write and makes the render lag:
  don't combine them for continuous playback.

## Cost

- Per playing track per frame: one `ecs_ensure_id` + `ecs_modified_id` and component observers.
  A first write may move the entity to a new table.
- No animators: `AnimatorPlay` matches no tables.
- Benchmark: `bench/core` case `Animator` (1,000 and 10,000 animators, one `Position.y` track).
- Visual test: `test/integration/visual/animation_keyframes.flecs` (fixed delta).
- Out of scope: blending, crossfades, layers, events, skinning, morph targets, render-thread playback.

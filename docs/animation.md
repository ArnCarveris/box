# Keyframe animation

`box.animation` plays clips as data. An `AnimationClip` entity contains child
`AnimationTrack` entities; each track uses a [Curve](curves.md) to key one numeric
member of one component. An `Animator` plays the clip on an entity and its descendants.

Clips write real components on the game thread, so application code reads the
animated values. Any reflected numeric member can be animated, including transforms,
materials, lights, cameras and application components. [Transitions](transitions.md)
only animate rendered values and leave components at their targets.

## Authoring a clip

```flecs
using box.transform
using box.material
using box.animation
using box.curve

bounce {
  AnimationClip: {duration: 2}
  height {
    AnimationTrack: {component: Position, member: "y", curve: {
      kind: CurveCatmullRom, keys: [{0, 0}, {1, 2}, {2, 0}]
    }}
  }
  red {
    AnimationTrack: {component: Rgb, member: "r", curve: {keys: [{0, 255}, {2, 0}]}}
  }
  blue {
    AnimationTrack: {component: Rgb, member: "b", curve: {keys: [{0, 0}, {2, 255}]}}
  }
}

ball {
  Position: {0, 0, 0}
  Sphere: {}
  Rgb: {255, 0, 0}
  DynamicMaterial
  Animator: {clip: bounce}
}
```

| Component | Fields |
| --- | --- |
| `AnimationClip` | `duration` in seconds; zero uses the last key of any track |
| `AnimationTrack` | `component`, `member`, optional `target`, `curve` |
| `Animator` | `clip`, `time`, `speed`, `mode`, `paused`, `finished` |

One track animates one number. Separate axes/channels need separate tracks, each
with its own keys and curve kind. Nested members use dotted paths such as
`color.r` of `Emissive`. Targets use paths relative to the animated entity,
such as `arm.hand`; no target means the animated entity itself.

Float, double and integer members work. Structs, enums, bools, strings and entity
members do not. Integer results round and clamp to the type's range; float results
can overshoot. Rotation is Euler radians interpolated as plain numbers: from 3 to
-3 takes the long route. Key the intermediate angles you want.

Keys use seconds from clip start. Before the first and after the last key, the
curve holds its endpoint. `box_animation_clip_duration` returns the explicit or
inferred duration. A zero-duration clip holds time at zero.

Tracks resolve reflection when set and cache the member offset/type. An invalid
component, missing/nonnumeric member, empty curve or failed curve validation logs
an error naming the clip, track and reason; the track is skipped. Edits take effect
on the next sample.

`box_animation_sample(world, track, time, &value)` samples without allocating. It
returns false for invalid, deleted or keyless tracks, including deleted components.
See [animation.h](../include/box/animation.h) for the C API. Setting a track copies
its strings and curve keys; callers retain ownership of their input storage.

## Playback

`AnimatorPlay` advances time by `delta_time * speed`, samples tracks and writes
their values. Constructor defaults are speed 1 and `AnimationLoop`. In C, preserve
them with `ecs_ensure`, or supply every field when using `ecs_set`, which zeros
omitted members:

```c
Animator *a = ecs_ensure(world, ball, Animator);
a->clip = bounce;
ecs_modified(world, ball, Animator);
```

| Mode | Behavior |
| --- | --- |
| `AnimationOnce` | Clamp to 0..duration; hold the final value and set `finished` |
| `AnimationLoop` | Wrap into 0..duration; duration itself becomes zero |
| `AnimationPingPong` | Play forward, backward, then repeat |

Once finishes at the end it plays toward: duration for positive speed, zero for
negative speed. Seeking back or reversing speed clears finished. Ping-pong keeps
its direction privately, including across seeks. Large deltas wrap through as
many passes as needed.

Speed 2 doubles playback, zero holds time, negative plays backward. A new animator
with negative speed and time zero starts at the clip's end. Otherwise its first
frame samples the initial time plus one frame delta. Read `time` and `finished`
for gameplay timing; there are no clip events.

### Seeking and pausing

Writing `time` seeks. A playing animator then advances normally: setting time to 1
before a 0.25-second frame samples 1.25.

A paused animator holds time and normally writes nothing, leaving components free
for other systems. Seeking while paused samples once. Changing its clip or tracks,
including keys or track targets, also samples once. Pausing itself writes nothing;
unrelated clip edits and hierarchy changes leave application-written values alone.
A finished Once animator or one at speed zero writes only when its time changes.

### Prefabs and targets

`Animator` copies into prefab instances, so each owns its time, speed and pause.
Later edits to the prefab's Animator do not change existing instances. Prefabs do
not play. Targets resolve relative to each instance, including unnamed children
spawned from named prefab children.

Writing an inherited or missing component uses `ecs_ensure`, creating an owned
copy. Afterward that component no longer follows the prefab, including members
that the clip does not write.

Target paths resolve once, then refresh when the clip changes, clips/tracks are set
or removed, or descendants are added, removed, renamed or reparented. A missing
target skips its track and warns once; a target that appears later is used on the
next frame. Deleted or moved targets are skipped safely.

An entity has one Animator. Multiple animators can control parts of a model from
different entities. Avoid writing the same member from two animators: the later
iteration wins, and that order is not guaranteed.

## Frame order and cost

Playback runs on the game thread in `EcsOnUpdate`, after input/early application
phases and before the declared Velocity and AngularVelocity systems. Writes merge
and run `OnSet` observers before the next system. Transforms update in `EcsPreStore`
and render extraction follows `EcsOnStore`, so values appear in WorldTransform and
on screen in the same frame. Disabled animators hold time and write nothing.
Missing, deleted or invalid clips do nothing and leave current values in place.

Each track costs an `ecs_ensure_id` and `ecs_modified_id` per playing frame, plus
component observers. A first write may move the entity to a table to own the
component. With no animators, playback matches no tables and does not run.

- Use `DynamicMaterial` on shapes whose materials animate every frame, to avoid
  repeatedly resolving and hashing shared materials.
- Consider `DynamicTransform` for many animated transforms. Static transform writes
  queue recomputation of the entity and descendants.
- A Transition on the same component restarts on every animation write, causing the
  rendered value to lag. Avoid combining them for continuous clip playback.

`bench/core` case `Animator` measures 1,000 and 10,000 animators with one Position.y
track each. The visual example
[animation_keyframes.flecs](../test/integration/visual/animation_keyframes.flecs)
uses a fixed delta: after N frames the clip shows initial time + N × delta.
`SetAt` is only needed for separate changes at known frames.

Blending, crossfades, layers, clip events, skeletons, skinning, morph targets,
model import, shell playback controls and render-thread playback are out of scope.

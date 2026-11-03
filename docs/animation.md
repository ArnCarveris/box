# Keyframe animation

`box.animation` holds keyframe animations as data. A clip is an entity with `AnimationClip`, and
its tracks are its children with `AnimationTrack`. Each track keys one numeric member of one
component with a `Curve` of `box.curve` (see `include/box/curve.h` for the kinds, the key format
and the evaluator). An `Animator` plays a clip on an entity and its descendants (see
[Playback](#playback)).

Unlike a `Transition`, which only moves the rendered value, a clip writes the real component on
the game thread: the application reads the animated value, and any reflected component can be
animated without a render path of its own — `Position`, `Rgb`, `PointLight`, `Camera`, effect
settings or a component of the application. A write costs what an `ecs_set` costs (see
[The cost of a write](#the-cost-of-a-write)).

## The model

```c
ECS_STRUCT(AnimationTrack, {
    ecs_entity_t component; /* Component the track writes */
    char *member;           /* Path of a numeric member, such as "y" or "color.r" */
    char *target;           /* Path relative to the animated entity, NULL for itself */
    Curve curve;            /* Keys: seconds from the clip start, value in the member unit */
});

ECS_STRUCT(AnimationClip, {
    float duration;         /* Seconds; 0 for the time of the last key of any track */
});
```

- **One track, one number.** A `Curve` is scalar, so a track animates one member: a `Position`
  track names `x`, `y` or `z`, and `Rgb` takes up to three tracks. Each axis has its own keys and
  its own curve kind. Nested members are dotted paths, such as `color.r` of `Emissive`.
- **Numeric members only.** `float`, `double` and the integer widths. A struct, an enum, a bool,
  a string or an entity member is an error. An integer member rounds to nearest and clamps to the
  range of its type, so a Catmull-Rom curve that overshoots its keys keeps an `Rgb` channel in
  0..255.
- **Rotation is Euler.** `Rotation` is three angles in radians, so each axis is its own curve and
  is interpolated as a plain number: there is no shortest path, and going from 3 to -3 turns the
  long way round. Key the angles you want to pass through.
- **Time.** Key times are seconds from the start of the clip. Before the first key and after the
  last the curve holds the first or last value. A clip lasts `duration` seconds, or when that is
  0 until the last key of any of its tracks (`box_animation_clip_duration`).
- **Target.** `target` is a path relative to the animated entity, so one clip can move the parts
  of a model: `target: "arm.hand"`. Without it the track writes the animated entity itself.

## Resolving and errors

When an `AnimationTrack` is set, its member is found once through the reflection of the
component and its offset and kind are kept in a private component, so sampling and writing touch
no reflection. A change of the track reaches the next sample.

A track is invalid when its component is not set, is deleted, has no reflection, when the member
is unknown or not numeric, when the curve has no keys (a track has no default value to write) or
when `box_curve_validate` rejects the curve. An invalid track logs one error that names the clip,
the track and the reason, and is skipped:

```
AnimationTrack 'height' of clip 'bounce': curve: key 2: time 0.5 is not after time 1 of key 1
```

## Sampling

```c
bool box_animation_sample(const ecs_world_t *world, ecs_entity_t track, float time, float *value);
```

It evaluates the curve of the track at `time` and returns false for an invalid, deleted or
keyless track, or a track whose component was deleted. It does not allocate.

## Example

A clip that bounces an entity up and fades it from red to blue over two seconds. In script:

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
```

The same clip in C. `ecs_set_ptr` copies the member string and the keys, so the caller keeps
ownership of its own:

```c
ecs_entity_t clip = ecs_entity(world, { .name = "bounce" });
ecs_set(world, clip, AnimationClip, { .duration = 2 });

AnimationTrack height = {
    .component = ecs_id(Position),
    .member = "y",
    .curve = { .kind = CurveCatmullRom }
};
CurveKey keys[] = {{0, 0}, {1, 2}, {2, 0}};
for (int i = 0; i < 3; i ++) {
    *ecs_vec_append_t(NULL, &height.curve.keys, CurveKey) = keys[i];
}

ecs_entity_t track = ecs_entity(world, { .name = "height", .parent = clip });
ecs_set_ptr(world, track, AnimationTrack, &height);
ecs_vec_fini_t(NULL, &height.curve.keys, CurveKey);

float y;
if (box_animation_sample(world, track, 0.5f, &y)) {
    /* y is 1.125 */
}
```

## Playback

```c
ECS_ENUM(AnimationMode, {
    AnimationOnce = 0,    /* Plays to the end and holds the last value */
    AnimationLoop = 1,    /* Starts over at the end */
    AnimationPingPong = 2 /* Plays back and forth */
});

ECS_STRUCT(Animator, {
    ecs_entity_t clip;  /* Entity with AnimationClip */
    float time;         /* Seconds into the clip, written by the system, may be set to seek */
    float speed;        /* Multiplier of the frame delta, default 1, may be negative */
    AnimationMode mode; /* Default AnimationLoop */
    bool paused;        /* Holds the time; the values are still written once when it changes */
    bool finished;      /* A clip in AnimationOnce reached its end, written by the system */
});
```

An entity with `Animator` plays its clip. Every frame the system `AnimatorPlay` advances `time`
by `delta_time * speed`, wraps it by the mode, samples every track at the new time and writes the
values into the components of the entity or of the targets of the tracks. The constructor sets
`speed` 1 and `AnimationLoop`, so `Animator: {clip: bounce}` in script loops at normal speed. In C
`ecs_set` with a compound literal sets the members it leaves out to 0, a `speed` of 0 included:
add the component and change the members, or give all of them.

```flecs
ball {
  Position: {0, 0, 0}
  Sphere: {}
  Rgb: {255, 0, 0}
  DynamicMaterial
  Animator: {clip: bounce}
}
```

```c
Animator *a = ecs_ensure(world, ball, Animator); /* speed 1, AnimationLoop */
a->clip = bounce;
ecs_modified(world, ball, Animator);
```

The first frame of an animator plays from its `time` (0 unless set) plus one frame delta. The
application reads `time` and `finished` to react at a moment of the clip; there are no events.

### Modes

The duration of a clip is `AnimationClip.duration`, or when that is 0 the last key of its tracks
(`box_animation_clip_duration`). A clip of duration 0 holds its time at 0.

- **`AnimationOnce`** clamps `time` to 0..duration and holds the last value. `finished` becomes
  true at the end the clip plays towards: the duration, or 0 for a negative speed. Setting `time`
  back, or the speed the other way, plays it again and clears `finished`.
- **`AnimationLoop`** wraps `time` into 0..duration, the duration itself becoming 0.
- **`AnimationPingPong`** plays to the end, turns and plays back to 0, and turns again. `time`
  stays the time into the clip; the direction is kept by the system, so a seek keeps it.

A frame delta larger than the clip wraps exactly: a delta of 4.5 s on a loop of 2 s lands on
0.5 s, and on a ping pong of 2 s on 0.5 s on the way out (4 s is one full round).

### Speed and direction

`speed` scales the frame delta: 2 plays twice as fast, 0 holds the time like `paused`, and a
negative speed plays back. An animator whose first frame has a negative speed and a `time` of 0
starts at the end of the clip, so a backwards `AnimationOnce` plays from the end and finishes at 0
and a backwards loop starts at its last value.

### Seeking and pausing

`time` is ordinary data: the application sets it to seek, and the next frame plays from there.

- A playing animator advances from the new time, so setting `time` to 1 before a frame of 0.25 s
  shows the clip at 1.25 s.
- A **paused** animator holds its time and writes nothing per frame: the values of its tracks are
  free for the application or another system to change. Setting its `time` writes the values at
  that time once, so seeking while paused shows the frame, and the frame in which `paused`
  becomes true writes nothing. Changing the clip or one of its tracks also writes once.
- A finished `AnimationOnce` animator, or one with speed 0, writes only when its time changes.

### Prefabs and the time of instances

`Animator` is `(OnInstantiate, Copy)`, flecs' override (named `EcsOverride` in older flecs): each
instance of a prefab owns a copy, so each keeps its own time, speed and pause. The values on the
prefab, from C or from a script, reach the instances when they are created; changing the
prefab's `Animator` later changes no instance. The prefab itself does not play.

```flecs
prefab Walker {
  Animator: {clip: walk}
  legs {
    Position: {0, 0, 0}
  }
}

walker_a : Walker {}
walker_b : Walker {
  Animator: {clip: walk, time: 0.5}   /* half a second ahead */
}
```

The targets of an instance are resolved from the instance, so `walker_b` writes its own `legs`,
never the prefab's.

**A target that inherits the component gets its own copy.** The write is an `ecs_ensure`, so an
instance whose `Rgb` comes from its prefab owns an `Rgb` after the first frame, copied from the
prefab and then animated. From then on it no longer follows the prefab: a later change of the
prefab's `Rgb` does not reach the members the clip does not write either.

### Hierarchy

The `target` of a track is a path relative to the animated entity (`"arm.hand"`), so one clip
moves the parts of a model. The animator resolves the paths of its tracks once, into a private
per entity component that holds, per track, the target entity and its component, and looks up no
path per frame. It resolves them again when:

- `clip` changes,
- a track of any clip, or an `AnimationClip`, is set or removed (a key edit reaches the next
  frame),
- a child is added under the animated entity or one of its descendants, removed, reparented or
  renamed (through `ChildOf`, `Parent` or the name).

A target that does not exist skips its track, with one warning that names the animated entity,
the path and the track; resolving again does not repeat it. A target that appears later, such
as a child reparented under the entity, is written from the next frame on, and one that is
deleted or moved away is skipped. A deleted target never crashes a frame.

**One clip per entity.** An entity plays one clip. To play two on one model, give each its own
`Animator` on a different entity: one on the root and one on the arm, or two children whose
tracks target the parts they move. The clips may write different components, or different
members, of the same entity. When two animators write the same member of the same
component, the later one in the iteration of `AnimatorPlay` wins, which is not an order to rely
on.

### Rules

- **No clip, or an invalid clip.** An `Animator` whose `clip` is 0, deleted or has no
  `AnimationClip` does nothing: its time does not advance and nothing is written. Deleting a clip
  while it plays leaves the values where they were.
- **Disabled.** `AnimatorPlay` is a flecs system, which skips entities with `Disabled` and
  prefabs: a disabled animator holds its time and writes nothing until it is enabled again.
- **Main thread.** The system runs on the game thread in `EcsOnUpdate`, also in the browser where
  the render shares the main thread. There is no render thread playback.
- **No animators, no cost.** Without an `Animator` the system matches no table and does not run:
  a frame touches no clip and no track. The observers that keep the resolved targets current
  return at once while no entity has an animator.
- **Transition.** A `Transition` on a written component, such as `Transition: [Position: {...}]`
  next to a `Position` track, restarts its transition on every write, so the rendered value lags
  behind and never settles while the clip plays. They are not meant to be combined; this is not
  prevented.

### Order in the frame

`AnimatorPlay` runs in `EcsOnUpdate`, after the input and the application's own early phases
(`EcsOnLoad`, `EcsPostLoad`, `EcsPreUpdate`) and before `Velocity` and `AngularVelocity`, which
are declared after it in the same phase. It writes through the stage of the system and declares
a write of any component, so flecs merges its writes, and runs the `OnSet` observers, before the
next system. The static transforms observe those writes and the dynamic ones run every frame, both
in `EcsPreStore`, and the render extract runs after `EcsOnStore`: a value written in a frame is
in `WorldTransform` and on screen in that same frame.

### Scenes at a known frame

A scene that changes at a known frame uses `SetAt` (`include/box/app.h`). A scene that only plays
keyframe animation needs none: with a fixed delta (`--delta-time`, `App.delta_time`) an animator
started at time `t` shows `t + frames * delta` at a known frame. The visual scene
`test/integration/visual/animation_keyframes.flecs` renders its third frame at 1/60 s per frame.

## The cost of a write

Each track writes with `ecs_ensure_id` and `ecs_modified_id`, so the value reacts as any write
does: `OnSet` observers run, the material of a shape gets its new color, change detection and
queries see the change and the transforms move. That is also its cost, one `ecs_set` per track
per frame plus what the observers of the component do. Some writes cost more than others:

- **`Rgb`, `PbrMaterial`, `Emissive` and the other material components.** A material write on a
  shape without `DynamicMaterial` resolves its shared material again in the next extract, which
  hashes the material (`src/render/materials.c`): a shape animating its color every frame hashes
  every frame. A shape that animates a material component wants `DynamicMaterial`, which keeps
  its own material slot and skips the hash.
- **Transforms.** A static entity whose `Position`, `Rotation` or `Scale` is written is queued and
  recomputed with its descendants. Many animated transforms are cheaper as `DynamicTransform`.
- **A first write to an inherited or missing component** adds it to the entity, a table move.
  Later writes do not.

The `bench/core` case `Animator` measures a frame of 1000 and 10000 animators with one
`Position.y` track each, reported per animator.

## Out of scope

Blending between clips, cross fades, layers, events at a time of the clip (read `time` and
`finished`), a shell command to control playback, a step or hold curve kind (two keys close
together make a near jump), skeletons, skinning, morph targets, model import and render thread
playback.

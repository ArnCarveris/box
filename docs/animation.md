# Animation clips

`box.animation` holds keyframe animations as data. A clip is an entity with `AnimationClip`, and
its tracks are its children with `AnimationTrack`. Each track keys one numeric member of one
component with a `Curve` of `box.curve` (see `include/box/curve.h` for the kinds, the key format
and the evaluator). This page covers clips and sampling; playing a clip is added by the
AnimationPlayback task.

Unlike a `Transition`, which only moves the rendered value, a clip writes the real component on
the game thread: the application reads the animated value, and any reflected component can be
animated without a render path of its own — `Position`, `Rgb`, `PointLight`, `Camera`, effect
settings or a component of the application. A write costs what an `ecs_set` costs.

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

## Out of scope

Playing a clip (AnimationPlayback), blending between clips, a step or hold curve kind (two keys
close together make a near jump), skeletons, skinning, morph targets and model import.

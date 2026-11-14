# Curves

`box.curve` defines scalar keyed curves for particles, lights, emission and
[keyframe animation](animation.md). Geometry paths and transitions share cubic
math but use different formats. This guide covers authoring and practical limits;
[curve.h](../include/box/curve.h) defines the evaluator API.

Working recipes are in [test/assets/curves](../test/assets/curves/), checked by
`CurveRecipes` in `test/cpu/particle`.

## The format

```flecs
using box.curve
using box.particle
using box.transform

smoke {
  Position: {0, 0, 0}
  Emitter: {rate: 20, lifetime: {2, 3}, size: {0.5, 0.5}, seed: 1}
  Curves: {
    opacity: {keys: [{0, 0}, {0.1, 1}, {1, 0}]}
    size: {kind: CurveCatmullRom, keys: [{0, 0.3}, {0.4, 1.8}, {1, 2.2}]}
    spin: {kind: CurveBezier, keys: [{time: 0, value: 1, out: -2}, {time: 1, value: 0, in: 0}]}
  }
}
```

A `Curve` is `{kind, keys}` (`include/box/curve.h`):

| Field  | Type              | Default       | Meaning                                         |
|--------|-------------------|---------------|-------------------------------------------------|
| `kind` | `box.curve.Kind`       | `CurveLinear` | Shape of every span of the curve                |
| `keys` | `ecs_vec(CurveKey)` | none        | Keys in order of strictly increasing time       |

A `box.curve.Key` is `{time, value, in, out}`, positionally `{0.5, 2}` or by name
`{time: 0.5, value: 2, out: -3}`:

| Field   | Default | Meaning                                                        |
|---------|---------|----------------------------------------------------------------|
| `time`  | 0       | Time of the key, strictly after the time of the previous key   |
| `value` | 0       | Value at the time                                              |
| `in`    | 0       | Slope into the key in value per unit of time, `CurveBezier` only |
| `out`   | 0       | Slope out of the key in value per unit of time, `CurveBezier` only |

- `box.curve.Kind` is `CurveLinear` (0), `CurveCatmullRom` or `CurveBezier`. The kind is per curve,
  not per key. Write the full names: `Linear` is a `TimingFunction` constant and `CatmullRom` a
  `PathSegmentKind` constant, and either in a curve is a script error, `unresolved reference
  'Linear'`, that stops the script.
- No keys means the default of the field that holds the curve, so `Curves: {}` changes
  nothing. One key is a constant. Before the first key and after the last the curve holds the
  value of that key.
- Times and values are finite and times increase strictly. Each field defines the range of its
  times: 0 to 1 over the life of a particle or over a period, seconds for an animation track.
- Curves are stored as authored: a bad curve keeps its keys in the component, logs an error and
  acts as the default of its field until it is fixed.

A particle curve, `ParticleCurve`, adds `max_keys` to `kind` and `keys`: a second list of keys
of the same kind. Each particle draws a number `u` from 0 to 1 for the field once and follows
`mix(keys(t), max_keys(t), u)` for its whole life, a random curve between the two, like Unity's
"random between two curves". Without `keys` the first curve is the default of the field.

A color over the life, `ParticleGradient`, is `{kind, keys, max_keys}` whose keys are
`{time, color}`, with display encoded colors `{255, 128, 0}`. Each channel is a curve of the
kind, blended in linear light; a channel above 255 is a script error. Without keys it is white.

## Fields that take a curve

| Component | Field | Effect | Default | Times | Evaluated | `max_keys` |
|-----------|-------|--------|---------|-------|-----------|------------|
| `ParticleCurves` | `size` | Multiplies the size a particle spawned with | 1 | life, 0 to 1 | GPU, per particle, draw | yes |
| `ParticleCurves` | `opacity` | Multiplies the opacity | 1 | life | GPU, draw | yes |
| `ParticleCurves` | `rotation` | Adds turns to the rotation, 1 = 360 degrees | 0 | life | GPU, draw | yes |
| `ParticleCurves` | `color` | `ParticleGradient`, multiplies the color in linear light | white | life | GPU, draw | yes |
| `ParticleCurves` | `spin` | Multiplies the angular velocity; the turned angle is the area under it | 1 | life | GPU, draw | yes |
| `ParticleMotion` | `velocity_x`, `_y`, `_z` | Adds world units per second to the movement, not to the stored velocity | 0 | life | GPU, integrate | yes, one draw for the three axes |
| `ParticleMotion` | `force_x`, `_y`, `_z` | Adds world units per second squared to the acceleration | 0 | life | GPU, integrate | yes, one draw for the three axes |
| `ParticleMotion` | `speed` | Multiplies the stored velocity in the movement of a frame | 1 | life | GPU, integrate | yes |
| `ParticleMotion` | `drag` | Multiplies the drag of the `ParticleForces` | 1 | life | GPU, integrate | yes |
| `ParticleLight` | `curve` | Multiplies `intensity` | 1 | 0 to 1 of `period` of the playback clock | CPU, exact | error |
| `ParticleTimeline` | `rate` | Multiplies `ParticleEmitter.rate` | 1 | 0 to 1 of `period` | CPU, exact | error |
| `ParticleTimeline` | `speed` | Multiplies the start speed of the particles spawned now | 1 | 0 to 1 of `period` | CPU, exact | error |
| `Track` | `curve` | Is the value of the member, a `Curve` without `max_keys` | none, a track needs keys | seconds of the clip | CPU, exact | - |

"Life" is the normalized age of a particle, 0 at birth and 1 at death. The GPU curves are baked
to 64 samples; the CPU curves are evaluated exactly at their time. `ParticleCurves` sits on the
emitter entity, `ParticleMotion` and `ParticleTimeline` too; `ParticleLight` needs a
`PointLight` or `SpotLight` on the emitter entity. See [Animation](animation.md) for tracks.

Two neighbours use the same cubic math without a `Curve`:

- `Path2D` and `Path3D` segments of kind `CatmullRom` (`include/box/geometry.h`) are uniform
  Catmull-Rom curves through their anchors, for profiles, lathes and sweeps.
- `Spec` with `timing_function: CubicBezier` and `bezier: {x1, y1, x2, y2}` is a CSS
  `cubic-bezier()` ease of a transition (see [Transitions](transitions.md)).

## Choosing a kind

Every kind turns each span between two keys into one cubic whose time is linear in the curve
parameter, so they differ only in the two control values of each span:

- **`CurveLinear`**: straight lines between the keys, with corners at the keys. It never leaves
  the range of its keys. Use it for ramps, holds (two keys of the same value) and anything that
  must hit exact values, and for gradients unless smooth color matters.
- **`CurveCatmullRom`**: a smooth curve through every key without slopes to write. The slope at
  a key is `(v[k + 1] - v[k - 1]) / (t[k + 1] - t[k - 1])`, using the real time spacing so uneven
  keys work. At the first and last key the missing neighbour has the value of the key and lies one
  span further, so the end slope is half that of the end span; a looping light or timeline curve
  takes its end neighbours across the wrap instead (see
  [Flicker on a light](#flicker-on-a-light)). Use it for organic shapes: growth, flicker, a smooth
  path through points.
- **`CurveBezier`**: you write the slopes. The controls of a span from key a to key b are
  `a.value + a.out * dt / 3` and `b.value - b.in * dt / 3`, a third of the span `dt` along. Use it
  for eases and for shapes the others cannot make, such as a fast start that settles.

### Overshoot and clamps

Catmull-Rom and Bezier curves can pass beyond their keys: Catmull-Rom around a key that is a peak
or a dip between neighbours of different heights, and where keys are unevenly spaced, Bezier
wherever a slope is steep or points the other way. Where values clamp:

- `size`, `opacity` and each channel of `color` of `ParticleCurves` clamp at 0 after they
  multiply, so an overshoot below 0 hides a particle for a moment. There is no upper clamp: an
  overshoot above the keys makes a particle larger or brighter than they say.
- `rotation` and `spin` are not clamped and may go negative: a negative spin turns back.
- `ParticleMotion.drag` below 0 is no drag. The velocity, force and speed curves are not clamped.
- A negative `ParticleTimeline.rate` counts as 0. `speed` is not clamped; below 0 it reverses
  the start velocity.
- `ParticleLight.curve` is not clamped.
- An `Track` of an integer member rounds and clamps to its type, so an `Rgb` channel stays
  in 0..255; a float member takes the overshoot.

To keep a curve within its keys, use `CurveLinear`, or give a Catmull-Rom peak neighbours of
equal value, which makes its slope 0, or keep both Bezier slopes of a span between 0 and 3 times
its straight-line slope, `3 * (b.value - a.value) / dt`: its controls then lie between its keys.

### Slopes for `CurveBezier`

Slopes are in value per unit of time, and for particles the unit of time is the whole life. A
span from 1 to 0 over the whole life has a straight-line slope of -1:

- `out: -1, in: -1` is the straight line, the same as `CurveLinear`.
- `out: 0, in: 0` eases out of the first key and into the second, a smoothstep.
- `out: -3, in: 0` is `(1 - t)^3`: about half left at t = 0.2, an eighth at t = 0.5. -3 is the
  steepest start that does not overshoot below 0; -6 would dip below 0 and clamp.
- A slope with the sign opposite to the span makes the curve go the wrong way first.

`in` of the first key and `out` of the last key are unused. Write the keys by name,
`{time: 0, value: 1, out: -3}`, so it is clear which slope is which.

### Why a sharp change softens

GPU curves are baked to 64 samples from 0 to 1, 63 intervals, and read linearly between two
samples. A change faster than an interval, such as two keys 0.001 apart, becomes a ramp over
1/63 of the life, about 1.6%. Keys at 1/3 and 2/3 fall exactly on samples. There is no step
kind: two keys close together are the nearest thing. The CPU curves of lights, timelines and
tracks are exact and keep a sharp change sharp.

## Recipes

These examples show growth, explicit slopes, looping clocks and per-particle
randomness. Keep emitter color white or set it to the desired base: gradients
multiply it.

### Puff that grows and fades

Soft grey balls that swell quickly to about twice their size, keep growing slowly as they drift
up, and thin out to nothing.

```flecs
using box.curve
using box.particle
using box.transform

puff {
  Position: {0, 0, 0}
  Emitter: {
    max: 200, rate: 25, shape: EmitterSphere, radius: 0.1, spread: 25,
    speed: {0.4, 0.8}, lifetime: {1.5, 2.5}, size: {0.4, 0.6},
    opacity: {0.6, 0.8}, color: {200, 200, 205}, seed: 2
  }
  Forces: {gravity: {0, 0.3, 0}, drag: 0.8}
  Curves: {
    size: {kind: CurveCatmullRom, keys: [{0, 0.3}, {0.3, 1.8}, {1, 2.4}]}
    opacity: {keys: [{0, 0}, {0.1, 1}, {1, 0}]}
  }
}
```

(`test/assets/curves/puff.flecs`)

### Spark that shrinks quickly

Bright additive streaks that lose half their size in the first fifth of their life, the
`(1 - t)^3` of `out: -3`, and then glow out as small points while they fall.

```flecs
using box.curve
using box.particle
using box.transform

spark {
  Position: {0, 0, 0}
  Emitter: {
    max: 400, rate: 120, spread: 35, speed: {4, 7}, lifetime: {0.4, 0.8},
    size: {0.06, 0.1}, color: {255, 200, 120}, seed: 3
  }
  Forces: {drag: 1.5}
  Appearance: {blend: Additive, stretch: 0.03}
  Curves: {
    size: {kind: CurveBezier, keys: [{time: 0, value: 1, out: -3}, {time: 1, value: 0, in: 0}]}
    opacity: {keys: [{0, 1}, {0.7, 1}, {1, 0}]}
  }
}
```

(`test/assets/curves/spark.flecs`)

### Flicker on a light

The point light of a small fire wavers irregularly between 70% and 120% of its intensity,
repeating every 0.7 s, and fades out over 0.3 s once the emitter stops.

```flecs
using box.curve
using box.light
using box.material
using box.particle
using box.transform

campfire {
  Position: {0, 0, 0}
  Rgb: {255, 150, 60}
  PointLight: {intensity: 3, range: 6}
  Emitter: {
    rate: 60, shape: EmitterDisc, radius: 0.2, speed: {0.5, 1}, lifetime: {0.6, 0.9},
    size: {0.15, 0.25}, color: {255, 160, 60}, seed: 7
  }
  Appearance: {blend: Additive}
  box.particle.Light: {
    intensity: 3, period: 0.7, loop: true, fade: 0.3
    curve: {kind: CurveCatmullRom, keys: [
      {0, 1}, {0.12, 0.75}, {0.25, 1.1}, {0.4, 0.85}, {0.55, 1.2}, {0.7, 0.7}, {0.85, 1.05}, {1, 1}
    ]}
  }
}
```

(`test/assets/curves/flicker.flecs`)

A looping curve should end at the value it starts with, or the light jumps at every wrap. Uneven
key times keep the flicker from looking regular. The `Rgb` of the entity colors the light.

A looping `CurveCatmullRom` joins smoothly at the wrap. When `ParticleLight.loop` or
`ParticleTimeline.loop` is on, its keys form a cycle: the neighbour before the first key is the
second to last key one period earlier, and the neighbour after the last key is the second key one
period later. The slope out of the last key is then the slope into the first, so plain keys give a
smooth pulse without slopes to work out:

```flecs
curve: {kind: CurveCatmullRom, keys: [{0, 0.4}, {0.3, 1.3}, {0.6, 0.8}, {1, 0.4}]}
```

This holds only when the first key is at 0 and the last at 1. Keys that start after 0 or end
before 1 hold their end values across the wrap, and their end slopes keep the mirrored neighbour
of a curve that does not loop. A loop whose last value differs from its first still jumps at the
wrap, so end on the starting value. `CurveLinear` and `CurveBezier` are the
same with or without `loop`; for a Bezier loop give the first key an `out` equal to the `in` of the
last. Curves of `ParticleCurves`, animation tracks and all other fields have no loop and keep the
end neighbours of [Choosing a kind](#choosing-a-kind).

### Random size curves

Every particle follows its own curve between two, so some shrink steadily while others swell to
twice their size halfway and then collapse.

```flecs
using box.curve
using box.particle
using box.transform

bubbles {
  Position: {0, 0, 0}
  Emitter: {
    rate: 40, spread: 30, speed: {0.5, 1}, lifetime: {2, 2}, size: {0.2, 0.2}, seed: 8
  }
  Forces: {gravity: {0, 0.2, 0}}
  Curves: {
    size: {
      kind: CurveCatmullRom
      keys: [{0, 1}, {1, 0.4}]
      max_keys: [{0, 1}, {0.5, 2}, {1, 0.2}]
    }
  }
}
```

(`test/assets/curves/random_size.flecs`)

The two lists need not have the same number of keys or the same times. The blend is the same
for the whole life of a particle, so a particle never jumps between the two.

### More examples

| Recipe | File | Main idea |
| --- | --- | --- |
| Fade in/out | [fade.flecs](../test/assets/curves/fade.flecs) | Linear opacity with equal-valued keys for a hold |
| Flame and smoke | [flame.flecs](../test/assets/curves/flame.flecs) | Color gradient and growth; alpha blending lets smoke darken the background |
| Burst that stops and rises | [burst.flecs](../test/assets/curves/burst.flecs) | Drag multiplies `ParticleForces.drag`; velocity curves add movement without changing stored velocity |
| Pulsing emission | [pulse.flecs](../test/assets/curves/pulse.flecs) | Rate and spawn speed over the emitter clock |
| Lathe and sweep | [path.flecs](../test/assets/curves/path.flecs) | Catmull-Rom geometry paths |
| Custom UI ease | [ease.flecs](../test/assets/curves/ease.flecs) | Transition `CubicBezier` handles |

Path segments use `CatmullRom`, not `CurveCatmullRom`. Tangents use neighboring
anchors regardless of segment kind. Closed Catmull-Rom paths join across the wrap;
profiles sample curved segments in 16 steps, or in the `segments` of the Sweep or
Loft. Sweep UVs follow arc length. On a
horizontal path, profile y points down (-Y), so road profiles put their top at
negative y.

## Showcase

Four whole effects in `test/assets/curves/showcase/`, each built of several emitters under one
root entity, and shown together by `test/integration/visual/curves_showcase.flecs`:

| File | Effect | What the curves do that constants cannot |
|------|--------|------------------------------------------|
| `campfire.flecs` | Flame, smoke, embers, light | Embers pop out and slow down (`ParticleMotion.speed`), keep rising (`velocity_y`), wander and twinkle each their own way (`max_keys` mirrored or out of phase); the light flickers |
| `shockwave.flecs` | Flash, ring front, dust ring, debris | Rings that race out and stop dead (`speed`, `(1 - t)^3`), a light flash in step with the burst, debris that stops turning as it settles (`spin`) |
| `orb.flecs` | Glowing ball, halo, motes | Motes that stream along a double helix from Bezier velocity curves; the light and the halo emission pulse together |
| `chimney.flecs` | Stack and puffing smoke | Two puffs a period of different strength (`ParticleTimeline.rate`), faster starts (`speed`), smoke that shoots up before the wind takes it (`ParticleMotion.drag`) |

Common authoring traps:

- Spin is invisible on the default circle. Use a sprite or billboard mesh.
- Without `ParticleForces`, particles feel no gravity. `Forces: {}`
  supplies the default -9.81 gravity.
- Velocity-curve times use normalized life, but values are units per second.
  Use a fixed lifetime to make every particle trace the same shape.
- `color_variance` varies channels separately. For brightness variation without
  hue changes, blend constant white and grey gradients with `max_keys`.
- Ground collision stops the particle center. Raise collision height by half
  the drawn size to keep a resting billboard above the ground.
- A burst at one period meets a looping light's flash, but that light also
  flashes at time zero before the first burst.

## Emitter clock

`ParticleTimeline.period` is seconds of `ParticlePlayback.time`. Curve times are
0..1 over that period. Loop repeats; otherwise curves hold their final values.
A frame spawns the base rate times the area of the nonnegative rate curve over
that frame, including wraps, so counts do not depend on frame rate. Speed is
sampled at the frame's ending clock and affects only particles spawned that frame.
Bursts are not scaled. Pause holds the clock, stop emits nothing, reset restarts it.
Lights and timelines reject `max_keys` because they have no per-particle draw.

## Checking the result

A bad curve logs an error identifying the component, entity, field, list and key.
It retains its authored keys but uses the field default until fixed. Check for:

- Nonfinite times, values or slopes, duplicate/decreasing times, or particle
  times outside 0..1.
- A light or timeline with keys but a nonpositive period.
- `max_keys` on a light or timeline.
- Short kind names such as `Linear`, a gradient channel above 255, a scalar
  where a color is required, or a misspelled member.

Script errors stop evaluation; fix the first error and rerun to expose later
ones. `in` and `out` on non-Bezier curves are valid but ignored. A valid curve
whose baked samples overflow also falls back to the field default.
See [mistakes](../test/assets/curves/mistakes/) for failing examples.

CPU tests cannot inspect shader output. To render a recipe, put it in a scene
with a camera and view, for example `/tmp/fx/puff.flecs`:

```flecs
include test/assets/curves/puff

using box.transform
using box.camera
using box.view

camera {
  Position: {0, 1, 6}
  Camera: {fov: 60}
}
main_view {
  View: {camera: camera, clear: {enabled: true, color: {16, 18, 28, 255}}}
}
```

```sh
python3 tools/integration.py visual --dir /tmp/fx --out /tmp/fx/out --frames 60 puff
```

The harness uses a fixed 1/60-second delta, writes `puff.png` and `index.html`,
and reports a missing baseline even though it produced the image. Compare several
frame counts to inspect birth, growth and steady state. Run the scene live with
`bake3 run apps/boxrun --local-env -- /tmp/fx/puff.flecs`.
`ParticleStats` counts live and spawned particles when checking rate curves.

## Limits and cost

There is no step kind, per-key kind, curve editor or free time-axis Bezier handle.
For a hold, repeat a value; for a near jump, use close keys, subject to GPU sampling.
ParticleTimeline only changes rate and start speed: other spawn ranges are redrawn
from the seed for live particles, so changing them would affect existing particles.
Per-axis size, trails and sub-emitters are not supported by these curves.

GPU curves bake to 64 samples on change. Rows are shared by baked content, and
flat samples become constants that need no table read. `max_keys` needs rows for
both curves unless both are the same constant. Each keyed field normally reads
two samples per particle per frame; spin also reads its integral, color reads
four-channel samples, and `max_keys` doubles the reads. Motion reads in integration;
appearance reads in drawing. CPU light, timeline and track curves evaluate exactly
and use no GPU row.

A keyed color curve can cost more through sorting than through sampling:
alpha-blended particles without `ParticleAppearance` normally draw unsorted, but
keyed color or nonzero color variance enables back-to-front sorting. With
`ParticleAppearance`, alpha particles always sort and additive particles never do.
Bezier gradients have no authored slopes and ease at every key; colors blend in
linear light both along the gradient and between its two random bounds.

Run `bake3 run --cfg release bench/render --local-env -- Particle` for extraction
and drawing costs. Historical CPU measurements used a one-core arm64 Linux VM;
draw measurements used software Vulkan. Measure on the target GPU before relying
on absolute timings.

# Curves

`box.curve` is one keyed curve, linear, Catmull-Rom or Bezier, that particles, lights, emission,
keyframe animation and, through the same cubic math, geometry paths and transitions use. This
guide is for writing curves in `.flecs` scripts without a curve editor: the format, every field
that takes a curve, how each kind behaves, recipes, how to check the result, what is not
supported and what it costs.

The recipes are files in `test/assets/curves/`, loaded by the `CurveRecipes` suite of
`test/cpu/particle`, which fails when one of them logs a warning or an error. Copy them. Whole
effects built from them are in [Showcase](#showcase).

## The format

```flecs
using box.curve
using box.particle

smoke {
  ParticleEmitter: {rate: 20, lifetime: {2, 3}, size: {0.5, 0.5}, seed: 1}
  ParticleCurves: {
    opacity: {keys: [{0, 0}, {0.1, 1}, {1, 0}]}
    size: {kind: CurveCatmullRom, keys: [{0, 0.3}, {0.4, 1.8}, {1, 2.2}]}
    spin: {kind: CurveBezier, keys: [{time: 0, value: 1, out: -2}, {time: 1, value: 0, in: 0}]}
  }
}
```

(`test/assets/curves/format.flecs`)

A `Curve` is `{kind, keys}` (`include/box/curve.h`):

| Field  | Type              | Default       | Meaning                                         |
|--------|-------------------|---------------|-------------------------------------------------|
| `kind` | `CurveKind`       | `CurveLinear` | Shape of every span of the curve                |
| `keys` | `ecs_vec(CurveKey)` | none        | Keys in order of strictly increasing time       |

A `CurveKey` is `{time, value, in, out}`, positionally `{0.5, 2}` or by name
`{time: 0.5, value: 2, out: -3}`:

| Field   | Default | Meaning                                                        |
|---------|---------|----------------------------------------------------------------|
| `time`  | 0       | Time of the key, strictly after the time of the previous key   |
| `value` | 0       | Value at the time                                              |
| `in`    | 0       | Slope into the key in value per unit of time, `CurveBezier` only |
| `out`   | 0       | Slope out of the key in value per unit of time, `CurveBezier` only |

- `CurveKind` is `CurveLinear` (0), `CurveCatmullRom` or `CurveBezier`. The kind is per curve,
  not per key. Write the full names: `Linear` is a `TimingFunction` constant and `CatmullRom` a
  `PathSegmentKind` constant, and either in a curve is a script error, `unresolved reference
  'Linear'`, that stops the script.
- No keys means the default of the field that holds the curve, so `ParticleCurves: {}` changes
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
| `AnimationTrack` | `curve` | Is the value of the member, a `Curve` without `max_keys` | none, a track needs keys | seconds of the clip | CPU, exact | - |

"Life" is the normalized age of a particle, 0 at birth and 1 at death. The GPU curves are baked
to 64 samples; the CPU curves are evaluated exactly at their time. `ParticleCurves` sits on the
emitter entity, `ParticleMotion` and `ParticleTimeline` too; `ParticleLight` needs a
`PointLight` or `SpotLight` on the emitter entity. See `docs/animation.md` for tracks.

Two neighbours use the same cubic math without a `Curve`:

- `Path2D` and `Path3D` segments of kind `CatmullRom` (`include/box/geometry.h`) are uniform
  Catmull-Rom curves through their anchors, for profiles, lathes and sweeps.
- `TransitionSpec` with `timing_function: CubicBezier` and `bezier: {x1, y1, x2, y2}` is a CSS
  `cubic-bezier()` ease of a transition (`docs/transitions.md`).

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
- An `AnimationTrack` of an integer member rounds and clamps to its type, so an `Rgb` channel stays
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

Each recipe is a whole file. The particle recipes leave `ParticleEmitter.color` white or set it
to the base color, since `color` multiplies it.

### Fade in and out

Each particle appears over the first fifth of its life, stays and fades out over the last half,
so the stream has no hard edge where particles are born or die.

```flecs
using box.curve
using box.particle

fade {
  ParticleEmitter: {
    rate: 30, spread: 20, speed: {0.5, 1}, lifetime: {2, 2.5}, size: {0.2, 0.2}, seed: 1
  }
  ParticleForces: {gravity: {0, 0, 0}}
  ParticleCurves: {
    opacity: {keys: [{0, 0}, {0.2, 1}, {0.5, 1}, {1, 0}]}
  }
}
```

(`test/assets/curves/fade.flecs`)

### Puff that grows and fades

Soft grey balls that swell quickly to about twice their size, keep growing slowly as they drift
up, and thin out to nothing.

```flecs
using box.curve
using box.particle

puff {
  ParticleEmitter: {
    max: 200, rate: 25, shape: EmitterSphere, radius: 0.1, spread: 25,
    speed: {0.4, 0.8}, lifetime: {1.5, 2.5}, size: {0.4, 0.6},
    opacity: {0.6, 0.8}, color: {200, 200, 205}, seed: 2
  }
  ParticleForces: {gravity: {0, 0.3, 0}, drag: 0.8}
  ParticleCurves: {
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

spark {
  ParticleEmitter: {
    max: 400, rate: 120, spread: 35, speed: {4, 7}, lifetime: {0.4, 0.8},
    size: {0.06, 0.1}, color: {255, 200, 120}, seed: 3
  }
  ParticleForces: {drag: 1.5}
  ParticleAppearance: {blend: ParticleAdditive, stretch: 0.03}
  ParticleCurves: {
    size: {kind: CurveBezier, keys: [{time: 0, value: 1, out: -3}, {time: 1, value: 0, in: 0}]}
    opacity: {keys: [{0, 1}, {0.7, 1}, {1, 0}]}
  }
}
```

(`test/assets/curves/spark.flecs`)

### Flame from white through orange to dark smoke

Particles are born white hot, turn yellow and orange as they rise, darken to red and end as grey
smoke that grows and fades. They are alpha blended, the default without `ParticleAppearance`,
because additive particles can only brighten and smoke must darken what is behind it.

```flecs
using box.curve
using box.particle

flame {
  ParticleEmitter: {
    max: 600, rate: 120, shape: EmitterDisc, radius: 0.15, spread: 10,
    speed: {0.8, 1.2}, lifetime: {1.2, 1.8}, size: {0.25, 0.35}, seed: 4
  }
  ParticleForces: {gravity: {0, 0.6, 0}, turbulence: 1.5}
  ParticleCurves: {
    color: {keys: [
      {0, {255, 255, 230}}, {0.15, {255, 210, 90}}, {0.35, {250, 120, 30}},
      {0.55, {120, 40, 20}}, {0.7, {60, 55, 55}}, {1, {45, 45, 45}}
    ]}
    size: {kind: CurveCatmullRom, keys: [{0, 0.6}, {0.4, 1}, {1, 2.5}]}
    opacity: {keys: [{0, 0}, {0.05, 1}, {0.6, 0.8}, {1, 0}]}
  }
}
```

(`test/assets/curves/flame.flecs`)

For a brighter fire, add a second emitter with `ParticleAppearance: {blend: ParticleAdditive}`
for the hot part only, and keep this one for the smoke.

### Burst that slows down and rises

Every 1.5 s a ball of particles flies out, stops dead at about a third of its life when the drag
curve comes on, then floats up as the velocity curve lifts it. Constant forces cannot do this.

```flecs
using box.curve
using box.particle

burst {
  ParticleEmitter: {
    max: 400, rate: 0, shape: EmitterSphere, radius: 0.05, spread: 180,
    speed: {4, 6}, lifetime: {1.4, 1.4}, size: {0.08, 0.12}, color: {255, 190, 90}, seed: 5
  }
  ParticleBurst: {count: 150, at: 1.5, repeat: -1}
  ParticleForces: {gravity: {0, 0, 0}, drag: 12}
  ParticleAppearance: {blend: ParticleAdditive}
  ParticleMotion: {
    drag: {keys: [{0, 0}, {0.25, 0}, {0.35, 1}]}
    velocity_y: {keys: [{0, 0}, {0.4, 0}, {0.7, 1.5}]}
  }
  ParticleCurves: {
    opacity: {keys: [{0, 1}, {0.7, 1}, {1, 0}]}
  }
}
```

(`test/assets/curves/burst.flecs`)

The drag curve multiplies the drag of 12 of `ParticleForces`. The velocity curve moves the
particles without entering their velocity, so the drag does not fight it.

### Pulsing emission

The emitter breathes once a second, from nothing to 200 particles a second and back, and the
particles of the peak leave twice as fast as those of the quiet part, so each pulse is a shell.

```flecs
using box.curve
using box.particle

pulse {
  ParticleEmitter: {
    max: 1000, rate: 200, spread: 180, speed: {1, 1}, lifetime: {1, 1},
    size: {0.05, 0.05}, color: {120, 220, 255}, seed: 6
  }
  ParticleForces: {gravity: {0, 0, 0}}
  ParticleTimeline: {
    period: 1, loop: true,
    rate: {kind: CurveCatmullRom, keys: [{0, 0}, {0.5, 1}, {1, 0}]}
    speed: {kind: CurveCatmullRom, keys: [{0, 0.5}, {0.5, 1}, {1, 0.5}]}
  }
}
```

(`test/assets/curves/pulse.flecs`)

### Flicker on a light

The point light of a small fire wavers irregularly between 70% and 120% of its intensity,
repeating every 0.7 s, and fades out over 0.3 s once the emitter stops.

```flecs
using box.curve
using box.light
using box.material
using box.particle

campfire {
  Rgb: {255, 150, 60}
  PointLight: {intensity: 3, range: 6}
  ParticleEmitter: {
    rate: 60, shape: EmitterDisc, radius: 0.2, speed: {0.5, 1}, lifetime: {0.6, 0.9},
    size: {0.15, 0.25}, color: {255, 160, 60}, seed: 7
  }
  ParticleAppearance: {blend: ParticleAdditive}
  ParticleLight: {
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
end neighbours of [Choosing a kind](#choosing-a-kind). This refines the end rule of the Curves epic
design ("at the first and last key the neighbour is the key itself") for the looping clock curves
of `ParticleLight` and `ParticleTimeline` only (`box_curve_loop_span_controls`,
`include/box/curve.h`).

### Random size curves

Every particle follows its own curve between two, so some shrink steadily while others swell to
twice their size halfway and then collapse.

```flecs
using box.curve
using box.particle

bubbles {
  ParticleEmitter: {
    rate: 40, spread: 30, speed: {0.5, 1}, lifetime: {2, 2}, size: {0.2, 0.2}, seed: 8
  }
  ParticleForces: {gravity: {0, 0.2, 0}}
  ParticleCurves: {
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

### Catmull-Rom paths for geometry

A vase lathed from a profile that starts with a line, so its foot is flat, and runs through four
anchors with a smooth belly and lip; and a tube swept along a smooth route, with a round section
made from the four anchors of a closed path.

```flecs
using box.transform
using box.geometry
using box.material

vase_outer {
  Path2D: {
    start: {0, 0},
    segments: [
      {kind: Line, end: {0.4, 0}},
      {kind: CatmullRom, end: {0.75, 0.6}},
      {kind: CatmullRom, end: {0.35, 1.4}},
      {kind: CatmullRom, end: {0.3, 1.8}},
      {kind: CatmullRom, end: {0.5, 2.1}}
    ],
    closed: false
  }
}

vase {
  Position: {2, 0, 0}
  Lathe: {profile: vase_outer, cap_start: true, cap_end: true}
  Rgb: {70, 165, 205}
}

tube_profile {
  Path2D: {
    start: {0.25, 0},
    segments: [
      {kind: CatmullRom, end: {0, 0.25}},
      {kind: CatmullRom, end: {-0.25, 0}},
      {kind: CatmullRom, end: {0, -0.25}}
    ],
    closed: true
  }
}

tube_section { Profile2D: {outer: tube_profile} }

tube_route {
  Path3D: {
    start: {-1.5, 0.3, 0},
    segments: [
      {kind: CatmullRom, end: {-0.5, 2, 0.5}},
      {kind: CatmullRom, end: {0.5, 0.6, -0.5}},
      {kind: CatmullRom, end: {1.5, 2.2, 0}}
    ]
  }
}

tube {
  Position: {-2, 0, 0}
  Sweep: {path: tube_route, profile: tube_section, scale: {1, 1}, rings: 96}
  Rgb: {230, 155, 65}
}
```

(`test/assets/curves/path.flecs`)

Path segments are `PathSegmentKind`, so here the kind is `CatmullRom`, not `CurveCatmullRom`.
A segment takes its tangents from the anchors of the segments next to it, whatever their kind,
and a closed path whose last segment is Catmull-Rom closes with a Catmull-Rom segment, so four
anchors make a round section. Profiles sample a curved segment in 16 steps.

A Sweep maps its texture by arc length: u runs around the profile and v along the path by
distance, so the anchors need not be evenly spaced for an even texture. With
`TextureMapping: {mode: MappingObject, size: 2}` the texture repeats every 2 units along and
across the swept surface whatever the length of the path. Along a horizontal path the profile y
points down (-Y), so a road profile puts its top at negative y.

### A custom ease for UI

A panel that springs to a new scale with a small overshoot, a CSS "back" ease, and fades with a
fast start and a long soft landing.

```flecs
using box.transform
using box.geometry
using box.material
using box.transition

panel {
  Scale: {1, 1, 1}
  Rect: {240, 120}
  Rgb: {40, 44, 56}
  Opacity: {1}
  Transition: [
    Scale: {duration: 0.25, timing_function: CubicBezier, bezier: {0.34, 1.56, 0.64, 1}},
    Opacity: {duration: 0.3, timing_function: CubicBezier, bezier: {0.2, 0, 0, 1}}
  ]
}
```

(`test/assets/curves/ease.flecs`)

`x1` and `x2` must lie in 0..1; `y1` and `y2` are free, and above 1 the value overshoots its
target before it settles. Only a new value animates, so the panel moves when a script, the
shell or C sets its `Scale` or `Opacity` later. `docs/transitions.md` has the details.

## Showcase

Four whole effects in `test/assets/curves/showcase/`, each built of several emitters under one
root entity, and shown together by `test/integration/visual/curves_showcase.flecs`:

| File | Effect | What the curves do that constants cannot |
|------|--------|------------------------------------------|
| `campfire.flecs` | Flame, smoke, embers, light | Embers pop out and slow down (`ParticleMotion.speed`), keep rising (`velocity_y`), wander and twinkle each their own way (`max_keys` mirrored or out of phase); the light flickers |
| `shockwave.flecs` | Flash, ring front, dust ring, debris | Rings that race out and stop dead (`speed`, `(1 - t)^3`), a light flash in step with the burst, debris that stops turning as it settles (`spin`) |
| `orb.flecs` | Glowing ball, halo, motes | Motes that stream along a double helix from Bezier velocity curves; the light and the halo emission pulse together |
| `chimney.flecs` | Stack and puffing smoke | Two puffs a period of different strength (`ParticleTimeline.rate`), faster starts (`speed`), smoke that shoots up before the wind takes it (`ParticleMotion.drag`) |

What they needed beyond the recipes:

- **A spin you can see.** `rotation` and `spin` turn a particle around the view axis, which a
  `ParticleCircle`, the default shape, does not show. Draw the particles as a
  `ParticleSprite` with a texture, or as a `ParticleBillboard` of the mesh of another entity:
  `ParticleAppearance: {shape: ParticleBillboard, mesh: chunk, lit: 0.8}`. The mesh entity is
  drawn itself, so put it out of view, `chunk { Position: {0, -1000, 0}, Box: {1, 0.7, 0.5} }`.
- **Particles that fall.** Gravity is a field of `ParticleForces`, and without any
  `ParticleForces` (on the emitter, on the entity its `forces` names or as a singleton) particles
  feel no force at all. Debris thrown up without it flies off for good: give the emitter
  `ParticleForces: {}` for the default gravity of -9.81.
- **Shapes in space from velocity curves.** A velocity curve is in world units per second, but
  its time is the life, so the same keys trace a different path for every lifetime. Give a
  particle that must trace a shape a fixed `lifetime: {L, L}`. A circle of radius `r` turned
  `N` times over the life is `cos` on one axis and `sin` on the other with an amplitude
  `A = 2 pi N r / L`: keys at every quarter turn, `CurveBezier`, slope 0 at the peaks and
  `+-2 pi N A` at the zero crossings (`orb.flecs`). The circle starts at the spawn point, so
  its centre is `r` to the side of it, not at the emitter: particles spawned over a sphere make
  a cloud of small circles, not an orbit. To circle an axis, spawn at one point `r` from the
  axis (`EmitterPoint`) and give the emitter a speed along the axis: every particle then rides
  the same path, and the particles of all ages draw a helix that stands still while they
  stream along it. A second emitter on the other side of the axis with the curves negated
  draws a second strand. `max_keys` with another amplitude gives each particle its own
  radius, as the three velocity axes share one draw, but the circles all touch at the spawn
  point, so it fans out from there rather than making rings around the axis.
- **A darker or lighter color, not another hue.** `color_variance` draws each channel on its
  own, so `{30, 25, 20}` on brown debris gives green and red chunks. Vary the brightness with a
  constant `color` gradient between white and grey instead, `color: {keys: [{0, {255, 255,
  255}}], max_keys: [{0, {130, 130, 130}}]}` (`shockwave.flecs`).
- **Billboards that lie on the ground.** `ParticleCollision` stops the center of a particle at
  `height`, so a resting billboard sinks half into the ground; raise `height` by half the
  drawn size.
- **A light in step with a burst.** A `ParticleBurst` with `at: 2.5, repeat: -1` goes off at
  2.5, 5, 7.5 s, and a `ParticleLight` with `period: 2.5, loop: true` starts its curve at 0, 2.5,
  5 s, so a flash at the start of the curve meets every burst. The light also flashes once at
  0 s, before the first burst.
- **Moving an included effect.** `include` makes the entities of the file where it puts them.
  Open the root again after the include to place it, `campfire { Position: {-2.6, 0, 0.5} }`;
  its emitters, lights and meshes are its children and move with it. Shared helpers such as
  a billboard mesh stay at the root of the file, with a name that does not collide.

## Checking the result

### Errors

A bad particle curve logs one error when its component is set, naming the component, the entity,
the field, the list and the key, and draws as the default of its field. The files of
`test/assets/curves/mistakes/` give these:

```
invalid ParticleCurves on late: size key 2: time 1.5 is outside of 0 to 1, drawn as the default of size
invalid ParticleCurves on unordered: opacity key 2: time 0.5 is not after time 0.6 of key 1, drawn as the default of opacity
invalid ParticleCurves on jump: spin max_keys key 1: time 0 is not after time 0 of key 0, drawn as the default of spin
invalid ParticleLight on lamp: curve max_keys is not allowed, a light has no random draw to blend two curves by, drawn as the default of curve
invalid ParticleLight on still: curve has keys but period is 0, set the seconds it spans with a period above 0, drawn as the default of curve
invalid ParticleTimeline on seconds: rate key 1: time 2 is outside of 0 to 1, drawn as the default of rate
```

`ParticleTimeline` logs the same as `still` for a `rate` or `speed` with keys and no `period`.

The other problems of a key are `time is not finite`, `value is not finite`, `in is not finite`
and `out is not finite`, and of a curve `kind 3 is not CurveLinear, CurveCatmullRom or
CurveBezier`. An `AnimationTrack` logs `AnimationTrack 'height' of clip 'bounce': curve: key 2:
...` (`docs/animation.md`), and a `Transition` entry with a handle outside 0..1 logs an error
naming the entity and the component (`docs/transitions.md`).

Script errors stop the script, so the entities after them are not made:

- `unresolved reference 'Linear'`: a short kind name, see [The format](#the-format).
- `value 300 is out of bounds for type U8`: a gradient channel above 255.
- `invalid cast from flecs.meta.i64 to box.types.rgb`: a number where `color` wants a color,
  `{0, 1}` instead of `{0, {255, 255, 255}}`.
- `cannot open scope for type 'box.curve.CurveKey'`: a color, `{0, {255, 0, 0}}`, where a
  curve wants a number.
- `unknown member 'velocty_x' for type 'box.particle.ParticleMotion'`: a misspelled field.

These name the line of the script, not the field: look for the curve on that line.

Some mistakes log nothing, as they are valid:

- `in` and `out` on a curve that is not `CurveBezier` are ignored, so a curve that should
  start steep is a straight line. Give it `kind: CurveBezier`.
- Particles without any `ParticleForces` do not fall, see [Showcase](#showcase).

A script reports only its first error, curve errors included: with two bad curves in one file
the second is logged only once the first is fixed (`test/assets/curves/mistakes/two.flecs`).
Fix and run again until it is quiet.

### Looking at frames

The CPU tests cannot see the shaders. To see an effect, write a scene with a camera and a view
that includes the effect, here `/tmp/fx/puff.flecs`:

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

and render it headless with the visual harness of `tools/integration.py`:

```sh
python3 tools/integration.py visual --dir /tmp/fx --out /tmp/fx/out --frames 60 puff
```

It renders the scene with a fixed delta of 1/60 s, so 60 frames are one second, writes the last
frame to `/tmp/fx/out/puff.png` and a page of every render to `/tmp/fx/out/index.html`, and
reports a failure for the missing baseline; the image is there all the same. `include` names a
file from the repository root without `.flecs`, as the harness links `etc`, `apps` and
`test/assets` next to the scene it renders; an effect of your own can also be written into the
scene itself. Render again with other `--frames` to see other times, for example 6, 30 and 90
for the start, the middle and a steady state, and look at the PNGs. A scene of
`test/integration/visual` runs 3 frames unless it has a line `// frames: n`, as
`curves_showcase.flecs` does to show its effects seconds in; `--frames` overrides both. A run of the app,
`bake3 run apps/boxrun --local-env -- /tmp/fx/puff.flecs`, plays it live, and its `capture`
shell command writes a PNG of a frame (README).

To see a curve rather than an effect, plot it: burst one still particle per emitter, with a
lifetime that puts it at a chosen age when the frame is captured, as
`test/integration/visual/particles_curve_kinds.flecs` does for 16 ages of each kind. The
`particles_*.flecs` scenes of that folder are tested examples of every curve component.

`ParticleStats` (`get ParticleStats` in the shell) counts the live and spawned particles, which
shows a rate curve at work without an image.

## Not supported

- **Start size, lifetime, rotation or opacity over the emitter duration.** A `Particle` stores
  only its position, velocity, remaining life and index; its lifetime, size, rotation, spin and
  opacity are drawn again from its seed every frame from the current ranges of the emitter. A
  curve over the emitter clock that changed those ranges would change the live particles too,
  so `ParticleTimeline` has only `rate` and `speed`, which act once at spawn (the speed is kept
  in the velocity). Growing `Particle` to store them is a decision for later. For a size that
  changes with the emitter clock, use two emitters or a `ParticleCurves` over the life.
- **`max_keys` on lights and timelines.** They have no per particle random draw to blend by.
- **A step kind, and per key kinds.** For a jump use two keys close together, which on the GPU
  ramps over 1/63 of the life; for a hold, two keys of the same value. A curve that must be
  straight in one part and smooth in another is a `CurveBezier` with the straight-line slopes
  there.
- **Centripetal Catmull-Rom.** Catmull-Rom is uniform in its parameter, spaced by key time.
- **Free Bezier handles in time.** A key's handles sit a third of the span along, so time stays
  linear in the parameter. Only the CSS `cubic-bezier()` of transitions solves for x.
- **Per axis size, trails, sub-emitters, and curves on MSDF text outlines.** Text outlines keep
  their own double precision curves.
- **A curve editor.** Curves are text for now; the components keep what was authored so an
  editor can read and write them later.

## The cost

What curves cost, from the `Particle` cases of `bench/render` (`Particle.c`). Run them with
`bake3 run --cfg release bench/render --local-env -- Particle`.

Where it goes:

- **Nothing without curves.** An emitter without `ParticleCurves` or `ParticleMotion` reads no
  row; the shaders check the row of each curve and skip it.
- **A constant reads nothing.** A curve with no keys, one key, or 64 equal samples is a value in
  the uniform block of the emitter, not a row.
- **A keyed curve reads two samples** of the curve table per particle per frame, `spin` four
  (its values and their area), `color` two samples of four floats, and `max_keys` doubles that.
  A `ParticleMotion` reads its rows in the integrate pass, the others in the draw.
- **A keyed `color` sorts.** Alpha blended particles without `ParticleAppearance` are drawn
  unsorted; a color curve makes them sort back to front every frame, which costs more than all
  the curve reads together. With `ParticleAppearance` alpha particles always sort, and additive
  ones never do.
- **Baking is on change only.** A curve is baked when its component changes, and a key list the
  table has seen is not sampled again. Rows are shared by content, so instances of an effect
  template and the plays of `box_particle_burst_at` share the rows of their template.
- **Lights, timelines and tracks** are evaluated on the CPU once per entity per frame, exactly,
  and take no row.

The extract cases run 4096 emitters of up to 8 particles into the null backend, per frame, in a
release build on an arm64 Linux VM with one core:

| Case                                 | Frame (ms) | Rows | Bakes per frame | Note                        |
|--------------------------------------|-----------:|-----:|----------------:|-----------------------------|
| `extract_4k_no_curves`               |      0.329 |    0 |               0 | baseline                    |
| `extract_4k_constant_curves`         |      0.363 |    0 |               0 | one key per curve           |
| `extract_4k_curves`                  |      0.473 | 4104 |               0 | keyed, the size differs per emitter |
| `extract_4k_curves_max`              |      0.550 | 8208 |               0 | the same with `max_keys`    |
| `extract_4k_curves_change_one`       |      0.433 | 4105 |               1 | one curve rewritten per frame |
| `effect_plays_1k`                    |      0.352 |    - |               0 | 1000 plays a second, no curves |
| `effect_plays_1k_curves`             |      0.391 |    8 |               0 | the same effect with keyed curves |

So a constant curve costs about 8 ns per emitter per frame, keyed curves about 35 ns and
`max_keys` another 20 ns, with no bake in a steady frame. Rewriting one curve a frame bakes
it once a frame and stays within the noise of the steady keyed case. The 1000 plays a
second of an effect share 8 entries of rows between all their instances.

The draw cases render a million particles. On this machine they ran on a software Vulkan device,
so only their ratios mean something: constant curves (`draw_1m_constant_curves`, 0.78 s a frame)
cost the same as none (`draw_1m`, 0.78 s), the row reads of flat two key curves
(`draw_1m_flat_curves`) add 2.5%, and the keyed curves with a color curve (`draw_1m_curves`, 3.7
s) take 4.7 times as long, nearly all of it the sort the color curve turns on: `max_keys`
(`draw_1m_curves_max`) adds 0.4% to that, and keyed and `max_keys` `ParticleMotion`
(`draw_1m_curves_max_motion`) another 10%. Measure on a GPU before relying on absolute numbers.

## Refinements of the design

The Curves epic was designed up front; these are the places where the code settled a detail,
written down so that this guide and the code agree.

- **Catmull-Rom ends.** The missing neighbour of the first or last key has the value of the key
  and lies one span further out (`box_curve_catmull_rom_slope`, `src/curve/curve.c`), so evenly
  spaced keys give the uniform controls `p1 + (p2 - p0) / 6` and the end slope is half that of
  the end span.
- **Rotation adds turns.** `ParticleCurves.rotation` is in turns, times 360 degrees, added to the
  rotation a particle spawned with; every other particle curve multiplies.
- **Particle times are 0 to 1.** A particle, light or timeline curve with a time outside 0 to 1
  is an error, not clamped (`box_particle_curve_check`, `src/particle/curves.c`).
- **Errors log, curves keep their keys.** The validate hooks always accept the component and log
  the first problem of each bad curve; the extract and the CPU evaluators treat a bad curve as
  the default of its field.
- **`CurveBezier` gradients have flat slopes.** `ParticleGradient` keys have no `in` or `out`, so
  a Bezier gradient eases in and out of every key (`box_particle_gradient_bake`,
  `src/particle/simulate.h`).
- **Rows are shared by baked content.** Curve table rows are found by the hash of their samples,
  not of their keys, so two different key lists that bake to the same samples share a row
  (`box_particle_rows_acquire`, `src/render/particle_rows.c`).
- **A flat curve is a constant.** A curve whose 64 samples are all equal takes no row and reads
  nothing on the GPU (`box_particle_rows_curve`, `box_particle_rows_gradient`). With `max_keys`
  both curves take rows unless both are the same constant.
- **Samples that are not finite fall back.** A valid curve whose baked samples are not finite,
  from slopes large enough to overflow, draws as the default of its field like a curve that
  fails validation.
- **A light curve is exact and has no `max_keys`.** `ParticleLight.curve` is evaluated by
  `box_curve_value` on the CPU, not baked, and `max_keys` on it is an error.
- **Motion axes share a draw.** The three velocity curves of `ParticleMotion` blend toward their
  `max_keys` by one draw, as do the three force curves, so a direction stays a direction; speed
  and drag have their own.
- **Colors blend in linear light,** both along a gradient and between a gradient and its
  `max_keys`.
- **A color curve sorts alpha particles.** Alpha blended particles without `ParticleAppearance`
  are drawn unsorted, since particles of one color blend the same in any order; a keyed color
  curve makes them sort back to front (`box_particles_block_look`, `src/render/particles.c`).
- **The emitter clock** (`ParticleTimeline`, `src/particle/timeline.c`):
  - `period` is in seconds of `ParticlePlayback.time`, and curve times run from 0 to 1 over it.
    A period of 0, no keys or a bad curve is 1.
  - `loop` repeats the curves every period; without it the curves hold their last key after the
    period. A looping Catmull-Rom curve from 0 to 1 takes its end neighbours across the wrap, so
    it joins smoothly.
  - A frame spawns `rate` times the area of the rate curve above 0 over its part of the clock,
    across the wrap of a loop too, so the count does not depend on the frame rate. A negative
    rate value counts as 0.
  - `speed` is the value at the clock after the frame. It multiplies the start speed of the
    particles spawned in that frame only and is not clamped. Live particles keep their velocity.
  - Paused holds the clock, stopped emits nothing, a reset restarts the clock, and bursts are not
    scaled.
  - `max_keys` is an error on both curves, as on a light (`box_particle_timeline_curve_check`).

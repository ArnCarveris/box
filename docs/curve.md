# Curve

`box.curve` (`include/box/curve.h`) defines `Curve`, scalar keyed values over time, and one cubic
evaluator (C, and WGSL twin in `etc/shaders/include/curve.wgsl`) shared by particles, lights,
emission, keyframe animation, audio notes/automation and texture graphs.
Working recipes: `test/assets/curves/` (checked by `CurveRecipes` in `test/cpu/particle`);
broken examples: `test/assets/curves/mistakes/`.

## Format

| Type | Script name | Fields |
| --- | --- | --- |
| `Curve` | `Curve` | `kind` (default `CurveLinear`), `keys` |
| `CurveKey` | `box.curve.Key` | `time`, `value`, `in`, `out` (slopes, value per unit time, `CurveBezier` only) |
| `CurveKind` | `box.curve.Kind` | `CurveLinear` (0), `CurveCatmullRom`, `CurveBezier` |
| `ParticleCurve` | `box.particle.Curve` | `kind`, `keys`, `max_keys` (second curve, per-particle random blend) |
| `ParticleGradient` | `box.particle.Gradient` | `kind`, `keys: [{time, color}]`, `max_keys`; display encoded `rgb` |

```flecs
opacity: {keys: [{0, 0}, {0.1, 1}, {1, 0}]}
size: {kind: CurveCatmullRom, keys: [{0, 0.3}, {0.4, 1.8}, {1, 2.2}]}
spin: {kind: CurveBezier, keys: [{time: 0, value: 1, out: -2}, {time: 1, value: 0, in: 0}]}
size: {kind: CurveCatmullRom, keys: [{0, 1}, {1, 0.4}], max_keys: [{0, 1}, {0.5, 2}, {1, 0.2}]}
color: {keys: [{0, {255, 240, 200}}, {0.6, {230, 70, 20}}, {1, {80, 15, 5}}]}
```

- Keys: `{time, value}` or `{time, value, in, out}` positionally, or by name. Prefer names for Bezier.
- Kind is per curve, not per key. Write full names: `Linear` is a `TimingFunction` constant and
  `CatmullRom` a `PathSegmentKind` constant; either in a curve is a script error
  (`unresolved reference`) that stops the script.
- No keys = the default of the field (`Curves: {}` changes nothing). One key = constant.
  Before the first / after the last key the curve holds that key's value.
- Times finite and strictly increasing; values and slopes finite.
- Each field defines its time axis: particle life 0..1, 0..1 of a `period`, seconds (tracks),
  beats (audio).
- Curves are stored as authored: a bad curve keeps its keys, logs an error, and acts as the field
  default until fixed.
- C: `ecs_set(world, e, Curve, { CurveCatmullRom, BOX_CURVE_KEYS({0, 0.3f}, {1, 2}) });`.
  Components holding a curve own their keys via `BOX_OWNED_CURVE(T, member)` in `box_owned_hooks`.

## Every field that takes a curve

| Component.field | Effect | Default | Time axis | Evaluated | `max_keys` |
| --- | --- | --- | --- | --- | --- |
| `ParticleCurves.size` | × spawn size | 1 | life 0..1 | GPU, 64 samples | yes |
| `ParticleCurves.opacity` | × opacity | 1 | life | GPU | yes |
| `ParticleCurves.rotation` | + turns (1 = 360°) | 0 | life | GPU | yes |
| `ParticleCurves.color` | gradient, × color in linear light | white | life | GPU | yes |
| `ParticleCurves.spin` | × angular velocity; angle = area under it | 1 | life | GPU | yes |
| `ParticleMotion.velocity_x/_y/_z` | + units/s to movement (not stored velocity) | 0 | life | GPU integrate | yes, one draw for 3 axes |
| `ParticleMotion.force_x/_y/_z` | + units/s² to acceleration | 0 | life | GPU integrate | yes, one draw for 3 axes |
| `ParticleMotion.speed` | × stored velocity in a frame's movement | 1 | life | GPU integrate | yes |
| `ParticleMotion.drag` | × `ParticleForces.drag`, <0 = none | 1 | life | GPU integrate | yes |
| `ParticleLight.curve` | × `intensity` | 1 | 0..1 of `period` | CPU exact | error |
| `ParticleTimeline.rate` | × `ParticleEmitter.rate` | 1 | 0..1 of `period` | CPU exact | error |
| `ParticleTimeline.speed` | × start speed of spawns now | 1 | 0..1 of `period` | CPU exact | error |
| `AnimationTrack.curve` (`Track`) | is the member value | none, needs keys | seconds of clip | CPU exact | n/a (`Curve`) |
| `Note.bend` (audio) | pitch bend, semitones -48..48 | 0 | beats of note | CPU | n/a |
| `Note.mod` / `Note.pressure` | vibrato scale / cutoff up to 2 octaves, 0..1 | 1 / 0 | beats of note | CPU | n/a |
| `AutomationClip.curve` (audio) | value of a float member | - | beats of clip | CPU | n/a |
| `TexCurve.curve` (texture node) | maps input to output | - | input value | GPU per texel, ≤32 keys | n/a |

Not a `Curve` but same cubic math: `Path2D`/`Path3D` segments of kind `CatmullRom` (geometry), and
`TransitionSpec` `CubicBezier` handles (see `docs/transition.md`).

## Kinds

Every span becomes one cubic with time linear in the parameter; kinds differ only in controls.

| Kind | Controls of span a→b | Use |
| --- | --- | --- |
| `CurveLinear` | 1/3 and 2/3 of the way: straight lines, never leaves key range | ramps, holds, exact values, gradients |
| `CurveCatmullRom` | slope at key k = `(v[k+1]-v[k-1]) / (t[k+1]-t[k-1])`; end key mirrors itself one span away | smooth organic shapes without slopes |
| `CurveBezier` | `a.value + a.out*dt/3`, `b.value - b.in*dt/3` | eases, fast-start-then-settle |

`in` of the first key and `out` of the last are unused. `in`/`out` on non-Bezier curves are valid
but ignored.

**Bezier slopes** (unit of time = whole life for particles). Span 1→0 over life, linear slope -1:
- `out: -1, in: -1` = straight line. `out: 0, in: 0` = smoothstep.
- `out: -3, in: 0` = `(1 - t)^3`: half left at t≈0.2, 1/8 at 0.5; steepest that does not undershoot 0.
- Slope with the opposite sign of the span goes the wrong way first.

**Overshoot**: Catmull-Rom around uneven peaks/dips or uneven spacing; Bezier with steep slopes.
To stay within keys: use Linear, give a Catmull-Rom peak equal neighbours, or keep Bezier slopes of
a span between 0 and `3 * (b.value - a.value) / dt`. Where values clamp:
- `ParticleCurves.size`, `opacity`, `color` channels clamp at 0 after multiplying; no upper clamp.
- `rotation`, `spin` unclamped (negative spin turns back). `ParticleMotion.drag` < 0 is no drag;
  velocity/force/speed unclamped. `ParticleTimeline.rate` < 0 counts as 0; `speed` < 0 reverses.
  `ParticleLight.curve` unclamped.
- Track on an integer member rounds and clamps to its type (`Rgb` stays 0..255); floats overshoot.

## Looping (lights, timelines, audio automation)

With `ParticleLight.loop` / `ParticleTimeline.loop`, a `CurveCatmullRom` whose keys span exactly
0..1 treats its keys as a cycle: neighbours are taken across the wrap, so the end slope equals the
start slope (`box_curve_loop_span_controls`). End on the starting value or it jumps at the wrap.
Keys not covering 0..1 hold their end values across the wrap. Linear/Bezier are the same with or
without loop (for a Bezier loop give the first key an `out` equal to the last key's `in`).
`ParticleCurves`, motion curves and tracks never loop.

```flecs
curve: {kind: CurveCatmullRom, keys: [{0, 0.4}, {0.3, 1.3}, {0.6, 0.8}, {1, 0.4}]}
```

## Recipes (`test/assets/curves/`)

| File | Idea |
| --- | --- |
| `puff.flecs` | Catmull-Rom size swell + linear fade |
| `spark.flecs` | Bezier size `out: -3` = `(1-t)^3` shrink |
| `flicker.flecs` | looping Catmull-Rom on `box.particle.Light`, uneven key times, `fade` |
| `random_size.flecs` | `max_keys`: per-particle curve between two |
| `fade.flecs` | equal-valued keys for a hold |
| `flame.flecs` | color gradient + growth |
| `burst.flecs` | `Motion.drag` and `velocity_y` curves |
| `pulse.flecs` | `Timeline` rate/speed over a looping period |
| `path.flecs` / `ease.flecs` | geometry Catmull-Rom paths / transition `CubicBezier` |
| `showcase/*.flecs` | campfire, shockwave, orb, chimney (`test/integration/visual/curves_showcase.flecs`) |

```flecs
using box.curve
using box.particle
using box.transform

spark {
  Position: {0, 0, 0}
  Emitter: {max: 400, rate: 120, spread: 35, speed: {4, 7}, lifetime: {0.4, 0.8},
    size: {0.06, 0.1}, color: {255, 200, 120}, seed: 3}
  Forces: {drag: 1.5}
  Appearance: {blend: Additive, stretch: 0.03}
  Curves: {
    size: {kind: CurveBezier, keys: [{time: 0, value: 1, out: -3}, {time: 1, value: 0, in: 0}]}
    opacity: {keys: [{0, 1}, {0.7, 1}, {1, 0}]}
  }
}
```

## Errors

Particle curves log `invalid <Component> on <entity>: <field> <problem>, drawn as the default of
<field>`; tracks log `AnimationTrack '<track>' of clip '<clip>': <reason>`. Check for:
- Non-finite time/value/slope; times not strictly increasing (`key 2: time 0.5 is not after time
  0.5 of key 1`), also in `max_keys`.
- Particle curve times outside 0..1 (seconds used instead of normalized life/period).
- Light/timeline curve with keys but `period` ≤ 0; `max_keys` on a light or timeline.
- Short kind names (`Linear`), gradient channel > 255, scalar where a color is expected.
Script errors stop evaluation: fix the first and rerun to see the next.

## C API

| Function | Does |
| --- | --- |
| `box_curve_value(curve, t, fallback)` | value at t; fallback without keys |
| `box_curve_area(curve, from, to, fallback)` | exact integral (closed form per span) |
| `box_curve_loop_value/_area(curve, period, ...)` | looping variants; period 0 = non-looping |
| `box_curve_span_controls` / `box_curve_loop_span_controls` | control values of span k |
| `box_curve_bake(curve, from, to, count, fallback, out)` | evenly spaced samples |
| `box_curve_validate(curve)` | NULL or first problem (free with `ecs_os_free`) |
| `box_curve_cubic`, `box_curve_cubic3` | de Casteljau scalar / vec3 |
| `box_curve_ease(x1, y1, x2, y2, x)`, `box_curve_solve(x1, x2, x)` | CSS cubic-bezier ease |

## Cost and limits

- GPU particle curves bake to 64 samples over 0..1 (63 intervals), read linearly: a change faster
  than 1/63 of life (~1.6%) softens into a ramp. Keys at 1/3, 2/3 land on samples. CPU curves
  (lights, timelines, tracks) are exact.
- No step kind, per-key kind or free time-axis handles: a near-jump is two close keys.
- Rebaked on change; rows shared by identical content; flat curves become constants (no table
  read). `max_keys` needs two rows unless both are the same constant.
- Per particle per frame a keyed field reads ~2 samples; spin also reads its integral; color reads
  4-channel samples; `max_keys` doubles reads.
- Benchmark: `bake3 run --cfg release bench/render --local-env -- Particle`.
- Render a recipe: put it in a scene with a camera and `View`, then
  `python3 tools/integration.py visual --dir <dir> --out <dir>/out --frames 60 <scene>`
  (fixed 1/60 s delta) or `bake3 run apps/boxrun --local-env -- <scene>.flecs`.

# Particle

`box.particle` (`include/box/particle.h`) provides GPU particle emitters. Particles never reach the
ECS: each emitter sends a parameter block in the render packet, and the renderer keeps the
particles in pools it owns, then spawns, simulates (compute) and draws them. With a fixed timestep
a run is the same every time. Curve authoring (every curve field, recipes, errors, cost) is in
`docs/curve.md`.

## Components (script names drop the `Particle` prefix)

| C name | Script | Key fields (defaults) |
| --- | --- | --- |
| `ParticleEmitter` | `Emitter` | `max` (1000, clamp `BOX_PARTICLE_MAX` 4194304), `rate`/s (0), `shape` (`EmitterPoint`), `radius`, `extents` (half size), `source` (mesh for `EmitterMesh`, 0 = self), `direction` ({0,1,0}; {0,0,0} = at rest), `spread` (half-angle °), `speed` ({1,1}), `radial`, `lifetime` ({1,2}), `size` (diameter, {0.1,0.1}), `rotation` (°), `spin` (°/s), `opacity` ({1,1}), `acceleration`, `inherit_velocity`, `color` (255,255,255), `color_variance`, `forces` (entity), `seed`, `linger`, `local` |
| `ParticleRange` | `Range` | `{min, max}`; max ≤ min hands out min |
| `EmitterShape` | `EmitterShape` | `EmitterPoint`, `EmitterSphere`, `EmitterBox`, `EmitterDisc` (XZ), `EmitterMesh` (area weighted) |
| `ParticleForces` | `Forces` | `gravity` ({0,-9.81,0}), `wind`, `drag` (rate/s), `turbulence`, `turbulence_scale` (1), `drift` |
| `ParticleCollision` | `Collision` | `ground` (true), `height` (0), `bounce` (0.5), `friction` (0.2), `die` |
| `ParticlePlayback` | `Playback` | `state` (`Playing`/`Paused`/`Stopped`), `time` (engine-written), `reset` (one frame) |
| `ParticleBurst` | `Burst` | `count`, `at` (first time and period; 0 = once on first frame), `repeat` (<0 = forever) |
| `ParticleCurves` | `Curves` | `size`, `opacity`, `rotation` (turns), `color` (gradient), `spin`: over life, GPU |
| `ParticleMotion` | `Motion` | `velocity_x/_y/_z`, `force_x/_y/_z`, `speed`, `drag`: over life, GPU |
| `ParticleTimeline` | `Timeline` | `period`, `loop`, `rate`, `speed`: multiply emission over the playback clock, CPU |
| `ParticleLight` | `box.particle.Light` | `intensity` (1), `curve`, `period`, `loop`, `fade`: drives the emitter's `PointLight`/`SpotLight` |
| `ParticleAppearance` | `Appearance` | `shape` (`Circle`/`Sprite`/`Card`), `blend` (`Alpha`/`Additive`), `texture`, `columns`, `rows`, `fps` (0 = sheet once over life), `softness`, `stretch`, `lit` (0..1) |
| `EffectFlash` | `EffectFlash` | `intensity` (0 = the light's), `seconds` (0 = 0.2 s): flash of a `PointLight` per effect play |
| `ParticleSettings` | `box.particle.Settings` | singleton: `effect_max`, `effect_total` (0 = any), `effect_overflow` (`EffectRecycle`/`EffectSkip`/`EffectMerge`), `effect_distance`, `effect_merge` |
| `ParticleEffect` | `Effect` | on an effect instance root: `effect`, `scale`, `time`, `emit`, `retire` |
| `ParticleStats` | `Stats` | singleton: `emitters`, `simulated`, `particles`, `spawned`, `bytes`, `lingering`, `effects`, `recycled`, `merged`, `dropped` |

## Script

```flecs
using box.curve
using box.light
using box.material
using box.particle
using box.transform

flame {
  Position: {0, 0.15, 0}
  Rgb: {255, 140, 50}
  PointLight: {intensity: 4, range: 6}
  box.particle.Light: {
    intensity: 4, period: 0.9, loop: true
    curve: {kind: CurveCatmullRom, keys: [{0, 1}, {0.22, 1.15}, {0.5, 1.2}, {0.63, 0.75}, {1, 1}]}
  }
  Emitter: {
    max: 400, rate: 90, shape: EmitterDisc, radius: 0.25, spread: 8,
    speed: {0.6, 1}, radial: {-0.35, -0.2}, lifetime: {0.6, 1}, size: {0.35, 0.5},
    opacity: {0.7, 0.9}, seed: 11
  }
  Forces: {gravity: {0, 1.2, 0}, turbulence: 1.2, turbulence_scale: 2}
  Appearance: {blend: Additive}
  Curves: {
    color: {keys: [{0, {255, 240, 200}}, {0.25, {255, 170, 60}}, {0.6, {230, 70, 20}}, {1, {80, 15, 5}}]}
    size: {kind: CurveCatmullRom, keys: [{0, 0.5}, {0.25, 1}, {1, 0.2}]}
    opacity: {keys: [{0, 0}, {0.1, 1}, {0.45, 0.7}, {0.8, 0}]}
  }
}

burst {
  Position: {0, 0, 0}
  Emitter: {max: 400, rate: 0, shape: EmitterSphere, radius: 0.05, spread: 180,
    speed: {4, 6}, lifetime: {1.4, 1.4}, size: {0.08, 0.12}, seed: 5}
  Burst: {count: 150, at: 1.5, repeat: -1}
  Forces: {gravity: {0, 0, 0}, drag: 12}
  Motion: {
    drag: {keys: [{0, 0}, {0.25, 0}, {0.35, 1}]}
    velocity_y: {keys: [{0, 0}, {0.4, 0}, {0.7, 1.5}]}
  }
}

pulse {
  Position: {0, 0, 0}
  Emitter: {rate: 200, spread: 180, lifetime: {1, 1}, size: {0.05, 0.05}}
  Timeline: {period: 1, loop: true, rate: {kind: CurveCatmullRom, keys: [{0, 0}, {0.5, 1}, {1, 0}]}}
}
```

## One-shot effects

A template (prefab or script template) whose root/descendants with `Emitter` are layers, usually
`rate: 0` plus a `Burst`. `box.particle.burst_at(template, position, scale)` plays it at a world
point from a pooled instance; the instance returns to the pool disabled once layers and flashes
are done. Budgets come from the `ParticleSettings` singleton.

```flecs
$ { box.particle.Settings: {effect_max: 24, effect_total: 96, effect_overflow: EffectRecycle} }

prefab Blast {
  sparks {
    Position: {0, 0, 0}
    Emitter: {max: 200, rate: 0, shape: EmitterSphere, radius: 0.1, spread: 180,
      speed: {2, 4}, lifetime: {1, 1}, size: {0.05, 0.1}, color: {255, 170, 60}, seed: 4}
    Appearance: {blend: Additive}
    Burst: {count: 150}
  }
  light {
    Position: {0, 0, 0.3}
    PointLight: {intensity: flecs.script.math.PI, range: 5}
    EffectFlash: {intensity: 6 * flecs.script.math.PI, seconds: 0.5}
  }
}

const lit: entity = box.particle.burst_at(Blast, {-2.5, 1, 0}, 1)
```

C: `ecs_entity_t root = box_particle_burst_at(world, effect, pos, scale);` returns 0 when the
template has no emitter or a full budget dropped the burst. From a system on the main thread before
`EcsPreStore`, pass the stage: the instance is made when the stage merges.

## Shell

```
get fx box.particle.Playback
set fx box.particle.Playback {state: Paused}
get $ box.particle.Stats
```

## Behavior rules and gotchas

- No `Forces` anywhere = no gravity at all. `Forces: {}` gives -9.81. Forces add up from the world
  singleton, `ParticleEmitter.forces` entity, and the emitter entity.
- Spawn count per frame = `rate * delta`, fraction carried. A full pool spawns nothing; changing
  `max` empties the pool.
- Particles live in world space (plus `drift`) unless `local: true` (move/turn/scale with emitter;
  forces still act in world directions; `inherit_velocity` does nothing).
- World scale of the emitter (cube root of its 3x3 determinant) multiplies size, speeds,
  acceleration, softness, gravity, wind, turbulence, drift and motion velocity/force curves;
  `drag` and collision `height` do not scale. Non-uniform scale stretches only the spawn volume.
- `Playback`: `Paused` freezes pool and clock; `Stopped` ends emission, live particles finish;
  `Playing` again restarts the clock and burst schedule; `reset: true` empties the pool and
  zeroes the clock for one frame. No `Playback` = plays.
- `Burst` may sit on a descendant of an emitter; it uses the nearest emitter at or above it.
  A burst larger than free slots spawns what fits. Bursts are not scaled by `Timeline`.
- `Timeline` only scales rate and start speed; size/lifetime/opacity ranges have no clock curve.
  Timeline/Light curves with keys need `period > 0` and no `max_keys`.
- `ParticleLight`: paused holds, stopped fades over `fade` s, playing restarts.
- Collision: hit lifts onto the plane, keeps `bounce` of normal speed (reversed), loses
  `friction` of tangential speed, or dies with `die`. Early deaths keep their slot and count
  until their lifetime.
- `linger: true`: deleting the emitter lets live particles finish (counted in `Stats.lingering`).
- `EmitterMesh` samples a table of points refilled when the shape, source or seed changes.
- Without `Appearance`: soft alpha-blended circles. Particles draw in the view's HDR image before
  bloom (colors > 1 bloom) with atmosphere aerial perspective. Draw order: alpha before additive,
  then by sprite, then farthest emitter first; no per-particle sort.
- Spin is invisible on the default circle: use a sprite.
- `ParticleCard` draws the texture (sheet frames as a sprite) on a world space quad that tumbles:
  each particle draws a random initial orientation and a random world axis from streams of its
  own; `rotation` is the initial angle and `spin` (with the `rotation`/`spin` curves) the
  angular speed around that axis. Both sides are lit (the normal faces the camera); edge on it
  keeps 1.5 pixels of width. With a `Collision` ground (not `die`), a card within 0.01 units of
  the plane and slower than 0.5 units/s (both times world scale) rests: it stops turning and
  eases flat (normal to ±Y, keeping its heading, lifted 2% of its size off the plane against
  depth fighting) until it dies, so give it an opacity fade.
  Orientations stay in world directions also for `local` emitters; `stretch` does not apply.
- A card pool keeps a 4-byte card word per particle after its slots (`max` 1000: 1152 slots
  instead of 1024, 14 pools per page instead of 16) and clamps `max` to half
  `BOX_PARTICLE_MAX`; other shapes keep their pools unchanged. Switching to or from `Card`
  empties the pool.

```flecs
leaves {
  Position: {0, 4, 0}
  Emitter: {max: 300, rate: 30, shape: EmitterBox, extents: {3, 0.2, 3}, direction: {0, 0, 0},
    lifetime: {8, 8}, size: {0.25, 0.35}, spin: {90, 240}, seed: 3}
  Forces: {drag: 2.5, turbulence: 1.5}
  Collision: {bounce: 0.1, friction: 0.6}
  Appearance: {shape: Card, texture: leaf, lit: 1}
  Curves: {opacity: {keys: [{0, 1}, {0.85, 1}, {1, 0}]}}
}
```
- Each drawn property has its own random stream: changing one range keeps the other draws.
- Scenes write `box.particle.Light` and `box.particle.Settings` qualified (`Settings` also exists
  in `box.audio`).

## Cost

- `ParticleStats.bytes` = device memory of pools; `simulated` = pools that ran compute this frame.
  Live counts come from extract spawn accounting, nothing is read back from the GPU.
- Curves bake to 64-sample rows, shared between identical curves; constant curves take no row.
- Benchmark: `bake3 run --cfg release bench/render --local-env -- Particle`.
- Tests: `test/cpu/particle`, `test/gpu/render` (`Particle`), visual
  `test/integration/visual/particles_*.flecs`.

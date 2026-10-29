# Porting ore_else's visuals to box

How the flecs-engine features that ore_else's models, animations and effects use map to box. The
rule of the OreElse epic: port everything box can already render; skip only rendering features
box does not have. The skipped features are listed at the end.

## Transforms and geometry

- `Position3`, `Rotation3`, `Scale3` become `Position`, `Rotation`, `Scale`. Animated parts need
  `DynamicTransform` (`Velocity` and `AngularVelocity` imply it).
- **Euler order differs.** flecs-engine composes `Rx * Ry * Rz` (`glm_euler_xyz`), box
  `Ry * Rx * Rz` (`glm_euler_yxz`). They agree unless both x and y are non-zero. That happens in
  the behemoth's legs (`ore_else/etc/critters/behemoth.flecs`, about lines 194-218), the pad clamp
  (`ore_else/etc/rocket/pad_clamp.flecs:21`), and in C the missile orientation (pitch and yaw)
  and the corpse pose. Convert the angles or split the rotation into a yaw parent and a pitch
  child. The random tilts of flora and ore chunks can be ported as they are.
- `LookAt` has no box equivalent: compute a `Rotation` (bunnyhopper's scene does this for its
  camera and sun). Box's `Camera.fov` is in degrees: PI / 3.6 is 50.
- `Box`, `Quad`, `Sphere`, `HemiSphere` and `IcoSphere` have the same names and parameters.
- `Cylinder` and `Cone` have a built-in radius of 0.5 in flecs-engine and no field for it; box's
  `radius` defaults to 0, so always set `radius: 0.5` (and keep the `Scale`).
- flecs-engine's immediate draws (`flecsEngine_draw`, used for the laser beam, tile markers and
  ghosts) become persistent entities that are moved, shown and hidden.

## Materials and textures

- `Rgba {r, g, b, a}` becomes `Rgb {r, g, b}`, plus `Opacity {a / 255}` when a is below 255.
- **Color bytes are linear in flecs-engine, sRGB in box.** flecs-engine divides a color byte by
  255 (`flecsEngine_colorChannelToFloat`) and samples its textures as stored (`RGBA8Unorm`);
  box decodes color bytes and color textures from sRGB, so the same bytes look darker and more
  saturated in box. Write the sRGB encoding of the original's byte instead,
  `round(255 * srgb_encode(b / 255))` (`apps/oreelse/tools/linear_colors.py encode r g b`), for
  every color: `Rgb`, `Emissive`, light colors, particle colors and color curves, `Tint`s, the
  UI. Alpha stays as it is. A color computed in script computes the original's bytes and
  encodes them (`oreelse.srgb()`). Load color textures (albedo, emissive, sprites, UI images)
  with `TexturePath: {..., linear: true}`, not normal or ORM maps. The bunnyhopper port kept
  the bytes; ore_else does not, playtesting showed it too saturated (`apps/oreelse/README.md`,
  Colors).
- `AlphaBlend` becomes `Opacity` below 1 (or `BlendMode`). For the textured flora fronds and
  membranes use `AlphaCutoff: {0.5}` instead.
- `PbrMaterial`, `Emissive` and `TextureTransform` map one to one. `Emissive` is not affected by
  `Tint`.
- Recoloring a whole model in C (the original's `oreCharParts`, which sets the `Rgba` and the
  `Emissive` of every part of a corpse) becomes a `Tint` on the model's root, which box composes
  down the hierarchy. As the tint leaves emission alone, the emissive parts (eyes, visors,
  glowing spots) get their emission turned off by a system, as the ghosts' `OreGhostDim` does.
- `Texture` / `Svg` become `TexturePath {path, width, height}`, with `linear: true` for a color
  texture; asset paths become `apps/oreelse/etc/assets/...` (copy the SVGs from
  `ore_else/etc/assets` unchanged).
- `PbrTextures.roughness` becomes `PbrTextures.orm` (red occlusion, green roughness, blue
  metallic). `terrain_dirt_rough.svg` holds only green, so box would read it as full occlusion:
  add `terrain_dirt_orm.svg` with red and blue at 255.
- `*_normal.svg` go to `PbrTextures.normal`. Box expects green pointing up the image; check one
  lit render and flip green in the SVG if the bumps look inverted.

## Lights

- `PointLight {intensity, range}` maps to box's `PointLight`; the units match. The falloff
  differs: flecs-engine `(1 - (d/r)^4) / (d^2 + 1)`, box `(1 - (d/r)^4)^2 / d^2`, so box is much
  brighter within one unit of a light. Retune the small lamps (range 2.6) by eye against the
  original's look.
- One-shot flashes (explosions, muzzle flashes) can be `EffectFlash` in a particle effect or a
  short C fade.
- `DirectionalLight` with `Rgb`, and `Atmosphere {sun}` as bunnyhopper does; flecs-engine's
  `CelestialLight` top of atmosphere settings have no equivalent.

## Particles

flecs-engine keeps the look and physics in shared pools (`Particles`) that bursts and emitters
spawn into. Box gives every `ParticleEmitter` layer its own `ParticleAppearance`,
`ParticleForces` and `ParticleCurves`. So each original burst or emitter becomes a box layer that
copies the settings of its pool. One-shot effects are prefabs played with
`box_particle_burst_at(world, effect, pos, scale)` (or `box.particle.burst_at` from script).

| flecs-engine | box |
|---|---|
| pool texture | `ParticleAppearance {shape: ParticleSprite, texture}` |
| additive blend, sort | `ParticleAppearance.blend` |
| gravity g (down) | `ParticleForces.gravity {0, -g, 0}` |
| drag, turbulence, turbulence_scale | `ParticleForces.drag`, `.turbulence`, `.turbulence_scale` |
| soft_depth, lighting | `ParticleAppearance.softness`, `.lit` |
| alpha and size envelopes, Bezier curves | `ParticleCurves.opacity` and `.size`, sampled at t = 0, 1/3, 2/3, 1 |
| color_ramp | `ParticleCurves.color` |
| count, rate, enabled | `ParticleBurst.count`, `ParticleEmitter.rate`, `ParticlePlayback` |
| size, max_age with variance | `size {min, max}`, `lifetime {min, max}` |
| rotation, spin (radians) | `rotation`, `spin` (degrees) |
| velocity with variance | `direction`, `speed {min, max}`, `spread` |
| radial | `radial {r, r}`, along the offset of the spawn point in the frame of the emitter |
| shape Sphere, Disc | `EmitterSphere`, `EmitterDisc` with `radius` |
| shape Hemisphere, Ring | `EmitterMesh` over a `HemiSphere` or `Torus` |
| shape Cone with angle | `EmitterDisc` with `spread` |
| stretch above 1 | `ParticleAppearance.stretch` |
| inherit_velocity | `inherit_velocity` |
| burst scale from C | the `scale` argument of `box_particle_burst_at` |
| beam and weld lights | `ParticleLight` |

Particle color and opacity are clamped to [0, 1] in box, so the original's emissive pools (glow,
fire, ember) cannot be brighter than 1. Where that matters, add an additive halo layer, as
bunnyhopper's carrot sparkle does. A squash stretch below 1 and `turbulence_speed` have no
equivalent; leave them out.

## Scene

| flecs-engine (`ore_else/etc/engine.flecs`) | box |
|---|---|
| `RenderView` ambient 0.5 from the atmosphere | `View.lighting` with the `Atmosphere` environment at intensity 0.5, no `AmbientLight` |
| `SSAO {radius: 0.5}` | `Gtao {radius: 0.5}` |
| `Bloom` | `Bloom` |
| `AutoExposure` | `AutoExposure` with the same fields |
| `Tony` | `Tonemap {operator_: TonyMcMapface}` |
| shadow map 4096, range 140 | `ShadowSettings {resolution: 4096, max_distance: 140}` |
| `screen_px_threshold: 3` | `CullSettings {min_dimension_px: 3}` |

## Animation

The original's animation components (`Spinner`, `Piston`, `Shake`, `Vent`, `WorkEmitter`,
`WorkLight`, `StatusLamp`, `FlashLight`, `Limb`, `Flight`) are game code: port them as app
components and C systems that run only while `Playing` (the game's single pause mechanism; the
original's ran while paused). Box's `AngularVelocity` ignores the game state, so do not use it for
them.

## Not ported: rendering features box does not have

- `SurfaceWear` and `SurfaceEffects` (dust, grime, scratch, grain, streak, edge wear, burn): the
  buildings' aging and the scorch marks from damage, and the C that drives them.
- Cloud shadows.
- Stars and the celestial top of atmosphere light.
- Particles brighter than 1 (approximated with halo layers where it matters).

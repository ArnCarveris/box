# All effects showcase

- `showcase.flecs` runs gtao, ssr, height fog, bloom, auto exposure and tone mapping over a
  checker floor and wall, crates with four material layers, a metal ball, a spinning box, an
  emissive bar and spark, an alpha, an additive and a frosted panel over their own backdrops, a
  polished slab and a colonnade to 88 units; `showcase_off.flecs` is the same content with the
  chain off. Both are baselined and repeat bit identical, and what each region of the image
  proves is listed in `test/integration/visual/README.md`.
- Found and fixed: the sorted pass left the normal attachment of the opaque pass alone, so the
  occlusion and the reflection pass read a blended pixel as the surface behind it. It draws
  there now, a blended fragment folding its coverage into the indirect diffuse fraction and
  dropping the indirect specular one, a two sided opaque draw writing its own values instead of
  leaving the stale ones. The worst AO on the three panels falls from 24, 70 and 37 levels to
  8, 13 and 7, which is the background seen through them, SSR from 48, 72 and 57 to 1, and the
  slab keeps its 77. `box_gtao_ambient` and `box_gtao_covered` in `renderer/gtao.h` are the
  twin the three new `Effects` cases check.
- By eye: no AO on the fog or on the panels, fog over the reflections, no double gamma (a
  neutral chain matches the direct path within one level outside the panels, which a float
  target blends in linear light and an 8 bit one in display space), no banding (0.26 residual
  against a 9 by 9 mean over the sky, 0.10 with the dither off), 466 clipped pixels against
  42481 with the chain off, and a bloom that halves with the energy of its emitter.
- 139 `view` (49 `Effects`), 233 `render` and 24 `shader` cases green, the 41 other visual
  baselines unchanged, lint 0, `bench.json` within the noise of the machine. At 1280x720 over
  300 headless frames the chain costs 1.195 ms mean CPU frame time against 0.460 with every
  effect off, the median of five runs. Left out: bloom runs before the auto exposure, so its
  firefly clamp is in scene units rather than exposure relative; a follow up should settle it.

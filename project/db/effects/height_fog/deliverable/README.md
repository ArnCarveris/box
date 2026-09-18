# Height fog

- `Fog {color, density, falloff, start, max_opacity, height}` sits on `box.effects.stack.fog`
  (order 700, ahead of the bloom and the tone map), off by default, so a scene turns it on with
  `Effect: {enabled: true}` and every existing capture stays bit identical.
- The pass reads the color and the depth of the view. The effect uniform now also carries the
  camera position and the clip to world matrix, which extract fills for every view with a chain,
  so the shader rebuilds the world position of a pixel and integrates from `start` to it, or to
  5000 units when it hit nothing. The density is `density * exp(-falloff * (y - height))`.
- That integrates in closed form along a straight ray as
  `base * (1 - exp(-falloff * dy * len)) / (falloff * dy)` with `base * len` as the level limit.
  `1 - exp(-depth)` is the coverage, capped by `max_opacity` and mixed in linear light;
  `src/renderer/fog.h` is the twin the tests check against a numeric integral at seven ray slopes.
- 33 `Effects` cases and 112 `view` green, `bench.json` unchanged against the tasks before it,
  and at 1280x720 over 300 headless frames the pass costs 0.154 ms mean CPU frame time against
  0.132 off, the median of five runs.
- `height_fog.png` is a colonnade receding to 100 units, fog height 0.5 and falloff 0.45: the sky
  reads 68 at the top where the ray leaves the layer and 179 at the horizon where it stays in it,
  and the floor fades from 170 near the camera to 200 at the back.

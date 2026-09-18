# Screen space reflections

- `Ssr {steps, thickness, max_roughness, intensity, fade}` sits on `box.effects.stack.ssr`
  (order 650, between the occlusion and the fog), off by default, so every existing capture
  stays unchanged. The pass reads the color, the depth and the normals of the view.
- Every pixel marches its reflection ray in world space, projects each step with the new
  world to clip matrix of the effect uniform and calls it a hit when the ray has travelled
  past the surface the depth shows there by less than `thickness`, which grows with the
  distance marched; five bisections then refine the crossing. `renderer/ssr.h` is the twin
  the tests check, for the march, the roughness weight and the edge fade.
- A hit is weighted by `1 - smoothstep(max_roughness/2, max_roughness, roughness)` and by an
  edge fade over `fade` of the image, and what is left falls back to the prefiltered
  environment of the view, which is the radiance the mesh pass already used, so a miss leaves
  the pixel exactly as it was and there is no boundary between traced and untraced pixels.
  A rough hit reads a five tap neighbourhood scaled by its roughness instead of tracing more.
- The fourth channel of the normal attachment now packs the indirect specular fraction and
  the roughness into one half float, so the pass scales only the specular indirect term:
  `rgb * (1 + specular * intensity * (reflection / prefiltered - 1))`.
- The depth pyramid of a view keeps the farthest depth of a region because it exists to hide
  instances behind walls, which cannot bound a march from the near side, so the march reads
  the depth target and spends its budget on the bisection instead. That is the one deviation.
- 46 `Effects`, 136 `view`, 233 `render` and 24 `shader` cases green, `bench.json` unchanged
  against the gtao task apart from the SVG work of another epic. At 1280x720 over 300
  headless frames the pass costs 0.292 ms mean CPU frame time at 32 steps against 0.198 with
  the effect off, the median of five runs. It runs at the resolution of the view.
- `ssr.png` is a glossy floor under colored boxes, a lamp and a metal sphere, all of which
  the floor reflects, next to `ssr_off.png` where only the prefiltered environment is left.

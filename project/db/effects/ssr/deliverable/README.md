# Screen space reflections

- `Ssr {steps, thickness, max_roughness, intensity, fade}` sits on `box.effects.stack.ssr`
  (order 650, between the occlusion and the fog), off by default, so every existing capture
  stays unchanged. The pass reads the color, the depth and the normals of the view.
- Every pixel marches its reflection ray in world space, projects each step with the new
  world to clip matrix of the effect uniform and calls it a crossing when the ray passed
  behind the surface the depth shows there while the step before it was still in front of
  that surface; five bisections then refine it. `thickness` is the tolerance of the
  confidence of a hit rather than its test, so the phase of the steps cannot turn one pixel
  into a miss while its neighbour hits; `effects/ssr_noise` has that story.
  `renderer/ssr.h` is the twin the tests check, for the march, the confidence, the roughness
  weight and the edge fade.
- A hit is weighted by `1 - smoothstep(max_roughness/2, max_roughness, roughness)` and by an
  edge fade over `fade` of the image, and what is left falls back to the prefiltered
  environment of the view, which is the radiance the mesh pass already used, so a miss leaves
  the pixel exactly as it was and there is no boundary between traced and untraced pixels.
  The edge fade follows the whole path of a ray, not the texel it hit, and a ray fades out
  over the last third of its span, so a ray that leaves the image finds its neighbours
  already faded to the same environment.
  A rough hit reads a five tap neighbourhood scaled by its roughness instead of tracing more.
- The fourth channel of the normal attachment now packs the indirect specular fraction and
  the roughness into one half float, so the pass scales only the specular indirect term:
  `rgb * (1 + specular * intensity * (reflection / prefiltered - 1))`.
- The depth pyramid of a view keeps the farthest depth of a region because it exists to hide
  instances behind walls, which cannot bound a march from the near side, so the march reads
  the depth target and spends its budget on the bisection instead. That is the one deviation.
- 46 `Effects`, 136 `view`, 233 `render` and 24 `shader` cases green, `bench.json` unchanged
  against the gtao task apart from the SVG work of another epic. At 1280x720 over 300
  headless frames the pass costs 0.271 ms mean CPU frame time at 32 steps against 0.198 with
  the effect off, the median of five runs. It runs at the resolution of the view, and
  `python3 tools/ssr_check.py` counts the isolated misses left in the floor of its scene.
- `ssr.png` is a glossy floor under colored boxes, a lamp and a metal sphere, all of which
  the floor reflects, next to `ssr_off.png` where only the prefiltered environment is left.

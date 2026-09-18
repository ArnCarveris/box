# GTAO

- `Gtao {radius, slices, steps, power, temporal, half_res}` sits on `box.effects.stack.gtao`
  (order 600, ahead of the fog), off by default, so every existing capture stays unchanged.
  The pass declares the color, depth, normals, motion and the new occlusion input.
- The chain runs after the lighting, so the pass cannot reach the ambient term directly. The
  second attachment of the mesh pass now holds the world normal octahedrally in two RGBA16F
  channels and the indirect diffuse and indirect specular fraction of the pixel in the other
  two, and the pass scales the image by `1 - indirect * (1 - ao)`. Direct light, punctual
  light and emissive are untouched, which is what the ambient only rule asks for.
- The occlusion input is work the frame does: a trace pass walks `slices` slices of the
  hemisphere at half resolution, finds the highest occluder on each side of a slice over
  `steps` samples and integrates the closed form of Jimenez et al between the two horizons;
  a resolve pass denoises that over a four by four block, which cancels the ordered four by
  four rotation of the trace exactly, and blends the history of the view at 0.9, reprojected
  with the motion vectors and rejected where the view distance of a pixel no longer matches.
  `temporal` turns the blend off; the fixed headless step keeps the accumulation deterministic.
  The chain pass upsamples the result with a depth aware two by two tap.
- 41 `Effects`, 131 `view`, 233 `render` and 24 `shader` cases green. `Effects.extract_chain`
  rises 7% because the stack holds one effect more; the other moves in `bench.json` come from
  the texture and job work of another epic.
- At 1280x720 over 300 headless frames the passes cost 0.475 ms mean CPU frame time at half
  resolution and 0.626 at full against 0.204 with the effect off, the median of five runs.
- `gtao.png` is a room corner with boxes and spheres at radius 2.5, four slices and power 2,
  next to `gtao_off.png`: the contacts, the corner and the foot of the pillar darken.

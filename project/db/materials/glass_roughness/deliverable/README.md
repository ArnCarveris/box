# Milk glass roughness sweep

- `glass_sweep.flecs` rows six `Opacity 0.5` `AlphaBlend` panels at roughness 0,
  0.2, 0.4, 0.6, 0.8 and 1.0 over checker boxes, spheres and the tiled floor.
  Before: panels 1 to 5 look identical with the geometry behind them sharp, and
  only roughness 1 changes, into a blocky flat wash bleeding past its edge.
- The background copy is now a filtered pyramid: a thirteen tap downsample per
  level and an eight tap tent upsample mixed back at half weight, stopped at 32
  texels on the short axis (five levels at 1280x720) and sampled trilinearly.
- `src/renderer/blur.h` holds the shared mapping `box_blur_level(roughness,
  levels) = sqrt(roughness) * (levels - 1)`, repeated in the WGSL: the radius
  doubles per level, so the root spreads the six steps evenly up the chain.
- Translucent fragments shade as glass: the pyramid is read at a screen position
  bent by the view space normal, tinted with the hue of the material and faded by
  Fresnel, the diffuse response fades with opacity and the sun and environment
  specular sit on top. That kernel, the refracted uv with a roughness driven lod
  and transmission in the PBR response come from the flecs-engine reference.
- `box_view_t.translucent_count` from extract builds the chain once per view per
  frame, only when an alpha or premultiplied instance is visible.
- After: six distinct smooth steps and no blockiness; `milk_glass.png` and
  `blending.png` still read right and every other scene is bit identical.
- 1280x720 release headless, median of seven: sweep 0.44 to 0.53 ms, milk glass
  0.42 to 0.51 ms, the sweep without panels 0.27 to 0.28 ms. 28 `Blending` cases
  (12 new), 595 tests green, lint clean, `bench.json` unchanged, wasm builds.

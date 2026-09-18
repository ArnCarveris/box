# HDR scene target

- A view whose chain runs draws the mesh, transparent and glass passes into a pooled RGBA16F
  target instead of its 8 bit one, the blurred glass copy follows that format and the last
  pass of the chain writes the image of the view. A view without a chain keeps the direct
  path and the scenes without effects render bit identical.
- The encode moved to the end: `box_view_gamma` clears the gamma flag of the view uniform
  when the scene target is float, `box_effect_decode_flag` tells a pass its image is already
  linear and `box_effect_encode_flag` is one on the last pass only. A pass that grades in
  display space asks for `fx_to_encoded`, which keeps the color grade of `effect_toggle`.
- The clear color of a view is given in the space its target shows, so the float target is
  cleared with the decoded value and the background of a view with a chain is unchanged.
- The mesh, normal, sorted and blur pipelines are built per target format, the float set the
  first time a chain asks for it.
- 36 `Effects` and 126 `view` cases green; `bench.json` moves within noise against the height
  fog task (`Svg.rasterize_256` is the resvg change of another epic).
- At 1280x720 over 300 headless frames the tone map costs 0.211 ms mean CPU frame time
  against 0.199 off and the bloom 0.331 against 0.158, the median of five runs.
- `tonemap.png` raises its sun to intensity 9: the hemisphere, the cone and the glow roll off
  instead of clipping flat. `bloom.png` blooms its 40 and 200 strength emitters at their real
  energy and `auto_exposure.png` moves only within the comparison tolerance.

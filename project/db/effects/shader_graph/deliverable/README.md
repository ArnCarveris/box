# Post process graph

- `box.effects` holds `Effect {shader, order, scale, enabled, color, depth, normals, motion}`
  and the trivial `ColorGrade {exposure, saturation}`. A view runs the effects its new
  `View.effect_list[8]` names, or every enabled effect in `order` when the list is empty;
  `View.effects` turns the chain off. Parameters come from the first other reflected struct
  on the effect entity, so a later effect adds a component and no renderer code.
- Extract builds the chain per view into `box_effect_t` and records the range and the union
  of the declared inputs on the view, so the packet says what the frame has to produce. A
  world whose effects are all off counts zero enabled effects and skips the build entirely,
  which keeps `Extract.static_1_changed` at 519 ns against 508 ns before the epic.
- The renderer draws a view with a chain into a pooled target, ping pongs between pooled
  targets of the size each `scale` asks for and writes the last pass into the view target;
  entries are matched by size and format and handed back after the chain, so the passes of
  a view and the views of a frame share textures. Depth binds the depth target, normals come
  from a second color attachment of the mesh pass through its `fs_normal` stage, motion from
  a reprojection of the depth with the previous view projection into RG16F. An input nobody
  asks for binds a one texel dummy and allocates nothing.
- 14 `Effects` cases, 104 `view` and 700 tests in total green. `Effects.extract_chain` is
  0.74 us for eight passes and nothing else in `bench.json` moves beyond noise. At 1280x720
  on `primitives.flecs` an empty chain is 0.185 ms mean CPU frame time and the color grade
  0.179 ms, so the pass is below the run to run spread on the CPU. `effect_toggle.png` is
  the grade at exposure 0.7 and saturation 1.8; the 26 existing scenes are bit identical.

# Light modulator plugins and cloud shadows: completion report

## What was built
- `LightModulator {shader, enabled}` on a light or on a child of one names a `Shader` whose WGSL
  defines `box_light_modulate(p, n, l, time, params)`. The registry splices every registration into
  the mesh shader through `generated/light_modulators.wgsl` as `box_light_modulate_<index>` and
  `box_light_modulate_all` multiplies the ones the modulator mask of a light names, after its
  shadow, so the indirect light is untouched. A light without one keeps the plain variant, a broken
  plugin becomes the identity and reports through its `ShaderStatus`, and several modulators on one
  light multiply.
- Parameters are any reflected component registered with `box_light_modulator_params`, written into
  a sixteen float block per modulator in member order and bound as `light_modulators`.
- Two shipped plugins under `etc/shaders/light/`: `clouds.wgsl` with `CloudShadows`, whose zero
  density follows the overcast of the world's `Weather`, and `caustics.wgsl` with `Caustics`, the
  example a project copies. Both live in `box.light` and need no engine change to be replaced.

## Verification
- 563 tests over `test/render` (386), `test/shader` (30) and `test/view` (147), all passing; the new
  `Modulators` suite holds 11 cases over the generated include, the packet params, the plain
  variant, the masks, several modulators, disabling, the broken plugin, reload and the weather.
- Scenes `cloud_shadows` and `caustics` baselined; all 89 visual scenes pass, the other 87 unchanged.
- `Modulators.extract_10k_modulated_sun` 245.5 us against `extract_10k_plain_sun` 245.4 us: the
  modulator adds nothing measurable to the extract. `python3 tools/lint.py` reports 0 problems.

## Not in this task
- The modulator scales the diffuse and specular sun term but not the glass specular lobe.
- No quality tier switch beyond `LightModulator.enabled`; there is no global quality setting yet.

# Bloom after exposure

- Reordered the built in stack: `auto_exposure` now runs at order 800, `bloom` at 850,
  `tonemap` stays at 900, so exposure multiplies the color before bloom sees it and
  its firefly clamp and knee are exposure relative. `fog` (700) and the rest keep
  their order. Fixed a bug the swap exposed: `box_effect_stack` matched parameter
  components to entities by array index, pinning `Bloom` and `AutoExposure` to their
  old slots; it now follows the array the effects run in.
- Added `Effects_builtin_stack_orders_exposure_before_bloom_before_tonemap`, checking
  fog < exposure < bloom < tonemap on `Effect.order` and the extracted pass sequence.
- 50 `view.Effects` (one new) and 140 `view` green, lint 0, `bench/render` (24 cases)
  unchanged. `bloom.flecs` and `tonemap.flecs` enable only their own effect, so their
  renders stayed bit identical; `showcase.flecs` runs exposure and bloom together and
  shifted (max delta 21, 234px over 16, under the fail threshold), so its baseline is
  refreshed and attached here. `showcase_off.flecs` and the other 41 are unchanged.
- Left out: no bench isolates the bloom pass cost by name; `bench/render` stands in.

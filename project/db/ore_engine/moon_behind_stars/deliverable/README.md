# Moon behind stars: completion report

## What was built
- `sky_moon_mask` in `etc/shaders/sky.wgsl` returns the coverage of the moon disk with the
  same antialiased edge `sky_moon` uses, and the star field is scaled by one minus that mask
  before it joins the sun, the moon and the sky, so no star shows through the moon.
- `celestial_moon_stars.flecs`: a night sky with the moon over a dense star field, baselined
  (`celestial_moon_stars.png`).

## Verification
- `test/render` 415 green (the Sky suite lives there), lint 0 problems.
- Every visual scene matches its baseline; `celestial_moon` and `time_of_day_night` differ by
  a few pixels along the moon rim, under the threshold, so they keep their baselines.

## Not in this task
- No bench: one multiply per sky pixel.

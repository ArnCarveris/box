# Moon drawn behind the stars

The star field adds on top of the moon disk in the sky shader, so stars show through the moon; the moon must occlude the stars behind it.

## Details

In `etc/shaders/sky.wgsl` the fragment shader sums the sun disk, the moon and the star field for
a sky pixel, so a star behind the moon adds its radiance on top of the moon disk and shows
through it. The moon is an occluder: inside its disk the stars contribute nothing, with the same
edge width `sky_moon` uses so the rim stays antialiased. Change the composition in the shader
only; `src/sky/celestial.c` and the packet stay as they are.

Verification: `test/sky` green, a visual scene with a night sky where the moon crosses a dense
star field (`celestial_moon.flecs` or a new `celestial_moon_stars.flecs`) baselined, and
`time_of_day_night.flecs` and every other scene unchanged or refreshed with a stated reason.

## References

- `etc/shaders/sky.wgsl`, `src/sky/celestial.c`, `project/db/sky/deliverable/README.md`

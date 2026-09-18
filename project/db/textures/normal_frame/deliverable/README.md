# Normal map tangent frame

`normal_bump.flecs` puts a real hemisphere beside a flat box carrying one large
hemispherical bump: under a sun from the upper left both are lit on the same side.

The frame sign was not the defect. `box_cotangent_frame` in `src/renderer/normal.h`,
the CPU twin of `perturb`, divides the uv gradients by area = n . (dp1 x dp2), which
is positive for a camera facing surface because the left handed world and WebGPU's
downward framebuffer y cancel. The sign is now computed, not assumed, and 8
`NormalMap` tests pin it for either y direction and either facing.

Only the generator's green changed: `bumps_normal.png` stored it along -v and lit its
bumps from below. The row flip stays. The new `textured.png` lights its bumps like the
sphere; tiling, svg_icons, pbr_spheres, lights_city and baked_room are bit identical.

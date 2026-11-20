// The fog of the FogVolumes of a view between the camera and a blended fragment, from the
// integrated froxel grid of the view (fog_volume.wgsl) at the uv and view depth of the fragment.
// The fog apply draw fogs the opaque image before the blended draws, so each of them adds the fog
// in front of itself. view.fog_grid.w is 0 for a view without fog volumes, which binds a texel of
// no light and full transmittance. The includer declares the View uniform as view (lights.wgsl)
// and the clamping trilinear sampler of binding 10 as aerial_sampler (aerial.wgsl).
#pragma once

#include "froxel_share.wgsl"

@group(0) @binding(29) var fog_grid: texture_3d<f32>;

// Light scattered toward the camera (rgb) and transmittance (a) of the fog volumes between the
// camera and a fragment at a framebuffer position and world position, from one trilinear tap of
// the grid. The grid holds them at the far side of each slice, so a depth in the first slice
// takes a share of it.
fn fog_froxels(pos: vec4f, world: vec3f) -> vec4f {
    if (view.fog_grid.w < 0.5) {
        return vec4f(0.0, 0.0, 0.0, 1.0);
    }

    let uv = pos.xy * view.viewport.zw;
    let depth = -(view.view * vec4f(world, 1.0)).z;
    let slices = view.fog_grid.z;
    let t = froxel_share_of(depth, view.fog_range.x, view.fog_range.y,
        view.fog_range.z > 0.5) * slices;
    let w = (max(t, 1.0) - 0.5) / slices;
    let far_ = textureSampleLevel(fog_grid, aerial_sampler, vec3f(uv * view.fog_grid.xy, w), 0.0);
    return mix(vec4f(0.0, 0.0, 0.0, 1.0), far_, saturate(t));
}

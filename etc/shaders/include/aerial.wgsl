// Aerial perspective volume of a view (atmosphere.wgsl), shared by the forward pass and the fog
// volumes: x and y the uv of the screen, z the distance from the camera, rgb the in scattered
// light and a the transmittance up to it. The includer declares the View uniform as view
// (lights.wgsl); view.aerial.w is 1 when the view applies it.
#pragma once

@group(0) @binding(9) var aerial_volume: texture_3d<f32>;
@group(0) @binding(10) var aerial_sampler: sampler;

// In scattered light (rgb) and transmittance (a) of the atmosphere between a fragment and the
// camera, from one trilinear tap of the aerial perspective volume.
fn aerial_sample(pos: vec4f, world: vec3f) -> vec4f {
    let uv = pos.xy * view.viewport.zw;
    let km = length(world - view.camera.xyz) * view.aerial.x;
    let slices = view.aerial.z;
    let w = (km * view.aerial.y * (slices - 1.0) + 0.5) / slices;
    return textureSampleLevel(aerial_volume, aerial_sampler, vec3f(uv, w), 0.0);
}

// Bindings every effect pass shares: the image so far (binding 0) and a linear clamp sampler
// (binding 1). A pass declares its uniforms at binding 2 and, when it uses them, the depth of
// the view at binding 3, a second image at binding 4, the adapted exposure at binding 5 and a
// 3D lookup table at binding 6.
#pragma once
#include "fullscreen.wgsl"

@group(0) @binding(0) var input_texture: texture_2d<f32>;
@group(0) @binding(1) var input_sampler: sampler;

// Luminance of a linear color.
fn luminance(c: vec3f) -> f32 {
    return dot(c, vec3f(0.2126, 0.7152, 0.0722));
}

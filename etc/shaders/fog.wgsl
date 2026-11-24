// Exponential height fog (include/height_fog.wgsl) applied to the image. In a view that renders
// the Atmosphere the fog refers to, the fog pass of atmosphere.wgsl wrote the color of the sky
// it takes into the 2x1 image at binding 4.
//
// When the view's first effect is the fog and it draws fog volumes, their apply draw in the
// forward pass applies the fog instead and the view skips this pass.
#include "include/effect.wgsl"
#include "include/height_fog.wgsl"

@group(0) @binding(2) var<uniform> fog: HeightFog;
@group(0) @binding(3) var depth_texture: texture_depth_2d;
// Texel 0: the ambient of the sky, texel 1: the glow toward the main light
@group(0) @binding(4) var sky_texture: texture_2d<f32>;

@fragment fn fs(v: FullscreenOut) -> @location(0) vec4f {
    let texel = vec2i(v.pos.xy);
    let src = textureLoad(input_texture, texel, 0);
    let depth = textureLoad(depth_texture, texel, 0);
    let result = height_fog(fog, v.uv, depth, sky_texture);
    let factor = result.a;
    let a = src.a * (1.0 - factor) + factor;
    return vec4f(src.rgb * (1.0 - factor) + result.rgb * factor, a);
}

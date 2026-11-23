// Vignette: multiplies the color by mix(1, color, v * intensity), where v is a smoothstep of the
// distance from the center in half diagonals, with the aspect corrected by the roundness. A
// black vignette darkens the edges, a colored one tints them. Alpha passes through.
#include "include/effect.wgsl"
#include "include/vignette.wgsl"

@group(0) @binding(2) var<uniform> vignette: Vignette;

@fragment fn fs(v: FullscreenOut) -> @location(0) vec4f {
    let src = textureLoad(input_texture, vec2i(v.pos.xy), 0);
    return vec4f(vignette_apply(vignette, src.rgb, v.uv), src.a);
}

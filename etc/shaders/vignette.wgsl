// Vignette: multiplies the color by mix(1, color, v * intensity), where v is a smoothstep of the
// distance from the center in half diagonals, with the aspect corrected by the roundness. A
// black vignette darkens the edges, a colored one tints them. Alpha passes through.
#include "include/effect.wgsl"

struct Vignette {
    // intensity, radius, smoothness, roundness
    params: vec4f,
    // xy: center in normalized image coordinates, z: width over height of the image
    shape: vec4f,
    // rgb: linear color of the vignette
    color: vec4f,
};

@group(0) @binding(2) var<uniform> vignette: Vignette;

// Factor at a uv, 0 inside the radius to 1 past the falloff; a hard step without smoothness.
fn vignette_factor(uv: vec2f) -> f32 {
    let radius = vignette.params.y;
    let smoothness = vignette.params.z;
    let sx = mix(1.0, vignette.shape.z, vignette.params.w);
    let p = (uv - vignette.shape.xy) * vec2f(sx, 1.0);
    let d = length(p) / (0.5 * sqrt(sx * sx + 1.0));
    if (smoothness <= 0.0) {
        return select(0.0, 1.0, d >= radius);
    }

    let t = clamp((d - radius) / smoothness, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

@fragment fn fs(v: FullscreenOut) -> @location(0) vec4f {
    let src = textureLoad(input_texture, vec2i(v.pos.xy), 0);
    let k = vignette_factor(v.uv) * vignette.params.x;
    return vec4f(mix(src.rgb, src.rgb * vignette.color.rgb, k), src.a);
}

// The vignette of vignette.wgsl as a function of its uniforms, which the tone map pass applies
// too when a vignette pass follows it (tonemap.wgsl).
#pragma once

struct Vignette {
    // intensity, radius, smoothness, roundness
    params: vec4f,
    // xy: center in normalized image coordinates, z: width over height of the image
    shape: vec4f,
    // rgb: linear color of the vignette
    color: vec4f,
};

// Factor at a uv, 0 inside the radius to 1 past the falloff; a hard step without smoothness.
fn vignette_factor(vignette: Vignette, uv: vec2f) -> f32 {
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

// A linear color under the vignette at a uv.
fn vignette_apply(vignette: Vignette, color: vec3f, uv: vec2f) -> vec3f {
    let k = vignette_factor(vignette, uv) * vignette.params.x;
    return mix(color, color * vignette.color.rgb, k);
}

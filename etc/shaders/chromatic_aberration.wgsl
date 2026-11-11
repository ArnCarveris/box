// Chromatic aberration: samples the input along the line from the pixel toward the center, from
// -offset to +offset, and weights each tap by a red, green, blue spectral ramp normalized so that
// the weights of each channel sum to 1; a white image stays white. The offset is
// intensity * width * (d / d_corner)^falloff, 0 at the center. Alpha is the one of the center tap.
#include "include/effect.wgsl"

struct ChromaticAberration {
    // x: largest offset in pixels, y: taps, z: falloff, w: 1 / distance to the farthest corner
    params: vec4f,
    // xy: center in pixels, zw: 1 / size of the image
    shape: vec4f,
};

@group(0) @binding(2) var<uniform> ca: ChromaticAberration;

// Unnormalized spectral ramp at s from 0 (-offset, red) to 1 (+offset, blue).
fn ca_ramp(s: f32) -> vec3f {
    return max(vec3f(1.0 - 2.0 * s, 1.0 - abs(2.0 * s - 1.0), 2.0 * s - 1.0), vec3f(0.0));
}

@fragment fn fs(v: FullscreenOut) -> @location(0) vec4f {
    let src = textureLoad(input_texture, vec2i(v.pos.xy), 0);
    let delta = v.pos.xy - ca.shape.xy;
    let d = length(delta);
    if (d <= 0.0 || ca.params.x <= 0.0) {
        return src;
    }

    let offset = ca.params.x * pow(d * ca.params.w, ca.params.z);
    let step = delta / d * offset;
    let n = i32(ca.params.y);
    let last = f32(n - 1);
    var color = vec3f(0.0);
    var sum = vec3f(0.0);
    for (var i = 0; i < n; i++) {
        let s = f32(i) / last;
        let w = ca_ramp(s);
        let uv = (v.pos.xy + step * (2.0 * s - 1.0)) * ca.shape.zw;
        color += textureSampleLevel(input_texture, input_sampler, uv, 0.0).rgb * w;
        sum += w;
    }

    return vec4f(color / sum, src.a);
}

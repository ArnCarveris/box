// Opaque snapshot of transmission (gpu_transmission.c): every mip above 0 of the snapshot of a
// view renders fullscreen from the mip below it with the 13 tap downsample of Jimenez (Next
// Generation Post Processing in Call of Duty: Advanced Warfare), through a linear clamp
// sampler. Transmissive draws sample the pyramid at a level that follows their roughness.
#include "include/fullscreen.wgsl"

@group(0) @binding(0) var src_tex: texture_2d<f32>;
@group(0) @binding(1) var src_smp: sampler;

@fragment fn fs_main(in: FullscreenOut) -> @location(0) vec4f {
    let ps = 1.0 / vec2f(textureDimensions(src_tex));
    let pl = 2.0 * ps;
    let ns = -1.0 * ps;
    let nl = -2.0 * ps;
    let uv = in.uv;
    let a = textureSampleLevel(src_tex, src_smp, uv + vec2f(nl.x, pl.y), 0.0).rgb;
    let b = textureSampleLevel(src_tex, src_smp, uv + vec2f(0.00, pl.y), 0.0).rgb;
    let c = textureSampleLevel(src_tex, src_smp, uv + vec2f(pl.x, pl.y), 0.0).rgb;
    let d = textureSampleLevel(src_tex, src_smp, uv + vec2f(nl.x, 0.00), 0.0).rgb;
    let e = textureSampleLevel(src_tex, src_smp, uv, 0.0).rgb;
    let f = textureSampleLevel(src_tex, src_smp, uv + vec2f(pl.x, 0.00), 0.0).rgb;
    let g = textureSampleLevel(src_tex, src_smp, uv + vec2f(nl.x, nl.y), 0.0).rgb;
    let h = textureSampleLevel(src_tex, src_smp, uv + vec2f(0.00, nl.y), 0.0).rgb;
    let i = textureSampleLevel(src_tex, src_smp, uv + vec2f(pl.x, nl.y), 0.0).rgb;
    let j = textureSampleLevel(src_tex, src_smp, uv + vec2f(ns.x, ps.y), 0.0).rgb;
    let k = textureSampleLevel(src_tex, src_smp, uv + vec2f(ps.x, ps.y), 0.0).rgb;
    let l = textureSampleLevel(src_tex, src_smp, uv + vec2f(ns.x, ns.y), 0.0).rgb;
    let m = textureSampleLevel(src_tex, src_smp, uv + vec2f(ps.x, ns.y), 0.0).rgb;
    var sample = (a + c + g + i) * 0.03125;
    sample += (b + d + f + h) * 0.0625;
    sample += (e + j + k + l + m) * 0.125;
    return vec4f(sample, 1.0);
}

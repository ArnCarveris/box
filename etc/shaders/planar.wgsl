// Pyramid of the planar reflections (gpu_planar.c): fs_copy writes mip 0 of a layer of the
// reflection array of a view from the image a reflection rendered, its colors multiplied by its
// alpha, its coverage, which is 0 where it drew nothing; fs_down renders every mip above from the
// one below with the 13 tap downsample of Jimenez, as transmission.wgsl. Both draw into the
// corner of the layer the image of the reflection covers (the viewport), so a pixel of mip m + 1
// reads the four of mip m at twice its position.
#include "include/fullscreen.wgsl"

@group(0) @binding(0) var src_tex: texture_2d<f32>;
@group(0) @binding(1) var src_smp: sampler;

@fragment fn fs_copy(in: FullscreenOut) -> @location(0) vec4f {
    let size = vec2i(textureDimensions(src_tex));
    let c = textureLoad(src_tex, clamp(vec2i(in.pos.xy), vec2i(0), size - 1), 0);
    let a = clamp(c.a, 0.0, 1.0);
    return vec4f(max(c.rgb, vec3f(0.0)) * a, a);
}

@fragment fn fs_down(in: FullscreenOut) -> @location(0) vec4f {
    let ps = 1.0 / vec2f(textureDimensions(src_tex));
    let pl = 2.0 * ps;
    let ns = -1.0 * ps;
    let nl = -2.0 * ps;
    let uv = in.pos.xy * 2.0 * ps;
    let a = textureSampleLevel(src_tex, src_smp, uv + vec2f(nl.x, pl.y), 0.0);
    let b = textureSampleLevel(src_tex, src_smp, uv + vec2f(0.00, pl.y), 0.0);
    let c = textureSampleLevel(src_tex, src_smp, uv + vec2f(pl.x, pl.y), 0.0);
    let d = textureSampleLevel(src_tex, src_smp, uv + vec2f(nl.x, 0.00), 0.0);
    let e = textureSampleLevel(src_tex, src_smp, uv, 0.0);
    let f = textureSampleLevel(src_tex, src_smp, uv + vec2f(pl.x, 0.00), 0.0);
    let g = textureSampleLevel(src_tex, src_smp, uv + vec2f(nl.x, nl.y), 0.0);
    let h = textureSampleLevel(src_tex, src_smp, uv + vec2f(0.00, nl.y), 0.0);
    let i = textureSampleLevel(src_tex, src_smp, uv + vec2f(pl.x, nl.y), 0.0);
    let j = textureSampleLevel(src_tex, src_smp, uv + vec2f(ns.x, ps.y), 0.0);
    let k = textureSampleLevel(src_tex, src_smp, uv + vec2f(ps.x, ps.y), 0.0);
    let l = textureSampleLevel(src_tex, src_smp, uv + vec2f(ns.x, ns.y), 0.0);
    let m = textureSampleLevel(src_tex, src_smp, uv + vec2f(ps.x, ns.y), 0.0);
    var sample = (a + c + g + i) * 0.03125;
    sample += (b + d + f + h) * 0.0625;
    sample += (e + j + k + l + m) * 0.125;
    return sample;
}

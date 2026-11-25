// The render origin, which the world positions of a pass are relative to: whole world units and
// the rest of a unit, split on the CPU in double precision (box_gpu_origin_split), from the View
// or the Cascade uniforms. The rest comes in steps of 1 / 1024 of a unit, which moves the
// patterns by less than a millimeter and keeps them continuous across a move by whole units. A
// pattern of the world position adds the phase of the origin to that of the position relative
// to it, so it stays where it is in the world across a move of the origin and precise far from
// the origin of the world. An origin of 0 has a phase of exactly 0.
#pragma once

struct RenderOrigin {
    whole: vec3i,
    rest: vec3f,
};

// Bits of the rest of a unit of each axis (BOX_GPU_ORIGIN_REST_BITS).
const ORIGIN_REST_BITS: u32 = 10u;

// The origin of View.origin or Cascade.origin: the whole units in xyz, the rest of each in w,
// ORIGIN_REST_BITS each, x lowest.
fn render_origin(packed: vec4i) -> RenderOrigin {
    let rests = bitcast<u32>(packed.w);
    let mask = (1u << ORIGIN_REST_BITS) - 1u;
    let r = vec3u(rests, rests >> ORIGIN_REST_BITS, rests >> (2u * ORIGIN_REST_BITS)) &
        vec3u(mask);
    return RenderOrigin(packed.xyz, vec3f(r) / f32(1u << ORIGIN_REST_BITS));
}

// The fraction of the origin times k, per axis: fract(origin * k), to about 1e-5. The whole
// units go in bytes, each times the fraction of k times its weight, which is exact in f32, so
// no product is large enough to round away the fraction.
fn origin_phase(o: RenderOrigin, k: vec3f) -> vec3f {
    var p = fract(o.rest * k);
    var w = o.whole;
    var s = k;
    for (var j = 0u; j < 4u; j++) {
        // The top byte keeps the sign
        let b = select(w & vec3i(255), w, j == 3u);
        p = fract(p + fract(vec3f(b) * fract(s)));
        w = w >> vec3u(8u);
        s = s * 256.0;
    }
    return p;
}

// The fraction of the dot product of the origin and k: fract(dot(origin, k)).
fn origin_phase_dot(o: RenderOrigin, k: vec3f) -> f32 {
    let p = origin_phase(o, k);
    return fract(p.x + p.y + p.z);
}

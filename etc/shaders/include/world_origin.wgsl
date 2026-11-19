// The absolute position of the local origin (WorldOrigin) as the world space patterns take it:
// whole world units and the rest of a unit, split on the CPU in double precision
// (box_gpu_origin_split), from the View or the Cascade uniforms. The rest comes in steps of
// 1 / 1024 of a unit, which moves the patterns by less than a millimeter and keeps them
// continuous across a shift by whole units. A pattern of the absolute position adds the phase of
// the origin to that of the local position, so it stays where it is in the world across a shift
// of the origin and precise far from it. An origin of 0 has a phase of exactly 0, so a scene
// that never shifts renders as before.
#pragma once

struct WorldOrigin {
    whole: vec3i,
    rest: vec3f,
};

// Bits of the rest of a unit of each axis (BOX_GPU_ORIGIN_REST_BITS).
const ORIGIN_REST_BITS: u32 = 10u;

// The origin of View.origin or Cascade.origin: the whole units in xyz, the rest of each in w,
// ORIGIN_REST_BITS each, x lowest.
fn world_origin(packed: vec4i) -> WorldOrigin {
    let rests = bitcast<u32>(packed.w);
    let mask = (1u << ORIGIN_REST_BITS) - 1u;
    let r = vec3u(rests, rests >> ORIGIN_REST_BITS, rests >> (2u * ORIGIN_REST_BITS)) &
        vec3u(mask);
    return WorldOrigin(packed.xyz, vec3f(r) / f32(1u << ORIGIN_REST_BITS));
}

// The fraction of the origin times k, per axis: fract(origin * k), to about 1e-5. The whole
// units go in bytes, each times the fraction of k times its weight, which is exact in f32, so
// no product is large enough to round away the fraction.
fn origin_phase(o: WorldOrigin, k: vec3f) -> vec3f {
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
fn origin_phase_dot(o: WorldOrigin, k: vec3f) -> f32 {
    let p = origin_phase(o, k);
    return fract(p.x + p.y + p.z);
}

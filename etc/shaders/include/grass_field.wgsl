// Large scale variation of grass over the world, a function of the absolute world x and z that
// the grass blades (grass_place.wgsl) and the far field of a terrain share, so the field keeps
// its colors where the blades end. Patches of drier, yellower grass tens of meters across and
// lusher, darker ones between them, and the height of the blades rising and falling over a few
// meters to tens of meters. Self contained, so any shader can include it.
//
// The position is absolute: a shader with local positions adds the world origin first (for
// example whole + rest of world_origin.wgsl). In f32 the patterns stay smooth to about 100 km
// from the absolute origin.
#pragma once

// Wavelengths in meters of the layers of the dry patches, the lush patches and the height
const GRASS_FIELD_DRY: f32 = 41.0;
const GRASS_FIELD_DRY_DETAIL: f32 = 11.0;
const GRASS_FIELD_LUSH: f32 = 23.0;
const GRASS_FIELD_TALL: f32 = 17.0;
const GRASS_FIELD_TALL_DETAIL: f32 = 4.5;

// Factors of the color of dry and lush grass
const GRASS_FIELD_DRY_TINT: vec3f = vec3f(1.35, 1.12, 0.62);
const GRASS_FIELD_LUSH_TINT: vec3f = vec3f(0.78, 0.94, 0.86);

// Share of their height the shortest blades of the variation keep
const GRASS_FIELD_SHORT: f32 = 0.55;

// Hash of a lattice point with a salt, from 0 to 1.
fn grass_field_hash(c: vec2i, salt: u32) -> f32 {
    var h = (bitcast<u32>(c.x) * 0x8da6b343u) ^ (bitcast<u32>(c.y) * 0xd8163841u) ^
        (salt * 0xcb1ab31fu);
    h = h ^ (h >> 16u);
    h = h * 0x7feb352du;
    h = h ^ (h >> 15u);
    h = h * 0x846ca68bu;
    h = h ^ (h >> 16u);
    return f32(h >> 8u) * (1.0 / 16777216.0);
}

// Smooth value noise of a position in lattice units, from 0 to 1.
fn grass_field_noise(p: vec2f, salt: u32) -> f32 {
    let i = floor(p);
    let f = p - i;
    let c = vec2i(i);
    let u = f * f * (3.0 - 2.0 * f);
    let a = grass_field_hash(c, salt);
    let b = grass_field_hash(c + vec2i(1, 0), salt);
    let d = grass_field_hash(c + vec2i(0, 1), salt);
    let e = grass_field_hash(c + vec2i(1, 1), salt);
    return mix(mix(a, b, u.x), mix(d, e, u.x), u.y);
}

// The variation of grass at an absolute world x and z: in rgb the factor of its color, in w the
// factor of its height, 0.55 to 1. An amount of 0 is no variation, a factor of 1, and 1 the full
// variation.
fn grass_field_variation(xz: vec2f, amount: f32) -> vec4f {
    let dry = grass_field_noise(xz / GRASS_FIELD_DRY, 1u) * 0.7 +
        grass_field_noise(xz / GRASS_FIELD_DRY_DETAIL, 2u) * 0.3;
    let lush = grass_field_noise(xz / GRASS_FIELD_LUSH, 3u);
    let tall = grass_field_noise(xz / GRASS_FIELD_TALL, 4u) * 0.7 +
        grass_field_noise(xz / GRASS_FIELD_TALL_DETAIL, 5u) * 0.3;
    let d = smoothstep(0.45, 0.8, dry);
    let l = smoothstep(0.45, 0.85, lush) * (1.0 - d);
    let tint = mix(vec3f(1.0), GRASS_FIELD_DRY_TINT, d) * mix(vec3f(1.0), GRASS_FIELD_LUSH_TINT, l);
    let height = mix(GRASS_FIELD_SHORT, 1.0, smoothstep(0.2, 0.75, tall)) * mix(1.0, 0.85, d);
    let a = saturate(amount);
    return vec4f(mix(vec3f(1.0), tint, a), mix(1.0, height, a));
}

// The color of a field of grass seen from afar at an absolute world x and z: the mean of the
// linear colors of its blades from the base to the tip, weighted toward the tips that cover it
// from above, times the variation there.
fn grass_field_color(xz: vec2f, base: vec3f, tip: vec3f, amount: f32) -> vec3f {
    return mix(base, tip, 0.6) * grass_field_variation(xz, amount).rgb;
}

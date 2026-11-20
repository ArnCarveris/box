// Wind sway of the vertices of a WindSway material (Scene.wind), the same in every pass that
// places them: the forward and depth prepass vertex stages of the mesh shaders, the shadow
// casters and the wireframe. The includer declares the material table (material.wgsl).
//
// The patterns over the world (the gust field, the wave of the sway and the base) come from the
// CPU packed for the absolute position of the local origin (WorldOrigin), the phase of the
// flutter adds that of the origin, so plants sway the same across a shift of the origin.
//
// The main bend is a function of the world position of a vertex only, so every shape of a plant
// with the same bend, height and base moves together without cracks: the height above the base
// over the height, clamped and squared, times the bend and the strength of the wind there. The
// strength is the speed times a gust factor read from a smooth noise field of gust patches that
// travels along the wind (its wrapped offset, wind_gust.wgsl, which the particles share), and
// the plants sway around their lean in a slow wave along the wind. A vertex that moves sideways
// drops so the length to the base is about kept. The flutter adds a fast per vertex motion that grows with the distance from the origin
// of the shape over the flutter reach.
//
// A material with WIND_PIVOTS whose mesh has per vertex data (the _data entry points set it with
// wind_vertex_data) first turns every vertex of a branch rigidly about the pivot of the branch
// (data0.xyz in object space), by an angle read at the pivot, and a branch of level 2 or more
// then with its parent about the pivot of the parent (data1.xyz), so it stays attached to it.
// The level and the flutter weight of the vertex are packed in data0.w, the stiffness of the
// branch and of its parent in data1.w (WindSway and box_wind_sway_vertex in material.h). It then
// bends the vertex by its rest position as above; the flutter is weighted by the flutter weight.
// A material with WIND_FOLIAGE does not read data1: a vertex turns about the pivot of its twig
// with a stiffness of 0 and without its parent, like the twig, so a leaf stays on it.
#pragma once

#include "wind_gust.wgsl"

// Relative amplitudes of the sway along and across the wind (BOX_RENDER_WIND_SWAY and
// BOX_RENDER_WIND_CROSS).
const WIND_SWAY: f32 = 0.25;
const WIND_CROSS: f32 = 0.12;

// Wind speed of the full flutter (BOX_WIND_FULL_SPEED).
const WIND_FULL: f32 = 10.0;

// Flag of a record with a WindSway (BOX_RENDER_WIND_PRESENT).
const WIND_PRESENT: u32 = 1u;

const WIND_TAU: f32 = 6.283185307179586;

// Flag of a record with pivots (BOX_RENDER_WIND_PIVOTS).
const WIND_PIVOTS: u32 = 64u;

// Flag of a record with pivots that reads only data0, the foliage of a MeshGraph whose data1
// holds the seed, hue, growth and occlusion of its cards (BOX_RENDER_WIND_FOLIAGE).
const WIND_FOLIAGE: u32 = 2048u;

// Deepest branch level a record with pivots tells apart (BOX_MATERIAL_SWAY_LEVELS).
const WIND_LEVELS: f32 = 4.0;

// Lean of a branch around which it swings, and the relative amplitude of its swing about the
// wind axis (BOX_RENDER_WIND_BRANCH_CROSS).
const WIND_BRANCH_LEAN: f32 = 0.6;
const WIND_BRANCH_CROSS: f32 = 0.3;

// Per vertex data of the vertex being placed, set by the _data entry points (wind_vertex_data);
// without it a WIND_PIVOTS material sways as without pivots.
var<private> wind_data: bool = false;
var<private> wind_data0: vec4f = vec4f(0.0);
var<private> wind_data1: vec4f = vec4f(0.0);

// Make the per vertex data of the vertex being placed available to wind_move.
fn wind_vertex_data(data0: vec4f, data1: vec4f) {
    wind_data = true;
    wind_data0 = data0;
    wind_data1 = data1;
}

// Axis the phase of the flutter of a vertex runs along (BOX_RENDER_WIND_FLUTTER_AXIS); its
// dot product with the absolute position of the local origin is wrapped to 20 pi
// (WIND_FLUTTER_PERIOD, BOX_RENDER_WIND_FLUTTER_PERIOD), the period of the three sines of the
// flutter in it.
const WIND_FLUTTER_AXIS: vec3f = vec3f(3.1, 1.7, 2.3);
const WIND_FLUTTER_PERIOD: f32 = 62.83185307179586;

// World offset of a vertex and the derivative of its horizontal offset along world Y, which the
// normal bends by, and the turn of its branch about its pivot as a unit quaternion, which the
// normal turns by first (the identity without one).
struct WindMove {
    offset: vec3f,
    slope: vec3f,
    turn: vec4f,
};

// A vector turned by a unit quaternion.
fn wind_rotate(v: vec3f, q: vec4f) -> vec3f {
    let t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

// The turn by a then by b, as one unit quaternion.
fn wind_compose(b: vec4f, a: vec4f) -> vec4f {
    return vec4f(b.w * a.xyz + a.w * b.xyz + cross(b.xyz, a.xyz), b.w * a.w - dot(b.xyz, a.xyz));
}

// Whether a material reads the pivots of the per vertex data of the vertex being placed.
fn wind_pivots(m: Material) -> bool {
    return wind_data && (m.flags & WIND_PIVOTS) != 0u;
}

// Phase of a branch, 0..1, hashed from the bits of its object space pivot, so the vertices of a
// branch and of its children (whose data1.xyz is the same pivot) agree.
fn wind_branch_phase(pivot: vec3f) -> f32 {
    var h = dot(bitcast<vec3u>(pivot), vec3u(0x8da6b343u, 0xd8163841u, 0xcb1ab31fu));
    h = (h ^ (h >> 16u)) * 0x7feb352du;
    h = (h ^ (h >> 15u)) * 0x846ca68bu;
    h = h ^ (h >> 16u);
    return f32(h >> 8u) / 16777216.0;
}

// The turn of a branch of a level (1 or more), a stiffness and a phase about its world pivot as a
// unit quaternion, by the wind at the pivot, so every vertex of the branch turns alike. The
// branch leans about the horizontal axis across the wind and swings around its lean at level
// times the branch frequency, offset by its phase and travelling along the wind like the sway,
// and swings less about the wind axis.
fn wind_turn(pivot: vec3f, level: f32, stiffness: f32, phase0: f32, m: Material) -> vec4f {
    var result = vec4f(0.0, 0.0, 0.0, 1.0);
    let free = 1.0 - clamp(stiffness, 0.0, 1.0);
    if (level >= 1.0 && free > 0.0 && m.wind_branch != 0.0) {
        let dir = m.wind_direction;
        let strength = m.wind_speed * wind_gust(pivot.xz, m.wind_offset, m.wind_frequency,
            m.wind_gust);
        let amount = m.wind_branch * free * clamp(strength / WIND_FULL, 0.0, 1.0);
        let phase = WIND_TAU * (level * m.wind_branch_phase + phase0) -
            0.5 * dot(pivot.xz, dir) * m.wind_frequency;
        let along = WIND_BRANCH_LEAN + (1.0 - WIND_BRANCH_LEAN) * sin(phase);
        let cross = WIND_BRANCH_CROSS * sin(2.0 * phase + 1.3);
        // A turn about (dir.y, 0, -dir.x) moves the vertices above the pivot along the wind, one
        // about the wind axis across it
        let r = vec3f(dir.y, 0.0, -dir.x) * (amount * along) +
            vec3f(dir.x, 0.0, dir.y) * (amount * cross);
        let angle = length(r);
        if (angle > 0.0) {
            result = vec4f(r * (sin(0.5 * angle) / angle), cos(0.5 * angle));
        }
    }
    return result;
}

// The stiffness of the branch of the vertex being placed (x) and of its parent (y), packed in
// data1.w (box_wind_sway_vertex).
fn wind_stiffness() -> vec2f {
    let q = max(round(wind_data1.w), 0.0);
    let parent = floor(q / 256.0);
    return clamp(vec2f(q - 256.0 * parent, parent) / 255.0, vec2f(0.0), vec2f(1.0));
}

// Whether a material moves its vertices: it has a WindSway and the wind blows.
fn wind_sways(m: Material) -> bool {
    return (m.flags & WIND_PRESENT) != 0u && m.wind_speed > 0.0;
}

// The move of a vertex at a world position of a shape placed by model, whose origin is its
// translation, by the WindSway and the wind of its material; wind_sways(m) is true. absolute is
// the absolute position of the local origin, which the phase of the flutter takes.
fn wind_move(world: vec3f, model: mat4x4f, m: Material, absolute: WorldOrigin) -> WindMove {
    let origin = model[3].xyz;
    let pivots = wind_pivots(m);
    let dir = m.wind_direction;
    let across = vec2f(-dir.y, dir.x);
    let strength = m.wind_speed * wind_gust(world.xz, m.wind_offset, m.wind_frequency,
        m.wind_gust);
    let phase = WIND_TAU * m.wind_sway - 0.5 * dot(world.xz, dir) * m.wind_frequency;
    let along = 1.0 + WIND_SWAY * sin(phase);
    let cross = WIND_CROSS * sin(2.0 * phase + 1.3);

    let h = world.y - m.wind_base;
    let t = clamp(h / m.wind_height, 0.0, 1.0);
    let amount = m.wind_bend * strength;
    let bend = amount * t * t;
    let d = dir * (bend * along) + across * (bend * cross);
    let rate = select(0.0, amount * 2.0 * t / m.wind_height, h > 0.0 && h < m.wind_height);
    let ds = dir * (rate * along) + across * (rate * cross);
    let drop = select(0.0, min(0.5 * dot(d, d) / max(h, 1e-3), h), h > 0.0);

    var o: WindMove;
    o.offset = vec3f(d.x, -drop, d.y);
    o.slope = vec3f(ds.x, 0.0, ds.y);
    o.turn = vec4f(0.0, 0.0, 0.0, 1.0);
    // The level in the integer part of data0.w, the flutter weight times 0.5 in its fraction
    let packed = max(wind_data0.w, 0.0);
    let weight = clamp(2.0 * fract(packed), 0.0, 1.0);
    if (pivots) {
        // A branch turns about its pivot, then with its parent about the pivot of the parent
        let level = min(floor(packed), WIND_LEVELS);
        let foliage = (m.flags & WIND_FOLIAGE) != 0u;
        let stiffness = select(wind_stiffness(), vec2f(0.0), foliage);
        let pivot = (model * vec4f(wind_data0.xyz, 1.0)).xyz;
        o.turn = wind_turn(pivot, level, stiffness.x, wind_branch_phase(wind_data0.xyz), m);
        var moved = pivot + wind_rotate(world - pivot, o.turn);
        if (level >= 2.0 && !foliage) {
            let parent = (model * vec4f(wind_data1.xyz, 1.0)).xyz;
            let turn = wind_turn(parent, level - 1.0, stiffness.y,
                wind_branch_phase(wind_data1.xyz), m);
            moved = parent + wind_rotate(moved - parent, turn);
            o.turn = wind_compose(turn, o.turn);
        }
        o.offset += moved - world;
    }
    if (m.wind_flutter > 0.0) {
        var reach = clamp(length(world - origin) / m.wind_reach, 0.0, 1.0);
        if (pivots) {
            reach = weight;
        }
        let size = m.wind_flutter * clamp(strength / WIND_FULL, 0.0, 1.0) * reach;
        let q = dot(world, WIND_FLUTTER_AXIS) + WIND_FLUTTER_PERIOD *
            origin_phase_dot(absolute, WIND_FLUTTER_AXIS / WIND_FLUTTER_PERIOD);
        let a = WIND_TAU * m.wind_flutter_phase;
        o.offset += size * vec3f(0.4 * sin(a + q), sin(2.0 * a + 1.3 * q),
            0.4 * sin(a + 0.7 * q + 1.0));
    }
    return o;
}

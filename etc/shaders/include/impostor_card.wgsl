// Cards of the impostors (ImpostorDraw, render/impostor.c): the card of an instance is a square
// through the center of the bounding sphere of the bake, facing the viewer, that shows the
// frames of the octahedral grid of the atlases nearest the direction of the viewer. The mesh
// and shadow shaders draw it with their _impostor entry points; the math is shared here.
#pragma once
#include "instance.wgsl"

// Alpha below which a card drops a fragment
const IMPOSTOR_CUTOFF: f32 = 0.5;

// Cosine below which a frame is seen too edge on for the projection onto its plane
const IMPOSTOR_GRAZING: f32 = 0.05;

// The grid of the bake of an impostor, from the per vertex data of the card mesh of its prefab:
// data0 holds the views per axis, 1 for a hemisphere grid, the radius of the bounding sphere over
// the half size of the card and that half size, data1 the center of the sphere in the frame of
// the root.
struct ImpostorGrid {
    frames: u32,
    hemisphere: bool,
    ratio: f32,
    half: f32,
    center: vec3f,
};

fn impostor_grid(d: VertexData) -> ImpostorGrid {
    return ImpostorGrid(u32(max(round(d.data0.x), 2.0)), d.data0.y > 0.5, d.data0.z,
        max(d.data0.w, 1e-6), d.data1.xyz);
}

fn impostor_sign(v: f32) -> f32 {
    return select(-1.0, 1.0, v >= 0.0);
}

// Direction toward the viewer of view (x, y) of a grid, as box_impostor_frame_direction.
fn impostor_frame_direction(frames: u32, hemisphere: bool, cell: vec2u) -> vec3f {
    let n = max(f32(frames) - 1.0, 1.0);
    let e = 2.0 * vec2f(cell) / n - 1.0;
    var d: vec3f;
    if (hemisphere) {
        let t = vec2f((e.x + e.y) * 0.5, (e.x - e.y) * 0.5);
        d = vec3f(t.x, 1.0 - abs(t.x) - abs(t.y), t.y);
    } else {
        d = vec3f(e.x, 1.0 - abs(e.x) - abs(e.y), e.y);
        if (d.y < 0.0) {
            let ox = d.x;
            d.x = (1.0 - abs(d.z)) * impostor_sign(ox);
            d.z = (1.0 - abs(ox)) * impostor_sign(d.z);
        }
    }
    return normalize(d);
}

// Position of a direction toward the viewer in a grid, in cells from 0 to frames - 1, which
// box_impostor_frame_of rounds to the nearest view.
fn impostor_grid_position(frames: u32, hemisphere: bool, dir: vec3f) -> vec2f {
    var d = dir;
    if (hemisphere) {
        d.y = max(d.y, 0.0);
    }
    let l1 = abs(d.x) + abs(d.y) + abs(d.z);
    d = select(vec3f(0.0, 1.0, 0.0), d / max(l1, 1e-12), l1 > 0.0);
    var e = d.xz;
    if (hemisphere) {
        e = vec2f(d.x + d.z, d.x - d.z);
    } else if (d.y < 0.0) {
        e = vec2f((1.0 - abs(d.z)) * impostor_sign(d.x), (1.0 - abs(d.x)) * impostor_sign(d.z));
    }
    let n = max(f32(frames) - 1.0, 1.0);
    return clamp((e + 1.0) * 0.5 * n, vec2f(0.0), vec2f(n));
}

// Right and up axes of the image of a view from direction d, as the bake frames it: up along
// +Y, or -Z for a view straight down or up.
fn impostor_axes(d: vec3f) -> mat2x3f {
    var up = vec3f(0.0, 1.0, 0.0);
    if (abs(d.y) > 0.999) {
        up = vec3f(0.0, 0.0, -1.0);
    }
    let right = normalize(cross(-d, up));
    return mat2x3f(right, cross(right, -d));
}

// A world direction in the frame of the root of a model matrix, unit: the inverse of its linear
// part up to scale. A hemisphere grid sees nothing below the horizon, so a direction from below
// is taken as on the horizon, which keeps the card upright around the up axis.
fn impostor_local_direction(model: mat4x4f, dir: vec3f, hemisphere: bool) -> vec3f {
    let c0 = model[0].xyz;
    let c1 = model[1].xyz;
    let c2 = model[2].xyz;
    let flip = select(1.0, -1.0, dot(c0, cross(c1, c2)) < 0.0);
    var d = vec3f(dot(cross(c1, c2), dir), dot(cross(c2, c0), dir), dot(cross(c0, c1), dir));
    d *= flip;
    if (hemisphere) {
        d.y = max(d.y, 0.0);
    }
    return select(vec3f(0.0, 0.0, 1.0), normalize(d), dot(d, d) > 1e-12);
}

// A normal in the frame of the root of a model matrix in the world, unit.
fn impostor_world_normal(model: mat4x4f, n: vec3f) -> vec3f {
    let c0 = model[0].xyz;
    let c1 = model[1].xyz;
    let c2 = model[2].xyz;
    let flip = select(1.0, -1.0, dot(c0, cross(c1, c2)) < 0.0);
    let w = mat3x3f(cross(c1, c2), cross(c2, c0), cross(c0, c1)) * n * flip;
    return select(vec3f(0.0, 1.0, 0.0), normalize(w), dot(w, w) > 1e-12);
}

// Margin of a card around the box of the bake as seen along its direction, in half sizes of the
// card, for the parallax and the dilation of the views
const IMPOSTOR_MARGIN: f32 = 0.03;

// A corner of the card facing direction e, relative to the center in the frame of the root, from
// the UV of the vertex: 0 and 1 to the left and right, bottom and top. The card covers the box
// of the bake with half extents extent as seen along e, no more than the views frame: a ray
// along e outside of it misses everything the views show, so its fragments would be dropped.
fn impostor_corner(e: vec3f, uv: vec2f, half: f32, extent: vec3f) -> vec3f {
    let axes = impostor_axes(e);
    let size = vec2f(dot(abs(axes[0]), extent), dot(abs(axes[1]), extent));
    let c = (uv * 2.0 - 1.0) * min(size + IMPOSTOR_MARGIN * half, vec2f(half));
    return axes[0] * c.x + axes[1] * c.y;
}

// The three views around a direction and their weights: the corners of the triangle of the
// grid cell the direction falls in, weighted by its barycentric coordinates. A cell packs x in
// its low and y in its high half.
struct ImpostorFrames {
    cells: vec3u,
    weights: vec3f,
};

fn impostor_frames(frames: u32, hemisphere: bool, dir: vec3f) -> ImpostorFrames {
    let g = impostor_grid_position(frames, hemisphere, dir);
    let base = min(floor(g), vec2f(max(f32(frames) - 2.0, 0.0)));
    let f = clamp(g - base, vec2f(0.0), vec2f(1.0));
    var second = base + vec2f(0.0, 1.0);
    var w = vec3f(1.0 - f.y, f.y - f.x, f.x);
    if (f.x >= f.y) {
        second = base + vec2f(1.0, 0.0);
        w = vec3f(1.0 - f.x, f.x - f.y, f.y);
    }
    let third = base + vec2f(1.0);
    return ImpostorFrames(vec3u(impostor_pack(base), impostor_pack(second),
        impostor_pack(third)), w);
}

// The view of a grid nearest a direction, packed.
fn impostor_nearest(frames: u32, hemisphere: bool, dir: vec3f) -> u32 {
    let g = impostor_grid_position(frames, hemisphere, dir);
    return impostor_pack(clamp(round(g), vec2f(0.0), vec2f(f32(frames) - 1.0)));
}

fn impostor_pack(cell: vec2f) -> u32 {
    return u32(cell.x) | (u32(cell.y) << 16u);
}

fn impostor_unpack(cell: u32) -> vec2u {
    return vec2u(cell & 0xFFFFu, cell >> 16u);
}

// Coordinates in the image of the view from direction d of the point q of the card, relative to
// the center, seen along e: where the ray through q meets the plane of the view through the
// center, over the half size of the card, -1 to 1 across the image.
fn impostor_plane(q: vec3f, e: vec3f, d: vec3f, half: f32) -> vec2f {
    let t = -dot(q, d) / max(dot(e, d), IMPOSTOR_GRAZING);
    let p = q + e * t;
    let axes = impostor_axes(d);
    return vec2f(dot(p, axes[0]), dot(p, axes[1])) / half;
}

// UV in an atlas of image coordinates of a view of a grid, held inside the cell.
fn impostor_uv(frames: u32, cell: vec2u, a: vec2f) -> vec2f {
    let local = clamp(vec2f(0.5 + 0.5 * a.x, 0.5 - 0.5 * a.y), vec2f(0.0), vec2f(1.0));
    return (vec2f(cell) + local) / f32(frames);
}

// Height of a texel of the depth atlas above the plane of its view toward the viewer of the
// view, in the units of the root: the depth runs from the near side of the sphere, 0, to its
// far side, 1.
fn impostor_height(depth: f32, half: f32, ratio: f32) -> f32 {
    return ratio * half * (1.0 - 2.0 * depth);
}

// Image coordinates of a view moved to the surface along the ray of the viewer, from the height
// of the surface at the coordinates of the plane: the parallax that lines up the surfaces of
// the blended views, so they do not ghost.
fn impostor_parallax(a: vec2f, e: vec3f, d: vec3f, height: f32, half: f32) -> vec2f {
    let axes = impostor_axes(d);
    let t = height / max(dot(e, d), IMPOSTOR_GRAZING);
    return a + vec2f(dot(e, axes[0]), dot(e, axes[1])) * t / half;
}

// Mip of the atlases for the screen footprint of image coordinates, the cell res texels wide.
fn impostor_lod(a: vec2f, res: f32) -> f32 {
    let dx = dpdx(a);
    let dy = dpdy(a);
    let footprint = max(length(dx), length(dy)) * 0.5 * res;
    return max(log2(max(footprint, 1e-6)), 0.0);
}

// Model matrix of a card from its rows.
fn impostor_model(r0: vec4f, r1: vec4f, r2: vec4f) -> mat4x4f {
    return instance_model(InstanceTransform(array<vec4f, 3>(r0, r1, r2)));
}

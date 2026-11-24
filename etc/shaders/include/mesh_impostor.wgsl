// The impostor cards of the forward pass and the depth prepass (box_render_batch_t.impostor):
// each instance is one card that blends the three views of the atlases nearest the direction of
// the camera, lit as the other meshes from the normal atlas at the point of the depth atlas. The
// card itself is flat: it draws at the depth of its plane, so the depth test drops the fragments
// of a card behind another before they are shaded. Included at the end of mesh.wgsl, after
// shade().
//
// The batch binds the albedo atlas as albedo_map, the depth atlas as orm_map and the normal
// atlas as normal_map; the vertex stage reads the grid from the per vertex data of the card mesh.
#pragma once
#include "impostor_card.wgsl"

// A vertex of a card. The image coordinates of the three views (plane) are affine across the
// card; the rest is the same for the whole card.
struct ImpostorOut {
    @builtin(position) @invariant pos: vec4f,
    // Image coordinates of the first and second view
    @location(0) plane01: vec4f,
    // Image coordinates of the third view
    @location(1) plane2: vec2f,
    // xyz the packed cells of the views, w the views per axis, 1 << 16 for a hemisphere grid
    @location(2) @interpolate(flat) cells: vec4u,
    // xyz the weights of the views, w the half size of the card
    @location(3) @interpolate(flat) weights: vec4f,
    // xyz the direction toward the camera in the frame of the root, w the radius of the sphere
    // over the half size of the card
    @location(4) @interpolate(flat) eye: vec4f,
    // xyz the center of the sphere in the frame of the root
    @location(5) @interpolate(flat) center: vec4f,
    @location(6) @interpolate(flat) row0: vec4f,
    @location(7) @interpolate(flat) row1: vec4f,
    @location(8) @interpolate(flat) row2: vec4f,
    // x the dither code of the DrawRange with draw_range, y the planet light slot, z the metallic
    // and roughness of the material of the card as unorm16
    @location(9) @interpolate(flat) misc: vec4u,
};

fn impostor_vertex(v: VertexIn, d: VertexData) -> ImpostorOut {
    let slot = slot_of(v.slot);
    let local = instance_local(transforms[slot], instance_page);
    let model = instance_model(local);
    let shading = shadings[slot];
    let material = material_fetch(shading_material(shading));
    let grid = impostor_grid(d);
    let center = (model * vec4f(grid.center, 1.0)).xyz;
    var toward = view.camera.xyz - center;
    if (view.lights.w == 1u) {
        toward = vec3f(view.view[0].z, view.view[1].z, view.view[2].z);
    }
    let e = impostor_local_direction(model, toward, grid.hemisphere);
    let q = impostor_corner(e, v.uv, grid.half, abs(v.position - grid.center));
    let frames = impostor_frames(grid.frames, grid.hemisphere, e);
    var planes: array<vec2f, 3>;
    for (var k = 0u; k < 3u; k++) {
        let dir = impostor_frame_direction(grid.frames, grid.hemisphere,
            impostor_unpack(frames.cells[k]));
        planes[k] = impostor_plane(q, e, dir, grid.half);
    }

    var o: ImpostorOut;
    o.pos = view.view_proj * (model * vec4f(grid.center + q, 1.0));
    o.plane01 = vec4f(planes[0], planes[1]);
    o.plane2 = planes[2];
    o.cells = vec4u(frames.cells, grid.frames | select(0u, 1u << 16u, grid.hemisphere));
    o.weights = vec4f(frames.weights, grid.half);
    o.eye = vec4f(e, grid.ratio);
    o.center = vec4f(grid.center, 0.0);
    o.row0 = local.rows[0];
    o.row1 = local.rows[1];
    o.row2 = local.rows[2];
    o.misc = vec4u(vertex_range(v.slot), shading_planet(shading),
        pack2x16unorm(vec2f(material.metallic, material.roughness)), 0u);
    return o;
}

@vertex fn vs_impostor(v: VertexIn, d: VertexData) -> ImpostorOut {
    return impostor_vertex(v, d);
}

// The surface a card shows at a fragment, blended from its views by weight and coverage: the
// display decoded albedo, the coverage, the normal and the point of the surface relative to the
// center, both in the frame of the root.
struct ImpostorSurface {
    albedo: vec3f,
    alpha: f32,
    normal: vec3f,
    point: vec3f,
};

fn impostor_surface(f: ImpostorOut) -> ImpostorSurface {
    let frames = f.cells.w & 0xFFFFu;
    let hemisphere = (f.cells.w >> 16u) != 0u;
    let half = f.weights.w;
    let res = f32(textureDimensions(albedo_map).x) / f32(frames);
    let lod = impostor_lod(f.plane01.xy, res);
    var planes = array<vec2f, 3>(f.plane01.xy, f.plane01.zw, f.plane2);
    var o = ImpostorSurface(vec3f(0.0), 0.0, vec3f(0.0), vec3f(0.0));
    for (var k = 0u; k < 3u; k++) {
        let w = f.weights[k];
        if (w <= 0.0) {
            continue;
        }
        let cell = impostor_unpack(f.cells[k]);
        let d = impostor_frame_direction(frames, hemisphere, cell);
        let first = textureSampleLevel(orm_map, material_sampler,
            impostor_uv(frames, cell, planes[k]), lod).r;
        let a = impostor_parallax(planes[k], f.eye.xyz, d,
            impostor_height(first, half, f.eye.w), half);
        let uv = impostor_uv(frames, cell, a);
        let albedo = textureSampleLevel(albedo_map, material_sampler, uv, lod);
        let normal = textureSampleLevel(normal_map, material_sampler, uv, lod).rgb * 2.0 - 1.0;
        let depth = textureSampleLevel(orm_map, material_sampler, uv, lod).r;
        let inside = all(abs(a) <= vec2f(1.0));
        let cover = select(0.0, albedo.a, inside) * w;
        let axes = impostor_axes(d);
        let point = (axes[0] * a.x + axes[1] * a.y) * half +
            d * impostor_height(depth, half, f.eye.w);
        o.albedo += albedo.rgb * cover;
        o.normal += normal * cover;
        o.point += point * cover;
        o.alpha += cover;
    }

    if (o.alpha > 0.0) {
        o.albedo /= o.alpha;
        o.point /= o.alpha;
    }
    return o;
}

// The coverage of a card at a fragment, as impostor_surface blends it, from the depth and the
// alpha of its views alone. The views left are not read once the coverage is on one side of
// IMPOSTOR_CUTOFF whatever they hold: the result is exact only as far as that side.
fn impostor_coverage(f: ImpostorOut) -> f32 {
    let frames = f.cells.w & 0xFFFFu;
    let hemisphere = (f.cells.w >> 16u) != 0u;
    let half = f.weights.w;
    let res = f32(textureDimensions(albedo_map).x) / f32(frames);
    let lod = impostor_lod(f.plane01.xy, res);
    var planes = array<vec2f, 3>(f.plane01.xy, f.plane01.zw, f.plane2);
    var alpha = 0.0;
    var left = max(f.weights.x, 0.0) + max(f.weights.y, 0.0) + max(f.weights.z, 0.0);
    for (var k = 0u; k < 3u; k++) {
        let w = f.weights[k];
        if (w <= 0.0) {
            continue;
        }
        if (alpha >= IMPOSTOR_CUTOFF || alpha + left < IMPOSTOR_CUTOFF) {
            break;
        }
        left -= w;
        let cell = impostor_unpack(f.cells[k]);
        let d = impostor_frame_direction(frames, hemisphere, cell);
        let first = textureSampleLevel(orm_map, material_sampler,
            impostor_uv(frames, cell, planes[k]), lod).r;
        let a = impostor_parallax(planes[k], f.eye.xyz, d,
            impostor_height(first, half, f.eye.w), half);
        if (all(abs(a) <= vec2f(1.0))) {
            alpha += textureSampleLevel(albedo_map, material_sampler,
                impostor_uv(frames, cell, a), lod).a * w;
        }
    }
    return alpha;
}

// The world position of the surface of a card at a fragment.
fn impostor_world(f: ImpostorOut, s: ImpostorSurface) -> vec3f {
    let model = impostor_model(f.row0, f.row1, f.row2);
    return (model * vec4f(f.center.xyz + s.point, 1.0)).xyz;
}

// Whether the dither of the DrawRange of a card or its coverage drops a fragment.
fn impostor_dropped(f: ImpostorOut, alpha: f32) -> bool {
    return (draw_range && !range_keeps(f.misc.x, f.pos.xy)) || alpha < IMPOSTOR_CUTOFF;
}

// Distance toward the sun, in radii of the bounding sphere, of the point a card is lit at from
// the point it shows: the card casts from the view nearest the light, whose surface differs from
// the blend of the views toward the camera, and would shadow itself where they disagree.
const IMPOSTOR_SHADOW_OFFSET: f32 = 0.1;

// The point a card is lit at, moved toward the sun from the point of its surface.
fn impostor_lit_at(model: mat4x4f, world: vec3f, radius: f32) -> vec3f {
    if (view.lights.x == 0u) {
        return world;
    }

    let scale = length(model[1].xyz);
    return world + normalize(lights[0].position) * IMPOSTOR_SHADOW_OFFSET * radius * scale;
}

// A fragment of a card lit at the point of its surface.
fn impostor_lit(f: ImpostorOut, c: ImpostorSurface) -> vec4f {
    let model = impostor_model(f.row0, f.row1, f.row2);
    let world = impostor_world(f, c);
    let n = impostor_world_normal(model, c.normal);
    let scalars = unpack2x16unorm(f.misc.z);
    if (scene_planet_light) {
        fragment_planet = f.misc.y & 0xFFu;
    }

    var s: Surface;
    s.albedo = c.albedo;
    s.alpha = 1.0;
    s.metallic = clamp(scalars.x, 0.0, 1.0);
    s.roughness = clamp(scalars.y, MIN_ROUGHNESS, 1.0);
    s.occlusion = view_occlusion(f.pos);
    s.emissive = vec3f(0.0);
    s.normal = n;
    s.geometric = n;
    let v = normalize(view.camera.xyz - world);
    let reflected = shade(s, v, impostor_lit_at(model, world, f.eye.w * f.weights.w));
    var color = reflected.diffuse + reflected.specular;
    if (scene_aerial && view.aerial.w > 0.0) {
        color = aerial_perspective(color, 1.0, f.pos, world);
    }

    return output(color, 1.0);
}

// The cards of a view without a depth prepass, which drop the fragments they do not cover.
@fragment fn fs_impostor(f: ImpostorOut) -> @location(0) vec4f {
    let c = impostor_surface(f);
    if (impostor_dropped(f, c.alpha)) {
        discard;
    }
    return impostor_lit(f, c);
}

// The cards of a view after its depth prepass, which test the depth their prepass wrote with
// Equal: only the fragments the prepass kept are shaded.
@fragment fn fs_impostor_equal(f: ImpostorOut) -> @location(0) vec4f {
    return impostor_lit(f, impostor_surface(f));
}

// The depth prepass of the cards writes the depth of their plane where they cover.
@fragment fn fs_impostor_depth(f: ImpostorOut) {
    if (impostor_dropped(f, impostor_coverage(f))) {
        discard;
    }
}

// The overdraw count of the cards, as the alpha tested meshes in green.
@fragment fn fs_impostor_count(f: ImpostorOut) -> @location(0) vec4f {
    if (impostor_dropped(f, impostor_coverage(f))) {
        discard;
    }
    return vec4f(0.0, 1.0, 0.0, 0.0);
}

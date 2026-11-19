// Areas of a portal-room scene as a view sees them (box_portal_shade_pack, src/portal/shading.c):
// a fragment takes the area whose box holds it, a little off its surface along its geometric
// normal, so the inner face of a wall belongs to the room and the outer one to the outdoors. The
// area sets the ambient (ambient_area), which lights reach the fragment (area_lights: those of
// its own area directly, those of the areas behind its open portals and the sun only along rays
// through the openings, area_through and area_sun) and the fog of the air the view ray crosses
// on its way to the eye, split at the portals the area is seen through (area_fog). A view
// without areas binds a block with a zero header, and every fragment keeps the scene's light.
// The includer declares the View uniform as view (lights.wgsl).
#pragma once

// vec4 of the block, BOX_PORTAL_SHADE_MAX.
const AREA_BLOCK: u32 = 4096u;

// vec4 of the record of an area, BOX_PORTAL_SHADE_RECORD.
const AREA_RECORD: u32 = 3u;

// vec4 of a light through portals, BOX_PORTAL_SHADE_THROUGH.
const AREA_THROUGH: u32 = 8u;

// Portals of a fog chain at most, BOX_PORTAL_SHADE_LINKS.
const AREA_LINKS: u32 = 4u;

// Distance off its surface along the geometric normal of the point whose area a fragment takes.
const AREA_LOOKUP: f32 = 0.1;

// Distance toward the sun at which it is traced as a point light, BOX_PORTAL_SUN_FAR.
const AREA_SUN_FAR: f32 = 1000.0;

// Distance outside the planes of an entry a fragment still counts as seen through it.
const AREA_ENTRY_SLACK: f32 = 0.02;

@group(0) @binding(24) var<uniform> area_block: array<vec4f, 4096>;

// Index of the area of the fragment, 0 for the outdoors, -1 without areas (area_fragment).
var<private> fragment_area: i32 = -1;

// First vec4 of the record of that area.
var<private> fragment_record: u32 = 0u;

// Bring a world point into the rest of the mover of a box: slot + 1 of the mover, 0 for a box
// that stays, whose three world to rest rows follow the boxes of the block.
fn area_rest(p: vec3f, mover: u32, boxes: u32) -> vec3f {
    if (mover == 0u) {
        return p;
    }

    let rows = 1u + 2u * boxes + 3u * (mover - 1u);
    let q = vec4f(p, 1.0);
    return vec3f(dot(area_block[rows], q), dot(area_block[rows + 1u], q),
        dot(area_block[rows + 2u], q));
}

// Find the area of a fragment at world with the geometric normal, for the area functions.
fn area_fragment(world: vec3f, geometric: vec3f) {
    fragment_area = -1;
    let header = area_block[0];
    if (header.w == 0.0) {
        return;
    }

    let p = world + geometric * AREA_LOOKUP;
    let boxes = min(u32(header.x), (AREA_BLOCK - 1u) / 2u);
    var area = 0;
    for (var i = 0u; i < boxes; i++) {
        let lo = area_block[1u + 2u * i];
        let hi = area_block[2u + 2u * i];
        let at = area_rest(p, u32(hi.w), boxes);
        if (all(at >= lo.xyz) && all(at <= hi.xyz)) {
            area = i32(lo.w);
            break;
        }
    }

    fragment_area = area;
    fragment_record = u32(header.z) + AREA_RECORD * u32(area);
}

// Ambient light of the fragment in a direction: that of its area, or the scene's ambient for
// the outdoors and without areas.
fn ambient_area(dir: vec3f, scene: vec3f) -> vec3f {
    if (fragment_area < 0) {
        return scene;
    }

    let r = area_block[fragment_record];
    if (r.x < 0.0) {
        return scene;
    }

    return r.rgb * (0.7 + 0.3 * dir.y);
}

// Whether a point or spot light of the clusters lights the fragment directly: a light outside
// every area or in the area of the fragment. Lights of other areas reach it through portals.
fn area_lights(light: Light) -> bool {
    return fragment_area < 0 || light.area == 0u || i32(light.area) - 1 == fragment_area;
}

// How much of a light at l reaches p through the opening at a of the block (center, half width,
// half height): the segment from p to l has to cross it. Its edges cast a penumbra as wide as an
// emitter of the size makes it at p.
fn area_aperture(p: vec3f, l: vec3f, a: u32, size: f32) -> f32 {
    let c = area_block[a].xyz;
    let r = area_block[a + 1u].xyz;
    let u = area_block[a + 2u].xyz;
    let n = normalize(cross(r, u));
    let sp = dot(p - c, n);
    let sl = dot(l - c, n);
    if (sp * sl > 0.0 || abs(sp - sl) < 1e-5) {
        return 0.0;
    }

    let h = p + (l - p) * (sp / (sp - sl)) - c;
    let rl = max(length(r), 1e-4);
    let ul = max(length(u), 1e-4);
    let edge = min(rl - abs(dot(h, r)) / rl, ul - abs(dot(h, u)) / ul);
    let w = size * abs(sp) / max(abs(sl), 1e-3) + 0.01;
    return smoothstep(-w, w, edge);
}

// Color of a light at l that reaches p through the openings of the light at rec of the block.
fn area_through_at(rec: u32, p: vec3f, l: vec3f) -> vec3f {
    let t = area_block[rec];
    var vis = area_aperture(p, l, rec + 2u, t.z);
    if (t.y > 1.5) {
        vis *= area_aperture(p, l, rec + 5u, t.z);
    }

    return area_block[rec + 1u].rgb * vis;
}

// Lights that shine into the area of the fragment through its portals, the sun among them.
fn area_through_count() -> u32 {
    if (fragment_area < 0) {
        return 0u;
    }

    return u32(area_block[fragment_record + 2u].w);
}

// First vec4 of the k-th light through portals of the area of the fragment.
fn area_through_record(k: u32) -> u32 {
    return u32(area_block[fragment_record + 2u].z) + k * AREA_THROUGH;
}

// Index in the light buffer of a light through portals, -1 for the sun.
fn area_through_light(rec: u32) -> i32 {
    return i32(area_block[rec].x);
}

// Color the openings let through of a light through portals at l that reaches p.
fn area_through(rec: u32, p: vec3f, l: vec3f) -> vec3f {
    return area_through_at(rec, p, l);
}

// Factor of a directional light toward l at p, shadowed by shadow: for the fragment of an area,
// the sun through the openings to the outdoors, shadowed, plus the unshadowed sun of the area;
// for the outdoors and without areas the shadow.
fn area_sun(p: vec3f, l: vec3f, shadow: f32) -> vec3f {
    if (fragment_area <= 0) {
        return vec3f(shadow);
    }

    let lp = p + l * AREA_SUN_FAR;
    let count = area_through_count();
    var through = vec3f(0.0);
    for (var k = 0u; k < count; k++) {
        let rec = area_through_record(k);
        if (area_through_light(rec) < 0) {
            through += area_through_at(rec, p, lp);
        }
    }

    return min(through, vec3f(1.0)) * shadow + vec3f(max(area_block[fragment_record].w, 0.0));
}

// Fog of the air between the eye and world: in scattered light in rgb and transmittance in w.
// The view ray is split at the portals of the entry the fragment is seen through, each stretch
// fogged by the area in front of its portal, the last by the area of the fragment.
fn area_fog(world: vec3f) -> vec4f {
    if (fragment_area < 0) {
        return vec4f(0.0, 0.0, 0.0, 1.0);
    }

    let own = area_block[fragment_record + 1u];
    let slot = area_block[fragment_record + 2u];
    let eye = view.camera.xyz;
    let dist = length(world - eye);
    let count = u32(slot.y);
    var at = u32(slot.x);
    var chosen = 0u;
    var found = false;
    for (var e = 0u; e < count && !found; e++) {
        let head = area_block[at];
        let planes = u32(head.x);
        var inside = true;
        for (var k = 0u; k < planes; k++) {
            let pl = area_block[at + 1u + k];
            if (dot(pl.xyz, world) + pl.w < -AREA_ENTRY_SLACK) {
                inside = false;
                break;
            }
        }

        if (inside || e == 0u) {
            chosen = at;
            found = inside;
        }

        at += 1u + planes + 2u * u32(head.y);
    }

    var links = 0u;
    var chain = 0u;
    if (count > 0u) {
        links = min(u32(area_block[chosen].y), AREA_LINKS);
        chain = chosen + 1u + u32(area_block[chosen].x);
    }

    var t: array<f32, 5>;
    t[0] = 0.0;
    for (var k = 0u; k < links; k++) {
        let pl = area_block[chain + 2u * k];
        let se = dot(pl.xyz, eye) + pl.w;
        let sp = dot(pl.xyz, world) + pl.w;
        var tk = 1.0;
        if (abs(se - sp) > 1e-6) {
            tk = clamp(se / (se - sp), 0.0, 1.0);
        }
        t[k + 1u] = max(tk, t[k]);
    }

    var e = exp(-dist * (1.0 - t[links]) * own.w);
    var fog = vec4f(own.rgb * (1.0 - e), e);
    for (var k = i32(links) - 1; k >= 0; k--) {
        let f = area_block[chain + 2u * u32(k) + 1u];
        e = exp(-dist * (t[k + 1] - t[k]) * f.w);
        fog = vec4f(fog.rgb * e + f.rgb * (1.0 - e), fog.w * e);
    }

    return fog;
}

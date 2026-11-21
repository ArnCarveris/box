// Grass draw (gpu_grass.c): one indirect draw per Grass and level of detail in the depth prepass
// and the forward pass of a lit view, every instance a blade the place pass appended to the range
// of its level (grass_place.wgsl). The vertex stage builds the blade as a triangle strip from the
// vertex index, the level in its first vertex: two vertices per row across the blade and one at
// the tip, along a cubic bezier that leans along its facing and bends over toward its tip, the
// rows turning around the vertical as the blade curls. A blade seen edge on widens in view space
// so it keeps covering about as many pixels.
//
// A blade shorter than the fold height of its Grass folds into a V: the first half of its strip
// runs from the tip of one half down to the root and the second half from the root up to the tip
// of the other, the two sharing the stem up to the fold between their first and second segment
// and turning apart above it, each leaning out to its own side and turning away from the other
// around the vertical, each as wide at the fold as a blade at its root. The first half turns its
// sides around so both keep the winding and the shading of a straight blade. Below the fold height the halves part the
// more the shorter the blade, from not at all at the fold height, where a folded blade draws as
// the straight one, so the field shows no step. A blade of a single triangle cannot fold and
// widens as much instead.
//
// The blades are opaque and lit by the sun of the scene through its shadows and cookie, by the
// ambient light and seen through the aerial perspective; the fog passes after the forward pass
// fog them. Their normals round across their width, their roots are darker and occluded, a soft
// specular lobe catches the sun and the light behind a blade shines through it, tinted by its
// color, as the back lobe of a DiffuseTransmission. With distance, or where a blade covers few
// pixels, the normals flatten toward the ground and the specular widens and dims, so the field
// neither shimmers nor sparkles without temporal anti-aliasing.
#include "include/lights.wgsl"
#include "include/shadow.wgsl"
#include "include/cookie.wgsl"
#include "include/ambient.wgsl"
#include "include/aerial.wgsl"
#include "include/grass.wgsl"

const PI: f32 = 3.141592653589793;

// Share of the light behind a blade that comes through it
const TRANSLUCENCY: f32 = 1.0;

// Light that scatters forward through a blade toward an eye that looks at the light
const FORWARD: f32 = 2.0;

// How far the normal of a blade leans toward up near and far, so the field shades like the
// ground it covers
const NORMAL_UP: f32 = 0.3;
const NORMAL_UP_FAR: f32 = 0.85;

// How far the normal turns toward the edges across the width of a blade
const ROUND: f32 = 0.7;

// Ambient light and sun that reach the base of a blade, under the others
const BASE_OCCLUSION: f32 = 0.25;
const BASE_SHADOW: f32 = 0.55;

// Reflectance of a blade at normal incidence, and the exponents of its specular lobe near and far
const SPECULAR: f32 = 0.04;
const GLOSS: f32 = 40.0;
const GLOSS_FAR: f32 = 6.0;

// Largest lean of a blade in radians
const LEAN_MAX: f32 = 1.2;

// Largest curl of a blade at its tip in radians
const CURL_MAX: f32 = 0.6;

// Largest angle in radians each half of a folded blade turns out from the vertical by
const FOLD_SPLAY: f32 = 0.5;

// Largest angle in radians each half of a folded blade turns around the vertical by, so the
// halves face apart and the V shows from every side
const FOLD_TURN: f32 = 0.8;

// Share of the length of a half of a folded blade below its fold, its first segment at level 0
const FOLD_AT: f32 = 0.2;

// Share of the fold height below which a blade is fully folded (BOX_GRASS_FOLD_BLEND)
const FOLD_BLEND: f32 = 0.7;

// How much wider than its width a blade seen edge on draws
const EDGE_WIDEN: f32 = 0.8;

// Blade widths per pixel and distances in level 0 ends over which the normals flatten
const FLAT_PIXELS: vec2f = vec2f(0.12, 0.5);
const FLAT_DISTANCE: vec2f = vec2f(0.3, 1.5);

// How far the root to tip gradient of a flat blade moves toward the point along it that shades
// like the whole blade
const FLAT_GRADIENT: f32 = 0.8;
const FLAT_ALONG: f32 = 0.6;

@group(0) @binding(0) var<uniform> view: View;
@group(0) @binding(1) var<storage, read> lights: array<Light>;

@group(1) @binding(0) var<uniform> params: GrassParams;
@group(1) @binding(1) var<storage, read> blades: array<GrassBlade>;

struct VertexOut {
    @builtin(position) @invariant clip: vec4f,
    @location(0) world: vec3f,
    // Normal of the blade without its rounding, on its front
    @location(1) normal: vec3f,
    // What the rounding adds to the normal, on either side
    @location(2) rounding: vec3f,
    // 0 at the base, 1 at the tip
    @location(3) along: f32,
    // 0 near, 1 where the normals and specular are flat
    @location(4) flatten: f32,
    @location(5) @interpolate(flat) color: vec3f,
};

// Point of a cubic bezier that starts at 0.
fn grass_bezier(p1: vec3f, p2: vec3f, p3: vec3f, t: f32) -> vec3f {
    let s = 1.0 - t;
    return 3.0 * s * s * t * p1 + 3.0 * s * t * t * p2 + t * t * t * p3;
}

// Tangent of a cubic bezier that starts at 0.
fn grass_bezier_tangent(p1: vec3f, p2: vec3f, p3: vec3f, t: f32) -> vec3f {
    let s = 1.0 - t;
    return 3.0 * s * s * p1 + 6.0 * s * t * (p2 - p1) + 3.0 * t * t * (p3 - p2);
}

// Rotation of a vector around a unit axis.
fn grass_rotate(v: vec3f, axis: vec3f, angle: f32) -> vec3f {
    let c = cos(angle);
    return v * c + cross(axis, v) * sin(angle) + axis * (dot(axis, v) * (1.0 - c));
}

// Where along a half of a folded blade of a number of segments a row of it lies, from its
// root: the first segment ends at the fold, the others share the rest.
fn grass_fold_along(k: u32, segments: u32) -> f32 {
    if (k == 0u || segments < 2u) {
        return f32(k) / f32(segments);
    }
    return mix(FOLD_AT, 1.0, f32(k - 1u) / f32(segments - 1u));
}

// How far a blade of a height folds, 0 straight to 1 fully parted.
fn grass_fold(height: f32) -> f32 {
    let at = params.shape.w;
    if (at <= 0.0) {
        return 0.0;
    }
    return 1.0 - smoothstep(FOLD_BLEND * at, at, height);
}

// Direction from a point toward the eye.
fn grass_to_eye(world: vec3f) -> vec3f {
    if (view.lights.w != 0u) {
        return normalize(vec3f(view.view[0].z, view.view[1].z, view.view[2].z));
    }
    return normalize(view.camera.xyz - world);
}

@vertex
fn vs(@builtin(vertex_index) vertex: u32, @builtin(instance_index) instance: u32) -> VertexOut {
    var out: VertexOut;
    let lod = min(vertex / GRASS_LOD_VERTICES, GRASS_LODS - 1u);
    let v = vertex % GRASS_LOD_VERTICES;
    let segments = grass_segments(lod);
    let b = blades[lod * params.config.w + instance];
    let row = min(v / 2u, segments);
    var t = f32(row) / f32(segments);
    var side = select(f32(v & 1u) * 2.0 - 1.0, 0.0, v >= 2u * segments);
    let up = vec3f(0.0, 1.0, 0.0);
    let facing = vec3f(cos(b.yaw), 0.0, sin(b.yaw));

    // A short blade folds: its first rows run down one half to the root, the others up the
    // other half, the first half with its sides turned around to keep the winding. A single
    // triangle widens instead
    let fold = grass_fold(b.height);
    let folded = fold > 0.0 && segments >= 3u;
    var part = 0.0;
    var width = b.width;
    if (folded) {
        let rows = (segments - 1u) / 2u;
        if (row <= rows) {
            t = grass_fold_along(rows - row, rows);
            side = -side;
            part = -1.0;
        } else {
            t = grass_fold_along(row - rows - 1u, rows);
            part = 1.0;
        }
    } else if (segments < 3u) {
        width *= 1.0 + fold;
    }

    // The spine: the tip leans along the facing, the second control point stays higher the more
    // the blade bends, so it arcs over toward its tip
    let a = b.lean * LEAN_MAX;
    let h = b.height;
    let p3 = up * (h * cos(a)) + facing * (h * sin(a));
    let p1 = up * (h * 0.4);
    let p2 = up * (h * mix(0.7, 0.95, b.bend)) + facing * (h * sin(a) * mix(0.45, 0.15, b.bend));
    var local = grass_bezier(p1, p2, p3, t);
    var tangent = normalize(grass_bezier_tangent(p1, p2, p3, max(t, 1e-3)));

    // The rows turn around the vertical as the blade curls
    let twist = b.yaw + b.curl * CURL_MAX * t * t;
    var across = vec3f(-sin(twist), 0.0, cos(twist));

    // Above the fold each half leans out around the facing toward its own side and turns
    // around the vertical away from the other
    if (folded && t > FOLD_AT + 1e-4) {
        let angle = part * fold * FOLD_SPLAY;
        let turn = part * fold * FOLD_TURN;
        let pivot = grass_bezier(p1, p2, p3, FOLD_AT);
        local = pivot + grass_rotate(grass_rotate(local - pivot, facing, angle), up, turn);
        tangent = grass_rotate(grass_rotate(tangent, facing, angle), up, turn);
        across = grass_rotate(grass_rotate(across, facing, angle), up, turn);
    }

    let spine = b.position + local;
    let flat_normal = normalize(cross(across, tangent));

    // Wider toward the base, narrowing faster toward the tip, a half of a folded blade from its
    // fold as a whole blade from its root; seen edge on the blade widens across the view
    var w = t;
    if (folded) {
        w = mix(t, max(t - FOLD_AT, 0.0) / (1.0 - FOLD_AT), fold);
    }
    let half_width = 0.5 * width * (1.0 - w * w) * mix(1.0, 1.0 - w, 0.5);
    let eye = grass_to_eye(b.position);
    let eye_xz = normalize(eye.xz + vec2f(1e-6, 0.0));
    let across_xz = normalize(across.xz + vec2f(0.0, 1e-6));
    let edge = abs(dot(across_xz, eye_xz));
    let screen = vec3f(-eye_xz.y, 0.0, eye_xz.x);
    let order = select(-1.0, 1.0, dot(across, screen) >= 0.0);
    let world = spine + across * (side * half_width) +
        screen * (order * side * half_width * EDGE_WIDEN * edge * edge);

    // Normals flatten with distance and where the blade is only a few pixels wide
    let d = length(view.camera.xyz - b.position);
    let pixels = grass_pixel(d) / max(b.width, 1e-5);
    out.flatten = max(smoothstep(FLAT_PIXELS.x, FLAT_PIXELS.y, pixels),
        smoothstep(FLAT_DISTANCE.x * params.lod.x, FLAT_DISTANCE.y * params.lod.x, d));
    out.normal = flat_normal;
    out.rounding = across * (side * ROUND * (1.0 - out.flatten));
    out.world = world;
    out.clip = view.view_proj * vec4f(world, 1.0);
    out.along = t;
    out.color = unpack4x8unorm(b.tint).rgb * (2.0 * b.shade) *
        mix(1.0, 0.85, saturate(1.0 - b.clump));
    return out;
}

// Light a blade reflects toward the eye: the ambient light, occluded toward its base, and the
// directional lights, the first through its shadows and cookie, on its front with a specular
// lobe and through it, the light through it tinted by the color of the blade.
fn grass_light(world: vec3f, front: vec3f, n: vec3f, along: f32, flatten: f32, albedo: vec3f)
    -> vec3f
{
    let up = vec3f(0.0, 1.0, 0.0);
    let lit = normalize(mix(n, up, mix(NORMAL_UP, NORMAL_UP_FAR, flatten)));
    let v = grass_to_eye(world);
    let occlusion = mix(BASE_OCCLUSION, 1.0, pow(along, 0.8));
    let shadow = mix(BASE_SHADOW, 1.0, along);
    let gloss = mix(GLOSS, GLOSS_FAR, flatten);
    let glow = saturate(albedo * 2.5);
    var sum = albedo * ambient(lit) * occlusion;
    for (var i = 0u; i < view.lights.x; i++) {
        let light = lights[i];
        let l = normalize(light.position);
        var visible = shadow;
        if (i == 0u) {
            visible *= shadow_factor(world, lit) * light_cookie(world, l, lit);
        }

        let diffuse = albedo * saturate(dot(lit, l));
        let hv = normalize(l + v);
        let fresnel = SPECULAR + (1.0 - SPECULAR) * pow(1.0 - saturate(dot(v, hv)), 5.0);
        let specular = (gloss + 8.0) / (8.0 * PI) * pow(saturate(dot(lit, hv)), gloss) *
            fresnel * mix(1.0, 0.3, flatten) * PI * saturate(dot(lit, l));
        let back = saturate(dot(-front, l)) +
            FORWARD * pow(saturate(dot(-v, l)), 6.0) * (1.0 - 0.5 * flatten);
        let through = albedo * glow * (back * TRANSLUCENCY * mix(0.3, 1.0, along));
        sum += light.color * ((diffuse + specular + through) * visible / PI);
    }
    return sum;
}

@fragment
fn fs(in: VertexOut, @builtin(front_facing) front: bool) -> @location(0) vec4f {
    let flat_normal = normalize(in.normal) * select(-1.0, 1.0, front);
    let n = normalize(flat_normal + in.rounding);

    // Where a blade spans few pixels the root to tip gradient flattens toward its mean, so
    // bright single-pixel tips do not sparkle
    let along = mix(in.along, FLAT_ALONG, FLAT_GRADIENT * in.flatten);
    let gradient = mix(params.base.rgb, params.tip.rgb, smoothstep(0.0, 1.0, along));
    let albedo = gradient * in.color;
    var color = grass_light(in.world, flat_normal, n, along, in.flatten, albedo);
    if (view.aerial.w > 0.0) {
        let ap = aerial_sample(in.clip, in.world);
        color = color * ap.a + ap.rgb;
    }

    return vec4f(color, 1.0);
}

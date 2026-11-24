// Grass draw (gpu_grass.c): one indirect draw per Grass and level of detail in the depth prepass
// and the forward pass of a lit view, every instance a blade the place pass appended to the range
// of its level (grass_place.wgsl). The vertex stage builds the blade as a triangle strip from the
// vertex index, the level in its first vertex: two vertices per row across the blade and one at
// the tip, along a cubic bezier that leans along its facing and bends over toward its tip, the
// rows turning around the vertical as the blade curls. A blade seen edge on widens in view space
// so it keeps covering about as many pixels.
//
// A blade bends, shades and is placed in the local frame of the packet, and its clip position is
// render_view_proj of its root relative to the render origin plus its shape around the root, so
// it keeps its precision near the render origin. The shadow casters stay in the local frame.
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
// In the Wind a blade bends about its root toward the wave of the wind where it stands
// (grass_wind.wgsl), which rolls the gusts over the field with the WindSway plants beside it: its
// control points turn about the root by a growing share of the bend toward the tip, so the root
// stays on the ground and the curve keeps about its length, and its rows turn with it, so the
// bend also turns the normals and shifts the light on the blade. The up the normals lean toward
// bends with the blade too, and a blade a gust bends over beyond the steady wind shows a sheen
// while one that rises in a lull darkens, so the gusts read as bands of light rolling over the
// field even where the blades are too small to see. Around that lean its clump
// swings at the phase of the clump and the blade flutters and twists at its own phase, both
// more as a gust stirs them, the stiffness of the blade and its clump damping all of it. In
// still air the blades only sway faintly. The bend is a function of the blade, its place and the
// wind of the frame, so the depth prepass and the forward pass agree on it.
//
// The blades that cast shadows draw into the cascades of the directional light and the light
// tiles with vs_shadow and, into the octants of a point light tile, vs_shadow_point and
// fs_shadow_point: the same blades bent by the same wind, widened across the light where it sees
// them edge on. Where they cast, the darkening of the base of the blades weakens, as the shadows
// of the others darken it, and grows back as those shadows fade out.
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

// Sun that reaches the base of a blade where the blades cast shadows (GrassParams.caster): all
// of it, the shadows of the others darken the base instead
const BASE_SHADOW_CAST: f32 = 1.0;

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

// Share of the bend of a blade a stiff blade keeps
const WIND_STIFF: f32 = 0.4;

// Swing in radians of a clump along the wind at full wind, and across it as a share of that
const WIND_SWING: f32 = 0.18;
const WIND_SWING_CROSS: f32 = 0.5;

// Flutter in radians of a blade at full wind, of which WIND_FLUTTER_STEADY flutters without a gust
const WIND_FLUTTER: f32 = 0.1;
const WIND_FLUTTER_STEADY: f32 = 0.3;

// Twist in radians of the tip of a blade around its length as it flutters at full wind
const WIND_TWIST: f32 = 0.35;

// Swing and flutter in still air, as a share of those at full wind
const WIND_CALM: f32 = 0.2;

// Shares of the bend of a blade its three control points turn by, from the root
const WIND_BEND_ALONG: vec3f = vec3f(0.15, 0.5, 1.0);

// How much brighter a blade shows per radian a gust bends it over beyond the lean of the steady
// wind, the sheen of its face to the sky, and darker per radian a lull lets it rise
const WIND_SHEEN: f32 = 1.2;

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
    // Up turned by the bend of the blade where it is, which the normal leans toward
    @location(6) up: vec3f,
    // Angle the gust where the blade stands bends it over beyond the steady wind at this height,
    // below 0 in a lull
    @location(7) gust: f32,
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

// The bend of a blade in the wind: xy the horizontal direction and angle in radians its tip turns
// toward about its root, z the angle its tip twists by around its length, w the angle the gust or
// lull where it stands adds to the lean of the steady wind, below 0 in a lull.
fn grass_wind_bend(b: GrassBlade) -> vec4f {
    let w = params.wind;
    let wave = grass_wind_wave(b.position.xz, w);
    let bits = b.tint >> 24u;
    let stiffness = f32(bits & 15u) / 15.0;
    let clump = f32(bits >> 4u) / 16.0;
    let own = grass_unit(grass_hash(bitcast<u32>(b.yaw)));
    let free = mix(1.0, WIND_STIFF, stiffness);
    let full = clamp(w.strength.x / GRASS_WIND_FULL, 0.0, 1.0);
    let a = GRASS_WIND_TAU * w.time.x;
    let dir = w.field.xy;
    let across = vec2f(-dir.y, dir.x);

    // The lean toward the wave
    let strength = length(wave.xy);
    let lean = grass_wind_lean(strength);
    var r = vec2f(0.0);
    if (strength > 0.0) {
        r = wave.xy * (lean / strength);
    }

    // The clump swings around the lean as the gust stirs it, faintly in still air
    let swing = WIND_SWING * (full * wave.w + WIND_CALM * (1.0 - full));
    let sp = GRASS_WIND_TAU * clump;
    r += dir * (swing * sin(a + sp)) +
        across * (swing * WIND_SWING_CROSS * sin(2.0 * a + 1.3 + sp));

    // The blade flutters at its own phase, more in a gust
    let flutter = WIND_FLUTTER * (full * mix(WIND_FLUTTER_STEADY, 1.0, wave.w) +
        WIND_CALM * (1.0 - full));
    let fp = GRASS_WIND_TAU * own;
    r += vec2f(sin(5.0 * a + fp), sin(7.0 * a + 1.7 * fp + 2.0)) * flutter;
    let twist = WIND_TWIST / WIND_FLUTTER * flutter * sin(6.0 * a + fp + 1.0);
    let gust = lean - grass_wind_lean(w.strength.x / GRASS_WIND_FULL);
    return vec4f(r * free, twist * free, gust * free);
}

// Axis about which a turn of a horizontal direction moves a point above it toward it.
fn grass_wind_axis(r: vec2f) -> vec3f {
    let l = length(r);
    if (l <= 0.0) {
        return vec3f(1.0, 0.0, 0.0);
    }
    return vec3f(r.y, 0.0, -r.x) / l;
}

// Direction from a point toward the eye.
fn grass_to_eye(world: vec3f) -> vec3f {
    if (view.lights.w != 0u) {
        return normalize(vec3f(view.view[0].z, view.view[1].z, view.view[2].z));
    }
    return normalize(view.camera.xyz - world);
}

// The shape of a vertex of a blade: where it is and how it turns.
struct GrassShape {
    world: vec3f,       // Local world position
    relative: vec3f,    // Position relative to the origin grass_shape was given
    normal: vec3f,      // Normal of the blade without its rounding, on its front
    across: vec3f,      // Direction across the blade at its row
    side: f32,          // -1 and 1 on its edges, 0 at its tip
    along: f32,         // 0 at the base, 1 at the tip
    up: vec3f,          // Up turned by the bend of the blade where it is
    gust: f32,          // Angle the gust bends it over beyond the steady wind at this height
};

// The blade of a vertex of a draw and its level.
fn grass_blade_of(vertex: u32, instance: u32) -> GrassBlade {
    let lod = min(vertex / GRASS_LOD_VERTICES, GRASS_LODS - 1u);
    return blades[lod * params.config.w + instance];
}

// The shape of a vertex of a blade of a level, with the index of the vertex within its level, the
// direction toward the eye the blade widens across when seen edge on and the local position its
// relative position is taken from: the root of the blade minus it, plus the shape around the
// root, so the shape keeps its precision near that origin. An origin of 0 gives the world
// position bit for bit.
fn grass_shape(b: GrassBlade, lod: u32, v: u32, eye: vec3f, origin: vec3f) -> GrassShape {
    var out: GrassShape;
    let segments = grass_segments(lod);
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
    var p3 = up * (h * cos(a)) + facing * (h * sin(a));
    var p1 = up * (h * 0.4);
    var p2 = up * (h * mix(0.7, 0.95, b.bend)) + facing * (h * sin(a) * mix(0.45, 0.15, b.bend));

    // The wind turns the control points about the root, the more the higher up, so the blade
    // bends over and stays rooted
    let wind = grass_wind_bend(b);
    let wind_angle = length(wind.xy);
    let wind_axis = grass_wind_axis(wind.xy);
    if (wind_angle > 0.0) {
        p1 = grass_rotate(p1, wind_axis, wind_angle * WIND_BEND_ALONG.x);
        p2 = grass_rotate(p2, wind_axis, wind_angle * WIND_BEND_ALONG.y);
        p3 = grass_rotate(p3, wind_axis, wind_angle * WIND_BEND_ALONG.z);
    }

    var local = grass_bezier(p1, p2, p3, t);
    var tangent = normalize(grass_bezier_tangent(p1, p2, p3, max(t, 1e-3)));

    // The rows turn around the vertical as the blade curls and twists in the wind, and with the
    // bend of the blade where they are
    let twist = b.yaw + (b.curl * CURL_MAX + wind.z) * t * t;
    var across = vec3f(-sin(twist), 0.0, cos(twist));
    out.up = up;
    out.gust = wind.w * t;
    if (wind_angle > 0.0) {
        across = grass_rotate(across, wind_axis, wind_angle * t);
        out.up = grass_rotate(up, wind_axis, wind_angle * t);
    }

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
    let relative_spine = (b.position - origin) + local;

    // Wider toward the base, narrowing faster toward the tip, a half of a folded blade from its
    // fold as a whole blade from its root; seen edge on the blade widens across the eye
    var w = t;
    if (folded) {
        w = mix(t, max(t - FOLD_AT, 0.0) / (1.0 - FOLD_AT), fold);
    }
    let half_width = 0.5 * width * (1.0 - w * w) * mix(1.0, 1.0 - w, 0.5);
    let eye_xz = normalize(eye.xz + vec2f(1e-6, 0.0));
    let across_xz = normalize(across.xz + vec2f(0.0, 1e-6));
    let edge = abs(dot(across_xz, eye_xz));
    let screen = vec3f(-eye_xz.y, 0.0, eye_xz.x);
    let order = select(-1.0, 1.0, dot(across, screen) >= 0.0);
    let width_side = across * (side * half_width);
    let width_eye = screen * (order * side * half_width * EDGE_WIDEN * edge * edge);
    out.world = spine + width_side + width_eye;
    out.relative = relative_spine + width_side + width_eye;
    out.normal = normalize(cross(across, tangent));
    out.across = across;
    out.side = side;
    out.along = t;
    return out;
}

@vertex
fn vs(@builtin(vertex_index) vertex: u32, @builtin(instance_index) instance: u32) -> VertexOut {
    var out: VertexOut;
    let lod = min(vertex / GRASS_LOD_VERTICES, GRASS_LODS - 1u);
    let b = grass_blade_of(vertex, instance);
    let s = grass_shape(b, lod, vertex % GRASS_LOD_VERTICES, grass_to_eye(b.position),
        view.render_delta.xyz);

    // Normals flatten with distance and where the blade is only a few pixels wide
    let d = length(view.camera.xyz - b.position);
    let pixels = grass_pixel(d) / max(b.width, 1e-5);
    out.flatten = max(smoothstep(FLAT_PIXELS.x, FLAT_PIXELS.y, pixels),
        smoothstep(FLAT_DISTANCE.x * params.lod.x, FLAT_DISTANCE.y * params.lod.x, d));
    out.normal = s.normal;
    out.rounding = s.across * (s.side * ROUND * (1.0 - out.flatten));
    out.world = s.world;
    out.clip = view.render_view_proj * vec4f(s.relative, 1.0);
    out.along = s.along;
    out.up = s.up;
    out.gust = s.gust;
    out.color = unpack4x8unorm(b.tint).rgb * (2.0 * b.shade) *
        mix(1.0, 0.85, saturate(1.0 - b.clump));
    return out;
}

// Direction from a point toward the light the blades cast shadows of (GrassParams.light).
fn grass_to_light(world: vec3f) -> vec3f {
    if (params.light.w == 0.0) {
        return normalize(params.light.xyz);
    }
    return normalize(params.light.xyz - world + vec3f(0.0, 1e-6, 0.0));
}

// The world position of a vertex of a blade that casts a shadow, which widens across the light
// when the light sees it edge on.
fn grass_caster(vertex: u32, instance: u32) -> vec3f {
    let lod = min(vertex / GRASS_LOD_VERTICES, GRASS_LODS - 1u);
    let b = grass_blade_of(vertex, instance);
    return grass_shape(b, lod, vertex % GRASS_LOD_VERTICES, grass_to_light(b.position),
        vec3f(0.0)).world;
}

// A blade that casts a shadow into a cascade of the directional light or a spot light tile, the
// view projection of its params that of the cascade or the tile, depth only. Does not read the
// view, the shadow pass binds none.
@vertex
fn vs_shadow(@builtin(vertex_index) vertex: u32, @builtin(instance_index) instance: u32)
    -> @builtin(position) vec4f
{
    return params.view_proj * vec4f(grass_caster(vertex, instance), 1.0);
}

// A blade that casts a shadow into an octant of a point light tile, with its distances to the 3
// half spaces of the octant.
struct GrassPointOut {
    @builtin(position) clip: vec4f,
    @location(0) octant: vec3f,
};

// 1 / sqrt(3), the length of the axis of an octant of a point light tile
const OCTANT_AXIS: f32 = 0.57735026919;

// Axis signs of an octant: bit 0 negates x, bit 1 y and bit 2 z.
fn grass_octant_signs(octant: u32) -> vec3f {
    let bits = vec3u(octant) & vec3u(1u, 2u, 4u);
    return select(vec3f(1.0), vec3f(-1.0), bits != vec3u(0u));
}

// A blade that casts a shadow into the octant of a point light tile of its params, projected as
// octant_position of shadow.wgsl projects the casters.
@vertex
fn vs_shadow_point(@builtin(vertex_index) vertex: u32, @builtin(instance_index) instance: u32)
    -> GrassPointOut
{
    let world = grass_caster(vertex, instance);
    let signs = grass_octant_signs(u32(max(params.light.w - 2.0, 0.0)));
    let v = world - params.light.xyz;
    let fold = select(0.0, 1.0, signs.z < 0.0);
    let near_ = params.planes.x;
    let far_ = params.planes.y;
    let scale = near_ / (far_ - near_);
    let offset = near_ * far_ / (far_ - near_);
    let axis = OCTANT_AXIS * dot(signs, v);
    var out: GrassPointOut;
    out.clip = vec4f(
        OCTANT_AXIS * (v.x + fold * signs.x * signs.z * v.z),
        -OCTANT_AXIS * (v.y + fold * signs.y * signs.z * v.z),
        offset - scale * axis,
        axis);
    out.octant = signs * v;
    return out;
}

// Discard a fragment of a blade outside the octant of its point light tile.
@fragment
fn fs_shadow_point(in: GrassPointOut) {
    if (any(in.octant < vec3f(0.0))) {
        discard;
    }
}

// Share of the sun that reaches the base of a blade at a point: where the blades cast shadows
// the shadows of the others darken the base, so it takes less darkening of its own, which grows
// back as their shadows fade out toward the distance they cast within. The blades that cast
// shrink by the same smoothstep (grass_place.wgsl), their shadows covering its square.
fn grass_base_shadow(world: vec3f) -> f32 {
    if (params.caster.x <= 0.0) {
        return BASE_SHADOW;
    }
    let d = length(view.camera.xyz - world);
    let size = 1.0 - smoothstep(params.caster.y, params.caster.x, d);
    return mix(BASE_SHADOW, BASE_SHADOW_CAST, size * size);
}

// Light a blade reflects toward the eye: the ambient light, occluded toward its base, and the
// directional lights, the first through its shadows and cookie, on its front with a specular
// lobe and through it, the light through it tinted by the color of the blade.
fn grass_light(world: vec3f, front: vec3f, n: vec3f, up: vec3f, along: f32, flatten: f32,
    albedo: vec3f) -> vec3f
{
    let lit = normalize(mix(n, up, mix(NORMAL_UP, NORMAL_UP_FAR, flatten)));
    let v = grass_to_eye(world);
    let occlusion = mix(BASE_OCCLUSION, 1.0, pow(along, 0.8));
    let shadow = mix(grass_base_shadow(world), 1.0, along);
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
    let up = normalize(in.up);
    let albedo = gradient * in.color * max(1.0 + WIND_SHEEN * in.gust, 0.5);
    var color = grass_light(in.world, flat_normal, n, up, along, in.flatten, albedo);
    if (view.aerial.w > 0.0) {
        let ap = aerial_sample(in.clip, in.world);
        color = color * ap.a + ap.rgb;
    }

    return vec4f(color, 1.0);
}

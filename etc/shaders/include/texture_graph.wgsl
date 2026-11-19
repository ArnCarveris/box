// Shared code of the compute stages the GPU backend generates for the texture graphs
// (src/render/gpu_texture_graph.c). A stage evaluates its nodes per texel in workgroups of 8x8
// and stores the value of its last node in box_texgraph_out at binding 2. On the world the
// stages before the output evaluate the grid of the image grown by the apron on each side, so
// the neighbourhood nodes read their inputs past the edges of the window; the texel of a node
// is a texel of that grid, the output stage stores the texels of the image only. The node kinds call
// the hashing, the reads of materialized inputs and the samples of bound textures below.

const BOX_TEXGRAPH_PI: f32 = 3.141592653589793;

// Uniforms of the stages of a graph
struct BoxTexGraphStage {
    // Size of the image in texels
    size: vec2u,
    // TextureGraphDomain of the coordinates: 0 the plane, 1 the sphere, 2 the world
    domain: u32,
    // How the output stage stores its value: 0 as computed, 1 display encoded with alpha kept,
    // 2 the first channel as (x, 0, 0, 1)
    encode: u32,
    // Seconds of the world clock at the evaluation, which TexTime reads
    time: f32,
    // Texels the grid of the stages grows the image by on each side on the world, else 0
    apron: u32,
    pad1: u32,
    pad2: u32,
    // Longitudes of the first and last column edges and latitudes of the top and bottom row
    // edges of the image on the sphere in radians, (0, 2 pi, -pi / 2, pi / 2) for the whole
    // sphere (TextureGraph.longitude_start and the others, resolved)
    region: vec4f,
    // Meters between neighbouring texels along x and z of the world in xy, 0 on the other
    // domains
    world: vec4f,
};

// Parameter bytes of every node of the graph, at the offsets of the compiled graph
@group(0) @binding(0) var<storage, read> box_texgraph_params: array<vec4u>;
@group(0) @binding(1) var<uniform> box_texgraph_stage: BoxTexGraphStage;

// Size of the grid of texels the stages evaluate: the image grown by the apron on each side
fn box_texgraph_grid() -> vec2i {
    return vec2i(box_texgraph_stage.size) + 2 * i32(box_texgraph_stage.apron);
}

// Whether a materialized input has a node: the texture of an input without one is 1x1
fn box_texgraph_has(t: texture_2d<f32>) -> bool {
    return all(vec2i(textureDimensions(t)) == box_texgraph_grid());
}

// Parameters of the node whose bytes start at vec4 at: the p argument of a node function
fn box_texgraph_node_params(at: u32) -> array<vec4u, 4> {
    return array<vec4u, 4>(box_texgraph_params[at], box_texgraph_params[at + 1u],
        box_texgraph_params[at + 2u], box_texgraph_params[at + 3u]);
}

// Longitude and latitude of a position in texels of the sphere, with the texel centers at + 0.5.
// They are those of an EllipsoidShell whose uv is the uv of the image: the longitude turns from
// +x toward +z of the shell, the latitude rises toward its +y, which is -y of the directions, as
// the rows of the whole sphere run from +y down to -y while the v of a whole shell rises
fn box_texgraph_lonlat(pos: vec2f) -> vec2f {
    let r = box_texgraph_stage.region;
    let uv = pos / vec2f(box_texgraph_stage.size);
    return vec2f(mix(r.x, r.y, uv.x), mix(r.z, r.w, uv.y));
}

// Unit direction of a longitude and latitude of the sphere, y up and longitude along x as the
// environment images map it (-z at longitude pi, the center of the whole sphere)
fn box_texgraph_lonlat_dir(ll: vec2f) -> vec3f {
    let cl = cos(ll.y);
    return vec3f(-cl * sin(ll.x), -sin(ll.y), cl * cos(ll.x));
}

// Position in texels of a longitude and latitude of the sphere, the inverse of
// box_texgraph_lonlat: the longitude is taken the turn nearest the middle of the range
fn box_texgraph_lonlat_position(ll: vec2f) -> vec2f {
    let r = box_texgraph_stage.region;
    let mid = 0.5 * (r.x + r.y);
    let a = ll.x - mid + BOX_TEXGRAPH_PI;
    let lon = mid + a - 2.0 * BOX_TEXGRAPH_PI * floor(a / (2.0 * BOX_TEXGRAPH_PI)) -
        BOX_TEXGRAPH_PI;
    return vec2f((lon - r.x) / (r.y - r.x), (ll.y - r.z) / (r.w - r.z)) *
        vec2f(box_texgraph_stage.size);
}

// Angle in radians between the centers of neighbouring texels of the sphere: along a row at
// the equator in x, along a column in y
fn box_texgraph_texel_angle() -> vec2f {
    let r = box_texgraph_stage.region;
    return abs(vec2f(r.y - r.x, r.w - r.z)) / vec2f(box_texgraph_stage.size);
}

// Texels of a unit of the radii, reaches and ranges of the nodes: a texel of a 512 wide image on
// the plane, 2 pi / 512 rad on the sphere, in texels along a row at the equator, a meter along
// x on the world
fn box_texgraph_unit() -> f32 {
    if (box_texgraph_stage.domain == 2u) {
        return 1.0 / box_texgraph_stage.world.x;
    }

    if (box_texgraph_stage.domain == 1u) {
        return 2.0 * BOX_TEXGRAPH_PI / 512.0 / box_texgraph_texel_angle().x;
    }

    return f32(box_texgraph_stage.size.x) / 512.0;
}

// Whether the image of the sphere covers the whole longitude, so its rows wrap around
fn box_texgraph_full_longitude() -> bool {
    let r = box_texgraph_stage.region;
    return abs(r.y - r.x) >= 2.0 * BOX_TEXGRAPH_PI - 1e-4;
}

// Whether the top (x) and the bottom (y) edge of the image of the sphere is a pole
fn box_texgraph_poles() -> vec2<bool> {
    let r = box_texgraph_stage.region;
    let pole = 0.5 * BOX_TEXGRAPH_PI - 1e-4;
    return vec2<bool>(abs(r.z) >= pole, abs(r.w) >= pole);
}

// Texels half a turn of longitude along a row of the sphere
fn box_texgraph_half_turn() -> f32 {
    return BOX_TEXGRAPH_PI / box_texgraph_texel_angle().x;
}

// Column of the meridian opposite a column of the sphere, half a turn away: around the row with
// the whole longitude, else toward the inside of the image
fn box_texgraph_opposite(x: i32) -> i32 {
    let w = i32(box_texgraph_stage.size.x);
    if (box_texgraph_full_longitude()) {
        return x + w / 2;
    }

    let half = i32(round(box_texgraph_half_turn()));
    return select(x - half, x + half, x + half < w);
}

// Coordinates of the center of a texel in the domain of the graph: (u, v, 0, 1) on the plane
// with v 0 at the top, the unit direction of its longitude and latitude on the sphere
// (box_texgraph_lonlat), which for the whole sphere is the equirectangular texel, y up and
// longitude along x as the environment images map it (-z at u 0.5). On the world (x, z, 0, 1) in
// meters from the origin of the window, the texel of the grid at the apron at (0, 0)
fn box_texgraph_coords(texel: vec2i) -> vec4f {
    if (box_texgraph_stage.domain == 2u) {
        let at = vec2f(texel - vec2i(i32(box_texgraph_stage.apron)));
        return vec4f(at * box_texgraph_stage.world.xy, 0.0, 1.0);
    }

    if (box_texgraph_stage.domain == 1u) {
        return vec4f(box_texgraph_lonlat_dir(box_texgraph_lonlat(vec2f(texel) + 0.5)), 1.0);
    }

    let uv = (vec2f(texel) + 0.5) / vec2f(box_texgraph_stage.size);
    return vec4f(uv, 0.0, 1.0);
}

// Texel of the image at integer coordinates outside it: the plane repeats. The sphere wraps
// around in longitude when it covers the whole of it and continues past a pole it covers on the
// opposite meridian (a row above the top is the row below it half a turn away), up to one height
// beyond; past the other edges it clamps. The world clamps to its grid
fn box_texgraph_wrap(texel: vec2i) -> vec2i {
    let size = vec2i(box_texgraph_stage.size);
    var t = texel;
    if (box_texgraph_stage.domain == 2u) {
        return clamp(t, vec2i(0), box_texgraph_grid() - 1);
    }

    if (box_texgraph_stage.domain == 1u) {
        let poles = box_texgraph_poles();
        if (t.y < 0 && poles.x) {
            t = vec2i(box_texgraph_opposite(t.x), -1 - t.y);
        } else if (t.y >= size.y && poles.y) {
            t = vec2i(box_texgraph_opposite(t.x), 2 * size.y - 1 - t.y);
        }

        t.y = clamp(t.y, 0, size.y - 1);
        if (!box_texgraph_full_longitude()) {
            t.x = clamp(t.x, 0, size.x - 1);
            return t;
        }
    }

    let x = ((t.x % size.x) + size.x) % size.x;
    let y = ((t.y % size.y) + size.y) % size.y;
    return vec2i(x, y);
}

// Value of a materialized input at a texel, wrapped as the domain does; a scalar is in x
fn box_texgraph_load(t: texture_2d<f32>, texel: vec2i) -> vec4f {
    return textureLoad(t, box_texgraph_wrap(texel), 0);
}

// Texel of the image at integer coordinates outside it, continuing across the poles of the
// sphere: on an image of both poles a row past the top or the bottom is the row mirrored back on
// the opposite meridian, half the width around, any number of times; else as box_texgraph_wrap.
// The plane repeats as box_texgraph_wrap does
fn box_texgraph_wrap_poles(texel: vec2i) -> vec2i {
    let size = vec2i(box_texgraph_stage.size);
    var t = texel;
    if (box_texgraph_stage.domain == 1u && all(box_texgraph_poles())) {
        let period = 2 * size.y;
        var y = ((t.y % period) + period) % period;
        if (y >= size.y) {
            y = period - 1 - y;
            t.x = box_texgraph_opposite(t.x);
        }
        t.y = y;
    }

    return box_texgraph_wrap(t);
}

// Value of a materialized input at a texel, continuing across the poles on the sphere
fn box_texgraph_load_poles(t: texture_2d<f32>, texel: vec2i) -> vec4f {
    return textureLoad(t, box_texgraph_wrap_poles(texel), 0);
}

// Value of a materialized input between texels, bilinear by hand at a position in texels with
// the texel centers at + 0.5
fn box_texgraph_bilinear(t: texture_2d<f32>, pos: vec2f) -> vec4f {
    let p = pos - 0.5;
    let base = floor(p);
    let f = p - base;
    let i = vec2i(base);
    let a = box_texgraph_load(t, i);
    let b = box_texgraph_load(t, i + vec2i(1, 0));
    let c = box_texgraph_load(t, i + vec2i(0, 1));
    let d = box_texgraph_load(t, i + vec2i(1, 1));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// Position in texels of the image of a direction on the sphere, the inverse of
// box_texgraph_coords, with the texel centers at + 0.5; outside the range of the image it is
// outside the image, where the reads clamp
fn box_texgraph_sphere_position(d: vec3f) -> vec2f {
    let lat = acos(clamp(d.y, -1.0, 1.0)) - 0.5 * BOX_TEXGRAPH_PI;
    let lon = atan2(-d.x, d.z);
    return box_texgraph_lonlat_position(vec2f(lon, lat));
}

// Frame of the center of a texel of the sphere: its direction, the unit tangent toward growing
// u (east on the whole sphere) and the unit tangent toward shrinking v (north on the whole
// sphere), in the columns
fn box_texgraph_sphere_frame(texel: vec2i) -> mat3x3f {
    let r = box_texgraph_stage.region;
    let ll = box_texgraph_lonlat(vec2f(texel) + 0.5);
    let sl = sin(ll.y);
    let cl = cos(ll.y);
    let so = sin(ll.x);
    let co = cos(ll.x);
    let su = select(-1.0, 1.0, r.y >= r.x);
    let sv = select(-1.0, 1.0, r.w >= r.z);
    return mat3x3f(vec3f(-cl * so, -sl, cl * co), vec3f(-co, 0.0, -so) * su,
        vec3f(-sl * so, cl, sl * co) * sv);
}

// Differences of a materialized scalar input around a texel as GenNormal takes them: (east -
// west, south - north) of its neighbours, times strength and the texels of a unit
// (box_texgraph_unit), so the slope does not depend on the size or the range. On the plane the
// neighbours are the adjacent texels, wrapped. On the sphere they lie on the great circles
// through the texel toward growing u and shrinking v (east and north of the whole sphere), one
// texel of the equator away, read between texels: the spacing along a row follows the latitude,
// a whole longitude wraps, poles in the range are crossed and the other edges clamp. east and
// north point along the UV tangents of an EllipsoidShell of the range of the image (of a Sphere
// mesh for the whole sphere), at unit length: a normal map scales them with
// box_texgraph_normal_scale. On the world they are the central differences per meter along x
// and z times strength, the slope of a height in meters with strength 1.
fn box_texgraph_gradient(t: texture_2d<f32>, texel: vec2i, strength: f32) -> vec2f {
    if (box_texgraph_stage.domain == 2u) {
        let e = box_texgraph_load(t, texel + vec2i(1, 0)).x;
        let w = box_texgraph_load(t, texel - vec2i(1, 0)).x;
        let n = box_texgraph_load(t, texel - vec2i(0, 1)).x;
        let so = box_texgraph_load(t, texel + vec2i(0, 1)).x;
        return vec2f(e - w, so - n) * strength / (2.0 * box_texgraph_stage.world.xy);
    }

    let scale = strength * box_texgraph_unit();
    if (box_texgraph_stage.domain == 1u) {
        let frame = box_texgraph_sphere_frame(texel);
        let p = frame[0];
        let east = frame[1];
        let north = frame[2];
        let angle = box_texgraph_texel_angle().x;
        let c = p * cos(angle);
        let s = sin(angle);
        let e = box_texgraph_bilinear(t, box_texgraph_sphere_position(c + east * s)).x;
        let w = box_texgraph_bilinear(t, box_texgraph_sphere_position(c - east * s)).x;
        let n = box_texgraph_bilinear(t, box_texgraph_sphere_position(c + north * s)).x;
        let so = box_texgraph_bilinear(t, box_texgraph_sphere_position(c - north * s)).x;
        return vec2f(e - w, so - n) * scale;
    }

    let e = box_texgraph_load(t, texel + vec2i(1, 0)).x;
    let w = box_texgraph_load(t, texel - vec2i(1, 0)).x;
    let n = box_texgraph_load(t, texel - vec2i(0, 1)).x;
    let so = box_texgraph_load(t, texel + vec2i(0, 1)).x;
    return vec2f(e - w, so - n) * scale;
}

// Largest factor of box_texgraph_normal_scale near the poles, where the v tangent of the mesh
// vanishes: closer to a pole the relief fades instead of losing the precision of an RGBA8 normal
const BOX_TEXGRAPH_POLE_SCALE: f32 = 8.0;

// Factors of the differences of box_texgraph_gradient for a normal map. A mesh takes the frame of
// a normal map from its UVs (cotangent_frame in mesh_surface.wgsl): the gradients of u and v,
// scaled by one factor so the longer has unit length. On the UVs of an EllipsoidShell of the
// range of the image the gradient of u is 1 / r times that of v, r = |longitude range|
// cos(latitude) / |latitude range| (2 sin(theta) on the whole sphere, as a Sphere mesh, theta the
// angle from the north pole), so the u tangent has length min(1, 1 / r) and the v tangent
// min(1, r). The factors divide by them, so the normal decodes to the differences the sphere has.
// On the plane they are 1, as GenNormal.
fn box_texgraph_normal_scale(texel: vec2i) -> vec2f {
    if (box_texgraph_stage.domain != 1u) {
        return vec2f(1.0);
    }

    let r = box_texgraph_stage.region;
    let lat = box_texgraph_lonlat(vec2f(texel) + 0.5).y;
    let k = abs(r.y - r.x) * cos(lat) / max(abs(r.w - r.z), 1e-6);
    return vec2f(k, 1.0) * clamp(1.0 / k, 1.0, BOX_TEXGRAPH_POLE_SCALE);
}

// Equirectangular uv of a direction, the inverse of box_texgraph_coords on the whole sphere: u
// 0.5 at -z, v 0 at +y
fn box_texgraph_equirect(d: vec3f) -> vec2f {
    let len = length(d);
    let n = select(vec3f(0.0, 0.0, -1.0), d / len, len > 0.0);
    let phi = atan2(n.x, -n.z);
    let theta = acos(clamp(n.y, -1.0, 1.0));
    return vec2f(phi / (2.0 * BOX_TEXGRAPH_PI) + 0.5, theta / BOX_TEXGRAPH_PI);
}

// Texel of a sampled texture at integer coordinates, which repeat or clamp outside it: with wrap
// on the sphere only the longitude repeats and the rows clamp at the poles
fn box_texgraph_texel(t: texture_2d<f32>, texel: vec2i, wrap: bool) -> vec4f {
    let size = vec2i(textureDimensions(t, 0));
    var p = clamp(texel, vec2i(0), size - 1);
    if (wrap) {
        p.x = ((texel.x % size.x) + size.x) % size.x;
        if (box_texgraph_stage.domain != 1u) {
            p.y = ((texel.y % size.y) + size.y) % size.y;
        }
    }

    return textureLoad(t, p, 0);
}

// Bilinear sample of the first level of a sampled texture at uv, 0..1 across it with v 0 at the
// top row, repeating or clamping outside it; a single channel texture reads as (r, 0, 0, 1)
fn box_texgraph_sample(t: texture_2d<f32>, uv: vec2f, wrap: bool) -> vec4f {
    let size = vec2f(textureDimensions(t, 0));
    var q = uv;
    if (wrap) {
        q.x = fract(q.x);
        if (box_texgraph_stage.domain != 1u) {
            q.y = fract(q.y);
        }
    }

    let p = clamp(q, vec2f(-1.0), vec2f(2.0)) * size - 0.5;
    let base = floor(p);
    let f = p - base;
    let i = vec2i(base);
    let a = box_texgraph_texel(t, i, wrap);
    let b = box_texgraph_texel(t, i + vec2i(1, 0), wrap);
    let c = box_texgraph_texel(t, i + vec2i(0, 1), wrap);
    let d = box_texgraph_texel(t, i + vec2i(1, 1), wrap);
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// PCG hash of a word
fn box_texgraph_hash(x: u32) -> u32 {
    let state = x * 747796405u + 2891336453u;
    let word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// Hash of two words
fn box_texgraph_hash2(p: vec2u) -> u32 {
    return box_texgraph_hash(p.x ^ box_texgraph_hash(p.y));
}

// Hash of three words
fn box_texgraph_hash3(p: vec3u) -> u32 {
    return box_texgraph_hash(p.x ^ box_texgraph_hash(p.y ^ box_texgraph_hash(p.z)));
}

// Uniform random number in 0..1 of a hash
fn box_texgraph_random(h: u32) -> f32 {
    return f32(h >> 8u) / 16777216.0;
}

// Display encoding of linear light, clamped to 0..1
fn box_texgraph_srgb(c: vec3f) -> vec3f {
    let x = clamp(c, vec3f(0.0), vec3f(1.0));
    return select(1.055 * pow(x, vec3f(1.0 / 2.4)) - 0.055, x * 12.92, x <= vec3f(0.0031308));
}

// Linear light of a display encoded color, the inverse of box_texgraph_srgb, clamped to 0..1
fn box_texgraph_linear(c: vec3f) -> vec3f {
    let x = clamp(c, vec3f(0.0), vec3f(1.0));
    return select(pow((x + 0.055) / 1.055, vec3f(2.4)), x / 12.92, x <= vec3f(0.04045));
}

// Value the output stage stores, as box_texgraph_stage.encode asks
fn box_texgraph_encode(v: vec4f) -> vec4f {
    switch box_texgraph_stage.encode {
        case 1u: {
            return vec4f(box_texgraph_srgb(v.rgb), clamp(v.a, 0.0, 1.0));
        }
        case 2u: {
            return vec4f(v.x, 0.0, 0.0, 1.0);
        }
        default: {
            return v;
        }
    }
}

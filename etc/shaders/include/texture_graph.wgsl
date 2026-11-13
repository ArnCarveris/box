// Shared code of the compute stages the GPU backend generates for the texture graphs
// (src/render/gpu_texture_graph.c). A stage evaluates its nodes per texel in workgroups of 8x8
// and stores the value of its last node in box_texgraph_out at binding 2. The node kinds call
// the hashing and the reads of materialized inputs below.

const BOX_TEXGRAPH_PI: f32 = 3.141592653589793;

// Uniforms of the stages of a graph
struct BoxTexGraphStage {
    // Size of the image in texels
    size: vec2u,
    // TextureGraphDomain of the coordinates: 0 the plane, 1 the sphere
    domain: u32,
    // How the output stage stores its value: 0 as computed, 1 display encoded with alpha kept,
    // 2 the first channel as (x, 0, 0, 1)
    encode: u32,
};

// Parameter bytes of every node of the graph, at the offsets of the compiled graph
@group(0) @binding(0) var<storage, read> box_texgraph_params: array<vec4u>;
@group(0) @binding(1) var<uniform> box_texgraph_stage: BoxTexGraphStage;

// Parameters of the node whose bytes start at vec4 at: the p argument of a node function
fn box_texgraph_node_params(at: u32) -> array<vec4u, 4> {
    return array<vec4u, 4>(box_texgraph_params[at], box_texgraph_params[at + 1u],
        box_texgraph_params[at + 2u], box_texgraph_params[at + 3u]);
}

// Coordinates of the center of a texel in the domain of the graph: (u, v, 0, 1) on the plane
// with v 0 at the top, the unit direction of the equirectangular texel on the sphere, y up and
// longitude along x as the environment images map it (-z at u 0.5)
fn box_texgraph_coords(texel: vec2i) -> vec4f {
    let uv = (vec2f(texel) + 0.5) / vec2f(box_texgraph_stage.size);
    if (box_texgraph_stage.domain == 1u) {
        let phi = (uv.x - 0.5) * 2.0 * BOX_TEXGRAPH_PI;
        let theta = uv.y * BOX_TEXGRAPH_PI;
        return vec4f(sin(theta) * sin(phi), cos(theta), -sin(theta) * cos(phi), 1.0);
    }

    return vec4f(uv, 0.0, 1.0);
}

// Texel of the image at integer coordinates outside it: the plane repeats, the sphere wraps
// around in longitude and continues past a pole on the opposite meridian (a row above the top
// is the row below it half a turn away), up to one height beyond
fn box_texgraph_wrap(texel: vec2i) -> vec2i {
    let size = vec2i(box_texgraph_stage.size);
    var t = texel;
    if (box_texgraph_stage.domain == 1u) {
        if (t.y < 0) {
            t = vec2i(t.x + size.x / 2, -1 - t.y);
        } else if (t.y >= size.y) {
            t = vec2i(t.x + size.x / 2, 2 * size.y - 1 - t.y);
        }

        t.y = clamp(t.y, 0, size.y - 1);
    }

    let x = ((t.x % size.x) + size.x) % size.x;
    let y = ((t.y % size.y) + size.y) % size.y;
    return vec2i(x, y);
}

// Value of a materialized input at a texel, wrapped as the domain does; a scalar is in x
fn box_texgraph_load(t: texture_2d<f32>, texel: vec2i) -> vec4f {
    return textureLoad(t, box_texgraph_wrap(texel), 0);
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
// box_texgraph_coords, with the texel centers at + 0.5
fn box_texgraph_sphere_position(d: vec3f) -> vec2f {
    let theta = acos(clamp(d.y, -1.0, 1.0));
    let phi = atan2(d.x, -d.z);
    let uv = vec2f(phi / (2.0 * BOX_TEXGRAPH_PI) + 0.5, theta / BOX_TEXGRAPH_PI);
    return uv * vec2f(box_texgraph_stage.size);
}

// Differences of a materialized scalar input around a texel as GenNormal takes them: (east -
// west, south - north) of its neighbours, times strength (0 for 1) and the width over 512, so
// the slope does not depend on the size. On the plane the neighbours are the adjacent texels,
// wrapped. On the sphere they lie on the great circles through the texel toward east (growing
// u) and north (shrinking v), one texel of the equator away, read between texels: the spacing
// along a row follows the latitude, the longitude wraps and the poles are crossed. east and
// north are the frame of the UV tangents of a Sphere mesh, so the normal maps match it.
fn box_texgraph_gradient(t: texture_2d<f32>, texel: vec2i, strength: f32) -> vec2f {
    let size = vec2f(box_texgraph_stage.size);
    let scale = select(strength, 1.0, strength == 0.0) * size.x / 512.0;
    if (box_texgraph_stage.domain == 1u) {
        let uv = (vec2f(texel) + 0.5) / size;
        let phi = (uv.x - 0.5) * 2.0 * BOX_TEXGRAPH_PI;
        let theta = uv.y * BOX_TEXGRAPH_PI;
        let st = sin(theta);
        let ct = cos(theta);
        let sp = sin(phi);
        let cp = cos(phi);
        let p = vec3f(st * sp, ct, -st * cp);
        let east = vec3f(cp, 0.0, sp);
        let north = vec3f(-ct * sp, st, ct * cp);
        let angle = 2.0 * BOX_TEXGRAPH_PI / size.x;
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

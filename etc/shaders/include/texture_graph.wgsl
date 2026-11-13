// Shared code of the compute stages the GPU backend generates for the texture graphs
// (src/render/gpu_texture_graph.c). A stage evaluates its nodes per texel in workgroups of 8x8
// and stores the value of its last node in box_texgraph_out at binding 2. The node kinds call
// the hashing, the reads of materialized inputs and the samples of bound textures below.

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
    // Seconds of the world clock at the evaluation, which TexTime reads
    time: f32,
    pad0: u32,
    pad1: u32,
    pad2: u32,
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
// around in longitude and clamps at the poles
fn box_texgraph_wrap(texel: vec2i) -> vec2i {
    let size = vec2i(box_texgraph_stage.size);
    let x = ((texel.x % size.x) + size.x) % size.x;
    var y = ((texel.y % size.y) + size.y) % size.y;
    if (box_texgraph_stage.domain == 1u) {
        y = clamp(texel.y, 0, size.y - 1);
    }

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

// Equirectangular uv of a direction, the inverse of box_texgraph_coords on the sphere: u 0.5 at
// -z, v 0 at +y
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

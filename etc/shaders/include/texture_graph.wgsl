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

// Texel of the image at integer coordinates outside it, continuing across the poles of the
// sphere: a row past the top or the bottom is the row mirrored back on the opposite meridian,
// half the width around; the plane repeats as box_texgraph_wrap does
fn box_texgraph_wrap_poles(texel: vec2i) -> vec2i {
    let size = vec2i(box_texgraph_stage.size);
    var t = texel;
    if (box_texgraph_stage.domain == 1u) {
        let period = 2 * size.y;
        var y = ((t.y % period) + period) % period;
        if (y >= size.y) {
            y = period - 1 - y;
            t.x = t.x + size.x / 2;
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

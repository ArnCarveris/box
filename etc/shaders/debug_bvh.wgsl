// Boxes of the spatial index trees of the debug pass of a view (BvhDebug): instance k draws the
// 12 edges of box k as lines, in the color of its tree, fainter with its depth and at half the
// alpha for the leaf of an entity.
#include "include/lights.wgsl"

// Colors of the static, dynamic and UI tree, linear.
const STATIC_COLOR: vec3f = vec3f(0.1, 0.85, 0.15);
const DYNAMIC_COLOR: vec3f = vec3f(1.0, 0.35, 0.02);
const UI_COLOR: vec3f = vec3f(0.9, 0.1, 0.9);

// Bit of BvhBox.kind of the leaf of an entity, BOX_RENDER_DEBUG_BOX_LEAF.
const LEAF: u32 = 4u;

// Alpha of the root and of the boxes at FADE_DEPTH and below.
const ROOT_ALPHA: f32 = 1.0;
const DEEP_ALPHA: f32 = 0.35;
const FADE_DEPTH: f32 = 12.0;

// Scale of the reversed depth of a line, which pulls it toward the camera so that a box on a
// surface is not hidden by it.
const DEPTH_PULL: f32 = 1.0005;

// A box, matching box_render_debug_box_t.
struct BvhBox {
    min: vec3f,
    // Tree: 0 static, 1 dynamic, 2 UI, with LEAF for the leaf of an entity
    kind: u32,
    max: vec3f,
    // Depth in its tree, 0 for the root
    depth: u32,
};

@group(0) @binding(0) var<uniform> view: View;
@group(1) @binding(0) var<storage, read> boxes: array<BvhBox>;

struct LineOut {
    @builtin(position) pos: vec4f,
    @location(0) @interpolate(flat) color: vec4f,
};

// End i of the 12 edges of a box: edge i / 2 runs along axis i / 8.
fn box_corner(b: BvhBox, i: u32) -> vec3f {
    let edge = i / 2u;
    let axis = edge / 4u;
    let k = edge % 4u;
    let e = f32(i & 1u);
    let b0 = f32(k & 1u);
    let b1 = f32((k >> 1u) & 1u);
    var c = vec3f(b0, b1, e);
    if (axis == 0u) {
        c = vec3f(e, b0, b1);
    } else if (axis == 1u) {
        c = vec3f(b0, e, b1);
    }

    return mix(b.min, b.max, c);
}

@vertex fn vs(@builtin(vertex_index) i: u32, @builtin(instance_index) instance: u32) ->
    LineOut
{
    let b = boxes[instance];
    let tree = b.kind & 3u;
    var color = STATIC_COLOR;
    if (tree == 1u) {
        color = DYNAMIC_COLOR;
    } else if (tree == 2u) {
        color = UI_COLOR;
    }

    var alpha = mix(ROOT_ALPHA, DEEP_ALPHA, saturate(f32(b.depth) / FADE_DEPTH));
    if ((b.kind & LEAF) != 0u) {
        alpha *= 0.5;
    }

    var o: LineOut;
    o.pos = view.view_proj * vec4f(box_corner(b, i % 24u), 1.0);
    o.pos.z *= DEPTH_PULL;
    o.color = vec4f(color * alpha, alpha);
    return o;
}

@fragment fn fs(in: LineOut) -> @location(0) vec4f {
    return in.color;
}

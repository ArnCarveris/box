// Ground truth ambient occlusion, a port of XeGTAO (Intel, MIT) as Esoterica runs it, computed
// from the reversed Z depth of the depth prepass before the forward pass, which multiplies the
// indirect light by it (mesh.wgsl).
#include "include/fullscreen.wgsl"

struct Gtao {
    // working width, height and their inverse
    viewport: vec4f,
    // NDC to view space: multiplier xy, addend xy, for a working pixel position in 0..1
    ndc_to_view: vec4f,
    // NDC to view multiplier times the working pixel size xy, depth unpack numerator and addend
    unpack: vec4f,
    // effect radius (times the radius multiplier), falloff range, final value power, blur beta
    effect: vec4f,
    // slices, steps per slice, sample distribution power, thin occluder compensation
    quality: vec4f,
    // depth mip sampling offset, 1 at half resolution, two unused
    extent: vec4f,
};

@group(0) @binding(0) var<uniform> gtao: Gtao;
@group(0) @binding(1) var depth_texture: texture_depth_2d;
@group(0) @binding(2) var out_depth0: texture_storage_2d<r32float, write>;
@group(0) @binding(3) var out_depth1: texture_storage_2d<r32float, write>;
@group(0) @binding(4) var out_depth2: texture_storage_2d<r32float, write>;
@group(0) @binding(5) var out_depth3: texture_storage_2d<r32float, write>;
@group(0) @binding(6) var source_mip: texture_2d<f32>;
@group(0) @binding(7) var out_depth4: texture_storage_2d<r32float, write>;
@group(0) @binding(8) var depth_mips: texture_2d<f32>;
@group(0) @binding(9) var out_term: texture_storage_2d<r32float, write>;
@group(0) @binding(10) var out_edges: texture_storage_2d<r32float, write>;
@group(0) @binding(11) var source_term: texture_2d<f32>;
@group(0) @binding(12) var source_edges: texture_2d<f32>;
@group(0) @binding(13) var out_ao: texture_storage_2d<r32float, write>;

const PI: f32 = 3.141592653589793;
const PI_HALF: f32 = 1.5707963267948966;
const DEPTH_MIP_LEVELS: f32 = 5.0;
const OCCLUSION_TERM_SCALE: f32 = 1.5;
// View depth of a pixel without depth, far enough that it occludes nothing and small enough
// that positions derived from it stay finite.
const SKY_DEPTH: f32 = 1e6;

// Positive view depth of a reversed Z depth, SKY_DEPTH where nothing was drawn.
fn view_depth(depth: f32) -> f32 {
    if (depth <= 0.0) {
        return SKY_DEPTH;
    }

    return clamp(gtao.unpack.z / (gtao.unpack.w + depth), 0.0, SKY_DEPTH);
}

// View depth of a working pixel: the depth of the pixel, or at half resolution of the top left
// pixel of its 2x2 block.
fn source_depth(p: vec2i) -> f32 {
    let size = vec2i(textureDimensions(depth_texture));
    let full = select(p, p * 2, gtao.extent.y > 0.0);
    return view_depth(textureLoad(depth_texture, clamp(full, vec2i(0), size - 1), 0));
}

// View space position of a working pixel position in 0..1 and its view depth; z is negative in
// front of the camera.
fn view_position(screen: vec2f, depth: f32) -> vec3f {
    return vec3f((gtao.ndc_to_view.xy * screen + gtao.ndc_to_view.zw) * depth, -depth);
}

// XeGTAO_DepthMIPFilter: weighted average depth that drops the depths far in front of the
// farthest one.
fn depth_mip_filter(d0: f32, d1: f32, d2: f32, d3: f32) -> f32 {
    let max_depth = max(max(d0, d1), max(d2, d3));
    let effect_radius = max(0.75 * gtao.effect.x, 1e-4);
    let falloff_range = max(gtao.effect.y * effect_radius, 1e-6);
    let falloff_from = effect_radius * (1.0 - gtao.effect.y);
    let falloff_mul = -1.0 / falloff_range;
    let falloff_add = falloff_from / falloff_range + 1.0;
    let w0 = clamp((max_depth - d0) * falloff_mul + falloff_add, 0.0, 1.0);
    let w1 = clamp((max_depth - d1) * falloff_mul + falloff_add, 0.0, 1.0);
    let w2 = clamp((max_depth - d2) * falloff_mul + falloff_add, 0.0, 1.0);
    let w3 = clamp((max_depth - d3) * falloff_mul + falloff_add, 0.0, 1.0);
    let sum = w0 + w1 + w2 + w3;
    return (w0 * d0 + w1 * d1 + w2 * d2 + w3 * d3) / sum;
}

fn store_depth0(p: vec2i, value: f32) {
    if (all(p < vec2i(textureDimensions(out_depth0)))) {
        textureStore(out_depth0, p, vec4f(value, 0.0, 0.0, 0.0));
    }
}

var<workgroup> scratch: array<array<f32, 8>, 8>;

// XeGTAO_PrefilterDepths16x16 up to mip 3: each invocation writes 2x2 texels of mip 0. The
// writes are guarded instead of returning early, so the barriers stay in uniform control flow.
@compute @workgroup_size(8, 8) fn prefilter(
    @builtin(global_invocation_id) gid: vec3u,
    @builtin(local_invocation_id) lid: vec3u)
{
    let base = vec2i(gid.xy);
    let pix = base * 2;
    let d0 = source_depth(pix);
    let d1 = source_depth(pix + vec2i(1, 0));
    let d2 = source_depth(pix + vec2i(0, 1));
    let d3 = source_depth(pix + vec2i(1, 1));
    store_depth0(pix, d0);
    store_depth0(pix + vec2i(1, 0), d1);
    store_depth0(pix + vec2i(0, 1), d2);
    store_depth0(pix + vec2i(1, 1), d3);

    let dm1 = depth_mip_filter(d0, d1, d2, d3);
    if (all(base < vec2i(textureDimensions(out_depth1)))) {
        textureStore(out_depth1, base, vec4f(dm1, 0.0, 0.0, 0.0));
    }

    scratch[lid.x][lid.y] = dm1;
    workgroupBarrier();

    if (all(lid.xy % vec2u(2u) == vec2u(0u))) {
        let dm2 = depth_mip_filter(scratch[lid.x][lid.y], scratch[lid.x + 1u][lid.y],
            scratch[lid.x][lid.y + 1u], scratch[lid.x + 1u][lid.y + 1u]);
        let p2 = base / 2;
        if (all(p2 < vec2i(textureDimensions(out_depth2)))) {
            textureStore(out_depth2, p2, vec4f(dm2, 0.0, 0.0, 0.0));
        }

        scratch[lid.x][lid.y] = dm2;
    }

    workgroupBarrier();

    if (all(lid.xy % vec2u(4u) == vec2u(0u))) {
        let dm3 = depth_mip_filter(scratch[lid.x][lid.y], scratch[lid.x + 2u][lid.y],
            scratch[lid.x][lid.y + 2u], scratch[lid.x + 2u][lid.y + 2u]);
        let p3 = base / 4;
        if (all(p3 < vec2i(textureDimensions(out_depth3)))) {
            textureStore(out_depth3, p3, vec4f(dm3, 0.0, 0.0, 0.0));
        }
    }
}

fn source_mip_at(p: vec2i) -> f32 {
    let size = vec2i(textureDimensions(source_mip));
    return textureLoad(source_mip, clamp(p, vec2i(0), size - 1), 0).r;
}

// Mip 4 of the depth from mip 3, as the last step of XeGTAO_PrefilterDepths16x16.
@compute @workgroup_size(8, 8) fn prefilter_last(@builtin(global_invocation_id) gid: vec3u) {
    let p = vec2i(gid.xy);
    if (any(p >= vec2i(textureDimensions(out_depth4)))) {
        return;
    }

    let s = p * 2;
    let d = depth_mip_filter(source_mip_at(s), source_mip_at(s + vec2i(1, 0)),
        source_mip_at(s + vec2i(0, 1)), source_mip_at(s + vec2i(1, 1)));
    textureStore(out_depth4, p, vec4f(d, 0.0, 0.0, 0.0));
}

// Depth of a working pixel at a mip, clamped to the edge of the image of that mip.
fn depth_at(p: vec2i, mip: i32) -> f32 {
    let size = max(vec2i(gtao.viewport.xy) >> vec2u(u32(mip)), vec2i(1));
    return textureLoad(depth_mips, clamp(p, vec2i(0), size - 1), mip).r;
}

// Point sample of the depth at a working position in 0..1 and a mip level, nearest mip.
fn depth_sample(screen: vec2f, mip_level: f32) -> f32 {
    let mip = i32(clamp(floor(mip_level + 0.5), 0.0, DEPTH_MIP_LEVELS - 1.0));
    let size = max(vec2i(gtao.viewport.xy) >> vec2u(u32(mip)), vec2i(1));
    return depth_at(vec2i(floor(screen * vec2f(size))), mip);
}

fn calculate_edges(center: f32, left: f32, right: f32, top: f32, bottom: f32) -> vec4f {
    var edges = vec4f(left, right, top, bottom) - center;
    let slope_lr = (edges.y - edges.x) * 0.5;
    let slope_tb = (edges.w - edges.z) * 0.5;
    let adjusted = edges + vec4f(slope_lr, -slope_lr, slope_tb, -slope_tb);
    edges = min(abs(edges), abs(adjusted));
    return clamp(1.25 - edges / (center * 0.011), vec4f(0.0), vec4f(1.0));
}

fn pack_edges(edges: vec4f) -> f32 {
    let e = round(clamp(edges, vec4f(0.0), vec4f(1.0)) * 2.9);
    return dot(e, vec4f(64.0 / 255.0, 16.0 / 255.0, 4.0 / 255.0, 1.0 / 255.0));
}

fn unpack_edges(packed: f32) -> vec4f {
    let v = u32(packed * 255.5);
    let e = vec4f(f32((v >> 6u) & 3u), f32((v >> 4u) & 3u), f32((v >> 2u) & 3u),
        f32(v & 3u)) / 3.0;
    return clamp(e, vec4f(0.0), vec4f(1.0));
}

fn calculate_normal(edges: vec4f, center: vec3f, left: vec3f, right: vec3f, top: vec3f,
    bottom: vec3f) -> vec3f
{
    let accepted = clamp(vec4f(edges.x * edges.z, edges.z * edges.y, edges.y * edges.w,
        edges.w * edges.x) + 0.01, vec4f(0.0), vec4f(1.0));
    let l = normalize(left - center);
    let r = normalize(right - center);
    let t = normalize(top - center);
    let b = normalize(bottom - center);
    let n = accepted.x * cross(t, l) + accepted.y * cross(r, t) + accepted.z * cross(b, r) +
        accepted.w * cross(l, b);
    return normalize(n);
}

fn fast_sqrt(x: f32) -> f32 {
    return bitcast<f32>(0x1fbd1df5 + (bitcast<i32>(x) >> 1u));
}

// Input -1..1, output 0..PI.
fn fast_acos(in_x: f32) -> f32 {
    let x = abs(in_x);
    var res = -0.156583 * x + PI_HALF;
    res *= fast_sqrt(1.0 - x);
    return select(PI - res, res, in_x >= 0.0);
}

const HILBERT_WIDTH: u32 = 64u;

fn hilbert_index(x: u32, y: u32) -> u32 {
    var px = x;
    var py = y;
    var index = 0u;
    for (var level = HILBERT_WIDTH / 2u; level > 0u; level /= 2u) {
        let rx = select(0u, 1u, (px & level) > 0u);
        let ry = select(0u, 1u, (py & level) > 0u);
        index += level * level * ((3u * rx) ^ ry);
        if (ry == 0u) {
            if (rx == 1u) {
                px = (HILBERT_WIDTH - 1u) - px;
                py = (HILBERT_WIDTH - 1u) - py;
            }

            let temp = px;
            px = py;
            py = temp;
        }
    }

    return index;
}

// R2 sequence along the Hilbert curve, with noise index 0: the same every frame.
fn spatio_temporal_noise(p: vec2u) -> vec2f {
    let index = hilbert_index(p.x, p.y);
    return fract(0.5 + f32(index) * vec2f(0.75487766624669276005, 0.5698402909980532659114));
}

// Depth of a neighbor outside the image from the depth of the pixel and of its opposite neighbor.
fn border_depth(center: f32, opposite: f32) -> f32 {
    return max(2.0 * center - opposite, 0.5 * center);
}

fn output_term(p: vec2i, visibility: f32) {
    let v = clamp(visibility / OCCLUSION_TERM_SCALE, 0.0, 1.0);
    textureStore(out_term, p, vec4f(v, 0.0, 0.0, 0.0));
}

// XeGTAO_MainPass with the normal generated in place, without bent normals.
@compute @workgroup_size(8, 8) fn main_pass(@builtin(global_invocation_id) gid: vec3u) {
    let pix = vec2i(gid.xy);
    if (any(pix >= vec2i(gtao.viewport.xy))) {
        return;
    }

    let pixel_size = gtao.viewport.zw;
    let screen = (vec2f(pix) + 0.5) * pixel_size;
    let size = vec2i(gtao.viewport.xy);
    var z = depth_at(pix, 0);
    var left_z = depth_at(pix + vec2i(-1, 0), 0);
    var top_z = depth_at(pix + vec2i(0, -1), 0);
    var right_z = depth_at(pix + vec2i(1, 0), 0);
    var bottom_z = depth_at(pix + vec2i(0, 1), 0);
    // A neighbor outside the image continues the slope of the opposite one, so the normal of a
    // pixel on the border is not bent toward the camera by a repeated depth.
    if (pix.x == 0) {
        left_z = border_depth(z, right_z);
    }
    if (pix.x == size.x - 1) {
        right_z = border_depth(z, left_z);
    }
    if (pix.y == 0) {
        top_z = border_depth(z, bottom_z);
    }
    if (pix.y == size.y - 1) {
        bottom_z = border_depth(z, top_z);
    }
    let edges = calculate_edges(z, left_z, right_z, top_z, bottom_z);
    textureStore(out_edges, pix, vec4f(pack_edges(edges), 0.0, 0.0, 0.0));

    let effect_radius = gtao.effect.x;
    if (z >= SKY_DEPTH || effect_radius <= 0.0) {
        output_term(pix, 1.0);
        return;
    }

    let center = view_position(screen, z);
    let left = view_position(screen + vec2f(-1.0, 0.0) * pixel_size, left_z);
    let right = view_position(screen + vec2f(1.0, 0.0) * pixel_size, right_z);
    let top = view_position(screen + vec2f(0.0, -1.0) * pixel_size, top_z);
    let bottom = view_position(screen + vec2f(0.0, 1.0) * pixel_size, bottom_z);
    let normal = calculate_normal(edges, center, left, right, top, bottom);

    // Move the center slightly toward the camera against depth imprecision (FP32 depth).
    z *= 0.99999;
    let pos = view_position(screen, z);
    let view_vec = normalize(-pos);

    let sample_power = gtao.quality.z;
    let thin_occluder = gtao.quality.w;
    let falloff_range = max(gtao.effect.y * effect_radius, 1e-6);
    let falloff_from = effect_radius * (1.0 - gtao.effect.y);
    let falloff_mul = -1.0 / falloff_range;
    let falloff_add = falloff_from / falloff_range + 1.0;

    let noise = spatio_temporal_noise(gid.xy);
    let slice_count = gtao.quality.x;
    let steps = gtao.quality.y;
    let pixel_too_close = 1.3;
    let pixel_view_size = z * gtao.unpack.xy;
    let screen_radius = effect_radius / pixel_view_size.x;
    var visibility = clamp((10.0 - screen_radius) / 100.0, 0.0, 1.0) * 0.5;
    let min_s = pixel_too_close / screen_radius;

    for (var slice = 0.0; slice < slice_count; slice += 1.0) {
        let slice_k = (slice + noise.x) / slice_count;
        let phi = slice_k * PI;
        let cos_phi = cos(phi);
        let sin_phi = sin(phi);
        let omega = vec2f(cos_phi, -sin_phi) * screen_radius;
        let direction = vec3f(cos_phi, sin_phi, 0.0);
        let ortho_direction = direction - dot(direction, view_vec) * view_vec;
        let axis = normalize(cross(ortho_direction, view_vec));
        let projected = normal - axis * dot(normal, axis);
        let sign_norm = sign(dot(ortho_direction, projected));
        var projected_length = length(projected);
        let cos_norm = clamp(dot(projected, view_vec) / projected_length, 0.0, 1.0);
        let n = sign_norm * fast_acos(cos_norm);
        let low_horizon_cos0 = cos(n + PI_HALF);
        let low_horizon_cos1 = cos(n - PI_HALF);
        var horizon_cos0 = low_horizon_cos0;
        var horizon_cos1 = low_horizon_cos1;

        for (var k = 0.0; k < steps; k += 1.0) {
            let step_base_noise = (slice + k * steps) * 0.6180339887498948482;
            let step_noise = fract(noise.y + step_base_noise);
            var s = (k + step_noise) / steps;
            s = pow(s, sample_power);
            s += min_s;
            var offset = s * omega;
            let offset_length = length(offset);
            let mip_level = clamp(log2(offset_length) - gtao.extent.x, 0.0, DEPTH_MIP_LEVELS);
            offset = round(offset) * pixel_size;

            let screen0 = screen + offset;
            let sample0 = view_position(screen0, depth_sample(screen0, mip_level));
            let screen1 = screen - offset;
            let sample1 = view_position(screen1, depth_sample(screen1, mip_level));
            let delta0 = sample0 - pos;
            let delta1 = sample1 - pos;
            let dist0 = length(delta0);
            let dist1 = length(delta1);
            let horizon0 = delta0 / dist0;
            let horizon1 = delta1 / dist1;
            let falloff0 = length(vec3f(delta0.xy, delta0.z * (1.0 + thin_occluder)));
            let falloff1 = length(vec3f(delta1.xy, delta1.z * (1.0 + thin_occluder)));
            let weight0 = clamp(falloff0 * falloff_mul + falloff_add, 0.0, 1.0);
            let weight1 = clamp(falloff1 * falloff_mul + falloff_add, 0.0, 1.0);
            var shc0 = dot(horizon0, view_vec);
            var shc1 = dot(horizon1, view_vec);
            shc0 = mix(low_horizon_cos0, shc0, weight0);
            shc1 = mix(low_horizon_cos1, shc1, weight1);
            horizon_cos0 = max(horizon_cos0, shc0);
            horizon_cos1 = max(horizon_cos1, shc1);
        }

        projected_length = mix(projected_length, 1.0, 0.05);
        let h0 = -fast_acos(horizon_cos1);
        let h1 = fast_acos(horizon_cos0);
        let iarc0 = (cos_norm + 2.0 * h0 * sin(n) - cos(2.0 * h0 - n)) / 4.0;
        let iarc1 = (cos_norm + 2.0 * h1 * sin(n) - cos(2.0 * h1 - n)) / 4.0;
        visibility += projected_length * (iarc0 + iarc1);
    }

    visibility /= slice_count;
    visibility = pow(max(visibility, 1e-6), gtao.effect.z);
    visibility = max(0.03, visibility);
    output_term(pix, visibility);
}

fn term_at(p: vec2i) -> f32 {
    let size = vec2i(textureDimensions(source_term));
    return textureLoad(source_term, clamp(p, vec2i(0), size - 1), 0).r;
}

fn edges_at(p: vec2i) -> f32 {
    let size = vec2i(textureDimensions(source_edges));
    return textureLoad(source_edges, clamp(p, vec2i(0), size - 1), 0).r;
}

// The 2x2 quad GatherRed returns at the texel corner p: (p.x - 1, p.y), p, (p.x, p.y - 1) and
// (p.x - 1, p.y - 1).
fn gather_term(p: vec2i) -> vec4f {
    return vec4f(term_at(p + vec2i(-1, 0)), term_at(p), term_at(p + vec2i(0, -1)),
        term_at(p + vec2i(-1, -1)));
}

fn gather_edges(p: vec2i) -> vec4f {
    return vec4f(edges_at(p + vec2i(-1, 0)), edges_at(p), edges_at(p + vec2i(0, -1)),
        edges_at(p + vec2i(-1, -1)));
}

fn add_sample(value: f32, weight: f32, sum: ptr<function, f32>, sum_weight: ptr<function, f32>) {
    *sum += weight * value;
    *sum_weight += weight;
}

// XeGTAO_Denoise of the two horizontal pixels of an invocation.
fn denoise_pair(base: vec2i, final_apply: bool) {
    let blur_amount = select(gtao.effect.w / 5.0, gtao.effect.w, final_apply);
    let diag_weight = 0.85 * 0.5;
    let edges_q0 = gather_edges(base);
    let edges_q1 = gather_edges(base + vec2i(2, 0));
    let edges_q2 = gather_edges(base + vec2i(1, 2));
    let vis_q0 = gather_term(base);
    let vis_q1 = gather_term(base + vec2i(2, 0));
    let vis_q2 = gather_term(base + vec2i(0, 2));
    let vis_q3 = gather_term(base + vec2i(2, 2));
    let size = vec2i(textureDimensions(out_ao));

    for (var side = 0; side < 2; side ++) {
        let pix = vec2i(base.x + side, base.y);
        let left_side = side == 0;
        let edges_l = unpack_edges(select(edges_q0.y, edges_q0.x, left_side));
        let edges_t = unpack_edges(select(edges_q1.w, edges_q0.z, left_side));
        let edges_r = unpack_edges(select(edges_q1.y, edges_q1.x, left_side));
        let edges_b = unpack_edges(select(edges_q2.z, edges_q2.w, left_side));
        var edges_c = unpack_edges(select(edges_q1.x, edges_q0.y, left_side));
        edges_c *= vec4f(edges_l.y, edges_r.x, edges_t.w, edges_b.z);
        let leak_threshold = 2.5;
        let leak_strength = 0.5;
        let edginess = (clamp(4.0 - leak_threshold - dot(edges_c, vec4f(1.0)), 0.0, 1.0) /
            (4.0 - leak_threshold)) * leak_strength;
        edges_c = clamp(edges_c + edginess, vec4f(0.0), vec4f(1.0));

        let weight_tl = diag_weight * (edges_c.x * edges_l.z + edges_c.z * edges_t.x);
        let weight_tr = diag_weight * (edges_c.z * edges_t.y + edges_c.y * edges_r.z);
        let weight_bl = diag_weight * (edges_c.w * edges_b.x + edges_c.x * edges_l.w);
        let weight_br = diag_weight * (edges_c.y * edges_r.w + edges_c.w * edges_b.y);

        let value = select(vis_q1.x, vis_q0.y, left_side);
        let value_l = select(vis_q0.y, vis_q0.x, left_side);
        let value_t = select(vis_q1.w, vis_q0.z, left_side);
        let value_r = select(vis_q1.y, vis_q1.x, left_side);
        let value_b = select(vis_q3.w, vis_q2.z, left_side);
        let value_tl = select(vis_q0.z, vis_q0.w, left_side);
        let value_br = select(vis_q3.z, vis_q3.w, left_side);
        let value_tr = select(vis_q1.z, vis_q1.w, left_side);
        let value_bl = select(vis_q2.z, vis_q2.w, left_side);

        var sum_weight = blur_amount;
        var sum = value * sum_weight;
        add_sample(value_l, edges_c.x, &sum, &sum_weight);
        add_sample(value_r, edges_c.y, &sum, &sum_weight);
        add_sample(value_t, edges_c.z, &sum, &sum_weight);
        add_sample(value_b, edges_c.w, &sum, &sum_weight);
        add_sample(value_tl, weight_tl, &sum, &sum_weight);
        add_sample(value_tr, weight_tr, &sum, &sum_weight);
        add_sample(value_bl, weight_bl, &sum, &sum_weight);
        add_sample(value_br, weight_br, &sum, &sum_weight);

        let ao = sum / sum_weight * select(1.0, OCCLUSION_TERM_SCALE, final_apply);
        if (all(pix < size)) {
            textureStore(out_ao, pix, vec4f(ao, 0.0, 0.0, 0.0));
        }
    }
}

@compute @workgroup_size(8, 8) fn denoise(@builtin(global_invocation_id) gid: vec3u) {
    denoise_pair(vec2i(gid.xy) * vec2i(2, 1), false);
}

@compute @workgroup_size(8, 8) fn denoise_final(@builtin(global_invocation_id) gid: vec3u) {
    denoise_pair(vec2i(gid.xy) * vec2i(2, 1), true);
}

// Working AO term at a working position in texels, bilinear between the 4 nearest texels.
fn term_bilinear(coord: vec2f) -> f32 {
    let c = coord - 0.5;
    let p = vec2i(floor(c));
    let f = fract(c);
    let top = mix(term_at(p), term_at(p + vec2i(1, 0)), f.x);
    let bottom = mix(term_at(p + vec2i(0, 1)), term_at(p + vec2i(1, 1)), f.x);
    return mix(top, bottom, f.y);
}

// Bilateral upsample of the half resolution AO (BilateralUpsample.esf of Esoterica): bilinear
// where the 2x2 working depths around the pixel match its depth, else along the matching row
// or column, else the working texel of the closest depth.
@fragment fn upsample(f: FullscreenOut) -> @location(0) vec4f {
    let pixel = vec2i(f.pos.xy);
    let raw = textureLoad(depth_texture, pixel, 0);
    if (raw <= 0.0) {
        return vec4f(1.0);
    }

    let full_depth = view_depth(raw);
    // Working texel k holds the depth of render pixel 2k.
    let coord = f.pos.xy * 0.5;
    let base = vec2i(floor(coord - 0.5));
    // GatherRed order: (0, 1), (1, 1), (1, 0), (0, 0)
    let low = vec4f(depth_at(base + vec2i(0, 1), 0), depth_at(base + vec2i(1, 1), 0),
        depth_at(base + vec2i(1, 0), 0), depth_at(base, 0));
    let full_offsets = vec2f(select(-0.25, 0.25, (pixel.x & 1) != 0),
        select(-0.25, 0.25, (pixel.y & 1) != 0));
    let difference = abs(low - full_depth);
    let normalized = difference / max(full_depth, 1e-6);
    let threshold = normalized < vec4f(0.03);
    var offsets = vec2f(0.0);
    if (all(threshold)) {
    } else if (all(threshold.wz)) {
        offsets = vec2f(0.0, -0.5 + full_offsets.y);
    } else if (all(threshold.xy)) {
        offsets = vec2f(0.0, 0.5 + full_offsets.y);
    } else if (all(threshold.wx)) {
        offsets = vec2f(-0.5 + full_offsets.x, 0.0);
    } else if (all(threshold.zy)) {
        offsets = vec2f(0.5 + full_offsets.x, 0.0);
    } else {
        var smallest = 10000.0;
        var closest = vec2f(0.0);
        if (difference.x < smallest) {
            smallest = difference.x;
            closest = vec2f(-0.5, 0.5);
        }
        if (difference.y < smallest) {
            smallest = difference.y;
            closest = vec2f(0.5, 0.5);
        }
        if (difference.z < smallest) {
            smallest = difference.z;
            closest = vec2f(0.5, -0.5);
        }
        if (difference.w < smallest) {
            smallest = difference.w;
            closest = vec2f(-0.5, -0.5);
        }
        offsets = full_offsets + closest;
    }

    return vec4f(term_bilinear(coord + offsets), 0.0, 0.0, 1.0);
}

// TextureLayers of the layers entry points of the mesh shaders (the _layers fragment entry
// points, box_gpu_layered): up to four layers that change the resolved surface before it
// is lit. The mesh shaders include it and their _layers entry points only with
// BOX_TEXTURE_LAYERS, the separate layers modules (BOX_RENDER_SHADER_MESH_LAYERS), so the
// modules of the batches without TextureLayers are unchanged. The includer includes
// mesh_common.wgsl and mesh_surface.wgsl first.
#pragma once

// The values of one TextureLayer, box_render_texture_layer_t: color.a is the amount (0 does
// nothing), direction.w the facing, coverage the cavity, edge width, normal and porosity,
// material the roughness and metallic constants, the world size of one repeat and the
// triplanar blend, transform the scale and offset of the UVs, modes the LAYER_* blend of the
// albedo, roughness, metallic and occlusion, flags.x the LAYER_HAS_* and LAYER_UV flags.
struct TextureLayer {
    color: vec4f,
    direction: vec4f,
    coverage: vec4f,
    material: vec4f,
    transform: vec4f,
    modes: vec4u,
    flags: vec4u,
};

const LAYER_KEEP: u32 = 0u;
const LAYER_MIX: u32 = 1u;
const LAYER_MULTIPLY: u32 = 2u;

const LAYER_HAS_ALBEDO: u32 = 1u;
const LAYER_HAS_ORM: u32 = 2u;
const LAYER_HAS_NORMAL: u32 = 4u;
const LAYER_HAS_MASK: u32 = 8u;
const LAYER_UV: u32 = 16u;

// Slices of each layer in the array of the layer maps: albedo (display encoded, as stored),
// ORM, normal and mask, from the first slice of the layers (after the heights of a blend).
const LAYER_MAPS: u32 = 4u;

// Group 1 of the plain layers entry points besides the material group: the maps of the layers
// as slices of one array; the blend entry points find them in blend_heights after the heights.
// The uniforms of the four layers, a zero TextureLayer for an unused slot.
@group(1) @binding(13) var layer_maps: texture_2d_array<f32>;
@group(1) @binding(14) var<uniform> texture_layer0: TextureLayer;
@group(1) @binding(15) var<uniform> texture_layer1: TextureLayer;
@group(1) @binding(16) var<uniform> texture_layer2: TextureLayer;
@group(1) @binding(17) var<uniform> texture_layer3: TextureLayer;

fn texture_layer(k: u32) -> TextureLayer {
    switch k {
        case 0u: {
            return texture_layer0;
        }
        case 1u: {
            return texture_layer1;
        }
        case 2u: {
            return texture_layer2;
        }
        default: {
            return texture_layer3;
        }
    }
}

// The properties of the resolved surface that the layers change, in linear light. normal is
// the shading normal, geometric the vertex normal (bent by a WindSway), low how low the surface
// lies in the heights of a TextureBlend (blend_low), 0 without.
struct LayerSurface {
    albedo: vec3f,
    metallic: f32,
    roughness: f32,
    occlusion: f32,
    normal: vec3f,
    geometric: vec3f,
    low: f32,
};

// How low a fragment of a TextureBlend lies for the cavity of the layers: 1 - the height of its
// layers with a height image, weighted as they blend; 0 without heights (a contrast of 0) and on
// the tiles of a TileTerrain.
fn blend_low(b: BlendFragment) -> f32 {
    if (b.tiled != 0u || !(blending.contrast > 0.0)) {
        return 0.0;
    }
    var h = 0.0;
    var w = 0.0;
    for (var k = 0u; k < 4u; k++) {
        if (b.w[k] > 0.0 && (blending.flags & (BLEND_HEIGHT << k)) != 0u) {
            let t = blending.transforms[k];
            h += b.w[k] * textureSampleGrad(blend_heights, material_sampler, b.uv * t.xy + t.zw,
                k, b.gx * t.xy, b.gy * t.xy).r;
            w += b.w[k];
        }
    }
    return select(0.0, clamp(1.0 - h / w, 0.0, 1.0), w > 0.0);
}

// What the maps of one layer give a fragment: the albedo times its constant in linear light,
// the ORM, the world normal and the mask.
struct LayerSample {
    albedo: vec3f,
    orm: vec3f,
    normal: vec3f,
    mask: f32,
};

// Direction of v perpendicular to the normal n, of unit length; zero along n.
fn layer_along(v: vec3f, n: vec3f) -> vec3f {
    let p = v - n * dot(n, v);
    return p * inverseSqrt(max(dot(p, p), 1e-12));
}

fn layer_tap(maps: texture_2d_array<f32>, slice: u32, uv: vec2f, gx: vec2f, gy: vec2f) -> vec4f {
    return textureSampleGrad(maps, material_sampler, uv, slice, gx, gy);
}

// The maps of a layer at one set of UVs and gradients, weighted by w; the normal map is turned
// into the world by tangent and bitangent around n.
fn layer_taps(maps: texture_2d_array<f32>, first: u32, flags: u32, uv: vec2f, gx: vec2f,
    gy: vec2f, tangent: vec3f, bitangent: vec3f, n: vec3f, w: f32, o: ptr<function, LayerSample>)
{
    if ((flags & LAYER_HAS_ALBEDO) != 0u) {
        (*o).albedo += w * material_linear(layer_tap(maps, first, uv, gx, gy).rgb);
    } else {
        (*o).albedo += vec3f(w);
    }
    if ((flags & LAYER_HAS_ORM) != 0u) {
        (*o).orm += w * layer_tap(maps, first + 1u, uv, gx, gy).rgb;
    } else {
        (*o).orm += vec3f(w);
    }
    if ((flags & LAYER_HAS_NORMAL) != 0u) {
        let m = layer_tap(maps, first + 2u, uv, gx, gy).xyz * 2.0 - 1.0;
        (*o).normal += w * (tangent * m.x + bitangent * m.y + n * m.z);
    } else {
        (*o).normal += w * n;
    }
    if ((flags & LAYER_HAS_MASK) != 0u) {
        (*o).mask += w * layer_tap(maps, first + 3u, uv, gx, gy).r;
    } else {
        (*o).mask += w;
    }
}

// Sample the maps of a layer with its mapping: the UVs of the mesh with LAYER_UV, else the world
// position projected on the plane of the dominant axis of the geometric normal (the faces of a
// Box), the three projections blended above a blend of 0. dx and dy are the screen derivatives
// of the world position, uv and the gradients those of the mesh UVs.
fn layer_sample(maps: texture_2d_array<f32>, first: u32, l: TextureLayer, f: VertexOut,
    s: LayerSurface, dx: vec3f, dy: vec3f, ux: vec2f, uy: vec2f) -> LayerSample {
    var o: LayerSample;
    o.albedo = vec3f(0.0);
    o.orm = vec3f(0.0);
    o.normal = vec3f(0.0);
    o.mask = 0.0;
    let t = l.transform;
    let flags = l.flags.x;
    let n = s.geometric;
    if ((flags & LAYER_UV) != 0u) {
        let frame = cotangent_frame_grad(n, dx, dy, ux * t.xy, uy * t.xy);
        let tangent = select(frame.tangent, layer_along(vec3f(1.0, 0.0, 0.0), n), frame.degenerate);
        let bitangent = select(frame.bitangent, cross(n, tangent), frame.degenerate);
        layer_taps(maps, first, flags, f.uv.xy * t.xy + t.zw, ux * t.xy, uy * t.xy, tangent,
            bitangent, n, 1.0, &o);
    } else {
        let size = max(l.material.z, 1e-6);
        let p = f.world / size;
        let px = dx / size;
        let py = dy / size;
        let sg = mapping_signs(n);
        let axis = mapping_axis(n);
        let one = vec3f(select(0.0, 1.0, axis == 0u), select(0.0, 1.0, axis == 1u),
            select(0.0, 1.0, axis == 2u));
        let w = select(one, mapping_weights(n, l.material.w), l.material.w > 0.0);
        let planes = mapping_planes(vec4f(p, p.x), sg);
        let gx = mapping_planes(vec4f(px, px.x), sg);
        let gy = mapping_planes(vec4f(py, py.x), sg);
        for (var k = 0u; k < 3u; k++) {
            if (w[k] == 0.0) {
                continue;
            }
            let tangent = layer_along(mapping_tangent(k, sg), n);
            let bitangent = layer_along(mapping_bitangent(k), n);
            layer_taps(maps, first, flags, planes[k] * t.xy + t.zw, gx[k] * t.xy, gy[k] * t.xy,
                tangent, bitangent, n, w[k], &o);
        }
    }
    o.albedo *= l.color.rgb;
    o.normal = select(n, normalize(o.normal), dot(o.normal, o.normal) > 1e-12);
    return o;
}

// How much of a fragment a layer covers. The score is how well the normal faces the direction
// of the layer (allowed above a cosine of 2 facing - 1, ignored at a facing of 0), times the
// mask raised by the cavity times how occluded or low the surface is (lowered for a negative
// cavity),
// so crevices come first without holding back open surfaces. The normal is the shading normal
// mixed toward the geometric one by the normal of the layer: a layer that flattens the surface
// (a water film) fills its bumps, so they do not decide where it lies. The amount moves a
// threshold over the score from 1 (nothing) to below 0 (everything allowed), the edge width
// soft. A score of 0 (not allowed by the rules) stays uncovered at any amount, faded over the
// edge width.
fn layer_coverage(l: TextureLayer, s: LayerSurface, mask: f32) -> f32 {
    let facing = l.direction.w;
    let n = mix(s.normal, s.geometric, l.coverage.z);
    let d = dot(n, l.direction.xyz) * inverseSqrt(max(dot(n, n), 1e-12));
    let limit = 2.0 * facing - 1.0;
    let toward = clamp((d - limit) / max(1.0 - limit, 1e-3), 0.0, 1.0);
    let allowed = select(1.0, clamp(toward * 16.0, 0.0, 1.0), facing > 0.0);
    let orientation = mix(1.0, toward, facing) * allowed;
    let cavity = l.coverage.x * max(1.0 - clamp(s.occlusion, 0.0, 1.0), s.low);
    let score = orientation * clamp(clamp(mask, 0.0, 1.0) + cavity, 0.0, 1.0);
    let edge = max(l.coverage.y, 1e-4);
    let threshold = (1.0 - l.color.a) * (1.0 + edge) - edge;
    return smoothstep(threshold, threshold + edge, score) * clamp(score / edge, 0.0, 1.0) *
        select(0.0, 1.0, l.color.a > 0.0);
}

fn layer_blend(mode: u32, v: f32, layer: f32, k: f32) -> f32 {
    switch mode {
        case 1u: {
            return mix(v, layer, k);
        }
        case 2u: {
            return v * mix(1.0, layer, k);
        }
        default: {
            return v;
        }
    }
}

fn layer_blend3(mode: u32, v: vec3f, layer: vec3f, k: f32) -> vec3f {
    switch mode {
        case 1u: {
            return mix(v, layer, k);
        }
        case 2u: {
            return v * mix(vec3f(1.0), layer, k);
        }
        default: {
            return v;
        }
    }
}

// Apply the layers in order over a front facing surface. maps holds the maps of the layers from
// slice first (layer_maps from 0, or blend_heights after the heights of a blend). The porosity
// of the surface beneath, (1 - metallic) times its roughness, scales the change of the albedo
// and the roughness by porosity; metallic, occlusion and normal change by the coverage. The
// derivatives are taken once, before the layers branch.
@diagnostic(off, derivative_uniformity)
fn apply_layers(maps: texture_2d_array<f32>, first: u32, f: VertexOut, surface: LayerSurface)
    -> LayerSurface {
    var s = surface;
    let dx = dpdx(f.world);
    let dy = dpdy(f.world);
    let ux = dpdx(f.uv.xy);
    let uy = dpdy(f.uv.xy);
    for (var k = 0u; k < 4u; k++) {
        let l = texture_layer(k);
        if (l.color.a <= 0.0) {
            continue;
        }
        let m = layer_sample(maps, first + k * LAYER_MAPS, l, f, s, dx, dy, ux, uy);
        let covered = layer_coverage(l, s, m.mask);
        if (covered <= 0.0) {
            continue;
        }
        let porous = (1.0 - s.metallic) * s.roughness;
        let soaked = covered * mix(1.0, porous, l.coverage.w);
        s.albedo = max(layer_blend3(l.modes.x, s.albedo, m.albedo, soaked), vec3f(0.0));
        s.roughness = clamp(layer_blend(l.modes.y, s.roughness, l.material.x * m.orm.g,
            soaked), MIN_ROUGHNESS, 1.0);
        s.metallic = clamp(layer_blend(l.modes.z, s.metallic, l.material.y * m.orm.b, covered),
            0.0, 1.0);
        s.occlusion = clamp(layer_blend(l.modes.w, s.occlusion, m.orm.r, covered), 0.0, 1.0);
        let bent = mix(s.normal, m.normal, covered * l.coverage.z);
        s.normal = select(s.normal, normalize(bent), dot(bent, bent) > 1e-12);
    }
    return s;
}

// Forward pass and depth prepass of the tiles of a Terrain (box.terrain), which always draw with
// this shader (BOX_RENDER_SHADER_TERRAIN) with the Terrain shader features of their scene
// (SceneShaderFeatures): texture_blend, triplanar and texture_layers. A tile reads the values of
// its surface from the surface stream (TerrainTileParams), its heights from the surface assets
// of the grid mesh, and passes on only what its fragments read (TerrainOut). The fragments are
// lit as the mesh shader lights them: a GGX specular and a Lambert diffuse lobe.
//
// fs_terrain shades the layers of the TextureBlend of the terrain weighed by the masks of the
// tile (BLEND_TERRAIN), fs_terrain_plain the PbrTextures of a terrain without one. With
// BOX_TEXTURE_LAYERS (BOX_RENDER_SHADER_TERRAIN_LAYERS) both apply the TextureLayers of the
// terrain. vs_terrain_bake and fs_terrain_bake bake the material of a tile with masks into its
// cell of the bake atlas of the grid mesh (box_gpu_terrain_bake).
#include "include/mesh_common.wgsl"
#include "include/mesh_indirect.wgsl"
#include "include/mesh_direct.wgsl"
#include "include/mesh_surface.wgsl"
#include "include/mesh_blend.wgsl"
#include "include/terrain_bake.wgsl"
#ifdef BOX_TEXTURE_LAYERS
#include "include/mesh_layers.wgsl"
#endif

// Terrain.triplanar of the scene: a TextureMapping blend above 0 blends the projections of the
// three axes, else the hard projection.
override material_triplanar: bool = true;

// The bake atlas of the grid mesh of a terrain blend (gpu_terrain.c), blending.bake describing
// it: layer 0 the square root of the albedo and the occlusion (0 for a cell not baked), layer 1
// x and z of the normal as n * 0.5 + 0.5, the roughness and the metallic, layer 2 the light of
// the sun the heights let through and their ambient occlusion. The cell of slot s is s %
// columns across and s / columns down, the bake of the tile at its left and the bake of the next
// coarser level, half the intervals, right of it.
@group(1) @binding(18) var terrain_bake: texture_2d_array<f32>;

// What a vertex of a tile passes on. uv.xy the UVs of the material, the TextureTransform of the
// mesh UVs or the offset of a projected mapping (mapping_offset), uv.zw the grid uv of the
// morphed vertex, where the fragments read the masks and the bake. object what a projected
// mapping projects (mapping_world). color the color of the material and its alpha; tile the
// slot of the tile, the focus relative to its center and the morph distances in tile sizes
// (pack2x16float); material its metallic and roughness, 1 plus its mapping blend (0 without a
// mapping) and its emission (pack2x16float); extra the planet light slot in its second byte and
// the dither code of the DrawRange in its high half.
struct TerrainOut {
    @builtin(position) @invariant pos: vec4f,
    @location(0) world: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec4f,
    @location(3) object: vec4f,
    @location(4) @interpolate(flat) color: vec4f,
    @location(5) @interpolate(flat) tile: vec4u,
    @location(6) @interpolate(flat) material: vec4u,
    @location(7) @interpolate(flat) extra: u32,
};

// The TerrainTileParams of the instance of a slot, from its surface.
fn terrain_tile(slot: u32) -> TerrainTileParams {
    return TerrainTileParams(surfaces[slot].p[1], surfaces[slot].p[2], surfaces[slot].p[3],
        surfaces[slot].p[4], surfaces[slot].p[5]);
}

// Clip position of a vertex of a tile, as vs_terrain and vs_terrain_depth place it.
fn terrain_clip(slot: u32, local: vec3f) -> vec4f {
    let world = instance_model(transforms[slot]) * vec4f(local, 1.0);
    return view.view_proj * world;
}

fn terrain_vertex_out(v: VertexIn) -> TerrainOut {
    let slot = slot_of(v.slot);
    let t = terrain_tile(slot);
    let placed = terrain_vertex_of(t, v.position);
    let model = instance_model(transforms[slot]);
    let world = model * vec4f(placed.position, 1.0);
    let p = Placed(model, placed.normal, world, view.view_proj * world, placed.position,
        vec3f(0.0), vec4f(0.0, 0.0, 0.0, 1.0));
    let frame = model_frame(p);
    let shading = shadings[slot];
    let material = material_fetch(shading_material(shading));
    let tint = select(mesh_shading_tint(shading), vec4f(0.0, 0.0, 0.0, 1.0),
        view.reflection.y != 0u);
    let size = max(t.shape.x, 1e-6);
    var o: TerrainOut;
    o.pos = terrain_clip(slot, placed.position);
    o.world = world.xyz;
    o.normal = frame.normal;
    o.color = vec4f(material_color(material, tint), shading.alpha);
    o.object = vec4f(0.0, 0.0, 0.0, 1.0);
    o.uv = vec4f(v.uv * material.uv.xy + material.uv.zw, terrain_grid / (t.grid.w - 1.0));
    var mapped = 0.0;
    if (material_mapped(material)) {
        mapped = 1.0 + material_blend(material);
        o.uv = vec4f(mapping_offset(material), o.uv.zw);
        if (material_world(material) || material_frame(material)) {
            o.object = mapping_world(material, world.xyz, world_origin(view.origin));
        } else {
            o.object = mapping_object(material, v.position, frame.scale);
        }
    }
    o.tile = vec4u(u32(t.grid.x), pack2x16float(t.morph.xy / size),
        pack2x16float(t.morph.zw / size), 0u);
    o.material = vec4u(pack2x16float(vec2f(material.metallic, material.roughness)),
        pack2x16float(vec2f(mapped, 0.0)), pack2x16float(material.emissive.xy),
        pack2x16float(vec2f(material.emissive.z, 0.0)));
    o.extra = vertex_range(v.slot) << 16u;
    if (scene_planet_light) {
        o.extra |= shading_planet(shading) << 8u;
    }
    return o;
}

@vertex fn vs_terrain(v: VertexIn) -> TerrainOut {
    return terrain_vertex_out(v);
}

@vertex fn vs_terrain_depth(v: VertexIn) -> DepthOut {
    let slot = slot_of(v.slot);
    return DepthOut(terrain_clip(slot, terrain_position_of(terrain_tile(slot), v.position)),
        vertex_range(v.slot));
}

fn tf_metallic_roughness(f: TerrainOut) -> vec2f {
    return unpack2x16float(f.material.x);
}

fn tf_mapped(f: TerrainOut) -> f32 {
    return unpack2x16float(f.material.y).x;
}

fn tf_emissive(f: TerrainOut) -> vec3f {
    return vec3f(unpack2x16float(f.material.z), unpack2x16float(f.material.w).x);
}

// Morph factor of a fragment at a grid uv, as the vertex stage morphs the grid.
fn terrain_fragment_morph(f: TerrainOut, uv: vec2f) -> f32 {
    let focus = unpack2x16float(f.tile.y);
    let morph = unpack2x16float(f.tile.z);
    let d = length(uv - vec2f(0.5) - focus);
    return clamp((d - morph.x) / max(morph.y - morph.x, 1e-4), 0.0, 1.0);
}

// The masks of a fragment of a terrain tile with masks: those of its tile at its grid uv, faded
// into the masks of the next coarser level, at every second texel of the window, by the morph
// factor of its distance to the focus, as the vertex stage morphs the grid (terrain_vertex_of),
// so at the edge of its range the tile has the masks of the coarser tiles it meets. The cell of
// slot s in the mask atlas is s % columns across and s / columns down, twice as wide as the
// masks: the masks of the tile at its left, the coarse ones at the right. The border texels of
// the cells keep the bilinear filter of the sampler within a cell; the coarse masks are only read
// where the tile morphs.
fn terrain_mask(f: TerrainOut) -> vec4f {
    if ((blending.flags & BLEND_MASKED) == 0u) {
        return vec4f(1.0, 0.0, 0.0, 0.0);
    }
    let texels = f32(blending.terrain & 0xFFFFu);
    let columns = max(blending.terrain >> 16u, 1u);
    let slot = f.tile.x;
    let origin = vec2f(f32(slot % columns) * 2.0 * texels, f32(slot / columns) * texels);
    let dims = vec2f(textureDimensions(blend_mask));
    let steps = texels - 1.0 - 2.0 * TERRAIN_BORDER;
    let uv = f.uv.zw;
    let t = uv * steps + vec2f(TERRAIN_BORDER);
    var mask = textureSampleLevel(blend_mask, material_sampler, (origin + t + vec2f(0.5)) / dims,
        0.0);
    let k = terrain_fragment_morph(f, uv);
    if (k > 0.0) {
        let coarse = textureSampleLevel(blend_mask, material_sampler,
            (origin + vec2f(texels, 0.0) + t * 0.5 + vec2f(0.5)) / dims, 0.0);
        mask = mix(mask, coarse, k);
    }
    return mask;
}

// Bake texels per pixel below which a terrain fragment shades its material in full, and above
// which it takes its bake alone; it fades between them.
const TERRAIN_BAKE_NEAR: f32 = 0.7;
const TERRAIN_BAKE_FAR: f32 = 1.4;

// Up component of the normal below which a terrain fragment takes none of the light of its bake,
// which the bake spreads top down over a steep face, and above which it takes all of it.
const TERRAIN_BAKE_STEEP: f32 = 0.35;
const TERRAIN_BAKE_FLAT: f32 = 0.6;

// The material of a terrain fragment from its bake (terrain_bake_material).
struct TerrainBaked {
    albedo: vec3f,
    occlusion: f32,
    normal: vec3f,
    roughness: f32,
    metallic: f32,
};

// Where a fragment of a terrain tile reads its bake: the uv of its texel in the cell of its tile,
// in the cell of the next coarser level, and the fade between them as terrain_mask fades the
// masks.
struct TerrainBakeAt {
    fine: vec2f,
    coarse: vec2f,
    morph: f32,
};

fn terrain_bake_at(f: TerrainOut) -> TerrainBakeAt {
    let uv = clamp(f.uv.zw, vec2f(0.0), vec2f(1.0));
    let texels = f32(blending.bake & 0xFFFFu);
    let columns = max(blending.bake >> 16u, 1u);
    let coarse = floor((texels + 1.0) * 0.5);
    let slot = f.tile.x;
    let origin = vec2f(f32(slot % columns) * (texels + coarse), f32(slot / columns) * texels);
    let dims = vec2f(textureDimensions(terrain_bake));
    return TerrainBakeAt((origin + vec2f(0.5) + uv * (texels - 1.0)) / dims,
        (origin + vec2f(texels + 0.5, 0.5) + uv * (coarse - 1.0)) / dims,
        terrain_fragment_morph(f, uv));
}

// A layer of the bake of a terrain fragment.
fn terrain_bake_layer(at: TerrainBakeAt, layer: u32) -> vec4f {
    var v = textureSampleLevel(terrain_bake, material_sampler, at.fine, layer, 0.0);
    if (at.morph > 0.0) {
        let c = textureSampleLevel(terrain_bake, material_sampler, at.coarse, layer, 0.0);
        v = select(mix(v, c, at.morph), vec4f(0.0), layer == 2u && c.a == 0.0);
    }
    return v;
}

// Whether the fragments of the batch have a bake atlas.
fn terrain_baking() -> bool {
    return (blending.flags & (BLEND_TERRAIN | BLEND_MASKED)) == (BLEND_TERRAIN | BLEND_MASKED) &&
        blending.bake != 0u;
}

// How far the bake of a fragment of a terrain tile stands in for its material, by the texels of
// the bake a pixel covers: from the coarse derivatives of its grid uv, so a quad takes one side,
// and their determinant, so a steep face whose pixels cover few texels shades in full. 0 without
// a bake atlas.
@diagnostic(off, derivative_uniformity)
fn terrain_bake_far(f: TerrainOut) -> f32 {
    let texels = f32(blending.bake & 0xFFFFu);
    let jx = dpdxCoarse(f.uv.zw);
    let jy = dpdyCoarse(f.uv.zw);
    let reach = sqrt(abs(jx.x * jy.y - jx.y * jy.x)) * max(texels - 1.0, 1.0);
    return select(0.0, smoothstep(TERRAIN_BAKE_NEAR, TERRAIN_BAKE_FAR, reach), terrain_baking());
}

// The light of the bake of a fragment of a terrain tile: x the light of the sun its heights let
// through, y their ambient occlusion and w 1 where the bake exists; (1, 1, 0, 0) without one.
fn terrain_bake_light(f: TerrainOut) -> vec4f {
    if (!terrain_baking()) {
        return vec4f(1.0, 1.0, 0.0, 0.0);
    }
    let c = terrain_bake_layer(terrain_bake_at(f), 2u);
    return select(vec4f(c.r, c.g, 0.0, 1.0), vec4f(1.0, 1.0, 0.0, 0.0), c.a == 0.0);
}

// The material of the bake of a fragment of a terrain tile with one (terrain_bake_light).
fn terrain_bake_material(f: TerrainOut) -> TerrainBaked {
    let at = terrain_bake_at(f);
    let a = terrain_bake_layer(at, 0u);
    let b = terrain_bake_layer(at, 1u);
    let nxz = b.rg * 2.0 - 1.0;
    return TerrainBaked(a.rgb * a.rgb, a.a,
        normalize(vec3f(nxz.x, sqrt(max(1.0 - dot(nxz, nxz), 0.0)), nxz.y)), b.b, b.a);
}

// Direction of v perpendicular to the normal n, of unit length; zero along n.
fn tangent_along(v: vec3f, n: vec3f) -> vec3f {
    let p = v - n * dot(n, v);
    return p * inverseSqrt(max(dot(p, p), 1e-12));
}

// The two layers of the TextureBlend a fragment shades, the two that weigh the most, and their
// weights, normalized; b has a weight of 0 where one layer is alone.
struct TerrainPair {
    a: u32,
    b: u32,
    w: vec2f,
};

fn terrain_pair(w: vec4f) -> TerrainPair {
    var a = 0u;
    for (var k = 1u; k < 4u; k++) {
        if (w[k] > w[a]) {
            a = k;
        }
    }
    var b = select(0u, 1u, a == 0u);
    for (var k = 0u; k < 4u; k++) {
        if (k != a && w[k] > w[b]) {
            b = k;
        }
    }
    let sum = max(w[a] + w[b], 1e-6);
    return TerrainPair(a, b, vec2f(w[a], w[b]) / sum);
}

// The weights of a pair of layers shaped by their heights at uv, as blend_height_weights shapes
// the weights of all four: each weight w times exp(contrast * (h + w - m)), m the largest h + w,
// normalized, and a layer under BLEND_HEIGHT_DROP of the sum dropped.
fn terrain_pair_heights(p: TerrainPair, uv: vec2f, gx: vec2f, gy: vec2f) -> vec2f {
    if (!(blending.contrast > 0.0) || p.w.y <= 0.0) {
        return p.w;
    }
    var h = vec2f(0.5);
    let ta = blending.transforms[p.a];
    let tb = blending.transforms[p.b];
    if ((blending.flags & (BLEND_HEIGHT << p.a)) != 0u) {
        h.x = textureSampleGrad(blend_heights, material_sampler, uv * ta.xy + ta.zw, p.a,
            gx * ta.xy, gy * ta.xy).r;
    }
    if ((blending.flags & (BLEND_HEIGHT << p.b)) != 0u) {
        h.y = textureSampleGrad(blend_heights, material_sampler, uv * tb.xy + tb.zw, p.b,
            gx * tb.xy, gy * tb.xy).r;
    }
    let s = h + p.w;
    let m = max(s.x, s.y);
    var o = p.w * exp(blending.contrast * (s - vec2f(m)));
    let sum = o.x + o.y;
    if (!(sum > 1e-12)) {
        return p.w;
    }
    o = select(vec2f(0.0), o, o > vec2f(sum * BLEND_HEIGHT_DROP));
    return o / (o.x + o.y);
}

// How low a fragment lies in the heights of a pair of layers for the cavity of the
// TextureLayers: 1 - their height, weighted as they blend; 0 without heights.
fn terrain_pair_low(p: TerrainPair, w: vec2f, uv: vec2f, gx: vec2f, gy: vec2f) -> f32 {
    if (!(blending.contrast > 0.0)) {
        return 0.0;
    }
    var h = 0.0;
    var sum = 0.0;
    for (var i = 0u; i < 2u; i++) {
        let k = select(p.a, p.b, i == 1u);
        if (w[i] > 0.0 && (blending.flags & (BLEND_HEIGHT << k)) != 0u) {
            let t = blending.transforms[k];
            h += w[i] * textureSampleGrad(blend_heights, material_sampler, uv * t.xy + t.zw, k,
                gx * t.xy, gy * t.xy).r;
            sum += w[i];
        }
    }
    return select(0.0, clamp(1.0 - h / sum, 0.0, 1.0), sum > 0.0);
}

// The weighted albedo, ORM and tangent space normal of a pair of layers at uv.
struct BlendSample {
    albedo: vec4f,
    orm: vec3f,
    normal: vec3f,
};

fn terrain_pair_sample(p: TerrainPair, w: vec2f, uv: vec2f, gx: vec2f, gy: vec2f) -> BlendSample {
    var o: BlendSample;
    o.albedo = vec4f(0.0);
    o.orm = vec3f(0.0);
    var m = vec3f(0.0);
    for (var i = 0u; i < 2u; i++) {
        if (w[i] <= 0.0) {
            continue;
        }
        let k = select(p.a, p.b, i == 1u);
        let t = blending.transforms[k];
        let at = uv * t.xy + t.zw;
        let lx = gx * t.xy;
        let ly = gy * t.xy;
        o.albedo += w[i] * textureSampleGrad(blend_albedo, material_sampler, at, k, lx, ly);
        o.orm += w[i] * textureSampleGrad(blend_orm, material_sampler, at, k, lx, ly).rgb;
        m += w[i] * (textureSampleGrad(blend_normal, material_sampler, at, k, lx, ly).xyz * 2.0 -
            1.0);
    }
    o.normal = select(vec3f(0.0, 0.0, 1.0), normalize(m), dot(m, m) > 1e-12);
    return o;
}

// The weights of the projections of a triplanar mapping a fragment samples: the dominant axis
// and the next one (biplanar), renormalized.
fn terrain_biplanar(w: vec3f, dominant: u32) -> vec3f {
    var second = select(0u, 1u, dominant == 0u);
    for (var k = 0u; k < 3u; k++) {
        if (k != dominant && w[k] > w[second]) {
            second = k;
        }
    }
    var o = vec3f(0.0);
    o[dominant] = w[dominant];
    o[second] = w[second];
    return o / max(o.x + o.y + o.z, 1e-6);
}

// The layers of a fragment of a TextureBlend and how low it lies in their heights.
struct TerrainLayers {
    layers: Layers,
    low: f32,
};

// The layers of a fragment of the TextureBlend of a terrain, weighed by its masks: the two layers
// that weigh the most, shaped by their heights on the dominant projection. With a triplanar
// mapping the dominant projection and the next one blend as the triplanar mapping of one set of
// textures does; else one projection, the mapping or the UVs.
@diagnostic(off, derivative_uniformity)
fn terrain_blend_layers(f: TerrainOut, geometric: vec3f) -> TerrainLayers {
    let mapped = tf_mapped(f);
    let pair = terrain_pair(blend_weights(terrain_mask(f), blending));
    var o: TerrainLayers;
    o.low = 0.0;
    o.layers.albedo = vec4f(0.0);
    o.layers.emissive = vec3f(1.0);
    o.layers.orm = vec3f(0.0);
    if (material_triplanar && mapped > 1.0) {
        let dx = dpdx(f.object);
        let dy = dpdy(f.object);
        let n = f.normal;
        let s = mapping_signs(n);
        let dominant = mapping_axis(n);
        let w = terrain_biplanar(mapping_weights(n, mapped - 1.0), dominant);
        var planes = mapping_planes(f.object, s);
        var gx = mapping_planes(dx, s);
        var gy = mapping_planes(dy, s);
        let lw = terrain_pair_heights(pair, planes[dominant] + f.uv.xy, gx[dominant],
            gy[dominant]);
        var normal = vec3f(0.0);
        for (var k = 0u; k < 3u; k++) {
            if (w[k] == 0.0) {
                continue;
            }
            let tangent = tangent_along(mapping_tangent(k, s), geometric);
            let bitangent = tangent_along(mapping_bitangent(k), geometric);
            if (k == dominant) {
                o.layers.frame = Frame(tangent, bitangent, false);
            }
            let b = terrain_pair_sample(pair, lw, planes[k] + f.uv.xy, gx[k], gy[k]);
            o.layers.albedo += w[k] * b.albedo;
            o.layers.orm += w[k] * b.orm;
            let m = b.normal;
            normal += w[k] * (tangent * m.x + bitangent * m.y + geometric * m.z);
        }
        o.layers.normal = select(geometric, normalize(normal), dot(normal, normal) > 1e-12);
        return o;
    }

    var uv = f.uv.xy;
    if (mapped != 0.0) {
        uv = mapping_uv(f.object, f.normal, f.uv.xy);
    }
    let gx = dpdx(uv);
    let gy = dpdy(uv);
    let lw = terrain_pair_heights(pair, uv, gx, gy);
    let b = terrain_pair_sample(pair, lw, uv, gx, gy);
    o.layers.albedo = b.albedo;
    o.layers.orm = b.orm;
    o.layers.frame = cotangent_frame_grad(geometric, dpdx(f.world), dpdy(f.world), gx, gy);
    o.layers.normal = perturb(geometric, o.layers.frame, b.normal);
    o.low = terrain_pair_low(pair, lw, uv, gx, gy);
    return o;
}

// The layers of a fragment of a terrain without a TextureBlend: its PbrTextures with a triplanar
// mapping, one projection or the UVs.
@diagnostic(off, derivative_uniformity)
fn terrain_plain_layers(f: TerrainOut, geometric: vec3f) -> Layers {
    let mapped = tf_mapped(f);
    var o: Layers;
    if (material_triplanar && mapped > 1.0) {
        let dx = dpdx(f.object);
        let dy = dpdy(f.object);
        let n = f.normal;
        let w = mapping_weights(n, mapped - 1.0);
        let s = mapping_signs(n);
        let dominant = mapping_axis(n);
        var planes = mapping_planes(f.object, s);
        var gx = mapping_planes(dx, s);
        var gy = mapping_planes(dy, s);
        o.albedo = vec4f(0.0);
        o.emissive = vec3f(0.0);
        o.orm = vec3f(0.0);
        var normal = vec3f(0.0);
        for (var k = 0u; k < 3u; k++) {
            if (w[k] == 0.0) {
                continue;
            }
            let uv = planes[k] + f.uv.xy;
            o.albedo += w[k] * textureSampleGrad(albedo_map, material_sampler, uv, gx[k], gy[k]);
            o.emissive += w[k] * textureSampleGrad(emissive_map, material_sampler, uv, gx[k],
                gy[k]).rgb;
            o.orm += w[k] * textureSampleGrad(orm_map, material_sampler, uv, gx[k], gy[k]).rgb;
            let m = textureSampleGrad(normal_map, material_sampler, uv, gx[k], gy[k]).xyz *
                2.0 - 1.0;
            let tangent = tangent_along(mapping_tangent(k, s), geometric);
            let bitangent = tangent_along(mapping_bitangent(k), geometric);
            normal += w[k] * (tangent * m.x + bitangent * m.y + geometric * m.z);
            if (k == dominant) {
                o.frame = Frame(tangent, bitangent, false);
            }
        }
        o.normal = select(geometric, normalize(normal), dot(normal, normal) > 1e-12);
        return o;
    }

    var uv = f.uv.xy;
    if (mapped != 0.0) {
        uv = mapping_uv(f.object, f.normal, f.uv.xy);
    }
    o.albedo = textureSample(albedo_map, material_sampler, uv);
    o.emissive = textureSample(emissive_map, material_sampler, uv).rgb;
    o.orm = textureSample(orm_map, material_sampler, uv).rgb;
    let m = textureSample(normal_map, material_sampler, uv).xyz * 2.0 - 1.0;
    o.frame = cotangent_frame(geometric, f.world, uv);
    o.normal = perturb(geometric, o.frame, m);
    return o;
}

// The surface of a fragment of a tile: occlusion scales the indirect light only, geometric is
// the vertex normal before the normal map, which the shadow lookup is offset along, and shade
// the share of the first directional light the heights of the terrain hold back.
struct Surface {
    albedo: vec3f,
    alpha: f32,
    metallic: f32,
    roughness: f32,
    occlusion: f32,
    emissive: vec3f,
    normal: vec3f,
    geometric: vec3f,
    shade: f32,
};

fn surface_of(f: TerrainOut, geometric: vec3f, layers: Layers) -> Surface {
    let mr = tf_metallic_roughness(f);
    var s: Surface;
    s.albedo = f.color.rgb * layers.albedo.rgb;
    s.alpha = f.color.a * layers.albedo.a;
    s.metallic = clamp(mr.x * layers.orm.b, 0.0, 1.0);
    s.roughness = clamp(mr.y * layers.orm.g, MIN_ROUGHNESS, 1.0);
    s.occlusion = layers.orm.r;
    s.emissive = tf_emissive(f) * layers.emissive;
    s.normal = layers.normal;
    s.geometric = geometric;
    s.shade = 0.0;
    return s;
}

#ifdef BOX_TEXTURE_LAYERS
// The surface after the TextureLayers of the terrain, whose maps are slices of maps from first.
fn layered(f: TerrainOut, s: Surface, maps: texture_2d_array<f32>, first: u32, low: f32)
    -> Surface {
    var o = s;
    let l = apply_layers_at(maps, first, f.world, f.uv.xy, LayerSurface(s.albedo, s.metallic,
        s.roughness, s.occlusion, s.normal, s.geometric, low));
    o.albedo = l.albedo;
    o.metallic = l.metallic;
    o.roughness = l.roughness;
    o.occlusion = l.occlusion;
    o.normal = l.normal;
    return o;
}
#endif

// Light reflected toward the eye by one light arriving from direction l with a radiance, faded by
// the shadow terminator of the shading normal (shadow_terminator).
fn light_lobes(s: Surface, v: vec3f, l: vec3f, radiance: vec3f, side: u32) -> Lit {
    let n = s.normal;
    let a = s.roughness * s.roughness;
    let f0 = mix(vec3f(0.04), s.albedo, s.metallic);
    let diffuse_color = s.albedo * (1.0 - s.metallic);
    let ndv = max(dot(n, v), 1e-4);
    let h = normalize(l + v);
    let ndl = max(dot(n, l), 0.0);
    let ndh = max(dot(n, h), 0.0);
    let ldh = max(dot(l, h), 0.0);
    let f = fresnel_schlick(ldh, f0);
    let specular = distribution_ggx(ndh, a) * visibility_smith(ndv, ndl, a) * f;
    let diffuse = (vec3f(1.0) - f) * diffuse_color / PI;
    let shaded = ndl * shadow_terminator(s.geometric, n, l);
    return Lit(diffuse * radiance * shaded, specular * radiance * shaded);
}

// An unlit view shows the albedo as the diffuse part and the emission as the specular one, a
// view of an impostor frame its channel (impostor_channel).
fn shade(s: Surface, v: vec3f, world: vec3f) -> Lit {
    if (view.lights.z > 1u) {
        return impostor_channel(s.albedo, s.normal, world);
    }

    if (view.lights.z == 0u) {
        return Lit(s.albedo, s.emissive);
    }

    let n = s.normal;
    let a = s.roughness * s.roughness;
    let f0 = mix(vec3f(0.04), s.albedo, s.metallic);
    let diffuse_color = s.albedo * (1.0 - s.metallic);
    let ndv = max(dot(n, v), 1e-4);
    let ab = env_brdf(s.roughness, ndv);
    let spec = mix(reflect(-v, n), n, a);
    let cluster = fragment_cluster(world);
    let e = indirect_light(world, n, spec, s.roughness, cluster);
    let specular_albedo = f0 * ab.x + ab.y;
    let indirect_specular = specular_albedo * e.specular *
        specular_occlusion(ndv, s.occlusion, a);
    let indirect_diffuse = (vec3f(1.0) - specular_albedo) * diffuse_color * e.diffuse *
        s.occlusion;
    let direct = lights_direct(s, v, world, cluster, 1u, 0.0);
    return Lit(direct.diffuse + indirect_diffuse, direct.specular + indirect_specular +
        s.emissive);
}

// The surface of a fragment shaded in full, faded toward the surface of its bake by how far the
// bake stands in for it (terrain_bake_far), and lit by the sun and the ambient occlusion its
// heights let through, less so on steep faces (TERRAIN_BAKE_STEEP). The bake is read after the
// material, so nothing of it stays live while the material is shaded.
fn terrain_baked_mix(near: Surface, f: TerrainOut, geometric: vec3f, far: f32) -> Surface {
    var s = near;
    let light = terrain_bake_light(f);
    if (light.w == 0.0) {
        return s;
    }
    if (far > 0.0) {
        let baked = terrain_bake_material(f);
        s.albedo = mix(s.albedo, baked.albedo, far);
        s.roughness = mix(s.roughness, clamp(baked.roughness, MIN_ROUGHNESS, 1.0), far);
        s.metallic = mix(s.metallic, baked.metallic, far);
        s.occlusion = mix(s.occlusion, baked.occlusion, far);
        s.normal = normalize(mix(s.normal, baked.normal, far));
    }
    let flat = smoothstep(TERRAIN_BAKE_STEEP, TERRAIN_BAKE_FLAT, geometric.y);
    s.occlusion *= mix(1.0, light.y, flat);
    s.shade = (1.0 - light.x) * flat;
    return s;
}

fn terrain_lit(f: TerrainOut, shaded: Surface) -> vec4f {
    if (draw_range && !range_keeps(f.extra >> 16u, f.pos.xy)) {
        discard;
    }
    if (scene_planet_light) {
        fragment_planet = (f.extra >> 8u) & 0xFFu;
    }
    var s = shaded;
    s.occlusion = min(s.occlusion, view_occlusion(f.pos));
    let v = normalize(view.camera.xyz - f.world);
    let reflected = shade(s, v, f.world);
    var color = reflected.diffuse + reflected.specular;
    if (scene_aerial && view.aerial.w > 0.0) {
        color = aerial_perspective(color, 1.0, f.pos, f.world);
    }

    return vec4f(color, 1.0);
}

// A tile with a bake atlas shades from its bake alone where a bake texel is no smaller than a
// pixel, in full and faded into the bake below that, and lit by the sun and the ambient
// occlusion of its heights either way (terrain_baked_mix).
@fragment fn fs_terrain(f: TerrainOut) -> @location(0) vec4f {
    let geometric = normalize(f.normal);
    let far = terrain_bake_far(f);
    let alone = far >= 1.0 && terrain_bake_light(f).w != 0.0;
    var s: Surface;
    if (alone) {
        let baked = terrain_bake_material(f);
        var layers: Layers;
        layers.albedo = vec4f(1.0);
        layers.emissive = vec3f(1.0);
        layers.orm = vec3f(1.0);
        layers.normal = baked.normal;
        s = surface_of(f, geometric, layers);
        s.albedo = baked.albedo;
        s.roughness = clamp(baked.roughness, MIN_ROUGHNESS, 1.0);
        s.metallic = baked.metallic;
        s.occlusion = baked.occlusion;
    } else {
        let b = terrain_blend_layers(f, geometric);
        s = surface_of(f, geometric, b.layers);
#ifdef BOX_TEXTURE_LAYERS
        s = layered(f, s, blend_heights, blending.count, b.low);
#endif
    }
    return terrain_lit(f, terrain_baked_mix(s, f, geometric, select(far, 0.0, alone)));
}

@fragment fn fs_terrain_plain(f: TerrainOut) -> @location(0) vec4f {
    let geometric = normalize(f.normal);
    var s = surface_of(f, geometric, terrain_plain_layers(f, geometric));
#ifdef BOX_TEXTURE_LAYERS
    s = layered(f, s, layer_maps, 0u, 0.0);
#endif
    return terrain_lit(f, s);
}

// What a bake writes into the layers of the bake atlas of a terrain grid mesh.
struct TerrainBakeOut {
    @location(0) albedo: vec4f,
    @location(1) normal: vec4f,
    @location(2) light: vec4f,
};

// The vertex stage of a bake: the tile placed as vs_terrain places it, at the morph of its cell,
// drawn top down into the cell with the texels at the ends of a row on its edges, the vertices on
// its edges moved out by half a texel so those texels are covered.
@vertex fn vs_terrain_bake(v: VertexIn) -> TerrainOut {
    let bake = terrain_bake_params;
    terrain_forced_morph = bake.cell.w;
    terrain_pad = bake.tile.w;
    var o = terrain_vertex_out(v);
    let texels = bake.cell.z;
    let px = vec2f(0.5) + terrain_grid / (bake.grid.w - 1.0) * (texels - 1.0);
    o.pos = vec4f(px.x / texels * 2.0 - 1.0, 1.0 - px.y / texels * 2.0, 0.5, 1.0);
    o.tile.y = pack2x16float(vec2f(0.0));
    o.tile.z = pack2x16float(select(vec2f(60000.0, 65000.0), vec2f(-2.0, -1.0),
        bake.cell.w > 0.5));
    return o;
}

// The fragment stage of a bake: the material of the texel as fs_terrain shades it in full, on
// the normal of the Catmull-Rom heights of the tile, with the light of the sun and the ambient
// occlusion the heights of the terrain let through.
@fragment fn fs_terrain_bake(f: TerrainOut) -> TerrainBakeOut {
    let bake = terrain_bake_params;
    let texels = bake.cell.z;
    let rough = bake.cell.w > 0.5;
    let uv = clamp((f.pos.xy - bake.cell.xy - vec2f(0.5)) / (texels - 1.0), vec2f(0.0),
        vec2f(1.0));
    let g = uv * terrain_bake_steps();
    let geometric = terrain_smooth_normal(g, rough);
    let b = terrain_blend_layers(f, geometric);
    var s = surface_of(f, geometric, b.layers);
#ifdef BOX_TEXTURE_LAYERS
    s = layered(f, s, blend_heights, blending.count, b.low);
#endif
    let h = terrain_smooth(g, rough);
    let p = bake.tile.xy + uv * bake.tile.z;
    let texel = terrain_bake_meters() / (texels - 1.0);
    var o: TerrainBakeOut;
    o.albedo = vec4f(sqrt(clamp(s.albedo, vec3f(0.0), vec3f(1.0))),
        clamp(s.occlusion, 2.0 / 255.0, 1.0));
    o.normal = vec4f(s.normal.x * 0.5 + 0.5, s.normal.z * 0.5 + 0.5, s.roughness, s.metallic);
    o.light = vec4f(terrain_bake_sun(p, h, texel), terrain_bake_ao(p, h, texel), 0.0, 1.0);
    return o;
}

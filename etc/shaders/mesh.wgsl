#include "include/view.wgsl"
#include "include/light.wgsl"
#include "include/probe.wgsl"
#include "include/spline.wgsl"

@group(0) @binding(0) var<uniform> u: View;
@group(1) @binding(0) var<storage, read> lights: array<Light>;
@group(1) @binding(1) var<storage, read> clusters: array<vec4<u32>>;
@group(1) @binding(2) var<storage, read> light_indices: array<u32>;
@group(2) @binding(0) var env_irradiance: texture_cube<f32>;
@group(2) @binding(1) var env_specular: texture_cube<f32>;
@group(2) @binding(2) var env_brdf: texture_2d<f32>;
@group(2) @binding(3) var env_sampler: sampler;
@group(3) @binding(0) var<storage, read> probes: array<Probe>;
@group(3) @binding(1) var<storage, read> bake_data: array<u32>;

#include "include/sparse_probe.wgsl"

#include "include/shadow.wgsl"
#include "include/textures.wgsl"
#include "include/common.wgsl"
#include "include/gbuffer.wgsl"
#include "include/tiling.wgsl"
#include "include/pbr.wgsl"
#include "include/cluster.wgsl"
#include "include/shape2d.wgsl"
#include "include/surface.wgsl"
#include "include/light_modulator.wgsl"

struct LightModulatorBlock {
  p: array<LightModulatorParams, MODULATOR_MAX>,
};

@group(0) @binding(4) var<uniform> light_modulators: LightModulatorBlock;

#include "generated/light_modulators.wgsl"

override box_solid: bool = false;

struct VertexIn {
  @location(0) pos: vec3<f32>,
  @location(1) normal: vec3<f32>,
  @location(2) uv: vec2<f32>,
  @location(3) row0: vec4<f32>,
  @location(4) row1: vec4<f32>,
  @location(5) row2: vec4<f32>,
  @location(6) color: vec4<f32>,
  @location(7) emissive: vec4<f32>,
  @location(8) params: vec4<f32>,
  @location(9) textures: vec2<u32>,
  @location(10) uv_xform: vec4<f32>,
  @location(11) uv_mode: u32,
  @location(12) bounds_pos: vec4<f32>,
  @location(13) vertex_color: vec4<f32>,
  @location(14) shape: vec4<u32>,
  @location(15) layer_data: vec2<u32>,
  @location(16) clip_rect: vec4<f32>,
  @location(17) sweep_center: vec3<f32>,
  @location(18) sweep_tangent: vec3<f32>,
  @location(19) sweep_profile: vec4<f32>,
};

struct VertexOut {
  @builtin(position) clip: vec4<f32>,
  @location(0) world_pos: vec3<f32>,
  @location(1) normal: vec3<f32>,
  @location(2) uv: vec2<f32>,
  @location(3) @interpolate(flat) color: vec4<f32>,
  @location(4) @interpolate(flat) emissive: vec4<f32>,
  @location(5) @interpolate(flat) params: vec4<f32>,
  @location(6) @interpolate(flat) shape: vec4<u32>,
  @location(7) @interpolate(flat) scale_handedness: vec4<f32>,
  @location(8) @interpolate(flat) textures: vec2<u32>,
  @location(9) local_pos: vec3<f32>,
  @location(10) local_normal: vec3<f32>,
  @location(11) @interpolate(flat) uv_xform: vec4<f32>,
  @location(12) @interpolate(flat) uv_mode: vec4<u32>,
  @location(13) bounds_pos: vec3<f32>,
  @location(14) vertex_color: vec4<f32>,
  @location(15) @interpolate(flat) clip_rect: vec4<f32>,
};

// Bit of the mode word that says the instance carries a clip rectangle, which
// is BOX_MODE_CLIP_SHIFT of the packet.
const MESH_CLIP_BIT: u32 = 0x40u;
const MESH_BACKDROP_BIT: u32 = 0x80u;

// Drop a fragment the clip rectangle of its instance leaves out. The rectangle
// is axis aligned in the plane a UI tree is laid out in, so the test is the
// world position of the fragment against its four edges and an instance without
// one pays a single bit test.
fn clip_fragment(in: VertexOut) {
  if (!box_solid && (in.uv_mode.x & MESH_CLIP_BIT) != 0u) {
    if (in.world_pos.x < in.clip_rect.x || in.world_pos.x > in.clip_rect.z ||
        in.world_pos.y < in.clip_rect.y || in.world_pos.y > in.clip_rect.w) {
      discard;
    }
  }
}

@vertex fn vs_main(in: VertexIn) -> VertexOut {
  let m = mat4x4<f32>(
    vec4(in.row0.x, in.row1.x, in.row2.x, 0.0),
    vec4(in.row0.y, in.row1.y, in.row2.y, 0.0),
    vec4(in.row0.z, in.row1.z, in.row2.z, 0.0),
    vec4(in.row0.w, in.row1.w, in.row2.w, 1.0));
  let sweep = spline_sweep(in.pos, in.normal, in.sweep_center,
    in.sweep_tangent, in.sweep_profile);
  let world = m * vec4(sweep.position, 1.0);
  var out: VertexOut;
  out.clip = u.proj * u.view * world;
  out.world_pos = world.xyz;
  out.normal = normalize((m * vec4(sweep.normal, 0.0)).xyz);
  out.uv = in.uv;
  out.color = in.color;
  out.emissive = in.emissive;
  out.textures = in.textures;
  out.params = in.params;
  out.shape = in.shape;
  let local_scale = vec3(
    length(vec3(in.row0.x, in.row1.x, in.row2.x)),
    length(vec3(in.row0.y, in.row1.y, in.row2.y)),
    length(vec3(in.row0.z, in.row1.z, in.row2.z)));
  out.local_pos = sweep.position * local_scale;
  out.local_normal = sweep.normal;
  out.uv_xform = in.uv_xform;
  out.uv_mode = vec4(in.uv_mode, u32(in.params.w), in.layer_data.x, in.layer_data.y);
  out.bounds_pos = in.bounds_pos.xyz;
  out.vertex_color = in.vertex_color;
  out.clip_rect = in.clip_rect;
  out.scale_handedness = vec4(local_scale,
    dot(cross(m[0].xyz, m[1].xyz), m[2].xyz));

  return out;
}

fn baked(world: vec3<f32>, gn: vec3<f32>, n: vec3<f32>, albedo: vec3<f32>,
    metallic: f32) -> vec3<f32> {
  let spacing = bake_spacing();
  let origin = world + gn * spacing * 0.02;
  let coord = origin / spacing - vec3(0.5);
  let lo = vec3<i32>(floor(coord));
  let f = fract(coord);
  let cell_chunk_coord = vec3<i32>(floor(vec3<f32>(lo) / 4.0));
  let cell_chunk = bake_chunk(cell_chunk_coord);
  if (cell_chunk == 0xffffffffu) { return vec3(0.0); }
  let cell_local = vec3<u32>(lo - cell_chunk_coord * 4);
  var sr = vec4<f32>(0.0);
  var sg = vec4<f32>(0.0);
  var sb = vec4<f32>(0.0);
  var total = 0.0;
  for (var i = 0u; i < 8u; i ++) {
    let bx = (i & 1u) != 0u;
    let by = (i & 2u) != 0u;
    let bz = (i & 4u) != 0u;
    let corner = vec3<u32>(i & 1u, (i >> 1u) & 1u, (i >> 2u) & 1u);
    let relative = cell_local + corner;
    let neighbor = relative >> vec3(2u);
    let chunk = bake_data[bake_data[5] + cell_chunk * 8u + neighbor.x + neighbor.y * 2u + neighbor.z * 4u];
    if (chunk == 0xffffffffu) { continue; }
    let w = select(1.0 - f.x, f.x, bx) * select(1.0 - f.y, f.y, by) *
      select(1.0 - f.z, f.z, bz);
    let local = relative & vec3(3u);
    let p = probes[chunk * 64u + local.x + local.y * 4u + local.z * 16u];
    if (p.r.x < 0.0) { continue; }
    sr += p.r * w;
    sg += p.g * w;
    sb += p.b * w;
    total += w;
  }
  let basis = vec4(SH_A0, SH_A1 * n.y, SH_A1 * n.z, SH_A1 * n.x);
  let irradiance = max(vec3(dot(sr, basis), dot(sg, basis), dot(sb, basis)), vec3(0.0));
  return irradiance * albedo * (1.0 - metallic) / max(total, 1e-4);
}

fn punctual(world_pos: vec3<f32>, n: vec3<f32>, v: vec3<f32>, albedo: vec3<f32>,
    metallic: f32, roughness: f32, skip_static: bool) -> vec3<f32> {
  var sum = vec3<f32>(0.0);
  let vp = (u.view * vec4(world_pos, 1.0)).xyz;
  let cluster = clusters[cluster_index(vp)];
  let entry = select(cluster.xy, cluster.zw, skip_static);
  for (var i = 0u; i < entry.y; i = i + 1u) {
    let lt = lights[light_indices[entry.x + i]];
    let delta = lt.position - world_pos;
    let dist = length(delta);
    let l = delta / max(dist, 1e-4);
    let window = clamp(1.0 - pow(dist / max(lt.range, 1e-4), 4.0), 0.0, 1.0);
    var atten = window * window / (dist * dist + 1.0);
    if (lt.kind == 1u) {
      let cd = dot(-l, lt.direction);
      let span = max(lt.cos_inner - lt.cos_outer, 1e-4);
      let cone = clamp((cd - lt.cos_outer) / span, 0.0, 1.0);
      atten = atten * cone * cone;
    }
    var radiance = lt.color * lt.intensity * atten;
    if (lt.modulator != 0u) {
      radiance = radiance *
        box_light_modulate_all(lt.modulator, world_pos, n, l, u.frame_time);
    }
    sum = sum + shade(n, v, l, albedo, metallic, roughness) * radiance;
  }
  return sum;
}

#include "generated/layers.wgsl"

struct MeshOut {
  @location(0) color: vec4<f32>,
  @location(1) normal: vec4<f32>,
};

// Luminance weights the indirect fractions of a pixel are measured with.
const MESH_LUMA: vec3<f32> = vec3(0.2126, 0.7152, 0.0722);

// A view that shows a UI tree lights nothing: a node has to show the color it
// was given, and the four percent the environment reflects off a dielectric
// would lift its black to a grey. The stage hands the blend the albedo and the
// emissive of the surface, encoded like the lit stage encodes, and reports no
// indirect light for the passes that read the second attachment.
fn unlit_shade(in: VertexOut, s: ptr<function, Surface>, blend: u32,
    out_normal: ptr<function, vec3<f32>>,
    out_indirect: ptr<function, vec3<f32>>) -> vec4<f32> {
  let albedo = (*s).albedo;
  var color = albedo + (*s).emissive;
  if (u.gamma_encode > 0.5) {
    color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
  }

  *out_normal = (*s).normal;
  *out_indirect = vec3(0.0, 0.0, (*s).roughness);

  let backdrop = select(0.0, in.params.w,
    (in.uv_mode.x & MESH_BACKDROP_BIT) != 0u);
  return resolve(color, vec3(0.0), albedo, (*s).alpha, in.color.a,
    in.clip.xy, (*s).roughness, (*s).normal,
    normalize(u.eye - in.world_pos), blend, backdrop);
}

fn mesh_shade(in: VertexOut, front: bool, out_normal: ptr<function, vec3<f32>>,
    out_indirect: ptr<function, vec3<f32>>) -> vec4<f32> {
  clip_fragment(in);
  let projected = tile_uv(in.uv_mode.x, in.uv_xform, in.uv, in.local_pos, in.local_normal);
  let uv = wrap_uv(in.uv_mode.x, projected);
  let dp1 = dpdx(in.world_pos);
  let dp2 = dpdy(in.world_pos);
  let bounds_fw = fwidth(in.bounds_pos);
  let layer_fw = fwidth(in.local_pos);
  let footprint = length(abs(dp1) + abs(dp2));
  let curvature = length(fwidth(normalize(in.normal))) / max(footprint, 1e-6);
  let shape_fw = bounds_fw.xy;
  var ddx = dpdx(projected);
  var ddy = dpdy(projected);
  if ((in.uv_mode.x & 0xFu) >= 2u) {
    let step = max(length(dp1), length(dp2));
    let lim = step * max(abs(in.uv_xform.x), abs(in.uv_xform.y)) * 4.0 + 1e-8;
    ddx = clamp(ddx, vec2(-lim), vec2(lim));
    ddy = clamp(ddy, vec2(-lim), vec2(lim));
  }
  let blend = (in.uv_mode.x >> 8u) & 0xFu;
  let surface_front = front == (in.scale_handedness.w >= 0.0);
  let plain = !surface_front && ((in.uv_mode.x >> 12u) & 0x3u) == 1u;
  var gn = normalize(in.normal);
  if (!surface_front) {
    gn = -gn;
  }
  var n = gn;
  var albedo = in.color.rgb * srgb_to_linear(in.vertex_color.rgb);
  var alpha = in.color.a;
  var emitted = in.emissive.rgb;
  var rough = in.params.y * in.vertex_color.a;
  var slot_albedo = in.textures.x & 0xFFFFu;
  var slot_emissive = in.textures.x >> 16u;
  var slot_rough = in.textures.y & 0xFFFFu;
  var slot_normal = in.textures.y >> 16u;
  if (plain) {
    slot_albedo = 0u;
    slot_emissive = 0u;
    slot_rough = 0u;
    slot_normal = 0u;
  }
  if (slot_albedo != 0u) {
    let texel = tex_sample(slot_albedo, uv, ddx, ddy);
    albedo = albedo * srgb_to_linear(texel.rgb);
    if (blend == 0u) {
      if (!box_solid && texel.a < 0.5) {
        discard;
      }
    } else {
      alpha = alpha * texel.a;
    }
  }
  if (slot_emissive != 0u) {
    let t = srgb_to_linear(tex_sample(slot_emissive, uv, ddx, ddy).rgb);
    emitted = emitted + t * in.emissive.w;
  }
  if (slot_rough != 0u) {
    rough = rough * tex_sample(slot_rough, uv, ddx, ddy).g;
  }
  if (slot_normal != 0u) {
    let ts = tex_sample(slot_normal, uv, ddx, ddy).xyz * 2.0 - vec3(1.0);
    n = perturb(n, dp1, dp2, ddx, ddy, ts);
  }
  if ((in.uv_mode.x & SHAPE2D_BIT) != 0u && (in.uv_mode.x & SHAPE2D_SOFT) != 0u) {
    alpha = alpha * shape2d_soft(in.shape, in.bounds_pos.xy, shape_fw);
  } else if ((in.uv_mode.x & SHAPE2D_BIT) != 0u) {
    let shape = shape2d_eval(in.shape, in.bounds_pos.xy, shape_fw);
    let border = unpack4x8unorm(in.shape.z);
    albedo = mix(albedo, srgb_to_linear(border.rgb), shape.y);
    alpha = alpha * shape.x * mix(1.0, border.a, shape.y);
    if (!box_solid && blend == 0u && shape.x < 0.5) {
      discard;
    }
  }
  var s: Surface;
  s.albedo = albedo;
  s.alpha = alpha;
  s.emissive = emitted;
  s.roughness = rough;
  s.metallic = in.params.x;
  s.normal = n;
  s.geo_normal = gn;
  s.world_pos = in.world_pos;
  s.local = in.bounds_pos;
  s.local_footprint = bounds_fw;
  s.world_footprint = max(abs(dp1), abs(dp2));
  s.layer_pos = in.local_pos;
  s.layer_normal = in.local_normal;
  s.layer_footprint = layer_fw;
  s.footprint = footprint;
  s.curvature = curvature;
  s.edge_bounds = (in.uv_mode.x & SHAPE2D_BIT) == 0u && in.shape.w == 1u;
  s.layer_extent = bitcast<vec3<f32>>(in.shape.xyz) * in.scale_handedness.xyz;
  s.layer_data = material_layer_data_unpack(
    (in.uv_mode.x & LAYER_DATA_BIT) != 0u, in.uv_mode.z, in.uv_mode.w);
  let mask = in.uv_mode.x >> u32(MODE_LAYER_SHIFT);
  apply_layers(&s, mask);
  if (u.unlit != 0u) {
    return unlit_shade(in, &s, blend, out_normal, out_indirect);
  }

  let l = normalize(-u.sun_dir);
  let v = normalize(u.eye - in.world_pos);
  let sn = s.normal;
  let metallic = s.metallic;
  let roughness = clamp(s.roughness, 0.04, 1.0);
  let sun = u.sun_color * u.sun_intensity;
  let parts = environment_parts(sn, v, s.albedo, metallic, roughness);
  let env = parts.diffuse + parts.specular;
  let is_baked = in.params.z > 0.5;
  var shadow = 1.0;
  if (dot(sn, l) > 0.0 && u.sun_intensity != 0.0) {
    shadow = shadow_factor(in.world_pos, gn);
  }
  var spec = vec3<f32>(0.0);
  if ((blend == 1u || blend == 2u) && u.plain_blend == 0u) {
    spec = glass_specular(sn, v, l, s.albedo, metallic, roughness, sun, shadow);
  }
  var direct = shade(sn, v, l, s.albedo, metallic, roughness) * sun * shadow;
  if (u.sun_modulator != 0u) {
    direct = direct *
      box_light_modulate_all(u.sun_modulator, in.world_pos, sn, l, u.frame_time);
  }

  var color = direct + env + s.emissive;
  color = color + punctual(in.world_pos, sn, v, s.albedo, metallic, roughness, is_baked);
  var indirect = parts.diffuse;
  if (is_baked) {
    let probe = baked(in.world_pos, gn, sn, s.albedo, metallic);
    color = color + probe;
    indirect = indirect + probe;
  }
  let total = max(dot(max(color, vec3(0.0)), MESH_LUMA), 1e-4);
  *out_indirect = vec3(
    clamp(dot(max(indirect, vec3(0.0)), MESH_LUMA) / total, 0.0, 1.0),
    clamp(dot(max(parts.specular, vec3(0.0)), MESH_LUMA) / total, 0.0, 1.0),
    roughness);
#if DEBUG_NORMALS
  color = sn * 0.5 + vec3(0.5);
  spec = vec3<f32>(0.0);
#endif
  if (u.gamma_encode > 0.5) {
    color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
    spec = pow(max(spec, vec3(0.0)), vec3(1.0 / 2.2));
  }
  *out_normal = sn;
  let backdrop = select(0.0, in.params.w,
    (in.uv_mode.x & MESH_BACKDROP_BIT) != 0u);
  return resolve(color, spec, s.albedo, s.alpha, in.color.a, in.clip.xy,
    roughness, sn, v, blend, backdrop);
}

// A subtree that composites as one image is drawn by a quad that shows the
// target its own view rendered it into, weighted by the opacity of the group.
// The target holds premultiplied color over a transparent clear, so the weight
// reaches the blend as one more multiply and the children of the subtree never
// show through each other. The quad binds that target in the slot the glass
// model reads the scene copy from, which is why this stage needs no binding of
// its own.
fn group_composite(in: VertexOut) -> vec4<f32> {
  clip_fragment(in);
  let uv = vec2(in.bounds_pos.x, 1.0 - in.bounds_pos.y);
  return textureSampleLevel(scene_tex, scene_samp, uv, 0.0) * in.color.a;
}

@fragment fn fs_group(in: VertexOut) -> @location(0) vec4<f32> {
  return group_composite(in);
}

@fragment fn fs_group_cover(in: VertexOut) -> MeshOut {
  var out: MeshOut;
  out.color = group_composite(in);
  out.normal = vec4(0.0, 0.0, 0.0, clamp(out.color.a, 0.0, 1.0));
  return out;
}

@fragment fn fs_main(in: VertexOut, @builtin(front_facing) front: bool)
    -> @location(0) vec4<f32> {
  var normal = vec3<f32>(0.0);
  var indirect = vec3<f32>(0.0);
  return mesh_shade(in, front, &normal, &indirect);
}

// The second attachment holds the world normal octahedrally in its first two
// channels, the indirect diffuse fraction of the pixel in the third and the
// indirect specular fraction packed with the roughness in the fourth, which is
// what an ambient occlusion or a reflection pass needs to change the indirect
// light of a pixel without touching the direct.
@fragment fn fs_normal(in: VertexOut, @builtin(front_facing) front: bool) -> MeshOut {
  var normal = vec3<f32>(0.0);
  var indirect = vec3<f32>(0.0);
  var out: MeshOut;
  out.color = mesh_shade(in, front, &normal, &indirect);
  let oct = oct_encode(normalize(normal));
  out.normal = vec4(oct.x, oct.y, indirect.x, gb_pack(indirect.y, indirect.z));
  return out;
}

// The sorted pass draws over the normal attachment the opaque pass filled, and
// a pixel a transparent draw covered is no longer the surface that pass shaded:
// the light it shows is partly the light of the draw. The attachment keeps the
// world normal of the surface behind, because a reflection still leaves from
// it, and this stage hands the blend the coverage of the fragment in the alpha
// channel. The blend state scales the indirect diffuse fraction by what is left
// of the surface behind and drops the indirect specular fraction, so an
// occlusion pass darkens only the part of the pixel that still comes from the
// surface and a reflection pass leaves a covered pixel alone.
@fragment fn fs_cover(in: VertexOut, @builtin(front_facing) front: bool) -> MeshOut {
  var normal = vec3<f32>(0.0);
  var indirect = vec3<f32>(0.0);
  var out: MeshOut;
  out.color = mesh_shade(in, front, &normal, &indirect);
  out.normal = vec4(0.0, 0.0, 0.0, clamp(out.color.a, 0.0, 1.0));
  return out;
}

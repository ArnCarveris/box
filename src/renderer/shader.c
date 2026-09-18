/**
 * @file renderer/shader.c
 * @brief WGSL source of the mesh, cluster and environment prefilter shaders.
 */

#include "renderer_internal.h"

/** Stringify the expansion of a macro */
#define BOX_WGSL_STR_(x) #x

/** Turn the value of a macro into a WGSL literal */
#define BOX_WGSL_STR(x) BOX_WGSL_STR_(x)

/** View uniform block, laid out as box_view_t */
#define BOX_WGSL_VIEW \
    "struct View {\n" \
    "  view: mat4x4<f32>,\n" \
    "  proj: mat4x4<f32>,\n" \
    "  eye: vec3<f32>,\n" \
    "  env_intensity: f32,\n" \
    "  sun_dir: vec3<f32>,\n" \
    "  sun_intensity: f32,\n" \
    "  sun_color: vec3<f32>,\n" \
    "  gamma_encode: f32,\n" \
    "  env_sky: vec4<f32>,\n" \
    "  env_ground: vec4<f32>,\n" \
    "  cluster_x: u32,\n" \
    "  cluster_y: u32,\n" \
    "  cluster_z: u32,\n" \
    "  cluster_max: u32,\n" \
    "  cluster_near: f32,\n" \
    "  cluster_far: f32,\n" \
    "  light_count: u32,\n" \
    "  env_generation: u32,\n" \
    "};\n"

/** Light storage element, laid out as box_light_t */
#define BOX_WGSL_LIGHT \
    "struct Light {\n" \
    "  position: vec3<f32>,\n" \
    "  range: f32,\n" \
    "  color: vec3<f32>,\n" \
    "  intensity: f32,\n" \
    "  direction: vec3<f32>,\n" \
    "  cos_inner: f32,\n" \
    "  cos_outer: f32,\n" \
    "  kind: u32,\n" \
    "  is_static: u32,\n" \
    "  pad: f32,\n" \
    "};\n"

/** Order 1 spherical harmonics probe, laid out as box_probe_t */
#define BOX_WGSL_PROBE \
    "struct Probe {\n" \
    "  r: vec4<f32>,\n" \
    "  g: vec4<f32>,\n" \
    "  b: vec4<f32>,\n" \
    "};\n" \
    "const SH_Y0: f32 = 0.282095;\n" \
    "const SH_Y1: f32 = 0.488603;\n" \
    "const SH_A0: f32 = 0.886227;\n" \
    "const SH_A1: f32 = 1.023327;\n"

/** Probe grid descriptor of one static instance, laid out as box_probe_desc_t */
#define BOX_WGSL_DESC \
    "struct Desc {\n" \
    "  position: vec4<f32>,\n" \
    "  extent: vec4<f32>,\n" \
    "  span: array<vec4<f32>, 3>,\n" \
    "  grid: vec4<u32>,\n" \
    "};\n" \
    "fn probe_dim(d: Desc) -> vec3<u32> {\n" \
    "  return max(d.grid.yzw, vec3<u32>(1u));\n" \
    "}\n" \
    "fn probe_position(d: Desc, index: u32) -> vec3<f32> {\n" \
    "  let dim = probe_dim(d);\n" \
    "  let c = vec3<f32>(vec3<u32>(index % dim.x, (index / dim.x) % dim.y,\n" \
    "    index / (dim.x * dim.y)));\n" \
    "  let last = vec3<f32>(dim) - vec3(1.0);\n" \
    "  let u = select(vec3(0.0), c / max(last, vec3(1.0)) - vec3(0.5), last > vec3(0.0));\n" \
    "  return d.position.xyz + d.span[0].xyz * u.x + d.span[1].xyz * u.y + d.span[2].xyz * u.z;\n" \
    "}\n"

/** Uniforms and storage types of the bake passes, laid out as box_bake_uniform_t */
#define BOX_WGSL_BAKE \
    "struct Bake {\n" \
    "  grid_min: vec4<f32>,\n" \
    "  grid_cell: vec4<f32>,\n" \
    "  grid_dim: vec4<u32>,\n" \
    "  light_count: u32,\n" \
    "  emitter_count: u32,\n" \
    "  steps: u32,\n" \
    "  pad: u32,\n" \
    "};\n" \
    "struct Emitter {\n" \
    "  position: vec3<f32>,\n" \
    "  radius: f32,\n" \
    "  color: vec3<f32>,\n" \
    "  range: f32,\n" \
    "};\n" \
    "struct Job {\n" \
    "  slot: u32,\n" \
    "  index: u32,\n" \
    "};\n" \
    "struct Info {\n" \
    "  desc_count: u32,\n" \
    "  job_count: u32,\n" \
    "  pad0: u32,\n" \
    "  pad1: u32,\n" \
    "};\n"

/** One array texture and sampler per size class, sampled through a packed handle.
 * `perturb` is the shader side of normal.h and bends a normal with a map sample. */
#define BOX_WGSL_TEXTURES \
    "@group(2) @binding(4) var tex0: texture_2d_array<f32>;\n" \
    "@group(2) @binding(5) var tex1: texture_2d_array<f32>;\n" \
    "@group(2) @binding(6) var tex2: texture_2d_array<f32>;\n" \
    "@group(2) @binding(7) var tex3: texture_2d_array<f32>;\n" \
    "@group(2) @binding(8) var samp0: sampler;\n" \
    "@group(2) @binding(9) var samp1: sampler;\n" \
    "@group(2) @binding(10) var samp2: sampler;\n" \
    "@group(2) @binding(11) var samp3: sampler;\n" \
    "@group(2) @binding(12) var scene_tex: texture_2d<f32>;\n" \
    "@group(2) @binding(13) var scene_samp: sampler;\n" \
    "fn tex_sample(slot: u32, uv: vec2<f32>, ddx: vec2<f32>, ddy: vec2<f32>) -> vec4<f32> {\n" \
    "  let cls = slot >> 14u;\n" \
    "  let layer = (slot & 0x3FFFu) - 1u;\n" \
    "  var result = vec4<f32>(1.0);\n" \
    "  if (cls == 0u) {\n" \
    "    result = textureSampleGrad(tex0, samp0, uv, layer, ddx, ddy);\n" \
    "  } else if (cls == 1u) {\n" \
    "    result = textureSampleGrad(tex1, samp1, uv, layer, ddx, ddy);\n" \
    "  } else if (cls == 2u) {\n" \
    "    result = textureSampleGrad(tex2, samp2, uv, layer, ddx, ddy);\n" \
    "  } else {\n" \
    "    result = textureSampleGrad(tex3, samp3, uv, layer, ddx, ddy);\n" \
    "  }\n" \
    "  return result;\n" \
    "}\n" \
    "fn srgb_to_linear(c: vec3<f32>) -> vec3<f32> {\n" \
    "  let lo = c / 12.92;\n" \
    "  let hi = pow((c + vec3(0.055)) / 1.055, vec3(2.4));\n" \
    "  return select(lo, hi, c > vec3(0.04045));\n" \
    "}\n" \
    "fn perturb(n: vec3<f32>, dp1: vec3<f32>, dp2: vec3<f32>, duv1: vec2<f32>,\n" \
    "    duv2: vec2<f32>, ts: vec3<f32>) -> vec3<f32> {\n" \
    "  let perp1 = cross(dp2, n);\n" \
    "  let perp2 = cross(n, dp1);\n" \
    "  let t = perp1 * duv1.x + perp2 * duv2.x;\n" \
    "  let b = perp1 * duv1.y + perp2 * duv2.y;\n" \
    "  let area = dot(n, cross(dp1, dp2));\n" \
    "  let side = select(-1.0, 1.0, area >= 0.0);\n" \
    "  let inv = side * inverseSqrt(max(max(dot(t, t), dot(b, b)), 1e-12));\n" \
    "  return normalize(t * (inv * ts.x) + b * (inv * ts.y) + n * ts.z);\n" \
    "}\n"

/** Texture tiling and wrapping, the shader side of tiling.h */
#define BOX_WGSL_TILING \
    "fn tile_uv(mode: u32, xform: vec4<f32>, mesh_uv: vec2<f32>, p: vec3<f32>,\n" \
    "    n: vec3<f32>) -> vec2<f32> {\n" \
    "  let proj = mode & 0xFu;\n" \
    "  let a = abs(n);\n" \
    "  let r = length(p.xz);\n" \
    "  let angle = atan2(p.z, p.x);\n" \
    "  var base = mesh_uv;\n" \
    "  if (proj == 1u) {\n" \
    "    if (a.x >= a.y && a.x >= a.z) {\n" \
    "      base = vec2(select(p.z, -p.z, n.x < 0.0), p.y);\n" \
    "    } else if (a.y >= a.z) {\n" \
    "      base = vec2(p.x, select(p.z, -p.z, n.y < 0.0));\n" \
    "    } else {\n" \
    "      base = vec2(select(-p.x, p.x, n.z < 0.0), p.y);\n" \
    "    }\n" \
    "  } else if (proj == 2u) {\n" \
    "    base = vec2(angle * r, p.y);\n" \
    "  } else if (proj == 3u) {\n" \
    "    base = vec2(r, angle * r);\n" \
    "  }\n" \
    "  return base * xform.xy + xform.zw;\n" \
    "}\n" \
    "fn wrap_uv(mode: u32, uv: vec2<f32>) -> vec2<f32> {\n" \
    "  let w = (mode >> 4u) & 0xFu;\n" \
    "  var result = uv;\n" \
    "  if (w == 1u) {\n" \
    "    result = clamp(uv, vec2(0.0), vec2(1.0));\n" \
    "  } else if (w == 2u) {\n" \
    "    let t = uv - 2.0 * floor(uv * 0.5);\n" \
    "    result = vec2(1.0) - abs(t - vec2(1.0));\n" \
    "  }\n" \
    "  return result;\n" \
    "}\n"

/** Combines the shaded color with the blurred copy of the opaque target.
 * The alpha blended modes premultiply and take part of their background from
 * the mip chain of the copy, the roughness picking the mip and the weight. */
#define BOX_WGSL_RESOLVE \
    "fn resolve(color: vec3<f32>, alpha: f32, frag: vec2<f32>, roughness: f32,\n" \
    "    blend: u32) -> vec4<f32> {\n" \
    "  var rgb = color;\n" \
    "  var a = alpha;\n" \
    "  if (blend == 1u || blend == 2u) {\n" \
    "    let dims = vec2<f32>(textureDimensions(scene_tex, 0));\n" \
    "    let levels = f32(textureNumLevels(scene_tex)) - 1.0;\n" \
    "    let lod = clamp(roughness, 0.0, 1.0) *\n" \
    "      min(levels, f32(" BOX_WGSL_STR(BOX_SCENE_BLUR_MIPS) "));\n" \
    "    let bg = textureSampleLevel(scene_tex, scene_samp, frag / dims, lod).rgb;\n" \
    "    let t = roughness * roughness;\n" \
    "    var pre = rgb * a;\n" \
    "    if (blend == 2u) {\n" \
    "      pre = rgb;\n" \
    "    }\n" \
    "    rgb = pre + (1.0 - a) * t * bg;\n" \
    "    a = 1.0 - (1.0 - a) * (1.0 - t);\n" \
    "  }\n" \
    "  return vec4(rgb, a);\n" \
    "}\n"

/** Cluster a view space position falls in, the shader side of box_cluster_index */
#define BOX_WGSL_CLUSTER_INDEX \
    "fn cluster_index(vp: vec3<f32>) -> u32 {\n" \
    "  let depth = max(vp.z, 1e-4);\n" \
    "  let ndc_x = u.proj[0][0] * vp.x / depth;\n" \
    "  let ndc_y = u.proj[1][1] * vp.y / depth;\n" \
    "  let fx = (ndc_x + 1.0) * 0.5 * f32(u.cluster_x);\n" \
    "  let fy = (1.0 - ndc_y) * 0.5 * f32(u.cluster_y);\n" \
    "  let t = log(depth / u.cluster_near) / log(u.cluster_far / u.cluster_near);\n" \
    "  let fz = t * f32(u.cluster_z);\n" \
    "  let x = min(u32(max(fx, 0.0)), u.cluster_x - 1u);\n" \
    "  let y = min(u32(max(fy, 0.0)), u.cluster_y - 1u);\n" \
    "  let z = min(u32(max(fz, 0.0)), u.cluster_z - 1u);\n" \
    "  return x + y * u.cluster_x + z * u.cluster_x * u.cluster_y;\n" \
    "}\n"

const char *box_mesh_shader_wgsl =
    BOX_WGSL_VIEW
    BOX_WGSL_LIGHT
    BOX_WGSL_PROBE
    BOX_WGSL_DESC
    "@group(0) @binding(0) var<uniform> u: View;\n"
    "@group(1) @binding(0) var<storage, read> lights: array<Light>;\n"
    "@group(1) @binding(1) var<storage, read> clusters: array<vec2<u32>>;\n"
    "@group(1) @binding(2) var<storage, read> light_indices: array<u32>;\n"
    "@group(2) @binding(0) var env_irradiance: texture_cube<f32>;\n"
    "@group(2) @binding(1) var env_specular: texture_cube<f32>;\n"
    "@group(2) @binding(2) var env_brdf: texture_2d<f32>;\n"
    "@group(2) @binding(3) var env_sampler: sampler;\n"
    "@group(3) @binding(0) var<storage, read> probes: array<Probe>;\n"
    "@group(3) @binding(1) var<storage, read> probe_descs: array<Desc>;\n"
    BOX_WGSL_TEXTURES
    BOX_WGSL_TILING
    BOX_WGSL_RESOLVE
    BOX_WGSL_CLUSTER_INDEX
    "fn probe_axis(span: vec3<f32>, rel: vec3<f32>, last: f32) -> f32 {\n"
    "  var result = 0.0;\n"
    "  let len = dot(span, span);\n"
    "  if (len > 1e-8) {\n"
    "    result = dot(rel, span) / len * last;\n"
    "  }\n"
    "  return result;\n"
    "}\n"
    "fn probe_coord(slot: u32, world: vec3<f32>) -> vec3<f32> {\n"
    "  let d = probe_descs[slot];\n"
    "  let last = vec3<f32>(probe_dim(d)) - vec3(1.0);\n"
    "  let rel = world - probe_position(d, 0u);\n"
    "  return vec3(probe_axis(d.span[0].xyz, rel, last.x),\n"
    "              probe_axis(d.span[1].xyz, rel, last.y),\n"
    "              probe_axis(d.span[2].xyz, rel, last.z));\n"
    "}\n"
    "struct VertexIn {\n"
    "  @builtin(instance_index) instance: u32,\n"
    "  @location(0) pos: vec3<f32>,\n"
    "  @location(1) normal: vec3<f32>,\n"
    "  @location(2) uv: vec2<f32>,\n"
    "  @location(3) row0: vec4<f32>,\n"
    "  @location(4) row1: vec4<f32>,\n"
    "  @location(5) row2: vec4<f32>,\n"
    "  @location(6) color: vec4<f32>,\n"
    "  @location(7) emissive: vec4<f32>,\n"
    "  @location(8) params: vec4<f32>,\n"
    "  @location(9) textures: vec2<u32>,\n"
    "  @location(10) uv_xform: vec4<f32>,\n"
    "  @location(11) uv_mode: u32,\n"
    "};\n"
    "struct VertexOut {\n"
    "  @builtin(position) clip: vec4<f32>,\n"
    "  @location(0) world_pos: vec3<f32>,\n"
    "  @location(1) normal: vec3<f32>,\n"
    "  @location(2) uv: vec2<f32>,\n"
    "  @location(3) color: vec4<f32>,\n"
    "  @location(4) emissive: vec4<f32>,\n"
    "  @location(5) params: vec4<f32>,\n"
    "  @location(6) @interpolate(flat) slot: u32,\n"
    "  @location(7) local: vec3<f32>,\n"
    "  @location(8) @interpolate(flat) textures: vec2<u32>,\n"
    "  @location(9) local_pos: vec3<f32>,\n"
    "  @location(10) local_normal: vec3<f32>,\n"
    "  @location(11) @interpolate(flat) uv_xform: vec4<f32>,\n"
    "  @location(12) @interpolate(flat) uv_mode: u32,\n"
    "};\n"
    "@vertex fn vs_main(in: VertexIn) -> VertexOut {\n"
    "  let m = mat4x4<f32>(\n"
    "    vec4(in.row0.x, in.row1.x, in.row2.x, 0.0),\n"
    "    vec4(in.row0.y, in.row1.y, in.row2.y, 0.0),\n"
    "    vec4(in.row0.z, in.row1.z, in.row2.z, 0.0),\n"
    "    vec4(in.row0.w, in.row1.w, in.row2.w, 1.0));\n"
    "  let world = m * vec4(in.pos, 1.0);\n"
    "  var out: VertexOut;\n"
    "  out.clip = u.proj * u.view * world;\n"
    "  out.world_pos = world.xyz;\n"
    "  out.normal = normalize((m * vec4(in.normal, 0.0)).xyz);\n"
    "  out.uv = in.uv;\n"
    "  out.color = in.color;\n"
    "  out.emissive = in.emissive;\n"
    "  out.textures = in.textures;\n"
    "  out.params = in.params;\n"
    "  out.slot = in.instance;\n"
    "  out.local_pos = in.pos * vec3(\n"
    "    length(vec3(in.row0.x, in.row1.x, in.row2.x)),\n"
    "    length(vec3(in.row0.y, in.row1.y, in.row2.y)),\n"
    "    length(vec3(in.row0.z, in.row1.z, in.row2.z)));\n"
    "  out.local_normal = in.normal;\n"
    "  out.uv_xform = in.uv_xform;\n"
    "  out.uv_mode = in.uv_mode;\n"
    "  out.local = vec3<f32>(0.0);\n"
    "  if (in.params.z > 0.5) {\n"
    "    out.local = probe_coord(in.instance, world.xyz);\n"
    "  }\n"
    "  return out;\n"
    "}\n"
    "fn shade(n: vec3<f32>, v: vec3<f32>, l: vec3<f32>, albedo: vec3<f32>,\n"
    "    metallic: f32, roughness: f32) -> vec3<f32> {\n"
    "  let h = normalize(l + v);\n"
    "  let ndl = max(dot(n, l), 0.0);\n"
    "  let shininess = (1.0 - roughness) * (1.0 - roughness) * 512.0 + 2.0;\n"
    "  let spec = pow(max(dot(n, h), 0.0), shininess) * (1.0 - roughness);\n"
    "  let f0 = mix(vec3(0.04), albedo, metallic);\n"
    "  return (albedo * (1.0 - metallic) + f0 * spec) * ndl;\n"
    "}\n"
    "fn environment(n: vec3<f32>, v: vec3<f32>, albedo: vec3<f32>,\n"
    "    metallic: f32, roughness: f32) -> vec3<f32> {\n"
    "  let ndv = max(dot(n, v), 1e-4);\n"
    "  let f0 = mix(vec3(0.04), albedo, metallic);\n"
    "  let irradiance = textureSampleLevel(env_irradiance, env_sampler, n, 0.0).rgb;\n"
    "  let diffuse = irradiance * albedo * (1.0 - metallic);\n"
    "  let r = reflect(-v, n);\n"
    "  let mip = roughness * (f32(" BOX_WGSL_STR(BOX_ENV_MIPS) ") - 1.0);\n"
    "  let prefiltered = textureSampleLevel(env_specular, env_sampler, r, mip).rgb;\n"
    "  let ab = textureSampleLevel(env_brdf, env_sampler, vec2(ndv, roughness), 0.0).xy;\n"
    "  return diffuse + prefiltered * (f0 * ab.x + vec3(ab.y));\n"
    "}\n"
    "fn baked(slot: u32, local: vec3<f32>, n: vec3<f32>, albedo: vec3<f32>,\n"
    "    metallic: f32) -> vec3<f32> {\n"
    "  let d = probe_descs[slot];\n"
    "  let dim = probe_dim(d);\n"
    "  let t = clamp(local, vec3(0.0), vec3<f32>(dim) - vec3(1.0));\n"
    "  let f = t - floor(t);\n"
    "  let lo = vec3<u32>(floor(t));\n"
    "  let hi = min(lo + vec3<u32>(1u), dim - vec3<u32>(1u));\n"
    "  var sr = vec4<f32>(0.0);\n"
    "  var sg = vec4<f32>(0.0);\n"
    "  var sb = vec4<f32>(0.0);\n"
    "  for (var i = 0u; i < 8u; i = i + 1u) {\n"
    "    let bx = (i & 1u) != 0u;\n"
    "    let by = (i & 2u) != 0u;\n"
    "    let bz = (i & 4u) != 0u;\n"
    "    let c = vec3<u32>(select(lo.x, hi.x, bx), select(lo.y, hi.y, by),\n"
    "      select(lo.z, hi.z, bz));\n"
    "    let w = select(1.0 - f.x, f.x, bx) * select(1.0 - f.y, f.y, by) *\n"
    "      select(1.0 - f.z, f.z, bz);\n"
    "    let p = probes[d.grid.x + c.x + c.y * dim.x + c.z * dim.x * dim.y];\n"
    "    sr = sr + p.r * w;\n"
    "    sg = sg + p.g * w;\n"
    "    sb = sb + p.b * w;\n"
    "  }\n"
    "  let basis = vec4(SH_A0, SH_A1 * n.y, SH_A1 * n.z, SH_A1 * n.x);\n"
    "  let irradiance = max(vec3(dot(sr, basis), dot(sg, basis), dot(sb, basis)), vec3(0.0));\n"
    "  return irradiance * albedo * (1.0 - metallic);\n"
    "}\n"
    "fn punctual(world_pos: vec3<f32>, n: vec3<f32>, v: vec3<f32>, albedo: vec3<f32>,\n"
    "    metallic: f32, roughness: f32, skip_static: bool) -> vec3<f32> {\n"
    "  var sum = vec3<f32>(0.0);\n"
    "  let vp = (u.view * vec4(world_pos, 1.0)).xyz;\n"
    "  let entry = clusters[cluster_index(vp)];\n"
    "  for (var i = 0u; i < entry.y; i = i + 1u) {\n"
    "    let lt = lights[light_indices[entry.x + i]];\n"
    "    if (skip_static && lt.is_static != 0u) {\n"
    "      continue;\n"
    "    }\n"
    "    let delta = lt.position - world_pos;\n"
    "    let dist = length(delta);\n"
    "    let l = delta / max(dist, 1e-4);\n"
    "    let window = clamp(1.0 - pow(dist / max(lt.range, 1e-4), 4.0), 0.0, 1.0);\n"
    "    var atten = window * window / (dist * dist + 1.0);\n"
    "    if (lt.kind == 1u) {\n"
    "      let cd = dot(-l, lt.direction);\n"
    "      let span = max(lt.cos_inner - lt.cos_outer, 1e-4);\n"
    "      let cone = clamp((cd - lt.cos_outer) / span, 0.0, 1.0);\n"
    "      atten = atten * cone * cone;\n"
    "    }\n"
    "    let radiance = lt.color * lt.intensity * atten;\n"
    "    sum = sum + shade(n, v, l, albedo, metallic, roughness) * radiance;\n"
    "  }\n"
    "  return sum;\n"
    "}\n"
    "@fragment fn fs_main(in: VertexOut, @builtin(front_facing) front: bool)\n"
    "    -> @location(0) vec4<f32> {\n"
    "  let projected = tile_uv(in.uv_mode, in.uv_xform, in.uv, in.local_pos, in.local_normal);\n"
    "  let uv = wrap_uv(in.uv_mode, projected);\n"
    "  let dp1 = dpdx(in.world_pos);\n"
    "  let dp2 = dpdy(in.world_pos);\n"
    "  var ddx = dpdx(projected);\n"
    "  var ddy = dpdy(projected);\n"
    "  if ((in.uv_mode & 0xFu) >= 2u) {\n"
    "    let step = max(length(dp1), length(dp2));\n"
    "    let lim = step * max(abs(in.uv_xform.x), abs(in.uv_xform.y)) * 4.0 + 1e-8;\n"
    "    ddx = clamp(ddx, vec2(-lim), vec2(lim));\n"
    "    ddy = clamp(ddy, vec2(-lim), vec2(lim));\n"
    "  }\n"
    "  let blend = (in.uv_mode >> 8u) & 0xFu;\n"
    "  let plain = !front && ((in.uv_mode >> 12u) & 0x3u) == 1u;\n"
    "  var n = normalize(in.normal);\n"
    "  if (!front) {\n"
    "    n = -n;\n"
    "  }\n"
    "  var albedo = in.color.rgb;\n"
    "  var alpha = in.color.a;\n"
    "  var emitted = in.emissive.rgb;\n"
    "  var rough = in.params.y;\n"
    "  var slot_albedo = in.textures.x & 0xFFFFu;\n"
    "  var slot_emissive = in.textures.x >> 16u;\n"
    "  var slot_rough = in.textures.y & 0xFFFFu;\n"
    "  var slot_normal = in.textures.y >> 16u;\n"
    "  if (plain) {\n"
    "    slot_albedo = 0u;\n"
    "    slot_emissive = 0u;\n"
    "    slot_rough = 0u;\n"
    "    slot_normal = 0u;\n"
    "  }\n"
    "  if (slot_albedo != 0u) {\n"
    "    let texel = tex_sample(slot_albedo, uv, ddx, ddy);\n"
    "    albedo = albedo * srgb_to_linear(texel.rgb);\n"
    "    if (blend == 0u) {\n"
    "      if (texel.a < 0.5) {\n"
    "        discard;\n"
    "      }\n"
    "    } else {\n"
    "      alpha = alpha * texel.a;\n"
    "    }\n"
    "  }\n"
    "  if (slot_emissive != 0u) {\n"
    "    let t = srgb_to_linear(tex_sample(slot_emissive, uv, ddx, ddy).rgb);\n"
    "    emitted = emitted + t * in.emissive.w;\n"
    "  }\n"
    "  if (slot_rough != 0u) {\n"
    "    rough = rough * tex_sample(slot_rough, uv, ddx, ddy).g;\n"
    "  }\n"
    "  if (slot_normal != 0u) {\n"
    "    let ts = tex_sample(slot_normal, uv, ddx, ddy).xyz * 2.0 - vec3(1.0);\n"
    "    n = perturb(n, dp1, dp2, ddx, ddy, ts);\n"
    "  }\n"
    "  let l = normalize(-u.sun_dir);\n"
    "  let v = normalize(u.eye - in.world_pos);\n"
    "  let metallic = in.params.x;\n"
    "  let roughness = clamp(rough, 0.04, 1.0);\n"
    "  let sun = u.sun_color * u.sun_intensity;\n"
    "  let env = environment(n, v, albedo, metallic, roughness);\n"
    "  let is_baked = in.params.z > 0.5;\n"
    "  var color = shade(n, v, l, albedo, metallic, roughness) * sun + env + emitted;\n"
    "  color = color + punctual(in.world_pos, n, v, albedo, metallic, roughness, is_baked);\n"
    "  if (is_baked) {\n"
    "    color = color + baked(in.slot, in.local, n, albedo, metallic);\n"
    "  }\n"
    "  if (u.gamma_encode > 0.5) {\n"
    "    color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));\n"
    "  }\n"
    "  return resolve(color, alpha, in.clip.xy, roughness, blend);\n"
    "}\n";

const char *box_cluster_shader_wgsl =
    BOX_WGSL_VIEW
    BOX_WGSL_LIGHT
    "@group(0) @binding(0) var<uniform> u: View;\n"
    "@group(0) @binding(1) var<storage, read> lights: array<Light>;\n"
    "@group(0) @binding(2) var<storage, read_write> clusters: array<vec2<u32>>;\n"
    "@group(0) @binding(3) var<storage, read_write> light_indices: array<u32>;\n"
    "fn slice_depth(slice: f32) -> f32 {\n"
    "  return u.cluster_near * pow(u.cluster_far / u.cluster_near, slice);\n"
    "}\n"
    "@compute @workgroup_size(4, 4, 4)\n"
    "fn cs_main(@builtin(global_invocation_id) gid: vec3<u32>) {\n"
    "  if (gid.x >= u.cluster_x || gid.y >= u.cluster_y || gid.z >= u.cluster_z) {\n"
    "    return;\n"
    "  }\n"
    "  let cluster = gid.x + gid.y * u.cluster_x + gid.z * u.cluster_x * u.cluster_y;\n"
    "  let z0 = slice_depth(f32(gid.z) / f32(u.cluster_z));\n"
    "  let z1 = slice_depth(f32(gid.z + 1u) / f32(u.cluster_z));\n"
    "  let tx = 1.0 / u.proj[0][0];\n"
    "  let ty = 1.0 / u.proj[1][1];\n"
    "  let x0 = (f32(gid.x) / f32(u.cluster_x)) * 2.0 - 1.0;\n"
    "  let x1 = (f32(gid.x + 1u) / f32(u.cluster_x)) * 2.0 - 1.0;\n"
    "  let y1 = 1.0 - (f32(gid.y) / f32(u.cluster_y)) * 2.0;\n"
    "  let y0 = 1.0 - (f32(gid.y + 1u) / f32(u.cluster_y)) * 2.0;\n"
    "  let cx = vec4(x0 * tx * z0, x0 * tx * z1, x1 * tx * z0, x1 * tx * z1);\n"
    "  let cy = vec4(y0 * ty * z0, y0 * ty * z1, y1 * ty * z0, y1 * ty * z1);\n"
    "  let lo = vec3(min(min(cx.x, cx.y), min(cx.z, cx.w)),\n"
    "                min(min(cy.x, cy.y), min(cy.z, cy.w)), z0);\n"
    "  let hi = vec3(max(max(cx.x, cx.y), max(cx.z, cx.w)),\n"
    "                max(max(cy.x, cy.y), max(cy.z, cy.w)), z1);\n"
    "  let base = cluster * u.cluster_max;\n"
    "  var count = 0u;\n"
    "  for (var i = 0u; i < u.light_count && count < u.cluster_max; i = i + 1u) {\n"
    "    let p = (u.view * vec4(lights[i].position, 1.0)).xyz;\n"
    "    let d = p - clamp(p, lo, hi);\n"
    "    if (dot(d, d) <= lights[i].range * lights[i].range) {\n"
    "      light_indices[base + count] = i;\n"
    "      count = count + 1u;\n"
    "    }\n"
    "  }\n"
    "  clusters[cluster] = vec2(base, count);\n"
    "}\n";

/** Per pass uniform block of the environment passes, laid out as box_env_uniform_t */
#define BOX_WGSL_ENV \
    "struct Env {\n" \
    "  sky: vec4<f32>,\n" \
    "  ground: vec4<f32>,\n" \
    "  params: vec4<f32>,\n" \
    "};\n" \
    "@group(0) @binding(0) var<uniform> e: Env;\n" \
    "struct EnvOut {\n" \
    "  @builtin(position) clip: vec4<f32>,\n" \
    "  @location(0) uv: vec2<f32>,\n" \
    "};\n" \
    "@vertex fn vs_main(@builtin(vertex_index) i: u32) -> EnvOut {\n" \
    "  var p = array<vec2<f32>, 3>(vec2(-1.0, -3.0), vec2(-1.0, 1.0), vec2(3.0, 1.0));\n" \
    "  var out: EnvOut;\n" \
    "  out.clip = vec4(p[i], 0.0, 1.0);\n" \
    "  out.uv = vec2(p[i].x * 0.5 + 0.5, 0.5 - p[i].y * 0.5);\n" \
    "  return out;\n" \
    "}\n" \
    "fn face_dir(face: u32, uv: vec2<f32>) -> vec3<f32> {\n" \
    "  let s = uv.x * 2.0 - 1.0;\n" \
    "  let t = uv.y * 2.0 - 1.0;\n" \
    "  var d = vec3(s, -t, 1.0);\n" \
    "  if (face == 0u) { d = vec3(1.0, -t, -s); }\n" \
    "  else if (face == 1u) { d = vec3(-1.0, -t, s); }\n" \
    "  else if (face == 2u) { d = vec3(s, 1.0, t); }\n" \
    "  else if (face == 3u) { d = vec3(s, -1.0, -t); }\n" \
    "  else if (face == 5u) { d = vec3(-s, -t, -1.0); }\n" \
    "  return normalize(d);\n" \
    "}\n"

/** Low discrepancy sampling and the GGX half vector, shared by the environment passes */
#define BOX_WGSL_ENV_SAMPLE \
    "fn radical_inverse(index: u32) -> f32 {\n" \
    "  var bits = index;\n" \
    "  bits = (bits << 16u) | (bits >> 16u);\n" \
    "  bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);\n" \
    "  bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);\n" \
    "  bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);\n" \
    "  bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);\n" \
    "  return f32(bits) * 2.3283064365386963e-10;\n" \
    "}\n" \
    "fn hammersley(i: u32, count: u32) -> vec2<f32> {\n" \
    "  return vec2(f32(i) / f32(count), radical_inverse(i));\n" \
    "}\n" \
    "fn tangent_to_world(h: vec3<f32>, n: vec3<f32>) -> vec3<f32> {\n" \
    "  var up = vec3(0.0, 0.0, 1.0);\n" \
    "  if (abs(n.z) > 0.999) { up = vec3(1.0, 0.0, 0.0); }\n" \
    "  let tangent = normalize(cross(up, n));\n" \
    "  let bitangent = cross(n, tangent);\n" \
    "  return normalize(tangent * h.x + bitangent * h.y + n * h.z);\n" \
    "}\n" \
    "fn importance_ggx(xi: vec2<f32>, n: vec3<f32>, roughness: f32) -> vec3<f32> {\n" \
    "  let a = roughness * roughness;\n" \
    "  let phi = 6.28318531 * xi.x;\n" \
    "  let cos_theta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));\n" \
    "  let sin_theta = sqrt(max(1.0 - cos_theta * cos_theta, 0.0));\n" \
    "  let h = vec3(sin_theta * cos(phi), sin_theta * sin(phi), cos_theta);\n" \
    "  return tangent_to_world(h, n);\n" \
    "}\n"

const char *box_env_shader_wgsl =
    BOX_WGSL_ENV
    BOX_WGSL_ENV_SAMPLE
    "@fragment fn fs_gradient(in: EnvOut) -> @location(0) vec4<f32> {\n"
    "  let d = face_dir(u32(e.params.x), in.uv);\n"
    "  let t = clamp(d.y * 0.5 + 0.5, 0.0, 1.0);\n"
    "  return vec4(mix(e.ground.rgb, e.sky.rgb, t), 1.0);\n"
    "}\n"
    "@fragment fn fs_brdf(in: EnvOut) -> @location(0) vec4<f32> {\n"
    "  let ndv = max(in.uv.x, 1e-3);\n"
    "  let roughness = max(in.uv.y, 0.02);\n"
    "  let v = vec3(sqrt(max(1.0 - ndv * ndv, 0.0)), 0.0, ndv);\n"
    "  let n = vec3(0.0, 0.0, 1.0);\n"
    "  let count = u32(" BOX_WGSL_STR(BOX_ENV_BRDF_SAMPLES) ");\n"
    "  let k = roughness * roughness * 0.5;\n"
    "  var scale = 0.0;\n"
    "  var bias = 0.0;\n"
    "  for (var i = 0u; i < count; i = i + 1u) {\n"
    "    let h = importance_ggx(hammersley(i, count), n, roughness);\n"
    "    let l = normalize(2.0 * dot(v, h) * h - v);\n"
    "    let ndl = max(l.z, 0.0);\n"
    "    let ndh = max(h.z, 1e-4);\n"
    "    let vdh = max(dot(v, h), 0.0);\n"
    "    if (ndl > 0.0) {\n"
    "      let g = (ndl / (ndl * (1.0 - k) + k)) * (ndv / (ndv * (1.0 - k) + k));\n"
    "      let vis = g * vdh / (ndh * ndv);\n"
    "      let fc = pow(1.0 - vdh, 5.0);\n"
    "      scale = scale + (1.0 - fc) * vis;\n"
    "      bias = bias + fc * vis;\n"
    "    }\n"
    "  }\n"
    "  return vec4(scale / f32(count), bias / f32(count), 0.0, 1.0);\n"
    "}\n";

const char *box_env_filter_wgsl =
    BOX_WGSL_ENV
    "@group(0) @binding(1) var src: texture_cube<f32>;\n"
    "@group(0) @binding(2) var src_sampler: sampler;\n"
    BOX_WGSL_ENV_SAMPLE
    "fn importance_cosine(xi: vec2<f32>, n: vec3<f32>) -> vec3<f32> {\n"
    "  let phi = 6.28318531 * xi.x;\n"
    "  let cos_theta = sqrt(max(1.0 - xi.y, 0.0));\n"
    "  let sin_theta = sqrt(xi.y);\n"
    "  let h = vec3(sin_theta * cos(phi), sin_theta * sin(phi), cos_theta);\n"
    "  return tangent_to_world(h, n);\n"
    "}\n"
    "@fragment fn fs_specular(in: EnvOut) -> @location(0) vec4<f32> {\n"
    "  let n = face_dir(u32(e.params.x), in.uv);\n"
    "  let count = u32(" BOX_WGSL_STR(BOX_ENV_SPECULAR_SAMPLES) ");\n"
    "  var sum = vec3<f32>(0.0);\n"
    "  var weight = 0.0;\n"
    "  for (var i = 0u; i < count; i = i + 1u) {\n"
    "    let h = importance_ggx(hammersley(i, count), n, e.params.y);\n"
    "    let l = normalize(2.0 * dot(n, h) * h - n);\n"
    "    let ndl = dot(n, l);\n"
    "    if (ndl > 0.0) {\n"
    "      sum = sum + textureSampleLevel(src, src_sampler, l, 0.0).rgb * ndl;\n"
    "      weight = weight + ndl;\n"
    "    }\n"
    "  }\n"
    "  return vec4(sum / max(weight, 1e-4), 1.0);\n"
    "}\n"
    "@fragment fn fs_irradiance(in: EnvOut) -> @location(0) vec4<f32> {\n"
    "  let n = face_dir(u32(e.params.x), in.uv);\n"
    "  let count = u32(" BOX_WGSL_STR(BOX_ENV_IRRADIANCE_SAMPLES) ");\n"
    "  var sum = vec3<f32>(0.0);\n"
    "  for (var i = 0u; i < count; i = i + 1u) {\n"
    "    let l = importance_cosine(hammersley(i, count), n);\n"
    "    sum = sum + textureSampleLevel(src, src_sampler, l, 0.0).rgb;\n"
    "  }\n"
    "  return vec4(sum / f32(count), 1.0);\n"
    "}\n";

const char *box_bake_grid_wgsl =
    BOX_WGSL_BAKE
    BOX_WGSL_DESC
    "@group(0) @binding(0) var<uniform> u: Bake;\n"
    "@group(0) @binding(1) var<storage, read_write> occupancy: array<atomic<u32>>;\n"
    "@group(1) @binding(0) var<storage, read> descs: array<Desc>;\n"
    "@group(1) @binding(1) var<uniform> info: Info;\n"
    "@compute @workgroup_size(64)\n"
    "fn cs_grid(@builtin(global_invocation_id) gid: vec3<u32>) {\n"
    "  if (gid.x >= info.desc_count || u.grid_dim.x == 0u) {\n"
    "    return;\n"
    "  }\n"
    "  let d = descs[gid.x];\n"
    "  if (d.extent.w == 0.0) {\n"
    "    return;\n"
    "  }\n"
    "  let limit = vec3<f32>(u.grid_dim.xyz) - vec3(1.0);\n"
    "  let lo = (d.position.xyz - d.extent.xyz - u.grid_min.xyz) / u.grid_cell.xyz;\n"
    "  let hi = (d.position.xyz + d.extent.xyz - u.grid_min.xyz) / u.grid_cell.xyz;\n"
    "  let a = vec3<u32>(clamp(lo, vec3(0.0), limit));\n"
    "  let b = vec3<u32>(clamp(hi, vec3(0.0), limit));\n"
    "  for (var z = a.z; z <= b.z; z = z + 1u) {\n"
    "    for (var y = a.y; y <= b.y; y = y + 1u) {\n"
    "      for (var x = a.x; x <= b.x; x = x + 1u) {\n"
    "        let index = x + y * u.grid_dim.x + z * u.grid_dim.x * u.grid_dim.y;\n"
    "        atomicOr(&occupancy[index >> 5u], 1u << (index & 31u));\n"
    "      }\n"
    "    }\n"
    "  }\n"
    "}\n";

const char *box_bake_shader_wgsl =
    BOX_WGSL_LIGHT
    BOX_WGSL_BAKE
    BOX_WGSL_PROBE
    BOX_WGSL_DESC
    "@group(0) @binding(0) var<uniform> u: Bake;\n"
    "@group(0) @binding(1) var<storage, read> lights: array<Light>;\n"
    "@group(0) @binding(2) var<storage, read> emitters: array<Emitter>;\n"
    "@group(0) @binding(3) var<storage, read> occupancy: array<u32>;\n"
    "@group(1) @binding(0) var<storage, read> descs: array<Desc>;\n"
    "@group(1) @binding(1) var<uniform> info: Info;\n"
    "@group(1) @binding(2) var<storage, read_write> probes: array<Probe>;\n"
    "@group(1) @binding(3) var<storage, read> jobs: array<Job>;\n"
    "fn solid(p: vec3<f32>) -> bool {\n"
    "  let t = (p - u.grid_min.xyz) / u.grid_cell.xyz;\n"
    "  let dim = vec3<f32>(u.grid_dim.xyz);\n"
    "  if (any(t < vec3(0.0)) || any(t >= dim)) {\n"
    "    return false;\n"
    "  }\n"
    "  let c = vec3<u32>(t);\n"
    "  let index = c.x + c.y * u.grid_dim.x + c.z * u.grid_dim.x * u.grid_dim.y;\n"
    "  return (occupancy[index >> 5u] & (1u << (index & 31u))) != 0u;\n"
    "}\n"
    "fn box_exit(extent: vec3<f32>, dir: vec3<f32>) -> f32 {\n"
    "  var result = 0.0;\n"
    "  let a = abs(dir);\n"
    "  for (var i = 0u; i < 3u; i = i + 1u) {\n"
    "    if (a[i] > 1e-4) {\n"
    "      let t = extent[i] / a[i];\n"
    "      if (result == 0.0 || t < result) {\n"
    "        result = t;\n"
    "      }\n"
    "    }\n"
    "  }\n"
    "  return result;\n"
    "}\n"
    "fn visible(origin: vec3<f32>, own: vec3<f32>, dest: vec3<f32>, radius: f32) -> f32 {\n"
    "  let delta = dest - origin;\n"
    "  let dist = length(delta);\n"
    "  if (dist < 1e-4 || u.steps == 0u) {\n"
    "    return 1.0;\n"
    "  }\n"
    "  let dir = delta / dist;\n"
    "  let margin = u.grid_cell.w;\n"
    "  let t0 = box_exit(own, dir) + margin;\n"
    "  let t1 = dist - radius - margin;\n"
    "  if (t1 <= t0) {\n"
    "    return 1.0;\n"
    "  }\n"
    "  let step = (t1 - t0) / f32(u.steps);\n"
    "  for (var i = 0u; i < u.steps; i = i + 1u) {\n"
    "    if (solid(origin + dir * (t0 + (f32(i) + 0.5) * step))) {\n"
    "      return 0.0;\n"
    "    }\n"
    "  }\n"
    "  return 1.0;\n"
    "}\n"
    "@compute @workgroup_size(64)\n"
    "fn cs_bake(@builtin(global_invocation_id) gid: vec3<u32>) {\n"
    "  if (gid.x >= info.job_count) {\n"
    "    return;\n"
    "  }\n"
    "  let job = jobs[gid.x];\n"
    "  let d = descs[job.slot];\n"
    "  let origin = probe_position(d, job.index);\n"
    "  let own = d.extent.xyz / vec3<f32>(probe_dim(d));\n"
    "  var sr = vec4<f32>(0.0);\n"
    "  var sg = vec4<f32>(0.0);\n"
    "  var sb = vec4<f32>(0.0);\n"
    "  for (var i = 0u; i < u.light_count; i = i + 1u) {\n"
    "    let lt = lights[i];\n"
    "    let delta = lt.position - origin;\n"
    "    let dist = length(delta);\n"
    "    let range = max(lt.range, 1e-4);\n"
    "    if (dist >= range) {\n"
    "      continue;\n"
    "    }\n"
    "    let dir = delta / max(dist, 1e-4);\n"
    "    let window = clamp(1.0 - pow(dist / range, 4.0), 0.0, 1.0);\n"
    "    var atten = window * window / (dist * dist + 1.0);\n"
    "    if (lt.kind == 1u) {\n"
    "      let cd = dot(-dir, lt.direction);\n"
    "      let span = max(lt.cos_inner - lt.cos_outer, 1e-4);\n"
    "      let cone = clamp((cd - lt.cos_outer) / span, 0.0, 1.0);\n"
    "      atten = atten * cone * cone;\n"
    "    }\n"
    "    if (atten <= 0.0) {\n"
    "      continue;\n"
    "    }\n"
    "    let vis = visible(origin, own, lt.position, 0.0);\n"
    "    let value = lt.color * lt.intensity * atten * vis;\n"
    "    let basis = vec4(SH_Y0, SH_Y1 * dir.y, SH_Y1 * dir.z, SH_Y1 * dir.x);\n"
    "    sr = sr + value.r * basis;\n"
    "    sg = sg + value.g * basis;\n"
    "    sb = sb + value.b * basis;\n"
    "  }\n"
    "  for (var i = 0u; i < u.emitter_count; i = i + 1u) {\n"
    "    let em = emitters[i];\n"
    "    let delta = em.position - origin;\n"
    "    let dist = length(delta);\n"
    "    let range = max(em.range, 1e-4);\n"
    "    if (dist >= range) {\n"
    "      continue;\n"
    "    }\n"
    "    let dir = delta / max(dist, 1e-4);\n"
    "    let radius = max(em.radius, 1e-4);\n"
    "    let reach = max(dist, radius);\n"
    "    let window = clamp(1.0 - pow(dist / range, 4.0), 0.0, 1.0);\n"
    "    let atten = window * window * radius * radius / (reach * reach);\n"
    "    if (atten <= 0.0) {\n"
    "      continue;\n"
    "    }\n"
    "    let vis = visible(origin, own, em.position, radius);\n"
    "    let value = em.color * atten * vis;\n"
    "    let basis = vec4(SH_Y0, SH_Y1 * dir.y, SH_Y1 * dir.z, SH_Y1 * dir.x);\n"
    "    sr = sr + value.r * basis;\n"
    "    sg = sg + value.g * basis;\n"
    "    sb = sb + value.b * basis;\n"
    "  }\n"
    "  probes[d.grid.x + job.index] = Probe(sr, sg, sb);\n"
    "}\n";

const char *box_downsample_wgsl =
    "@group(0) @binding(0) var src: texture_2d<f32>;\n"
    "@group(0) @binding(1) var samp: sampler;\n"
    "struct Out {\n"
    "  @builtin(position) pos: vec4<f32>,\n"
    "  @location(0) uv: vec2<f32>,\n"
    "};\n"
    "@vertex fn vs_main(@builtin(vertex_index) index: u32) -> Out {\n"
    "  let corner = vec2<f32>(f32((index << 1u) & 2u), f32(index & 2u));\n"
    "  var out: Out;\n"
    "  out.pos = vec4(corner * 2.0 - vec2(1.0), 0.0, 1.0);\n"
    "  out.uv = vec2(corner.x, 1.0 - corner.y);\n"
    "  return out;\n"
    "}\n"
    "@fragment fn fs_main(in: Out) -> @location(0) vec4<f32> {\n"
    "  return textureSampleLevel(src, samp, in.uv, 0.0);\n"
    "}\n";

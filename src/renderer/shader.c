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
    "  pad: vec2<f32>,\n" \
    "};\n"

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
    "@group(0) @binding(0) var<uniform> u: View;\n"
    "@group(1) @binding(0) var<storage, read> lights: array<Light>;\n"
    "@group(1) @binding(1) var<storage, read> clusters: array<vec2<u32>>;\n"
    "@group(1) @binding(2) var<storage, read> light_indices: array<u32>;\n"
    "@group(2) @binding(0) var env_irradiance: texture_cube<f32>;\n"
    "@group(2) @binding(1) var env_specular: texture_cube<f32>;\n"
    "@group(2) @binding(2) var env_brdf: texture_2d<f32>;\n"
    "@group(2) @binding(3) var env_sampler: sampler;\n"
    BOX_WGSL_CLUSTER_INDEX
    "struct VertexIn {\n"
    "  @location(0) pos: vec3<f32>,\n"
    "  @location(1) normal: vec3<f32>,\n"
    "  @location(2) uv: vec2<f32>,\n"
    "  @location(3) row0: vec4<f32>,\n"
    "  @location(4) row1: vec4<f32>,\n"
    "  @location(5) row2: vec4<f32>,\n"
    "  @location(6) color: vec4<f32>,\n"
    "  @location(7) emissive: vec4<f32>,\n"
    "  @location(8) params: vec4<f32>,\n"
    "};\n"
    "struct VertexOut {\n"
    "  @builtin(position) clip: vec4<f32>,\n"
    "  @location(0) world_pos: vec3<f32>,\n"
    "  @location(1) normal: vec3<f32>,\n"
    "  @location(2) uv: vec2<f32>,\n"
    "  @location(3) color: vec4<f32>,\n"
    "  @location(4) emissive: vec3<f32>,\n"
    "  @location(5) params: vec4<f32>,\n"
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
    "  out.emissive = in.emissive.rgb;\n"
    "  out.params = in.params;\n"
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
    "fn punctual(world_pos: vec3<f32>, n: vec3<f32>, v: vec3<f32>, albedo: vec3<f32>,\n"
    "    metallic: f32, roughness: f32) -> vec3<f32> {\n"
    "  var sum = vec3<f32>(0.0);\n"
    "  let vp = (u.view * vec4(world_pos, 1.0)).xyz;\n"
    "  let entry = clusters[cluster_index(vp)];\n"
    "  for (var i = 0u; i < entry.y; i = i + 1u) {\n"
    "    let lt = lights[light_indices[entry.x + i]];\n"
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
    "@fragment fn fs_main(in: VertexOut) -> @location(0) vec4<f32> {\n"
    "  let n = normalize(in.normal);\n"
    "  let l = normalize(-u.sun_dir);\n"
    "  let v = normalize(u.eye - in.world_pos);\n"
    "  let metallic = in.params.x;\n"
    "  let roughness = clamp(in.params.y, 0.04, 1.0);\n"
    "  let albedo = in.color.rgb;\n"
    "  let sun = u.sun_color * u.sun_intensity;\n"
    "  let env = environment(n, v, albedo, metallic, roughness);\n"
    "  var color = shade(n, v, l, albedo, metallic, roughness) * sun + env + in.emissive;\n"
    "  color = color + punctual(in.world_pos, n, v, albedo, metallic, roughness);\n"
    "  if (u.gamma_encode > 0.5) {\n"
    "    color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));\n"
    "  }\n"
    "  return vec4(color, in.color.a);\n"
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

/**
 * @file renderer/shader.c
 * @brief WGSL source of the lit instanced mesh shader and the cluster compute shader.
 */

#include "renderer_internal.h"

/** View uniform block, laid out as box_view_t */
#define BOX_WGSL_VIEW \
    "struct View {\n" \
    "  view: mat4x4<f32>,\n" \
    "  proj: mat4x4<f32>,\n" \
    "  eye: vec3<f32>,\n" \
    "  ambient: f32,\n" \
    "  sun_dir: vec3<f32>,\n" \
    "  sun_intensity: f32,\n" \
    "  sun_color: vec3<f32>,\n" \
    "  gamma_encode: f32,\n" \
    "  cluster_x: u32,\n" \
    "  cluster_y: u32,\n" \
    "  cluster_z: u32,\n" \
    "  cluster_max: u32,\n" \
    "  cluster_near: f32,\n" \
    "  cluster_far: f32,\n" \
    "  light_count: u32,\n" \
    "  pad: f32,\n" \
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
    "  let sky = albedo * u.ambient * (0.6 + 0.4 * n.y);\n"
    "  var color = shade(n, v, l, albedo, metallic, roughness) * sun + sky + in.emissive;\n"
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

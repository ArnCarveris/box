#include "include/light.wgsl"
#include "include/bake.wgsl"
#include "include/probe.wgsl"

struct SparseBake {
  job_count: u32,
  pad0: u32,
  pad1: u32,
  pad2: u32,
};

@group(0) @binding(0) var<uniform> u: SparseBake;
@group(0) @binding(1) var<storage, read> lights: array<Light>;
@group(0) @binding(2) var<storage, read> emitters: array<Emitter>;
@group(0) @binding(3) var<storage, read> bake_data: array<u32>;
@group(1) @binding(0) var<storage, read_write> probes: array<Probe>;
@group(1) @binding(1) var<storage, read> jobs: array<u32>;

#include "include/sparse_probe.wgsl"
#include "include/bake_visibility.wgsl"

@compute @workgroup_size(BAKE_GROUP)
fn cs_bake(@builtin(global_invocation_id) gid: vec3<u32>) {
  let job_id = gid.x + gid.y * 65535u * BAKE_GROUP;
  if (job_id >= u.job_count) {
    return;
  }
  let job = jobs[job_id];
  let chunk = job / 64u;
  let origin = bake_probe_position(chunk, job % 64u);
  if (!bake_visible(origin, origin)) {
    probes[job] = Probe(vec4(-1.0, 0.0, 0.0, 0.0), vec4(0.0), vec4(0.0));
    return;
  }
  let chunk_info = bake_chunk_offset(chunk);
  var sr = vec4<f32>(0.0);
  var sg = vec4<f32>(0.0);
  var sb = vec4<f32>(0.0);
  for (var i = 0u; i < bake_data[chunk_info + 5u]; i = i + 1u) {
    let lt = lights[bake_data[bake_data[chunk_info + 4u] + i]];
    let delta = lt.position - origin;
    let dist = length(delta);
    let range = max(lt.range, 1e-4);
    if (dist >= range) {
      continue;
    }
    let dir = delta / max(dist, 1e-4);
    let window = clamp(1.0 - pow(dist / range, 4.0), 0.0, 1.0);
    var atten = window * window / (dist * dist + 1.0);
    if (lt.kind == 1u) {
      let cd = dot(-dir, lt.direction);
      let span = max(lt.cos_inner - lt.cos_outer, 1e-4);
      let cone = clamp((cd - lt.cos_outer) / span, 0.0, 1.0);
      atten = atten * cone * cone;
    }
    if (atten <= 0.0) {
      continue;
    }
    let vis = select(0.0, 1.0, bake_visible(origin, lt.position));
    let value = lt.color * lt.intensity * atten * vis;
    let basis = vec4(SH_Y0, SH_Y1 * dir.y, SH_Y1 * dir.z, SH_Y1 * dir.x);
    sr = sr + value.r * basis;
    sg = sg + value.g * basis;
    sb = sb + value.b * basis;
  }
  for (var i = 0u; i < bake_data[chunk_info + 7u]; i = i + 1u) {
    let emitter_id = bake_data[bake_data[chunk_info + 6u] + i];
    let em = emitters[emitter_id];
    let delta = em.position - origin;
    let dist = length(delta);
    let range = max(em.range, 1e-4);
    if (dist >= range) {
      continue;
    }
    let dir = delta / max(dist, 1e-4);
    let radius = max(em.radius, 1e-4);
    let reach = max(dist, radius);
    let window = clamp(1.0 - pow(dist / range, 4.0), 0.0, 1.0);
    let atten = window * window * radius * radius / (reach * reach);
    if (atten <= 0.0) {
      continue;
    }
    let ignore = bake_data[bake_data[4] + emitter_id];
    let vis = select(0.0, 1.0, bake_visible_ignoring(origin, em.position, ignore));
    let value = em.color * atten * vis;
    let basis = vec4(SH_Y0, SH_Y1 * dir.y, SH_Y1 * dir.z, SH_Y1 * dir.x);
    sr = sr + value.r * basis;
    sg = sg + value.g * basis;
    sb = sb + value.b * basis;
  }
  probes[job] = Probe(sr, sg, sb);
}

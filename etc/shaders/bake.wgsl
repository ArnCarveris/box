#include "include/light.wgsl"
#include "include/bake.wgsl"
#include "include/probe.wgsl"

@group(0) @binding(0) var<uniform> u: Bake;
@group(0) @binding(1) var<storage, read> lights: array<Light>;
@group(0) @binding(2) var<storage, read> emitters: array<Emitter>;
@group(0) @binding(3) var<storage, read> occupancy: array<u32>;
@group(1) @binding(0) var<storage, read> descs: array<Desc>;
@group(1) @binding(1) var<uniform> info: Info;
@group(1) @binding(2) var<storage, read_write> probes: array<Probe>;
@group(1) @binding(3) var<storage, read> jobs: array<Job>;

fn solid(p: vec3<f32>) -> bool {
  let t = (p - u.grid_min.xyz) / u.grid_cell.xyz;
  let dim = vec3<f32>(u.grid_dim.xyz);
  if (any(t < vec3(0.0)) || any(t >= dim)) {
    return false;
  }
  let c = vec3<u32>(t);
  let index = c.x + c.y * u.grid_dim.x + c.z * u.grid_dim.x * u.grid_dim.y;
  return (occupancy[index >> 5u] & (1u << (index & 31u))) != 0u;
}

fn box_exit(extent: vec3<f32>, dir: vec3<f32>) -> f32 {
  var result = 0.0;
  let a = abs(dir);
  for (var i = 0u; i < 3u; i = i + 1u) {
    if (a[i] > 1e-4) {
      let t = extent[i] / a[i];
      if (result == 0.0 || t < result) {
        result = t;
      }
    }
  }
  return result;
}

fn visible(origin: vec3<f32>, own: vec3<f32>, dest: vec3<f32>, radius: f32) -> f32 {
  let delta = dest - origin;
  let dist = length(delta);
  if (dist < 1e-4 || u.steps == 0u) {
    return 1.0;
  }
  let dir = delta / dist;
  let margin = u.grid_cell.w;
  let t0 = box_exit(own, dir) + margin;
  let t1 = dist - radius - margin;
  if (t1 <= t0) {
    return 1.0;
  }
  let step = (t1 - t0) / f32(u.steps);
  for (var i = 0u; i < u.steps; i = i + 1u) {
    if (solid(origin + dir * (t0 + (f32(i) + 0.5) * step))) {
      return 0.0;
    }
  }
  return 1.0;
}

@compute @workgroup_size(BAKE_GROUP)
fn cs_bake(@builtin(global_invocation_id) gid: vec3<u32>) {
  if (gid.x >= info.job_count) {
    return;
  }
  let job = jobs[gid.x];
  let d = descs[job.slot];
  let origin = probe_position(d, job.index);
  let own = d.extent.xyz / vec3<f32>(probe_dim(d));
  var sr = vec4<f32>(0.0);
  var sg = vec4<f32>(0.0);
  var sb = vec4<f32>(0.0);
  for (var i = 0u; i < u.light_count; i = i + 1u) {
    let lt = lights[i];
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
    let vis = visible(origin, own, lt.position, 0.0);
    let value = lt.color * lt.intensity * atten * vis;
    let basis = vec4(SH_Y0, SH_Y1 * dir.y, SH_Y1 * dir.z, SH_Y1 * dir.x);
    sr = sr + value.r * basis;
    sg = sg + value.g * basis;
    sb = sb + value.b * basis;
  }
  for (var i = 0u; i < u.emitter_count; i = i + 1u) {
    let em = emitters[i];
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
    let vis = visible(origin, own, em.position, radius);
    let value = em.color * atten * vis;
    let basis = vec4(SH_Y0, SH_Y1 * dir.y, SH_Y1 * dir.z, SH_Y1 * dir.x);
    sr = sr + value.r * basis;
    sg = sg + value.g * basis;
    sb = sb + value.b * basis;
  }
  probes[d.grid.x + job.index] = Probe(sr, sg, sb);
}

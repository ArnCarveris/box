// Particle simulation. The integrate pass ages every live particle of a pool,
// moves it by its velocity and acceleration and pushes the slot of a particle
// that reaches its lifetime onto the dead list; the spawn pass pops as many of
// those slots as the packet asked for and fills them with a particle drawn from
// the shape, the ranges and the seed of the emitter.
//
// Every random number comes from a hash of the seed and the index of the
// particle in the stream of the emitter, so the particles of a frame are the
// same on every run and on every device. The slot a particle lands in is not,
// which only changes the order the sprites are blended in.
//
// The channels are one buffer each: the position carries the age in its w, the
// velocity the lifetime, the acceleration the rotation and the color the
// opacity. A slot with a lifetime of zero holds no particle.

struct Emitter {
  row0: vec4<f32>,
  row1: vec4<f32>,
  row2: vec4<f32>,
  color: vec4<f32>,
  acceleration: vec4<f32>,
  direction: vec4<f32>,
  extents: vec4<f32>,
  speed: vec2<f32>,
  lifetime: vec2<f32>,
  size: vec2<f32>,
  rotation: vec2<f32>,
  opacity: vec2<f32>,
  delta: f32,
  pad: f32,
  count: u32,
  spawn: u32,
  first: u32,
  shape: u32,
  seed: u32,
  points: u32,
  flags: u32,
  pad2: u32,
};

struct Point {
  position: vec4<f32>,
  normal: vec4<f32>,
};

@group(0) @binding(0) var<uniform> e: Emitter;
@group(0) @binding(1) var<storage, read_write> position: array<vec4<f32>>;
@group(0) @binding(2) var<storage, read_write> velocity: array<vec4<f32>>;
@group(0) @binding(3) var<storage, read_write> force: array<vec4<f32>>;
@group(0) @binding(4) var<storage, read_write> color: array<vec4<f32>>;
@group(0) @binding(5) var<storage, read_write> size: array<f32>;
@group(0) @binding(6) var<storage, read_write> dead: array<u32>;
@group(0) @binding(7) var<storage, read_write> counts: array<atomic<u32>>;
@group(0) @binding(8) var<storage, read> table: array<Point>;

const SHAPE_POINT: u32 = 0u;
const SHAPE_SPHERE: u32 = 1u;
const SHAPE_BOX: u32 = 2u;
const SHAPE_DISC: u32 = 3u;
const SHAPE_MESH: u32 = 4u;
const SHAPE_SPLINE: u32 = 5u;

const FREE: u32 = 0u;
const LIVE: u32 = 1u;

fn hash(value: u32) -> u32 {
  var x = value;
  x = x ^ (x >> 16u);
  x = x * 0x7feb352du;
  x = x ^ (x >> 15u);
  x = x * 0x846ca68bu;
  x = x ^ (x >> 16u);
  return x;
}

fn random(particle: u32, stream: u32) -> f32 {
  let h = hash(e.seed ^ hash(particle * 16u + stream));
  return f32(h) * (1.0 / 4294967296.0);
}

fn between(range: vec2<f32>, t: f32) -> f32 {
  return range.x + max(range.y - range.x, 0.0) * t;
}

fn to_world(local: vec3<f32>) -> vec3<f32> {
  let p = vec4(local, 1.0);
  return vec3(dot(e.row0, p), dot(e.row1, p), dot(e.row2, p));
}

// Unit direction inside the cone of the spread around an axis, drawn uniformly
// over the cap the cosine of the spread cuts out of the sphere.
fn cone(axis: vec3<f32>, cos_spread: f32, particle: u32) -> vec3<f32> {
  let c = mix(clamp(cos_spread, -1.0, 1.0), 1.0, random(particle, 4u));
  let s = sqrt(max(1.0 - c * c, 0.0));
  let phi = random(particle, 5u) * 6.2831853;
  var up = vec3(0.0, 1.0, 0.0);
  if (abs(axis.y) > 0.99) {
    up = vec3(1.0, 0.0, 0.0);
  }
  let t = normalize(cross(up, axis));
  let b = cross(axis, t);
  return normalize(t * (cos(phi) * s) + b * (sin(phi) * s) + axis * c);
}

fn rotate(local: vec3<f32>) -> vec3<f32> {
  let d = vec4(local, 0.0);
  let world = vec3(dot(e.row0, d), dot(e.row1, d), dot(e.row2, d));
  if (length(world) < 0.000001) {
    return vec3(0.0, 1.0, 0.0);
  }
  return normalize(world);
}

fn unit_vector(particle: u32, stream: u32) -> vec3<f32> {
  let z = random(particle, stream) * 2.0 - 1.0;
  let a = random(particle, stream + 1u) * 6.2831853;
  let r = sqrt(max(1.0 - z * z, 0.0));
  return vec3(r * cos(a), z, r * sin(a));
}

// Spawn point of a particle in the space of the emitter, with the direction the
// shape emits along in its w free axis.
fn spawn_point(particle: u32, axis: ptr<function, vec3<f32>>) -> vec3<f32> {
  var local = vec3(0.0);
  *axis = vec3(0.0, 1.0, 0.0);
  if (e.shape == SHAPE_SPHERE) {
    let dir = unit_vector(particle, 0u);
    let radius = e.extents.w * pow(random(particle, 2u), 1.0 / 3.0);
    local = dir * radius;
    *axis = dir;
  } else if (e.shape == SHAPE_BOX) {
    local = vec3(random(particle, 0u) * 2.0 - 1.0, random(particle, 1u) * 2.0 - 1.0,
      random(particle, 2u) * 2.0 - 1.0) * e.extents.xyz;
  } else if (e.shape == SHAPE_DISC) {
    let a = random(particle, 0u) * 6.2831853;
    let radius = e.extents.w * sqrt(random(particle, 1u));
    local = vec3(cos(a) * radius, 0.0, sin(a) * radius);
    *axis = normalize(vec3(local.x, 0.0001, local.z));
  } else if ((e.shape == SHAPE_MESH || e.shape == SHAPE_SPLINE) && e.points > 0u) {
    let index = min(u32(random(particle, 0u) * f32(e.points)), e.points - 1u);
    local = table[index].position.xyz;
    *axis = table[index].normal.xyz;
  }
  return local;
}

@compute @workgroup_size(PARTICLE_GROUP)
fn cs_integrate(@builtin(global_invocation_id) id: vec3<u32>) {
  let i = id.x;
  if (i >= e.count) {
    return;
  }
  let v = velocity[i];
  if (v.w <= 0.0) {
    return;
  }
  let p = position[i];
  let age = p.w + e.delta;
  if (age >= v.w) {
    velocity[i] = vec4(0.0);
    let slot = atomicAdd(&counts[FREE], 1u);
    if (slot < e.count) {
      dead[slot] = i;
    }
    atomicSub(&counts[LIVE], 1u);
    return;
  }
  let moved = v.xyz + force[i].xyz * e.delta;
  position[i] = vec4(p.xyz + moved * e.delta, age);
  velocity[i] = vec4(moved, v.w);
}

@compute @workgroup_size(PARTICLE_GROUP)
fn cs_spawn(@builtin(global_invocation_id) id: vec3<u32>) {
  let k = id.x;
  if (k >= e.spawn) {
    return;
  }
  let taken = atomicSub(&counts[FREE], 1u);
  if (taken == 0u || taken > e.count) {
    atomicAdd(&counts[FREE], 1u);
    return;
  }
  let i = dead[taken - 1u];
  let particle = e.first + k;

  var axis = vec3(0.0, 1.0, 0.0);
  let local = spawn_point(particle, &axis);
  var dir = e.direction.xyz;
  if (length(dir) < 0.000001) {
    dir = axis;
  }
  let world_dir = cone(rotate(dir), e.direction.w, particle);
  let speed = between(e.speed, random(particle, 6u));
  let life = between(e.lifetime, random(particle, 7u));

  position[i] = vec4(to_world(local), 0.0);
  velocity[i] = vec4(world_dir * speed, max(life, 0.0001));
  force[i] = vec4(e.acceleration.xyz, radians(between(e.rotation, random(particle, 8u))));
  color[i] = vec4(e.color.rgb, between(e.opacity, random(particle, 9u)));
  size[i] = between(e.size, random(particle, 10u));
  atomicAdd(&counts[LIVE], 1u);
}

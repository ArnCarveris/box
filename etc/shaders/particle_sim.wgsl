// Particle simulation. The integrate pass ages every live particle of a pool,
// moves it by its velocity and acceleration and pushes the slot of a particle
// that reaches its lifetime onto the dead list; the spawn pass pops as many of
// those slots as the packet asked for and fills them with a particle drawn from
// the shape, the ranges and the seed of the emitter.
//
// Every random number comes from a hash of the seed and the index of the
// particle in the stream of the emitter, so the particles of a frame are the
// same on every run and on every device. The slot a particle lands in is taken
// off the top of the dead list in spawn order by a pass of one thread, so it is
// the same on every run as well until a pool starts recycling the slots its
// dead particles hand back, which no order fixes.
//
// The channels keep the place of a particle and the velocity that integrates it
// in full precision, the position with the age in its w and the velocity with
// the lifetime, and pack what only reaches the image into half floats: the
// color with its opacity into two words and the rotation with the size into
// one. A slot with a lifetime of zero holds no particle.
//
// Nothing is dispatched over the slots a pool has never used: the packet counts
// the particles an emitter spawned since its pool was built and the integrate
// pass is dispatched over that many slots, so a pool that is filling costs the
// part of a pass it is using. An added pool also appends every slot it leaves
// alive to a compacted list that the finish pass writes the length of into the
// arguments its sprite pass is drawn from, so its draw expands the particles
// that are alive and nothing else.
//
// The integrate pass is a semi implicit Euler step with the drag solved
// exactly: the forces are added to the velocity, the drag pulls that velocity
// towards the wind over exp(-drag * delta), which is the closed form of the
// equation and is stable at any timestep, and the position follows the new
// velocity. src/particle/simulate.h is the C twin of the functions below, so a
// change here is a change in both places.
//
// The table carries the spawn points of the shape first and the colliders of
// the emitter after them, because this pass already binds the storage buffers a
// device guarantees.

#include "include/particle.wgsl"

@group(0) @binding(0) var<uniform> e: Emitter;
@group(0) @binding(1) var<storage, read_write> position: array<vec4<f32>>;
@group(0) @binding(2) var<storage, read_write> velocity: array<vec4<f32>>;
@group(0) @binding(3) var<storage, read_write> color: array<vec2<u32>>;
@group(0) @binding(4) var<storage, read_write> extra: array<u32>;
@group(0) @binding(5) var<storage, read_write> slots: array<u32>;
@group(0) @binding(6) var<storage, read_write> counts: array<atomic<u32>>;
@group(0) @binding(7) var<storage, read> table: array<Point>;
@group(1) @binding(0) var<storage, read_write> args: array<u32>;

const ALIVE_LIST: u32 = 0u;

const SHAPE_POINT: u32 = 0u;
const SHAPE_SPHERE: u32 = 1u;
const SHAPE_BOX: u32 = 2u;
const SHAPE_DISC: u32 = 3u;
const SHAPE_MESH: u32 = 4u;
const SHAPE_SPLINE: u32 = 5u;

const COLLIDER_SPHERE: f32 = 0.0;

const TURBULENCE_SCALE: f32 = 0.35;
const TURBULENCE_SEEDS: u32 = 1024u;

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

// Empty the slot of a particle and hand it back to the dead list.
fn kill(i: u32) {
  velocity[i] = vec4(0.0);
  let slot = atomicAdd(&counts[FREE], 1u);
  if (slot < e.count) {
    slots[slot] = i;
  }
  atomicSub(&counts[LIVE], 1u);
}

// The compacted list of an added pool is appended to one workgroup at a time:
// the workgroup counts in its own memory what it has to write, takes that many
// entries off the global cursor with a single atomic and writes them side by
// side. Ten million particles would otherwise be ten million atomic adds on one
// word, which is most of what the pass would cost. A blended pool is drawn in
// slot order and never reads the list, so it pays none of this.
var<workgroup> wg_alive: atomic<u32>;
var<workgroup> wg_base: u32;

// Turbulence of the field of the emitter at a point, three sines of the point
// so that a particle passing the same place is always pushed the same way.
fn wander(p: vec3<f32>) -> vec3<f32> {
  let amount = e.wind.w;
  if (amount <= 0.0) {
    return vec3(0.0);
  }
  let q = p * TURBULENCE_SCALE + vec3(f32(e.seed % TURBULENCE_SEEDS) * 0.37);
  return vec3(sin(q.y * 1.3 + q.z * 0.7), sin(q.z * 1.1 + q.x * 0.9),
    sin(q.x * 1.7 + q.y * 0.5)) * amount;
}

// Velocity after one step of the forces: the acceleration of the emitter and
// its turbulence first, then the drag pulling it towards the wind.
fn accelerate(v: vec3<f32>, turbulence: vec3<f32>) -> vec3<f32> {
  var out = v + (e.acceleration.xyz + turbulence) * e.delta;
  let drag = e.acceleration.w;
  if (drag > 0.0) {
    out = e.wind.xyz + (out - e.wind.xyz) * exp(-drag * e.delta);
  }
  return out;
}

// Lift a particle back onto the surface it ran into and turn its velocity
// around, keeping the bounce of the speed into it and losing the friction of
// the speed along it.
fn respond(n: vec3<f32>, depth: f32, p: ptr<function, vec3<f32>>,
  v: ptr<function, vec3<f32>>)
{
  *p = *p + n * depth;
  var into = dot(*v, n);
  let along = *v - n * into;
  if (into < 0.0) {
    into = -into * e.collision.y;
  }
  *v = along * (1.0 - e.collision.z) + n * into;
}

// Test a particle against one collider of the table and answer it.
fn collider(k: u32, p: ptr<function, vec3<f32>>, v: ptr<function, vec3<f32>>) -> bool {
  let c = table[e.points + k];
  let d = *p - c.position.xyz;
  var hit = false;
  if (c.position.w == COLLIDER_SPHERE) {
    let distance = length(d);
    if (distance < c.normal.x) {
      let n = select(vec3(0.0, 1.0, 0.0), d / max(distance, 0.000001),
        distance > 0.000001);
      respond(n, c.normal.x - distance, p, v);
      hit = true;
    }
  } else {
    let out = c.normal.xyz - abs(d);
    if (out.x > 0.0 && out.y > 0.0 && out.z > 0.0) {
      var n = vec3(select(-1.0, 1.0, d.x >= 0.0), 0.0, 0.0);
      var depth = out.x;
      if (out.y < depth) {
        n = vec3(0.0, select(-1.0, 1.0, d.y >= 0.0), 0.0);
        depth = out.y;
      }
      if (out.z < depth) {
        n = vec3(0.0, 0.0, select(-1.0, 1.0, d.z >= 0.0));
        depth = out.z;
      }
      respond(n, depth, p, v);
      hit = true;
    }
  }
  return hit;
}

// Answer the ground plane and every collider of the emitter.
fn resolve(p: ptr<function, vec3<f32>>, v: ptr<function, vec3<f32>>) -> bool {
  var hit = false;
  if ((e.flags & GROUND) != 0u && (*p).y < e.collision.x) {
    respond(vec3(0.0, 1.0, 0.0), e.collision.x - (*p).y, p, v);
    hit = true;
  }
  for (var k = 0u; k < e.colliders; k = k + 1u) {
    if (collider(k, p, v)) {
      hit = true;
    }
  }
  return hit;
}

// Write the instances both draws of an added pool take: the length of the
// compacted list, which holds exactly the particles that are alive and in
// whatever order the frame appended them, because adding is commutative. A
// blended pool never runs this pass. It is drawn over its slots in slot order,
// with as many instances as the pool has ever used, because the order two
// sprites blend in has to be the same from frame to frame and the order a
// compaction produces is not; the slots that hold nothing are clipped away by
// the vertex stage, as they always were.
@compute @workgroup_size(1)
fn cs_finish() {
  let alive = min(atomicLoad(&counts[ALIVE]), e.count);
  args[DRAW + 1u] = alive;
  args[INDEXED + 1u] = alive;
}

// Step one particle of the pool, or leave a free slot alone.
//
// @param i Slot of the particle
// @return true when the slot still holds a live particle after the step
fn step(i: u32) -> bool {
  var result = false;
  if (i >= e.count) {
    return result;
  }

  let v = velocity[i];
  if (v.w <= 0.0) {
    return result;
  }

  let p = position[i];
  let age = p.w + e.delta;
  if (age >= v.w) {
    kill(i);
    return result;
  }

  var moved = accelerate(v.xyz, wander(p.xyz));
  var placed = p.xyz + moved * e.delta;
  if (e.colliders > 0u || (e.flags & GROUND) != 0u) {
    if (resolve(&placed, &moved) && (e.flags & DIE) != 0u) {
      kill(i);
      return result;
    }
  }

  position[i] = vec4(placed, age);
  velocity[i] = vec4(moved, v.w);
  result = true;
  return result;
}

// Append the slots of a workgroup that hold a live particle to the compacted
// list of an added pool, with one atomic for the whole workgroup. Every thread
// reaches every barrier, which is what a workgroup barrier asks for.
fn compact(local: u32, i: u32, alive: bool) {
  let wanted = (e.flags & ADDITIVE) != 0u;
  if (local == 0u) {
    atomicStore(&wg_alive, 0u);
  }

  workgroupBarrier();
  var mine = 0u;
  if (wanted && alive) {
    mine = atomicAdd(&wg_alive, 1u);
  }

  workgroupBarrier();
  if (local == 0u && wanted) {
    wg_base = atomicAdd(&counts[ALIVE], atomicLoad(&wg_alive));
  }

  workgroupBarrier();
  if (wanted && alive && wg_base + mine < e.count) {
    slots[e.count + wg_base + mine] = i;
  }
}

@compute @workgroup_size(PARTICLE_GROUP)
fn cs_integrate(@builtin(global_invocation_id) id: vec3<u32>,
  @builtin(local_invocation_index) local: u32)
{
  let i = id.x;
  compact(local, i, step(i));
}

// Take the slots this frame spawns into off the top of the dead list in one
// step, so that the particle a frame spawns first always lands in the slot the
// list has on top instead of in whichever slot its thread won a race for. That
// is what makes a pool that has not recycled a slot yet draw the same image on
// every run, and it costs one pass of one thread.
@compute @workgroup_size(1)
fn cs_alloc() {
  let free = min(atomicLoad(&counts[FREE]), e.count);
  let take = min(e.spawn, free);
  atomicStore(&counts[BASE], free);
  atomicStore(&counts[TAKE], take);
  atomicStore(&counts[FREE], free - take);
  atomicAdd(&counts[LIVE], take);
}

// Fill one slot of the dead list with a new particle of the stream.
//
// @param k Index of the particle in the spawns of this frame
// @param slot Receives the slot the particle landed in
// @return true when the frame had a slot for it
fn spawn(k: u32, slot: ptr<function, u32>) -> bool {
  if (k >= atomicLoad(&counts[TAKE])) {
    return false;
  }

  let i = slots[atomicLoad(&counts[BASE]) - 1u - k];
  *slot = i;
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

  let tint = mix(e.color.rgb, e.color_max.rgb, random(particle, 11u));

  position[i] = vec4(to_world(local), 0.0);
  velocity[i] = vec4(world_dir * speed, max(life, 0.0001));
  extra[i] = pack2x16float(vec2(radians(between(e.rotation, random(particle, 8u))),
    between(e.size, random(particle, 10u))));
  color[i] = pack_color(vec4(tint, between(e.opacity, random(particle, 9u))));

  return true;
}

@compute @workgroup_size(PARTICLE_GROUP)
fn cs_spawn(@builtin(global_invocation_id) id: vec3<u32>,
  @builtin(local_invocation_index) local: u32)
{
  var slot = 0u;
  let filled = spawn(id.x, &slot);
  compact(local, slot, filled);
}

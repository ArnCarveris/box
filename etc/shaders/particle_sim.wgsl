// Particle simulation: one dispatch of each entry point per pool of the packet. integrate ages
// the used slots of the pool by the frame, frees the particles whose age reached their
// lifetime, moves the others under the acceleration, gravity, wander field and drag toward the
// wind, by the curves of their ParticleMotion at their age and with the drift of their frame of
// reference, collides them with the ground plane, and writes the indirect draw arguments of
// the pool. spawn then writes the spawned particles into the slots the extract picked. Each
// spawn draws its lifetime, position, direction, speed and radial speed from the seed and its
// spawn index, each from its own stream, and adds the velocity it inherits from the emitter;
// the C twin is box_particle_* in src/particle/simulate.h.
//
// An alpha blended pool that is drawn back to front is sorted for each view that draws it:
// sort_keys writes the key of every slot from its view depth, then a bitonic sort orders the
// keys ascending. sort_local sorts every run of SORT_GROUP keys in workgroup memory, or all the
// keys of a pool that sorts fewer, and for every larger run length k sort_global does the steps
// of k down to SORT_GROUP and sort_merge the rest in workgroup memory. Keys tie break on the
// slot, so a sort is the same every run.
#include "include/lights.wgsl"
#include "include/particle.wgsl"

@group(0) @binding(0) var<uniform> emitter: Emitter;
@group(0) @binding(1) var<storage, read_write> particles: array<Particle>;
@group(0) @binding(2) var<storage, read> slots: array<u32>;
@group(0) @binding(3) var<storage, read> table: array<TablePoint>;
@group(0) @binding(4) var<storage, read_write> args: array<u32>;
@group(0) @binding(5) var<storage, read_write> keys: array<vec2u>;
@group(0) @binding(6) var<uniform> view: View;
@group(0) @binding(7) var<uniform> step: SortStep;
// Rows of the curves of the emitters, which integrate reads for the curves of ParticleMotion
@group(0) @binding(8) var<storage, read> curves: array<f32>;

// Run length k and compare distance j of a step of the bitonic sort.
struct SortStep {
    k: u32,
    j: u32,
    pad0: u32,
    pad1: u32,
};

// Keys a workgroup sorts in workgroup memory, two per invocation
const SORT_GROUP: u32 = 512u;

var<workgroup> sorting: array<vec2u, 512>;

@compute @workgroup_size(256)
fn integrate(@builtin(global_invocation_id) id: vec3u) {
    let i = id.x;
    if (i == 0u) {
        let at = emitter.pool.w * 4u;
        args[at] = emitter.draw.x;
        args[at + 1u] = emitter.pool.x;
        args[at + 2u] = 0u;
        args[at + 3u] = 0u;
    }

    if (i >= emitter.pool.x) {
        return;
    }

    var p = particles[i];
    if (p.remaining == 0u) {
        return;
    }

    p.remaining = particle_age(p.remaining, emitter.life.x);
    if (p.remaining != 0u) {
        // Without motion the identity of particle_motion_none() moves the particle as before,
        // bit for bit; one integrate after the check keeps the compiled step the same, where a
        // second call in an else branch was seen to change it by a rounding
        var motion = particle_motion_none();
        if (emitter.gradient_max.y != 0u) {
            let seed = emitter.spawn.y;
            let life = particle_life(seed, p.index, emitter.life.y, emitter.life.z);
            motion = particle_motion(emitter.motion, seed, p.index,
                particle_life_age(life, p.remaining));
        }

        particle_integrate(&p, emitter.acceleration.xyz, emitter.wind, emitter.turbulence.xy,
            emitter.wander.xyz, emitter.drift, motion, emitter.acceleration.w);
        if (!particle_collide(&p, emitter.ground.xyz, emitter.collide)) {
            p.remaining = 0u;
        }
    }

    particles[i] = p;
}

@compute @workgroup_size(256)
fn spawn(@builtin(global_invocation_id) id: vec3u) {
    let k = id.x;
    if (k >= emitter.spawn.z) {
        return;
    }

    let slot = slots[emitter.pool.y + k];
    if (slot == NO_SLOT) {
        return;
    }

    let seed = emitter.spawn.y;
    let index = emitter.spawn.w + k;
    let u = vec3f(
        particle_unit(particle_hash(seed, index, STREAM_SHAPE)),
        particle_unit(particle_hash(seed, index, STREAM_SHAPE + 1u)),
        particle_unit(particle_hash(seed, index, STREAM_SHAPE + 2u)));
    var local = particle_shape(emitter.spawn.x, emitter.extents.w, emitter.extents.xyz, u);
    let points = emitter.pool.z;
    if (emitter.spawn.x == EMITTER_MESH && points != 0u) {
        local = table[particle_hash(seed, index, STREAM_POINT) % points].position.xyz;
    }

    let c = vec2f(
        particle_unit(particle_hash(seed, index, STREAM_DIRECTION)),
        particle_unit(particle_hash(seed, index, STREAM_DIRECTION + 1u)));
    let cone = particle_cone(emitter.direction.xyz, emitter.direction.w, c);
    let world = (emitter.model * vec4f(cone, 0.0)).xyz;
    let length2 = dot(world, world);
    let direction = select(vec3f(0.0), world * inverseSqrt(length2), length2 > 0.0);
    let speed = particle_draw(seed, index, STREAM_SPEED, emitter.speed_size.xy);
    let offset = (emitter.model * vec4f(local, 0.0)).xyz;
    let radial = particle_draw(seed, index, STREAM_RADIAL, emitter.opacity_radial.zw);

    var p: Particle;
    p.position = (emitter.model * vec4f(local, 1.0)).xyz;
    p.remaining = particle_life(seed, index, emitter.life.y, emitter.life.z);
    p.velocity = direction * speed + particle_radial(offset, radial) + emitter.inherit.xyz;
    p.index = index;
    particles[slot] = p;
}

// Write the sort key and slot of every key of a pool, free slots and slots past the used ones
// last.
@compute @workgroup_size(256)
fn sort_keys(@builtin(global_invocation_id) id: vec3u) {
    let i = id.x;
    if (i >= emitter.draw.z) {
        return;
    }

    var key = SORT_DEAD;
    if (i < emitter.pool.x) {
        let p = particles[i];
        if (p.remaining != 0u) {
            key = particle_sort_key(-(view.view * vec4f(p.position, 1.0)).z);
        }
    }

    keys[emitter.life.w + i] = vec2u(key, i);
}

// Whether a key goes before another, by key and then by slot.
fn sort_before(a: vec2u, b: vec2u) -> bool {
    return a.x < b.x || (a.x == b.x && a.y < b.y);
}

// Compare and swap two keys of workgroup memory by the direction of the run they are in.
fn sort_shared_step(base: u32, t: u32, k: u32, j: u32) {
    let i = 2u * j * (t / j) + (t % j);
    let l = i + j;
    let a = sorting[i];
    let b = sorting[l];
    let ascending = ((base + i) & k) == 0u;
    if (sort_before(b, a) == ascending) {
        sorting[i] = b;
        sorting[l] = a;
    }
}

// Sort every run of SORT_GROUP keys, ascending and descending in turn, or the keys of a pool
// that sorts fewer, a power of two, ascending.
@compute @workgroup_size(256)
fn sort_local(@builtin(workgroup_id) group: vec3u, @builtin(local_invocation_id) local: vec3u) {
    let base = group.x * SORT_GROUP;
    let first = emitter.life.w + base;
    let n = min(emitter.draw.z, SORT_GROUP);
    let t = local.x;
    if (t < n) {
        sorting[t] = keys[first + t];
    }
    if (t + 256u < n) {
        sorting[t + 256u] = keys[first + t + 256u];
    }
    workgroupBarrier();
    for (var k = 2u; k <= n; k = k * 2u) {
        for (var j = k / 2u; j > 0u; j = j / 2u) {
            if (t < n / 2u) {
                sort_shared_step(base, t, k, j);
            }
            workgroupBarrier();
        }
    }

    if (t < n) {
        keys[first + t] = sorting[t];
    }
    if (t + 256u < n) {
        keys[first + t + 256u] = sorting[t + 256u];
    }
}

// The steps of a run length below SORT_GROUP, in workgroup memory.
@compute @workgroup_size(256)
fn sort_merge(@builtin(workgroup_id) group: vec3u, @builtin(local_invocation_id) local: vec3u) {
    let base = group.x * SORT_GROUP;
    let first = emitter.life.w + base;
    let t = local.x;
    sorting[t] = keys[first + t];
    sorting[t + 256u] = keys[first + t + 256u];
    workgroupBarrier();
    for (var j = SORT_GROUP / 2u; j > 0u; j = j / 2u) {
        sort_shared_step(base, t, step.k, j);
        workgroupBarrier();
    }

    keys[first + t] = sorting[t];
    keys[first + t + 256u] = sorting[t + 256u];
}

// One step of compare distance SORT_GROUP or more, over the keys of the pool.
@compute @workgroup_size(256)
fn sort_global(@builtin(global_invocation_id) id: vec3u) {
    let t = id.x;
    let j = step.j;
    let i = 2u * j * (t / j) + (t % j);
    let l = i + j;
    if (l >= emitter.draw.z) {
        return;
    }

    let first = emitter.life.w;
    let a = keys[first + i];
    let b = keys[first + l];
    let ascending = (i & step.k) == 0u;
    if (sort_before(b, a) == ascending) {
        keys[first + i] = b;
        keys[first + l] = a;
    }
}

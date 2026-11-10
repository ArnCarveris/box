// Particle simulation: one dispatch of each entry point over every pool of the packet, every
// workgroup taking the pool and the chunk of it its work item names. integrate ages the used
// slots of the pool by the frame, frees the particles whose age reached their lifetime, moves
// the others under the acceleration, gravity, wander field and drag toward the wind, by the
// curves of their ParticleMotion at their age and with the drift of their frame of reference,
// and collides them with the ground plane. spawn then writes the spawned particles into the
// slots the extract picked. Each spawn draws its lifetime, position, direction, speed and radial
// speed from the seed and its spawn index, each from its own stream, and adds the velocity it
// inherits from the emitter; the C twin is box_particle_* in src/particle/simulate.h.
#include "include/lights.wgsl"
#include "include/particle.wgsl"

// The blocks of the pools of the packet, BOX_GPU_PARTICLE_STRIDE apart
struct EmitterBlock {
    @size(512) emitter: Emitter,
};

// The first work item of the batch of a dispatch
struct Batch {
    first: u32,
    pad0: u32,
    pad1: u32,
    pad2: u32,
};

@group(0) @binding(0) var<storage, read> blocks: array<EmitterBlock>;
// The particle arena, the slots of a pool from emitter.gradient_max.z
@group(0) @binding(1) var<storage, read_write> particles: array<Particle>;
@group(0) @binding(2) var<storage, read> slots: array<u32>;
// The mesh tables, the points of a pool from emitter.pool.w
@group(0) @binding(3) var<storage, read> table: array<TablePoint>;
// Rows of the curves of the emitters, which integrate reads for the curves of ParticleMotion
@group(0) @binding(8) var<storage, read> curves: array<f32>;
// The pool and the chunk of it of every workgroup of the dispatches of the packet
@group(0) @binding(9) var<storage, read> work: array<vec2u>;
@group(0) @binding(10) var<uniform> batch: Batch;

@compute @workgroup_size(256)
fn integrate(@builtin(workgroup_id) group: vec3u, @builtin(local_invocation_id) local: vec3u) {
    let item = work[batch.first + group.x];
    let emitter = blocks[item.x].emitter;
    let i = item.y * 256u + local.x;
    if (i >= emitter.pool.x) {
        return;
    }

    var p = particles[emitter.gradient_max.z + i];
    if (p.remaining == 0u) {
        return;
    }

    p.remaining = particle_age(p.remaining, emitter.life.x);
    if (p.remaining != 0u) {
        // Keep one integrate after the check: a second call in an else branch can change the
        // compiled step by a rounding.
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

    particles[emitter.gradient_max.z + i] = p;
}

@compute @workgroup_size(256)
fn spawn(@builtin(workgroup_id) group: vec3u, @builtin(local_invocation_id) lane: vec3u) {
    let item = work[batch.first + group.x];
    let emitter = blocks[item.x].emitter;
    let k = item.y * 256u + lane.x;
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
        let point = emitter.pool.w + particle_hash(seed, index, STREAM_POINT) % points;
        local = table[point].position.xyz;
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
    particles[emitter.gradient_max.z + slot] = p;
}

// Particle simulation: one dispatch of each entry point per pool of the packet. integrate ages
// the used slots of the pool by the frame, frees the particles whose age reached their
// lifetime, moves the others under the acceleration, gravity, wander field and drag toward the
// wind and collides them with the ground plane, and writes the indirect draw arguments of the
// pool. spawn then writes the spawned particles into the slots the extract picked. Each spawn
// draws its lifetime, position, direction and speed from the seed and its spawn index, each
// from its own stream, and adds the velocity it inherits from the emitter; the C twin is
// box_particle_* in src/particle/simulate.h.
#include "include/particle.wgsl"

@group(0) @binding(0) var<uniform> emitter: Emitter;
@group(0) @binding(1) var<storage, read_write> particles: array<Particle>;
@group(0) @binding(2) var<storage, read> slots: array<u32>;
@group(0) @binding(3) var<storage, read> table: array<TablePoint>;
@group(0) @binding(4) var<storage, read_write> args: array<u32>;

@compute @workgroup_size(256)
fn integrate(@builtin(global_invocation_id) id: vec3u) {
    let i = id.x;
    if (i == 0u) {
        let at = emitter.pool.w * 4u;
        args[at] = 6u;
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
        particle_integrate(&p, emitter.acceleration.xyz, emitter.wind, emitter.turbulence.xy,
            emitter.wander.xyz, emitter.acceleration.w);
        if (!particle_collide(&p, emitter.ground.xyz, emitter.collide.x)) {
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

    var p: Particle;
    p.position = (emitter.model * vec4f(local, 1.0)).xyz;
    p.remaining = particle_life(seed, index, emitter.life.y, emitter.life.z);
    p.velocity = direction * speed + emitter.inherit.xyz;
    p.index = index;
    particles[slot] = p;
}

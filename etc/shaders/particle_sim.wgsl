// Particle simulation: one dispatch of each entry point over every pool of the packet, every
// workgroup taking the pool and the chunk of it its work item names. integrate ages the used
// slots of the pool by the frame, frees the particles whose age reached their lifetime, moves
// the others under the acceleration, gravity, wander field and drag toward the wind, by the
// curves of their ParticleMotion at their age and with the drift of their frame of reference,
// and collides them with the ground plane. spawn then writes the spawned particles into the
// slots the extract picked. Each spawn draws its lifetime, position, direction, speed and radial
// speed from the seed and its spawn index, each from its own stream, and adds the velocity it
// inherits from the emitter; the C twin is box_particle_* in src/particle/simulate.h. A local
// emitter spawns, moves and keeps its particles in its space, under its world forces turned
// into that space. A ParticleCard pool keeps a card word per particle after its slots, which
// integrate sets once the card comes to rest on the ground plane and spawn clears. A pool with
// ParticleForces.global_wind adds the velocity of the Wind at each particle to its wind, with
// the gust read from the field the wind sway of the meshes reads (wind_gust.wgsl).
#include "include/lights.wgsl"
#include "include/particle.wgsl"
#include "include/wind_gust.wgsl"

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

// Write the card word of the particle in slot i of a ParticleCard pool, as particle_card_word
// reads it. Every invocation writes a member of its own, so neighbours share the slot safely.
fn particle_card_store(base: u32, cards: u32, i: u32, word: u32) {
    let k = base + cards + i / CARD_WORDS;
    switch (i % CARD_WORDS) {
        case 0u: { particles[k].position.x = bitcast<f32>(word); }
        case 1u: { particles[k].position.y = bitcast<f32>(word); }
        case 2u: { particles[k].position.z = bitcast<f32>(word); }
        case 3u: { particles[k].remaining = word; }
        case 4u: { particles[k].velocity.x = bitcast<f32>(word); }
        case 5u: { particles[k].velocity.y = bitcast<f32>(word); }
        case 6u: { particles[k].velocity.z = bitcast<f32>(word); }
        default: { particles[k].index = word; }
    }
}

// Have a card in the air that came to rest on the ground plane after its step remember the age
// it came to rest at, 1 plus its microseconds, so the draw stops turning it and lays it flat.
fn card_rest(emitter: Emitter, i: u32, p: Particle) {
    let base = emitter.gradient_max.z;
    if (particle_card_word(base, emitter.life.w, i) != 0u) {
        return;
    }

    var position = p.position;
    var velocity = p.velocity;
    if (emitter.gradient_max.w != 0u) {
        position = (emitter.model * vec4f(p.position, 1.0)).xyz;
        velocity = (emitter.model * vec4f(p.velocity, 0.0)).xyz;
    }

    if (particle_card_rests(emitter.collide, position.y, length(velocity), emitter.ground.x,
        emitter.turbulence.z))
    {
        let life = particle_life(emitter.spawn.y, p.index, emitter.life.y, emitter.life.z);
        particle_card_store(base, emitter.life.w, i, life - p.remaining + 1u);
    }
}

// World velocity the Wind pulls a particle at a world position toward, the share of the
// emitter: its velocity along X and Z times the gust factor there, as box_particle_global_wind.
fn particle_global_wind(emitter: Emitter, world: vec3f) -> vec3f {
    let g = emitter.global_wind;
    let offset = vec2f(emitter.wander.w, emitter.inherit.w);
    let speed = length(g.xy);
    let dir = select(vec2f(1.0, 0.0), g.xy / speed, speed > 0.0);
    let factor = wind_gust(world.xz, offset, dir, emitter.turbulence.w, g.z);
    return vec3f(g.x * factor, 0.0, g.y * factor);
}

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

    p.position += vec3f(emitter.color.w, emitter.variance.w, emitter.ground.w);
    p.remaining = particle_age(p.remaining, emitter.life.x);
    if (p.remaining != 0u) {
        // Keep one integrate after the check: a second call in an else branch can change the
        // compiled step by a rounding.
        var motion = particle_motion_none();
        if (emitter.gradient_max.y != 0u) {
            let seed = emitter.spawn.y;
            let life = particle_life(seed, p.index, emitter.life.y, emitter.life.z);
            motion = particle_motion_scaled(particle_motion(emitter.motion, seed, p.index,
                particle_life_age(life, p.remaining)), emitter.turbulence.z);
        }

        // A local emitter turns its world forces into its space and keeps the one integrate.
        var forces = LocalForces(emitter.acceleration.xyz, emitter.wind, emitter.turbulence.xy,
            emitter.drift);
        let local = emitter.gradient_max.w != 0u;
        // The Wind of a pool with global wind, at the world position of the particle; a uniform
        // branch that leaves the wind of every other pool as it is.
        if (emitter.turbulence.w != 0.0) {
            var world = p.position;
            if (local) {
                world = (emitter.model * vec4f(p.position, 1.0)).xyz;
            }

            forces.wind = vec4f(forces.wind.xyz + particle_global_wind(emitter, world),
                forces.wind.w);
        }

        var inverse = mat3x3f();
        if (local) {
            inverse = particle_inverse(emitter.model);
            particle_local(&p, emitter.model, inverse, &forces, &motion, emitter.wander.xyz,
                emitter.acceleration.w);
        }

        particle_integrate(&p, forces.acceleration, forces.wind, forces.turbulence,
            emitter.wander.xyz, forces.drift, motion, emitter.acceleration.w);
        var alive = true;
        if (local) {
            alive = particle_collide_local(&p, emitter.model, inverse, emitter.ground.xyz,
                emitter.collide);
        } else {
            alive = particle_collide(&p, emitter.ground.xyz, emitter.collide);
        }

        if (!alive) {
            p.remaining = 0u;
        } else if (emitter.life.w != 0u) {
            card_rest(emitter, i, p);
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
    if (emitter.gradient_max.w != 0u) {
        // A local emitter keeps its particles in its space; its inherit is zero
        p.position = local;
        p.velocity = particle_inverse(emitter.model) * p.velocity;
    }

    particles[emitter.gradient_max.z + slot] = p;
    if (emitter.life.w != 0u) {
        particle_card_store(emitter.gradient_max.z, emitter.life.w, slot, 0u);
    }
}

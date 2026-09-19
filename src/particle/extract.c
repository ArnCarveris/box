/**
 * @file particle/extract.c
 * @brief Writes the emitters of a world into the frame packet.
 *
 * One block per emitter carries its transform, its shape, the ranges a particle
 * starts between and how many particles the frame spawns. The count comes from
 * an accumulator on the fixed clock, so a frame of a fixed timestep always
 * spawns the same number of particles, and the pool of the renderer caps it.
 */

#include "particle_internal.h"
#include <math.h>

/** Degrees to radians, used instead of the possibly undefined M_PI */
#define BOX_PARTICLE_RADIANS (3.14159265358979323846f / 180.0f)

/** Shape constant of the packet that an EmitterShape names.
 *
 * @param shape The shape of the component
 * @return One of the BOX_PARTICLE shape constants
 */
static uint32_t box_particle_shape(
    EmitterShape shape)
{
    uint32_t result = BOX_PARTICLE_POINT;
    switch (shape) {
    case EmitterSphere: result = BOX_PARTICLE_SPHERE; break;
    case EmitterBox: result = BOX_PARTICLE_BOX; break;
    case EmitterDisc: result = BOX_PARTICLE_DISC; break;
    case EmitterMesh: result = BOX_PARTICLE_MESH; break;
    case EmitterSpline: result = BOX_PARTICLE_SPLINE; break;
    default: break;
    }

    return result;
}

/** Particles the pool of an emitter holds, inside the ceiling of the renderer.
 *
 * @param emitter The emitter
 * @return The pool size
 */
static uint32_t box_particle_max(
    const ParticleEmitter *emitter)
{
    uint32_t result = 0;
    if (emitter->max > 0) {
        result = (uint32_t)emitter->max;
    }

    if (result > BOX_PARTICLE_MAX) {
        result = BOX_PARTICLE_MAX;
    }

    return result;
}

/** Particles the frame spawns, from the rate, the clock and the pool size.
 *
 * The accumulator keeps the fraction the rate did not fill, so a rate that is
 * not a multiple of the frame rate still spawns its particles per second. More
 * particles than the pool holds are dropped here; the dead list of the pool is
 * what enforces the maximum exactly.
 *
 * @param emitter The emitter
 * @param impl Its bookkeeping
 * @param delta Seconds the frame advanced the world
 * @param max Particles the pool holds
 * @return The number of particles to spawn
 */
static uint32_t box_particle_spawn(
    const ParticleEmitter *emitter,
    ParticleEmitterImpl *impl,
    float delta,
    uint32_t max)
{
    uint32_t result = 0;
    if (emitter->rate <= 0.0f || delta <= 0.0f || !max) {
        impl->accumulator = 0;
        goto done;
    }

    impl->accumulator += (double)emitter->rate * (double)delta;
    if (impl->accumulator >= (double)max) {
        impl->accumulator = (double)max;
    }

    result = (uint32_t)impl->accumulator;
    impl->accumulator -= (double)result;
done:
    return result;
}

/** Copy the world transform of an emitter into the rows of its block.
 *
 * @param m The transform
 * @param out Receives the three rows
 */
static void box_particle_transform(
    const mat4 *m,
    float *out)
{
    int32_t row;
    for (row = 0; row < 3; row ++) {
        out[row * 4 + 0] = m->v[0 + row];
        out[row * 4 + 1] = m->v[4 + row];
        out[row * 4 + 2] = m->v[8 + row];
        out[row * 4 + 3] = m->v[12 + row];
    }
}

/** Write the ranges, the color and the shape parameters of an emitter.
 *
 * @param emitter The emitter
 * @param out The block of the packet
 */
static void box_particle_params(
    const ParticleEmitter *emitter,
    box_particle_emitter_t *out)
{
    vec3 dir = emitter->direction;
    if (vec3_length(dir) > 0.0f) {
        dir = vec3_normalize(dir);
    } else {
        dir = vec3_new(0, 0, 0);
    }

    float spread = emitter->spread;
    if (spread < 0.0f) {
        spread = 0.0f;
    }

    if (spread > 180.0f) {
        spread = 180.0f;
    }

    out->color[0] = box_srgb_to_linear(emitter->color.r);
    out->color[1] = box_srgb_to_linear(emitter->color.g);
    out->color[2] = box_srgb_to_linear(emitter->color.b);
    out->color[3] = 1.0f;
    out->acceleration[0] = emitter->acceleration.x;
    out->acceleration[1] = emitter->acceleration.y;
    out->acceleration[2] = emitter->acceleration.z;
    out->direction[0] = dir.x;
    out->direction[1] = dir.y;
    out->direction[2] = dir.z;
    out->direction[3] = cosf(spread * BOX_PARTICLE_RADIANS);
    out->extents[0] = emitter->extents.x;
    out->extents[1] = emitter->extents.y;
    out->extents[2] = emitter->extents.z;
    out->extents[3] = emitter->radius;
    out->speed[0] = emitter->speed.min;
    out->speed[1] = emitter->speed.max;
    out->lifetime[0] = emitter->lifetime.min;
    out->lifetime[1] = emitter->lifetime.max;
    out->size[0] = emitter->size.min;
    out->size[1] = emitter->size.max;
    out->rotation[0] = emitter->rotation.min;
    out->rotation[1] = emitter->rotation.max;
    out->opacity[0] = emitter->opacity.min;
    out->opacity[1] = emitter->opacity.max;
    out->shape = box_particle_shape(emitter->shape);
    out->seed = emitter->seed;
}

/** Copy the sampled table of an emitter into the points of the packet.
 *
 * @param impl The bookkeeping holding the table
 * @param p The packet
 * @param out The block of the packet
 */
static void box_particle_table(
    const ParticleEmitterImpl *impl,
    box_packet_t *p,
    box_particle_emitter_t *out)
{
    out->point_first = p->particle_points.count;
    out->point_count = impl->points;
    uint32_t i;
    for (i = 0; i < impl->points; i ++) {
        box_particle_point_t *point = box_vec_push(&p->particle_points);
        *point = impl->table[i];
    }
}

/** Handles of the two shaders of the module, once the renderer assigned them.
 *
 * @param world The world
 * @param state The registry
 * @param p The packet
 */
static void box_particle_shaders(
    ecs_world_t *world,
    const ParticleState *state,
    box_packet_t *p)
{
    const ShaderImpl *sim = state->sim_shader ?
        ecs_get(world, state->sim_shader, ShaderImpl) : NULL;
    const ShaderImpl *draw = state->draw_shader ?
        ecs_get(world, state->draw_shader, ShaderImpl) : NULL;
    if (sim && sim->assigned) {
        p->particle_sim_shader = sim->handle;
    }

    if (draw && draw->assigned) {
        p->particle_draw_shader = draw->handle;
    }
}

void box_particle_extract(
    ecs_world_t *world,
    box_packet_t *p)
{
    ecs_entity_t singleton = ecs_lookup(world, "box.particle.ParticleState");
    if (!singleton) {
        goto done;
    }

    const ParticleState *state = ecs_get_id(world, singleton, singleton);
    if (!state || !state->emitters) {
        goto done;
    }

    box_particle_shaders(world, state, p);
    float delta = ecs_get_world_info(world)->delta_time;
    ecs_iter_t it = ecs_query_iter(world, state->emitters);
    while (ecs_query_next(&it)) {
        const ParticleEmitter *emitter = ecs_field(&it, ParticleEmitter, 0);
        const WorldTransform *transform = ecs_field(&it, WorldTransform, 1);
        ParticleEmitterImpl *impl = ecs_field(&it, ParticleEmitterImpl, 2);
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            uint32_t max = box_particle_max(&emitter[i]);
            if (!impl[i].id) {
                impl[i].id = (uint32_t)it.entities[i];
            }

            box_particle_sample(world, &emitter[i], &impl[i]);
            box_particle_emitter_t *out = box_vec_push(&p->particles);
            memset(out, 0, sizeof(box_particle_emitter_t));
            box_particle_transform(&transform[i].m, out->transform);
            box_particle_params(&emitter[i], out);
            box_particle_table(&impl[i], p, out);
            out->id = impl[i].id;
            out->max = max;
            out->delta = delta;
            out->first = impl[i].spawned;
            out->spawn = box_particle_spawn(&emitter[i], &impl[i], delta, max);
            if (impl[i].max != max) {
                out->flags |= BOX_PARTICLE_RESET;
                impl[i].max = max;
            }

            impl[i].spawned += out->spawn;
        }
    }

done:
    return;
}

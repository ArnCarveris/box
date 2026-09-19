/**
 * @file particle/extract.c
 * @brief Writes the emitters of a world into the frame packet.
 *
 * One block per emitter carries its transform, its shape, the ranges a particle
 * starts between and how many particles the frame spawns. The count comes from
 * an accumulator on the fixed clock, so a frame of a fixed timestep always
 * spawns the same number of particles, and the pool of the renderer caps it.
 *
 * The block also carries the forces of the emitter, which are the sum of the
 * ParticleForces of the world, of the entity the emitter names and of the one
 * it carries itself, and what its particles collide with: the ground plane and
 * the colliders the collision index holds within the reach of the emitter,
 * written into the table of the block after its spawn points.
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
 * The bursts that went off this frame are added to what the rate asked for and
 * the two are capped together, so a burst never spawns past the pool either. An
 * emitter that is paused or stopped asks for nothing and keeps the fraction it
 * had, so playing it again carries on where it left off.
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
    if (impl->state != ParticlePlaying || !max) {
        goto done;
    }

    if (emitter->rate <= 0.0f || delta <= 0.0f) {
        impl->accumulator = 0;
    } else {
        impl->accumulator += (double)emitter->rate * (double)delta;
        if (impl->accumulator >= (double)max) {
            impl->accumulator = (double)max;
        }

        result = (uint32_t)impl->accumulator;
        impl->accumulator -= (double)result;
    }

    if (impl->burst) {
        result += impl->burst;
    }

    if (result > max) {
        result = max;
    }

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

/** Hold a channel of a display encoded color inside its byte range.
 *
 * @param value The channel
 * @return The channel as a byte
 */
static uint8_t box_particle_channel(
    int32_t value)
{
    int32_t result = value;
    if (result < 0) {
        result = 0;
    }

    if (result > 255) {
        result = 255;
    }

    return (uint8_t)result;
}

/** Write the two linear ends of the color range of an emitter.
 *
 * The variance is a per channel amount around the color of the emitter, so an
 * emitter that names none writes the same color twice and every particle of it
 * comes out in that color.
 *
 * @param emitter The emitter
 * @param out The block of the packet
 */
static void box_particle_color(
    const ParticleEmitter *emitter,
    box_particle_emitter_t *out)
{
    uint8_t color[3] = { emitter->color.r, emitter->color.g, emitter->color.b };
    uint8_t spread[3] = { emitter->color_variance.r, emitter->color_variance.g,
        emitter->color_variance.b };
    int32_t i;
    for (i = 0; i < 3; i ++) {
        out->color[i] = box_srgb_to_linear(
            box_particle_channel((int32_t)color[i] - (int32_t)spread[i]));
        out->color_max[i] = box_srgb_to_linear(
            box_particle_channel((int32_t)color[i] + (int32_t)spread[i]));
    }

    out->color[3] = 1.0f;
    out->color_max[3] = 1.0f;
}

/** Add one ParticleForces to the forces an emitter resolved so far.
 *
 * @param from The component, NULL when the source has none
 * @param out The sum
 */
static void box_particle_add(
    const ParticleForces *from,
    ParticleForces *out)
{
    if (from) {
        out->gravity = vec3_add(out->gravity, from->gravity);
        out->wind = vec3_add(out->wind, from->wind);
        out->drag += from->drag;
        out->turbulence += from->turbulence;
    }
}

/** Write the forces of an emitter into its block.
 *
 * The forces of the world reach every emitter, the forces of the entity the
 * emitter names reach the emitters that share it and the ones it carries itself
 * reach only its own particles; the three are added, so a group under one wind
 * can still be given a drag of its own.
 *
 * @param world The world
 * @param emitter The emitter
 * @param own The ParticleForces of the emitter entity, NULL when it has none
 * @param out The block of the packet
 */
static void box_particle_forces(
    ecs_world_t *world,
    const ParticleEmitter *emitter,
    const ParticleForces *own,
    box_particle_emitter_t *out)
{
    ParticleForces sum;
    memset(&sum, 0, sizeof(ParticleForces));
    box_particle_add(ecs_singleton_get(world, ParticleForces), &sum);
    if (emitter->forces && ecs_is_alive(world, emitter->forces)) {
        box_particle_add(ecs_get(world, emitter->forces, ParticleForces), &sum);
    }

    box_particle_add(own, &sum);
    out->acceleration[0] = emitter->acceleration.x + sum.gravity.x;
    out->acceleration[1] = emitter->acceleration.y + sum.gravity.y;
    out->acceleration[2] = emitter->acceleration.z + sum.gravity.z;
    out->acceleration[3] = sum.drag > 0.0f ? sum.drag : 0.0f;
    out->wind[0] = sum.wind.x;
    out->wind[1] = sum.wind.y;
    out->wind[2] = sum.wind.z;
    out->wind[3] = sum.turbulence > 0.0f ? sum.turbulence : 0.0f;
}

/** Write one collider of the world into the table of an emitter.
 *
 * A sphere keeps its radius, which it holds in every component of its half
 * extents, and every other shape is written as the box of its world bounds,
 * which is what keeps the test in the integrate pass a handful of instructions.
 *
 * @param collider The collider
 * @param out The entry of the table
 */
static void box_particle_collider(
    const Collider *collider,
    box_particle_point_t *out)
{
    vec3 center = collider->center;
    vec3 half = collider->half;
    out->position[3] = BOX_PARTICLE_COLLIDER_SPHERE;
    if (collider->kind != ColliderSphere) {
        vec3 min, max;
        box_collider_bounds(collider, &min, &max);
        center = vec3_scale(vec3_add(min, max), 0.5f);
        half = vec3_scale(vec3_sub(max, min), 0.5f);
        out->position[3] = BOX_PARTICLE_COLLIDER_BOX;
    }

    out->position[0] = center.x;
    out->position[1] = center.y;
    out->position[2] = center.z;
    out->normal[0] = half.x;
    out->normal[1] = half.y;
    out->normal[2] = half.z;
    out->normal[3] = 0.0f;
}

/** Write what the particles of an emitter collide with into its block.
 *
 * The colliders are taken from the collision index over the box the reach of
 * the emitter spans around it, capped at what the table holds, and the emitter
 * skips its own collider so a mesh that spawns particles is not hit by them.
 *
 * @param world The world
 * @param entity The emitter entity
 * @param collision Its ParticleCollision, NULL when it has none
 * @param origin World position of the emitter
 * @param p The packet
 * @param hits Vector the index query collects its entities in
 * @param out The block of the packet
 */
static void box_particle_collision(
    ecs_world_t *world,
    ecs_entity_t entity,
    const ParticleCollision *collision,
    vec3 origin,
    box_packet_t *p,
    ecs_vec_t *hits,
    box_particle_emitter_t *out)
{
    if (!collision) {
        goto done;
    }

    out->collision[0] = collision->height;
    out->collision[1] = collision->bounce;
    out->collision[2] = collision->friction;
    if (collision->ground) {
        out->flags |= BOX_PARTICLE_GROUND;
    }

    if (collision->die) {
        out->flags |= BOX_PARTICLE_DIE;
    }

    if (collision->reach <= 0.0f) {
        goto done;
    }

    vec3 reach = vec3_new(collision->reach, collision->reach, collision->reach);
    ecs_vec_clear(hits);
    box_collision_aabb(world, vec3_sub(origin, reach), vec3_add(origin, reach), hits);
    int32_t i, count = ecs_vec_count(hits);
    for (i = 0; i < count && out->collider_count < BOX_PARTICLE_COLLIDERS; i ++) {
        ecs_entity_t found = ecs_vec_get_t(hits, ecs_entity_t, i)[0];
        const Collider *collider = found == entity ?
            NULL : ecs_get(world, found, Collider);
        if (!collider || collider->kind == ColliderNone) {
            continue;
        }

        box_particle_collider(collider, box_vec_push(&p->particle_points));
        out->collider_count ++;
    }

done:
    return;
}

/** Write the four keys of a curve into the block.
 *
 * @param c The curve, NULL for a constant
 * @param value The constant of an emitter without the component
 * @param out Receives the four keys
 */
static void box_particle_keys(
    const ParticleCurve *c,
    float value,
    float *out)
{
    out[0] = c ? c->start : value;
    out[1] = c ? c->early : value;
    out[2] = c ? c->late : value;
    out[3] = c ? c->end : value;
}

/** Write one key of the gradient of an emitter as a linear color.
 *
 * @param color The key, NULL for white
 * @param out Receives the linear color
 */
static void box_particle_key_color(
    const rgb *color,
    float *out)
{
    out[0] = color ? box_srgb_to_linear(color->r) : 1.0f;
    out[1] = color ? box_srgb_to_linear(color->g) : 1.0f;
    out[2] = color ? box_srgb_to_linear(color->b) : 1.0f;
    out[3] = 1.0f;
}

/** Write the curves an emitter changes its particles over their life with.
 *
 * An emitter without the component writes the constants that leave a particle
 * as it was spawned, so the pass evaluates one expression either way.
 *
 * @param curves The curves, NULL when the emitter has none
 * @param out The block of the packet
 */
static void box_particle_curves(
    const ParticleCurves *curves,
    box_particle_emitter_t *out)
{
    box_particle_keys(curves ? &curves->size : NULL, 1.0f, out->size_curve);
    box_particle_keys(curves ? &curves->opacity : NULL, 1.0f, out->opacity_curve);
    box_particle_keys(curves ? &curves->rotation : NULL, 0.0f, out->turn_curve);
    box_particle_key_color(curves ? &curves->color.start : NULL, out->gradient[0]);
    box_particle_key_color(curves ? &curves->color.early : NULL, out->gradient[1]);
    box_particle_key_color(curves ? &curves->color.late : NULL, out->gradient[2]);
    box_particle_key_color(curves ? &curves->color.end : NULL, out->gradient[3]);
}

/** Texture handle an entity binds, through its own texture, an Image or PbrTextures.
 *
 * @param world The world
 * @param e The entity the appearance names, 0 for none
 * @return The texture handle, 0 when the entity binds none
 */
static uint32_t box_particle_texture(
    ecs_world_t *world,
    ecs_entity_t e)
{
    uint32_t result = 0;
    if (!e || !ecs_is_alive(world, e)) {
        goto done;
    }

    const RenderTexture *texture = ecs_get(world, e, RenderTexture);
    if (texture) {
        result = texture->handle;
        goto done;
    }

    const Image *image = ecs_get(world, e, Image);
    if (image && image->texture) {
        result = box_particle_texture(world, image->texture);
        goto done;
    }

    const PbrTextures *pbr = ecs_get(world, e, PbrTextures);
    if (pbr) {
        result = box_particle_texture(world, pbr->albedo);
    }

done:
    return result;
}

/** Write how the particles of an emitter are drawn.
 *
 * An emitter without the component draws soft blended circles, which is what
 * every emitter of the module did before there was an appearance.
 *
 * @param world The world
 * @param appearance The appearance, NULL when the emitter has none
 * @param out The block of the packet
 */
static void box_particle_appearance(
    ecs_world_t *world,
    const ParticleAppearance *appearance,
    box_particle_emitter_t *out)
{
    out->sheet[0] = 1.0f;
    out->sheet[1] = 1.0f;
    if (!appearance) {
        goto done;
    }

    if (appearance->shape == ParticleSprite) {
        out->appearance = BOX_PARTICLE_DRAW_SPRITE;
    } else if (appearance->shape == ParticleBillboard) {
        out->appearance = BOX_PARTICLE_DRAW_BILLBOARD;
    }

    if (appearance->blend == ParticleAdditive) {
        out->flags |= BOX_PARTICLE_ADDITIVE;
    }

    if (appearance->columns > 1) {
        out->sheet[0] = (float)appearance->columns;
    }

    if (appearance->rows > 1) {
        out->sheet[1] = (float)appearance->rows;
    }

    out->sheet[2] = appearance->fps > 0.0f ? appearance->fps : 0.0f;
    out->sheet[3] = appearance->softness > 0.0f ? appearance->softness : 0.0f;
    if (out->sheet[3] > 0.0f) {
        out->flags |= BOX_PARTICLE_SOFT;
    }

    out->texture = box_particle_texture(world, appearance->texture);
    if (appearance->mesh && ecs_is_alive(world, appearance->mesh)) {
        const RenderMesh *mesh = ecs_get(world, appearance->mesh, RenderMesh);
        out->mesh = mesh ? mesh->handle : 0;
    }

done:
    return;
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

    box_particle_color(emitter, out);
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

/** Advance the clock of every emitter of a world before its bursts are read.
 *
 * The clock has to run before the schedules are evaluated and the blocks are
 * written, because a burst that sits on a child of an emitter is reached by its
 * own query and only the emitter knows how far its playback has come.
 *
 * @param world The world
 * @param state The registry holding the emitter query
 * @param delta Seconds the frame advanced the world
 */
static void box_particle_clocks(
    ecs_world_t *world,
    const ParticleState *state,
    float delta)
{
    ecs_iter_t it = ecs_query_iter(world, state->emitters);
    while (ecs_query_next(&it)) {
        ParticleEmitterImpl *impl = ecs_field(&it, ParticleEmitterImpl, 2);
        ParticlePlayback *playback = ecs_field_is_set(&it, 7) ?
            ecs_field(&it, ParticlePlayback, 7) : NULL;
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            impl[i].flags = 0;
            box_particle_playback(playback ? &playback[i] : NULL, &impl[i],
                delta, &impl[i].flags);
        }
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
    box_particle_clocks(world, state, delta);
    box_particle_bursts(world, state);
    ecs_vec_t hits;
    ecs_vec_init_t(NULL, &hits, ecs_entity_t, 0);
    ecs_iter_t it = ecs_query_iter(world, state->emitters);
    while (ecs_query_next(&it)) {
        const ParticleEmitter *emitter = ecs_field(&it, ParticleEmitter, 0);
        const WorldTransform *transform = ecs_field(&it, WorldTransform, 1);
        ParticleEmitterImpl *impl = ecs_field(&it, ParticleEmitterImpl, 2);
        const ParticleForces *forces = ecs_field_is_set(&it, 3) ?
            ecs_field(&it, ParticleForces, 3) : NULL;
        const ParticleCollision *collision = ecs_field_is_set(&it, 4) ?
            ecs_field(&it, ParticleCollision, 4) : NULL;
        const ParticleCurves *curves = ecs_field_is_set(&it, 5) ?
            ecs_field(&it, ParticleCurves, 5) : NULL;
        const ParticleAppearance *look = ecs_field_is_set(&it, 6) ?
            ecs_field(&it, ParticleAppearance, 6) : NULL;
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
            box_particle_forces(world, &emitter[i], forces ? &forces[i] : NULL, out);
            box_particle_curves(curves ? &curves[i] : NULL, out);
            box_particle_appearance(world, look ? &look[i] : NULL, out);
            box_particle_table(&impl[i], p, out);
            out->origin[0] = out->transform[3];
            out->origin[1] = out->transform[7];
            out->origin[2] = out->transform[11];
            box_particle_collision(world, it.entities[i],
                collision ? &collision[i] : NULL,
                vec3_new(out->origin[0], out->origin[1], out->origin[2]),
                p, &hits, out);
            out->id = impl[i].id;
            out->max = max;
            out->flags |= impl[i].flags;
            out->delta = impl[i].state == ParticlePaused ? 0.0f : delta;
            out->first = impl[i].spawned;
            out->spawn = box_particle_spawn(&emitter[i], &impl[i], delta, max);
            if (impl[i].max != max) {
                out->flags |= BOX_PARTICLE_RESET;
                impl[i].max = max;
            }

            impl[i].spawned += out->spawn;
        }
    }

    ecs_vec_fini_t(NULL, &hits, ecs_entity_t);
done:
    return;
}

/**
 * @file render/water_caustics.c
 * @brief The water frames of the Water materials with WaterCaustics: the integral of their
 * WaterFlow velocity in cells, in double precision every frame, and the phase of their pattern.
 *
 * Each owner of a slot of the material table whose Water has WaterCaustics and a WaterFlow keeps
 * its frame in the table, which grows by the velocity over the scale and wraps to
 * BOX_RENDER_CAUSTICS_PERIOD cells, the period of the pattern of mesh_extended.wgsl.
 *
 * So it stays exact after hours, and a velocity that changes every frame moves the pattern on
 * without a jump.
 */

#include "draw.h"
#include <math.h>

double box_draws_water_caustics_wrap(
    double offset)
{
    double result = 0.0;
    if (isfinite(offset)) {
        result = fmod(offset, BOX_RENDER_CAUSTICS_PERIOD);
        if (result < 0.0) {
            result += BOX_RENDER_CAUSTICS_PERIOD;
        }

        if (result >= BOX_RENDER_CAUSTICS_PERIOD) {
            result = 0.0;
        }
    }

    return result;
}

float box_draws_water_caustics_frequency(
    const WaterCaustics *caustics)
{
    float scale = caustics->scale;
    if (!isfinite(scale)) {
        scale = BOX_MATERIAL_WATER_CAUSTICS_SCALE;
    }

    return 1.0f / glm_max(scale, BOX_RENDER_CAUSTICS_MIN);
}

float box_draws_water_caustics_phase(
    float speed,
    double time)
{
    float result = 0.0f;
    if (isfinite(speed) && isfinite(time)) {
        double v = fmod((double)speed * time / BOX_RENDER_CAUSTICS_LOOP, 1.0);
        if (v < 0.0) {
            v += 1.0;
        }

        result = (float)v;
        if (result >= 1.0f) {
            result = 0.0f;
        }
    }

    return result;
}

void box_draws_water_caustics_step(
    double *frame,
    const WaterCaustics *caustics,
    const WaterFlow *flow,
    double dt)
{
    int32_t i;
    if (!flow || !isfinite(dt) || dt <= 0.0) {
        goto done;
    }

    double frequency = (double)box_draws_water_caustics_frequency(caustics);
    for (i = 0; i < 2; i ++) {
        double v = isfinite(flow->velocity[i]) ? (double)flow->velocity[i] : 0.0;
        frame[i] = box_draws_water_caustics_wrap(frame[i] + v * frequency * dt);
    }

done:
    return;
}

bool box_draws_water_caustics_moves(
    const WaterCaustics *caustics,
    const WaterFlow *flow)
{
    return caustics && isfinite(caustics->strength) && caustics->strength > 0.0f &&
        (flow || (isfinite(caustics->speed) && caustics->speed != 0.0f));
}

void box_draws_water_caustics_frame(
    box_draw_materials_t *m,
    ecs_entity_t owner,
    const WaterCaustics *caustics,
    const WaterFlow *flow,
    float *out)
{
    out[0] = 0.0f;
    out[1] = 0.0f;
    if (!owner || !caustics || !flow) {
        goto done;
    }

    if (!ecs_map_is_init(&m->caustics)) {
        ecs_map_init(&m->caustics, NULL);
    }

    box_draw_water_caustics_state_t *s = ecs_map_get_deref(&m->caustics,
        box_draw_water_caustics_state_t, owner);
    if (!s) {
        s = ecs_map_ensure_alloc_t(&m->caustics, box_draw_water_caustics_state_t, owner);
        ecs_os_memset_t(s, 0, box_draw_water_caustics_state_t);
        box_draws_water_caustics_step(s->frame, caustics, flow, m->time);
        s->time = m->time;
    }

    out[0] = (float)s->frame[0];
    out[1] = (float)s->frame[1];
    out[0] = out[0] < (float)BOX_RENDER_CAUSTICS_PERIOD ? out[0] : 0.0f;
    out[1] = out[1] < (float)BOX_RENDER_CAUSTICS_PERIOD ? out[1] : 0.0f;
done:
    return;
}

bool box_draws_water_caustics_find(
    const box_draw_materials_t *m,
    ecs_entity_t owner,
    float *out)
{
    bool result = false;
    out[0] = 0.0f;
    out[1] = 0.0f;
    if (!owner || !ecs_map_is_init(&m->caustics)) {
        goto done;
    }

    const box_draw_water_caustics_state_t *s = ecs_map_get_deref(&m->caustics,
        box_draw_water_caustics_state_t, owner);
    if (s) {
        out[0] = (float)s->frame[0];
        out[1] = (float)s->frame[1];
        out[0] = out[0] < (float)BOX_RENDER_CAUSTICS_PERIOD ? out[0] : 0.0f;
        out[1] = out[1] < (float)BOX_RENDER_CAUSTICS_PERIOD ? out[1] : 0.0f;
        result = true;
    }

done:
    return result;
}

void box_draws_water_caustics_advance(
    const ecs_world_t *world,
    box_draw_materials_t *m)
{
    box_draw_material_t material;
    ecs_vec_t gone;
    int32_t i;
    if (!ecs_map_is_init(&m->caustics) || !ecs_map_count(&m->caustics)) {
        goto done;
    }

    ecs_vec_init_t(NULL, &gone, ecs_entity_t, 0);
    ecs_map_iter_t it = ecs_map_iter(&m->caustics);
    while (ecs_map_next(&it)) {
        ecs_entity_t owner = (ecs_entity_t)ecs_map_key(&it);
        box_draw_water_caustics_state_t *s = ecs_map_ptr(&it);
        bool alive = ecs_is_alive(world, owner) &&
            ecs_map_get(&m->owned, (ecs_map_key_t)owner) != NULL;
        if (alive) {
            box_draws_material_of(world, owner, m->time, &material);
        }

        if (!alive || !material.caustics || !material.flow) {
            *ecs_vec_append_t(NULL, &gone, ecs_entity_t) = owner;
            continue;
        }

        box_draws_water_caustics_step(s->frame, material.caustics, material.flow,
            m->time - s->time);
        s->time = m->time;
    }

    for (i = 0; i < ecs_vec_count(&gone); i ++) {
        ecs_map_remove_free(&m->caustics,
            (ecs_map_key_t)*ecs_vec_get_t(&gone, ecs_entity_t, i));
    }

    ecs_vec_fini_t(NULL, &gone, ecs_entity_t);
done:
    return;
}

void box_draws_water_caustics_fini(
    box_draw_materials_t *m)
{
    if (ecs_map_is_init(&m->caustics)) {
        ecs_map_iter_t it = ecs_map_iter(&m->caustics);
        while (ecs_map_next(&it)) {
            ecs_os_free(ecs_map_ptr(&it));
        }

        ecs_map_fini(&m->caustics);
    }
}

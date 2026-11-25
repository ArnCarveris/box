/**
 * @file render/water_waves.c
 * @brief The wave table: the travelling waves of the Water materials with WaterWaves, their
 * phases integrated in double precision every frame.
 *
 * Each owner of a slot of the material table whose Water has WaterWaves holds a set of the
 * table, which its record names (BOX_RENDER_WATER_WAVES_SHIFT).
 *
 * The phases grow by k times the WaterFlow velocity along the wave plus the angular frequency,
 * integrated over the total render time and wrapped to 0..2 pi, so they stay exact after hours
 * and a velocity that changes every frame moves the pattern on without a jump.
 */

#include "draw.h"
#include <math.h>

/** 2 pi in double precision */
#define BOX_WATER_TAU (6.283185307179586)

bool box_draws_water_wave_resolve(
    const WaterWave *wave,
    float *direction,
    double *k,
    double *omega)
{
    double dx = wave->direction[0], dz = wave->direction[1];
    double len = sqrt(dx * dx + dz * dz);
    double kk = 0.0, w = 0.0;
    bool result = isfinite(wave->wavelength) && wave->wavelength > 0.0f;
    if (!isfinite(len) || len < 1e-12) {
        dx = 1.0;
        dz = 0.0;
    } else {
        dx /= len;
        dz /= len;
    }

    if (result) {
        kk = BOX_WATER_TAU / (double)wave->wavelength;
        if (isfinite(wave->speed) && wave->speed != 0.0f) {
            w = (double)wave->speed * kk;
        } else {
            w = sqrt((double)BOX_MATERIAL_WATER_GRAVITY * kk);
        }
    }

    if (direction) {
        direction[0] = (float)dx;
        direction[1] = (float)dz;
    }

    if (k) {
        *k = kk;
    }

    if (omega) {
        *omega = w;
    }

    return result;
}

double box_draws_water_wave_rate(
    const WaterWave *wave,
    const WaterFlow *flow)
{
    float direction[2];
    double k, omega, result = 0.0;
    if (!box_draws_water_wave_resolve(wave, direction, &k, &omega)) {
        goto done;
    }

    result = omega;
    if (flow && isfinite(flow->velocity[0]) && isfinite(flow->velocity[1])) {
        result += k * ((double)direction[0] * (double)flow->velocity[0] +
            (double)direction[1] * (double)flow->velocity[1]);
    }

done:
    return result;
}

double box_draws_water_phase_wrap(
    double phase)
{
    double result = 0.0;
    if (isfinite(phase)) {
        result = fmod(phase, BOX_WATER_TAU);
        if (result < 0.0) {
            result += BOX_WATER_TAU;
        }

        if (result >= BOX_WATER_TAU) {
            result = 0.0;
        }
    }

    return result;
}

void box_draws_water_waves_step(
    double *phases,
    const WaterWaves *waves,
    const WaterFlow *flow,
    double dt)
{
    int32_t i;
    if (!isfinite(dt) || dt <= 0.0) {
        goto done;
    }

    for (i = 0; i < BOX_MATERIAL_WATER_WAVES; i ++) {
        phases[i] = box_draws_water_phase_wrap(
            phases[i] + box_draws_water_wave_rate(&waves->waves[i], flow) * dt);
    }

done:
    return;
}

void box_draws_water_waves_pack(
    box_render_water_waves_t *out,
    const WaterWaves *waves,
    const double *phases)
{
    int32_t i;
    ecs_os_memset_t(out, 0, box_render_water_waves_t);
    out->detail = isfinite(waves->detail) && waves->detail > 0.0f ? waves->detail : 0.0f;
    for (i = 0; i < BOX_MATERIAL_WATER_WAVES; i ++) {
        const WaterWave *wave = &waves->waves[i];
        box_render_water_wave_t *w = &out->waves[i];
        double k;
        if (!box_draws_water_wave_resolve(wave, w->direction, &k, NULL)) {
            w->direction[0] = 0.0f;
            w->direction[1] = 0.0f;
            continue;
        }

        float amplitude = isfinite(wave->amplitude) ? fabsf(wave->amplitude) : 0.0f;
        float phase = (float)box_draws_water_phase_wrap(phases[i]);
        w->k = (float)k;
        w->phase = phase < (float)BOX_WATER_TAU ? phase : 0.0f;
        w->slope = (float)((double)amplitude * k);
        w->steepness = isfinite(wave->steepness)
            ? glm_clamp(wave->steepness, 0.0f, 1.0f) : 0.0f;
        out->count ++;
    }
}

/** Pack the waves of a set for the render origin: the phase of each
 * wave moves back by k times the origin along it, so the waves at a local position are those at
 * its absolute position.
 *
 * @param out The set
 * @param waves The WaterWaves
 * @param phases The phases of its waves
 * @param origin Render origin
 */
static void box_water_waves_pack_at(
    box_render_water_waves_t *out,
    const WaterWaves *waves,
    const double *phases,
    const double *origin)
{
    double shifted[BOX_MATERIAL_WATER_WAVES];
    float direction[2];
    double k;
    int32_t i;
    for (i = 0; i < BOX_MATERIAL_WATER_WAVES; i ++) {
        shifted[i] = phases[i];
        if ((origin[0] != 0.0 || origin[2] != 0.0) &&
            box_draws_water_wave_resolve(&waves->waves[i], direction, &k, NULL))
        {
            shifted[i] = box_draws_water_phase_wrap(phases[i] -
                k * ((double)direction[0] * origin[0] + (double)direction[1] * origin[2]));
        }
    }

    box_draws_water_waves_pack(out, waves, shifted);
}

uint32_t box_draws_water_waves_find(
    const box_draw_materials_t *m,
    ecs_entity_t owner)
{
    uint32_t result = 0, i;
    if (!m->waves || !owner) {
        goto done;
    }

    for (i = 1; i < m->waves->used && !result; i ++) {
        if (m->waves->states[i].owner == owner) {
            result = i;
        }
    }

done:
    return result;
}

uint32_t box_draws_water_waves_set(
    box_draw_materials_t *m,
    ecs_entity_t owner,
    const WaterWaves *waves,
    const WaterFlow *flow)
{
    uint32_t result = box_draws_water_waves_find(m, owner), i;
    if (result || !owner) {
        goto done;
    }

    if (!m->waves) {
        m->waves = ecs_os_calloc_t(box_draw_water_waves_t);
        m->waves->used = 1;
    }

    box_draw_water_waves_t *w = m->waves;
    for (i = 1; i < (uint32_t)BOX_RENDER_WATER_WAVE_SETS && !result; i ++) {
        if (!w->states[i].owner) {
            result = i;
        }
    }

    if (!result) {
        if (!w->full) {
            ecs_warn("more than %d Water materials have WaterWaves, the others are flat",
                BOX_RENDER_WATER_WAVE_SETS - 1);
            w->full = true;
        }

        goto done;
    }

    box_draw_water_waves_state_t *s = &w->states[result];
    ecs_os_memset_t(s, 0, box_draw_water_waves_state_t);
    s->owner = owner;
    s->time = m->time;
    box_draws_water_waves_step(s->phases, waves, flow, m->time);
    box_water_waves_pack_at(&w->table[result], waves, s->phases, m->origin);
    if (result >= w->used) {
        w->used = result + 1;
    }

    w->dirty = true;
done:
    return result;
}

void box_draws_water_waves_advance(
    const ecs_world_t *world,
    box_draw_materials_t *m)
{
    box_draw_water_waves_t *w = m->waves;
    box_draw_material_t material;
    uint32_t i;
    if (!w) {
        goto done;
    }

    for (i = 1; i < w->used; i ++) {
        box_draw_water_waves_state_t *s = &w->states[i];
        if (!s->owner) {
            continue;
        }

        bool alive = ecs_is_alive(world, s->owner) &&
            ecs_map_get(&m->owned, (ecs_map_key_t)s->owner) != NULL;
        const box_draw_material_t *kept = &material;
        if (alive) {
            box_draws_material_of(world, s->owner, m->time, &material);
        }

        if (!alive || !kept->waves) {
            ecs_os_memset_t(s, 0, box_draw_water_waves_state_t);
            ecs_os_memset_t(&w->table[i], 0, box_render_water_waves_t);
            w->dirty = true;
            continue;
        }

        box_draws_water_waves_step(s->phases, kept->waves, kept->flow, m->time - s->time);
        s->time = m->time;
        box_water_waves_pack_at(&w->table[i], kept->waves, s->phases, m->origin);
        w->dirty = true;
    }

done:
    return;
}

void box_draws_water_waves_upload(
    box_draw_materials_t *m,
    box_render_packet_t *p)
{
    box_draw_water_waves_t *w = m->waves;
    if (!w || (!w->dirty && w->created)) {
        goto done;
    }

    if (!w->created) {
        box_render_buffer_desc_t *desc = box_render_packet_resource(p, BOX_RENDER_BUFFER,
            BOX_RENDER_CREATE, BOX_RENDER_BUFFER_WATER_WAVES, sizeof(box_render_buffer_desc_t));
        desc->size = (uint64_t)BOX_RENDER_WATER_WAVE_SETS * sizeof(box_render_water_waves_t);
        desc->usage = BOX_RENDER_BUFFER_UNIFORM;
        w->created = true;
    }

    uint32_t size = w->used * (uint32_t)sizeof(box_render_water_waves_t);
    void *dst = box_render_packet_upload(p, BOX_RENDER_BUFFER_WATER_WAVES, 0, size);
    ecs_os_memcpy(dst, w->table, (ecs_size_t)size);
    w->dirty = false;
done:
    return;
}

void box_draws_water_waves_fini(
    box_draw_materials_t *m)
{
    ecs_os_free(m->waves);
    m->waves = NULL;
}

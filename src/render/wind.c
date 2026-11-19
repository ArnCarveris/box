/**
 * @file render/wind.c
 * @brief The Wind of a world: the system that advances its WindStatus, and the WindSway fields
 * of the material records and the distance they move a vertex at most.
 *
 * The offset of the gust field and the phase of the sway are integrated on the CPU in double
 * precision and wrapped to their periods, like the wind offset of a FogVolume, so the motion
 * stays smooth after hours and a fixed delta time renders the same frames every run.
 *
 * A change of the speed only changes how fast they advance, so the plants do not jump.
 */

#include "draw.h"
#include <math.h>

void box_render_wind_resolve(
    const Wind *wind,
    Wind *out)
{
    float x = wind->direction[0], z = wind->direction[1];
    float length = sqrtf(x * x + z * z);
    *out = *wind;
    if (!isfinite(length) || length < 1e-6f) {
        out->direction[0] = 1.0f;
        out->direction[1] = 0.0f;
    } else {
        out->direction[0] = x / length;
        out->direction[1] = z / length;
    }

    out->speed = isfinite(wind->speed) && wind->speed > 0.0f ? wind->speed : 0.0f;
    out->gust = isfinite(wind->gust) ? glm_clamp(wind->gust, 0.0f, 1.0f) : 0.0f;
    out->gust_scale = isfinite(wind->gust_scale)
        ? glm_max(wind->gust_scale, BOX_RENDER_WIND_GUST_SCALE_MIN) : BOX_WIND_GUST_SCALE;
}

double box_render_wind_sway_frequency(
    const Wind *wind)
{
    double result = BOX_RENDER_WIND_SWAY_BASE + BOX_RENDER_WIND_SWAY_RATE * (double)wind->speed;

    return result < BOX_RENDER_WIND_SWAY_MAX ? result : BOX_RENDER_WIND_SWAY_MAX;
}

/** Wrap a value to 0..period, a value that is not finite to 0.
 *
 * @param v The value
 * @param period The period, positive
 * @return The wrapped value, below period
 */
static double box_wind_wrap(
    double v,
    double period)
{
    double result = isfinite(v) ? fmod(v, period) : 0.0;
    if (result < 0.0) {
        result += period;
    }

    if (result >= period) {
        result = 0.0;
    }

    return result;
}

/** Wrap a value to 0..period in double precision and round it to a float below the period.
 *
 * @param v The value
 * @param period The period, positive
 * @return The wrapped value
 */
static float box_wind_wrap_float(
    double v,
    double period)
{
    float result = (float)box_wind_wrap(v, period);

    return result >= (float)period ? 0.0f : result;
}

void box_render_wind_advance(
    const Wind *wind,
    double delta,
    WindStatus *status)
{
    Wind w;
    if (!isfinite(delta) || delta <= 0.0) {
        goto done;
    }

    box_render_wind_resolve(wind, &w);
    double period = BOX_WIND_GUST_PERIOD * (double)w.gust_scale;
    double travel = (double)w.speed * delta;
    status->offset_x = box_wind_wrap(status->offset_x + (double)w.direction[0] * travel, period);
    status->offset_z = box_wind_wrap(status->offset_z + (double)w.direction[1] * travel, period);
    status->sway = box_wind_wrap(
        status->sway + box_render_wind_sway_frequency(&w) * delta, 1.0);
    status->time = isfinite(status->time) ? status->time + delta : delta;
done:
    return;
}

void box_render_wind_frame(
    const ecs_world_t *world,
    box_render_wind_t *out)
{
    ecs_os_memset_t(out, 0, box_render_wind_t);
    out->direction[0] = 1.0f;
    out->frequency = 1.0f / BOX_WIND_GUST_SCALE;
    if (!ecs_id(Wind) || !ecs_id(WindStatus) || !ecs_is_alive(world, ecs_id(Wind)) ||
        !ecs_is_alive(world, ecs_id(WindStatus)))
    {
        goto done;
    }

    const Wind *wind = ecs_singleton_get(world, Wind);
    if (!wind) {
        goto done;
    }

    Wind w;
    box_render_wind_resolve(wind, &w);
    out->direction[0] = w.direction[0];
    out->direction[1] = w.direction[1];
    out->speed = w.speed;
    out->gust = w.gust;
    out->frequency = 1.0f / w.gust_scale;

    double origin[3];
    box_render_origin(world, origin);
    out->origin_y = origin[1];

    const WindStatus *s = ecs_get(world, ecs_id(Wind), WindStatus);
    double period = BOX_WIND_GUST_PERIOD * (double)w.gust_scale;
    double sx = s ? s->offset_x : 0.0, sz = s ? s->offset_z : 0.0, sway = s ? s->sway : 0.0;
    double along = (double)out->direction[0] * origin[0] +
        (double)out->direction[1] * origin[2];
    if (!isfinite(sway)) {
        sway = 0.0;
    }

    out->offset[0] = box_wind_wrap_float(sx - origin[0], period);
    out->offset[1] = box_wind_wrap_float(sz - origin[2], period);
    out->sway = box_wind_wrap_float(
        sway - 0.5 * along * (double)out->frequency / (2.0 * GLM_PI), 1.0);
    if (s) {
        out->time = isfinite(s->time) ? s->time : 0.0;
    }

done:
    return;
}

void box_render_wind_pack(
    box_render_material_t *out,
    const WindSway *sway,
    const box_render_wind_t *wind)
{
    if (!sway) {
        out->wind_direction[0] = 0.0f;
        out->wind_direction[1] = 0.0f;
        out->wind_offset[0] = 0.0f;
        out->wind_offset[1] = 0.0f;
        out->wind_speed = 0.0f;
        out->wind_gust = 0.0f;
        out->wind_frequency = 0.0f;
        out->wind_sway = 0.0f;
        out->wind_bend = 0.0f;
        out->wind_height = 0.0f;
        out->wind_base = 0.0f;
        out->wind_flutter = 0.0f;
        out->wind_reach = 0.0f;
        out->wind_flutter_phase = 0.0f;
        out->flags = 0u;
        out->reveal_threshold = 0.0f;
        goto done;
    }

    double frequency = isfinite(sway->flutter_frequency) ? (double)sway->flutter_frequency : 0.0;
    out->wind_direction[0] = wind->direction[0];
    out->wind_direction[1] = wind->direction[1];
    out->wind_offset[0] = wind->offset[0];
    out->wind_offset[1] = wind->offset[1];
    out->wind_speed = wind->speed;
    out->wind_gust = wind->gust;
    out->wind_frequency = wind->frequency;
    out->wind_sway = wind->sway;
    out->wind_bend = isfinite(sway->bend) ? sway->bend : 0.0f;
    out->wind_height = isfinite(sway->height)
        ? glm_max(sway->height, BOX_RENDER_WIND_MIN) : BOX_MATERIAL_SWAY_HEIGHT;
    out->wind_base = isfinite(sway->base)
        ? (float)((double)sway->base - wind->origin_y) : (float)(0.0 - wind->origin_y);
    out->wind_flutter = isfinite(sway->flutter) && sway->flutter > 0.0f ? sway->flutter : 0.0f;
    out->wind_reach = isfinite(sway->flutter_reach)
        ? glm_max(sway->flutter_reach, BOX_RENDER_WIND_MIN) : BOX_MATERIAL_SWAY_FLUTTER_REACH;
    out->wind_flutter_phase = box_wind_wrap_float(frequency * wind->time, 1.0);
    out->flags = BOX_RENDER_WIND_PRESENT;
    out->reveal_threshold = 0.0f;
done:
    return;
}

float box_render_wind_reach(
    const WindSway *sway,
    const box_render_wind_t *wind)
{
    float result = 0.0f;
    if (!sway || !(wind->speed > 0.0f)) {
        goto done;
    }

    float strength = wind->speed * (1.0f + wind->gust);
    float bend = isfinite(sway->bend) ? fabsf(sway->bend) : 0.0f;
    float flutter = isfinite(sway->flutter) && sway->flutter > 0.0f ? sway->flutter : 0.0f;
    result = bend * strength * (1.0f + BOX_RENDER_WIND_SWAY + BOX_RENDER_WIND_CROSS) +
        1.15f * flutter * glm_min(strength / BOX_WIND_FULL_SPEED, 1.0f);
    result = isfinite(result) ? result : 0.0f;
done:
    return result;
}

/** Advance the WindStatus of every Wind by the frame delta.
 *
 * @param it The iterator of the system
 */
static void BoxWindUpdate(
    ecs_iter_t *it)
{
    const Wind *w = ecs_field(it, Wind, 0);
    WindStatus *s = ecs_field(it, WindStatus, 1);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_render_wind_advance(&w[i], (double)it->delta_time, &s[i]);
    }
}

void box_render_wind_import(
    ecs_world_t *world)
{
    ecs_add_pair(world, ecs_id(Wind), EcsWith, ecs_id(WindStatus));
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "WindUpdate" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(Wind), .inout = EcsIn },
            { .id = ecs_id(WindStatus), .inout = EcsInOut }
        },
        .callback = BoxWindUpdate
    });
}

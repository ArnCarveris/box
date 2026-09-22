#include "sky_internal.h"
#include <math.h>

ECS_COMPONENT_DECLARE(WeatherBlend);

/** Hold an amount between a clear zero and a full one.
 *
 * @param value The amount
 * @return The amount inside the range
 */
static float box_weather_clamp(
    float value)
{
    return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

/** Move one amount toward its target over the transition of the weather.
 *
 * The step closes the same fraction of the gap per second whatever the frame
 * rate is, and an amount that stands within an epsilon of its target is put on
 * it, so a transition ends instead of trailing it forever.
 *
 * @param current The amount of the previous frame
 * @param target The amount the weather was set to
 * @param transition Seconds the amount eases over
 * @param delta Seconds the frame took
 * @return The amount of this frame
 */
static float box_weather_ease(
    float current,
    float target,
    float transition,
    float delta)
{
    float seconds = box_sky_or(transition, BOX_WEATHER_TRANSITION);
    float step = 1.0f - expf(-fmaxf(delta, 0.0f) / seconds);
    float result = current + (target - current) * step;
    if (fabsf(target - result) < BOX_WEATHER_EPSILON) {
        result = target;
    }

    return result;
}

/** Blend the amounts of one weather toward the amounts it was set to.
 *
 * @param w The weather
 * @param b The blend state of that weather
 * @param delta Seconds the frame took
 */
static void box_weather_step(
    Weather *w,
    WeatherBlend *b,
    float delta)
{
    float overcast = box_weather_clamp(w->overcast);
    float rain = box_weather_clamp(w->rain);
    if (!b->started) {
        b->amounts.overcast = overcast;
        b->amounts.rain = rain;
        b->started = true;
        goto done;
    }

    b->amounts.overcast = box_weather_ease(b->amounts.overcast, overcast,
        w->transition, delta);
    b->amounts.rain = box_weather_ease(b->amounts.rain, rain, w->transition, delta);
done:
    w->current = b->amounts;
}

/** Add what a rain raises an amount by, without passing a full one.
 *
 * @param amount The amount of the weather itself
 * @param raise Part of the rain that is added to it
 * @return The amount the frame is drawn with
 */
static float box_weather_raise(
    float amount,
    float raise)
{
    return box_weather_clamp(amount + raise);
}

/** Blend every weather of the world and drive what the amounts reach.
 *
 * @param it The system iterator
 */
static void BoxSkyWeatherUpdate(
    ecs_iter_t *it)
{
    Weather *w = ecs_field(it, Weather, 0);
    WeatherBlend *b = ecs_field(it, WeatherBlend, 1);

    int32_t i;
    for (i = 0; i < it->count; i ++) {
        box_weather_step(&w[i], &b[i], it->delta_time);
    }
}

bool box_sky_weather(
    ecs_world_t *world,
    WeatherAmounts *out)
{
    bool result = false;
    memset(out, 0, sizeof(WeatherAmounts));
    const SkyState *state = ecs_singleton_get(world, SkyState);
    if (!state || !state->weathers) {
        goto done;
    }

    ecs_iter_t it = ecs_query_iter(world, state->weathers);
    if (ecs_query_next(&it)) {
        const Weather *w = ecs_field(&it, Weather, 0);
        if (it.count) {
            *out = w[0].current;
            out->overcast = box_weather_raise(out->overcast,
                out->rain * BOX_WEATHER_RAIN_OVERCAST);
            result = true;
        }

        ecs_iter_fini(&it);
    }

done:
    return result;
}

void box_sky_weather_init(
    ecs_world_t *world)
{
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BoxSkyWeatherUpdate" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(Weather), .inout = EcsInOut },
            { .id = ecs_id(WeatherBlend), .inout = EcsInOut }
        },
        .callback = BoxSkyWeatherUpdate
    });
}

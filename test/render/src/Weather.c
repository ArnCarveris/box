/**
 * @file Weather.c
 * @brief Tests for the overcast, fog and rain a Weather blends over the sky.
 */

#include <box.h>
#include <box/sky.h>
#include <bake_test.h>
#include <math.h>
#include "../../../src/render/render_internal.h"

static ecs_world_t *w;
static box_packet_t packet;
static ecs_entity_t sun;
static ecs_entity_t atmosphere;
static ecs_entity_t fog;
static ecs_entity_t wetness;

void Weather_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, 0});
    ecs_set(w, cam, Camera, {.fov = 60});
    sun = ecs_new(w);
    ecs_set(w, sun, Rotation, {1.0f, 0, 0});
    ecs_set(w, sun, DirectionalLight, {.intensity = 3});
    atmosphere = ecs_new(w);
    ecs_set(w, atmosphere, Atmosphere, {.sun = sun});
    ecs_singleton_set(w, EnvironmentLight, {.intensity = 1, .source = atmosphere});
    fog = ecs_lookup(w, "box.effects.stack.fog");
    ecs_get_mut(w, fog, Effect)->enabled = true;
    ecs_modified(w, fog, Effect);
    wetness = ecs_lookup(w, "box.material.layers.wetness");
}

void Weather_teardown(void)
{
    uint32_t i;
    for (i = 0; i < packet.shader_create.count; i ++) {
        free(((box_shader_cmd_t*)box_vec_at(&packet.shader_create, i))->source);
    }

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Weather_a_world_without_weather_draws_a_clear_sky(void)
{
    // weather/states
    ecs_progress(w, 0);
    box_render_extract(w, &packet);

    test_assert(packet.sky.active == 1);
    test_assert(packet.sky.night[3] == 0.0f);
}

void Weather_the_overcast_reaches_the_sky(void)
{
    // weather/states
    ecs_singleton_set(w, Weather, {.overcast = 1});
    ecs_progress(w, 0);
    box_render_extract(w, &packet);

    test_assert(fabsf(packet.sky.night[3] - 1.0f) < 1e-5f);
}

void Weather_a_half_overcast_sits_between_the_two(void)
{
    // weather/states
    ecs_singleton_set(w, Weather, {.overcast = 0.5f});
    ecs_progress(w, 0);
    box_render_extract(w, &packet);

    test_assert(fabsf(packet.sky.night[3] - 0.5f) < 1e-5f);
}

void Weather_the_medium_stays_the_base_under_overcast(void)
{
    // weather/states
    box_render_extract(w, &packet);
    box_sky_t clear = packet.sky;

    box_packet_reset(&packet);
    ecs_singleton_set(w, Weather, {.overcast = 1});
    ecs_progress(w, 0);
    box_render_extract(w, &packet);

    test_int(memcmp(clear.scat_rayleigh, packet.sky.scat_rayleigh, sizeof(float) * 3), 0);
    test_int(memcmp(clear.scat_mie, packet.sky.scat_mie, sizeof(float) * 3), 0);
    test_int(memcmp(clear.ext_mie, packet.sky.ext_mie, sizeof(float) * 3), 0);
    test_int(memcmp(clear.bounds, packet.sky.bounds, sizeof(float) * 4), 0);
    test_int(memcmp(clear.sun_color, packet.sky.sun_color, sizeof(float) * 3), 0);
}

void Weather_the_sun_keeps_moving_under_overcast(void)
{
    // weather/states
    ecs_singleton_set(w, Weather, {.overcast = 1});
    ecs_progress(w, 0);
    box_render_extract(w, &packet);
    float first = packet.sky.sun_dir[1];

    box_packet_reset(&packet);
    ecs_set(w, sun, Rotation, {0.2f, 0, 0});
    ecs_progress(w, 0);
    box_render_extract(w, &packet);

    test_assert(fabsf(packet.sky.sun_dir[1] - first) > 0.1f);
}

void Weather_the_overcast_flattens_the_light_of_the_view(void)
{
    // weather/states
    box_render_extract(w, &packet);
    float clear[3] = { packet.view.sun_color[0], packet.view.sun_color[1],
        packet.view.sun_color[2] };
    float luma = clear[0] * 0.2126f + clear[1] * 0.7152f + clear[2] * 0.0722f;

    box_packet_reset(&packet);
    ecs_singleton_set(w, Weather, {.overcast = 1});
    ecs_progress(w, 0);
    box_render_extract(w, &packet);
    float flat = luma * BOX_WEATHER_OVERCAST_LIGHT;

    test_assert(clear[0] != clear[2]);
    test_assert(fabsf(packet.view.sun_color[0] - flat) < 1e-5f);
    test_assert(fabsf(packet.view.sun_color[1] - flat) < 1e-5f);
    test_assert(fabsf(packet.view.sun_color[2] - flat) < 1e-5f);
    test_assert(flat < luma);
}

void Weather_a_half_overcast_lights_between_the_two(void)
{
    // weather/states
    box_render_extract(w, &packet);
    float clear = packet.view.sun_color[0];
    float luma = packet.view.sun_color[0] * 0.2126f + packet.view.sun_color[1] * 0.7152f +
        packet.view.sun_color[2] * 0.0722f;

    box_packet_reset(&packet);
    ecs_singleton_set(w, Weather, {.overcast = 0.5f});
    ecs_progress(w, 0);
    box_render_extract(w, &packet);
    float expect = clear + (luma * BOX_WEATHER_OVERCAST_LIGHT - clear) * 0.5f;

    test_assert(fabsf(packet.view.sun_color[0] - expect) < 1e-5f);
}

void Weather_the_overcast_filters_the_environment_again(void)
{
    // weather/states
    box_render_extract(w, &packet);
    uint32_t first = packet.view.env_generation;

    box_packet_reset(&packet);
    ecs_singleton_set(w, Weather, {.overcast = 1});
    ecs_progress(w, 0);
    box_render_extract(w, &packet);

    test_assert(packet.view.env_generation != first);
}

void Weather_a_sliver_of_overcast_keeps_the_environment(void)
{
    // weather/states
    box_render_extract(w, &packet);
    uint32_t first = packet.view.env_generation;

    box_packet_reset(&packet);
    ecs_singleton_set(w, Weather, {.overcast = BOX_SKY_ENV_EPSILON * 0.5f});
    ecs_progress(w, 0);
    box_render_extract(w, &packet);

    test_uint(packet.view.env_generation, first);
}

void Weather_the_fog_raises_the_fog_density(void)
{
    // weather/states
    box_render_extract(w, &packet);
    float clear = ((const box_effect_t*)box_vec_at(&packet.effects, 0))->params[3];

    box_packet_reset(&packet);
    ecs_singleton_set(w, Weather, {.fog = 1});
    ecs_progress(w, 0);
    box_render_extract(w, &packet);
    float thick = ((const box_effect_t*)box_vec_at(&packet.effects, 0))->params[3];

    test_assert(clear > 0.0f);
    test_assert(fabsf(thick - clear * BOX_WEATHER_FOG_GAIN) < clear * 0.01f);
}

void Weather_a_half_fog_sits_between_the_two(void)
{
    // weather/states
    box_render_extract(w, &packet);
    float clear = ((const box_effect_t*)box_vec_at(&packet.effects, 0))->params[3];

    box_packet_reset(&packet);
    ecs_singleton_set(w, Weather, {.fog = 0.5f});
    ecs_progress(w, 0);
    box_render_extract(w, &packet);
    float half = ((const box_effect_t*)box_vec_at(&packet.effects, 0))->params[3];
    float expect = clear * (1.0f + 0.5f * (BOX_WEATHER_FOG_GAIN - 1.0f));

    test_assert(fabsf(half - expect) < clear * 0.01f);
}

void Weather_the_fog_leaves_the_color_and_the_framing_alone(void)
{
    // weather/states
    box_render_extract(w, &packet);
    const box_effect_t *pass = box_vec_at(&packet.effects, 0);
    float color[3] = { pass->params[0], pass->params[1], pass->params[2] };
    float falloff = pass->params[4];

    box_packet_reset(&packet);
    ecs_singleton_set(w, Weather, {.fog = 1});
    ecs_progress(w, 0);
    box_render_extract(w, &packet);
    const box_effect_t *thick = box_vec_at(&packet.effects, 0);

    test_assert(fabsf(thick->params[0] - color[0]) < 1e-6f);
    test_assert(fabsf(thick->params[1] - color[1]) < 1e-6f);
    test_assert(fabsf(thick->params[2] - color[2]) < 1e-6f);
    test_assert(fabsf(thick->params[4] - falloff) < 1e-6f);
}

void Weather_the_rain_wets_the_material_layer(void)
{
    // weather/states
    ecs_singleton_set(w, Weather, {.rain = 0.6f});
    ecs_progress(w, 0);
    const LayerParams *params = ecs_get(w, wetness, LayerParams);

    test_assert(params != NULL);
    test_assert(fabsf(params->a.w - 0.6f) < 1e-5f);
}

void Weather_a_world_without_weather_keeps_the_wetness_layer(void)
{
    // weather/states
    const LayerParams *before = ecs_get(w, wetness, LayerParams);
    float authored = before->a.w;
    ecs_progress(w, 0);
    const LayerParams *params = ecs_get(w, wetness, LayerParams);

    test_assert(authored > 0.0f);
    test_assert(fabsf(params->a.w - authored) < 1e-6f);
}

void Weather_the_first_frame_snaps_to_the_weather(void)
{
    // weather/states
    ecs_singleton_set(w, Weather, {.overcast = 1, .fog = 1, .rain = 1, .transition = 10});
    ecs_progress(w, 1.0f / 60.0f);
    const Weather *weather = ecs_singleton_get(w, Weather);

    test_assert(weather->current.overcast == 1.0f);
    test_assert(weather->current.fog == 1.0f);
    test_assert(weather->current.rain == 1.0f);
}

void Weather_a_new_target_is_eased_over_the_transition(void)
{
    // weather/states
    ecs_singleton_set(w, Weather, {.transition = 1});
    ecs_progress(w, 0);
    ecs_singleton_set(w, Weather, {.overcast = 1, .transition = 1});
    ecs_progress(w, 0.25f);
    const Weather *weather = ecs_singleton_get(w, Weather);

    test_assert(fabsf(weather->current.overcast - (1.0f - expf(-0.25f))) < 1e-4f);
}

void Weather_a_shorter_transition_moves_further(void)
{
    // weather/states
    ecs_singleton_set(w, Weather, {.transition = 4});
    ecs_progress(w, 0);
    ecs_singleton_set(w, Weather, {.overcast = 1, .transition = 4});
    ecs_progress(w, 0.5f);
    float slow = ecs_singleton_get(w, Weather)->current.overcast;

    ecs_singleton_set(w, Weather, {.transition = 0.5f});
    ecs_progress(w, 0);
    ecs_singleton_set(w, Weather, {.overcast = 1, .transition = 0.5f});
    ecs_progress(w, 0.5f);
    float fast = ecs_singleton_get(w, Weather)->current.overcast;

    test_assert(fast > slow);
}

void Weather_the_transition_arrives_at_the_target(void)
{
    // weather/states
    ecs_singleton_set(w, Weather, {.transition = 1});
    ecs_progress(w, 0);
    ecs_singleton_set(w, Weather, {.overcast = 1, .transition = 1});

    int32_t i;
    for (i = 0; i < 600; i ++) {
        ecs_progress(w, 1.0f / 60.0f);
    }

    test_assert(ecs_singleton_get(w, Weather)->current.overcast == 1.0f);
}

void Weather_a_write_of_one_amount_keeps_the_others(void)
{
    // weather/states
    ecs_singleton_set(w, Weather, {.overcast = 1, .transition = 1});
    ecs_progress(w, 0);
    ecs_singleton_set(w, Weather, {.overcast = 1, .fog = 1, .transition = 1});
    ecs_progress(w, 1.0f / 60.0f);
    const Weather *weather = ecs_singleton_get(w, Weather);

    test_assert(weather->current.overcast == 1.0f);
    test_assert(weather->current.fog > 0.0f);
    test_assert(weather->current.fog < 0.1f);
}

void Weather_an_amount_outside_the_range_is_held_inside_it(void)
{
    // weather/states
    ecs_singleton_set(w, Weather, {.overcast = 5, .fog = -2});
    ecs_progress(w, 0);
    const Weather *weather = ecs_singleton_get(w, Weather);

    test_assert(weather->current.overcast == 1.0f);
    test_assert(weather->current.fog == 0.0f);
}

void Weather_the_overcast_flattens_the_fog_color(void)
{
    // weather/states
    box_render_extract(w, &packet);
    const box_effect_t *clear = box_vec_at(&packet.effects, 0);
    float tint = fabsf(clear->params[2] - clear->params[0]);
    float red = clear->params[0];

    box_packet_reset(&packet);
    ecs_singleton_set(w, Weather, {.overcast = 1});
    ecs_progress(w, 0);
    box_render_extract(w, &packet);
    const box_effect_t *grey = box_vec_at(&packet.effects, 0);

    test_assert(tint > red * 0.02f);
    test_assert(fabsf(grey->params[1] - grey->params[0]) < 1e-6f);
    test_assert(fabsf(grey->params[2] - grey->params[0]) < 1e-6f);
}

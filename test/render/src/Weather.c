#include <box.h>
#include <box/sky.h>
#include <bake_test.h>
#include <math.h>
#include "../../../src/render/render_internal.h"

static ecs_world_t *w;
static box_packet_t packet;
static ecs_entity_t sun;
static ecs_entity_t atmosphere;

void Weather_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
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

void Weather_the_first_frame_snaps_to_the_weather(void)
{
    // weather/states
    ecs_singleton_set(w, Weather, {.overcast = 1, .rain = 1, .transition = 10});
    ecs_progress(w, 1.0f / 60.0f);
    const Weather *weather = ecs_singleton_get(w, Weather);

    test_assert(weather->current.overcast == 1.0f);
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
    ecs_singleton_set(w, Weather, {.overcast = 1, .rain = 1, .transition = 1});
    ecs_progress(w, 1.0f / 60.0f);
    const Weather *weather = ecs_singleton_get(w, Weather);

    test_assert(weather->current.overcast == 1.0f);
    test_assert(weather->current.rain > 0.0f);
    test_assert(weather->current.rain < 0.1f);
}

void Weather_an_amount_outside_the_range_is_held_inside_it(void)
{
    // weather/states
    ecs_singleton_set(w, Weather, {.overcast = 5, .rain = -2});
    ecs_progress(w, 0);
    const Weather *weather = ecs_singleton_get(w, Weather);

    test_assert(weather->current.overcast == 1.0f);
    test_assert(weather->current.rain == 0.0f);
}

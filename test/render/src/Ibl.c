/**
 * @file Ibl.c
 * @brief Tests for the environment and the fog the atmosphere drives.
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

void Ibl_setup(void)
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
}

void Ibl_teardown(void)
{
    uint32_t i;
    for (i = 0; i < packet.shader_create.count; i ++) {
        free(((box_shader_cmd_t*)box_vec_at(&packet.shader_create, i))->source);
    }

    box_packet_fini(&packet);
    ecs_fini(w);
}

void Ibl_the_fog_of_the_view_is_the_first_pass(void)
{
    // sky/sky_ibl
    box_render_extract(w, &packet);

    test_int(packet.effects.count, 1);
    test_int(packet.view.effect_count, 1);
}

void Ibl_the_fog_color_comes_from_the_sky(void)
{
    // sky/sky_ibl
    box_render_extract(w, &packet);
    const box_effect_t *pass = box_vec_at(&packet.effects, 0);

    test_assert(pass->params[0] > 0.0f);
    test_assert(pass->params[1] > 0.0f);
    test_assert(pass->params[2] > 0.0f);
}

void Ibl_a_low_sun_makes_the_fog_red(void)
{
    // sky/sky_ibl
    ecs_set(w, sun, Rotation, {asinf(0.9f), 0, 0});
    box_render_extract(w, &packet);
    const box_effect_t *high = box_vec_at(&packet.effects, 0);
    float high_ratio = high->params[2] / high->params[0];

    box_packet_reset(&packet);
    ecs_set(w, sun, Rotation, {asinf(0.02f), 0, 0});
    box_render_extract(w, &packet);
    const box_effect_t *low = box_vec_at(&packet.effects, 0);
    float low_ratio = low->params[2] / low->params[0];

    test_assert(low_ratio < high_ratio * 0.5f);
}

void Ibl_a_night_fog_keeps_the_night_tint(void)
{
    // sky/sky_ibl
    ecs_set(w, atmosphere, Atmosphere, {.sun = sun,
        .night_tint = {0, 0, 255}, .night_intensity = 0.5f});
    ecs_set(w, sun, Rotation, {-0.5f, 0, 0});
    box_render_extract(w, &packet);
    const box_effect_t *pass = box_vec_at(&packet.effects, 0);

    test_assert(pass->params[0] < 1e-4f);
    test_assert(fabsf(pass->params[2] - 0.5f) < 1e-3f);
}

void Ibl_the_fog_density_follows_the_world_scale(void)
{
    // sky/sky_ibl
    box_render_extract(w, &packet);
    float wide = ((const box_effect_t*)box_vec_at(&packet.effects, 0))->params[3];

    box_packet_reset(&packet);
    ecs_set(w, atmosphere, Atmosphere, {.sun = sun, .world_units_per_km = 500});
    box_render_extract(w, &packet);
    float narrow = ((const box_effect_t*)box_vec_at(&packet.effects, 0))->params[3];

    test_assert(wide > 0.0f);
    test_assert(fabsf(narrow - wide * 2.0f) < wide * 0.01f);
}

void Ibl_the_fog_intensity_scales_the_density(void)
{
    // sky/sky_ibl
    box_render_extract(w, &packet);
    float one = ((const box_effect_t*)box_vec_at(&packet.effects, 0))->params[3];

    box_packet_reset(&packet);
    ecs_set(w, atmosphere, Atmosphere, {.sun = sun, .fog_intensity = 4});
    box_render_extract(w, &packet);
    float four = ((const box_effect_t*)box_vec_at(&packet.effects, 0))->params[3];

    test_assert(fabsf(four - one * 4.0f) < one * 0.01f);
}

void Ibl_the_fog_falloff_follows_the_aerosol_layer(void)
{
    // sky/sky_ibl
    box_render_extract(w, &packet);
    const box_effect_t *pass = box_vec_at(&packet.effects, 0);
    float expect = 1.0f / (BOX_SKY_MIE_KM * BOX_SKY_UNITS_PER_KM);

    test_assert(fabsf(pass->params[4] - expect) < expect * 1e-3f);
}

void Ibl_the_fog_sits_at_the_sea_level_of_the_sky(void)
{
    // sky/sky_ibl
    ecs_set(w, atmosphere, Atmosphere, {.sun = sun, .sea_level_y = 12});
    box_render_extract(w, &packet);
    const box_effect_t *pass = box_vec_at(&packet.effects, 0);

    test_flt(pass->params[7], 12.0f);
}

void Ibl_a_camera_over_the_haze_sees_thinner_fog(void)
{
    // sky/sky_ibl
    box_render_extract(w, &packet);
    float low = ((const box_effect_t*)box_vec_at(&packet.effects, 0))->params[3];

    box_packet_reset(&packet);
    ecs_set(w, atmosphere, Atmosphere, {.sun = sun, .ground_altitude_km = 6});
    box_render_extract(w, &packet);
    float high = ((const box_effect_t*)box_vec_at(&packet.effects, 0))->params[3];

    test_assert(high < low * 0.6f);
}

void Ibl_a_negative_fog_intensity_keeps_the_authored_fog(void)
{
    // sky/sky_ibl
    ecs_set(w, atmosphere, Atmosphere, {.sun = sun, .fog_intensity = -1});
    ecs_set(w, fog, Fog, {.color = {0.25f, 0.5f, 0.75f}, .density = 0.125f,
        .falloff = 0.5f, .start = 3, .max_opacity = 0.5f, .height = 7});
    box_render_extract(w, &packet);
    const box_effect_t *pass = box_vec_at(&packet.effects, 0);

    test_flt(pass->params[0], 0.25f);
    test_flt(pass->params[1], 0.5f);
    test_flt(pass->params[2], 0.75f);
    test_flt(pass->params[3], 0.125f);
    test_flt(pass->params[4], 0.5f);
    test_flt(pass->params[7], 7.0f);
}

void Ibl_a_world_without_an_atmosphere_keeps_the_authored_fog(void)
{
    // sky/sky_ibl
    ecs_delete(w, atmosphere);
    ecs_set(w, fog, Fog, {.color = {0.25f, 0.5f, 0.75f}, .density = 0.125f});
    box_render_extract(w, &packet);
    const box_effect_t *pass = box_vec_at(&packet.effects, 0);

    test_flt(pass->params[0], 0.25f);
    test_flt(pass->params[3], 0.125f);
}

void Ibl_the_sky_leaves_the_framing_of_the_fog_alone(void)
{
    // sky/sky_ibl
    ecs_set(w, fog, Fog, {.start = 9, .max_opacity = 0.25f});
    box_render_extract(w, &packet);
    const box_effect_t *pass = box_vec_at(&packet.effects, 0);

    test_flt(pass->params[5], 9.0f);
    test_flt(pass->params[6], 0.25f);
}

void Ibl_the_environment_is_filtered_on_the_first_frame(void)
{
    // sky/sky_ibl
    box_render_extract(w, &packet);
    uint32_t first = packet.view.env_generation;

    box_packet_reset(&packet);
    box_render_extract(w, &packet);

    test_assert(first != 0);
    test_uint(packet.view.env_generation, first);
}

void Ibl_a_changed_medium_filters_the_environment_again(void)
{
    // sky/sky_ibl
    box_render_extract(w, &packet);
    uint32_t first = packet.view.env_generation;

    box_packet_reset(&packet);
    ecs_set(w, atmosphere, Atmosphere, {.sun = sun, .rayleigh = {.scattering_scale = 2}});
    box_render_extract(w, &packet);

    test_assert(packet.view.env_generation != first);
}

void Ibl_a_small_step_of_the_sun_keeps_the_environment(void)
{
    // sky/sky_ibl
    box_render_extract(w, &packet);
    uint32_t first = packet.view.env_generation;
    uint32_t sky = packet.sky.generation;

    box_packet_reset(&packet);
    ecs_set(w, sun, Rotation, {1.0f + BOX_SKY_SUN_EPSILON * 4.0f, 0, 0});
    box_render_extract(w, &packet);

    test_assert(packet.sky.generation != sky);
    test_uint(packet.view.env_generation, first);
}

void Ibl_a_large_step_of_the_sun_filters_the_environment_again(void)
{
    // sky/sky_ibl
    box_render_extract(w, &packet);
    uint32_t first = packet.view.env_generation;

    box_packet_reset(&packet);
    ecs_set(w, sun, Rotation, {1.0f + BOX_SKY_ENV_EPSILON * 4.0f, 0, 0});
    box_render_extract(w, &packet);

    test_assert(packet.view.env_generation != first);
}

void Ibl_a_moving_sun_filters_the_environment_less_often(void)
{
    // sky/sky_ibl
    float angle = 1.0f;
    uint32_t env = 0;
    uint32_t sky = 0;
    uint32_t env_changes = 0;
    uint32_t sky_changes = 0;

    int32_t i;
    for (i = 0; i < 60; i ++) {
        box_packet_reset(&packet);
        angle += BOX_SKY_SUN_EPSILON * 2.0f;
        ecs_set(w, sun, Rotation, {angle, 0, 0});
        box_render_extract(w, &packet);
        env_changes += packet.view.env_generation != env ? 1u : 0u;
        sky_changes += packet.sky.generation != sky ? 1u : 0u;
        env = packet.view.env_generation;
        sky = packet.sky.generation;
    }

    test_assert(sky_changes > 50);
    test_assert(env_changes * 3 < sky_changes);
}

void Ibl_a_camera_that_rises_a_little_keeps_the_environment(void)
{
    // sky/sky_ibl
    box_render_extract(w, &packet);
    uint32_t first = packet.view.env_generation;

    box_packet_reset(&packet);
    ecs_iter_t it = ecs_each(w, Camera);
    while (ecs_each_next(&it)) {
        ecs_set(w, it.entities[0], Position, {0, 20, 0});
    }

    box_render_extract(w, &packet);

    test_uint(packet.view.env_generation, first);
}

void Ibl_a_camera_that_climbs_filters_the_environment_again(void)
{
    // sky/sky_ibl
    box_render_extract(w, &packet);
    uint32_t first = packet.view.env_generation;

    box_packet_reset(&packet);
    ecs_iter_t it = ecs_each(w, Camera);
    while (ecs_each_next(&it)) {
        ecs_set(w, it.entities[0], Position, {0, 900, 0});
    }

    box_render_extract(w, &packet);

    test_assert(packet.view.env_generation != first);
}

void Ibl_a_gradient_environment_is_not_touched_by_the_sky(void)
{
    // sky/sky_ibl
    ecs_singleton_set(w, EnvironmentLight, {.intensity = 1});
    box_render_extract(w, &packet);
    uint32_t first = packet.view.env_generation;

    box_packet_reset(&packet);
    ecs_set(w, sun, Rotation, {1.0f + BOX_SKY_ENV_EPSILON * 8.0f, 0, 0});
    box_render_extract(w, &packet);

    test_int(packet.view.sky_env, 0);
    test_uint(packet.view.env_generation, first);
}

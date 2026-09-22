#include <box.h>
#include <box/sky.h>
#include <bake_test.h>
#include <math.h>
#include "../../../src/render/render_internal.h"

static ecs_world_t *w;
static box_packet_t packet;
static ecs_entity_t sun;
static ecs_entity_t atmosphere;

void Ibl_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, 0});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(w);
    ecs_set(w, view, View, {.camera = cam, .enabled = true});
    sun = ecs_new(w);
    ecs_set(w, sun, Rotation, {1.0f, 0, 0});
    ecs_set(w, sun, DirectionalLight, {.intensity = 3});
    atmosphere = ecs_new(w);
    ecs_set(w, atmosphere, Atmosphere, {.sun = sun});
    ecs_singleton_set(w, EnvironmentLight, {.intensity = 1, .source = atmosphere});
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

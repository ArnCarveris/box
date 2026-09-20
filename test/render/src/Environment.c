/**
 * @file Environment.c
 * @brief Tests for extracting the environment light into the frame packet view.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "../../../src/render/render_internal.h"

static ecs_world_t *w;
static box_packet_t packet;

void Environment_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_init(&packet);
    packet.width = 640;
    packet.height = 480;
    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, 0});
    ecs_set(w, cam, Camera, {.fov = 60});
}

void Environment_teardown(void)
{
    box_packet_fini(&packet);
    ecs_fini(w);
}

void Environment_defaults_without_component(void)
{
    // lighting/ibl
    test_assert(ecs_singleton_get(w, EnvironmentLight) == NULL);
    box_render_extract(w, &packet);
    test_flt(packet.view.env_sky[0], 1);
    test_flt(packet.view.env_sky[1], 1);
    test_flt(packet.view.env_sky[2], 1);
    test_assert(fabsf(packet.view.env_ground[0] - box_srgb_to_linear(BOX_ENV_GROUND)) < 1e-6f);
    test_assert(packet.view.env_ground[0] < packet.view.env_sky[0]);
    test_flt(packet.view.env_intensity, BOX_ENV_INTENSITY);
    test_int(packet.view.env_generation, 1);
}

void Environment_colors_are_linear(void)
{
    // lighting/ibl
    ecs_singleton_set(w, EnvironmentLight, {
        .sky_color = {255, 128, 0},
        .ground_color = {0, 128, 255},
        .intensity = 1
    });
    box_render_extract(w, &packet);
    test_flt(packet.view.env_sky[0], 1);
    test_assert(fabsf(packet.view.env_sky[1] - box_srgb_to_linear(128)) < 1e-6f);
    test_assert(packet.view.env_sky[1] < 0.3f);
    test_flt(packet.view.env_sky[2], 0);
    test_flt(packet.view.env_ground[0], 0);
    test_assert(fabsf(packet.view.env_ground[1] - box_srgb_to_linear(128)) < 1e-6f);
    test_flt(packet.view.env_ground[2], 1);
}

void Environment_intensity_copied_into_view(void)
{
    // lighting/ibl
    ecs_singleton_set(w, EnvironmentLight, {
        .sky_color = {255, 255, 255},
        .ground_color = {255, 255, 255},
        .intensity = 2.5f
    });
    box_render_extract(w, &packet);
    test_flt(packet.view.env_intensity, 2.5f);
    test_flt(packet.view.env_sky[0], 1);
    test_flt(packet.view.env_ground[0], 1);
}

void Environment_zero_intensity_is_one(void)
{
    // lighting/ibl
    ecs_singleton_set(w, EnvironmentLight, {
        .sky_color = {255, 255, 255},
        .ground_color = {0, 0, 0}
    });
    box_render_extract(w, &packet);
    test_flt(packet.view.env_intensity, 1);
}

void Environment_generation_stable_without_change(void)
{
    // lighting/ibl
    ecs_singleton_set(w, EnvironmentLight, {
        .sky_color = {120, 160, 240},
        .ground_color = {60, 50, 40},
        .intensity = 0.8f
    });
    box_render_extract(w, &packet);
    uint32_t first = packet.view.env_generation;
    test_assert(first != 0);

    int32_t i;
    for (i = 0; i < 3; i ++) {
        box_packet_reset(&packet);
        packet.width = 640;
        packet.height = 480;
        box_render_extract(w, &packet);
        test_int(packet.view.env_generation, first);
    }
}

void Environment_generation_changes_on_change(void)
{
    // lighting/ibl
    ecs_singleton_set(w, EnvironmentLight, {
        .sky_color = {120, 160, 240},
        .ground_color = {60, 50, 40},
        .intensity = 0.8f
    });
    box_render_extract(w, &packet);
    uint32_t first = packet.view.env_generation;

    ecs_singleton_set(w, EnvironmentLight, {
        .sky_color = {120, 160, 240},
        .ground_color = {60, 50, 40},
        .intensity = 1.6f
    });
    box_render_extract(w, &packet);
    test_int(packet.view.env_generation, first + 1);

    ecs_singleton_set(w, EnvironmentLight, {
        .sky_color = {121, 160, 240},
        .ground_color = {60, 50, 40},
        .intensity = 1.6f
    });
    box_render_extract(w, &packet);
    test_int(packet.view.env_generation, first + 2);
}

void Environment_removed_component_restores_defaults(void)
{
    // lighting/ibl
    ecs_singleton_set(w, EnvironmentLight, {
        .sky_color = {10, 20, 30},
        .ground_color = {40, 50, 60},
        .intensity = 3
    });
    box_render_extract(w, &packet);
    uint32_t generation = packet.view.env_generation;
    test_assert(packet.view.env_sky[0] < 0.01f);

    ecs_singleton_remove(w, EnvironmentLight);
    box_render_extract(w, &packet);
    test_int(packet.view.env_generation, generation + 1);
    test_flt(packet.view.env_sky[0], 1);
    test_flt(packet.view.env_intensity, BOX_ENV_INTENSITY);
}

void Environment_source_is_reserved(void)
{
    // lighting/ibl
    ecs_entity_t sky = ecs_new(w);
    ecs_singleton_set(w, EnvironmentLight, {
        .sky_color = {200, 210, 255},
        .ground_color = {80, 70, 60},
        .intensity = 1
    });
    box_render_extract(w, &packet);
    const EnvironmentLight *env = ecs_singleton_get(w, EnvironmentLight);
    test_uint(env->source, 0);
    uint32_t generation = packet.view.env_generation;

    ecs_singleton_set(w, EnvironmentLight, {
        .sky_color = {200, 210, 255},
        .ground_color = {80, 70, 60},
        .intensity = 1,
        .source = sky
    });
    box_render_extract(w, &packet);
    test_int(packet.view.env_generation, generation + 1);
}

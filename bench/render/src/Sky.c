/**
 * @file Sky.c
 * @brief Benchmarks for extracting the atmosphere into a frame packet.
 */

#include <box.h>
#include <box/sky.h>
#include <bake_bench.h>
#include "../../../src/render/render_internal.h"

void Sky_extract_steady(bench_t *b)
{
    // sky/atmosphere
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_packet_t packet;
    box_packet_init(&packet);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 2, -10});
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {1.0f, 0, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = 3});
    ecs_entity_t atmosphere = ecs_new(world);
    ecs_set(world, atmosphere, Atmosphere, {.sun = sun});
    ecs_singleton_set(world, EnvironmentLight, {
        .intensity = 1,
        .source = atmosphere
    });

    bench_set_items(b, 1);

    while (bench_iter(b)) {
        bench_pause(b);
        box_packet_reset(&packet);
        packet.width = 1920;
        packet.height = 1080;
        bench_resume(b);
        box_render_extract(world, &packet);
        bench_keep(packet.sky.generation);
    }

    box_packet_fini(&packet);
    ecs_fini(world);
}

void Sky_extract_moving_sun(bench_t *b)
{
    // sky/atmosphere
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_packet_t packet;
    box_packet_init(&packet);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 2, -10});
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {1.0f, 0, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = 3});
    ecs_entity_t atmosphere = ecs_new(world);
    ecs_set(world, atmosphere, Atmosphere, {.sun = sun});
    ecs_singleton_set(world, EnvironmentLight, {
        .intensity = 1,
        .source = atmosphere
    });

    float angle = 1.0f;
    bench_set_items(b, 1);

    while (bench_iter(b)) {
        bench_pause(b);
        box_packet_reset(&packet);
        packet.width = 1920;
        packet.height = 1080;
        angle += BOX_SKY_SUN_EPSILON * 4.0f;
        ecs_set(world, sun, Rotation, {angle, 0, 0});
        bench_resume(b);
        box_render_extract(world, &packet);
        bench_keep(packet.sky.generation);
    }

    box_packet_fini(&packet);
    ecs_fini(world);
}

void Sky_extract_moon_and_stars(bench_t *b)
{
    // sky/celestial
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_packet_t packet;
    box_packet_init(&packet);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 2, -10});
    ecs_set(world, camera, Camera, {.fov = 60});
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {1.0f, 0, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = 3});
    ecs_set(world, sun, CelestialLight, {.toa_intensity = 12});
    ecs_entity_t moon = ecs_new(world);
    ecs_set(world, moon, Rotation, {-1.0f, 0, 0});
    ecs_set(world, moon, DirectionalLight, {.intensity = 1});
    ecs_set(world, moon, CelestialLight, {.toa_intensity = 0.02f});
    ecs_entity_t stars = ecs_new(world);
    ecs_set(world, stars, Rotation, {0.2f, 0.4f, 0});
    ecs_set(world, stars, Stars, {0});
    ecs_set(world, stars, CelestialLight, {.toa_intensity = 2});
    ecs_entity_t atmosphere = ecs_new(world);
    ecs_set(world, atmosphere, Atmosphere, {
        .sun = sun,
        .moon = moon,
        .stars = stars
    });
    ecs_singleton_set(world, EnvironmentLight, {
        .intensity = 1,
        .source = atmosphere
    });

    float angle = 1.0f;
    bench_set_items(b, 1);

    while (bench_iter(b)) {
        bench_pause(b);
        box_packet_reset(&packet);
        packet.width = 1920;
        packet.height = 1080;
        angle += BOX_SKY_SUN_EPSILON * 4.0f;
        ecs_set(world, sun, Rotation, {angle, 0, 0});
        bench_resume(b);
        box_render_extract(world, &packet);
        bench_keep(packet.sky.moon_color[3]);
    }

    box_packet_fini(&packet);
    ecs_fini(world);
}

void Sky_advance_the_clock(bench_t *b)
{
    // sky/time_of_day
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {0, 0, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = 3});
    ecs_entity_t moon = ecs_new(world);
    ecs_set(world, moon, Rotation, {0, 0, 0});
    ecs_set(world, moon, DirectionalLight, {.intensity = 1});
    ecs_entity_t stars = ecs_new(world);
    ecs_set(world, stars, Rotation, {0, 0, 0});
    ecs_set(world, stars, Stars, {0});
    ecs_entity_t atmosphere = ecs_new(world);
    ecs_set(world, atmosphere, Atmosphere, {
        .sun = sun,
        .moon = moon,
        .stars = stars
    });
    ecs_entity_t clock = ecs_new(world);
    ecs_set(world, clock, TimeOfDay, {
        .atmosphere = atmosphere,
        .hour = 9,
        .day_of_year = 81,
        .latitude = 40,
        .time_scale = 0.5f,
        .moon_position_offset = 12
    });

    bench_set_items(b, 1);

    while (bench_iter(b)) {
        ecs_progress(world, 1.0f / 60.0f);
        bench_keep(ecs_get(world, clock, TimeOfDay)->daylight);
    }

    ecs_fini(world);
}

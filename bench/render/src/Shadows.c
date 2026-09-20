/**
 * @file Shadows.c
 * @brief Benchmarks for the cascade fit and the caster set of the shadow pass.
 */

#include <box.h>
#include <bake_bench.h>
#include <stdlib.h>
#include "../../../src/render/render_internal.h"

/** Static entities the caster benchmark resolves the caster flag of */
#define BENCH_CASTER_COUNT (100000)

/** Entities on one axis of the grid the caster benchmark spreads them over */
#define BENCH_CASTER_SPAN (400)

void Shadows_fit_cascades(bench_t *b)
{
    // views/shadows
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_packet_t packet;
    box_packet_init(&packet);
    packet.width = 1920;
    packet.height = 1080;

    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {0.9f, -0.6f, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = 3});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 60, -160});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.5f, .far_ = 500});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    ecs_get_mut(world, view, View)->camera = camera;
    ecs_modified(world, view, View);
    box_render_extract(world, &packet);

    box_view_t v = *(box_view_t*)box_vec_at(&packet.views, 0);
    bench_set_items(b, BOX_SHADOW_CASCADE_MAX);

    while (bench_iter(b)) {
        box_render_extract_shadows(world, view, true, &v);
        bench_keep(v.shadow_cascades);
    }

    box_packet_fini(&packet);
    ecs_fini(world);
}

void Shadows_casters_100k(bench_t *b)
{
    // views/shadows
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t *scene = ecs_os_malloc_n(ecs_entity_t, BENCH_CASTER_COUNT);
    int32_t i;
    for (i = 0; i < BENCH_CASTER_COUNT; i ++) {
        float x = (float)(i % BENCH_CASTER_SPAN) - 200.0f;
        float z = (float)(i / BENCH_CASTER_SPAN);
        scene[i] = ecs_new(world);
        ecs_set(world, scene[i], Position, {x, 0, z});
        ecs_set(world, scene[i], Box, {1, 1, 1});
        if (!(i % 100)) {
            ecs_add_id(world, scene[i], NoShadow);
        }
    }

    bench_set_items(b, BENCH_CASTER_COUNT);

    while (bench_iter(b)) {
        uint32_t casters = 0;
        for (i = 0; i < BENCH_CASTER_COUNT; i ++) {
            casters += box_render_casts_shadow(world, scene[i]) ? 1u : 0u;
        }

        bench_keep(casters);
    }

    ecs_os_free(scene);
    ecs_fini(world);
}

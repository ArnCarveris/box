/**
 * @file Shadow.c
 * @brief Tests that the shadow pass of a view walks the tree with its own threshold.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/render/render_internal.h"

void Shadow_a_caster_the_view_misses_is_replaced_by_its_proxy(void)
{
    // lod/cache_and_jobs
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t p;
    ecs_entity_t parts[16];
    int32_t i;

    ecs_singleton_set(w, LodSettings, {
        .enabled = true, .quality = 1, .error_px = 2, .shadow_error_px = 30,
        .hysteresis = 0.25f
    });

    ecs_entity_t sun = ecs_new(w);
    ecs_set(w, sun, Rotation, {0.8f, -0.55f, 0});
    ecs_set(w, sun, DirectionalLight, {.intensity = 1});

    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 2, -30});
    ecs_set(w, cam, LookAt, {0, 0, 0});
    ecs_set(w, cam, Camera, {.fov = 15, .far_ = 4000});
    ecs_entity_t view = ecs_new(w);
    ecs_ensure(w, view, View)->camera = cam;
    ecs_modified(w, view, View);

    ecs_entity_t root = ecs_new(w);
    ecs_set(w, root, Position, {60, 0, 20});
    for (i = 0; i < 16; i ++) {
        parts[i] = ecs_new(w);
        ecs_add_pair(w, parts[i], EcsChildOf, root);
        ecs_set(w, parts[i], Position, {(float)(i % 4) * 3, (float)(i / 4) * 3, 0});
        ecs_set(w, parts[i], Box, {1, 1, 1});
        ecs_set(w, parts[i], Rgb, {200, 100, 50});
    }

    ecs_add(w, root, RootBounds);
    box_packet_init(&p);
    for (i = 0; i < 3; i ++) {
        box_packet_reset(&p);
        p.width = 1920;
        p.height = 1080;
        box_render_extract(w, &p);
        box_collision_update(w);
    }

    test_assert(p.dynamic_instances.count > 0);
    test_bool(ecs_get(w, parts[0], RenderInstance)->lod_hidden, true);

    box_packet_fini(&p);
    ecs_fini(w);
}

void Shadow_a_small_shadow_threshold_keeps_the_caster_detailed(void)
{
    // lod/cache_and_jobs
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t p;
    ecs_entity_t parts[16];
    int32_t i;

    ecs_singleton_set(w, LodSettings, {
        .enabled = true, .quality = 1, .error_px = 2, .shadow_error_px = 0.001f,
        .hysteresis = 0.25f
    });

    ecs_entity_t sun = ecs_new(w);
    ecs_set(w, sun, Rotation, {0.8f, -0.55f, 0});
    ecs_set(w, sun, DirectionalLight, {.intensity = 1});

    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 2, -30});
    ecs_set(w, cam, LookAt, {0, 0, 0});
    ecs_set(w, cam, Camera, {.fov = 15, .far_ = 4000});
    ecs_entity_t view = ecs_new(w);
    ecs_ensure(w, view, View)->camera = cam;
    ecs_modified(w, view, View);

    ecs_entity_t root = ecs_new(w);
    ecs_set(w, root, Position, {60, 0, 20});
    for (i = 0; i < 16; i ++) {
        parts[i] = ecs_new(w);
        ecs_add_pair(w, parts[i], EcsChildOf, root);
        ecs_set(w, parts[i], Position, {(float)(i % 4) * 3, (float)(i / 4) * 3, 0});
        ecs_set(w, parts[i], Box, {1, 1, 1});
        ecs_set(w, parts[i], Rgb, {200, 100, 50});
    }

    ecs_add(w, root, RootBounds);
    box_packet_init(&p);
    for (i = 0; i < 3; i ++) {
        box_packet_reset(&p);
        p.width = 1920;
        p.height = 1080;
        box_render_extract(w, &p);
        box_collision_update(w);
    }

    test_int(p.dynamic_instances.count, 0);
    test_bool(ecs_get(w, parts[0], RenderInstance)->lod_hidden, false);

    box_packet_fini(&p);
    ecs_fini(w);
}

void Shadow_a_world_without_a_sun_walks_the_view_alone(void)
{
    // lod/cache_and_jobs
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t p;
    ecs_entity_t parts[16];
    int32_t i;

    ecs_singleton_set(w, LodSettings, {
        .enabled = true, .quality = 1, .error_px = 2, .shadow_error_px = 6,
        .hysteresis = 0.25f
    });

    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 2, -30});
    ecs_set(w, cam, LookAt, {0, 0, 0});
    ecs_set(w, cam, Camera, {.fov = 15, .far_ = 4000});
    ecs_entity_t view = ecs_new(w);
    ecs_ensure(w, view, View)->camera = cam;
    ecs_modified(w, view, View);

    ecs_entity_t root = ecs_new(w);
    ecs_set(w, root, Position, {60, 0, 20});
    for (i = 0; i < 16; i ++) {
        parts[i] = ecs_new(w);
        ecs_add_pair(w, parts[i], EcsChildOf, root);
        ecs_set(w, parts[i], Position, {(float)(i % 4) * 3, (float)(i / 4) * 3, 0});
        ecs_set(w, parts[i], Box, {1, 1, 1});
        ecs_set(w, parts[i], Rgb, {200, 100, 50});
    }

    ecs_add(w, root, RootBounds);
    box_packet_init(&p);
    for (i = 0; i < 3; i ++) {
        box_packet_reset(&p);
        p.width = 1920;
        p.height = 1080;
        box_render_extract(w, &p);
        box_collision_update(w);
    }

    test_int(p.dynamic_instances.count, 0);
    test_bool(ecs_get(w, parts[0], RenderInstance)->lod_hidden, true);

    box_packet_fini(&p);
    ecs_fini(w);
}

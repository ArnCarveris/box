/**
 * @file Hierarchy.c
 * @brief Tests that the extract builds the LOD hierarchy and applies what it decides.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/render/render_internal.h"

void Hierarchy_a_far_root_is_replaced_by_a_proxy(void)
{
    // lod/hierarchy_traversal
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t p;
    ecs_entity_t parts[4];
    int32_t i;

    ecs_singleton_set(w, LodSettings, {
        .enabled = true, .quality = 1, .error_px = 2, .hysteresis = 0.25f
    });

    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 2, -4000});
    ecs_set(w, cam, LookAt, {0, 0, 0});
    ecs_set(w, cam, Camera, {.fov = 60, .far_ = 100000});
    ecs_entity_t view = ecs_new(w);
    ecs_set(w, view, View, {.camera = cam, .enabled = true});

    ecs_entity_t root = ecs_new(w);
    ecs_set(w, root, Position, {0, 0, 0});
    for (i = 0; i < 4; i ++) {
        parts[i] = ecs_new(w);
        ecs_add_pair(w, parts[i], EcsChildOf, root);
        ecs_set(w, parts[i], Position, {(float)i, 0, 0});
        ecs_set(w, parts[i], Box, {1, 1, 1});
        ecs_set(w, parts[i], Rgb, {200, 100, 50});
    }

    ecs_add(w, root, RootBounds);

    box_packet_init(&p);
    p.width = 1920;
    p.height = 1080;
    box_render_extract(w, &p);
    box_collision_update(w);

    box_packet_reset(&p);
    p.width = 1920;
    p.height = 1080;
    box_render_extract(w, &p);

    box_packet_reset(&p);
    p.width = 1920;
    p.height = 1080;
    box_render_extract(w, &p);

    for (i = 0; i < 4; i ++) {
        test_bool(ecs_get(w, parts[i], RenderInstance)->lod_hidden, true);
    }

    test_int(p.static_updates.count, 0);
    test_int(p.dynamic_instances.count, 1);
    test_int(p.dynamic_batches.count, 1);

    box_instance_t *proxy = box_vec_at(&p.dynamic_instances, 0);
    test_assert(proxy->transform[0] > 3.9f);
    test_assert(proxy->color[0] > proxy->color[2]);

    box_packet_fini(&p);
    ecs_fini(w);
}

void Hierarchy_a_near_root_draws_its_leaves(void)
{
    // lod/hierarchy_traversal
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t p;
    ecs_entity_t parts[4];
    int32_t i;

    ecs_singleton_set(w, LodSettings, {
        .enabled = true, .quality = 1, .error_px = 2, .hysteresis = 0.25f
    });

    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 2, -12});
    ecs_set(w, cam, LookAt, {0, 0, 0});
    ecs_set(w, cam, Camera, {.fov = 60});
    ecs_entity_t view = ecs_new(w);
    ecs_set(w, view, View, {.camera = cam, .enabled = true});

    ecs_entity_t root = ecs_new(w);
    ecs_set(w, root, Position, {0, 0, 0});
    for (i = 0; i < 4; i ++) {
        parts[i] = ecs_new(w);
        ecs_add_pair(w, parts[i], EcsChildOf, root);
        ecs_set(w, parts[i], Position, {(float)i * 3.0f, 0, 0});
        ecs_set(w, parts[i], Box, {1, 1, 1});
    }

    ecs_add(w, root, RootBounds);

    box_packet_init(&p);
    p.width = 1920;
    p.height = 1080;
    box_render_extract(w, &p);
    box_collision_update(w);

    box_packet_reset(&p);
    p.width = 1920;
    p.height = 1080;
    box_render_extract(w, &p);

    for (i = 0; i < 4; i ++) {
        test_bool(ecs_get(w, parts[i], RenderInstance)->lod_hidden, false);
    }

    test_int(p.dynamic_instances.count, 0);
    box_packet_fini(&p);
    ecs_fini(w);
}

void Hierarchy_the_traversal_leaves_the_entities_alone(void)
{
    // lod/hierarchy_traversal
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t p;
    ecs_entity_t parts[4];
    int32_t i;

    ecs_singleton_set(w, LodSettings, {
        .enabled = true, .quality = 1, .error_px = 2, .hysteresis = 0.25f
    });

    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 2, -4000});
    ecs_set(w, cam, LookAt, {0, 0, 0});
    ecs_set(w, cam, Camera, {.fov = 60, .far_ = 100000});
    ecs_entity_t view = ecs_new(w);
    ecs_set(w, view, View, {.camera = cam, .enabled = true});

    ecs_entity_t root = ecs_entity(w, { .name = "tower" });
    ecs_set(w, root, Position, {0, 0, 0});
    for (i = 0; i < 4; i ++) {
        parts[i] = ecs_new(w);
        ecs_add_pair(w, parts[i], EcsChildOf, root);
        ecs_set(w, parts[i], Position, {(float)i, 0, 0});
        ecs_set(w, parts[i], Box, {1, 1, 1});
    }

    ecs_add(w, root, RootBounds);

    box_packet_init(&p);
    p.width = 1920;
    p.height = 1080;
    box_render_extract(w, &p);
    box_collision_update(w);

    box_packet_reset(&p);
    p.width = 1920;
    p.height = 1080;
    box_render_extract(w, &p);
    box_collision_update(w);

    test_str(ecs_get_name(w, root), "tower");
    for (i = 0; i < 4; i ++) {
        test_assert(ecs_is_alive(w, parts[i]));
        test_assert(ecs_has(w, parts[i], Box));
        test_assert(ecs_has(w, parts[i], Collider));
        test_assert(ecs_get_parent(w, parts[i]) == root);
        test_flt(ecs_get(w, parts[i], Position)->x, (float)i);
    }

    box_ray_hit_t hit;
    test_bool(box_collision_ray(w, vec3_new(2, 0, -50), vec3_new(0, 0, 1), 100, &hit), true);
    test_assert(hit.entity == parts[2]);

    box_packet_fini(&p);
    ecs_fini(w);
}

void Hierarchy_settings_off_draws_every_leaf(void)
{
    // lod/hierarchy_traversal
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t p;
    ecs_entity_t parts[4];
    int32_t i;

    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 2, -4000});
    ecs_set(w, cam, LookAt, {0, 0, 0});
    ecs_set(w, cam, Camera, {.fov = 60, .far_ = 100000});
    ecs_entity_t view = ecs_new(w);
    ecs_set(w, view, View, {.camera = cam, .enabled = true});

    ecs_entity_t root = ecs_new(w);
    ecs_set(w, root, Position, {0, 0, 0});
    for (i = 0; i < 4; i ++) {
        parts[i] = ecs_new(w);
        ecs_add_pair(w, parts[i], EcsChildOf, root);
        ecs_set(w, parts[i], Position, {(float)i, 0, 0});
        ecs_set(w, parts[i], Box, {1, 1, 1});
    }

    ecs_add(w, root, RootBounds);

    box_packet_init(&p);
    p.width = 1920;
    p.height = 1080;
    box_render_extract(w, &p);
    box_collision_update(w);

    box_packet_reset(&p);
    p.width = 1920;
    p.height = 1080;
    box_render_extract(w, &p);

    for (i = 0; i < 4; i ++) {
        test_bool(ecs_get(w, parts[i], RenderInstance)->lod_hidden, false);
    }

    test_int(p.dynamic_instances.count, 0);
    box_packet_fini(&p);
    ecs_fini(w);
}

void Hierarchy_turning_the_settings_off_shows_the_leaves_again(void)
{
    // lod/hierarchy_traversal
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t p;
    ecs_entity_t parts[4];
    int32_t i;

    ecs_singleton_set(w, LodSettings, {
        .enabled = true, .quality = 1, .error_px = 2, .hysteresis = 0.25f
    });

    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 2, -4000});
    ecs_set(w, cam, LookAt, {0, 0, 0});
    ecs_set(w, cam, Camera, {.fov = 60, .far_ = 100000});
    ecs_entity_t view = ecs_new(w);
    ecs_set(w, view, View, {.camera = cam, .enabled = true});

    ecs_entity_t root = ecs_new(w);
    ecs_set(w, root, Position, {0, 0, 0});
    for (i = 0; i < 4; i ++) {
        parts[i] = ecs_new(w);
        ecs_add_pair(w, parts[i], EcsChildOf, root);
        ecs_set(w, parts[i], Position, {(float)i, 0, 0});
        ecs_set(w, parts[i], Box, {1, 1, 1});
    }

    ecs_add(w, root, RootBounds);

    box_packet_init(&p);
    p.width = 1920;
    p.height = 1080;
    box_render_extract(w, &p);
    box_collision_update(w);

    box_packet_reset(&p);
    p.width = 1920;
    p.height = 1080;
    box_render_extract(w, &p);
    test_bool(ecs_get(w, parts[0], RenderInstance)->lod_hidden, true);

    ecs_singleton_set(w, LodSettings, {.enabled = false});
    box_packet_reset(&p);
    p.width = 1920;
    p.height = 1080;
    box_render_extract(w, &p);

    for (i = 0; i < 4; i ++) {
        test_bool(ecs_get(w, parts[i], RenderInstance)->lod_hidden, false);
    }

    test_int(p.static_updates.count, 4);
    test_int(p.dynamic_instances.count, 0);
    box_packet_fini(&p);
    ecs_fini(w);
}

void Hierarchy_the_walk_reports_what_it_cost(void)
{
    // lod/hierarchy_traversal
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t p;
    int32_t i;

    ecs_singleton_set(w, LodSettings, {
        .enabled = true, .quality = 1, .error_px = 2, .hysteresis = 0.25f
    });
    ecs_singleton_set(w, LodStats, {0});

    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 2, -4000});
    ecs_set(w, cam, LookAt, {0, 0, 0});
    ecs_set(w, cam, Camera, {.fov = 60, .far_ = 100000});
    ecs_entity_t view = ecs_new(w);
    ecs_set(w, view, View, {.camera = cam, .enabled = true});

    int32_t r;
    for (r = 0; r < 3; r ++) {
        ecs_entity_t root = ecs_new(w);
        ecs_set(w, root, Position, {(float)r * 20.0f, 0, 0});
        for (i = 0; i < 4; i ++) {
            ecs_entity_t part = ecs_new(w);
            ecs_add_pair(w, part, EcsChildOf, root);
            ecs_set(w, part, Position, {(float)i, 0, 0});
            ecs_set(w, part, Box, {1, 1, 1});
        }

        ecs_add(w, root, RootBounds);
    }

    box_packet_init(&p);
    p.width = 1920;
    p.height = 1080;
    box_render_extract(w, &p);
    box_collision_update(w);

    box_packet_reset(&p);
    p.width = 1920;
    p.height = 1080;
    box_render_extract(w, &p);

    const LodStats *stats = ecs_singleton_get(w, LodStats);
    test_assert(stats != NULL);
    test_int(stats->visited, 3);
    test_int(stats->proxies, 3);
    test_int(stats->drawn, 0);
    test_int(stats->nodes, 3);
    box_packet_fini(&p);
    ecs_fini(w);
}

void Hierarchy_a_detail_that_stops_contributing_is_pruned(void)
{
    // lod/detail_pruning
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t p;

    ecs_singleton_set(w, LodSettings, {
        .enabled = true, .quality = 1, .error_px = 2, .hysteresis = 0.25f
    });

    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -200});
    ecs_set(w, cam, LookAt, {0, 0, 0});
    ecs_set(w, cam, Camera, {.fov = 60, .far_ = 4000});
    ecs_entity_t view = ecs_new(w);
    ecs_set(w, view, View, {.camera = cam, .enabled = true});

    ecs_entity_t root = ecs_new(w);
    ecs_set(w, root, Position, {0, 0, 0});

    ecs_entity_t front = ecs_new(w);
    ecs_add_pair(w, front, EcsChildOf, root);
    ecs_set(w, front, Position, {0, 0, 0});
    ecs_set(w, front, Box, {10, 10, 1});
    ecs_set(w, front, Rgb, {140, 140, 140});

    ecs_entity_t back = ecs_new(w);
    ecs_add_pair(w, back, EcsChildOf, root);
    ecs_set(w, back, Position, {0, 0, 40});
    ecs_set(w, back, Box, {10, 10, 1});
    ecs_set(w, back, Rgb, {140, 140, 140});

    ecs_entity_t bolt = ecs_new(w);
    ecs_add_pair(w, bolt, EcsChildOf, root);
    ecs_set(w, bolt, Position, {4, 4, -1});
    ecs_set(w, bolt, Box, {0.4f, 0.4f, 0.4f});
    ecs_set(w, bolt, Rgb, {240, 90, 60});

    ecs_add(w, root, RootBounds);

    box_packet_init(&p);
    p.width = 1920;
    p.height = 1080;
    box_render_extract(w, &p);
    box_collision_update(w);

    box_packet_reset(&p);
    p.width = 1920;
    p.height = 1080;
    box_render_extract(w, &p);
    box_collision_update(w);

    test_bool(ecs_get(w, front, RenderInstance)->lod_hidden, false);
    test_bool(ecs_get(w, back, RenderInstance)->lod_hidden, false);
    test_bool(ecs_get(w, bolt, RenderInstance)->lod_hidden, true);
    test_int(p.dynamic_instances.count, 0);

    box_ray_hit_t hit;
    test_bool(box_collision_ray(w, vec3_new(4, 4, -50), vec3_new(0, 0, 1), 100, &hit), true);
    test_assert(hit.entity == bolt);
    test_assert(ecs_has(w, bolt, Box));
    test_assert(ecs_get_parent(w, bolt) == root);

    box_packet_fini(&p);
    ecs_fini(w);
}

void Hierarchy_a_pinned_detail_survives_pruning(void)
{
    // lod/detail_pruning
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t p;

    ecs_singleton_set(w, LodSettings, {
        .enabled = true, .quality = 1, .error_px = 2, .hysteresis = 0.25f
    });

    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -200});
    ecs_set(w, cam, LookAt, {0, 0, 0});
    ecs_set(w, cam, Camera, {.fov = 60, .far_ = 4000});
    ecs_entity_t view = ecs_new(w);
    ecs_set(w, view, View, {.camera = cam, .enabled = true});

    ecs_entity_t root = ecs_new(w);
    ecs_set(w, root, Position, {0, 0, 0});

    ecs_entity_t front = ecs_new(w);
    ecs_add_pair(w, front, EcsChildOf, root);
    ecs_set(w, front, Position, {0, 0, 0});
    ecs_set(w, front, Box, {10, 10, 1});
    ecs_set(w, front, Rgb, {140, 140, 140});

    ecs_entity_t back = ecs_new(w);
    ecs_add_pair(w, back, EcsChildOf, root);
    ecs_set(w, back, Position, {0, 0, 40});
    ecs_set(w, back, Box, {10, 10, 1});
    ecs_set(w, back, Rgb, {140, 140, 140});

    ecs_entity_t bolt = ecs_new(w);
    ecs_add_pair(w, bolt, EcsChildOf, root);
    ecs_set(w, bolt, Position, {4, 4, -1});
    ecs_set(w, bolt, Box, {0.4f, 0.4f, 0.4f});
    ecs_set(w, bolt, Rgb, {240, 90, 60});

    ecs_entity_t sign = ecs_new(w);
    ecs_add_pair(w, sign, EcsChildOf, root);
    ecs_add(w, sign, LodPin);
    ecs_set(w, sign, Position, {-4, 4, -1});
    ecs_set(w, sign, Box, {0.4f, 0.4f, 0.4f});
    ecs_set(w, sign, Rgb, {240, 220, 80});

    ecs_add(w, root, RootBounds);

    box_packet_init(&p);
    p.width = 1920;
    p.height = 1080;
    box_render_extract(w, &p);
    box_collision_update(w);

    box_packet_reset(&p);
    p.width = 1920;
    p.height = 1080;
    box_render_extract(w, &p);

    test_bool(ecs_get(w, bolt, RenderInstance)->lod_hidden, true);
    test_bool(ecs_get(w, sign, RenderInstance)->lod_hidden, false);
    box_packet_fini(&p);
    ecs_fini(w);
}

void Hierarchy_the_walk_reports_what_it_pruned(void)
{
    // lod/detail_pruning
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, Box);
    box_packet_t p;
    int32_t i;

    ecs_singleton_set(w, LodSettings, {
        .enabled = true, .quality = 1, .error_px = 2, .hysteresis = 0.25f
    });
    ecs_singleton_set(w, LodStats, {0});

    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 0, -200});
    ecs_set(w, cam, LookAt, {0, 0, 0});
    ecs_set(w, cam, Camera, {.fov = 60, .far_ = 4000});
    ecs_entity_t view = ecs_new(w);
    ecs_set(w, view, View, {.camera = cam, .enabled = true});

    ecs_entity_t root = ecs_new(w);
    ecs_set(w, root, Position, {0, 0, 0});

    ecs_entity_t front = ecs_new(w);
    ecs_add_pair(w, front, EcsChildOf, root);
    ecs_set(w, front, Position, {0, 0, 0});
    ecs_set(w, front, Box, {10, 10, 1});
    ecs_set(w, front, Rgb, {140, 140, 140});

    ecs_entity_t back = ecs_new(w);
    ecs_add_pair(w, back, EcsChildOf, root);
    ecs_set(w, back, Position, {0, 0, 40});
    ecs_set(w, back, Box, {10, 10, 1});
    ecs_set(w, back, Rgb, {140, 140, 140});

    for (i = 0; i < 5; i ++) {
        ecs_entity_t bolt = ecs_new(w);
        ecs_add_pair(w, bolt, EcsChildOf, root);
        ecs_set(w, bolt, Position, {(float)i - 2.0f, 4, -1});
        ecs_set(w, bolt, Box, {0.4f, 0.4f, 0.4f});
        ecs_set(w, bolt, Rgb, {240, 90, 60});
    }

    ecs_add(w, root, RootBounds);

    box_packet_init(&p);
    p.width = 1920;
    p.height = 1080;
    box_render_extract(w, &p);
    box_collision_update(w);

    box_packet_reset(&p);
    p.width = 1920;
    p.height = 1080;
    box_render_extract(w, &p);

    const LodStats *stats = ecs_singleton_get(w, LodStats);
    test_assert(stats != NULL);
    test_int(stats->pruned, 5);
    test_int(stats->drawn, 2);
    test_int(stats->proxies, 0);
    box_packet_fini(&p);
    ecs_fini(w);
}

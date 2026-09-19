/**
 * @file Cache.c
 * @brief Tests that generated proxies are cached, invalidated and built on jobs.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/render/render_internal.h"
#include "../../../src/render/lod_cache.h"
#include "../../../src/job/job_internal.h"

void Cache_a_subtree_keeps_its_parts_until_its_proxy_arrives(void)
{
    // lod/cache_and_jobs
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    ECS_IMPORT(w, BoxCollision);
    box_packet_t p;
    ecs_entity_t parts[24];
    int32_t i;

    ecs_singleton_set(w, LodSettings, {
        .enabled = true, .quality = 1, .error_px = 2, .hysteresis = 0.25f
    });

    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 2, -400});
    ecs_set(w, cam, LookAt, {10, 10, 10});
    ecs_set(w, cam, Camera, {.fov = 60, .far_ = 100000});

    ecs_entity_t root = ecs_new(w);
    ecs_set(w, root, Position, {0, 0, 0});
    for (i = 0; i < 24; i ++) {
        parts[i] = ecs_new(w);
        ecs_add_pair(w, parts[i], EcsChildOf, root);
        ecs_set(w, parts[i], Position, {
            (float)((i / 3) % 2) * 20 + (float)(i % 3),
            (float)((i / 6) % 2) * 20, (float)(i / 12) * 20
        });
        ecs_set(w, parts[i], Box, {1, 1, 1});
        ecs_set(w, parts[i], Rgb, {200, 100, 50});
    }

    ecs_add(w, root, LodPin);
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
    test_int(p.dynamic_instances.count, 0);
    test_bool(ecs_get(w, parts[0], RenderInstance)->lod_hidden, false);

    box_packet_reset(&p);
    p.width = 1920;
    p.height = 1080;
    box_render_extract(w, &p);
    test_assert(p.dynamic_instances.count > 1);
    test_bool(ecs_get(w, parts[0], RenderInstance)->lod_hidden, true);

    box_packet_fini(&p);
    ecs_fini(w);
}

void Cache_a_clustered_subtree_is_replaced_by_its_primitive_set(void)
{
    // lod/cache_and_jobs
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    ECS_IMPORT(w, BoxCollision);
    box_packet_t p;
    int32_t i;

    ecs_singleton_set(w, LodSettings, {
        .enabled = true, .quality = 1, .error_px = 2, .hysteresis = 0.25f
    });
    ecs_singleton_set(w, LodStats, {0});

    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 2, -400});
    ecs_set(w, cam, LookAt, {10, 10, 10});
    ecs_set(w, cam, Camera, {.fov = 60, .far_ = 100000});

    ecs_entity_t root = ecs_new(w);
    ecs_set(w, root, Position, {0, 0, 0});
    for (i = 0; i < 24; i ++) {
        ecs_entity_t part = ecs_new(w);

        ecs_add_pair(w, part, EcsChildOf, root);
        ecs_set(w, part, Position, {
            (float)((i / 3) % 2) * 20 + (float)(i % 3),
            (float)((i / 6) % 2) * 20, (float)(i / 12) * 20
        });
        ecs_set(w, part, Box, {1, 1, 1});
        ecs_set(w, part, Rgb, {200, 100, 50});
    }

    ecs_add(w, root, RootBounds);
    box_packet_init(&p);
    for (i = 0; i < 4; i ++) {
        box_packet_reset(&p);
        p.width = 1920;
        p.height = 1080;
        box_render_extract(w, &p);
        box_collision_update(w);
    }

    const box_lod_cache_t *cache = box_render_lod_cache(box_render_state(w));
    const box_lod_entry_t *entry = box_lod_cache_find(cache, root);

    test_assert(entry != NULL);
    test_bool(entry->ready, true);
    test_assert(entry->shape_count > 1);
    test_assert(entry->shape_count <= BOX_LOD_CACHE_SHAPES);
    test_int(ecs_singleton_get(w, LodStats)->shapes, 1);
    test_int(ecs_singleton_get(w, LodStats)->boxes, 0);
    test_int(ecs_singleton_get(w, LodStats)->cached, 1);

    box_packet_fini(&p);
    ecs_fini(w);
}

void Cache_many_materials_fall_back_to_a_merged_mesh(void)
{
    // lod/cache_and_jobs
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    ECS_IMPORT(w, BoxCollision);
    box_packet_t p;
    int32_t i;

    ecs_singleton_set(w, LodSettings, {
        .enabled = true, .quality = 1, .error_px = 2, .hysteresis = 0.25f
    });
    ecs_singleton_set(w, LodStats, {0});

    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 2, -400});
    ecs_set(w, cam, LookAt, {10, 10, 10});
    ecs_set(w, cam, Camera, {.fov = 60, .far_ = 100000});

    ecs_entity_t root = ecs_new(w);
    ecs_set(w, root, Position, {0, 0, 0});
    for (i = 0; i < 24; i ++) {
        ecs_entity_t part = ecs_new(w);

        ecs_add_pair(w, part, EcsChildOf, root);
        ecs_set(w, part, Position, {
            (float)((i / 3) % 2) * 20 + (float)(i % 3),
            (float)((i / 6) % 2) * 20, (float)(i / 12) * 20
        });
        ecs_set(w, part, Box, {1, 1, 1});
        ecs_set(w, part, Rgb, {(unsigned char)(10 + i * 10), 100, 50});
    }

    ecs_add(w, root, RootBounds);
    box_packet_init(&p);
    for (i = 0; i < 4; i ++) {
        box_packet_reset(&p);
        p.width = 1920;
        p.height = 1080;
        box_render_extract(w, &p);
        box_collision_update(w);
    }

    const box_lod_cache_t *cache = box_render_lod_cache(box_render_state(w));
    const box_lod_entry_t *entry = box_lod_cache_find(cache, root);

    test_assert(entry != NULL);
    test_assert(entry->shape_count > BOX_LOD_CACHE_SHAPES);
    test_assert(entry->mesh != 0);
    test_int(ecs_singleton_get(w, LodStats)->meshes, 1);
    test_int(ecs_singleton_get(w, LodStats)->shapes, 0);

    box_packet_fini(&p);
    ecs_fini(w);
}

void Cache_a_node_that_fills_the_view_keeps_its_parts(void)
{
    // lod/cache_and_jobs
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    ECS_IMPORT(w, BoxCollision);
    box_packet_t p;
    ecs_entity_t parts[24];
    int32_t i;

    ecs_singleton_set(w, LodSettings, {
        .enabled = true, .quality = 1, .error_px = 2, .hysteresis = 0.25f
    });

    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 2, -40});
    ecs_set(w, cam, LookAt, {10, 10, 10});
    ecs_set(w, cam, Camera, {.fov = 60, .far_ = 100000});

    ecs_entity_t root = ecs_new(w);
    ecs_set(w, root, Position, {0, 0, 0});
    for (i = 0; i < 24; i ++) {
        parts[i] = ecs_new(w);
        ecs_add_pair(w, parts[i], EcsChildOf, root);
        ecs_set(w, parts[i], Position, {
            (float)((i / 3) % 2) * 20 + (float)(i % 3),
            (float)((i / 6) % 2) * 20, (float)(i / 12) * 20
        });
        ecs_set(w, parts[i], Box, {1, 1, 1});
        ecs_set(w, parts[i], Rgb, {200, 100, 50});
    }

    ecs_add(w, root, LodPin);
    ecs_add(w, root, RootBounds);
    box_packet_init(&p);
    for (i = 0; i < 5; i ++) {
        box_packet_reset(&p);
        p.width = 1920;
        p.height = 1080;
        box_render_extract(w, &p);
        box_collision_update(w);
    }

    const box_lod_cache_t *cache = box_render_lod_cache(box_render_state(w));

    test_assert(box_lod_cache_find(cache, root) == NULL);
    test_int(p.dynamic_instances.count, 0);
    test_bool(ecs_get(w, parts[0], RenderInstance)->lod_hidden, false);

    box_packet_fini(&p);
    ecs_fini(w);
}

void Cache_moving_a_rigid_root_builds_nothing_again(void)
{
    // lod/cache_and_jobs
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    ECS_IMPORT(w, BoxCollision);
    box_packet_t p;
    int32_t i;

    ecs_singleton_set(w, LodSettings, {
        .enabled = true, .quality = 1, .error_px = 2, .hysteresis = 0.25f
    });

    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 2, -400});
    ecs_set(w, cam, LookAt, {10, 10, 10});
    ecs_set(w, cam, Camera, {.fov = 60, .far_ = 100000});

    ecs_entity_t root = ecs_new(w);
    ecs_set(w, root, Position, {0, 0, 0});
    for (i = 0; i < 24; i ++) {
        ecs_entity_t part = ecs_new(w);

        ecs_add_pair(w, part, EcsChildOf, root);
        ecs_set(w, part, Position, {
            (float)((i / 3) % 2) * 20 + (float)(i % 3),
            (float)((i / 6) % 2) * 20, (float)(i / 12) * 20
        });
        ecs_set(w, part, Box, {1, 1, 1});
        ecs_set(w, part, Rgb, {200, 100, 50});
    }

    ecs_add(w, root, RootBounds);
    box_packet_init(&p);
    for (i = 0; i < 4; i ++) {
        box_packet_reset(&p);
        p.width = 1920;
        p.height = 1080;
        box_render_extract(w, &p);
        box_collision_update(w);
    }

    box_lod_cache_t *cache = box_render_lod_cache(box_render_state(w));
    int32_t builds = cache->builds;
    uint32_t version = box_lod_cache_find(cache, root)->version;

    test_assert(builds > 0);
    ecs_set(w, root, Position, {60, 0, 0});
    for (i = 0; i < 3; i ++) {
        box_packet_reset(&p);
        p.width = 1920;
        p.height = 1080;
        box_render_extract(w, &p);
        box_collision_update(w);
    }

    test_int(cache->builds, builds);
    test_int(box_lod_cache_find(cache, root)->version, version);
    test_assert(p.dynamic_instances.count > 1);

    box_instance_t *proxy = box_vec_at(&p.dynamic_instances, 0);
    test_assert(proxy->transform[3] > 55.0f);

    box_packet_fini(&p);
    ecs_fini(w);
}

void Cache_a_changed_descendant_builds_its_proxy_again(void)
{
    // lod/cache_and_jobs
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    ECS_IMPORT(w, BoxCollision);
    box_packet_t p;
    ecs_entity_t parts[24];
    int32_t i;

    ecs_singleton_set(w, LodSettings, {
        .enabled = true, .quality = 1, .error_px = 2, .hysteresis = 0.25f
    });

    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 2, -400});
    ecs_set(w, cam, LookAt, {10, 10, 10});
    ecs_set(w, cam, Camera, {.fov = 60, .far_ = 100000});

    ecs_entity_t root = ecs_new(w);
    ecs_set(w, root, Position, {0, 0, 0});
    for (i = 0; i < 24; i ++) {
        parts[i] = ecs_new(w);
        ecs_add_pair(w, parts[i], EcsChildOf, root);
        ecs_set(w, parts[i], Position, {
            (float)((i / 3) % 2) * 20 + (float)(i % 3),
            (float)((i / 6) % 2) * 20, (float)(i / 12) * 20
        });
        ecs_set(w, parts[i], Box, {1, 1, 1});
        ecs_set(w, parts[i], Rgb, {200, 100, 50});
    }

    ecs_add(w, root, RootBounds);
    box_packet_init(&p);
    for (i = 0; i < 4; i ++) {
        box_packet_reset(&p);
        p.width = 1920;
        p.height = 1080;
        box_render_extract(w, &p);
        box_collision_update(w);
    }

    box_lod_cache_t *cache = box_render_lod_cache(box_render_state(w));
    int32_t builds = cache->builds;

    ecs_set(w, parts[0], Rgb, {10, 220, 30});
    for (i = 0; i < 3; i ++) {
        box_packet_reset(&p);
        p.width = 1920;
        p.height = 1080;
        box_render_extract(w, &p);
        box_collision_update(w);
    }

    test_assert(cache->builds > builds);
    test_int(box_lod_cache_find(cache, root)->version, 2);

    box_packet_fini(&p);
    ecs_fini(w);
}

void Cache_a_change_in_another_root_leaves_the_proxy_alone(void)
{
    // lod/cache_and_jobs
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    ECS_IMPORT(w, BoxCollision);
    box_packet_t p;
    ecs_entity_t roots[2];
    ecs_entity_t other = 0;
    int32_t i, j;

    ecs_singleton_set(w, LodSettings, {
        .enabled = true, .quality = 1, .error_px = 2, .hysteresis = 0.25f
    });

    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 2, -400});
    ecs_set(w, cam, LookAt, {10, 10, 10});
    ecs_set(w, cam, Camera, {.fov = 60, .far_ = 100000});

    for (j = 0; j < 2; j ++) {
        roots[j] = ecs_new(w);
        ecs_set(w, roots[j], Position, {(float)j * 60, 0, 0});
        for (i = 0; i < 24; i ++) {
            ecs_entity_t part = ecs_new(w);

            ecs_add_pair(w, part, EcsChildOf, roots[j]);
            ecs_set(w, part, Position, {
                (float)((i / 3) % 2) * 20 + (float)(i % 3),
                (float)((i / 6) % 2) * 20, (float)(i / 12) * 20
            });
            ecs_set(w, part, Box, {1, 1, 1});
            ecs_set(w, part, Rgb, {200, 100, 50});
            if (j == 1 && i == 0) {
                other = part;
            }
        }

        ecs_add(w, roots[j], RootBounds);
    }

    box_packet_init(&p);
    for (i = 0; i < 5; i ++) {
        box_packet_reset(&p);
        p.width = 1920;
        p.height = 1080;
        box_render_extract(w, &p);
        box_collision_update(w);
    }

    box_lod_cache_t *cache = box_render_lod_cache(box_render_state(w));
    uint32_t kept = box_lod_cache_find(cache, roots[0])->version;
    uint32_t changed = box_lod_cache_find(cache, roots[1])->version;

    ecs_set(w, other, Rgb, {10, 220, 30});
    for (i = 0; i < 3; i ++) {
        box_packet_reset(&p);
        p.width = 1920;
        p.height = 1080;
        box_render_extract(w, &p);
        box_collision_update(w);
    }

    test_int(box_lod_cache_find(cache, roots[0])->version, kept);
    test_assert(box_lod_cache_find(cache, roots[1])->version > changed);

    box_packet_fini(&p);
    ecs_fini(w);
}

void Cache_destroying_a_root_frees_its_proxy(void)
{
    // lod/cache_and_jobs
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    ECS_IMPORT(w, BoxCollision);
    box_packet_t p;
    int32_t i;

    ecs_singleton_set(w, LodSettings, {
        .enabled = true, .quality = 1, .error_px = 2, .hysteresis = 0.25f
    });

    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 2, -400});
    ecs_set(w, cam, LookAt, {10, 10, 10});
    ecs_set(w, cam, Camera, {.fov = 60, .far_ = 100000});

    ecs_entity_t root = ecs_new(w);
    ecs_set(w, root, Position, {0, 0, 0});
    for (i = 0; i < 24; i ++) {
        ecs_entity_t part = ecs_new(w);

        ecs_add_pair(w, part, EcsChildOf, root);
        ecs_set(w, part, Position, {
            (float)((i / 3) % 2) * 20 + (float)(i % 3),
            (float)((i / 6) % 2) * 20, (float)(i / 12) * 20
        });
        ecs_set(w, part, Box, {1, 1, 1});
        ecs_set(w, part, Rgb, {200, 100, 50});
    }

    ecs_add(w, root, RootBounds);
    box_packet_init(&p);
    for (i = 0; i < 4; i ++) {
        box_packet_reset(&p);
        p.width = 1920;
        p.height = 1080;
        box_render_extract(w, &p);
        box_collision_update(w);
    }

    box_lod_cache_t *cache = box_render_lod_cache(box_render_state(w));
    test_assert(box_lod_cache_find(cache, root) != NULL);

    ecs_delete(w, root);
    for (i = 0; i < 3; i ++) {
        box_packet_reset(&p);
        p.width = 1920;
        p.height = 1080;
        box_render_extract(w, &p);
        box_collision_update(w);
    }

    test_assert(box_lod_cache_find(cache, root) == NULL);
    test_int(ecs_map_count(&cache->entries), 0);

    box_packet_fini(&p);
    ecs_fini(w);
}

void Cache_the_job_system_builds_the_proxy_off_the_frame(void)
{
    // lod/cache_and_jobs
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxJob);
    ECS_IMPORT(w, BoxGeometry);
    ECS_IMPORT(w, BoxCollision);
    box_packet_t p;
    int32_t i;

    ecs_singleton_set(w, LodSettings, {
        .enabled = true, .quality = 1, .error_px = 2, .hysteresis = 0.25f
    });
    ecs_singleton_set(w, LodStats, {0});

    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 2, -400});
    ecs_set(w, cam, LookAt, {10, 10, 10});
    ecs_set(w, cam, Camera, {.fov = 60, .far_ = 100000});

    ecs_entity_t root = ecs_new(w);
    ecs_set(w, root, Position, {0, 0, 0});
    for (i = 0; i < 24; i ++) {
        ecs_entity_t part = ecs_new(w);

        ecs_add_pair(w, part, EcsChildOf, root);
        ecs_set(w, part, Position, {
            (float)((i / 3) % 2) * 20 + (float)(i % 3),
            (float)((i / 6) % 2) * 20, (float)(i / 12) * 20
        });
        ecs_set(w, part, Box, {1, 1, 1});
        ecs_set(w, part, Rgb, {200, 100, 50});
    }

    ecs_add(w, root, RootBounds);
    box_packet_init(&p);
    for (i = 0; i < 2; i ++) {
        box_packet_reset(&p);
        p.width = 1920;
        p.height = 1080;
        box_render_extract(w, &p);
        box_collision_update(w);
    }

    box_lod_cache_t *cache = box_render_lod_cache(box_render_state(w));
    test_int(cache->builds, 1);
    test_bool(box_lod_cache_find(cache, root)->ready, false);
    test_int(ecs_singleton_get(w, LodStats)->queued, 1);
    test_int(p.dynamic_instances.count, 0);

    box_job_flush(w);
    test_int(cache->queued, 0);
    test_bool(box_lod_cache_find(cache, root)->ready, true);

    box_packet_reset(&p);
    p.width = 1920;
    p.height = 1080;
    box_render_extract(w, &p);
    test_assert(p.dynamic_instances.count > 1);

    box_packet_fini(&p);
    ecs_fini(w);
}

void Cache_the_nearest_subtree_is_built_first(void)
{
    // lod/cache_and_jobs
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    ECS_IMPORT(w, BoxCollision);
    box_packet_t p;
    ecs_entity_t roots[6];
    int32_t i, j;

    ecs_singleton_set(w, LodSettings, {
        .enabled = true, .quality = 1, .error_px = 2, .hysteresis = 0.25f
    });

    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 2, -400});
    ecs_set(w, cam, LookAt, {0, 0, 1});
    ecs_set(w, cam, Camera, {.fov = 60, .far_ = 100000});

    for (j = 0; j < 6; j ++) {
        roots[j] = ecs_new(w);
        ecs_set(w, roots[j], Position, {0, 0, (float)j * 500});
        for (i = 0; i < 24; i ++) {
            ecs_entity_t part = ecs_new(w);

            ecs_add_pair(w, part, EcsChildOf, roots[j]);
            ecs_set(w, part, Position, {
                (float)((i / 3) % 2) * 20 + (float)(i % 3),
                (float)((i / 6) % 2) * 20, (float)(i / 12) * 20
            });
            ecs_set(w, part, Box, {1, 1, 1});
            ecs_set(w, part, Rgb, {200, 100, 50});
        }

        ecs_add(w, roots[j], RootBounds);
    }

    box_packet_init(&p);
    for (i = 0; i < 2; i ++) {
        box_packet_reset(&p);
        p.width = 1920;
        p.height = 1080;
        box_render_extract(w, &p);
        box_collision_update(w);
    }

    const box_lod_cache_t *cache = box_render_lod_cache(box_render_state(w));

    test_int(ecs_map_count(&cache->entries), BOX_LOD_CACHE_STARTS);
    for (j = 0; j < BOX_LOD_CACHE_STARTS; j ++) {
        test_assert(box_lod_cache_find(cache, roots[j]) != NULL);
    }

    test_assert(box_lod_cache_find(cache, roots[5]) == NULL);

    box_packet_fini(&p);
    ecs_fini(w);
}

void Cache_the_statistics_report_the_proxies_and_the_queue(void)
{
    // lod/cache_and_jobs
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    ECS_IMPORT(w, BoxCollision);
    box_packet_t p;
    int32_t i;

    ecs_singleton_set(w, LodSettings, {
        .enabled = true, .quality = 1, .error_px = 2, .hysteresis = 0.25f
    });
    ecs_singleton_set(w, LodStats, {0});

    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 2, -4000});
    ecs_set(w, cam, LookAt, {10, 10, 10});
    ecs_set(w, cam, Camera, {.fov = 60, .far_ = 100000});

    ecs_entity_t root = ecs_new(w);
    ecs_set(w, root, Position, {0, 0, 0});
    for (i = 0; i < 24; i ++) {
        ecs_entity_t part = ecs_new(w);

        ecs_add_pair(w, part, EcsChildOf, root);
        ecs_set(w, part, Position, {
            (float)((i / 3) % 2) * 20 + (float)(i % 3),
            (float)((i / 6) % 2) * 20, (float)(i / 12) * 20
        });
        ecs_set(w, part, Box, {1, 1, 1});
        ecs_set(w, part, Rgb, {200, 100, 50});
    }

    ecs_add(w, root, RootBounds);
    box_packet_init(&p);
    box_render_extract(w, &p);
    box_collision_update(w);

    box_packet_reset(&p);
    p.width = 1920;
    p.height = 1080;
    box_render_extract(w, &p);

    test_int(ecs_singleton_get(w, LodStats)->boxes, 1);
    test_int(ecs_singleton_get(w, LodStats)->shapes, 0);
    test_int(ecs_singleton_get(w, LodStats)->proxies, 1);
    test_int(ecs_singleton_get(w, LodStats)->nodes, 1);

    box_packet_reset(&p);
    p.width = 1920;
    p.height = 1080;
    box_render_extract(w, &p);

    test_int(ecs_singleton_get(w, LodStats)->boxes, 1);
    test_int(ecs_singleton_get(w, LodStats)->cached, 1);
    test_int(ecs_singleton_get(w, LodStats)->queued, 0);

    box_packet_fini(&p);
    ecs_fini(w);
}

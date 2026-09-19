/**
 * @file Impostor.c
 * @brief Tests the atlas, the capture and the billboard of an impostor.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/render/render_internal.h"
#include "../../../src/render/lod_cache.h"

void Impostor_the_error_falls_with_the_size_of_the_subtree(void)
{
    // lod/hlod_proxies/impostors
    float small = box_lod_impostor_error(vec3_new(0, 0, 0), vec3_new(2, 4, 2));
    float large = box_lod_impostor_error(vec3_new(0, 0, 0), vec3_new(20, 40, 20));
    float size[2];

    test_assert(small > 0.0f);
    test_assert(large > small * 9.0f);
    box_lod_impostor_size(vec3_new(0, 0, 0), vec3_new(6, 4, 8), size);
    test_assert(size[0] > 9.9f);
    test_assert(size[0] < 10.5f);
    test_assert(size[1] > 4.1f);
    test_assert(size[1] < 4.3f);
}

void Impostor_the_billboard_picks_the_tile_the_eye_stands_at(void)
{
    // lod/hlod_proxies/impostors
    box_lod_billboard_t bb = {
        .center = vec3_new(0, 0, 0), .axis = vec3_new(0, 0, -1), .radius = 1
    };

    test_int(box_lod_impostor_pick(&bb, vec3_new(0, 0, -10)), 0);
    test_int(box_lod_impostor_pick(&bb, vec3_new(-10, 0, 0)),
        BOX_LOD_IMPOSTOR_VIEWS / 4);
    test_int(box_lod_impostor_pick(&bb, vec3_new(0, 0, 10)),
        BOX_LOD_IMPOSTOR_VIEWS / 2);
    test_int(box_lod_impostor_pick(&bb, vec3_new(10, 0, 0)),
        BOX_LOD_IMPOSTOR_VIEWS * 3 / 4);
    test_int(box_lod_impostor_pick(&bb, vec3_new(0, 40, -10)), 0);
}

void Impostor_a_turned_node_picks_the_tile_of_its_own_axis(void)
{
    // lod/hlod_proxies/impostors
    box_lod_billboard_t bb = {
        .center = vec3_new(0, 0, 0), .axis = vec3_new(-1, 0, 0), .radius = 1
    };

    test_int(box_lod_impostor_pick(&bb, vec3_new(-10, 0, 0)), 0);
    test_int(box_lod_impostor_pick(&bb, vec3_new(0, 0, 10)),
        BOX_LOD_IMPOSTOR_VIEWS / 4);
}

void Impostor_a_sparse_subtree_is_given_an_atlas(void)
{
    // lod/hlod_proxies/impostors
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    ECS_IMPORT(w, BoxCollision);
    box_packet_t p;
    int32_t i;

    ecs_singleton_set(w, LodSettings, {
        .enabled = true, .quality = 1, .error_px = 4, .hysteresis = 0.25f
    });
    ecs_singleton_set(w, LodStats, {0});

    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 8, 0});
    ecs_set(w, cam, LookAt, {0, 8, 800});
    ecs_set(w, cam, Camera, {.fov = 60, .far_ = 100000});

    ecs_entity_t root = ecs_new(w);
    ecs_set(w, root, Position, {0, 0, 800});
    for (i = 0; i < 8; i ++) {
        ecs_entity_t mast = ecs_new(w);

        ecs_add_pair(w, mast, EcsChildOf, root);
        ecs_set(w, mast, Position, {(float)(i % 4) * 8.6f - 12.9f, 13,
            (float)(i / 4) * 26.0f - 13.0f});
        ecs_set(w, mast, Box, {0.6f, 26, 0.6f});
        ecs_set(w, mast, Rgb, {206, 138, 84});
    }

    ecs_entity_t hut = ecs_new(w);
    ecs_add_pair(w, hut, EcsChildOf, root);
    ecs_set(w, hut, Position, {0, 2.5f, 0});
    ecs_set(w, hut, Box, {6, 5, 6});
    ecs_set(w, hut, Rgb, {110, 190, 230});
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
    test_assert(entry->atlas != 0);
    test_bool(entry->captured, true);
    test_int(cache->atlases, 1);
    test_int(ecs_singleton_get(w, LodStats)->impostors, 1);

    box_packet_fini(&p);
    ecs_fini(w);
}

void Impostor_the_capture_renders_every_tile_before_the_frame(void)
{
    // lod/hlod_proxies/impostors
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    ECS_IMPORT(w, BoxCollision);
    box_packet_t p;
    int32_t i;

    ecs_singleton_set(w, LodSettings, {
        .enabled = true, .quality = 1, .error_px = 4, .hysteresis = 0.25f
    });

    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 8, 0});
    ecs_set(w, cam, LookAt, {0, 8, 800});
    ecs_set(w, cam, Camera, {.fov = 60, .far_ = 100000});

    ecs_entity_t root = ecs_new(w);
    ecs_set(w, root, Position, {0, 0, 800});
    for (i = 0; i < 8; i ++) {
        ecs_entity_t mast = ecs_new(w);

        ecs_add_pair(w, mast, EcsChildOf, root);
        ecs_set(w, mast, Position, {(float)(i % 4) * 8.6f - 12.9f, 13,
            (float)(i / 4) * 26.0f - 13.0f});
        ecs_set(w, mast, Box, {0.6f, 26, 0.6f});
        ecs_set(w, mast, Rgb, {206, 138, 84});
    }

    ecs_entity_t hut = ecs_new(w);
    ecs_add_pair(w, hut, EcsChildOf, root);
    ecs_set(w, hut, Position, {0, 2.5f, 0});
    ecs_set(w, hut, Box, {6, 5, 6});
    ecs_set(w, hut, Rgb, {110, 190, 230});
    ecs_add(w, root, RootBounds);

    const box_lod_cache_t *cache = box_render_lod_cache(box_render_state(w));
    box_packet_init(&p);
    for (i = 0; i < 6 && !cache->captures; i ++) {
        box_packet_reset(&p);
        p.width = 1920;
        p.height = 1080;
        box_render_extract(w, &p);
        box_collision_update(w);
    }

    const box_lod_entry_t *entry = box_lod_cache_find(cache, root);
    box_view_t *first = box_vec_at(&p.views, 0);

    test_int(cache->captures, 1);
    test_assert(p.views.count > 1);
    test_int(first->target, entry->atlas);
    test_int(first->strict_layers, 1);
    test_int(first->layer_mask, 0);
    test_int(first->width, BOX_LOD_IMPOSTOR_TILE * BOX_LOD_IMPOSTOR_VIEWS);
    test_int(first->height, BOX_LOD_IMPOSTOR_TILE);
    int32_t parts = ecs_vec_count(&entry->parts);

    test_assert(parts > 0);
    test_int(first->dynamic_count, (uint32_t)(BOX_LOD_IMPOSTOR_VIEWS * parts));
    test_int(((box_view_t*)box_vec_at(&p.views, p.views.count - 1))->target, 0);

    box_packet_fini(&p);
    ecs_fini(w);
}

void Impostor_the_billboard_is_one_sorted_draw_of_the_atlas(void)
{
    // lod/hlod_proxies/impostors
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    ECS_IMPORT(w, BoxCollision);
    box_packet_t p;
    int32_t i;

    ecs_singleton_set(w, LodSettings, {
        .enabled = true, .quality = 1, .error_px = 4, .hysteresis = 0.25f
    });

    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 8, 0});
    ecs_set(w, cam, LookAt, {0, 8, 800});
    ecs_set(w, cam, Camera, {.fov = 60, .far_ = 100000});

    ecs_entity_t root = ecs_new(w);
    ecs_set(w, root, Position, {0, 0, 800});
    for (i = 0; i < 8; i ++) {
        ecs_entity_t mast = ecs_new(w);

        ecs_add_pair(w, mast, EcsChildOf, root);
        ecs_set(w, mast, Position, {(float)(i % 4) * 8.6f - 12.9f, 13,
            (float)(i / 4) * 26.0f - 13.0f});
        ecs_set(w, mast, Box, {0.6f, 26, 0.6f});
        ecs_set(w, mast, Rgb, {206, 138, 84});
    }

    ecs_entity_t hut = ecs_new(w);
    ecs_add_pair(w, hut, EcsChildOf, root);
    ecs_set(w, hut, Position, {0, 2.5f, 0});
    ecs_set(w, hut, Box, {6, 5, 6});
    ecs_set(w, hut, Rgb, {110, 190, 230});
    ecs_add(w, root, RootBounds);

    const box_lod_cache_t *cache = box_render_lod_cache(box_render_state(w));
    box_packet_init(&p);
    for (i = 0; i < 6 && !cache->captures; i ++) {
        box_packet_reset(&p);
        p.width = 1920;
        p.height = 1080;
        box_render_extract(w, &p);
        box_collision_update(w);
    }

    const box_lod_entry_t *entry = box_lod_cache_find(cache, root);

    test_int(p.transparent.count, 1);

    box_transparent_t *draw = box_vec_at(&p.transparent, 0);

    test_int(draw->group, entry->atlas);
    test_assert(draw->mesh != 0);
    test_assert(draw->depth > 700.0f);
    test_int(p.dynamic_instances.count,
        (uint32_t)(BOX_LOD_IMPOSTOR_VIEWS * ecs_vec_count(&entry->parts)));

    box_packet_fini(&p);
    ecs_fini(w);
}

void Impostor_a_changed_descendant_captures_the_atlas_again(void)
{
    // lod/hlod_proxies/impostors
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    ECS_IMPORT(w, BoxCollision);
    box_packet_t p;
    ecs_entity_t masts[8];
    int32_t i;

    ecs_singleton_set(w, LodSettings, {
        .enabled = true, .quality = 1, .error_px = 4, .hysteresis = 0.25f
    });

    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 8, 0});
    ecs_set(w, cam, LookAt, {0, 8, 800});
    ecs_set(w, cam, Camera, {.fov = 60, .far_ = 100000});

    ecs_entity_t root = ecs_new(w);
    ecs_set(w, root, Position, {0, 0, 800});
    for (i = 0; i < 8; i ++) {
        masts[i] = ecs_new(w);
        ecs_add_pair(w, masts[i], EcsChildOf, root);
        ecs_set(w, masts[i], Position, {(float)(i % 4) * 8.6f - 12.9f, 13,
            (float)(i / 4) * 26.0f - 13.0f});
        ecs_set(w, masts[i], Box, {0.6f, 26, 0.6f});
        ecs_set(w, masts[i], Rgb, {206, 138, 84});
    }

    ecs_entity_t hut = ecs_new(w);
    ecs_add_pair(w, hut, EcsChildOf, root);
    ecs_set(w, hut, Position, {0, 2.5f, 0});
    ecs_set(w, hut, Box, {6, 5, 6});
    ecs_set(w, hut, Rgb, {110, 190, 230});
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
    int32_t captures = cache->captures;

    test_assert(captures > 0);
    for (i = 0; i < 2; i ++) {
        box_packet_reset(&p);
        p.width = 1920;
        p.height = 1080;
        box_render_extract(w, &p);
        box_collision_update(w);
    }

    test_int(cache->captures, captures);
    ecs_set(w, masts[0], Rgb, {20, 220, 40});
    for (i = 0; i < 3; i ++) {
        box_packet_reset(&p);
        p.width = 1920;
        p.height = 1080;
        box_render_extract(w, &p);
        box_collision_update(w);
    }

    test_assert(cache->captures > captures);

    box_packet_fini(&p);
    ecs_fini(w);
}

void Impostor_destroying_a_root_frees_its_atlas(void)
{
    // lod/hlod_proxies/impostors
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    ECS_IMPORT(w, BoxCollision);
    box_packet_t p;
    int32_t i;

    ecs_singleton_set(w, LodSettings, {
        .enabled = true, .quality = 1, .error_px = 4, .hysteresis = 0.25f
    });

    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 8, 0});
    ecs_set(w, cam, LookAt, {0, 8, 800});
    ecs_set(w, cam, Camera, {.fov = 60, .far_ = 100000});

    ecs_entity_t root = ecs_new(w);
    ecs_set(w, root, Position, {0, 0, 800});
    for (i = 0; i < 8; i ++) {
        ecs_entity_t mast = ecs_new(w);

        ecs_add_pair(w, mast, EcsChildOf, root);
        ecs_set(w, mast, Position, {(float)(i % 4) * 8.6f - 12.9f, 13,
            (float)(i / 4) * 26.0f - 13.0f});
        ecs_set(w, mast, Box, {0.6f, 26, 0.6f});
        ecs_set(w, mast, Rgb, {206, 138, 84});
    }

    ecs_entity_t hut = ecs_new(w);
    ecs_add_pair(w, hut, EcsChildOf, root);
    ecs_set(w, hut, Position, {0, 2.5f, 0});
    ecs_set(w, hut, Box, {6, 5, 6});
    ecs_set(w, hut, Rgb, {110, 190, 230});
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
    uint32_t atlas = box_lod_cache_find(cache, root)->atlas;

    test_assert(atlas != 0);
    ecs_delete(w, root);
    for (i = 0; i < 3; i ++) {
        box_packet_reset(&p);
        p.width = 1920;
        p.height = 1080;
        box_render_extract(w, &p);
        box_collision_update(w);
    }

    test_assert(box_lod_cache_find(cache, root) == NULL);
    test_int(cache->atlases, 0);

    box_packet_fini(&p);
    ecs_fini(w);
}

void Impostor_a_near_subtree_keeps_its_parts(void)
{
    // lod/hlod_proxies/impostors
    ecs_world_t *w = ecs_init();
    ECS_IMPORT(w, BoxGeometry);
    ECS_IMPORT(w, BoxCollision);
    box_packet_t p;
    ecs_entity_t masts[8];
    int32_t i;

    ecs_singleton_set(w, LodSettings, {
        .enabled = true, .quality = 1, .error_px = 4, .hysteresis = 0.25f
    });

    ecs_entity_t cam = ecs_new(w);
    ecs_set(w, cam, Position, {0, 8, 0});
    ecs_set(w, cam, LookAt, {0, 8, 100});
    ecs_set(w, cam, Camera, {.fov = 60, .far_ = 100000});

    ecs_entity_t root = ecs_new(w);
    ecs_set(w, root, Position, {0, 0, 100});
    for (i = 0; i < 8; i ++) {
        masts[i] = ecs_new(w);
        ecs_add_pair(w, masts[i], EcsChildOf, root);
        ecs_set(w, masts[i], Position, {(float)(i % 4) * 8.6f - 12.9f, 13,
            (float)(i / 4) * 26.0f - 13.0f});
        ecs_set(w, masts[i], Box, {0.6f, 26, 0.6f});
        ecs_set(w, masts[i], Rgb, {206, 138, 84});
        ecs_add(w, masts[i], LodPin);
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

    test_int(cache->atlases, 0);
    test_int(p.transparent.count, 0);
    test_bool(ecs_get(w, masts[0], RenderInstance)->lod_hidden, false);

    box_packet_fini(&p);
    ecs_fini(w);
}

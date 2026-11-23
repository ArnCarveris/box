/**
 * @file ViewLod.c
 * @brief Tests for View.lod: a View has no levels of detail unless set, scripts can set them, and
 * the packet holds them resolved to a valid level count and ascending distances.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"

/** Check that settings have no levels of detail.
 *
 * @param lod The settings
 */
static void lod_test_none(
    const LodSettings *lod)
{
    test_int(lod->levels, 0);
    for (int32_t i = 0; i < BOX_LOD_LEVELS_MAX; i ++) {
        test_flt(lod->distances[i], 0.0f);
    }
}

void ViewLod_defaults_constructed(void) {
    // renderer/views
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, View);
    lod_test_none(&ecs_get(world, e, View)->lod);
    test_assert(BOX_LOD_LEVELS_MAX == 4);
    ecs_fini(world);
}

void ViewLod_defaults_literal(void) {
    // renderer/views
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, View, {.order = 1});
    lod_test_none(&ecs_get(world, e, View)->lod);
    ecs_fini(world);
}

void ViewLod_defaults_script(void) {
    // renderer/views
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script_run(world, "views",
        "using box.view\n"
        "view { View: {} }\n"
        "culled { View: {culling: {enabled: true}} }\n", NULL) == 0);
    lod_test_none(&ecs_get(world, ecs_lookup(world, "view"), View)->lod);
    lod_test_none(&ecs_get(world, ecs_lookup(world, "culled"), View)->lod);
    ecs_fini(world);
}

void ViewLod_script_set(void) {
    // renderer/views
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script_run(world, "views",
        "using box.view\n"
        "view { View: {lod: {levels: 3, distances: [0, 20, 60, 0]}} }\n", NULL) == 0);
    const View *view = ecs_get(world, ecs_lookup(world, "view"), View);
    test_assert(view != NULL);
    test_int(view->lod.levels, 3);
    test_flt(view->lod.distances[1], 20.0f);
    test_flt(view->lod.distances[2], 60.0f);
    test_bool(view->lighting.enabled, true);
    ecs_fini(world);
}

void ViewLod_resolve(void) {
    // renderer/views
    LodSettings out;

    /* No levels, one level and negative counts are none */
    int32_t none[3] = { 0, 1, -2 };
    for (int32_t i = 0; i < 3; i ++) {
        LodSettings in = { .levels = none[i], .distances = { 5, 10, 20, 30 } };
        box_render_lod_settings(&in, &out);
        lod_test_none(&out);
    }

    /* Ascending distances apply as set, distances[0] is 0 */
    LodSettings valid = { .levels = 4, .distances = { 3, 10, 25, 50 } };
    box_render_lod_settings(&valid, &out);
    test_int(out.levels, 4);
    test_flt(out.distances[0], 0.0f);
    test_flt(out.distances[1], 10.0f);
    test_flt(out.distances[2], 25.0f);
    test_flt(out.distances[3], 50.0f);

    /* The count is clamped, the distances past it are 0 */
    LodSettings many = { .levels = 9, .distances = { 0, 10, 20, 30 } };
    box_render_lod_settings(&many, &out);
    test_int(out.levels, BOX_LOD_LEVELS_MAX);
    LodSettings two = { .levels = 2, .distances = { 0, 10, 20, 30 } };
    box_render_lod_settings(&two, &out);
    test_int(out.levels, 2);
    test_flt(out.distances[1], 10.0f);
    test_flt(out.distances[2], 0.0f);
    test_flt(out.distances[3], 0.0f);

    /* Descending, negative and non-finite distances become ascending */
    LodSettings bad = { .levels = 4, .distances = { 0, 40, 20, NAN } };
    box_render_lod_settings(&bad, &out);
    test_flt(out.distances[1], 40.0f);
    test_flt(out.distances[2], 40.0f);
    test_flt(out.distances[3], 40.0f);
    LodSettings negative = { .levels = 3, .distances = { 0, -5, INFINITY, 0 } };
    box_render_lod_settings(&negative, &out);
    test_flt(out.distances[1], 0.0f);
    test_flt(out.distances[2], 0.0f);
}

void ViewLod_resolve_fade(void) {
    // renderer/views
    LodSettings out;
    LodSettings set = { .levels = 3, .distances = { 0, 10, 30 }, .fade = 0.2f };
    box_render_lod_settings(&set, &out);
    test_flt(out.fade, 0.2f);

    LodSettings wide = { .levels = 3, .distances = { 0, 10, 30 }, .fade = 2.0f };
    box_render_lod_settings(&wide, &out);
    test_flt(out.fade, 0.5f);

    LodSettings negative = { .levels = 3, .distances = { 0, 10, 30 }, .fade = -1.0f };
    box_render_lod_settings(&negative, &out);
    test_flt(out.fade, 0.0f);

    LodSettings nan = { .levels = 3, .distances = { 0, 10, 30 }, .fade = NAN };
    box_render_lod_settings(&nan, &out);
    test_flt(out.fade, 0.0f);

    LodSettings none = { .levels = 1, .distances = { 0, 10, 30 }, .fade = 0.2f };
    box_render_lod_settings(&none, &out);
    test_flt(out.fade, 0.0f);
}

void ViewLod_packet(void) {
    // renderer/views
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t plain = ecs_new(world);
    ecs_set(world, plain, View, {.camera = camera});
    ecs_entity_t lod = ecs_new(world);
    ecs_set(world, lod, View, {.camera = camera, .order = 1,
        .lod = {.levels = 6, .distances = {0, 30, 10, 90}}});
    ecs_progress(world, 0);
    box_render_sync(world);

    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->views), 2);
    const box_render_view_t *first = ecs_vec_get_t(&p->views, box_render_view_t, 0);
    const box_render_view_t *second = ecs_vec_get_t(&p->views, box_render_view_t, 1);
    test_uint(first->view, plain);
    lod_test_none(&first->lod);
    test_uint(second->view, lod);
    test_int(second->lod.levels, BOX_LOD_LEVELS_MAX);
    test_flt(second->lod.distances[1], 30.0f);
    test_flt(second->lod.distances[2], 30.0f);
    test_flt(second->lod.distances[3], 90.0f);

    /* The View keeps the value as set */
    test_int(ecs_get(world, lod, View)->lod.levels, 6);
    ecs_fini(world);
    box_render_null_free(null);
}

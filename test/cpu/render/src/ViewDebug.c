/**
 * @file ViewDebug.c
 * @brief Tests for View.debug: the defaults of the View constructor, a C literal View without
 * debug drawing, script values that keep the defaults and the copy of the settings in the views
 * of the packet.
 *
 * The packet cases render into the null backend with one camera. The wire cases check the
 * wireframe settings and the extract of a view with WireframeOnly, which culls without occlusion
 * and renders no shadows.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"

/** Check that grid settings have the defaults of the View constructor, but for enabled.
 *
 * @param g The settings
 */
static void debug_expect_grid_defaults(
    const GridSettings *g)
{
    test_flt(g->height, 0);
    test_flt(g->cell_size, 1);
    test_int(g->major_every, 10);
    test_flt(g->fade_distance, 100);
    test_flt(g->line_width, 1);
    test_int(g->color.r, 128);
    test_int(g->color.g, 128);
    test_int(g->color.b, 128);
    test_int(g->color.a, 128);
    test_int(g->major_color.r, 128);
    test_int(g->major_color.g, 128);
    test_int(g->major_color.b, 128);
    test_int(g->major_color.a, 200);
}

void ViewDebug_ctor_defaults(void) {
    // debug/settings
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, View);
    const View *v = ecs_get(world, e, View);
    test_assert(v != NULL);
    test_bool(v->debug.grid.enabled, false);
    debug_expect_grid_defaults(&v->debug.grid);
    ecs_fini(world);
}

void ViewDebug_literal_off(void) {
    // debug/settings
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, View, {.camera = camera});
    const View *v = ecs_get(world, e, View);
    test_bool(v->debug.grid.enabled, false);
    test_flt(v->debug.grid.cell_size, 0);
    test_int(v->debug.grid.major_every, 0);
    test_flt(v->debug.grid.fade_distance, 0);
    test_flt(v->debug.grid.line_width, 0);
    test_int(v->debug.grid.color.a, 0);
    ecs_fini(world);
}

void ViewDebug_script_keeps_defaults(void) {
    // debug/settings
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script_run(world, "debug_grid",
        "using box.view\n"
        "on { View: {debug: {grid: {enabled: true}}} }\n"
        "tuned { View: {debug: {grid: {enabled: true, cell_size: 0.5, major_every: 4,\n"
        "    color: {255, 0, 0, 255}}}} }\n", NULL) == 0);

    const View *v = ecs_get(world, ecs_lookup(world, "on"), View);
    test_assert(v != NULL);
    test_bool(v->debug.grid.enabled, true);
    debug_expect_grid_defaults(&v->debug.grid);
    test_bool(v->lighting.enabled, true);

    v = ecs_get(world, ecs_lookup(world, "tuned"), View);
    test_assert(v != NULL);
    test_bool(v->debug.grid.enabled, true);
    test_flt(v->debug.grid.cell_size, 0.5f);
    test_int(v->debug.grid.major_every, 4);
    test_int(v->debug.grid.color.r, 255);
    test_int(v->debug.grid.color.g, 0);
    test_int(v->debug.grid.color.a, 255);
    test_flt(v->debug.grid.fade_distance, 100);
    test_flt(v->debug.grid.line_width, 1);
    test_int(v->debug.grid.major_color.a, 200);
    ecs_fini(world);
}

void ViewDebug_packet_copy(void) {
    // debug/settings
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t on = ecs_new(world);
    ecs_set(world, on, View, {.camera = camera, .debug = {.grid = {.enabled = true,
        .height = 2, .cell_size = 0.25f, .major_every = 8, .fade_distance = 50,
        .line_width = 2, .color = {10, 20, 30, 40}, .major_color = {50, 60, 70, 80}}},
        .order = 1});
    ecs_entity_t off = ecs_new(world);
    ecs_add(world, off, View);
    ecs_get_mut(world, off, View)->camera = camera;
    ecs_modified(world, off, View);

    ecs_progress(world, 0);
    box_render_sync(world);
    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->views), 2);
    const box_render_view_t *v0 = ecs_vec_get_t(&p->views, box_render_view_t, 0);
    const box_render_view_t *v1 = ecs_vec_get_t(&p->views, box_render_view_t, 1);
    test_uint(v0->view, off);
    test_uint(v1->view, on);
    test_bool(v0->debug.grid.enabled, false);
    debug_expect_grid_defaults(&v0->debug.grid);
    const GridSettings *g = &v1->debug.grid;
    test_bool(g->enabled, true);
    test_flt(g->height, 2);
    test_flt(g->cell_size, 0.25f);
    test_int(g->major_every, 8);
    test_flt(g->fade_distance, 50);
    test_flt(g->line_width, 2);
    test_int(g->color.r, 10);
    test_int(g->color.g, 20);
    test_int(g->color.b, 30);
    test_int(g->color.a, 40);
    test_int(g->major_color.r, 50);
    test_int(g->major_color.g, 60);
    test_int(g->major_color.b, 70);
    test_int(g->major_color.a, 80);

    ecs_get_mut(world, on, View)->debug.grid.enabled = false;
    ecs_modified(world, on, View);
    ecs_progress(world, 0);
    box_render_sync(world);
    p = &null->last;
    test_int(ecs_vec_count(&p->views), 2);
    test_bool(ecs_vec_get_t(&p->views, box_render_view_t, 1)->debug.grid.enabled, false);

    ecs_fini(world);
    box_render_null_free(null);
}

void ViewDebug_wire_defaults(void) {
    // debug/wireframe
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, View);
    const View *v = ecs_get(world, e, View);
    test_assert(v != NULL);
    test_int(v->debug.wireframe, WireframeOff);
    test_int(v->debug.wireframe_color.r, 255);
    test_int(v->debug.wireframe_color.g, 255);
    test_int(v->debug.wireframe_color.b, 255);
    test_int(v->debug.wireframe_color.a, 200);
    test_flt(v->debug.wireframe_width, 1);

    ecs_entity_t literal = ecs_new(world);
    ecs_set(world, literal, View, {0});
    v = ecs_get(world, literal, View);
    test_int(v->debug.wireframe, WireframeOff);
    test_int(v->debug.wireframe_color.a, 0);
    test_flt(v->debug.wireframe_width, 0);
    ecs_fini(world);
}

void ViewDebug_wire_script_keeps_defaults(void) {
    // debug/wireframe
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script_run(world, "debug_wire",
        "using box.view\n"
        "overlay { View: {debug: {wireframe: WireframeOverlay}} }\n"
        "only { View: {debug: {wireframe: WireframeOnly, wireframe_width: 3}} }\n"
        "tinted { View: {debug: {wireframe_color: {255, 0, 0, 255}}} }\n", NULL) == 0);

    const View *v = ecs_get(world, ecs_lookup(world, "overlay"), View);
    test_assert(v != NULL);
    test_int(v->debug.wireframe, WireframeOverlay);
    test_int(v->debug.wireframe_color.r, 255);
    test_int(v->debug.wireframe_color.a, 200);
    test_flt(v->debug.wireframe_width, 1);
    test_bool(v->debug.grid.enabled, false);
    test_flt(v->debug.grid.cell_size, 1);
    test_bool(v->culling.occlusion, true);

    v = ecs_get(world, ecs_lookup(world, "only"), View);
    test_assert(v != NULL);
    test_int(v->debug.wireframe, WireframeOnly);
    test_flt(v->debug.wireframe_width, 3);
    test_int(v->debug.wireframe_color.g, 255);
    test_int(v->debug.wireframe_color.a, 200);

    v = ecs_get(world, ecs_lookup(world, "tinted"), View);
    test_assert(v != NULL);
    test_int(v->debug.wireframe, WireframeOff);
    test_int(v->debug.wireframe_color.r, 255);
    test_int(v->debug.wireframe_color.g, 0);
    test_int(v->debug.wireframe_color.a, 255);
    test_flt(v->debug.wireframe_width, 1);
    ecs_fini(world);
}

void ViewDebug_wire_packet_copy(void) {
    // debug/wireframe
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t on = ecs_new(world);
    ecs_set(world, on, View, {.camera = camera, .debug = {.wireframe = WireframeOverlay,
        .wireframe_color = {10, 20, 30, 40}, .wireframe_width = 2.5f}, .order = 1});
    ecs_entity_t off = ecs_new(world);
    ecs_add(world, off, View);
    ecs_get_mut(world, off, View)->camera = camera;
    ecs_modified(world, off, View);

    ecs_progress(world, 0);
    box_render_sync(world);
    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->views), 2);
    const box_render_view_t *v0 = ecs_vec_get_t(&p->views, box_render_view_t, 0);
    const box_render_view_t *v1 = ecs_vec_get_t(&p->views, box_render_view_t, 1);
    test_uint(v0->view, off);
    test_uint(v1->view, on);
    test_int(v0->debug.wireframe, WireframeOff);
    test_int(v0->debug.wireframe_color.a, 200);
    test_flt(v0->debug.wireframe_width, 1);
    test_int(v1->debug.wireframe, WireframeOverlay);
    test_int(v1->debug.wireframe_color.r, 10);
    test_int(v1->debug.wireframe_color.g, 20);
    test_int(v1->debug.wireframe_color.b, 30);
    test_int(v1->debug.wireframe_color.a, 40);
    test_flt(v1->debug.wireframe_width, 2.5f);

    ecs_get_mut(world, on, View)->debug.wireframe = WireframeOnly;
    ecs_modified(world, on, View);
    ecs_progress(world, 0);
    box_render_sync(world);
    p = &null->last;
    test_int(ecs_vec_count(&p->views), 2);
    test_int(ecs_vec_get_t(&p->views, box_render_view_t, 1)->debug.wireframe, WireframeOnly);

    ecs_fini(world);
    box_render_null_free(null);
}

void ViewDebug_wire_only_no_occlusion(void) {
    // debug/wireframe
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t sun = ecs_new(world);
    ecs_set(world, sun, Rotation, {0.9f, 0.6f, 0});
    ecs_set(world, sun, DirectionalLight, {.intensity = 1});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f, .far_ = 100});
    ecs_entity_t modes[3];
    WireframeMode values[3] = { WireframeOff, WireframeOverlay, WireframeOnly };
    int32_t i;
    for (i = 0; i < 3; i ++) {
        modes[i] = ecs_new(world);
        ecs_add(world, modes[i], View);
        View *v = ecs_get_mut(world, modes[i], View);
        v->camera = camera;
        v->shadows.enabled = true;
        v->debug.wireframe = values[i];
        v->order = i;
        ecs_modified(world, modes[i], View);
    }

    ecs_progress(world, 0);
    box_render_sync(world);
    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->views), 3);
    for (i = 0; i < 3; i ++) {
        const box_render_view_t *v = ecs_vec_get_t(&p->views, box_render_view_t, i);
        test_uint(v->view, modes[i]);
        test_bool(v->cull.enabled, true);
        test_flt(v->cull.min_dimension, 1);
        test_bool(v->cull.occlusion, i != 2);
        test_bool(v->shadows.enabled, i != 2);
    }

    ecs_get_mut(world, modes[2], View)->debug.wireframe = WireframeOverlay;
    ecs_modified(world, modes[2], View);
    ecs_progress(world, 0);
    box_render_sync(world);
    p = &null->last;
    test_bool(ecs_vec_get_t(&p->views, box_render_view_t, 2)->cull.occlusion, true);

    ecs_fini(world);
    box_render_null_free(null);
}

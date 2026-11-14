/**
 * @file ViewDebug.c
 * @brief Tests for View.debug: the defaults of the View constructor, a C literal View without
 * debug drawing, script values that keep the defaults and the copy of the settings in the views
 * of the packet, for the grid and for the light and reflection probe views.
 *
 * The packet cases render into the null backend with one camera. The wire cases check the
 * wireframe settings and the extract of a view with WireframeOnly, which culls without occlusion
 * and renders no shadows.
 *
 * The capture case checks that a capture of the camera of a view copies its debug settings, and
 * that the other captures draw none.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"
#include "spatial.h"
#include <string.h>

/** Warnings logged while the log hook is set that name the view "probed" */
static int32_t debug_warnings;

/** Log hook that counts the warnings naming the view "probed".
 *
 * @param level Level of the message
 * @param file File that logged it
 * @param line Line that logged it
 * @param msg The message
 */
static void debug_count_warnings(
    int32_t level,
    const char *file,
    int32_t line,
    const char *msg)
{
    (void)file;
    (void)line;
    if (level == -2 && msg && strstr(msg, "probed")) {
        debug_warnings ++;
    }
}

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
    test_bool(v->debug.light_probes, false);
    test_bool(v->debug.reflection_probes, false);
    test_flt(v->debug.probe_size, 0);
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
    test_bool(v->debug.light_probes, false);
    test_bool(v->debug.reflection_probes, false);
    test_flt(v->debug.probe_size, 0);
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

void ViewDebug_script_probes_keep_others(void) {
    // debug/probes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script_run(world, "debug_probes",
        "using box.view\n"
        "lights { View: {debug: {light_probes: true}} }\n"
        "reflections { View: {debug: {reflection_probes: true, probe_size: 0.5}} }\n"
        "both { View: {debug: {grid: {enabled: true}, light_probes: true,\n"
        "    reflection_probes: true}} }\n", NULL) == 0);

    const View *v = ecs_get(world, ecs_lookup(world, "lights"), View);
    test_assert(v != NULL);
    test_bool(v->debug.light_probes, true);
    test_bool(v->debug.reflection_probes, false);
    test_flt(v->debug.probe_size, 0);
    test_bool(v->debug.grid.enabled, false);
    debug_expect_grid_defaults(&v->debug.grid);
    test_bool(v->lighting.baked, true);
    test_bool(v->lighting.reflections, true);

    v = ecs_get(world, ecs_lookup(world, "reflections"), View);
    test_assert(v != NULL);
    test_bool(v->debug.light_probes, false);
    test_bool(v->debug.reflection_probes, true);
    test_flt(v->debug.probe_size, 0.5f);
    debug_expect_grid_defaults(&v->debug.grid);

    v = ecs_get(world, ecs_lookup(world, "both"), View);
    test_assert(v != NULL);
    test_bool(v->debug.grid.enabled, true);
    test_bool(v->debug.light_probes, true);
    test_bool(v->debug.reflection_probes, true);
    test_flt(v->debug.probe_size, 0);
    debug_expect_grid_defaults(&v->debug.grid);
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
        .line_width = 2, .color = {10, 20, 30, 40}, .major_color = {50, 60, 70, 80}},
        .light_probes = true, .reflection_probes = true, .probe_size = 0.75f},
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
    test_bool(v0->debug.light_probes, false);
    test_bool(v0->debug.reflection_probes, false);
    test_flt(v0->debug.probe_size, 0);
    test_bool(v1->debug.light_probes, true);
    test_bool(v1->debug.reflection_probes, true);
    test_flt(v1->debug.probe_size, 0.75f);
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
    ecs_get_mut(world, on, View)->debug.light_probes = false;
    ecs_modified(world, on, View);
    ecs_progress(world, 0);
    box_render_sync(world);
    p = &null->last;
    test_int(ecs_vec_count(&p->views), 2);
    v1 = ecs_vec_get_t(&p->views, box_render_view_t, 1);
    test_bool(v1->debug.grid.enabled, false);
    test_bool(v1->debug.light_probes, false);
    test_bool(v1->debug.reflection_probes, true);
    test_flt(v1->debug.probe_size, 0.75f);

    ecs_fini(world);
    box_render_null_free(null);
}

void ViewDebug_light_probes_warn_once(void) {
    // debug/probes
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t scene = ecs_new(world);
    ecs_set(world, scene, Scene, {.baked = true});
    ecs_set(world, scene, LightBake, {.probe_spacing = 1, .bounds_padding = 0,
        .budget_ms = 0, .compute = true});
    ecs_entity_t floor = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, floor, Position, {0, 0, 0});
    ecs_set(world, floor, Box, {100, 8, 100});
    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, scene);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_entity(world, {.name = "probed"});
    ecs_add(world, view, View);
    View *v = ecs_get_mut(world, view, View);
    v->camera = camera;
    v->debug.light_probes = true;
    ecs_modified(world, view, View);

    ecs_os_api_log_t log = ecs_os_api.log_;
    debug_warnings = 0;
    ecs_os_api.log_ = debug_count_warnings;
    int32_t i;
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    ecs_os_api.log_ = log;
    test_int(debug_warnings, 1);
    const box_render_view_t *rv = ecs_vec_get_t(&null->last.views, box_render_view_t, 0);
    test_assert(rv->volume != 0);
    test_assert(ecs_get(world, scene, LightBakeStatus)->probes > BOX_RENDER_DEBUG_PROBES_MAX);

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

/** Find the view of the packet that renders into a target.
 *
 * @param p The packet
 * @param target Handle of the target
 * @return The view, NULL when no view renders into the target
 */
static const box_render_view_t* debug_view_of_target(
    const box_render_packet_t *p,
    uint32_t target)
{
    const box_render_view_t *result = NULL;
    int32_t i;
    for (i = 0; i < ecs_vec_count(&p->views); i ++) {
        const box_render_view_t *v = ecs_vec_get_t(&p->views, box_render_view_t, i);
        if (v->target == target) {
            result = v;
        }
    }

    return result;
}

void ViewDebug_capture_copies_debug(void) {
    // debug/capture
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t lone = ecs_new(world);
    ecs_set(world, lone, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *data = ecs_get_mut(world, view, View);
    data->camera = camera;
    data->debug.grid.enabled = true;
    data->debug.grid.cell_size = 0.5f;
    data->debug.wireframe = WireframeOnly;
    data->debug.wireframe_width = 2;
    data->debug.light_probes = true;
    ecs_modified(world, view, View);

    test_int(box_render_capture(world, &(box_render_capture_desc_t){
        .camera = camera, .width = 16, .height = 16, .path = "capture_debug.png"}), 0);
    test_int(box_render_capture(world, &(box_render_capture_desc_t){
        .camera = lone, .width = 16, .height = 16, .path = "capture_lone.png"}), 0);
    test_int(box_render_capture(world, &(box_render_capture_desc_t){
        .camera_data = {.fov = 60, .near_ = 0.1f}, .transform = GLM_MAT4_IDENTITY_INIT,
        .width = 16, .height = 16, .path = "capture_desc.png"}), 0);

    ecs_progress(world, 0);
    box_render_sync(world);
    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->views), 4);

    const box_render_view_t *source = ecs_vec_get_t(&p->views, box_render_view_t, 0);
    test_uint(source->view, view);
    test_uint(source->target, 0);

    const box_render_view_t *v = debug_view_of_target(p, BOX_RENDER_CAPTURE_TARGET_FIRST);
    test_assert(v != NULL);
    test_uint(v->view, view);
    test_uint(v->camera, camera);
    test_assert(memcmp(&v->debug, &source->debug, sizeof(ViewDebug)) == 0);
    test_bool(v->debug.grid.enabled, true);
    test_flt(v->debug.grid.cell_size, 0.5f);
    test_int(v->debug.wireframe, WireframeOnly);
    test_flt(v->debug.wireframe_width, 2);
    test_bool(v->debug.light_probes, true);
    test_bool(v->cull.occlusion, false);
    test_bool(v->cull.occlusion, source->cull.occlusion);

    ViewDebug zero = {0};
    v = debug_view_of_target(p, BOX_RENDER_CAPTURE_TARGET_FIRST + 1);
    test_assert(v != NULL);
    test_uint(v->view, 0);
    test_uint(v->camera, lone);
    test_assert(memcmp(&v->debug, &zero, sizeof(ViewDebug)) == 0);

    v = debug_view_of_target(p, BOX_RENDER_CAPTURE_TARGET_FIRST + 2);
    test_assert(v != NULL);
    test_uint(v->view, 0);
    test_uint(v->camera, 0);
    test_assert(memcmp(&v->debug, &zero, sizeof(ViewDebug)) == 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void ViewDebug_overdraw_script(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, View);
    test_int(ecs_get(world, e, View)->debug.overdraw, OverdrawOff);

    test_assert(ecs_script_run(world, "debug_overdraw",
        "using box.view\n"
        "heat { View: {debug: {overdraw: OverdrawCutout}} }\n", NULL) == 0);
    const View *v = ecs_get(world, ecs_lookup(world, "heat"), View);
    test_assert(v != NULL);
    test_int(v->debug.overdraw, OverdrawCutout);
    test_int(v->debug.wireframe, WireframeOff);
    test_int(v->debug.wireframe_color.a, 200);
    test_flt(v->debug.wireframe_width, 1);
    test_bool(v->debug.grid.enabled, false);
    test_flt(v->debug.grid.cell_size, 1);
    test_bool(v->culling.occlusion, true);
    ecs_fini(world);
}

void ViewDebug_bvh_defaults(void) {
    // debug/bvh
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, View);
    const BvhDebug *b = &ecs_get(world, e, View)->debug.bvh;
    test_bool(b->static_tree, false);
    test_bool(b->dynamic_tree, false);
    test_bool(b->ui_tree, false);
    test_bool(b->leaves, true);
    test_int(b->levels, 0);

    ecs_entity_t literal = ecs_new(world);
    ecs_set(world, literal, View, {0});
    test_bool(ecs_get(world, literal, View)->debug.bvh.leaves, false);

    test_assert(ecs_script_run(world, "debug_bvh",
        "using box.view\n"
        "trees { View: {debug: {bvh: {static_tree: true, dynamic_tree: true, levels: 3}}} }\n",
        NULL) == 0);
    b = &ecs_get(world, ecs_lookup(world, "trees"), View)->debug.bvh;
    test_bool(b->static_tree, true);
    test_bool(b->dynamic_tree, true);
    test_bool(b->ui_tree, false);
    test_bool(b->leaves, true);
    test_int(b->levels, 3);
    ecs_fini(world);
}

void ViewDebug_bvh_static_boxes(void) {
    // debug/bvh
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    int32_t i;
    for (i = 0; i < 4; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {(float)i * 3.0f, 0, 0});
        ecs_set(world, e, Box, {1, 1, 1});
    }

    ecs_entity_t off = ecs_new(world);
    ecs_add(world, off, View);
    ecs_get_mut(world, off, View)->camera = camera;
    ecs_modified(world, off, View);
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *data = ecs_get_mut(world, view, View);
    data->camera = camera;
    data->order = 1;
    data->debug.bvh.static_tree = true;
    ecs_modified(world, view, View);

    ecs_progress(world, 0);
    box_render_sync(world);
    const box_render_packet_t *p = &null->last;
    test_int(ecs_vec_count(&p->views), 2);
    const box_render_view_t *v0 = ecs_vec_get_t(&p->views, box_render_view_t, 0);
    const box_render_view_t *v1 = ecs_vec_get_t(&p->views, box_render_view_t, 1);
    test_uint(v0->view, off);
    test_int(v0->debug_box_count, 0);
    test_uint(v1->view, view);
    test_int(v1->debug_box_count, 7);
    test_int(ecs_vec_count(&p->debug_boxes), v1->debug_box_first + 7);

    int32_t leaves = 0, roots = 0;
    for (i = 0; i < v1->debug_box_count; i ++) {
        const box_render_debug_box_t *b = ecs_vec_get_t(&p->debug_boxes,
            box_render_debug_box_t, v1->debug_box_first + i);
        test_uint(b->kind & 3u, BOX_SPATIAL_STATIC);
        if (b->kind & BOX_RENDER_DEBUG_BOX_LEAF) {
            leaves ++;
            test_assert(b->depth > 0);
            test_flt(b->max[0] - b->min[0], 1);
            test_flt(b->max[1] - b->min[1], 1);
        }

        if (b->depth == 0) {
            roots ++;
            test_flt(b->min[0], -0.5f);
            test_flt(b->max[0], 9.5f);
            test_flt(b->min[1], -0.5f);
            test_flt(b->max[1], 0.5f);
        }
    }

    test_int(leaves, 4);
    test_int(roots, 1);

    data = ecs_get_mut(world, view, View);
    data->debug.bvh.static_tree = false;
    data->debug.bvh.dynamic_tree = true;
    data->debug.bvh.ui_tree = true;
    ecs_modified(world, view, View);
    ecs_progress(world, 0);
    box_render_sync(world);
    p = &null->last;
    test_int(ecs_vec_get_t(&p->views, box_render_view_t, 1)->debug_box_count, 0);
    test_int(ecs_vec_count(&p->debug_boxes), 0);

    ecs_fini(world);
    box_render_null_free(null);
}

void ViewDebug_bvh_levels_and_leaves(void) {
    // debug/bvh
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    int32_t i;
    for (i = 0; i < 4; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {(float)i * 3.0f, 0, 0});
        ecs_set(world, e, Box, {1, 1, 1});
    }

    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *data = ecs_get_mut(world, view, View);
    data->camera = camera;
    data->debug.bvh.static_tree = true;
    data->debug.bvh.levels = 1;
    ecs_modified(world, view, View);

    ecs_progress(world, 0);
    box_render_sync(world);
    const box_render_view_t *v = ecs_vec_get_t(&null->last.views, box_render_view_t, 0);
    test_int(v->debug_box_count, 1);
    const box_render_debug_box_t *b = ecs_vec_get_t(&null->last.debug_boxes,
        box_render_debug_box_t, v->debug_box_first);
    test_uint(b->depth, 0);
    test_uint(b->kind, BOX_SPATIAL_STATIC);

    data = ecs_get_mut(world, view, View);
    data->debug.bvh.levels = 2;
    ecs_modified(world, view, View);
    ecs_progress(world, 0);
    box_render_sync(world);
    v = ecs_vec_get_t(&null->last.views, box_render_view_t, 0);
    test_int(v->debug_box_count, 3);
    for (i = 0; i < v->debug_box_count; i ++) {
        b = ecs_vec_get_t(&null->last.debug_boxes, box_render_debug_box_t,
            v->debug_box_first + i);
        test_assert(b->depth < 2);
    }

    data = ecs_get_mut(world, view, View);
    data->debug.bvh.levels = 0;
    data->debug.bvh.leaves = false;
    ecs_modified(world, view, View);
    ecs_progress(world, 0);
    box_render_sync(world);
    v = ecs_vec_get_t(&null->last.views, box_render_view_t, 0);
    test_int(v->debug_box_count, 3);
    for (i = 0; i < v->debug_box_count; i ++) {
        b = ecs_vec_get_t(&null->last.debug_boxes, box_render_debug_box_t,
            v->debug_box_first + i);
        test_uint(b->kind, BOX_SPATIAL_STATIC);
    }

    ecs_fini(world);
    box_render_null_free(null);
}

void ViewDebug_bvh_dynamic_fat_leaves(void) {
    // debug/bvh
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t still = ecs_new(world);
    ecs_set(world, still, Position, {-5, 0, 0});
    ecs_set(world, still, Box, {1, 1, 1});
    int32_t i;
    for (i = 0; i < 2; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_add(world, e, DynamicTransform);
        ecs_set(world, e, Position, {(float)i * 4.0f, 0, 0});
        ecs_set(world, e, Box, {1, 1, 1});
    }

    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *data = ecs_get_mut(world, view, View);
    data->camera = camera;
    data->debug.bvh.dynamic_tree = true;
    ecs_modified(world, view, View);

    ecs_progress(world, 0);
    box_render_sync(world);
    const box_render_view_t *v = ecs_vec_get_t(&null->last.views, box_render_view_t, 0);
    test_int(v->debug_box_count, 3);
    int32_t leaves = 0;
    for (i = 0; i < v->debug_box_count; i ++) {
        const box_render_debug_box_t *b = ecs_vec_get_t(&null->last.debug_boxes,
            box_render_debug_box_t, v->debug_box_first + i);
        test_uint(b->kind & 3u, BOX_SPATIAL_DYNAMIC);
        test_assert(b->min[0] > -5.0f);
        if (b->kind & BOX_RENDER_DEBUG_BOX_LEAF) {
            leaves ++;
            test_assert(b->max[0] - b->min[0] > 1.0f);
            test_assert(b->min[1] < -0.5f);
            test_assert(b->max[1] > 0.5f);
        }
    }

    test_int(leaves, 2);

    data = ecs_get_mut(world, view, View);
    data->debug.bvh.static_tree = true;
    ecs_modified(world, view, View);
    ecs_progress(world, 0);
    box_render_sync(world);
    v = ecs_vec_get_t(&null->last.views, box_render_view_t, 0);
    test_int(v->debug_box_count, 4);
    const box_render_debug_box_t *first = ecs_vec_get_t(&null->last.debug_boxes,
        box_render_debug_box_t, v->debug_box_first);
    test_uint(first->kind, BOX_SPATIAL_STATIC | BOX_RENDER_DEBUG_BOX_LEAF);
    test_uint(first->depth, 0);
    test_flt(first->min[0], -5.5f);
    test_flt(first->max[0], -4.5f);

    ecs_fini(world);
    box_render_null_free(null);
}

void ViewDebug_bvh_capture_copies_boxes(void) {
    // debug/bvh
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {0, 0, 0});
    ecs_set(world, e, Box, {1, 1, 1});
    ecs_entity_t view = ecs_new(world);
    ecs_add(world, view, View);
    View *data = ecs_get_mut(world, view, View);
    data->camera = camera;
    data->debug.bvh.static_tree = true;
    ecs_modified(world, view, View);

    test_int(box_render_capture(world, &(box_render_capture_desc_t){
        .camera = camera, .width = 16, .height = 16, .path = "capture_bvh.png"}), 0);
    test_int(box_render_capture(world, &(box_render_capture_desc_t){
        .camera_data = {.fov = 60, .near_ = 0.1f}, .transform = GLM_MAT4_IDENTITY_INIT,
        .width = 16, .height = 16, .path = "capture_bvh_desc.png"}), 0);

    ecs_progress(world, 0);
    box_render_sync(world);
    const box_render_packet_t *p = &null->last;
    const box_render_view_t *source = ecs_vec_get_t(&p->views, box_render_view_t, 0);
    test_uint(source->view, view);
    test_int(source->debug_box_count, 1);

    const box_render_view_t *v = debug_view_of_target(p, BOX_RENDER_CAPTURE_TARGET_FIRST);
    test_assert(v != NULL);
    test_int(v->debug_box_first, source->debug_box_first);
    test_int(v->debug_box_count, 1);

    v = debug_view_of_target(p, BOX_RENDER_CAPTURE_TARGET_FIRST + 1);
    test_assert(v != NULL);
    test_int(v->debug_box_count, 0);

    ecs_fini(world);
    box_render_null_free(null);
}

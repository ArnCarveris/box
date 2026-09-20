/**
 * @file Fps.c
 * @brief Tests for the fps command: the window it prints and the overlay it switches.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/command/command_internal.h"
#include "../../../src/render/render_internal.h"

static ecs_world_t *w;
static ecs_entity_t session;

void Fps_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, BoxCommand);
    session = ecs_entity(w, { .name = "agent0" });
    ecs_add(w, session, Terminal);
}

void Fps_teardown(void)
{
    ecs_fini(w);
}

void Fps_prints_the_window_of_the_last_second(void)
{
    // monitoring/fps_command
    ecs_singleton_set(w, FpsOverlay, {.enabled = true, .scale = 1.0f});
    int32_t i;
    for (i = 0; i < 10; i ++) {
        ecs_progress(w, 0.02f);
    }

    bool error = true;
    char *out = box_command_run(w, session, "fps", &error);
    test_bool(error, false);
    test_str(out, "50 fps, mean 20.00 ms, worst 20.00 ms\n");
    ecs_os_free(out);
}

void Fps_worst_frame_is_the_slowest_of_the_window(void)
{
    // monitoring/fps_command
    ecs_singleton_set(w, FpsOverlay, {.enabled = true, .scale = 1.0f});
    ecs_progress(w, 0.01f);
    ecs_progress(w, 0.03f);
    bool error = true;
    char *out = box_command_run(w, session, "fps", &error);
    test_bool(error, false);
    test_str(out, "50 fps, mean 20.00 ms, worst 30.00 ms\n");
    ecs_os_free(out);
}

void Fps_on_enables_the_overlay(void)
{
    // monitoring/fps_command
    bool error = true;
    char *out = box_command_run(w, session, "fps on", &error);
    test_bool(error, false);
    test_str(out, "fps overlay on\n");
    ecs_os_free(out);

    const FpsOverlay *o = ecs_singleton_get(w, FpsOverlay);
    test_assert(o != NULL);
    test_bool(o->enabled, true);
    test_int(o->corner, FpsTopRight);
    test_flt(o->scale, 1.0f);
}

void Fps_off_disables_the_overlay(void)
{
    // monitoring/fps_command
    bool error = true;
    ecs_os_free(box_command_run(w, session, "fps on", &error));
    char *out = box_command_run(w, session, "fps off", &error);
    test_bool(error, false);
    test_str(out, "fps overlay off\n");
    ecs_os_free(out);

    const FpsOverlay *o = ecs_singleton_get(w, FpsOverlay);
    test_assert(o != NULL);
    test_bool(o->enabled, false);
}

void Fps_on_form_enables_the_overlay_on_the_target(void)
{
    // ore_engine/fps_command_overlay
    bool error = true;
    char *out = box_command_run(w, session, "fps on", &error);
    test_bool(error, false);
    test_str(out, "fps overlay on\n");
    ecs_os_free(out);

    const FpsOverlay *o = ecs_singleton_get(w, FpsOverlay);
    test_assert(o != NULL);
    test_bool(o->enabled, true);
}

void Fps_off_form_disables_the_overlay_on_the_target(void)
{
    // ore_engine/fps_command_overlay
    bool error = true;
    char *out = box_command_run(w, session, "fps off", &error);
    test_bool(error, false);
    test_str(out, "fps overlay off\n");
    ecs_os_free(out);

    const FpsOverlay *o = ecs_singleton_get(w, FpsOverlay);
    test_assert(o != NULL);
    test_bool(o->enabled, false);
}

void Fps_switching_keeps_the_panel_settings(void)
{
    // monitoring/fps_command
    ecs_singleton_set(w, FpsOverlay, {
        .enabled = true, .corner = FpsBottomLeft, .scale = 3.0f, .opacity = 0.25f
    });
    bool error = true;
    ecs_os_free(box_command_run(w, session, "fps off", &error));
    const FpsOverlay *o = ecs_singleton_get(w, FpsOverlay);
    test_bool(o->enabled, false);
    test_int(o->corner, FpsBottomLeft);
    test_flt(o->scale, 3.0f);
    test_flt(o->opacity, 0.25f);
}

void Fps_first_call_starts_the_window_with_the_panel(void)
{
    // ore_engine/fps_command_overlay
    test_assert(ecs_singleton_get(w, FpsOverlay) == NULL);
    bool error = true;
    char *out = box_command_run(w, session, "fps", &error);
    test_bool(error, false);
    test_str(out, "0 fps, mean 0.00 ms, worst 0.00 ms\n");
    ecs_os_free(out);

    const FpsOverlay *o = ecs_singleton_get(w, FpsOverlay);
    test_assert(o != NULL);
    test_bool(o->enabled, true);
}

void Fps_window_falls_back_to_the_current_frame(void)
{
    // monitoring/fps_command
    ecs_progress(w, 0.02f);
    bool error = true;
    char *out = box_command_run(w, session, "fps", &error);
    test_bool(error, false);
    test_str(out, "50 fps, mean 20.00 ms, worst 20.00 ms\n");
    ecs_os_free(out);
}

void Fps_window_follows_the_first_call(void)
{
    // monitoring/fps_command
    bool error = true;
    ecs_os_free(box_command_run(w, session, "fps", &error));
    int32_t i;
    for (i = 0; i < 4; i ++) {
        ecs_progress(w, 0.025f);
    }

    char *out = box_command_run(w, session, "fps", &error);
    test_bool(error, false);
    test_str(out, "40 fps, mean 25.00 ms, worst 25.00 ms\n");
    ecs_os_free(out);
}

void Fps_unknown_state_is_an_error(void)
{
    // monitoring/fps_command
    bool error = false;
    char *out = box_command_run(w, session, "fps maybe", &error);
    test_bool(error, true);
    test_str(out, "expected 'on' or 'off', got 'maybe'");
    ecs_os_free(out);
    test_assert(ecs_singleton_get(w, FpsOverlay) == NULL);
}

void Fps_on_targets_the_scene_view_over_a_ui_view(void)
{
    // ore_engine/fps_text
    ECS_IMPORT(w, BoxRender);
    ecs_entity_t layer = ecs_entity(w, { .name = "ui_layer" });
    box_render_add_ui_layer(w, layer);
    ecs_entity_t ui_view = ecs_entity(w, { .name = "ui_view" });
    ecs_set(w, ui_view, View, { .layers = { layer } });
    ecs_entity_t scene_view = ecs_entity(w, { .name = "scene_view" });
    ecs_add(w, scene_view, View);

    RenderState *state = box_render_state(w);
    box_view_src_t *ui_src = ecs_vec_append_t(NULL, &state->view_list, box_view_src_t);
    ui_src->entity = ui_view;
    box_view_src_t *scene_src = ecs_vec_append_t(NULL, &state->view_list, box_view_src_t);
    scene_src->entity = scene_view;

    bool error = true;
    char *out = box_command_run(w, session, "fps on", &error);
    test_bool(error, false);
    test_str(out, "fps overlay on\n");
    ecs_os_free(out);

    test_assert(ecs_get(w, scene_view, FpsOverlay) != NULL);
    test_assert(ecs_get(w, ui_view, FpsOverlay) == NULL);
}

void Fps_on_targets_the_singleton_when_every_view_is_ui(void)
{
    // ore_engine/fps_text
    ECS_IMPORT(w, BoxRender);
    ecs_entity_t layer = ecs_entity(w, { .name = "ui_layer" });
    box_render_add_ui_layer(w, layer);
    ecs_entity_t ui_view = ecs_entity(w, { .name = "ui_view" });
    ecs_set(w, ui_view, View, { .layers = { layer } });

    RenderState *state = box_render_state(w);
    box_view_src_t *ui_src = ecs_vec_append_t(NULL, &state->view_list, box_view_src_t);
    ui_src->entity = ui_view;

    bool error = true;
    char *out = box_command_run(w, session, "fps on", &error);
    test_bool(error, false);
    test_str(out, "fps overlay on\n");
    ecs_os_free(out);

    test_assert(ecs_singleton_get(w, FpsOverlay) != NULL);
    test_assert(ecs_get(w, ui_view, FpsOverlay) == NULL);
}

void Fps_help_lists_the_command(void)
{
    // monitoring/fps_command
    bool error = true;
    char *out = box_command_run(w, session, "help", &error);
    test_bool(error, false);
    test_assert(strstr(out, "fps") != NULL);
    ecs_os_free(out);
}

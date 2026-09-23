/**
 * @file Fps.c
 * @brief Tests for the fps command: the window it prints and the panel it switches.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/command/command_internal.h"

static ecs_world_t *w;
static ecs_entity_t session;

void Fps_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
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
    ecs_progress(w, 0.01f);
    ecs_progress(w, 0.03f);
    bool error = true;
    char *out = box_command_run(w, session, "fps", &error);
    test_bool(error, false);
    test_str(out, "50 fps, mean 20.00 ms, worst 30.00 ms\n");
    ecs_os_free(out);
}

void Fps_on_shows_the_panel(void)
{
    // monitoring/fps_command
    bool error = true;
    char *out = box_command_run(w, session, "fps on", &error);
    test_bool(error, false);
    test_str(out, "fps panel on\n");
    ecs_os_free(out);

    test_bool(ecs_mut_var_get_t(w, "box.monitor.fps_panel.visible", ecs_bool_t), true);
    test_assert(ecs_lookup(w, "box.monitor.fps_panel.panel") != 0);
}

void Fps_off_hides_the_panel(void)
{
    // monitoring/fps_command
    bool error = true;
    ecs_os_free(box_command_run(w, session, "fps on", &error));
    char *out = box_command_run(w, session, "fps off", &error);
    test_bool(error, false);
    test_str(out, "fps panel off\n");
    ecs_os_free(out);

    test_bool(ecs_mut_var_get_t(w, "box.monitor.fps_panel.visible", ecs_bool_t), false);
    test_assert(ecs_lookup(w, "box.monitor.fps_panel.panel") == 0);
}

void Fps_first_call_shows_the_panel(void)
{
    // monitoring/fps_command
    bool error = true;
    char *out = box_command_run(w, session, "fps", &error);
    test_bool(error, false);
    test_str(out, "0 fps, mean 0.00 ms, worst 0.00 ms\n");
    ecs_os_free(out);
    test_bool(ecs_mut_var_get_t(w, "box.monitor.fps_panel.visible", ecs_bool_t), true);
    test_assert(ecs_lookup(w, "box.monitor.fps_panel.panel") != 0);
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
    test_bool(ecs_mut_var_get_t(w, "box.monitor.fps_panel.visible", ecs_bool_t), false);
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

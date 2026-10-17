/**
 * @file Limits.c
 * @brief Tests for the bounds of command arguments, sessions, history, logs and images.
 */

#include <box.h>
#include <bake_test.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "render.h"

void Limits_i32_out_of_range(void) {
    // hardening/command_limits
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "commands 99999999999", &error);
    test_bool(error, true);
    test_str(out, "error: count: 99999999999 is out of range, -2147483648 to 2147483647");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "commands -2147483649", &error);
    test_bool(error, true);
    test_str(out, "error: count: -2147483649 is out of range, -2147483648 to 2147483647");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "commands 2147483647", &error);
    test_bool(error, false);
    ecs_os_free(out);

    ecs_fini(world);
}

void Limits_f64_not_finite(void) {
    // hardening/command_limits
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "mouse.scroll inf 0", &error);
    test_bool(error, true);
    test_str(out, "error: dx: inf is not a finite number");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "mouse.scroll 0 1e999", &error);
    test_bool(error, true);
    test_str(out, "error: dy: 1e999 is not a finite number");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "mouse.scroll 0 nan", &error);
    test_bool(error, true);
    test_str(out, "error: dy: nan is not a finite number");
    ecs_os_free(out);

    ecs_fini(world);
}

void Limits_sweep_steps_out_of_range(void) {
    // hardening/command_limits
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *line = flecs_asprintf("mouse.sweep [0, 0] [10, 10] %d", BOX_COMMAND_STEPS_MAX + 1);
    char *expect = flecs_asprintf("error: steps: %d is out of range, 0 to %d",
        BOX_COMMAND_STEPS_MAX + 1, BOX_COMMAND_STEPS_MAX);
    char *out = box_terminal_run(world, s, line, &error);
    test_bool(error, true);
    test_str(out, expect);
    ecs_os_free(out);
    ecs_os_free(expect);
    ecs_os_free(line);

    out = box_terminal_run(world, s, "mouse.sweep [0, 0] [10, 10] 2000000000", &error);
    test_bool(error, true);
    ecs_os_free(out);

    line = flecs_asprintf("mouse.sweep [0, 0] [10, 10] %d", BOX_COMMAND_STEPS_MAX);
    out = box_terminal_run(world, s, line, &error);
    test_bool(error, false);
    ecs_os_free(out);
    ecs_os_free(line);

    ecs_fini(world);
}

void Limits_drag_steps_out_of_range(void) {
    // hardening/command_limits
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "mouse.drag left [0, 0] [1, 1] 2000000000", &error);
    test_bool(error, true);
    char *expect = flecs_asprintf("error: steps: 2000000000 is out of range, 0 to %d",
        BOX_COMMAND_STEPS_MAX);
    test_str(out, expect);
    ecs_os_free(expect);
    ecs_os_free(out);

    ecs_progress(world, 0);
    test_bool(ecs_singleton_get(world, Input)->keys[KeyMouseLeft].down, false);

    ecs_fini(world);
}

void Limits_action_drag_steps_out_of_range(void) {
    // hardening/command_limits
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t jump = ecs_entity(world, { .name = "Jump" });
    ecs_set(world, jump, Action, { 0 });

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *out = box_terminal_run(world, s, "action.drag Jump [0, 0] [1, 1] 2000000000", &error);
    test_bool(error, true);
    char *expect = flecs_asprintf("error: steps: 2000000000 is out of range, 0 to %d",
        BOX_COMMAND_STEPS_MAX);
    test_str(out, expect);
    ecs_os_free(expect);
    ecs_os_free(out);

    ecs_fini(world);
}

void Limits_dur_out_of_range(void) {
    // hardening/command_limits
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    bool error = false;
    char *expect = flecs_asprintf("error: dur: 2000000000 is out of range, 0 to %d",
        BOX_COMMAND_FRAMES_MAX);
    char *out = box_terminal_run(world, s, "key.press A 2000000000", &error);
    test_bool(error, true);
    test_str(out, expect);
    ecs_os_free(out);

    out = box_terminal_run(world, s, "mouse.sweep [0, 0] [1, 1] 10 2000000000", &error);
    test_bool(error, true);
    test_str(out, expect);
    ecs_os_free(out);
    ecs_os_free(expect);

    out = box_terminal_run(world, s, "key.press A -1", &error);
    test_bool(error, true);
    ecs_os_free(out);

    ecs_fini(world);
}

void Limits_next_count_out_of_range(void) {
    // hardening/command_limits
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t parent = ecs_entity(world, { .name = "parent" });
    ecs_entity(world, { .name = "e1", .parent = parent });
    ecs_entity(world, { .name = "e2", .parent = parent });

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "query q (ChildOf, parent)", NULL));
    ecs_os_free(box_terminal_run(world, s, "iter q", NULL));

    bool error = false;
    char *expect = flecs_asprintf("error: count: 2147483647 is out of range, 0 to %d",
        BOX_COMMAND_NEXT_MAX);
    char *out = box_terminal_run(world, s, "next 2147483647", &error);
    test_bool(error, true);
    test_str(out, expect);
    ecs_os_free(out);
    ecs_os_free(expect);

    out = box_terminal_run(world, s, "next -1", &error);
    test_bool(error, true);
    ecs_os_free(out);

    ecs_os_free(box_terminal_run(world, s, "iter q", NULL));
    out = box_terminal_run(world, s, "next", &error);
    test_bool(error, false);
    test_str(out, "parent.e1");
    ecs_os_free(out);

    ecs_fini(world);
}

void Limits_session_cap(void) {
    // hardening/command_limits
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_int(ecs_singleton_get(world, TerminalLimits)->sessions, BOX_TERMINAL_SESSIONS);
    ecs_entity_t first = 0;
    int32_t i;
    for (i = 0; i < BOX_TERMINAL_SESSIONS; i ++) {
        ecs_entity_t s = box_terminal_new(world);
        test_assert(s != 0);
        first = first ? first : s;
    }

    ecs_log_set_level(-4);
    test_assert(box_terminal_new(world) == 0);

    ecs_http_reply_t reply = ECS_HTTP_REPLY_INIT;
    test_int(box_command_request(world, "GET", "/terminal/new", NULL, &reply), 400);
    char *body = ecs_strbuf_get(&reply.body);
    char *expect = flecs_asprintf("error: too many sessions, %d are open",
        BOX_TERMINAL_SESSIONS);
    test_str(body, expect);
    ecs_os_free(expect);
    ecs_os_free(body);

    ecs_delete(world, first);
    test_assert(box_terminal_new(world) != 0);

    ecs_fini(world);
}

void Limits_session_cap_set(void) {
    // hardening/command_limits
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_singleton_get_mut(world, TerminalLimits)->sessions = 2;
    ecs_singleton_modified(world, TerminalLimits);

    test_assert(box_terminal_new(world) != 0);
    test_assert(box_terminal_new(world) != 0);
    ecs_log_set_level(-4);
    test_assert(box_terminal_new(world) == 0);

    ecs_fini(world);
}

void Limits_idle_session_closed(void) {
    // hardening/command_limits
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_flt(ecs_singleton_get(world, TerminalLimits)->idle_timeout, BOX_TERMINAL_IDLE_TIMEOUT);
    ecs_entity_t s = box_terminal_new(world);
    ecs_entity_t agent = ecs_entity(world, { .name = "agent" });
    ecs_add(world, agent, Terminal);
    char *dir = ecs_os_strdup(ecs_get(world, s, TerminalStatus)->dir);

    ecs_progress(world, 0);
    test_assert(ecs_is_alive(world, s));

    ecs_singleton_get_mut(world, TerminalLimits)->idle_timeout = 0;
    ecs_singleton_modified(world, TerminalLimits);
    ecs_progress(world, 0);
    test_assert(!ecs_is_alive(world, s));
    test_assert(box_terminal_lookup(world, "s1") == 0);
    test_assert(ecs_has(world, agent, TerminalStatus));

    struct stat st;
    test_assert(stat(dir, &st) != 0);

    ecs_os_free(dir);
    ecs_fini(world);
}

void Limits_history_bounded(void) {
    // hardening/command_limits
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_singleton_get_mut(world, TerminalLimits)->history = 5;
    ecs_singleton_modified(world, TerminalLimits);

    ecs_entity_t s = box_terminal_new(world);
    int32_t i;
    for (i = 0; i < 20; i ++) {
        char *line = flecs_asprintf("nope %d", i);
        ecs_os_free(box_terminal_run(world, s, line, NULL));
        ecs_os_free(line);
    }

    test_int(ecs_get(world, s, TerminalStatus)->commands, 20);
    char *out = box_terminal_run(world, s, "commands", NULL);
    test_str(out,
        "$ nope 15\nerror: unknown command 'nope', see help\n"
        "$ nope 16\nerror: unknown command 'nope', see help\n"
        "$ nope 17\nerror: unknown command 'nope', see help\n"
        "$ nope 18\nerror: unknown command 'nope', see help\n"
        "$ nope 19\nerror: unknown command 'nope', see help");
    ecs_os_free(out);

    ecs_fini(world);
}

void Limits_history_default_bounded(void) {
    // hardening/command_limits
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_int(ecs_singleton_get(world, TerminalLimits)->history, BOX_TERMINAL_HISTORY);
    ecs_entity_t s = box_terminal_new(world);
    int32_t i, lines = 0;
    for (i = 0; i < BOX_TERMINAL_HISTORY + 50; i ++) {
        ecs_os_free(box_terminal_run(world, s, "nope", NULL));
    }

    char *out = box_terminal_run(world, s, "commands", NULL);
    const char *p = out;
    while ((p = strstr(p, "$ "))) {
        lines ++;
        p += 2;
    }

    test_int(lines, BOX_TERMINAL_HISTORY);
    ecs_os_free(out);

    ecs_fini(world);
}

void Limits_log_rotated(void) {
    // hardening/command_limits
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_int(ecs_singleton_get(world, TerminalLimits)->log_size, BOX_TERMINAL_LOG_SIZE);
    ecs_singleton_get_mut(world, TerminalLimits)->log_size = 200;
    ecs_singleton_modified(world, TerminalLimits);

    ecs_entity_t s = box_terminal_new(world);
    int32_t i;
    for (i = 0; i < 100; i ++) {
        ecs_os_free(box_terminal_run(world, s, "nope", NULL));
    }

    const char *dir = ecs_get(world, s, TerminalStatus)->dir;
    char *log = flecs_asprintf("%s/commands.log", dir);
    char *old = flecs_asprintf("%s/commands.log.1", dir);
    struct stat st;
    test_int(stat(log, &st), 0);
    test_assert(st.st_size <= 200);
    test_int(stat(old, &st), 0);
    test_assert(st.st_size <= 200);
    test_assert(st.st_size > 0);

    ecs_os_free(old);
    ecs_os_free(log);
    ecs_fini(world);
}

void Limits_images_bounded(void) {
    // hardening/command_limits
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    test_int(ecs_singleton_get(world, TerminalLimits)->images, BOX_TERMINAL_IMAGES);
    ecs_singleton_get_mut(world, TerminalLimits)->images = 2;
    ecs_singleton_modified(world, TerminalLimits);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 5});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 8, .height = 8, .resolution_scale = 1});
    ecs_entity_t s = box_terminal_new(world);
    ecs_progress(world, 0);

    int32_t i;
    for (i = 0; i < 4; i ++) {
        ecs_os_free(box_terminal_run(world, s, "capture", NULL));
        ecs_progress(world, 0);
        box_render_sync(world);
    }

    const char *dir = ecs_get(world, s, TerminalStatus)->dir;
    struct stat st;
    for (i = 0; i < 4; i ++) {
        char *path = flecs_asprintf("%s/resources/capture_%d.png", dir, i);
        if (i < 2) {
            test_assert(stat(path, &st) != 0);
        } else {
            test_int(stat(path, &st), 0);
        }

        ecs_os_free(path);
    }

    ecs_fini(world);
}

void Limits_images_pending_not_evicted(void) {
    // hardening/command_limits
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_singleton_get_mut(world, TerminalLimits)->images = 2;
    ecs_singleton_modified(world, TerminalLimits);

    ecs_entity_t camera = ecs_entity(world, { .name = "cam" });
    ecs_set(world, camera, Position, {0, 0, 5});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 8, .height = 8, .resolution_scale = 1});
    ecs_entity_t s = box_terminal_new(world);
    ecs_progress(world, 0);

    char *resources = flecs_asprintf("%s/resources", ecs_get(world, s, TerminalStatus)->dir);
    int32_t round;
    for (round = 0; round < 3; round ++) {
        bool error = false;
        char *out = box_terminal_run(world, s, "capture cam cam cam cam", &error);
        test_bool(error, true);
        test_str(out, "error: 2 images are still being written, TerminalLimits.images is 2");
        ecs_os_free(out);

        ecs_progress(world, 0);
        box_render_sync(world);

        int32_t pngs = 0;
        DIR *dir = opendir(resources);
        test_assert(dir != NULL);
        struct dirent *entry;
        while ((entry = readdir(dir))) {
            const char *ext = strrchr(entry->d_name, '.');
            pngs += ext && !strcmp(ext, ".png");
        }

        closedir(dir);
        test_int(pngs, 2);
    }

    ecs_os_free(resources);
    ecs_fini(world);
}

void Limits_next_out_of_range_keeps_iteration(void) {
    // hardening/command_limits
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t parent = ecs_entity(world, { .name = "parent" });
    ecs_entity(world, { .name = "e1", .parent = parent });
    ecs_entity(world, { .name = "e2", .parent = parent });
    ecs_entity(world, { .name = "e3", .parent = parent });

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "query q (ChildOf, parent)", NULL));
    ecs_os_free(box_terminal_run(world, s, "iter q", NULL));

    bool error = true;
    char *out = box_terminal_run(world, s, "next", &error);
    test_bool(error, false);
    test_str(out, "parent.e1");
    ecs_os_free(out);

    out = box_terminal_run(world, s, "next 5000", &error);
    test_bool(error, true);
    ecs_os_free(out);

    out = box_terminal_run(world, s, "next", &error);
    test_bool(error, false);
    test_str(out, "parent.e2");
    ecs_os_free(out);

    ecs_fini(world);
}

void Limits_command_resets_idle_timer(void) {
    // hardening/command_limits
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_singleton_get_mut(world, TerminalLimits)->idle_timeout = 0.2;
    ecs_singleton_modified(world, TerminalLimits);

    ecs_entity_t active = box_terminal_new(world);
    ecs_entity_t idle = box_terminal_new(world);
    test_assert(active != 0);
    test_assert(idle != 0);

    ecs_sleepf(0.3);
    ecs_os_free(box_terminal_run(world, active, "commands", NULL));
    ecs_progress(world, 0);

    test_assert(ecs_is_alive(world, active));
    test_assert(ecs_has(world, active, TerminalStatus));
    test_assert(!ecs_is_alive(world, idle));

    ecs_fini(world);
}

void Limits_images_evicted_once_written(void) {
    // render_fixes/capture_async
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_singleton_set(world, RenderSettings, {.in_flight = 2, .main_thread = true});
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_singleton_get_mut(world, TerminalLimits)->images = 1;
    ecs_singleton_modified(world, TerminalLimits);

    ecs_entity_t camera = ecs_entity(world, { .name = "cam" });
    ecs_set(world, camera, Position, {0, 0, 5});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {
        .camera = camera, .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t surface = ecs_new(world);
    ecs_set(world, surface, Surface, {.width = 8, .height = 8, .resolution_scale = 1});
    ecs_entity_t s = box_terminal_new(world);
    ecs_progress(world, 0);

    char *resources = flecs_asprintf("%s/resources", ecs_get(world, s, TerminalStatus)->dir);
    int32_t round, frame;
    for (round = 0; round < 4; round ++) {
        int32_t until = ecs_singleton_get(world, RenderStatus)->submitted + 2;
        ecs_os_free(box_terminal_run(world, s, "capture cam", NULL));
        for (frame = 0; frame < 8 &&
            ecs_singleton_get(world, RenderStatus)->rendered < until; frame ++)
        {
            ecs_progress(world, 0);
        }
    }

    for (frame = 0; frame < 4; frame ++) {
        ecs_progress(world, 0);
    }

    box_render_sync(world);

    int32_t pngs = 0;
    DIR *dir = opendir(resources);
    test_assert(dir != NULL);
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        const char *ext = strrchr(entry->d_name, '.');
        pngs += ext && !strcmp(ext, ".png");
    }

    closedir(dir);
    test_int(pngs, 1);

    ecs_os_free(resources);
    ecs_fini(world);
}

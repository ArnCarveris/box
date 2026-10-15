/**
 * @file Console.c
 * @brief The in game console: toggle, typing, running lines, history, look and drawing.
 */

#include <box.h>
#include <bake_test.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"

#define CONSOLE_INPUT "console.ui.screen.stack.panel.content.input"
#define CONSOLE_LOG "console.ui.screen.stack.panel.content.log"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

void Console_toggle_opens(void) {
    // ui/console
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 640, .height = 360, .resolution_scale = 1});
    ecs_progress(world, 0);
    test_bool(ecs_singleton_get(world, Console)->open, false);
    test_assert(ecs_lookup(world, "console.view") == 0);

    box_input_key_type(world, "`", 1);
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    test_bool(ecs_singleton_get(world, Console)->open, true);
    ecs_entity_t view = ecs_lookup(world, "console.view");
    test_assert(view != 0);
    ecs_entity_t camera = ecs_get(world, view, View)->camera;
    test_assert(camera == ecs_lookup(world, "console.ui.camera"));
    const Camera *cam = ecs_get(world, camera, Camera);
    test_bool(cam->ortho, true);
    test_flt(cam->ortho_scale, 1);
    test_assert(ecs_has(world, ecs_lookup(world, "console.ui"), Scene));
    const View *v = ecs_get(world, view, View);
    test_bool(v->clear.enabled, false);
    test_bool(v->lighting.enabled, false);
    test_bool(v->shadows.enabled, false);
    test_bool(v->culling.enabled, false);
    test_int(ecs_vec_count(&v->effects), 0);
    const Text *before = ecs_get(world, ecs_lookup(world, CONSOLE_INPUT ".before"),
        Text);
    test_str(before->value, "");

    ecs_fini(world);
}

void Console_toggle_closes(void) {
    // ui/console
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 640, .height = 360, .resolution_scale = 1});
    ecs_progress(world, 0);

    ecs_entity_t t = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, t, "key.type `", NULL));
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    test_bool(ecs_singleton_get(world, Console)->open, true);
    ecs_os_free(box_terminal_run(world, t, "key.type a`", NULL));
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    test_bool(ecs_singleton_get(world, Console)->open, false);
    test_assert(ecs_lookup(world, "console.view") == 0);
    const Text *before = ecs_get(world, ecs_lookup(world, CONSOLE_INPUT ".before"),
        Text);
    test_str(before->value, "a");

    ecs_os_free(box_terminal_run(world, t, "key.type `", NULL));
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    test_bool(ecs_singleton_get(world, Console)->open, true);
    test_assert(ecs_lookup(world, "console.view") != 0);

    ecs_fini(world);
}

void Console_escape_closes(void) {
    // ui/console
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 640, .height = 360, .resolution_scale = 1});
    ecs_progress(world, 0);

    box_input_key_type(world, "`", 1);
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    test_bool(ecs_singleton_get(world, Console)->open, true);
    box_input_key_press(world, KeyEscape, 1);
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    test_bool(ecs_singleton_get(world, Console)->open, false);
    test_assert(ecs_lookup(world, "console.view") == 0);

    ecs_fini(world);
}

void Console_toggle_remapped(void) {
    // ui/console
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 640, .height = 360, .resolution_scale = 1});
    ecs_entity_t toggle = ecs_lookup(world, BOX_CONSOLE_TOGGLE);
    test_assert(toggle != 0);
    test_assert(ecs_has(world, toggle, Action));
    ecs_set(world, toggle, Binding, { .key = KeyF1 });
    ecs_progress(world, 0);

    box_input_key_type(world, "`", 1);
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    test_bool(ecs_singleton_get(world, Console)->open, false);
    box_input_key_press(world, KeyF1, 1);
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    test_bool(ecs_singleton_get(world, Console)->open, true);
    box_input_key_press(world, KeyF1, 1);
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    test_bool(ecs_singleton_get(world, Console)->open, false);

    ecs_fini(world);
}

void Console_typing_goes_to_input(void) {
    // ui/console
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 640, .height = 360, .resolution_scale = 1});
    ecs_progress(world, 0);

    ecs_entity_t t = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, t, "key.type `", NULL));
    ecs_os_free(box_terminal_run(world, t, "key.type \"ls things\"", NULL));
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    ecs_entity_t before = ecs_lookup(world, CONSOLE_INPUT ".before");
    ecs_entity_t after = ecs_lookup(world, CONSOLE_INPUT ".after");
    test_str(ecs_get(world, before, Text)->value, "ls things");
    test_str(ecs_get(world, after, Text)->value, "");
    test_str(ecs_singleton_get(world, Input)->text, "");

    ecs_fini(world);
}

void Console_typing_fires_no_actions(void) {
    // ui/console
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 640, .height = 360, .resolution_scale = 1});
    ecs_entity_t jump = ecs_entity(world, { .name = "controls.Jump" });
    ecs_set(world, jump, Action, { 0 });
    ecs_set(world, jump, Binding, { .key = KeyJ });
    ecs_entity_t fire = ecs_entity(world, { .name = "controls.Fire" });
    ecs_set(world, fire, Action, { 0 });
    ecs_set(world, fire, Binding, { .key = KeyMouseLeft });
    ecs_progress(world, 0);

    box_input_key_type(world, "`", 1);
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    box_input_key_type(world, "jj", 1);
    box_input_key(world, KeyJ, true);
    box_input_mouse_click(world, KeyMouseLeft, NULL, false, 2);
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
        test_bool(ecs_get(world, jump, ActionState)->down, false);
        test_bool(ecs_get(world, jump, ActionState)->pressed, false);
        test_bool(ecs_get(world, fire, ActionState)->down, false);
    }

    ecs_progress(world, 0);
    test_bool(ecs_get(world, jump, ActionState)->down, false);
    test_str(ecs_get(world, ecs_lookup(world, CONSOLE_INPUT ".before"), Text)->value,
        "jj");

    ecs_fini(world);
}

void Console_close_restores_actions(void) {
    // ui/console
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 640, .height = 360, .resolution_scale = 1});
    ecs_entity_t jump = ecs_entity(world, { .name = "controls.Jump" });
    ecs_set(world, jump, Action, { 0 });
    ecs_set(world, jump, Binding, { .key = KeyJ });
    ecs_progress(world, 0);

    ecs_entity_t t = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, t, "key.type `j", NULL));
    ecs_os_free(box_terminal_run(world, t, "key.press Escape", NULL));
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
        test_bool(ecs_get(world, jump, ActionState)->down, false);
    }

    test_bool(ecs_singleton_get(world, Console)->open, false);
    ecs_os_free(box_terminal_run(world, t, "key.press J 2", NULL));
    ecs_progress(world, 0);
    test_bool(ecs_get(world, jump, ActionState)->pressed, true);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, jump, ActionState)->down, true);
    ecs_progress(world, 0);
    test_bool(ecs_get(world, jump, ActionState)->released, true);

    ecs_fini(world);
}

void Console_enter_runs_line(void) {
    // ui/console
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 640, .height = 360, .resolution_scale = 1});
    ecs_entity(world, { .name = "things.alpha" });
    ecs_entity(world, { .name = "things.beta" });
    ecs_progress(world, 0);

    ecs_entity_t t = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, t, "key.type `", NULL));
    ecs_os_free(box_terminal_run(world, t, "key.type \"ls things\"", NULL));
    ecs_os_free(box_terminal_run(world, t, "key.press Enter", NULL));
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    const Console *c = ecs_singleton_get(world, Console);
    const char *expect[] = {"> ls things", "alpha", "beta"};
    int32_t i, lines = 0;
    while (true) {
        char name[64];
        ecs_os_snprintf(name, sizeof(name), CONSOLE_LOG ".line_%d", lines);
        if (!ecs_lookup(world, name)) {
            break;
        }

        lines ++;
    }

    test_assert(lines >= 3);
    for (i = 0; i < 3; i ++) {
        char name[64];
        ecs_os_snprintf(name, sizeof(name), CONSOLE_LOG ".line_%d", lines - 3 + i);
        ecs_entity_t e = ecs_lookup(world, name);
        test_str(ecs_get(world, e, Text)->value, expect[i]);
        const Rgb *color = ecs_get(world, e, Rgb);
        const rgb *want = i ? &c->text : &c->command;
        test_int(color->r, want->r);
        test_int(color->g, want->g);
        test_int(color->b, want->b);
    }

    test_str(ecs_get(world, ecs_lookup(world, CONSOLE_INPUT ".before"), Text)->value,
        "");

    ecs_fini(world);
}

void Console_line_logged_in_session(void) {
    // ui/console
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 640, .height = 360, .resolution_scale = 1});
    ecs_entity(world, { .name = "things.alpha" });
    ecs_progress(world, 0);

    box_input_key_type(world, "`ls things", 1);
    box_input_key_press(world, KeyEnter, 1);
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    ecs_entity_t session = ecs_lookup(world, BOX_CONSOLE_SESSION);
    test_assert(session != 0);
    test_assert(ecs_has(world, session, Terminal));
    const TerminalStatus *st = ecs_get(world, session, TerminalStatus);
    test_int(st->commands, 1);
    char *path = flecs_asprintf("%s/commands.log", st->dir);
    char log[256] = {0};
    FILE *f = fopen(path, "r");
    test_assert(f != NULL);
    fread(log, 1, sizeof(log) - 1, f);
    fclose(f);
    test_str(log, "$ ls things\nalpha\n");
    ecs_os_free(path);

    char *out = box_terminal_run(world, session, "commands", NULL);
    test_str(out, "$ ls things\nalpha");
    ecs_os_free(out);

    ecs_fini(world);
}

void Console_error_output(void) {
    // ui/console
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 640, .height = 360, .resolution_scale = 1});
    ecs_progress(world, 0);

    box_input_key_type(world, "`ls nothing", 1);
    box_input_key_press(world, KeyEnter, 1);
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    const Console *c = ecs_singleton_get(world, Console);
    int32_t lines = 0;
    while (true) {
        char name[64];
        ecs_os_snprintf(name, sizeof(name), CONSOLE_LOG ".line_%d", lines);
        if (!ecs_lookup(world, name)) {
            break;
        }

        lines ++;
    }

    char name[64];
    ecs_os_snprintf(name, sizeof(name), CONSOLE_LOG ".line_%d", lines - 1);
    ecs_entity_t e = ecs_lookup(world, name);
    test_assert(!strncmp(ecs_get(world, e, Text)->value, BOX_COMMAND_ERROR,
        strlen(BOX_COMMAND_ERROR)));
    test_int(ecs_get(world, e, Rgb)->r, c->error.r);
    test_int(ecs_get(world, e, Rgb)->g, c->error.g);
    test_int(ecs_get(world, e, Rgb)->b, c->error.b);

    ecs_fini(world);
}

void Console_history(void) {
    // ui/console
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 640, .height = 360, .resolution_scale = 1});
    ecs_progress(world, 0);

    box_input_key_type(world, "`ls", 1);
    box_input_key_press(world, KeyEnter, 1);
    box_input_key_type(world, "man ls", 1);
    box_input_key_press(world, KeyEnter, 1);
    box_input_key_type(world, "dra", 1);
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    ecs_entity_t before = ecs_lookup(world, CONSOLE_INPUT ".before");
    const char *expect[] = {"man ls", "ls", "ls", "man ls", "dra"};
    Key keys[] = {KeyUp, KeyUp, KeyUp, KeyDown, KeyDown};
    int32_t i;
    for (i = 0; i < 5; i ++) {
        box_input_key_press(world, keys[i], 1);
        while (box_input_pending(world)) {
            ecs_progress(world, 0);
        }

        test_str(ecs_get(world, before, Text)->value, expect[i]);
    }

    ecs_fini(world);
}

void Console_cursor_editing(void) {
    // ui/console
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 640, .height = 360, .resolution_scale = 1});
    ecs_progress(world, 0);

    box_input_key_type(world, "`acd", 1);
    box_input_key_press(world, KeyLeft, 1);
    box_input_key_press(world, KeyLeft, 1);
    box_input_key_type(world, "b", 1);
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    ecs_entity_t before = ecs_lookup(world, CONSOLE_INPUT ".before");
    ecs_entity_t after = ecs_lookup(world, CONSOLE_INPUT ".after");
    test_str(ecs_get(world, before, Text)->value, "ab");
    test_str(ecs_get(world, after, Text)->value, "cd");

    box_input_key_press(world, KeyBackspace, 1);
    box_input_key_press(world, KeyDelete, 1);
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    test_str(ecs_get(world, before, Text)->value, "a");
    test_str(ecs_get(world, after, Text)->value, "d");

    box_input_key_press(world, KeyHome, 1);
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    test_str(ecs_get(world, before, Text)->value, "");
    test_str(ecs_get(world, after, Text)->value, "ad");

    box_input_key_press(world, KeyEnd, 1);
    box_input_key_type(world, "x ", 1);
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    test_str(ecs_get(world, before, Text)->value, "adx\xC2\xA0");
    test_str(ecs_get(world, after, Text)->value, "");
    float x = ecs_get(world, before, TextBounds)->width;
    box_input_key_type(world, " ", 1);
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    test_assert(ecs_get(world, before, TextBounds)->width > x);

    ecs_fini(world);
}

void Console_scroll(void) {
    // ui/console
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 640, .height = 360, .resolution_scale = 1});
    int32_t i;
    for (i = 0; i < 40; i ++) {
        char name[32];
        ecs_os_snprintf(name, sizeof(name), "many.c%02d", i);
        ecs_entity(world, { .name = name });
    }

    ecs_progress(world, 0);
    box_input_key_type(world, "`ls many", 1);
    box_input_key_press(world, KeyEnter, 1);
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    int32_t lines = 0;
    while (true) {
        char name[64];
        ecs_os_snprintf(name, sizeof(name), CONSOLE_LOG ".line_%d", lines);
        if (!ecs_lookup(world, name)) {
            break;
        }

        lines ++;
    }

    test_assert(lines > 2);
    test_assert(lines < 40);
    ecs_entity_t first = ecs_lookup(world, CONSOLE_LOG ".line_0");
    char expect[32];
    ecs_os_snprintf(expect, sizeof(expect), "c%02d", 40 - lines);
    test_str(ecs_get(world, first, Text)->value, expect);

    box_input_key_press(world, KeyPageUp, 1);
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    ecs_os_snprintf(expect, sizeof(expect), "c%02d", 40 - lines - (lines - 1));
    test_str(ecs_get(world, first, Text)->value, expect);

    box_input_mouse_scroll(world, 0, -1);
    ecs_progress(world, 0);
    ecs_os_snprintf(expect, sizeof(expect), "c%02d", 40 - lines - (lines - 1) + 3);
    test_str(ecs_get(world, first, Text)->value, expect);

    box_input_key_press(world, KeyPageDown, 1);
    box_input_key_press(world, KeyPageDown, 1);
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    ecs_os_snprintf(expect, sizeof(expect), "c%02d", 40 - lines);
    test_str(ecs_get(world, first, Text)->value, expect);

    ecs_fini(world);
}

void Console_view_stacked_last(void) {
    // ui/console
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 640, .height = 360, .resolution_scale = 1});
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t v1 = ecs_new(world);
    ecs_set(world, v1, View, {.camera = camera});
    ecs_progress(world, 0);

    box_input_key_type(world, "`", 1);
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    ecs_entity_t view = ecs_lookup(world, "console.view");
    test_assert(view > v1);

    ecs_entity_t v2 = ecs_new(world);
    ecs_set(world, v2, View, {.camera = camera});
    ecs_progress(world, 0);
    view = ecs_lookup(world, "console.view");
    test_assert(view > v2);
    test_assert(ecs_get(world, view, View)->camera ==
        ecs_lookup(world, "console.ui.camera"));

    ecs_fini(world);
}

void Console_rebuilt_after_delete(void) {
    // ui/console
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 640, .height = 360, .resolution_scale = 1});
    ecs_progress(world, 0);

    box_input_key_type(world, "`ab", 1);
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    ecs_delete(world, ecs_lookup(world, BOX_CONSOLE_SCOPE));
    ecs_progress(world, 0);
    test_assert(ecs_lookup(world, BOX_CONSOLE_VIEW) != 0);
    ecs_entity_t before = ecs_lookup(world, CONSOLE_INPUT ".before");
    test_assert(before != 0);
    test_str(ecs_get(world, before, Text)->value, "ab");

    box_input_key_press(world, KeyEnter, 1);
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    test_assert(ecs_lookup(world, BOX_CONSOLE_SESSION) != 0);

    ecs_fini(world);
}

void Console_look_from_script(void) {
    // ui/console
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 640, .height = 360, .resolution_scale = 1});
    test_assert(ecs_script_run(world, "look",
        "using box.console\n"
        "$ {\n"
        "  Console: {open: true, width: 0.5, height: 0.25, font_size: 24, line_height: 30,\n"
        "    padding: 5,\n"
        "    background: {255, 0, 0, 128}, input: {0, 255, 0}}\n"
        "}\n", NULL) == 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    test_bool(ecs_singleton_get(world, Console)->open, true);
    ecs_entity_t bg = ecs_lookup(world, "console.ui.screen.stack.panel");
    test_flt(ecs_get(world, bg, Rect)->width, 320);
    test_flt(ecs_get(world, bg, Rect)->height, 90);
    test_int(ecs_get(world, bg, Rgb)->r, 255);
    test_flt(ecs_get(world, bg, Opacity)->value, 128.0f / 255.0f);
    ecs_entity_t prompt = ecs_lookup(world, CONSOLE_INPUT ".prompt");
    test_flt(ecs_get(world, prompt, Text)->size, 24);
    test_flt(ecs_get(world, prompt, Text)->line_height, 30);
    test_int(ecs_get(world, prompt, Rgb)->g, 255);
    test_assert(ecs_lookup(world, CONSOLE_LOG ".line_0") != 0);
    test_assert(ecs_lookup(world, CONSOLE_LOG ".line_1") == 0);

    ecs_singleton_set(world, Console, {.open = true, .width = 1, .height = 0});
    ecs_progress(world, 0);
    test_flt(ecs_get(world, bg, Rect)->height, 0);
    test_assert(ecs_lookup(world, CONSOLE_LOG ".line_0") == 0);

    ecs_fini(world);
}

void Console_run_from_script(void) {
    // ui/console
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 640, .height = 360, .resolution_scale = 1});
    ecs_entity(world, { .name = "things.alpha" });
    test_assert(ecs_script_run(world, "run",
        "using box.text\n"
        "const out: string = box.console.run(\"ls things\")\n"
        "result {\n"
        "  Text: {value: $out}\n"
        "}\n", NULL) == 0);
    test_str(ecs_get(world, ecs_lookup(world, "result"), Text)->value, "alpha");

    ecs_singleton_set(world, Console, {.open = true, .height = 0.5f, .font_size = 16,
        .line_height = 20, .padding = 10, .scrollback = 100});
    ecs_progress(world, 0);
    test_str(ecs_get(world, ecs_lookup(world, CONSOLE_LOG ".line_5"), Text)->value,
        "> ls things");
    test_str(ecs_get(world, ecs_lookup(world, CONSOLE_LOG ".line_6"), Text)->value,
        "alpha");

    box_input_key_press(world, KeyUp, 1);
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    test_str(ecs_get(world, ecs_lookup(world, CONSOLE_INPUT ".before"), Text)->value,
        "ls things");

    ecs_fini(world);
}

void Console_scrollback_limit(void) {
    // ui/console
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 640, .height = 360, .resolution_scale = 1});
    ecs_entity(world, { .name = "things.alpha" });
    ecs_entity(world, { .name = "things.beta" });
    Console *c = ecs_singleton_get_mut(world, Console);
    c->scrollback = 2;
    c->open = true;
    ecs_singleton_modified(world, Console);
    ecs_os_free(box_console_run(world, "ls things", NULL));
    ecs_progress(world, 0);

    int32_t i;
    for (i = 0; i < 16; i ++) {
        char name[96];
        ecs_os_snprintf(name, sizeof(name),
            CONSOLE_LOG ".line_%d", i);
        const char *expect = i == 14 ? "alpha" : i == 15 ? "beta" : "";
        test_str(ecs_get(world, ecs_lookup(world, name), Text)->value, expect);
    }

    ecs_fini(world);
}

void Console_renders_over_scene(void) {
    // ui/console
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 5});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true,
        .clear = {.enabled = true, .color = {0, 0, 255, 255}}});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 320, .height = 180, .write_to_file = "console.png",
        .resolution_scale = 1});
    Console *c = ecs_singleton_get_mut(world, Console);
    c->open = true;
    c->background = (rgba){200, 0, 0, 255};
    ecs_singleton_modified(world, Console);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);

    char *path = flecs_asprintf("%s/console.png", ecs_singleton_get(world, AppStatus)->tmp_dir);
    int w = 0, h = 0, n = 0, x, lit = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    test_assert(px != NULL);
    unsigned char *top = &px[(4 * 320 + 150) * 4];
    test_assert(abs((int)top[0] - 200) < 3);
    test_assert(top[2] < 3);
    unsigned char *bottom = &px[(170 * 320 + 80) * 4];
    test_assert(abs((int)bottom[0] - 200) < 3);
    test_assert(bottom[2] < 3);
    unsigned char *right = &px[(90 * 320 + 200) * 4];
    test_assert(right[0] < 3);
    test_assert(right[2] > 252);
    int y;
    for (y = 140; y < 180; y ++) {
        for (x = 0; x < 40; x ++) {
            unsigned char *p = &px[(y * 320 + x) * 4];
            lit += p[1] > 128;
        }
    }

    test_assert(lit > 0);
    stbi_image_free(px);
    ecs_os_free(path);
    ecs_fini(world);
}

void Console_covers_left_half(void) {
    // ui/console_layout
    int32_t sizes[2][2] = {{640, 360}, {400, 300}};
    int32_t i;
    for (i = 0; i < 2; i ++) {
        float w = (float)sizes[i][0], h = (float)sizes[i][1];
        ecs_world_t *world = ecs_init();
        ECS_IMPORT(world, Box);
        App *app = ecs_singleton_get_mut(world, App);
        app->headless = true;
        ecs_singleton_modified(world, App);
        ecs_entity_t s = ecs_new(world);
        ecs_set(world, s, Surface, {.width = sizes[i][0], .height = sizes[i][1],
            .resolution_scale = 1});
        ecs_singleton_get_mut(world, Console)->open = true;
        ecs_singleton_modified(world, Console);
        ecs_progress(world, 0);
        ecs_progress(world, 0);

        ecs_entity_t stack = ecs_lookup(world, "console.ui.screen.stack");
        ecs_entity_t panel = ecs_lookup(world, "console.ui.screen.stack.panel");
        test_assert(panel != 0);
        test_flt(ecs_get(world, panel, Rect)->width, w / 2);
        test_flt(ecs_get(world, panel, Rect)->height, h);
        test_flt(ecs_get(world, stack, Position)->x + ecs_get(world, panel, Position)->x,
            -w / 4);
        test_flt(ecs_get(world, stack, Position)->y + ecs_get(world, panel, Position)->y, 0);
        ecs_fini(world);
    }
}

void Console_width_fraction(void) {
    // ui/console_layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 640, .height = 360, .resolution_scale = 1});
    Console *c = ecs_singleton_get_mut(world, Console);
    test_flt(c->width, 0.5f);
    test_flt(c->height, 1);
    c->open = true;
    c->width = 0.25f;
    ecs_singleton_modified(world, Console);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_entity_t panel = ecs_lookup(world, "console.ui.screen.stack.panel");
    test_flt(ecs_get(world, panel, Rect)->width, 160);
    test_flt(ecs_get(world, panel, Rect)->height, 360);

    c = ecs_singleton_get_mut(world, Console);
    c->width = 0;
    ecs_singleton_modified(world, Console);
    ecs_progress(world, 0);
    test_flt(ecs_get(world, panel, Rect)->width, 0);

    c = ecs_singleton_get_mut(world, Console);
    c->width = 1;
    ecs_singleton_modified(world, Console);
    ecs_progress(world, 0);
    test_flt(ecs_get(world, panel, Rect)->width, 640);
    ecs_fini(world);
}

void Console_follows_resize(void) {
    // ui/console_layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 640, .height = 360, .resolution_scale = 1});
    ecs_singleton_get_mut(world, Console)->open = true;
    ecs_singleton_modified(world, Console);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_entity_t panel = ecs_lookup(world, "console.ui.screen.stack.panel");
    ecs_entity_t stack = ecs_lookup(world, "console.ui.screen.stack");
    test_flt(ecs_get(world, panel, Rect)->width, 320);

    ecs_set(world, s, Surface, {.width = 1000, .height = 500, .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_flt(ecs_get(world, panel, Rect)->width, 500);
    test_flt(ecs_get(world, panel, Rect)->height, 500);
    test_flt(ecs_get(world, stack, Position)->x + ecs_get(world, panel, Position)->x, -250);

    ecs_set(world, s, Surface, {.width = 1000, .height = 500, .resolution_scale = 2,
        .pixel_ratio = 2});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_flt(ecs_get(world, panel, Rect)->width, 500);
    test_flt(ecs_get(world, panel, Rect)->height, 500);

    ecs_set(world, s, Surface, {.width = 300, .height = 600, .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_flt(ecs_get(world, panel, Rect)->width, 150);
    test_flt(ecs_get(world, panel, Rect)->height, 600);
    ecs_fini(world);
}

void Console_scrollback_follows_height(void) {
    // ui/console_layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 640, .height = 360, .resolution_scale = 1});
    ecs_singleton_get_mut(world, Console)->open = true;
    ecs_singleton_modified(world, Console);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_assert(ecs_lookup(world, CONSOLE_LOG ".line_15") != 0);
    test_assert(ecs_lookup(world, CONSOLE_LOG ".line_16") == 0);

    ecs_set(world, s, Surface, {.width = 640, .height = 200, .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_assert(ecs_lookup(world, CONSOLE_LOG ".line_7") != 0);
    test_assert(ecs_lookup(world, CONSOLE_LOG ".line_8") == 0);
    ecs_entity_t log = ecs_lookup(world, CONSOLE_LOG "");
    test_flt(ecs_get(world, log, LayoutBounds)->height, 160);

    ecs_set(world, s, Surface, {.width = 640, .height = 440, .resolution_scale = 1});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_assert(ecs_lookup(world, CONSOLE_LOG ".line_19") != 0);
    test_assert(ecs_lookup(world, CONSOLE_LOG ".line_20") == 0);
    ecs_fini(world);
}

void Console_panel_keeps_fraction_with_long_text(void) {
    // ui_fixes/grow_basis
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 640, .height = 360, .resolution_scale = 1});
    ecs_singleton_get_mut(world, Console)->open = true;
    ecs_singleton_modified(world, Console);
    ecs_progress(world, 0);

    char line[201];
    for (int32_t i = 0; i < 200; i ++) {
        line[i] = (char)('a' + i % 26);
    }

    line[200] = '\0';
    ecs_os_free(box_console_run(world, line, NULL));
    for (int32_t i = 0; i < 200; i ++) {
        box_input_platform_push(world, &(InputEvent){ .kind = InputText, .code = line[i] });
    }

    ecs_progress(world, 0);
    ecs_progress(world, 0);

    ecs_entity_t before = ecs_lookup(world, CONSOLE_INPUT ".before");
    test_str(ecs_get(world, before, Text)->value, line);
    test_assert(ecs_get(world, before, TextBounds)->width > 640);
    ecs_entity_t log = ecs_lookup(world, CONSOLE_LOG);
    float widest = 0;
    ecs_iter_t it = ecs_children(world, log);
    while (ecs_children_next(&it)) {
        for (int32_t i = 0; i < it.count; i ++) {
            const TextBounds *tb = ecs_get(world, it.entities[i], TextBounds);
            widest = tb && tb->width > widest ? tb->width : widest;
        }
    }

    test_assert(widest > 640);
    ecs_entity_t stack = ecs_lookup(world, "console.ui.screen.stack");
    ecs_entity_t panel = ecs_lookup(world, "console.ui.screen.stack.panel");
    test_flt(ecs_get(world, stack, LayoutBounds)->width, 320);
    test_flt(ecs_get(world, panel, Rect)->width, 320);
    test_flt(ecs_get(world, panel, Rect)->height, 360);
    test_flt(ecs_get(world, stack, Position)->x + ecs_get(world, panel, Position)->x, -160);

    ecs_fini(world);
}

void Console_paste_long(void) {
    // input_fixes/text_input_limit
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 640, .height = 360, .resolution_scale = 1});
    ecs_progress(world, 0);

    ecs_entity_t t = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, t, "key.type `", NULL));
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    char paste[1001];
    for (int32_t i = 0; i < 1000; i ++) {
        paste[i] = (char)('a' + i % 26);
        box_input_platform_push(world, &(InputEvent){ .kind = InputText, .code = paste[i] });
    }

    paste[1000] = '\0';
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    ecs_entity_t before = ecs_lookup(world, CONSOLE_INPUT ".before");
    test_str(ecs_get(world, before, Text)->value, paste);
    test_str(ecs_singleton_get(world, Input)->text, "");

    ecs_fini(world);
}

void Console_null_text(void) {
    // input_fixes/text_input_limit
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 640, .height = 360, .resolution_scale = 1});
    ecs_progress(world, 0);

    ecs_entity_t t = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, t, "key.type `", NULL));
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    ecs_singleton_set(world, Input, { .focused = true });
    ecs_progress(world, 0);
    ecs_os_free(box_terminal_run(world, t, "key.type \"ab\"", NULL));
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    ecs_entity_t before = ecs_lookup(world, CONSOLE_INPUT ".before");
    test_str(ecs_get(world, before, Text)->value, "ab");

    ecs_fini(world);
}

void Console_cursor_ill_formed(void) {
    // input_fixes/input_utf8
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 640, .height = 360, .resolution_scale = 1});
    ecs_progress(world, 0);

    box_input_key_type(world, "`", 1);
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    ecs_singleton_set(world, Input, { .text = "a\x80\x80\xE2\x82" "b", .focused = true });
    ecs_progress(world, 0);
    ecs_entity_t before = ecs_lookup(world, CONSOLE_INPUT ".before");
    ecs_entity_t after = ecs_lookup(world, CONSOLE_INPUT ".after");
    test_str(ecs_get(world, before, Text)->value, "a\x80\x80\xE2\x82" "b");

    const char *expect[5] = { "a\x80\x80\xE2\x82", "a\x80\x80", "a\x80", "a", "" };
    for (int32_t i = 0; i < 5; i ++) {
        box_input_key_press(world, KeyLeft, 1);
        while (box_input_pending(world)) {
            ecs_progress(world, 0);
        }

        test_str(ecs_get(world, before, Text)->value, expect[i]);
    }

    box_input_key_press(world, KeyRight, 1);
    box_input_key_press(world, KeyRight, 1);
    box_input_key_press(world, KeyDelete, 1);
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    test_str(ecs_get(world, before, Text)->value, "a\x80");
    test_str(ecs_get(world, after, Text)->value, "\xE2\x82" "b");

    box_input_key_press(world, KeyBackspace, 1);
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    test_str(ecs_get(world, before, Text)->value, "a");
    test_str(ecs_get(world, after, Text)->value, "\xE2\x82" "b");

    ecs_fini(world);
}

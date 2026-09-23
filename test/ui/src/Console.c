/**
 * @file Console.c
 * @brief Tests for the in game console: its binding, its session and its scrollback.
 */

#include <box.h>
#include <bake_test.h>
#include "../../../src/ui/ui_internal.h"

static ecs_world_t *w;

static ecs_entity_t Console_log(void)
{
    ecs_iter_t it = ecs_each(w, ConsoleLog);
    ecs_entity_t result = 0;
    while (ecs_each_next(&it) && !result) {
        if (it.count) {
            result = it.entities[0];
        }
    }

    return result;
}

static bool Console_has_color(
    ecs_entity_t log,
    Rgb color)
{
    bool result = false;
    ecs_iter_t it = ecs_children(w, log);
    while (ecs_children_next(&it)) {
        int32_t i;
        for (i = 0; i < it.count; i ++) {
            const Emissive *emissive = ecs_get(w, it.entities[i], Emissive);
            if (ecs_has_id(w, it.entities[i], UiGlyph) && emissive &&
                emissive->color.r == color.r && emissive->color.g == color.g &&
                emissive->color.b == color.b)
            {
                ecs_iter_fini(&it);
                result = true;
                goto done;
            }
        }
    }

done:
    return result;
}

static void Console_run(
    const char *line)
{
    ecs_entity_t session = ecs_singleton_get(w, Console)->session;
    ecs_set(w, session, TerminalInput, { .line = ecs_os_strdup(line) });
    ecs_progress(w, 0);
}

void Console_setup(void)
{
    w = ecs_init();
    ECS_IMPORT(w, Box);
    Surface *s = ecs_singleton_ensure(w, Surface);
    s->width = 960;
    s->height = 540;
}

void Console_teardown(void)
{
    ecs_fini(w);
}

void Console_singleton_installs_the_grave_binding(void)
{
    // ui/console
    ecs_singleton_set(w, Console, { .open = false });

    ecs_entity_t action = ecs_lookup(w, "actions.Console");
    test_assert(action != 0);
    test_assert(ecs_has(w, action, InputAction));
    test_assert(ecs_lookup(w, "actions.ConsoleLine") != 0);

    ecs_entity_t map = ecs_lookup(w, BOX_UI_CONSOLE_MAP);
    test_assert(map != 0);
    test_bool(ecs_get(w, map, InputMap)->enabled, true);

    ecs_entity_t binding = ecs_lookup_child(w, map, "grave");
    test_assert(binding != 0);
    test_uint(ecs_get(w, binding, Binding)->mask, BoxKeyGrave);
    test_uint(ecs_get(w, binding, Binding)->action, action);
}

void Console_grave_key_opens_and_closes_the_overlay(void)
{
    // ui/console
    ecs_singleton_set(w, Console, { .open = false });
    ecs_progress(w, 0);

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyGrave, .duration = 1 });
    ecs_progress(w, 0);
    test_bool(ecs_singleton_get(w, Console)->open, true);

    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyGrave, .duration = 1 });
    ecs_progress(w, 0);
    test_bool(ecs_singleton_get(w, Console)->open, false);
}

void Console_open_loads_the_script_and_takes_its_parts(void)
{
    // ui/console
    ecs_singleton_set(w, Console, { .open = true });
    ecs_progress(w, 0);

    const Console *console = ecs_singleton_get(w, Console);
    test_assert(console->root != 0);
    test_assert(ecs_has(w, console->root, Ui));

    ecs_iter_t it = ecs_each(w, ConsoleLog);
    test_bool(ecs_each_next(&it), true);
    test_assert(it.count >= 1);
    ecs_iter_fini(&it);

    it = ecs_each(w, TextInput);
    test_bool(ecs_each_next(&it), true);
    test_bool(ecs_get(w, it.entities[0], TextInput)->focused, true);
    ecs_iter_fini(&it);
}

void Console_open_creates_its_terminal_session(void)
{
    // ui/console
    ecs_singleton_set(w, Console, { .open = true });
    ecs_progress(w, 0);

    ecs_entity_t session = ecs_singleton_get(w, Console)->session;
    test_assert(session != 0);
    test_assert(ecs_has(w, session, Terminal));
    test_str(ecs_get_name(w, session), BOX_UI_CONSOLE_SESSION);
    test_assert(ecs_get(w, session, Terminal)->session_dir != NULL);
}

void Console_open_holds_the_keyboard(void)
{
    // ui/console
    ecs_entity_t map = ecs_entity(w, { .name = "game_map" });
    ecs_set(w, map, InputMap, { .enabled = true });
    ecs_singleton_set(w, Console, { .open = false });
    ecs_progress(w, 0);

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyGrave, .duration = 1 });
    ecs_progress(w, 0);
    test_bool(ecs_get(w, map, InputMap)->enabled, false);
    test_bool(ecs_get(w, ecs_lookup(w, BOX_UI_CONSOLE_MAP), InputMap)->enabled,
        true);

    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyGrave, .duration = 1 });
    ecs_progress(w, 0);
    test_bool(ecs_get(w, map, InputMap)->enabled, true);
}

void Console_closed_overlay_is_not_drawn(void)
{
    // ui/console
    ecs_singleton_set(w, Console, { .open = true });
    ecs_progress(w, 0);

    ecs_entity_t root = ecs_singleton_get(w, Console)->root;
    ecs_entity_t view = ecs_get(w, root, UiImpl)->view;
    test_assert(view != 0);
    test_bool(ecs_get(w, view, View)->enabled, true);

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyGrave, .duration = 1 });
    ecs_progress(w, 0);
    test_bool(ecs_singleton_get(w, Console)->open, false);
    test_bool(ecs_get(w, view, View)->enabled, false);
}

void Console_submitted_line_runs_on_the_session(void)
{
    // ui/console
    ecs_singleton_set(w, Console, { .open = true });
    ecs_progress(w, 0);

    ecs_entity_t input = 0;
    ecs_iter_t it = ecs_each(w, TextInput);
    while (ecs_each_next(&it)) {
        input = it.entities[0];
        break;
    }

    test_assert(input != 0);
    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = 'h' });
    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = 'e' });
    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = 'l' });
    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = 'p' });
    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = '\n' });
    ecs_progress(w, 0);

    ecs_entity_t session = ecs_singleton_get(w, Console)->session;
    const TerminalOutput *out = ecs_get(w, session, TerminalOutput);
    test_assert(out != NULL);
    test_bool(out->error, false);
    test_assert(strstr(out->result, "help") != NULL);
}

void Console_scrollback_shows_the_session_log(void)
{
    // ui/console
    ecs_singleton_set(w, Console, { .open = true });
    ecs_progress(w, 0);

    ecs_entity_t session = ecs_singleton_get(w, Console)->session;
    ecs_set(w, session, TerminalInput, { .line = ecs_os_strdup("man capture") });
    ecs_progress(w, 0);

    ecs_entity_t log = 0;
    ecs_iter_t it = ecs_each(w, ConsoleLog);
    while (ecs_each_next(&it)) {
        log = it.entities[0];
        break;
    }

    test_assert(log != 0);
    const Text *text = ecs_get(w, log, Text);
    test_assert(text != NULL);
    test_assert(strstr(text->value, "$ man capture") != NULL);
}

void Console_scrollback_ends_each_output_with_a_newline(void)
{
    ecs_singleton_set(w, Console, { .open = true });
    ecs_progress(w, 0);

    Console_run("pwd");
    Console_run("pwd");

    const Text *text = ecs_get(w, Console_log(), Text);
    test_str(text->value, "$ pwd\n/\n\n$ pwd\n/\n\n");
}

void Console_clear_command_empties_the_scrollback(void)
{
    ecs_singleton_set(w, Console, { .open = true });
    ecs_progress(w, 0);

    Console_run("pwd");
    test_assert(ecs_get(w, Console_log(), Text)->value[0] != '\0');
    Console_run("clear");

    const Text *text = ecs_get(w, Console_log(), Text);
    test_assert(text == NULL || text->value == NULL || text->value[0] == '\0');
}

void Console_input_is_anchored_to_the_bottom(void)
{
    ecs_singleton_set(w, Console, { .open = true });
    ecs_progress(w, 0);

    ecs_entity_t root = ecs_singleton_get(w, Console)->root;
    ecs_entity_t panel = ecs_lookup_child(w, root, "panel");
    ecs_entity_t prompt = ecs_lookup_child(w, panel, "prompt");
    const Anchor *anchor = ecs_get(w, prompt, Anchor);

    test_assert(anchor != NULL);
    test_int(anchor->h, AlignLeft);
    test_int(anchor->v, VAlignBottom);
}

void Console_background_uses_backdrop_blur(void)
{
    ecs_singleton_set(w, Console, { .open = true });
    ecs_progress(w, 0);

    ecs_entity_t root = ecs_singleton_get(w, Console)->root;
    ecs_entity_t backdrop = ecs_lookup_child(w, root, "backdrop");
    const BackdropBlur *blur = ecs_get(w, backdrop, BackdropBlur);

    test_assert(blur != NULL);
    test_assert(blur->radius > 0.0f);
}

void Console_terminal_colors_are_configurable(void)
{
    ecs_singleton_set(w, Console, { .open = true });
    ecs_progress(w, 0);

    ecs_entity_t log = Console_log();
    TerminalColors colors = {
        .text = {1, 2, 3},
        .muted = {4, 5, 6},
        .accent = {7, 8, 9},
        .error = {10, 11, 12},
        .path = {13, 14, 15},
        .type = {16, 17, 18},
        .number = {19, 20, 21},
        .string = {22, 23, 24}
    };
    ecs_set_ptr(w, log, TerminalColors, &colors);

    ecs_entity_t styled = ecs_entity(w, { .name = "styled" });
    ecs_set(w, styled, Position, {42, 5, 1});
    Console_run("pwd");
    Console_run("cat actions.Console");
    Console_run("cat styled");
    Console_run("not_a_command");

    test_bool(Console_has_color(log, colors.text), true);
    test_bool(Console_has_color(log, colors.muted), true);
    test_bool(Console_has_color(log, colors.accent), true);
    test_bool(Console_has_color(log, colors.error), true);
    test_bool(Console_has_color(log, colors.path), true);
    test_bool(Console_has_color(log, colors.type), true);
    test_bool(Console_has_color(log, colors.number), true);
    test_bool(Console_has_color(log, colors.string), true);
}

void Console_submitted_line_fires_the_line_action(void)
{
    // ui/console
    ecs_singleton_set(w, Console, { .open = true });
    ecs_progress(w, 0);

    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = 'h' });
    box_input_inject(w, &(InputEvent){ .kind = InputChar, .code = '\n' });
    ecs_progress(w, 0);

    ecs_entity_t submit = ecs_lookup(w, "actions.ConsoleLine");
    test_assert(submit != 0);
    test_bool(ecs_get(w, submit, InputAction)->pressed, true);
}

void Console_script_is_loaded_once(void)
{
    // ui/console
    ecs_singleton_set(w, Console, { .open = true });
    ecs_progress(w, 0);
    ecs_entity_t root = ecs_singleton_get(w, Console)->root;

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyGrave, .duration = 1 });
    ecs_progress(w, 0);
    ecs_progress(w, 0);
    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyGrave, .duration = 1 });
    ecs_progress(w, 0);

    test_bool(ecs_singleton_get(w, Console)->open, true);
    test_uint(ecs_singleton_get(w, Console)->root, root);
    test_int(ecs_count_id(w, ecs_id(ConsoleLog)), 1);
}

void Console_script_awaits_the_console_action(void)
{
    // ui/console
    ecs_entity_t fired = ecs_entity(w, { .name = "Fired" });
    ecs_entity_t s = ecs_script(w, { .code =
        "using flecs.script\n"
        "using box.input\n"
        "using box.ui\n"
        "actions {\n"
        "  Console { InputAction: {description: \"console\"} }\n"
        "}\n"
        "$ {\n"
        "  Console: {}\n"
        "}\n"
        "template Waiter {\n"
        "  mut hits = 0\n"
        "  async {\n"
        "    await on.pressed(actions.Console)\n"
        "    hits = hits + 1\n"
        "  }\n"
        "  if hits > 0 {\n"
        "    Fired\n"
        "  }\n"
        "}\n"
        "Waiter waiter\n" });
    test_assert(s != 0);

    ecs_entity_t waiter = ecs_lookup(w, "waiter");
    test_assert(waiter != 0);
    ecs_progress(w, 0);
    test_bool(ecs_has_id(w, waiter, fired), false);

    box_input_inject(w, &(InputEvent){
        .kind = InputKeyDown, .code = BoxKeyGrave, .duration = 1 });
    ecs_progress(w, 0);
    test_bool(ecs_singleton_get(w, Console)->open, true);
    test_bool(ecs_has_id(w, waiter, fired), true);
}

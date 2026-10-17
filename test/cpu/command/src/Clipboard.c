/**
 * @file Clipboard.c
 * @brief Tests for the clipboard.set and clipboard.get commands and pasting what they set.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>

/** Run a command that must succeed and return its output.
 *
 * @param world The world
 * @param s The terminal session
 * @param cmd The command
 * @return The output, which the caller frees
 */
static char* clipboard_run(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *cmd)
{
    bool error = true;
    char *out = box_terminal_run(world, s, cmd, &error);
    test_bool(error, false);

    return out;
}

void Clipboard_set_get(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    char *out = clipboard_run(world, s, "clipboard.set hello");
    test_str(out, "");
    ecs_os_free(out);
    test_str(box_input_clipboard_get(world), "hello");

    out = clipboard_run(world, s, "clipboard.get");
    test_str(out, "hello");
    ecs_os_free(out);

    box_input_clipboard_set(world, "from the api");
    out = clipboard_run(world, s, "clipboard.get");
    test_str(out, "from the api");
    ecs_os_free(out);

    ecs_fini(world);
}

void Clipboard_set_spaces(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(clipboard_run(world, s, "clipboard.set   two  words and more  "));
    char *out = clipboard_run(world, s, "clipboard.get");
    test_str(out, "two  words and more");
    ecs_os_free(out);

    ecs_fini(world);
}

void Clipboard_get_empty(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    char *out = clipboard_run(world, s, "clipboard.get");
    test_str(out, "");
    ecs_os_free(out);

    test_assert(ecs_lookup(world, "box.command.commands.clipboard.set") != 0);
    test_assert(ecs_has(world, ecs_lookup(world, "box.command.commands.clipboard.get"),
        EcsScriptFunction));

    ecs_fini(world);
}

void Clipboard_set_missing_text(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    box_input_clipboard_set(world, "kept");
    bool error = false;
    char *out = box_terminal_run(world, s, "clipboard.set", &error);
    test_bool(error, true);
    ecs_os_free(out);
    test_str(box_input_clipboard_get(world), "kept");

    ecs_fini(world);
}

void Clipboard_paste_shortcut(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(clipboard_run(world, s, "clipboard.set pasted text"));
    ecs_os_free(clipboard_run(world, s, "key.press V"));
    ecs_progress(world, 0);
    test_assert(ecs_singleton_get(world, Input)->paste == NULL);
    ecs_progress(world, 0);

    ecs_os_free(clipboard_run(world, s, "key.down Control"));
    ecs_os_free(clipboard_run(world, s, "key.press V"));
    ecs_os_free(clipboard_run(world, s, "key.up Control"));
    ecs_progress(world, 0);
    test_str(ecs_singleton_get(world, Input)->paste, "pasted text");
    ecs_progress(world, 0);
    test_assert(ecs_singleton_get(world, Input)->paste == NULL);

    ecs_fini(world);
}

void Clipboard_utf8(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(clipboard_run(world, s,
        "clipboard.set gr\xc3\xbc\xc3\x9f \xe2\x86\x92 \xf0\x9f\x93\x8b"));
    char *out = clipboard_run(world, s, "clipboard.get");
    test_str(out, "gr\xc3\xbc\xc3\x9f \xe2\x86\x92 \xf0\x9f\x93\x8b");
    ecs_os_free(out);

    ecs_os_free(clipboard_run(world, s, "key.down CtrlLeft"));
    ecs_os_free(clipboard_run(world, s, "key.press V"));
    ecs_progress(world, 0);
    test_str(ecs_singleton_get(world, Input)->paste,
        "gr\xc3\xbc\xc3\x9f \xe2\x86\x92 \xf0\x9f\x93\x8b");

    ecs_fini(world);
}

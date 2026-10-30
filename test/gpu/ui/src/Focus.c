/**
 * @file Focus.c
 * @brief The keyboard focus with a Surface: the console over a KeyCapture widget, a press through
 * a texture surface and Tab in the views of the focused surface.
 */

#include <box.h>
#include <bake_test.h>

#define FOCUS_CONSOLE_LINE "console.ui.screen.stack.panel.content.input.line"

/** Create a headless world with a surface and a script.
 *
 * @param code The script
 * @param surface Whether to create a 1280x720 surface without a view root
 * @return The world
 */
static ecs_world_t* focus_world(
    const char *code,
    bool surface)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    if (surface) {
        ecs_entity_t s = ecs_new(world);
        ecs_set(world, s, Surface, {.width = 1280, .height = 720, .resolution_scale = 1});
    }

    test_assert(ecs_script(world, { .code = code }) != 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    return world;
}

/** Run a terminal command and the frames until the input queue is drained, plus one.
 *
 * @param world The world
 * @param s The terminal session
 * @param cmd The command
 */
static void focus_run(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *cmd)
{
    ecs_os_free(box_command_run(world, s, cmd, NULL));
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    ecs_progress(world, 0);
}

/** Run a terminal command and count the frames in which an action was pressed.
 *
 * @param world The world
 * @param s The terminal session
 * @param cmd The command
 * @param action The action
 * @return Frames in which it was pressed, over four frames
 */
static int32_t focus_presses(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *cmd,
    ecs_entity_t action)
{
    int32_t i, result = 0;
    ecs_os_free(box_command_run(world, s, cmd, NULL));
    for (i = 0; i < 4; i ++) {
        ecs_progress(world, 0);
        result += ecs_get(world, action, ActionState)->pressed;
    }

    return result;
}

void Focus_console_over_key_capture(void) {
    // ui/focus
    ecs_world_t *world = focus_world(
        "using flecs.script\n"
        "using box.transform\n"
        "using box.geometry\n"
        "using box.camera\n"
        "using box.view\n"
        "using box.input\n"
        "using box.ui\n"
        "walk { Action: {}\n"
        "  Binding: {key: W}\n"
        "}\n"
        "hud {\n"
        "  Scene: {}\n"
        "  camera {\n"
        "    Position: {0, 0, 100}\n"
        "    Camera: {ortho: true, ortho_scale: 1}\n"
        "  }\n"
        "  field { Position: {0, 0, 0}\n"
        "    Rect: {200, 40}\n"
        "    Focusable\n"
        "    KeyCapture\n"
        "  }\n"
        "}\n"
        "hud_view {\n"
        "  View: {camera: hud.camera}\n"
        "}\n"
        "template Keys {\n"
        "  mut downs: i32 = 0\n"
        "  async {\n"
        "    while true {\n"
        "      await on.key_down(hud.field)\n"
        "      downs = downs + 1\n"
        "    }\n"
        "  }\n"
        "}\n"
        "keys { Keys: {} }\n", true);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t field = ecs_lookup(world, "hud.field");
    ecs_entity_t walk = ecs_lookup(world, "walk");
    const int32_t *downs = ecs_get_id(world, ecs_lookup(world, "keys"),
        ecs_lookup(world, "Keys.mut"));
    test_assert(downs != NULL);
    test_bool(box_ui_focus(world, field), true);
    test_int(focus_presses(world, s, "key_press W", walk), 0);
    downs = ecs_get_id(world, ecs_lookup(world, "keys"), ecs_lookup(world, "Keys.mut"));
    test_int(*downs, 1);

    focus_run(world, s, "key_type `");
    test_bool(ecs_singleton_get(world, Console)->open, true);
    focus_run(world, s, "key_type ls");
    const Text *line = ecs_get(world, ecs_lookup(world, FOCUS_CONSOLE_LINE), Text);
    test_assert(line != NULL);
    test_str(line->value, "~ $ ls");
    downs = ecs_get_id(world, ecs_lookup(world, "keys"), ecs_lookup(world, "Keys.mut"));
    test_int(*downs, 1);
    test_uint(ecs_singleton_get(world, FocusStatus)->focused, field);

    focus_run(world, s, "key_press Escape");
    test_bool(ecs_singleton_get(world, Console)->open, false);
    test_int(focus_presses(world, s, "key_press W", walk), 0);
    downs = ecs_get_id(world, ecs_lookup(world, "keys"), ecs_lookup(world, "Keys.mut"));
    test_int(*downs, 2);

    ecs_fini(world);
}

void Focus_render_to_texture(void) {
    // ui/focus
    ecs_world_t *world = focus_world(
        "using flecs.script\n"
        "using box.transform\n"
        "using box.geometry\n"
        "using box.material\n"
        "using box.camera\n"
        "using box.view\n"
        "using box.surface\n"
        "using box.ui\n"
        "screen_texture {}\n"
        "screen_ui {\n"
        "  Scene: {}\n"
        "  camera {\n"
        "    Position: {0, 0, 100}\n"
        "    Camera: {ortho: true, ortho_scale: 1}\n"
        "  }\n"
        "  button {\n"
        "    Position: {-50, 0, 0}\n"
        "    Rect: {60, 60}\n"
        "    Focusable\n"
        "  }\n"
        "}\n"
        "screen_views {\n"
        "  view {\n"
        "    View: {camera: screen_ui.camera}\n"
        "  }\n"
        "}\n"
        "screen_surface {\n"
        "  Surface: {width: 200, height: 100, texture: screen_texture, view_root: screen_views}\n"
        "}\n"
        "camera {\n"
        "  Position: {0, 0, 10}\n"
        "  Camera: {}\n"
        "}\n"
        "main_view {\n"
        "  View: {camera: camera}\n"
        "}\n"
        "quad {\n"
        "  Position: {0, 0, 0}\n"
        "  Quad: {4, 2}\n"
        "  PbrTextures: {albedo: screen_texture}\n"
        "}\n", false);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t button = ecs_lookup(world, "screen_ui.button");
    focus_run(world, s, "mouse_click left [584, 360]");
    test_uint(ecs_singleton_get(world, FocusStatus)->focused, button);
    test_bool(ecs_has(world, button, Focused), true);
    focus_run(world, s, "mouse_click left [702, 360]");
    test_uint(ecs_singleton_get(world, FocusStatus)->focused, 0);

    ecs_fini(world);
}

void Focus_tab_views_of_focused_surface(void) {
    // ui/focus
    ecs_world_t *world = focus_world(
        "using flecs.script\n"
        "using box.transform\n"
        "using box.geometry\n"
        "using box.camera\n"
        "using box.view\n"
        "using box.surface\n"
        "using box.ui\n"
        "game_ui {\n"
        "  Scene: {}\n"
        "  camera {\n"
        "    Position: {0, 0, 100}\n"
        "    Camera: {ortho: true, ortho_scale: 1}\n"
        "  }\n"
        "  form {\n"
        "    FocusScope\n"
        "    ok { Position: {0, 0, 0}\n"
        "      Rect: {100, 40}\n"
        "      Focusable\n"
        "    }\n"
        "  }\n"
        "}\n"
        "game_views {\n"
        "  view {\n"
        "    View: {camera: game_ui.camera}\n"
        "  }\n"
        "}\n"
        "window {\n"
        "  Surface: {width: 1280, height: 720, view_root: game_views}\n"
        "}\n"
        "other_ui {\n"
        "  Scene: {}\n"
        "  camera {\n"
        "    Position: {0, 0, 100}\n"
        "    Camera: {ortho: true, ortho_scale: 1}\n"
        "  }\n"
        "  form {\n"
        "    FocusScope\n"
        "    no { Position: {0, 0, 0}\n"
        "      Rect: {100, 40}\n"
        "      Focusable\n"
        "    }\n"
        "  }\n"
        "}\n"
        "other_view {\n"
        "  View: {camera: other_ui.camera}\n"
        "}\n", false);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    focus_run(world, s, "key_press Tab");
    test_uint(ecs_singleton_get(world, FocusStatus)->focused,
        ecs_lookup(world, "game_ui.form.ok"));
    test_bool(ecs_singleton_get(world, FocusStatus)->visible, true);

    ecs_fini(world);
}

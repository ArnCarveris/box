/**
 * @file Editor.c
 * @brief The editor overlay renders over the game views and under the console, in either open
 * order, and picks over its panel go to the editor, over the console to the console.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Width of the surface of the tests in points */
#define EDITOR_W (320)

/** Height of the surface of the tests in points */
#define EDITOR_H (180)

/** Background of the body of the panel in the dark theme */
static const unsigned char editor_background[3] = {32, 35, 41};

/** Create a headless world with a game view that clears to blue and a surface that is written
 * to a file.
 *
 * @param file Name of the PNG in the temporary folder
 * @param width Width of the surface
 * @param height Height of the surface
 * @param game Receives the game view, NULL to ignore
 * @return The world
 */
/** Test whether an entity is a scope or below it.
 *
 * @param world The world
 * @param e The entity
 * @param scope Path of the scope
 * @return Whether e is the scope or one of its descendants
 */
static bool editor_in_scope(
    ecs_world_t *world,
    ecs_entity_t e,
    const char *scope)
{
    ecs_entity_t root = ecs_lookup(world, scope);
    while (e && e != root) {
        e = ecs_get_parent(world, e);
    }

    return root != 0 && e == root;
}

static ecs_world_t* editor_world(
    const char *file,
    int32_t width,
    int32_t height,
    ecs_entity_t *game)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 5});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 255, 255}}});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = width, .height = height,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    Console *c = ecs_singleton_get_mut(world, Console);
    c->background = (rgba){200, 0, 0, 255};
    ecs_singleton_modified(world, Console);
    ecs_progress(world, 0);
    if (game) {
        game[0] = view;
    }

    return world;
}

/** Open the editor, covering the surface width times width, and run frames.
 *
 * @param world The world
 * @param width Part of the surface the panel covers
 */
static void editor_open(
    ecs_world_t *world,
    float width)
{
    Editor *e = ecs_singleton_get_mut(world, Editor);
    e->open = true;
    e->width = width;
    ecs_singleton_modified(world, Editor);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
}

/** Open the console and run frames.
 *
 * @param world The world
 */
static void editor_console_open(
    ecs_world_t *world)
{
    Console *c = ecs_singleton_get_mut(world, Console);
    c->open = true;
    ecs_singleton_modified(world, Console);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
}

/** Render the frame and load the PNG it wrote.
 *
 * @param world The world
 * @param file Name of the PNG in the temporary folder
 * @return The RGBA pixels, freed with stbi_image_free
 */
static unsigned char* editor_render(
    ecs_world_t *world,
    const char *file)
{
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_render_sync(world);
    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    ecs_os_free(path);
    test_assert(px != NULL);
    test_int(w, EDITOR_W);
    test_int(h, EDITOR_H);

    return px;
}

/** Check whether a pixel has a color.
 *
 * @param px The pixels
 * @param x Column
 * @param y Row from the top
 * @param r Red
 * @param g Green
 * @param b Blue
 * @return Whether every channel is within 3
 */
static bool editor_pixel(
    const unsigned char *px,
    int32_t x,
    int32_t y,
    int r,
    int g,
    int b)
{
    const unsigned char *p = &px[(y * EDITOR_W + x) * 4];

    return abs((int)p[0] - r) < 4 && abs((int)p[1] - g) < 4 && abs((int)p[2] - b) < 4;
}

/** Check the order of the editor, console and game views and the pixels of a frame with the
 * editor over the whole surface and the console over the left half.
 *
 * @param world The world
 * @param game The game view
 * @param file Name of the PNG
 */
static void editor_check_stack(
    ecs_world_t *world,
    ecs_entity_t game,
    const char *file)
{
    ecs_entity_t editor = ecs_lookup(world, BOX_EDITOR_VIEW);
    ecs_entity_t console = ecs_lookup(world, BOX_CONSOLE_VIEW);
    test_assert(editor != 0);
    test_assert(console != 0);
    test_assert(box_view_compare(ecs_get(world, game, View)->order, game,
        ecs_get(world, editor, View)->order, editor) < 0);
    test_assert(box_view_compare(ecs_get(world, editor, View)->order, editor,
        ecs_get(world, console, View)->order, console) < 0);

    unsigned char *px = editor_render(world, file);
    test_assert(editor_pixel(px, 80, 60, 200, 0, 0));
    test_assert(editor_pixel(px, 240, 120, editor_background[0], editor_background[1],
        editor_background[2]));
    stbi_image_free(px);
}

void Editor_renders_over_game(void) {
    // editor/overlay
    ecs_entity_t game;
    ecs_world_t *world = editor_world("editor_game.png", EDITOR_W, EDITOR_H, &game);
    editor_open(world, 0.25f);

    unsigned char *px = editor_render(world, "editor_game.png");
    test_assert(editor_pixel(px, 100, 120, 0, 0, 255));
    test_assert(editor_pixel(px, 300, 120, editor_background[0], editor_background[1],
        editor_background[2]));
    test_assert(!editor_pixel(px, 300, 4, 0, 0, 255));
    stbi_image_free(px);

    ecs_fini(world);
}

void Editor_closed_renders_game(void) {
    // editor/overlay
    ecs_entity_t game;
    ecs_world_t *world = editor_world("editor_closed.png", EDITOR_W, EDITOR_H, &game);
    editor_open(world, 0.25f);
    Editor *e = ecs_singleton_get_mut(world, Editor);
    e->open = false;
    ecs_singleton_modified(world, Editor);
    ecs_progress(world, 0);
    test_assert(ecs_lookup(world, BOX_EDITOR_VIEW) == 0);

    unsigned char *px = editor_render(world, "editor_closed.png");
    test_assert(editor_pixel(px, 300, 120, 0, 0, 255));
    stbi_image_free(px);

    ecs_fini(world);
}

void Editor_under_console_editor_first(void) {
    // editor/overlay
    ecs_entity_t game;
    ecs_world_t *world = editor_world("editor_first.png", EDITOR_W, EDITOR_H, &game);
    editor_open(world, 1);
    editor_console_open(world);
    editor_check_stack(world, game, "editor_first.png");

    ecs_fini(world);
}

void Editor_under_console_console_first(void) {
    // editor/overlay
    ecs_entity_t game;
    ecs_world_t *world = editor_world("console_first.png", EDITOR_W, EDITOR_H, &game);
    editor_console_open(world);
    editor_open(world, 1);
    editor_check_stack(world, game, "console_first.png");

    ecs_fini(world);
}

void Editor_reopened_under_console(void) {
    // editor/overlay
    ecs_entity_t game;
    ecs_world_t *world = editor_world("editor_reopen.png", EDITOR_W, EDITOR_H, &game);
    editor_open(world, 1);
    editor_console_open(world);
    Editor *e = ecs_singleton_get_mut(world, Editor);
    e->open = false;
    ecs_singleton_modified(world, Editor);
    ecs_progress(world, 0);
    editor_open(world, 1);
    editor_check_stack(world, game, "editor_reopen.png");

    ecs_fini(world);
}

void Editor_pick_panel_and_console(void) {
    // editor/overlay
    ecs_entity_t game;
    ecs_world_t *world = editor_world("editor_pick.png", 640, 360, &game);
    editor_console_open(world);
    editor_open(world, 0.25f);

    ecs_entity_t editor = ecs_lookup(world, BOX_EDITOR_VIEW);
    ecs_entity_t console = ecs_lookup(world, BOX_CONSOLE_VIEW);
    ecs_entity_t panel = ecs_lookup(world, BOX_EDITOR_PANEL);
    test_assert(editor != 0);
    test_assert(console != 0);

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "mouse.move [600, 200]", NULL));
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    ecs_progress(world, 0);
    const PickStatus *status = ecs_singleton_get(world, PickStatus);
    test_uint(status->view, editor);
    test_uint(status->target, ecs_lookup(world, BOX_EDITOR_PANEL ".body"));
    test_uint(status->handler, panel);
    test_assert(!editor_in_scope(world, status->hit, BOX_CONSOLE_UI ".screen.stack"));

    ecs_os_free(box_terminal_run(world, s, "mouse.move [100, 200]", NULL));
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    ecs_progress(world, 0);
    status = ecs_singleton_get(world, PickStatus);
    test_uint(status->hit_view, console);
    test_assert(editor_in_scope(world, status->hit, BOX_CONSOLE_UI ".screen.stack"));
    test_assert(status->view != editor);
    test_assert(status->handler != panel);
    test_assert(!editor_in_scope(world, status->target, BOX_EDITOR_UI));

    ecs_fini(world);
}

void Editor_panel_blocks_game(void) {
    // editor/overlay
    ecs_entity_t game;
    ecs_world_t *world = editor_world("editor_block.png", 640, 360, &game);
    editor_open(world, 0.25f);
    ecs_entity_t editor = ecs_lookup(world, BOX_EDITOR_VIEW);
    ecs_entity_t panel = ecs_lookup(world, BOX_EDITOR_PANEL);

    ecs_entity_t s = box_terminal_new(world);
    ecs_os_free(box_terminal_run(world, s, "mouse.move [600, 200]", NULL));
    ecs_os_free(box_terminal_run(world, s, "mouse.scroll 0 3", NULL));
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    ecs_progress(world, 0);
    const PickStatus *status = ecs_singleton_get(world, PickStatus);
    test_uint(status->view, editor);
    test_uint(status->handler, panel);

    ecs_os_free(box_terminal_run(world, s, "mouse.move [100, 200]", NULL));
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    ecs_progress(world, 0);
    status = ecs_singleton_get(world, PickStatus);
    test_assert(status->view != editor);
    test_assert(status->handler != panel);

    ecs_fini(world);
}

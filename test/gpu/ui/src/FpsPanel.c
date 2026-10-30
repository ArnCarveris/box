/**
 * @file FpsPanel.c
 * @brief The fps panel renders after the editor and before the console when all three are open,
 * in the top right corner of the surface, its chart red for frames slower than 30 fps, and clicks
 * and the mouse wheel over it go to the game.
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
#define FPS_W (640)

/** Height of the surface of the tests in points */
#define FPS_H (360)

/** Test whether an entity is a scope or below it.
 *
 * @param world The world
 * @param e The entity
 * @param scope Path of the scope
 * @return Whether e is the scope or one of its descendants
 */
static bool fps_in_scope(
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

/** Create a headless world with the widget library, a game view that clears to blue, a full
 * window of fixed samples over the top of the chart and a surface that is written to a file.
 *
 * @param file Name of the PNG in the temporary folder
 * @param game Receives the game view
 * @return The world
 */
static ecs_world_t* fps_world(
    const char *file,
    ecs_entity_t *game)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    char *widgets = box_app_locate("test/assets/ui/widgets.flecs");
    test_assert(ecs_script(world, {
        .entity = ecs_entity(world, { .name = "box.ui.widgets" }),
        .filename = widgets
    }) != 0);
    ecs_os_free(widgets);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);
    FrameSamples *samples = ecs_singleton_get_mut(world, FrameSamples);
    samples->hold = true;
    for (int32_t i = 0; i < BOX_FRAME_WINDOW; i ++) {
        box_frame_samples_add(samples, BOX_FPS_CHART_MS * 2);
    }

    ecs_singleton_modified(world, FrameSamples);
    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 5});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 255, 255}}});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = FPS_W, .height = FPS_H,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});
    Console *c = ecs_singleton_get_mut(world, Console);
    c->background = (rgba){200, 0, 0, 255};
    ecs_singleton_modified(world, Console);
    ecs_progress(world, 0);
    game[0] = view;

    return world;
}

/** Open the editor over the whole surface, the console and the fps panel, in that order.
 *
 * @param world The world
 */
static void fps_open_all(
    ecs_world_t *world)
{
    Editor *e = ecs_singleton_get_mut(world, Editor);
    e->open = true;
    e->width = 1;
    ecs_singleton_modified(world, Editor);
    ecs_progress(world, 0);
    Console *c = ecs_singleton_get_mut(world, Console);
    c->open = true;
    ecs_singleton_modified(world, Console);
    ecs_progress(world, 0);
    FpsPanel *p = ecs_singleton_get_mut(world, FpsPanel);
    p->open = true;
    ecs_singleton_modified(world, FpsPanel);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
}

/** Render the frame and load the PNG it wrote.
 *
 * @param world The world
 * @param file Name of the PNG in the temporary folder
 * @return The RGBA pixels, freed with stbi_image_free
 */
static unsigned char* fps_render(
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
    test_int(w, FPS_W);
    test_int(h, FPS_H);

    return px;
}

/** Check whether a pixel has a color.
 *
 * @param px The pixels
 * @param x Column
 * @param y Row from the top
 * @param c The color
 * @return Whether every channel is within 3
 */
static bool fps_pixel(
    const unsigned char *px,
    int32_t x,
    int32_t y,
    Rgb c)
{
    const unsigned char *p = &px[(y * FPS_W + x) * 4];

    return abs((int)p[0] - c.r) < 4 && abs((int)p[1] - c.g) < 4 && abs((int)p[2] - c.b) < 4;
}

/** Check the order of the views: the game, the editor, the fps panel, the console.
 *
 * @param world The world
 * @param game The game view
 */
static void fps_check_order(
    ecs_world_t *world,
    ecs_entity_t game)
{
    ecs_entity_t editor = ecs_lookup(world, BOX_EDITOR_VIEW);
    ecs_entity_t fps = ecs_lookup(world, BOX_FPS_VIEW);
    ecs_entity_t console = ecs_lookup(world, BOX_CONSOLE_VIEW);
    test_assert(editor != 0);
    test_assert(fps != 0);
    test_assert(console != 0);
    test_assert(box_view_compare(ecs_get(world, game, View)->order, game,
        ecs_get(world, editor, View)->order, editor) < 0);
    test_assert(box_view_compare(ecs_get(world, editor, View)->order, editor,
        ecs_get(world, fps, View)->order, fps) < 0);
    test_assert(box_view_compare(ecs_get(world, fps, View)->order, fps,
        ecs_get(world, console, View)->order, console) < 0);
}

void FpsPanel_between_editor_and_console(void) {
    // fps/panel
    ecs_entity_t game;
    ecs_world_t *world = fps_world("fps_stack.png", &game);
    fps_open_all(world);
    fps_check_order(world, game);

    rgb red = BOX_FPS_RED;
    unsigned char *px = fps_render(world, "fps_stack.png");
    test_assert(fps_pixel(px, 600, 80, (Rgb){red.r, red.g, red.b}));
    test_assert(fps_pixel(px, 315, 80, (Rgb){200, 0, 0}));
    test_assert(fps_pixel(px, 100, 300, (Rgb){200, 0, 0}));
    test_assert(!fps_pixel(px, 600, 300, (Rgb){0, 0, 255}));
    test_assert(!fps_pixel(px, 600, 300, (Rgb){red.r, red.g, red.b}));
    stbi_image_free(px);

    ecs_fini(world);
}

void FpsPanel_click_reaches_game(void) {
    // fps/panel
    ecs_entity_t game;
    ecs_world_t *world = fps_world("fps_click.png", &game);
    ecs_entity_t wall = ecs_new(world);
    ecs_set(world, wall, Position, {0, 0, 0});
    ecs_set(world, wall, Box, {20, 20, 1});
    ecs_add(world, wall, Interactive);
    ecs_progress(world, 0);

    FpsPanel *p = ecs_singleton_get_mut(world, FpsPanel);
    p->open = true;
    ecs_singleton_modified(world, FpsPanel);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_entity_t fps = ecs_lookup(world, BOX_FPS_VIEW);
    test_assert(fps != 0);
    test_assert(ecs_lookup(world, BOX_FPS_PANEL) != 0);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_os_free(box_command_run(world, s, "mouse_move [600, 60]", NULL));
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    ecs_progress(world, 0);
    const PickStatus *status = ecs_singleton_get(world, PickStatus);
    test_uint(status->view, game);
    test_uint(status->target, wall);
    test_uint(status->handler, wall);
    test_assert(status->view != fps);
    test_assert(!fps_in_scope(world, status->handler, BOX_FPS_UI));

    ecs_os_free(box_command_run(world, s, "mouse_click left [600, 60]", NULL));
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
    }

    ecs_progress(world, 0);
    status = ecs_singleton_get(world, PickStatus);
    test_uint(status->view, game);
    test_uint(status->target, wall);
    test_uint(status->handler, wall);
    test_assert(status->view != fps);
    test_assert(!fps_in_scope(world, status->target, BOX_FPS_UI));
    test_assert(!fps_in_scope(world, status->handler, BOX_FPS_UI));

    ecs_fini(world);
}

void FpsPanel_scroll_reaches_game(void) {
    // fps/panel
    ecs_entity_t game;
    ecs_world_t *world = fps_world("fps_scroll.png", &game);
    test_assert(ecs_script(world, { .code =
        "using flecs.script\n"
        "using box.transform\n"
        "using box.geometry\n"
        "using box.picking\n"
        "template Probe {\n"
        "  mut scrolls: i32 = 0\n"
        "  async {\n"
        "    while true {\n"
        "      await on.scroll(this)\n"
        "      scrolls = scrolls + 1\n"
        "    }\n"
        "  }\n"
        "}\n"
        "wall {\n"
        "  Position: {0, 0, 0}\n"
        "  Box: {20, 20, 1}\n"
        "  Interactive\n"
        "  Probe: {}\n"
        "}\n" }) != 0);

    typedef struct { int32_t scrolls; } Mut;
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");
    ecs_entity_t wall = ecs_lookup(world, "wall");
    test_assert(mut != 0);
    test_assert(wall != 0);
    ecs_progress(world, 0);

    FpsPanel *p = ecs_singleton_get_mut(world, FpsPanel);
    p->open = true;
    ecs_singleton_modified(world, FpsPanel);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    ecs_entity_t fps = ecs_lookup(world, BOX_FPS_VIEW);
    test_assert(fps != 0);
    test_assert(ecs_lookup(world, BOX_FPS_PANEL) != 0);
    test_int(((const Mut*)ecs_get_id(world, wall, mut))->scrolls, 0);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_os_free(box_command_run(world, s, "mouse_move [600, 60]", NULL));
    ecs_os_free(box_command_run(world, s, "mouse_scroll 0 3", NULL));
    float scroll = 0;
    while (box_input_pending(world)) {
        ecs_progress(world, 0);
        scroll += ecs_singleton_get(world, Input)->mouse.scroll[1];
    }

    ecs_progress(world, 0);
    scroll += ecs_singleton_get(world, Input)->mouse.scroll[1];
    const PickStatus *status = ecs_singleton_get(world, PickStatus);
    test_uint(status->view, game);
    test_uint(status->handler, wall);
    test_assert(status->view != fps);
    test_assert(!fps_in_scope(world, status->handler, BOX_FPS_UI));
    test_flt(scroll, 3);
    test_int(((const Mut*)ecs_get_id(world, wall, mut))->scrolls, 1);

    ecs_fini(world);
}

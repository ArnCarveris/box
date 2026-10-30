/**
 * @file ScrollFloat.c
 * @brief A scroll view shows and picks only the rows in view; a floating element draws and picks
 * above the other elements and outside the clip of its ancestors.
 */

#include <box.h>
#include <bake_test.h>
#include <stdlib.h>
#include "render.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** Create a headless world with an orthographic UI camera of 1 point per pixel, a view that
 * clears to black and a 64x64 surface that writes a file every frame.
 *
 * @param file The file of the surface
 * @return The world
 */
static ecs_world_t* sf_world(
    const char *file)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    App *app = ecs_singleton_get_mut(world, App);
    app->headless = true;
    ecs_singleton_modified(world, App);

    ecs_entity_t camera = ecs_new(world);
    ecs_set(world, camera, Position, {0, 0, 100});
    ecs_set(world, camera, Camera, {.ortho = true, .ortho_scale = 1, .near_ = 0.1f,
        .far_ = 1000});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera,
        .clear = {.enabled = true, .color = {0, 0, 0, 255}}});
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 64, .height = 64,
        .write_to_file = ECS_CONST_CAST(char*, file), .resolution_scale = 1});

    return world;
}

/** Render frames and load the image a surface wrote.
 *
 * @param world The world
 * @param file The file of the surface
 * @param frames Frames to render
 * @return The pixels, RGBA, free with stbi_image_free
 */
static unsigned char* sf_render(
    ecs_world_t *world,
    const char *file,
    int32_t frames)
{
    int32_t i;
    int w = 0, h = 0, n = 0;
    for (i = 0; i < frames; i ++) {
        ecs_progress(world, 0);
    }

    box_render_sync(world);
    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir, file);
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    ecs_os_free(path);
    test_assert(px != NULL);

    return px;
}

/** Get a pixel of a 64 pixel wide image.
 *
 * @param px The pixels
 * @param x The column
 * @param y The row
 * @return The pixel
 */
static const unsigned char* sf_at(
    const unsigned char *px,
    int32_t x,
    int32_t y)
{
    return &px[(y * 64 + x) * 4];
}

/** Check whether a pixel is mostly one channel.
 *
 * @param c The pixel
 * @param channel 0 for red, 1 for green, 2 for blue
 * @return Whether the channel is above 128 and the others below 64
 */
static bool sf_is(
    const unsigned char *c,
    int32_t channel)
{
    int32_t i;
    bool result = c[channel] > 128;
    for (i = 0; i < 3; i ++) {
        result = result && (i == channel || c[i] < 64);
    }

    return result;
}

/** Check whether a pixel is black.
 *
 * @param c The pixel
 * @return Whether every channel is below 3
 */
static bool sf_black(
    const unsigned char *c)
{
    return c[0] < 3 && c[1] < 3 && c[2] < 3;
}

/** Create a 20x20 scroll view at the origin with five 20x10 rows, green and red in turn.
 *
 * @param world The world
 * @param y The scroll offset
 * @return The view
 */
static ecs_entity_t sf_list(
    ecs_world_t *world,
    float y)
{
    ecs_entity_t result = ecs_new(world);
    ecs_set(world, result, Position, {0, 0, 0});
    ecs_set(world, result, Rect, {20, 20});
    ecs_set(world, result, Rgb, {0, 0, 0});
    ecs_set(world, result, Layout, { .direction = DirectionColumn });
    ecs_set(world, result, LayoutItem, {
        .width = { SizeModeFixed, 20 }, .height = { SizeModeFixed, 20 } });
    ecs_set(world, result, LayoutScroll, { .y = y, .vertical = true });
    ecs_add_id(world, result, ClipChildren);
    int32_t i;
    for (i = 0; i < 5; i ++) {
        ecs_entity_t row = ecs_new_w_pair(world, EcsChildOf, result);
        ecs_set(world, row, Rect, {20, 10});
        if (i % 2) {
            ecs_set(world, row, Rgb, {255, 0, 0});
        } else {
            ecs_set(world, row, Rgb, {0, 255, 0});
        }
    }

    return result;
}

void ScrollFloat_scrolled_list_shows_rows_in_view(void) {
    ecs_world_t *world = sf_world("scroll.png");
    ecs_entity_t list = sf_list(world, 10);
    unsigned char *px = sf_render(world, "scroll.png", 2);
    int32_t x, y;

    /* Rows 1 (red) and 2 (green) fill the view, nothing shows outside it */
    for (y = 0; y < 64; y ++) {
        for (x = 0; x < 64; x ++) {
            const unsigned char *c = sf_at(px, x, y);
            if (x < 22 || x >= 42 || y < 22 || y >= 42) {
                test_assert(sf_black(c));
            } else if (y < 32) {
                test_assert(sf_is(c, 0));
            } else {
                test_assert(sf_is(c, 1));
            }
        }
    }

    stbi_image_free(px);

    /* At 25 the bottom half of row 2, row 3 and the top half of row 4 show */
    ecs_set(world, list, LayoutScroll, { .y = 25, .vertical = true });
    px = sf_render(world, "scroll.png", 1);
    for (x = 22; x < 42; x ++) {
        for (y = 22; y < 27; y ++) {
            test_assert(sf_is(sf_at(px, x, y), 1));
        }

        for (y = 27; y < 37; y ++) {
            test_assert(sf_is(sf_at(px, x, y), 0));
        }

        for (y = 37; y < 42; y ++) {
            test_assert(sf_is(sf_at(px, x, y), 1));
        }

        test_assert(sf_black(sf_at(px, x, 21)));
        test_assert(sf_black(sf_at(px, x, 42)));
    }

    stbi_image_free(px);
    ecs_fini(world);
}

/** Script of the picking tests: a hud scene with a click counting template */
#define SF_PROBE \
    "using flecs.script\n" \
    "using box.transform\n" \
    "using box.geometry\n" \
    "using box.camera\n" \
    "using box.view\n" \
    "using box.layout\n" \
    "using box.picking\n" \
    "template Probe {\n" \
    "  mut clicks: i32 = 0\n" \
    "  async {\n" \
    "    while true {\n" \
    "      await on.click(this)\n" \
    "      clicks = clicks + 1\n" \
    "    }\n" \
    "  }\n" \
    "}\n"

/** The mutable state of a Probe */
typedef struct {
    int32_t clicks; /**< Clicks the element took */
} sf_mut_t;

/** Get the clicks a Probe element took.
 *
 * @param world The world
 * @param e The element
 * @return The clicks
 */
static int32_t sf_clicks(
    ecs_world_t *world,
    ecs_entity_t e)
{
    ecs_entity_t mut = ecs_lookup(world, "Probe.mut");
    test_assert(mut != 0);
    const sf_mut_t *m = ecs_get_id(world, e, mut);
    test_assert(m != NULL);

    return m->clicks;
}

/** Click at a point of the screen and let the click arrive.
 *
 * @param world The world
 * @param s The terminal session
 * @param cmd The click command
 */
static void sf_click(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *cmd)
{
    ecs_os_free(box_command_run(world, s, cmd, NULL));
    ecs_progress(world, 0);
    ecs_progress(world, 0);
}

void ScrollFloat_scrolled_list_picks_row_under_cursor(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script(world, { .code = SF_PROBE
        "template Row {\n"
        "  Rect: {100, 20}\n"
        "  Interactive\n"
        "  Probe: {}\n"
        "}\n"
        "hud {\n"
        "  Scene: {}\n"
        "  camera {\n"
        "    Position: {0, 0, 100}\n"
        "    Camera: {ortho: true, ortho_scale: 1}\n"
        "  }\n"
        "  list {\n"
        "    Position: {0, 0, 0}\n"
        "    Rect: {100, 40}\n"
        "    Layout: {direction: Column}\n"
        "    LayoutItem: {width: {mode: Fixed, value: 100}, height: {mode: Fixed, value: 40}}\n"
        "    LayoutScroll: {vertical: true}\n"
        "    ClipChildren\n"
        "    r0 { Row: {} }\n"
        "    r1 { Row: {} }\n"
        "    r2 { Row: {} }\n"
        "    r3 { Row: {} }\n"
        "    r4 { Row: {} }\n"
        "  }\n"
        "}\n"
        "hud_view {\n"
        "  View: {camera: hud.camera}\n"
        "}\n" }) != 0);

    ecs_entity_t list = ecs_lookup(world, "hud.list");
    ecs_entity_t r0 = ecs_lookup(world, "hud.list.r0");
    ecs_entity_t r1 = ecs_lookup(world, "hud.list.r1");
    ecs_entity_t r3 = ecs_lookup(world, "hud.list.r3");
    test_assert(list != 0 && r0 != 0 && r1 != 0 && r3 != 0);
    ecs_progress(world, 0);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    sf_click(world, s, "mouse_click left [640, 350]");
    test_int(sf_clicks(world, r0), 1);
    test_int(sf_clicks(world, r1), 0);

    /* Scrolled by one row, the same point is over the next row */
    ecs_set(world, list, LayoutScroll, { .y = 20, .vertical = true });
    ecs_progress(world, 0);
    sf_click(world, s, "mouse_click left [640, 350]");
    test_int(sf_clicks(world, r0), 1);
    test_int(sf_clicks(world, r1), 1);

    /* Row 3 lies below the view at this offset: clipped, it takes no click */
    test_flt(ecs_get(world, r3, Position)->y, -30);
    sf_click(world, s, "mouse_click left [640, 395]");
    test_int(sf_clicks(world, r3), 0);
    test_int(sf_clicks(world, r0), 1);
    test_int(sf_clicks(world, r1), 1);

    /* Scrolled to the end, row 3 is in view and takes the click */
    ecs_set(world, list, LayoutScroll, { .y = 60, .vertical = true });
    ecs_progress(world, 0);
    sf_click(world, s, "mouse_click left [640, 350]");
    test_int(sf_clicks(world, r3), 1);

    ecs_fini(world);
}

/** Create a scroll view whose button opens a floating list below and right of the view, and a
 * later sibling the list overlaps.
 *
 * The view covers x -20..0 and y 0..20, the 30x30 list x -20..10 and y -20..10, the sibling x
 * 0..20 and y -20..0.
 *
 * @param world The world
 * @param list Receives the floating list
 * @return The sibling
 */
static ecs_entity_t sf_dropdown(
    ecs_world_t *world,
    ecs_entity_t *list)
{
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, Position, {-10, 10, 0});
    ecs_set(world, view, Rect, {20, 20});
    ecs_set(world, view, Rgb, {0, 0, 0});
    ecs_set(world, view, Layout, { .direction = DirectionColumn });
    ecs_set(world, view, LayoutItem, {
        .width = { SizeModeFixed, 20 }, .height = { SizeModeFixed, 20 } });
    ecs_set(world, view, LayoutScroll, { .vertical = true });
    ecs_add_id(world, view, ClipChildren);
    ecs_entity_t button = ecs_new_w_pair(world, EcsChildOf, view);
    ecs_set(world, button, Rect, {20, 10});
    ecs_set(world, button, Rgb, {255, 0, 0});
    ecs_set(world, button, Layout, { .direction = DirectionColumn });
    ecs_set(world, button, LayoutItem, {
        .width = { SizeModeFixed, 20 }, .height = { SizeModeFixed, 10 } });
    *list = ecs_new_w_pair(world, EcsChildOf, button);
    ecs_set(world, *list, Rect, {30, 30});
    ecs_set(world, *list, Rgb, {0, 255, 0});
    ecs_set(world, *list, LayoutFloat, {
        .h = AlignLeft, .v = VAlignTop, .anchor_h = AlignLeft, .anchor_v = VAlignBottom });
    ecs_entity_t sibling = ecs_new(world);
    ecs_set(world, sibling, Position, {10, -10, 0});
    ecs_set(world, sibling, Rect, {20, 20});
    ecs_set(world, sibling, Rgb, {0, 0, 255});

    return sibling;
}

void ScrollFloat_float_draws_above_and_outside_clip(void) {
    ecs_world_t *world = sf_world("float.png");
    ecs_entity_t list;
    sf_dropdown(world, &list);
    unsigned char *px = sf_render(world, "float.png", 2);

    test_flt(ecs_get(world, list, Position)->x, 5);
    test_flt(ecs_get(world, list, Position)->y, -20);

    /* Over the sibling drawn after it */
    test_assert(sf_is(sf_at(px, 37, 42), 1));
    test_assert(sf_is(sf_at(px, 41, 51), 1));

    /* The sibling where the list does not reach */
    test_assert(sf_is(sf_at(px, 47, 42), 2));

    /* Below the view, outside its clip */
    test_assert(sf_is(sf_at(px, 22, 42), 1));
    test_assert(sf_is(sf_at(px, 22, 51), 1));

    /* Inside the view, over the rest of its content */
    test_assert(sf_is(sf_at(px, 22, 27), 1));
    test_assert(sf_is(sf_at(px, 22, 18), 0));

    /* Nothing past the list */
    test_assert(sf_black(sf_at(px, 22, 53)));
    test_assert(sf_black(sf_at(px, 43, 30)));

    stbi_image_free(px);
    ecs_fini(world);
}

void ScrollFloat_float_takes_click_over_sibling(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_assert(ecs_script(world, { .code = SF_PROBE
        "hud {\n"
        "  Scene: {}\n"
        "  camera {\n"
        "    Position: {0, 0, 100}\n"
        "    Camera: {ortho: true, ortho_scale: 1}\n"
        "  }\n"
        "  view {\n"
        "    Position: {-10, 10, 0}\n"
        "    Rect: {20, 20}\n"
        "    Layout: {direction: Column}\n"
        "    LayoutItem: {width: {mode: Fixed, value: 20}, height: {mode: Fixed, value: 20}}\n"
        "    LayoutScroll: {vertical: true}\n"
        "    ClipChildren\n"
        "    button {\n"
        "      Rect: {20, 10}\n"
        "      Layout: {direction: Column}\n"
        "      LayoutItem: {width: {mode: Fixed, value: 20}, height: {mode: Fixed, value: 10}}\n"
        "      list {\n"
        "        Rect: {30, 30}\n"
        "        LayoutFloat: {h: Left, v: Top, anchor_h: Left, anchor_v: Bottom}\n"
        "        Interactive\n"
        "        Probe: {}\n"
        "      }\n"
        "    }\n"
        "  }\n"
        "  sibling {\n"
        "    Position: {10, -10, 0}\n"
        "    Rect: {20, 20}\n"
        "    Interactive\n"
        "    Probe: {}\n"
        "  }\n"
        "}\n"
        "hud_view {\n"
        "  View: {camera: hud.camera}\n"
        "}\n" }) != 0);

    ecs_entity_t list = ecs_lookup(world, "hud.view.button.list");
    ecs_entity_t sibling = ecs_lookup(world, "hud.sibling");
    test_assert(list != 0 && sibling != 0);
    ecs_progress(world, 0);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);

    /* Where the list overlaps the sibling drawn after it */
    sf_click(world, s, "mouse_click left [645, 370]");
    test_int(sf_clicks(world, list), 1);
    test_int(sf_clicks(world, sibling), 0);

    /* Below the view, outside its clip */
    sf_click(world, s, "mouse_click left [630, 370]");
    test_int(sf_clicks(world, list), 2);
    test_int(sf_clicks(world, sibling), 0);

    /* The sibling where the list does not reach */
    sf_click(world, s, "mouse_click left [655, 370]");
    test_int(sf_clicks(world, list), 2);
    test_int(sf_clicks(world, sibling), 1);

    ecs_fini(world);
}

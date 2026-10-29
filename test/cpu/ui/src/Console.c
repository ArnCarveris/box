/**
 * @file Console.c
 * @brief The layout of the in game console: it covers the full height of the window it is drawn
 * in, also after the window is resized.
 */

#include <box.h>
#include <bake_test.h>

/** Path of the column of the visible scrollback lines */
#define CONSOLE_LOG "console.ui.screen.stack.panel.content.log"

/** Resize the window of a surface the way the platform does: the window reports its new size
 * in points through SurfaceStatus while the requested Surface size stays the same.
 *
 * @param world The world
 * @param s The surface
 * @param w Width of the window in points
 * @param h Height of the window in points
 */
static void console_window_resize(
    ecs_world_t *world,
    ecs_entity_t s,
    int32_t w,
    int32_t h)
{
    ecs_set(world, s, SurfaceStatus, {
        .width = w, .height = h, .point_width = w, .point_height = h,
        .render_width = w, .render_height = h, .window = true
    });
    ecs_progress(world, 0);
    ecs_progress(world, 0);
}

/** Check that the panel and its scrollback lines cover the full height of the window: the
 * panel is as high as the window and the visible lines fill the log up to less than one line.
 *
 * @param world The world
 * @param h Height of the window in points
 */
static void console_check_covers(
    ecs_world_t *world,
    float h)
{
    ecs_entity_t panel = ecs_lookup(world, "console.ui.screen.stack.panel");
    ecs_entity_t log = ecs_lookup(world, CONSOLE_LOG);
    const Console *c = ecs_singleton_get(world, Console);
    int32_t lines = 0;
    test_assert(panel != 0);
    test_assert(log != 0);
    ecs_iter_t it = ecs_children(world, log);
    while (ecs_children_next(&it)) {
        lines += it.count;
    }

    float log_h = ecs_get(world, log, LayoutBounds)->height;
    test_flt(ecs_get(world, panel, Rect)->height, h);
    test_flt(log_h, h - 2 * c->padding - c->line_height);
    test_assert((float)lines * c->line_height <= log_h);
    test_assert(log_h - (float)lines * c->line_height < c->line_height);
}

void Console_covers_height_after_window_resize(void) {
    // ui/console_layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 640, .height = 360});
    ecs_progress(world, 0);

    // Resized before the console opens for the first time
    console_window_resize(world, s, 640, 600);
    ecs_singleton_get_mut(world, Console)->open = true;
    ecs_singleton_modified(world, Console);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    console_check_covers(world, 600);

    // Resized while the console is open, larger and smaller
    console_window_resize(world, s, 640, 800);
    console_check_covers(world, 800);
    console_window_resize(world, s, 640, 200);
    console_check_covers(world, 200);
    ecs_fini(world);
}

void Console_covers_height_after_surface_resize(void) {
    // ui/console_layout
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t s = ecs_new(world);
    ecs_set(world, s, Surface, {.width = 640, .height = 360});
    ecs_singleton_get_mut(world, Console)->open = true;
    ecs_singleton_modified(world, Console);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    console_check_covers(world, 360);

    ecs_set(world, s, Surface, {.width = 640, .height = 800});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    console_check_covers(world, 800);

    ecs_set(world, s, Surface, {.width = 640, .height = 200});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    console_check_covers(world, 200);
    ecs_fini(world);
}

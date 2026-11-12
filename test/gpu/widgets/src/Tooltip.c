/**
 * @file Tooltip.c
 * @brief Tooltips of the widget gallery driven through the command API: the delay before they
 * show, hiding on leave and on a press, picks that pass through them and a tooltip that leaves
 * the clip of the scroll view its button is in.
 */

#include "gallery.h"
#include "render.h"
#include <bake_test.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/** Paths of the buttons with a tooltip and of their tooltips */
#define CANCEL BASICS ".buttons.cancel"
#define CANCEL_TIP CANCEL ".tip"
#define OFF BASICS ".buttons.off"
#define OFF_TIP OFF ".tip"
#define NOTES CONTAINERS ".notes"
#define SHARE NOTES ".share"
#define SHARE_TIP SHARE ".tip"

/** The check box under the tooltip of the cancel button */
#define SOUND BASICS ".toggles.sound"

/** Seconds of a frame while a test waits for a tooltip */
#define TOOLTIP_FRAME (1.0f / 30.0f)

/** Seconds the pointer rests on a parent before its tooltip shows, the default delay */
#define TOOLTIP_DELAY (0.5f)

/** Largest width of a tooltip in points, the default width */
#define TOOLTIP_WIDTH (240.0f)

/** Run frames of TOOLTIP_FRAME seconds.
 *
 * @param world The world
 * @param seconds Seconds of world time to run
 */
static void tooltip_wait(
    ecs_world_t *world,
    float seconds)
{
    int32_t i, frames = (int32_t)ceilf(seconds / TOOLTIP_FRAME);
    for (i = 0; i < frames; i ++) {
        ecs_progress(world, TOOLTIP_FRAME);
    }
}

/** Check whether a tooltip shows: its mut and whether it has a box.
 *
 * @param world The world
 * @param s The terminal session
 * @param path Path of the tooltip
 * @param shown Whether it must show
 */
static void tooltip_expect_shown(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *path,
    bool shown)
{
    char *line = flecs_asprintf("get %s box.widgets.Tooltip.mut.shown", path);
    gallery_expect(world, s, line, shown ? "true" : "false");
    ecs_os_free(line);
    ecs_entity_t e = ecs_lookup(world, path);
    test_assert(e != 0);
    test_bool(ecs_has(world, e, Rect), shown);
}

/** Move the cursor away from the widgets, onto the heading of the Basics panel.
 *
 * @param world The world
 * @param s The terminal session
 */
static void tooltip_move_away(
    ecs_world_t *world,
    ecs_entity_t s)
{
    char point[64];
    gallery_point(world, BASICS ".heading", 0, 0, point, sizeof(point));
    ecs_os_free(gallery_run(world, s, "mouse_move %s", point));
    gallery_settle(world, s);
}

void Tooltip_hidden_by_default(void) {
    // widgets/tooltip
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);

    gallery_expect(world, s, "logs warning 100", "");
    tooltip_expect_shown(world, s, CANCEL_TIP, false);
    tooltip_expect_shown(world, s, OFF_TIP, false);
    tooltip_expect_shown(world, s, SHARE_TIP, false);

    /* The tooltip floats and is never picked */
    ecs_entity_t tip = ecs_lookup(world, CANCEL_TIP);
    test_assert(tip != 0);
    test_assert(ecs_has(world, tip, SpatialIgnore));
    const LayoutFloat *f = ecs_get(world, tip, LayoutFloat);
    test_assert(f != NULL);
    test_bool(f->flip, true);
    test_bool(f->clip, false);

    ecs_fini(world);
}

void Tooltip_shows_after_delay(void) {
    // widgets/tooltip
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);
    float button[4], tip[4];

    gallery_hover(world, s, CANCEL, 0, 0, CANCEL);
    tooltip_expect_shown(world, s, CANCEL_TIP, false);
    tooltip_wait(world, TOOLTIP_DELAY * 0.6f);
    tooltip_expect_shown(world, s, CANCEL_TIP, false);
    tooltip_wait(world, TOOLTIP_DELAY * 0.6f);
    tooltip_expect_shown(world, s, CANCEL_TIP, true);
    gallery_expect(world, s, "get " CANCEL_TIP ".caption box.text.Text.value",
        "\"Discards the changes since the last save\"");

    /* The button keeps its hover under the tooltip */
    gallery_expect(world, s, "get " CANCEL " box.widgets.Button.mut.hover", "true");
    gallery_expect(world, s, "get $ box.picking.PickStatus.handler", "\"" CANCEL "\"");

    /* Below the button, centered on it, its text wrapped at the width */
    gallery_rect(world, CANCEL, button);
    gallery_rect(world, CANCEL_TIP, tip);
    test_assert(tip[1] >= button[3]);
    test_assert(fabsf((tip[0] + tip[2]) * 0.5f - (button[0] + button[2]) * 0.5f) < 1.0f);
    test_assert(tip[2] - tip[0] <= TOOLTIP_WIDTH + 0.5f);
    float line[4];
    gallery_rect(world, CANCEL_TIP ".caption", line);
    test_assert(line[3] - line[1] > 20.0f);

    /* It stays while the pointer rests */
    tooltip_wait(world, TOOLTIP_DELAY * 2);
    tooltip_expect_shown(world, s, CANCEL_TIP, true);

    ecs_fini(world);
}

void Tooltip_hides_on_leave(void) {
    // widgets/tooltip
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);

    gallery_hover(world, s, CANCEL, 0, 0, CANCEL);
    tooltip_wait(world, TOOLTIP_DELAY * 1.2f);
    tooltip_expect_shown(world, s, CANCEL_TIP, true);
    tooltip_move_away(world, s);
    tooltip_expect_shown(world, s, CANCEL_TIP, false);
    tooltip_wait(world, TOOLTIP_DELAY * 2);
    tooltip_expect_shown(world, s, CANCEL_TIP, false);

    /* Left before the delay, it never shows */
    gallery_hover(world, s, CANCEL, 0, 0, CANCEL);
    tooltip_wait(world, TOOLTIP_DELAY * 0.5f);
    tooltip_move_away(world, s);
    tooltip_wait(world, TOOLTIP_DELAY * 2);
    tooltip_expect_shown(world, s, CANCEL_TIP, false);

    /* Back on the button it shows after the delay again */
    gallery_hover(world, s, CANCEL, 0, 0, CANCEL);
    tooltip_wait(world, TOOLTIP_DELAY * 2.2f);
    tooltip_expect_shown(world, s, CANCEL_TIP, true);

    ecs_fini(world);
}

void Tooltip_hides_on_press(void) {
    // widgets/tooltip
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);

    gallery_hover(world, s, CANCEL, 0, 0, CANCEL);
    tooltip_wait(world, TOOLTIP_DELAY * 1.2f);
    tooltip_expect_shown(world, s, CANCEL_TIP, true);
    ecs_os_free(gallery_run(world, s, "mouse_button left true"));
    gallery_settle(world, s);
    tooltip_expect_shown(world, s, CANCEL_TIP, false);
    ecs_os_free(gallery_run(world, s, "mouse_button left false"));
    gallery_settle(world, s);
    test_int(gallery_changes(world, s), 1);

    /* It stays hidden while the pointer rests on, until it leaves and comes back */
    tooltip_wait(world, TOOLTIP_DELAY * 2);
    tooltip_expect_shown(world, s, CANCEL_TIP, false);
    tooltip_move_away(world, s);
    gallery_hover(world, s, CANCEL, 0, 0, CANCEL);
    tooltip_wait(world, TOOLTIP_DELAY * 1.2f);
    tooltip_expect_shown(world, s, CANCEL_TIP, true);

    ecs_fini(world);
}

void Tooltip_disabled_parent(void) {
    // widgets/tooltip
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);

    gallery_hover(world, s, OFF, 0, 0, PAGE);
    tooltip_wait(world, TOOLTIP_DELAY * 2);
    tooltip_expect_shown(world, s, OFF_TIP, false);

    ecs_fini(world);
}

void Tooltip_deleted_parent(void) {
    // widgets/tooltip
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);

    gallery_hover(world, s, CANCEL, 0, 0, CANCEL);
    tooltip_wait(world, TOOLTIP_DELAY * 1.2f);
    tooltip_expect_shown(world, s, CANCEL_TIP, true);
    ecs_entity_t tip = ecs_lookup(world, CANCEL_TIP);
    test_assert(tip != 0);
    ecs_delete(world, ecs_lookup(world, CANCEL));
    tooltip_wait(world, TOOLTIP_FRAME * 3);
    test_bool(ecs_is_alive(world, tip), false);

    ecs_fini(world);
}

void Tooltip_click_passes_through(void) {
    // widgets/tooltip
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);
    float tip[4], sound[4];

    gallery_hover(world, s, CANCEL, 0, 0, CANCEL);
    tooltip_wait(world, TOOLTIP_DELAY * 1.2f);
    tooltip_expect_shown(world, s, CANCEL_TIP, true);

    /* The tooltip covers part of the check box below the button */
    gallery_rect(world, CANCEL_TIP, tip);
    gallery_rect(world, SOUND, sound);
    float l = tip[0] > sound[0] ? tip[0] : sound[0];
    float t = tip[1] > sound[1] ? tip[1] : sound[1];
    float r = tip[2] < sound[2] ? tip[2] : sound[2];
    float b = tip[3] < sound[3] ? tip[3] : sound[3];
    test_assert(l + 2 < r && t + 2 < b);
    float x = (l + r) * 0.5f, y = (t + b) * 0.5f;

    /* The pick at a place of the tooltip goes to the check box under it, which a click checks */
    ecs_os_free(gallery_run(world, s, "mouse_move [%.1f, %.1f]", (double)x, (double)y));
    gallery_settle(world, s);
    gallery_expect(world, s, "get $ box.picking.PickStatus.handler", "\"" SOUND "\"");
    gallery_expect(world, s, "get " SOUND " box.widgets.Checkbox.mut.current", "false");
    ecs_os_free(gallery_run(world, s, "mouse_click left [%.1f, %.1f]", (double)x, (double)y));
    gallery_settle(world, s);
    gallery_expect(world, s, "get " SOUND " box.widgets.Checkbox.mut.current", "true");
    tooltip_expect_shown(world, s, CANCEL_TIP, false);

    ecs_fini(world);
}

void Tooltip_not_clipped_in_scroll(void) {
    // widgets/tooltip
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);
    float notes[4], share[4], tip[4];

    /* The Containers panel at the top of the page and the share button at the bottom of the
     * notes view, so the tooltip below it crosses the edge of the view */
    gallery_show(world, s, CONTAINERS);
    gallery_rect(world, NOTES, notes);
    gallery_rect(world, SHARE, share);
    const LayoutContent *c = ecs_get(world, ecs_lookup(world, NOTES), LayoutContent);
    test_assert(c != NULL);
    float y = c->y + share[3] - (notes[3] - 24);
    y = y < 0 ? 0 : y > c->max_y ? c->max_y : y;
    ecs_os_free(gallery_run(world, s, "set " NOTES " box.widgets.ScrollView.mut.y %.1f",
        (double)y));
    gallery_settle(world, s);
    gallery_rect(world, NOTES, notes);
    gallery_rect(world, SHARE, share);
    test_assert(share[1] >= notes[1] && share[3] <= notes[3]);

    gallery_hover(world, s, SHARE, 0, 0, SHARE);
    tooltip_wait(world, TOOLTIP_DELAY * 1.2f);
    tooltip_expect_shown(world, s, SHARE_TIP, true);

    /* Below the button, past the bottom of the view, drawn there */
    gallery_rect(world, SHARE_TIP, tip);
    test_assert(tip[1] >= share[3]);
    test_assert(tip[3] > notes[3]);
    ecs_entity_t e = ecs_lookup(world, SHARE_TIP);
    test_bool(box_render_float_hidden(world, e), false);

    ecs_entity_t window = ecs_lookup(world, "window");
    const SurfaceStatus *ss = ecs_get(world, window, SurfaceStatus);
    const WorldTransform *wt = ecs_get(world, e, WorldTransform);
    test_assert(ss != NULL && wt != NULL);
    float x = (tip[0] + tip[2]) * 0.5f, below = (notes[3] + tip[3]) * 0.5f;
    float point[3] = {
        x - (float)ss->point_width * 0.5f, (float)ss->point_height * 0.5f - below, wt->m[3][2]
    };
    test_bool(box_render_clip_contains(world, e, point), true);

    ecs_fini(world);
}

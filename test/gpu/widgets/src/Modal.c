/**
 * @file Modal.c
 * @brief The modal dialog of the Basics section of the widget gallery driven through the command
 * API: the backdrop that blocks the widgets under it, closing with the close button, Escape and
 * the backdrop, and the focus that stays inside the dialog and returns when it closes.
 */

#include "gallery.h"
#include <bake_test.h>
#include <stdlib.h>
#include <string.h>

/** Paths of the button that opens the dialog, the dialog and its parts */
#define OPEN BASICS ".buttons.open_dialog"
#define DIALOG BASICS ".buttons.dialog"
#define BACKDROP DIALOG ".backdrop"
#define SURFACE DIALOG ".surface"
#define CLOSE DIALOG ".heading.close"
#define DISCARD DIALOG ".dialog_actions.discard"
#define KEEP DIALOG ".dialog_actions.keep"

/** The button under the backdrop that the tests click */
#define SAVE BASICS ".buttons.save"

/** Frames the dialog takes after it opens or closes to move the focus */
#define MODAL_FRAMES (6)

/** Check whether the dialog is open: its mut, the gallery that follows its changes, and what it
 * draws and picks.
 *
 * @param world The world
 * @param s The terminal session
 * @param open Whether it must be open
 */
static void modal_expect_open(
    ecs_world_t *world,
    ecs_entity_t s,
    bool open)
{
    const char *v = open ? "true" : "false";
    gallery_expect(world, s, "get " DIALOG " box.widgets.Modal.mut.current", v);
    ecs_entity_t backdrop = ecs_lookup(world, BACKDROP);
    ecs_entity_t surface = ecs_lookup(world, SURFACE);
    test_assert(backdrop != 0);
    test_assert(surface != 0);
    test_bool(ecs_has(world, backdrop, Rect), open);
    test_bool(ecs_has(world, backdrop, InputBlocker), open);
    test_bool(ecs_has(world, surface, Rect), open);
    test_bool(ecs_has(world, ecs_lookup(world, DIALOG), ClipChildren), !open);
}

/** Open the dialog with a click on the Dialog button and run the frames that move the focus.
 *
 * @param world The world
 * @param s The terminal session
 */
static void modal_open(
    ecs_world_t *world,
    ecs_entity_t s)
{
    gallery_click(world, s, OPEN, 0, 0, OPEN);
    gallery_settle_frames(world, s, MODAL_FRAMES);
    modal_expect_open(world, s, true);
    gallery_expect(world, s, "get gallery Gallery.mut.dialog_open", "true");
}

/** Click a window point without checking the handler first.
 *
 * @param world The world
 * @param s The terminal session
 * @param x Window x in points
 * @param y Window y in points
 */
static void modal_click_at(
    ecs_world_t *world,
    ecs_entity_t s,
    float x,
    float y)
{
    ecs_os_free(gallery_run(world, s, "mouse_move [%.1f, %.1f]", (double)x, (double)y));
    gallery_settle(world, s);
    gallery_expect(world, s, "get $ box.picking.PickStatus.handler", "\"" BACKDROP "\"");
    ecs_os_free(gallery_run(world, s, "mouse_click left [%.1f, %.1f]", (double)x, (double)y));
    gallery_settle_frames(world, s, MODAL_FRAMES);
}

/** Click the backdrop at the title of the gallery, outside the dialog.
 *
 * @param world The world
 * @param s The terminal session
 */
static void modal_click_backdrop(
    ecs_world_t *world,
    ecs_entity_t s)
{
    float r[4];
    gallery_rect(world, HEADER ".title", r);
    modal_click_at(world, s, (r[0] + r[2]) * 0.5f, (r[1] + r[3]) * 0.5f);
}

void Modal_closed_by_default(void) {
    // widgets/modal
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);

    gallery_expect(world, s, "logs warning 100", "");
    modal_expect_open(world, s, false);
    gallery_expect(world, s, "get gallery Gallery.mut.dialog_open", "false");

    /* It floats, so it leaves the row of buttons as it was */
    const LayoutFloat *f = ecs_get(world, ecs_lookup(world, DIALOG), LayoutFloat);
    test_assert(f != NULL);
    test_bool(f->clip, false);
    test_assert(f->anchor == ecs_lookup(world, "gallery.hud.root"));

    /* Nothing of it is picked: the save button under its place gets the pointer */
    gallery_hover(world, s, SAVE, 0, 0, SAVE);

    /* Tab passes over its close button and children */
    gallery_tab_to(world, s, OPEN);
    gallery_key(world, s, "Tab");
    char *focused = gallery_focused(world, s);
    test_assert(strncmp(focused, DIALOG, strlen(DIALOG)) != 0);
    ecs_os_free(focused);

    ecs_fini(world);
}

void Modal_open_covers_window(void) {
    // widgets/modal
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);
    float backdrop[4], dialog[4], surface[4];

    modal_open(world, s);
    gallery_expect(world, s, "logs warning 100", "");

    /* The backdrop covers the window, which the root fills, the dialog is centered in it on its
     * surface */
    const SurfaceStatus *ss = ecs_get(world, ecs_lookup(world, "window"), SurfaceStatus);
    test_assert(ss != NULL);
    float root[4] = { 0, 0, (float)ss->point_width, (float)ss->point_height };
    gallery_rect(world, BACKDROP, backdrop);
    gallery_rect(world, DIALOG, dialog);
    gallery_rect(world, SURFACE, surface);
    int32_t i;
    for (i = 0; i < 4; i ++) {
        test_assert(abs((int)(backdrop[i] - root[i])) <= 1);
        test_assert(abs((int)(surface[i] - dialog[i])) <= 1);
    }

    test_assert(abs((int)((dialog[0] + dialog[2]) - (root[0] + root[2]))) <= 1);
    test_assert(abs((int)((dialog[1] + dialog[3]) - (root[1] + root[3]))) <= 1);
    test_assert(dialog[2] - dialog[0] > 359 && dialog[2] - dialog[0] < 361);
    gallery_expect(world, s, "get " DIALOG ".heading.caption box.text.Text.value",
        "\"Discard changes?\"");

    /* The title is at the top of the dialog, the close button at the right of it */
    float close[4], message[4];
    gallery_rect(world, CLOSE, close);
    gallery_rect(world, DIALOG ".message", message);
    test_assert(close[3] <= message[1]);
    test_assert(close[2] > message[2] - 2);

    ecs_fini(world);
}

void Modal_blocks_input(void) {
    // widgets/modal
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);
    float save[4];

    modal_open(world, s);
    ecs_os_free(gallery_run(world, s, "set " DIALOG " box.widgets.Modal.dismiss false"));
    gallery_settle(world, s);
    int32_t changes = gallery_changes(world, s);

    /* The save button under the backdrop gets neither the hover nor the click */
    gallery_rect(world, SAVE, save);
    modal_click_at(world, s, (save[0] + save[2]) * 0.5f, (save[1] + save[3]) * 0.5f);
    gallery_expect(world, s, "get " SAVE " box.widgets.Button.mut.hover", "false");
    gallery_expect(world, s, "get " SAVE " box.widgets.Button.mut.down", "false");
    test_int(gallery_changes(world, s), changes);
    modal_expect_open(world, s, true);

    /* The wheel over the backdrop does not scroll the page under it */
    const LayoutContent *c = ecs_get(world, ecs_lookup(world, PAGE), LayoutContent);
    test_assert(c != NULL);
    float y = c->y;
    ecs_os_free(gallery_run(world, s, "mouse_scroll 0 -5"));
    gallery_settle_frames(world, s, 4);
    c = ecs_get(world, ecs_lookup(world, PAGE), LayoutContent);
    test_flt(c->y, y);

    /* The dialog takes the pointer over the text in it, which handles none */
    gallery_hover(world, s, DIALOG ".message", 0, 0, DIALOG);

    ecs_fini(world);
}

void Modal_close_button(void) {
    // widgets/modal
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);

    modal_open(world, s);
    int32_t changes = gallery_changes(world, s);
    gallery_click(world, s, CLOSE, 0, 0, CLOSE);
    gallery_settle_frames(world, s, MODAL_FRAMES);
    modal_expect_open(world, s, false);

    /* The dialog reported the change, which the gallery followed, and so did its close button */
    gallery_expect(world, s, "get gallery Gallery.mut.dialog_open", "false");
    test_int(gallery_changes(world, s), changes + 2);

    /* Closed, the save button under it takes clicks again */
    gallery_click(world, s, SAVE, 0, 0, SAVE);
    test_int(gallery_changes(world, s), changes + 3);
    gallery_expect(world, s, "get gallery Gallery.mut.last", "\"save\"");

    /* The dialog opens again */
    modal_open(world, s);

    ecs_fini(world);
}

void Modal_escape(void) {
    // widgets/modal
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);

    modal_open(world, s);
    int32_t changes = gallery_changes(world, s);
    gallery_key(world, s, "Escape");
    gallery_settle_frames(world, s, MODAL_FRAMES);
    modal_expect_open(world, s, false);
    test_int(gallery_changes(world, s), changes + 1);
    gallery_expect(world, s, "get gallery Gallery.mut.last", "\"dialog: false\"");
    gallery_expect(world, s, "get gallery Gallery.mut.dialog_open", "false");

    /* Escape with the dialog closed changes nothing */
    gallery_key(world, s, "Escape");
    test_int(gallery_changes(world, s), changes + 1);

    ecs_fini(world);
}

void Modal_backdrop_dismiss(void) {
    // widgets/modal
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);

    modal_open(world, s);
    int32_t changes = gallery_changes(world, s);
    modal_click_backdrop(world, s);
    modal_expect_open(world, s, false);
    test_int(gallery_changes(world, s), changes + 1);
    gallery_expect(world, s, "get gallery Gallery.mut.last", "\"dialog: false\"");
    gallery_expect(world, s, "get gallery Gallery.mut.dialog_open", "false");

    ecs_fini(world);
}

void Modal_backdrop_no_dismiss(void) {
    // widgets/modal
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);

    modal_open(world, s);
    ecs_os_free(gallery_run(world, s, "set " DIALOG " box.widgets.Modal.dismiss false"));
    gallery_settle_frames(world, s, MODAL_FRAMES);
    int32_t changes = gallery_changes(world, s);
    modal_click_backdrop(world, s);
    modal_expect_open(world, s, true);
    test_int(gallery_changes(world, s), changes);

    /* The press cleared the focus, the dialog takes it back */
    char *focused = gallery_focused(world, s);
    test_assert(strncmp(focused, DIALOG, strlen(DIALOG)) == 0);
    ecs_os_free(focused);

    /* The close button still closes it */
    gallery_click(world, s, CLOSE, 0, 0, CLOSE);
    gallery_settle_frames(world, s, MODAL_FRAMES);
    modal_expect_open(world, s, false);

    ecs_fini(world);
}

void Modal_tab_cycles(void) {
    // widgets/modal
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);

    /* Opened, the focus is on the first focusable child */
    modal_open(world, s);
    gallery_expect_focus(world, s, DISCARD);

    /* Tab walks the close button and the children in draw order and stays inside */
    gallery_key(world, s, "Tab");
    gallery_expect_focus(world, s, KEEP);
    gallery_key(world, s, "Tab");
    gallery_expect_focus(world, s, CLOSE);
    gallery_key(world, s, "Tab");
    gallery_expect_focus(world, s, DISCARD);

    ecs_os_free(gallery_run(world, s, "key_down ShiftLeft"));
    gallery_settle(world, s);
    gallery_key(world, s, "Tab");
    gallery_expect_focus(world, s, CLOSE);
    gallery_key(world, s, "Tab");
    gallery_expect_focus(world, s, KEEP);
    ecs_os_free(gallery_run(world, s, "key_up ShiftLeft"));
    gallery_settle(world, s);

    /* Enter on the focused close button closes the dialog */
    gallery_key(world, s, "Tab");
    gallery_expect_focus(world, s, CLOSE);
    gallery_key(world, s, "Enter");
    gallery_settle_frames(world, s, MODAL_FRAMES);
    modal_expect_open(world, s, false);

    ecs_fini(world);
}

void Modal_focus_returns(void) {
    // widgets/modal
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);

    /* Opened from the keyboard, Escape gives the focus back to the Dialog button */
    gallery_tab_to(world, s, OPEN);
    gallery_key(world, s, "Enter");
    gallery_settle_frames(world, s, MODAL_FRAMES);
    modal_expect_open(world, s, true);
    gallery_expect_focus(world, s, DISCARD);
    gallery_key(world, s, "Escape");
    gallery_settle_frames(world, s, MODAL_FRAMES);
    modal_expect_open(world, s, false);
    gallery_expect_focus(world, s, OPEN);

    /* Opened with a click and closed with the close button, the same */
    modal_open(world, s);
    gallery_expect_focus(world, s, DISCARD);
    gallery_click(world, s, CLOSE, 0, 0, CLOSE);
    gallery_settle_frames(world, s, MODAL_FRAMES);
    modal_expect_open(world, s, false);
    gallery_expect_focus(world, s, OPEN);

    /* Tab goes on from there to the next widget after the Dialog button */
    gallery_key(world, s, "Tab");
    char *focused = gallery_focused(world, s);
    test_assert(strncmp(focused, BASICS, strlen(BASICS)) == 0);
    test_assert(strncmp(focused, DIALOG, strlen(DIALOG)) != 0);
    ecs_os_free(focused);

    ecs_fini(world);
}

void Modal_set_mut_no_change(void) {
    // widgets/modal
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);
    int32_t changes = gallery_changes(world, s);

    /* The mut set from the terminal opens and closes the dialog without a change */
    ecs_os_free(gallery_run(world, s, "set " DIALOG " box.widgets.Modal.mut.current true"));
    gallery_settle_frames(world, s, MODAL_FRAMES);
    modal_expect_open(world, s, true);
    gallery_expect_focus(world, s, DISCARD);
    ecs_os_free(gallery_run(world, s, "set " DIALOG " box.widgets.Modal.mut.current false"));
    gallery_settle_frames(world, s, MODAL_FRAMES);
    modal_expect_open(world, s, false);
    test_int(gallery_changes(world, s), changes);
    gallery_expect_focus(world, s, "#0");

    ecs_fini(world);
}

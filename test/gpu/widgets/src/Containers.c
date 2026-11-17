/**
 * @file Containers.c
 * @brief The Containers section of the widget gallery driven through the command API: dropdowns,
 * the tab view, the scroll view, the list view and the collapsible sections.
 */

#include "gallery.h"
#include "render.h"
#include "draw.h"
#include "picking.h"
#include <bake_test.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include <stb_image.h>

/** File the window writes for the captures */
#define CONTAINERS_CAPTURE "containers_border.png"

/** Paths of the widgets of the Containers section */
#define TABS CONTAINERS ".tabs"
#define NOTES CONTAINERS ".notes"
#define FORMAT NOTES ".picker.format"
#define FILES NOTES ".files"
#define QUALITY CONTAINERS ".quality_row.quality"
#define DETAILS CONTAINERS ".details"
#define ADVANCED CONTAINERS ".advanced"

/** Points of a wheel step, the height of a list row and the space above the first row */
#define CONTAINERS_STEP (40.0)
#define CONTAINERS_ROW (28.0)
#define CONTAINERS_INSET (4.0)

/** Thickness of the scroll bar of a scroll view */
#define CONTAINERS_BAR (10.0f)

/** Points between the scroll bars and the edges of their view, its border */
#define CONTAINERS_EDGE (1.0f)

/** The smaller of two numbers */
#define CONTAINERS_MIN(a, b) ((a) < (b) ? (a) : (b))

/** Load the gallery with the Containers panel at the top of the page, where all of it is in
 * view, so the tests can point at its widgets.
 *
 * @param session Receives the terminal session
 * @return The world
 */
static ecs_world_t* containers_load(
    ecs_entity_t *session)
{
    ecs_world_t *world = gallery_load(session);
    gallery_show(world, *session, CONTAINERS);

    return world;
}

/** Get a member of the mut of a widget of the Containers section, as the get command prints it.
 *
 * @param world The world
 * @param s The terminal session
 * @param path Path of the widget
 * @param tmpl Template of the widget, such as Dropdown
 * @param member Member of the mut
 * @return The value, owned by the caller
 */
static char* containers_mut(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *path,
    const char *tmpl,
    const char *member)
{
    return gallery_run(world, s, "get %s box.widgets.%s.mut.%s", path, tmpl, member);
}

/** Check a member of the mut of a widget.
 *
 * @param world The world
 * @param s The terminal session
 * @param path Path of the widget
 * @param tmpl Template of the widget
 * @param member Member of the mut
 * @param expect The value as the get command prints it
 */
static void containers_expect(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *path,
    const char *tmpl,
    const char *member,
    const char *expect)
{
    char *out = containers_mut(world, s, path, tmpl, member);
    test_str(out, expect);
    ecs_os_free(out);
}

/** Get a numeric member of the mut of a widget.
 *
 * @param world The world
 * @param s The terminal session
 * @param path Path of the widget
 * @param tmpl Template of the widget
 * @param member Member of the mut
 * @return The value
 */
static double containers_number(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *path,
    const char *tmpl,
    const char *member)
{
    return gallery_value(world, s, "get %s box.widgets.%s.mut.%s", path, tmpl, member);
}

/** Get the LayoutContent of a scroll view.
 *
 * @param world The world
 * @param path Path of the scroll view
 * @return The component
 */
static const LayoutContent* containers_content(
    ecs_world_t *world,
    const char *path)
{
    const LayoutContent *result = ecs_get(world, ecs_lookup(world, path), LayoutContent);
    test_assert(result != NULL);

    return result;
}

/** Get the window rectangle of an element, top left and bottom right.
 *
 * @param world The world
 * @param path Path of the element
 * @param r Receives left, top, right and bottom in window points
 */
static void containers_rect(
    ecs_world_t *world,
    const char *path,
    float r[4])
{
    ecs_entity_t e = ecs_lookup(world, path);
    ecs_entity_t window = ecs_lookup(world, "window");
    test_assert(e != 0);
    const WorldTransform *t = ecs_get(world, e, WorldTransform);
    const LayoutBounds *b = ecs_get(world, e, LayoutBounds);
    const SurfaceStatus *ss = ecs_get(world, window, SurfaceStatus);
    test_assert(t != NULL && b != NULL && ss != NULL);
    float x = (float)ss->point_width * 0.5f + t->m[3][0];
    float y = (float)ss->point_height * 0.5f - t->m[3][1];
    r[0] = x - b->width * 0.5f;
    r[1] = y - b->height * 0.5f;
    r[2] = x + b->width * 0.5f;
    r[3] = y + b->height * 0.5f;
}

/** Move the wheel over the place a cursor is at and run the input.
 *
 * @param world The world
 * @param s The terminal session
 * @param dx Wheel steps across, positive to the left
 * @param dy Wheel steps along, positive up
 */
static void containers_wheel(
    ecs_world_t *world,
    ecs_entity_t s,
    float dx,
    float dy)
{
    ecs_os_free(gallery_run(world, s, "mouse_scroll %.1f %.1f", (double)dx, (double)dy));
    gallery_settle(world, s);
}

/** Get the window point of the thumb of the vertical scroll bar of a scroll view.
 *
 * @param world The world
 * @param path Path of the scroll view
 * @param along Place along the thumb, 0 its top and 1 its bottom
 * @param out Receives the point as [x, y]
 * @param size Bytes of out
 */
static void containers_thumb(
    ecs_world_t *world,
    const char *path,
    float along,
    char *out,
    size_t size)
{
    const LayoutContent *c = containers_content(world, path);
    float r[4];
    containers_rect(world, path, r);
    float view = r[3] - r[1];
    float track = view - 2 * CONTAINERS_EDGE;
    float thumb = track * view / c->height;
    float at = c->max_y > 0 ? (track - thumb) * c->y / c->max_y : 0;
    ecs_os_snprintf(out, (ecs_size_t)size, "[%.1f, %.1f]",
        (double)(r[2] - CONTAINERS_EDGE - CONTAINERS_BAR * 0.5f),
        (double)(r[1] + CONTAINERS_EDGE + at + thumb * along));
}

/** Run a key or button command with a Space binding and count the frames it was pressed in.
 *
 * @param world The world
 * @param s The terminal session
 * @param cmd The command
 * @param action The action of the binding
 * @return Frames in which the action was pressed, over six frames
 */
static int32_t containers_presses(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *cmd,
    ecs_entity_t action)
{
    int32_t i, result = 0;
    ecs_os_free(gallery_run(world, s, "%s", cmd));
    for (i = 0; i < 6; i ++) {
        ecs_progress(world, 0);
        result += ecs_get(world, action, ActionState)->pressed;
    }

    gallery_settle(world, s);

    return result;
}

/** Give an entity the keyboard focus as Tab would, without tabbing through the whole gallery.
 *
 * Presses Tab once while the focus is not visible, so the focus that follows is visible.
 *
 * @param world The world
 * @param s The terminal session
 * @param path Path of the entity
 */
static void containers_focus(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *path)
{
    gallery_tab_to(world, s, path);
    gallery_expect_focus(world, s, path);
}

/** Check whether Tab, from the entity that has the focus, reaches an entity before the focus
 * leaves a part of the tree.
 *
 * @param world The world
 * @param s The terminal session
 * @param path Path of the entity
 * @param scope Path of the part of the tree, which holds the focused entity
 * @return Whether Tab focused it
 */
static bool containers_tab_reaches(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *path,
    const char *scope)
{
    size_t len = strlen(scope);
    bool result = false, inside = true;
    int32_t i;
    for (i = 0; i < 20 && !result && inside; i ++) {
        gallery_key(world, s, "Tab");
        char *focused = gallery_focused(world, s);
        result = !strcmp(focused, path);
        inside = !strncmp(focused, scope, len) && (!focused[len] || focused[len] == '.');
        ecs_os_free(focused);
    }

    test_assert(result || !inside);

    return result;
}

/** Check a window coordinate, which the transforms of the layout round a little.
 *
 * @param v The coordinate
 * @param expect The coordinate it must be
 */
static void gallery_expect_at(
    float v,
    float expect)
{
    if (fabsf(v - expect) >= 0.01f) {
        printf("coordinate %f, expected %f\n", (double)v, (double)expect);
    }

    test_assert(fabsf(v - expect) < 0.01f);
}

/** Check that a window coordinate is at most a bound, give or take the rounding.
 *
 * @param v The coordinate
 * @param bound The bound
 */
static void gallery_expect_at_most(
    float v,
    float bound)
{
    if (v >= bound + 0.01f) {
        printf("coordinate %f, expected at most %f\n", (double)v, (double)bound);
    }

    test_assert(v < bound + 0.01f);
}

/** Check that a window coordinate is at least a bound, give or take the rounding.
 *
 * @param v The coordinate
 * @param bound The bound
 */
static void gallery_expect_at_least(
    float v,
    float bound)
{
    if (v <= bound - 0.01f) {
        printf("coordinate %f, expected at least %f\n", (double)v, (double)bound);
    }

    test_assert(v > bound - 0.01f);
}

/** Get the window rectangle of a shape without LayoutBounds, such as a scroll bar, from its Rect.
 *
 * @param world The world
 * @param path Path of the shape
 * @param r Receives left, top, right and bottom in window points
 */
static void containers_shape(
    ecs_world_t *world,
    const char *path,
    float r[4])
{
    ecs_entity_t e = ecs_lookup(world, path);
    ecs_entity_t window = ecs_lookup(world, "window");
    test_assert(e != 0);
    const WorldTransform *t = ecs_get(world, e, WorldTransform);
    const Rect *rect = ecs_get(world, e, Rect);
    const SurfaceStatus *ss = ecs_get(world, window, SurfaceStatus);
    test_assert(t != NULL && rect != NULL && ss != NULL);
    float x = (float)ss->point_width * 0.5f + t->m[3][0];
    float y = (float)ss->point_height * 0.5f - t->m[3][1];
    r[0] = x - rect->width * 0.5f;
    r[1] = y - rect->height * 0.5f;
    r[2] = x + rect->width * 0.5f;
    r[3] = y + rect->height * 0.5f;
}

/** Check whether the clips around an entity keep a window point of it, as the 2D shader and
 * picking clip it.
 *
 * @param world The world
 * @param path Path of the entity
 * @param x Window x of the point
 * @param y Window y of the point
 * @return Whether no ClipChildren ancestor clips the point away
 */
static bool containers_clip_keeps(
    ecs_world_t *world,
    const char *path,
    float x,
    float y)
{
    ecs_entity_t e = ecs_lookup(world, path);
    ecs_entity_t window = ecs_lookup(world, "window");
    test_assert(e != 0);
    const WorldTransform *t = ecs_get(world, e, WorldTransform);
    const SurfaceStatus *ss = ecs_get(world, window, SurfaceStatus);
    test_assert(t != NULL && ss != NULL);
    float point[3] = {
        x - (float)ss->point_width * 0.5f, (float)ss->point_height * 0.5f - y, t->m[3][2]
    };

    return box_render_clip_contains(world, e, point);
}

/** Scroll the page so that the top of an element is at a window y, as far as the page scrolls.
 *
 * @param world The world
 * @param s The terminal session
 * @param path Path of the element
 * @param top Window y its top goes to
 */
static void containers_page_top(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *path,
    float top)
{
    float r[4];
    containers_rect(world, path, r);
    const LayoutContent *c = containers_content(world, PAGE);
    float y = c->y + r[1] - top;
    y = y < 0 ? 0 : y > c->max_y ? c->max_y : y;
    ecs_os_free(gallery_run(world, s, "set " PAGE " box.widgets.ScrollView.mut.y %.1f",
        (double)y));
    gallery_settle(world, s);
}

/** Get the handler PickStatus reports at a window point after moving the cursor there.
 *
 * @param world The world
 * @param s The terminal session
 * @param x Window x
 * @param y Window y
 * @return The handler as the get command prints it, owned by the caller
 */
static char* containers_handler_at(
    ecs_world_t *world,
    ecs_entity_t s,
    float x,
    float y)
{
    ecs_os_free(gallery_run(world, s, "mouse_move [%.1f, %.1f]", (double)x, (double)y));
    gallery_settle(world, s);

    return gallery_run(world, s, "get $ box.picking.PickStatus.handler");
}

/** Resize the window and run the frames that lay the gallery out again.
 *
 * @param world The world
 * @param width Width in points
 * @param height Height in points
 */
static void containers_resize(
    ecs_world_t *world,
    int32_t width,
    int32_t height)
{
    ecs_entity_t window = ecs_lookup(world, "window");
    int32_t i;
    Surface *surface = ecs_get_mut(world, window, Surface);
    surface->width = width;
    surface->height = height;
    ecs_modified(world, window, Surface);
    for (i = 0; i < 4; i ++) {
        ecs_progress(world, 0);
    }
}

void Containers_scene_loads(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = containers_load(&s);

    gallery_expect(world, s, "logs warning 100", "");
    gallery_expect(world, s, "get " CONTAINERS ".heading box.text.Text.value", "\"Containers\"");
    containers_expect(world, s, TABS, "TabView", "current", "0");
    containers_expect(world, s, TABS, "TabView", "count", "3");
    containers_expect(world, s, TABS ".audio", "Tab", "index", "1");
    containers_expect(world, s, QUALITY, "Dropdown", "current", "1");
    containers_expect(world, s, QUALITY, "Dropdown", "open", "false");
    containers_expect(world, s, FORMAT, "Dropdown", "current", "0");
    containers_expect(world, s, FILES, "ListView", "current", "0");
    containers_expect(world, s, NOTES, "ScrollView", "y", "0");
    containers_expect(world, s, DETAILS, "Collapsible", "current", "false");
    containers_expect(world, s, ADVANCED, "Collapsible", "current", "true");
    gallery_expect(world, s, "get " QUALITY ".caption box.text.Text.value", "\"Medium\"");
    gallery_expect(world, s, "get " FILES ".rows.row_0.caption box.text.Text.value",
        "\"scene_001.flecs\"");
    test_assert(ecs_lookup(world, QUALITY ".list.option_0") == 0);
    test_assert(ecs_has_id(world, ecs_lookup(world, NOTES), ClipChildren));
    test_assert(ecs_lookup(world, NOTES ".scrollbars.vscroll") != 0);
    test_assert(containers_content(world, NOTES)->max_y > CONTAINERS_STEP);

    /* The headers of the tabs follow each other in a row above the page */
    float general[4], audio[4], display[4], page[4];
    containers_rect(world, TABS ".bar.tab_0", general);
    containers_rect(world, TABS ".bar.tab_1", audio);
    containers_rect(world, TABS ".bar.tab_2", display);
    containers_rect(world, TABS ".general", page);
    test_assert(general[2] < audio[0]);
    test_assert(audio[2] < display[0]);
    test_flt(general[1], audio[1]);
    test_flt(general[3], page[1]);
    test_assert(page[3] > page[1]);
    test_flt(ecs_get(world, ecs_lookup(world, TABS ".audio"), LayoutBounds)->height, 0);
    gallery_expect(world, s, "get " TABS ".bar.tab_2.caption box.text.Text.value", "\"Display\"");

    ecs_fini(world);
}

void Containers_dropdown_click_opens(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = containers_load(&s);
    ecs_entity_t quality = ecs_lookup(world, QUALITY);

    gallery_click(world, s, QUALITY, 0, 0, QUALITY);
    containers_expect(world, s, QUALITY, "Dropdown", "open", "true");
    containers_expect(world, s, QUALITY, "Dropdown", "highlight", "1");
    gallery_expect_focus(world, s, QUALITY);
    test_assert(ecs_has_id(world, quality, KeyCapture));
    test_assert(ecs_lookup(world, QUALITY ".list.option_4") != 0);
    gallery_expect(world, s, "get " QUALITY ".list.option_3.caption box.text.Text.value",
        "\"Ultra\"");

    /* The list is as wide as the button, up to the rounding of the transforms of the page */
    float button[4], list[4];
    containers_rect(world, QUALITY, button);
    containers_rect(world, QUALITY ".list", list);
    test_assert(fabsf(list[0] - button[0]) < 0.01f);
    test_assert(fabsf(list[2] - button[2]) < 0.01f);

    gallery_click(world, s, QUALITY, 0, 0, QUALITY);
    containers_expect(world, s, QUALITY, "Dropdown", "open", "false");
    test_assert(!ecs_has_id(world, quality, KeyCapture));
    test_assert(ecs_lookup(world, QUALITY ".list.option_0") == 0);
    containers_expect(world, s, QUALITY, "Dropdown", "current", "1");
    test_int(gallery_changes(world, s), 0);
    gallery_expect_focus(world, s, QUALITY);

    ecs_fini(world);
}

void Containers_dropdown_keys_open(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = containers_load(&s);
    const char *keys[] = { "Enter", "Space", "Down", "Up" };
    int32_t i;

    containers_focus(world, s, QUALITY);
    for (i = 0; i < 4; i ++) {
        gallery_key(world, s, keys[i]);
        containers_expect(world, s, QUALITY, "Dropdown", "open", "true");
        containers_expect(world, s, QUALITY, "Dropdown", "highlight", "1");
        test_assert(ecs_has_id(world, ecs_lookup(world, QUALITY), KeyCapture));
        gallery_key(world, s, "Escape");
        containers_expect(world, s, QUALITY, "Dropdown", "open", "false");
        gallery_expect_focus(world, s, QUALITY);
    }

    containers_expect(world, s, QUALITY, "Dropdown", "current", "1");
    test_int(gallery_changes(world, s), 0);

    ecs_fini(world);
}

void Containers_dropdown_click_selects(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = containers_load(&s);

    gallery_click(world, s, QUALITY, 0, 0, QUALITY);
    gallery_click(world, s, QUALITY ".list.option_3", 0, 0, QUALITY);
    containers_expect(world, s, QUALITY, "Dropdown", "current", "3");
    containers_expect(world, s, QUALITY, "Dropdown", "open", "false");
    gallery_expect(world, s, "get " QUALITY ".caption box.text.Text.value", "\"Ultra\"");
    test_int(gallery_changes(world, s), 1);
    gallery_expect(world, s, "get gallery Gallery.mut.last", "\"quality: 3\"");
    gallery_expect_focus(world, s, QUALITY);

    /* Choosing the current option again closes the list without a change */
    gallery_click(world, s, QUALITY, 0, 0, QUALITY);
    gallery_click(world, s, QUALITY ".list.option_3", 0.5f, 0.5f, QUALITY);
    containers_expect(world, s, QUALITY, "Dropdown", "open", "false");
    test_int(gallery_changes(world, s), 1);

    ecs_fini(world);
}

void Containers_dropdown_hover_highlights(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = containers_load(&s);

    gallery_click(world, s, QUALITY, 0, 0, QUALITY);
    gallery_hover(world, s, QUALITY ".list.option_0", 0, 0, QUALITY);
    containers_expect(world, s, QUALITY, "Dropdown", "highlight", "0");
    gallery_hover(world, s, QUALITY ".list.option_4", 0.8f, 0.8f, QUALITY);
    containers_expect(world, s, QUALITY, "Dropdown", "highlight", "4");
    gallery_expect(world, s, "get " QUALITY ".list.option_4 box.material.Rgb",
        "{\"Rgb\":{\"r\":60, \"g\":65, \"b\":75}}");
    gallery_expect(world, s, "get " QUALITY ".list.option_0 box.material.Rgb",
        "{\"Rgb\":{\"r\":46, \"g\":50, \"b\":58}}");

    /* The keys move the highlight on from the hovered option */
    gallery_key(world, s, "Up");
    containers_expect(world, s, QUALITY, "Dropdown", "highlight", "3");
    gallery_key(world, s, "Enter");
    containers_expect(world, s, QUALITY, "Dropdown", "current", "3");

    ecs_fini(world);
}

void Containers_dropdown_keys_select(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = containers_load(&s);

    containers_focus(world, s, QUALITY);
    gallery_key(world, s, "Down");
    containers_expect(world, s, QUALITY, "Dropdown", "open", "true");
    gallery_key(world, s, "Down");
    gallery_key(world, s, "Down");
    containers_expect(world, s, QUALITY, "Dropdown", "highlight", "3");
    gallery_key(world, s, "Up");
    containers_expect(world, s, QUALITY, "Dropdown", "highlight", "2");
    gallery_key(world, s, "End");
    containers_expect(world, s, QUALITY, "Dropdown", "highlight", "4");
    gallery_key(world, s, "Down");
    containers_expect(world, s, QUALITY, "Dropdown", "highlight", "4");
    gallery_key(world, s, "Home");
    containers_expect(world, s, QUALITY, "Dropdown", "highlight", "0");
    gallery_key(world, s, "Up");
    containers_expect(world, s, QUALITY, "Dropdown", "highlight", "0");
    containers_expect(world, s, QUALITY, "Dropdown", "current", "1");
    test_int(gallery_changes(world, s), 0);

    gallery_key(world, s, "Enter");
    containers_expect(world, s, QUALITY, "Dropdown", "current", "0");
    containers_expect(world, s, QUALITY, "Dropdown", "open", "false");
    test_int(gallery_changes(world, s), 1);
    gallery_expect(world, s, "get gallery Gallery.mut.last", "\"quality: 0\"");

    /* Space chooses as Enter does */
    gallery_key(world, s, "Space");
    containers_expect(world, s, QUALITY, "Dropdown", "open", "true");
    gallery_key(world, s, "Down");
    gallery_key(world, s, "Space");
    containers_expect(world, s, QUALITY, "Dropdown", "current", "1");
    containers_expect(world, s, QUALITY, "Dropdown", "open", "false");
    test_int(gallery_changes(world, s), 2);
    gallery_expect_focus(world, s, QUALITY);

    ecs_fini(world);
}

void Containers_dropdown_escape_keeps(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = containers_load(&s);

    gallery_click(world, s, QUALITY, 0, 0, QUALITY);
    gallery_key(world, s, "Down");
    gallery_key(world, s, "Down");
    containers_expect(world, s, QUALITY, "Dropdown", "highlight", "3");
    gallery_key(world, s, "Escape");
    containers_expect(world, s, QUALITY, "Dropdown", "open", "false");
    containers_expect(world, s, QUALITY, "Dropdown", "current", "1");
    gallery_expect(world, s, "get " QUALITY ".caption box.text.Text.value", "\"Medium\"");
    test_int(gallery_changes(world, s), 0);
    gallery_expect_focus(world, s, QUALITY);

    /* Opening again highlights the current option */
    gallery_key(world, s, "Down");
    containers_expect(world, s, QUALITY, "Dropdown", "highlight", "1");

    ecs_fini(world);
}

void Containers_dropdown_press_outside_closes(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = containers_load(&s);
    char point[64];

    gallery_click(world, s, QUALITY, 0, 0, QUALITY);
    gallery_point(world, QUALITY ".list.option_2", 0, 0, point, sizeof(point));
    gallery_click(world, s, BASICS ".buttons.cancel", 0, 0, BASICS ".buttons.cancel");
    containers_expect(world, s, QUALITY, "Dropdown", "open", "false");
    containers_expect(world, s, QUALITY, "Dropdown", "focused", "false");
    gallery_expect_focus(world, s, BASICS ".buttons.cancel");
    containers_expect(world, s, QUALITY, "Dropdown", "current", "1");
    test_int(gallery_changes(world, s), 1);

    /* The closed list no longer takes the pointer where it was */
    ecs_os_free(gallery_run(world, s, "mouse_move %s", point));
    gallery_settle(world, s);
    char *handler = gallery_run(world, s, "get $ box.picking.PickStatus.handler");
    test_assert(strcmp(handler, "\"" QUALITY "\"") != 0);
    ecs_os_free(handler);

    /* A press on no widget clears the focus and closes the list too */
    gallery_click(world, s, QUALITY, 0, 0, QUALITY);
    gallery_click(world, s, CONTAINERS ".quality_row.quality_label", 0, 0, PAGE);
    containers_expect(world, s, QUALITY, "Dropdown", "open", "false");
    gallery_expect_focus(world, s, "#0");

    ecs_fini(world);
}

void Containers_dropdown_list_covers_below(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = containers_load(&s);
    float list[4], option[4], details[4], notes[4];

    gallery_click(world, s, FORMAT, 0, 0, FORMAT);
    containers_expect(world, s, FORMAT, "Dropdown", "open", "true");

    /* The list opens below the button, past the bottom of the scroll view it is in and over the
     * collapsible section below the view */
    containers_rect(world, FORMAT ".list", list);
    containers_rect(world, FORMAT ".list.option_0", option);
    containers_rect(world, DETAILS ".head", details);
    containers_rect(world, NOTES, notes);
    test_assert(option[1] >= notes[3]);
    test_assert(list[1] < details[3] && list[3] > details[1]);

    /* The options outside the clip of the view take the pointer over the section */
    gallery_hover(world, s, FORMAT ".list.option_0", 0, 0, FORMAT);
    containers_expect(world, s, FORMAT, "Dropdown", "highlight", "0");
    gallery_hover(world, s, FORMAT ".list.option_4", 0, 0.6f, FORMAT);
    containers_expect(world, s, FORMAT, "Dropdown", "highlight", "4");

    gallery_click(world, s, FORMAT ".list.option_0", 0.5f, 0.5f, FORMAT);
    gallery_click(world, s, FORMAT, 0, 0, FORMAT);
    gallery_click(world, s, FORMAT ".list.option_2", 0, 0, FORMAT);
    containers_expect(world, s, FORMAT, "Dropdown", "current", "2");
    gallery_expect(world, s, "get " FORMAT ".caption box.text.Text.value", "\"WebP\"");
    containers_expect(world, s, DETAILS, "Collapsible", "current", "false");
    test_int(gallery_changes(world, s), 1);
    gallery_expect(world, s, "get gallery Gallery.mut.last", "\"format: 2\"");

    /* Closed, the section under it takes the pointer again */
    gallery_hover(world, s, DETAILS ".head", 0, 0, DETAILS);

    ecs_fini(world);
}

void Containers_dropdown_opens_upward(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = containers_load(&s);
    float button[4], list[4];

    /* Near the bottom of the window the list flips above the button */
    gallery_click(world, s, QUALITY, 0, 0, QUALITY);
    containers_rect(world, QUALITY, button);
    containers_rect(world, QUALITY ".list", list);
    test_assert(list[3] <= button[1]);
    test_assert(list[1] >= 0);
    test_flt(list[3] - list[1], 5 * CONTAINERS_ROW + 2 * CONTAINERS_INSET);

    /* It still works as below the button */
    gallery_click(world, s, QUALITY ".list.option_0", 0, 0, QUALITY);
    containers_expect(world, s, QUALITY, "Dropdown", "current", "0");

    /* Higher up the list opens below the button */
    gallery_click(world, s, FORMAT, 0, 0, FORMAT);
    containers_rect(world, FORMAT, button);
    containers_rect(world, FORMAT ".list", list);
    test_assert(list[1] >= button[3]);

    ecs_fini(world);
}

/** Get the clip record the draws of the list of a dropdown nest in, the clip of the list that
 * hides it while its button is clipped out.
 *
 * @param world The world
 * @param path Path of the list
 * @return The record
 */
static const box_render_clip_t* containers_list_clip(
    ecs_world_t *world,
    const char *path)
{
    ecs_entity_t system = ecs_lookup(world, "box.render.extract.Draws");
    test_assert(system != 0);
    box_render_draws_t *d = ecs_system_get(world, system)->ctx;
    const DrawSlot *slot = ecs_get(world, ecs_lookup(world, path), DrawSlot);
    test_assert(slot != NULL);
    test_assert(slot->clip > 0);

    return ecs_vec_get_t(&d->clips.records, box_render_clip_t, slot->clip);
}

void Containers_dropdown_list_hides_scrolled_out(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = containers_load(&s);
    float page[4], button[4], option[4];

    /* With notes scrolled to its end and a window this low, the page can scroll the button
     * above its top. Opened near the top of the page, the list is drawn and picked */
    containers_resize(world, 1280, 360);
    ecs_os_free(gallery_run(world, s, "set " NOTES " box.widgets.ScrollView.mut.y %.1f",
        (double)containers_content(world, NOTES)->max_y));
    gallery_settle(world, s);
    gallery_rect(world, PAGE, page);
    containers_page_top(world, s, FORMAT, page[1] + 20);
    gallery_click(world, s, FORMAT, 0, 0, FORMAT);
    containers_expect(world, s, FORMAT, "Dropdown", "open", "true");
    ecs_entity_t list = ecs_lookup(world, FORMAT ".list");
    test_assert(list != 0);
    test_bool(ecs_get(world, list, LayoutFloat)->hide_clipped, true);
    test_bool(box_render_float_hidden(world, list), false);
    test_assert(containers_list_clip(world, FORMAT ".list")->area[0] > 0);
    containers_rect(world, FORMAT ".list.option_1", option);
    float x = (option[0] + option[2]) * 0.5f, y = (option[1] + option[3]) * 0.5f;
    test_bool(containers_clip_keeps(world, FORMAT ".list.option_1", x, y), true);
    char *handler = containers_handler_at(world, s, x, y);
    test_str(handler, "\"" FORMAT "\"");
    ecs_os_free(handler);

    /* The page scrolls the button above its top: the list stays open, floating over the page,
     * but is neither drawn nor picked */
    containers_rect(world, FORMAT, button);
    containers_page_top(world, s, FORMAT, page[1] - (button[3] - button[1]) - 10);
    containers_rect(world, FORMAT, button);
    test_assert(button[3] < page[1]);
    containers_expect(world, s, FORMAT, "Dropdown", "open", "true");
    test_bool(box_render_float_hidden(world, list), true);
    test_flt(containers_list_clip(world, FORMAT ".list")->area[0], 0);
    test_flt(containers_list_clip(world, FORMAT ".list")->area[1], 0);
    containers_rect(world, FORMAT ".list.option_1", option);
    x = (option[0] + option[2]) * 0.5f;
    y = (option[1] + option[3]) * 0.5f;
    test_assert(y > page[1] && y < page[3]);
    test_bool(containers_clip_keeps(world, FORMAT ".list.option_1", x, y), false);
    handler = containers_handler_at(world, s, x, y);
    test_assert(strcmp(handler, "\"" FORMAT "\"") != 0);
    ecs_os_free(handler);

    /* Scrolled back into view, the list shows and takes the pointer again */
    containers_page_top(world, s, FORMAT, page[1] + 20);
    containers_rect(world, FORMAT, button);
    test_assert(button[1] >= page[1]);
    test_bool(box_render_float_hidden(world, list), false);
    test_assert(containers_list_clip(world, FORMAT ".list")->area[0] > 0);
    containers_rect(world, FORMAT ".list.option_1", option);
    x = (option[0] + option[2]) * 0.5f;
    y = (option[1] + option[3]) * 0.5f;
    handler = containers_handler_at(world, s, x, y);
    test_str(handler, "\"" FORMAT "\"");
    ecs_os_free(handler);
    ecs_os_free(gallery_run(world, s, "mouse_click left [%.1f, %.1f]", (double)x, (double)y));
    gallery_settle(world, s);
    containers_expect(world, s, FORMAT, "Dropdown", "current", "1");
    containers_expect(world, s, FORMAT, "Dropdown", "open", "false");

    ecs_fini(world);
}

void Containers_dropdown_space_binding(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = containers_load(&s);
    ecs_entity_t jump = ecs_entity(world, { .name = "jump" });
    ecs_set(world, jump, Action, { NULL });
    ecs_set(world, jump, Binding, { .key = KeySpace });

    /* A click focuses without a visible focus, which leaves Space to the bindings */
    gallery_click(world, s, CONTAINERS ".quality_row.quality_label", 0, 0, PAGE);
    test_int(containers_presses(world, s, "key_press Space", jump), 1);

    /* While the list is open the dropdown captures every key */
    gallery_click(world, s, QUALITY, 0, 0, QUALITY);
    containers_expect(world, s, QUALITY, "Dropdown", "open", "true");
    test_int(containers_presses(world, s, "key_press Down", jump), 0);
    test_int(containers_presses(world, s, "key_down Space", jump), 0);
    containers_expect(world, s, QUALITY, "Dropdown", "open", "false");
    containers_expect(world, s, QUALITY, "Dropdown", "current", "2");
    test_int(containers_presses(world, s, "key_up Space", jump), 0);
    test_int(gallery_changes(world, s), 1);

    ecs_fini(world);
}

void Containers_tabs_click(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = containers_load(&s);
    const LayoutBounds *b = NULL;

    gallery_click(world, s, TABS ".bar.tab_1", 0, 0, TABS ".bar.tab_1");
    containers_expect(world, s, TABS, "TabView", "current", "1");
    test_int(gallery_changes(world, s), 1);
    gallery_expect(world, s, "get gallery Gallery.mut.last", "\"tabs: 1\"");
    gallery_expect_focus(world, s, TABS ".bar.tab_1");
    b = ecs_get(world, ecs_lookup(world, TABS ".audio"), LayoutBounds);
    test_assert(b->width > 0 && b->height > 0);
    b = ecs_get(world, ecs_lookup(world, TABS ".general"), LayoutBounds);
    test_flt(b->width, 0);
    test_flt(b->height, 0);
    gallery_expect(world, s, "get " TABS ".bar.tab_1.caption box.material.Rgb",
        "{\"Rgb\":{\"r\":214, \"g\":218, \"b\":224}}");
    gallery_expect(world, s, "get " TABS ".bar.tab_0.caption box.material.Rgb",
        "{\"Rgb\":{\"r\":142, \"g\":149, \"b\":160}}");
    gallery_expect(world, s, "get " TABS ".bar.tab_1.mark box.material.Rgb",
        "{\"Rgb\":{\"r\":98, \"g\":168, \"b\":122}}");

    /* The widgets of the shown page take the pointer */
    gallery_hover(world, s, TABS ".audio.channels", 0.75f, 0, TABS ".audio.channels");

    /* A click on the header of the selected tab changes nothing */
    gallery_click(world, s, TABS ".bar.tab_1", 0.5f, 0, TABS ".bar.tab_1");
    test_int(gallery_changes(world, s), 1);

    gallery_click(world, s, TABS ".bar.tab_2.caption", 0, 0, TABS ".bar.tab_2");
    containers_expect(world, s, TABS, "TabView", "current", "2");
    test_int(gallery_changes(world, s), 2);
    gallery_expect_focus(world, s, TABS ".bar.tab_2");

    ecs_fini(world);
}

void Containers_tabs_arrows(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = containers_load(&s);

    containers_focus(world, s, TABS ".bar.tab_0");
    gallery_expect(world, s, "get " TABS ".bar.tab_0 box.geometry.Border.width", "2");
    gallery_key(world, s, "Right");
    containers_expect(world, s, TABS, "TabView", "current", "1");
    gallery_expect_focus(world, s, TABS ".bar.tab_1");
    gallery_expect(world, s, "get " TABS ".bar.tab_1 box.geometry.Border.width", "2");
    gallery_expect(world, s, "get " TABS ".bar.tab_0 box.geometry.Border.width", "0");
    gallery_key(world, s, "Right");
    containers_expect(world, s, TABS, "TabView", "current", "2");
    gallery_expect_focus(world, s, TABS ".bar.tab_2");
    gallery_key(world, s, "Right");
    containers_expect(world, s, TABS, "TabView", "current", "0");
    gallery_expect_focus(world, s, TABS ".bar.tab_0");
    gallery_key(world, s, "Left");
    containers_expect(world, s, TABS, "TabView", "current", "2");
    gallery_expect_focus(world, s, TABS ".bar.tab_2");
    gallery_key(world, s, "Home");
    containers_expect(world, s, TABS, "TabView", "current", "0");
    gallery_key(world, s, "End");
    containers_expect(world, s, TABS, "TabView", "current", "2");
    gallery_expect_focus(world, s, TABS ".bar.tab_2");
    test_int(gallery_changes(world, s), 6);
    gallery_expect(world, s, "get gallery Gallery.mut.last", "\"tabs: 2\"");
    gallery_expect(world, s, "get $ box.ui.FocusStatus.visible", "true");

    /* Enter and Space on the header of the selected tab change nothing */
    gallery_key(world, s, "Enter");
    gallery_key(world, s, "Space");
    test_int(gallery_changes(world, s), 6);

    /* The arrow keys of a widget on a page stay with the widget */
    containers_focus(world, s, TABS ".display.mode.window");
    gallery_key(world, s, "Right");
    containers_expect(world, s, TABS, "TabView", "current", "2");
    gallery_expect_focus(world, s, TABS ".display.mode.full");

    ecs_fini(world);
}

void Containers_tabs_one_tab_stop(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = containers_load(&s);

    test_bool(ecs_has_id(world, ecs_lookup(world, TABS ".bar.tab_0"), Focusable), true);
    test_bool(ecs_has_id(world, ecs_lookup(world, TABS ".bar.tab_1"), Focusable), false);
    test_bool(ecs_has_id(world, ecs_lookup(world, TABS ".bar.tab_2"), Focusable), false);

    containers_focus(world, s, TABS ".bar.tab_0");
    gallery_key(world, s, "Tab");
    gallery_expect_focus(world, s, TABS ".general.toggles.notify");
    ecs_os_free(gallery_run(world, s, "key_down ShiftLeft"));
    gallery_key(world, s, "Tab");
    gallery_key(world, s, "Tab");
    ecs_os_free(gallery_run(world, s, "key_up ShiftLeft"));
    gallery_settle(world, s);
    gallery_expect_focus(world, s, TEXT ".submit.message.field");

    /* After a switch the header of the new tab is the tab stop */
    gallery_click(world, s, TABS ".bar.tab_1", 0, 0, TABS ".bar.tab_1");
    test_bool(ecs_has_id(world, ecs_lookup(world, TABS ".bar.tab_0"), Focusable), false);
    test_bool(ecs_has_id(world, ecs_lookup(world, TABS ".bar.tab_1"), Focusable), true);
    gallery_key(world, s, "Tab");
    gallery_expect_focus(world, s, TABS ".audio.level");

    ecs_fini(world);
}

void Containers_tabs_hidden_page(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = containers_load(&s);
    char point[64];

    /* The slider of the hidden Audio page is not picked where it lies */
    gallery_point(world, TABS ".audio.level", 0, 0.8f, point, sizeof(point));
    ecs_os_free(gallery_run(world, s, "mouse_move %s", point));
    gallery_settle(world, s);
    char *handler = gallery_run(world, s, "get $ box.picking.PickStatus.handler");
    test_assert(strcmp(handler, "\"" TABS ".audio.level\"") != 0);
    ecs_os_free(handler);
    ecs_os_free(gallery_run(world, s, "mouse_click left %s", point));
    gallery_settle(world, s);
    containers_expect(world, s, TABS ".audio.level", "Slider", "current", "60");

    /* Tab passes over the widgets of the hidden pages */
    containers_focus(world, s, TABS ".bar.tab_0");
    test_bool(containers_tab_reaches(world, s, TABS ".audio.level", TABS), false);
    containers_focus(world, s, TABS ".bar.tab_0");
    test_bool(containers_tab_reaches(world, s, TABS ".display.mode.window", TABS), false);

    /* Shown, the page takes the pointer and the focus, the other page no longer does */
    gallery_click(world, s, TABS ".bar.tab_1", 0, 0, TABS ".bar.tab_1");
    gallery_hover(world, s, TABS ".audio.level", 0, 0.8f, TABS ".audio.level");
    test_bool(containers_tab_reaches(world, s, TABS ".audio.level", TABS), true);
    test_bool(containers_tab_reaches(world, s, TABS ".general.apply", TABS), false);

    ecs_fini(world);
}

void Containers_scroll_wheel(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = containers_load(&s);
    float before[4], after[4];

    containers_rect(world, NOTES ".intro", before);
    gallery_hover(world, s, NOTES, -0.99f, 0, NOTES);
    containers_wheel(world, s, 0, -1);
    test_flt(containers_number(world, s, NOTES, "ScrollView", "y"), CONTAINERS_STEP);
    test_flt(containers_content(world, NOTES)->y, CONTAINERS_STEP);
    containers_rect(world, NOTES ".intro", after);
    test_flt(after[1], before[1] - CONTAINERS_STEP);

    containers_wheel(world, s, 0, -2);
    test_flt(containers_content(world, NOTES)->y,
        CONTAINERS_MIN(3 * CONTAINERS_STEP, containers_content(world, NOTES)->max_y));
    containers_wheel(world, s, 0, 1);
    containers_wheel(world, s, 0, 2);
    test_flt(containers_content(world, NOTES)->y, 0);

    /* The view does not scroll across, with or without shift */
    ecs_os_free(gallery_run(world, s, "key_down ShiftLeft"));
    containers_wheel(world, s, 0, -1);
    ecs_os_free(gallery_run(world, s, "key_up ShiftLeft"));
    gallery_settle(world, s);
    test_flt(containers_content(world, NOTES)->x, 0);
    test_flt(containers_content(world, NOTES)->y, 0);
    test_int(gallery_changes(world, s), 0);

    ecs_fini(world);
}

void Containers_scroll_wheel_clamps(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = containers_load(&s);
    float max_y = containers_content(world, NOTES)->max_y;

    gallery_hover(world, s, NOTES, -0.99f, 0, NOTES);
    containers_wheel(world, s, 0, -20);
    test_flt(containers_number(world, s, NOTES, "ScrollView", "y"), max_y);
    test_flt(containers_content(world, NOTES)->y, max_y);
    containers_wheel(world, s, 0, -3);
    test_flt(containers_number(world, s, NOTES, "ScrollView", "y"), max_y);

    /* One step up from the end moves back one step */
    containers_wheel(world, s, 0, 1);
    test_flt(containers_content(world, NOTES)->y, max_y - CONTAINERS_STEP);
    containers_wheel(world, s, 0, 30);
    test_flt(containers_number(world, s, NOTES, "ScrollView", "y"), 0);

    ecs_fini(world);
}

void Containers_scroll_thumb_drag(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = containers_load(&s);
    const LayoutContent *c = containers_content(world, NOTES);
    float max_y = c->max_y, r[4];
    char from[64], to[64];
    containers_rect(world, NOTES, r);
    float view = r[3] - r[1];
    float track = view - 2 * CONTAINERS_EDGE;
    float thumb = track * view / c->height;
    float travel = track - thumb;
    float bar_x = r[2] - CONTAINERS_EDGE - CONTAINERS_BAR * 0.5f;

    /* The thumb is the visible fraction of the track */
    test_flt(ecs_get(world, ecs_lookup(world, NOTES ".scrollbars.vscroll.knob"), Rect)->height,
        thumb - 4);

    containers_thumb(world, NOTES, 0.5f, from, sizeof(from));
    ecs_os_free(gallery_run(world, s, "mouse_move %s", from));
    gallery_settle(world, s);
    gallery_expect(world, s, "get $ box.picking.PickStatus.handler", "\"" NOTES "\"");
    ecs_os_snprintf(to, ECS_SIZEOF(to), "[%.1f, %.1f]", (double)bar_x,
        (double)(r[1] + CONTAINERS_EDGE + thumb * 0.5f + travel * 0.5f));
    ecs_os_free(gallery_run(world, s, "mouse_drag left %s %s", from, to));
    gallery_settle(world, s);
    test_assert(fabs(containers_content(world, NOTES)->y - max_y * 0.5) < 1);
    containers_expect(world, s, NOTES, "ScrollView", "drag", "0");

    /* Past the end of the track the offset stops at the end */
    containers_thumb(world, NOTES, 0.5f, from, sizeof(from));
    ecs_os_snprintf(to, ECS_SIZEOF(to), "[%.1f, %.1f]", (double)bar_x,
        (double)(r[3] + 100));
    ecs_os_free(gallery_run(world, s, "mouse_drag left %s %s", from, to));
    gallery_settle(world, s);
    test_flt(containers_content(world, NOTES)->y, max_y);
    test_flt(containers_number(world, s, NOTES, "ScrollView", "y"), max_y);

    containers_thumb(world, NOTES, 0.5f, from, sizeof(from));
    ecs_os_snprintf(to, ECS_SIZEOF(to), "[%.1f, %.1f]", (double)bar_x,
        (double)(r[1] - 100));
    ecs_os_free(gallery_run(world, s, "mouse_drag left %s %s", from, to));
    gallery_settle(world, s);
    test_flt(containers_content(world, NOTES)->y, 0);

    ecs_fini(world);
}

void Containers_scroll_track_pages(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = containers_load(&s);
    float max_y = containers_content(world, NOTES)->max_y, r[4];
    containers_rect(world, NOTES, r);
    float fx = 1.0f - CONTAINERS_BAR / (r[2] - r[0]);
    float view = r[3] - r[1];

    /* Below the thumb the view moves a page down, at most to the end */
    gallery_click(world, s, NOTES, fx, 0.95f, NOTES);
    test_flt(containers_content(world, NOTES)->y, CONTAINERS_MIN(view, max_y));
    containers_expect(world, s, NOTES, "ScrollView", "drag", "0");

    /* Above it a page up */
    gallery_click(world, s, NOTES, fx, -0.95f, NOTES);
    test_flt(containers_content(world, NOTES)->y, 0);
    test_int(gallery_changes(world, s), 0);

    ecs_fini(world);
}

void Containers_scroll_keys(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = containers_load(&s);
    float max_y = containers_content(world, NOTES)->max_y, r[4];
    containers_rect(world, NOTES, r);

    /* Without the focus the keys do nothing */
    gallery_hover(world, s, NOTES ".intro", 0, 0, NOTES);
    gallery_key(world, s, "Down");
    test_flt(containers_content(world, NOTES)->y, 0);

    containers_focus(world, s, NOTES);
    gallery_key(world, s, "Down");
    test_flt(containers_content(world, NOTES)->y, CONTAINERS_STEP);
    gallery_key(world, s, "Down");
    test_flt(containers_content(world, NOTES)->y, 2 * CONTAINERS_STEP);
    gallery_key(world, s, "Up");
    test_flt(containers_content(world, NOTES)->y, CONTAINERS_STEP);
    gallery_key(world, s, "End");
    test_flt(containers_content(world, NOTES)->y, max_y);
    gallery_key(world, s, "Down");
    test_flt(containers_number(world, s, NOTES, "ScrollView", "y"), max_y);
    gallery_key(world, s, "Home");
    test_flt(containers_content(world, NOTES)->y, 0);
    gallery_key(world, s, "PageDown");
    test_flt(containers_content(world, NOTES)->y, CONTAINERS_MIN(r[3] - r[1], max_y));
    gallery_key(world, s, "PageUp");
    test_flt(containers_content(world, NOTES)->y, 0);
    gallery_expect_focus(world, s, NOTES);

    /* The focus ring shows on the view */
    gallery_expect(world, s, "get " NOTES " box.geometry.Border.width", "2");

    /* A widget in the view keeps its keys: Down moves the selection of the list view */
    gallery_key(world, s, "Tab");
    gallery_expect_focus(world, s, FILES);
    gallery_key(world, s, "Down");
    containers_expect(world, s, FILES, "ListView", "current", "1");
    test_flt(containers_content(world, NOTES)->y, 0);
    gallery_key(world, s, "Tab");
    gallery_expect_focus(world, s, FORMAT);
    gallery_key(world, s, "Down");
    containers_expect(world, s, FORMAT, "Dropdown", "open", "true");
    test_flt(containers_content(world, NOTES)->y, 0);

    ecs_fini(world);
}

void Containers_scroll_tab_reaches(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = containers_load(&s);
    float r[4], v[4];
    containers_rect(world, NOTES, v);
    containers_rect(world, NOTES ".share", r);
    test_assert(r[1] > v[3]);
    test_flt(containers_content(world, NOTES)->y, 0);

    /* Tab does not skip the button below the fold, and scrolls it into view */
    gallery_tab_to(world, s, NOTES ".share");
    containers_rect(world, NOTES, v);
    containers_rect(world, NOTES ".share", r);
    test_assert(containers_content(world, NOTES)->y > 0);
    test_assert(r[1] >= v[1] - 0.5f);
    test_assert(r[3] <= v[3] + 0.5f);

    ecs_fini(world);
}

void Containers_scroll_clips_picking(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = containers_load(&s);
    float notes[4], share[4];
    char point[64];

    /* The Share button lies below the bottom of the view, over the sections below it */
    containers_rect(world, NOTES, notes);
    containers_rect(world, NOTES ".share", share);
    test_assert(share[1] > notes[3]);
    gallery_point(world, NOTES ".share", 0, 0, point, sizeof(point));
    ecs_os_free(gallery_run(world, s, "mouse_move %s", point));
    gallery_settle(world, s);
    char *handler = gallery_run(world, s, "get $ box.picking.PickStatus.handler");
    test_assert(strcmp(handler, "\"" NOTES ".share\"") != 0);
    ecs_os_free(handler);
    ecs_os_free(gallery_run(world, s, "mouse_click left %s", point));
    gallery_settle(world, s);
    char *last = gallery_run(world, s, "get gallery Gallery.mut.last");
    test_assert(strcmp(last, "\"share\"") != 0);
    ecs_os_free(last);

    /* Scrolled into view, the button takes the click */
    gallery_hover(world, s, NOTES, -0.99f, 0, NOTES);
    containers_wheel(world, s, 0, -20);
    gallery_click(world, s, NOTES ".share", 0, 0, NOTES ".share");
    gallery_expect(world, s, "get gallery Gallery.mut.last", "\"share\"");

    /* The top of the list view, now above the top of the view, does not take the pointer; the
     * page scrolled the view to show the button */
    float files[4];
    containers_rect(world, FILES, files);
    containers_rect(world, NOTES, notes);
    test_assert(files[1] < notes[1]);
    ecs_os_snprintf(point, ECS_SIZEOF(point), "[%.1f, %.1f]", (double)(files[0] + 40),
        (double)((files[1] + notes[1]) * 0.5f));
    ecs_os_free(gallery_run(world, s, "mouse_move %s", point));
    gallery_settle(world, s);
    handler = gallery_run(world, s, "get $ box.picking.PickStatus.handler");
    test_assert(strcmp(handler, "\"" FILES "\"") != 0);
    ecs_os_free(handler);

    ecs_fini(world);
}

void Containers_scroll_nested(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = containers_load(&s);

    /* The wheel over the list view scrolls the list view only */
    gallery_hover(world, s, FILES ".rows.row_1", 0, 0, FILES);
    containers_wheel(world, s, 0, -2);
    test_flt(containers_content(world, FILES)->y, 2 * CONTAINERS_STEP);
    test_flt(containers_content(world, NOTES)->y, 0);

    /* Elsewhere in the outer view it scrolls the outer view only */
    gallery_hover(world, s, NOTES ".intro", 0, 0, NOTES);
    containers_wheel(world, s, 0, -1);
    test_flt(containers_content(world, NOTES)->y, CONTAINERS_STEP);
    test_flt(containers_content(world, FILES)->y, 2 * CONTAINERS_STEP);

    /* The outer view moved the list view, which still takes the wheel under the cursor */
    gallery_hover(world, s, FILES ".rows.row_3", 0, 0, FILES);
    containers_wheel(world, s, 0, 1);
    test_flt(containers_content(world, FILES)->y, CONTAINERS_STEP);
    test_flt(containers_content(world, NOTES)->y, CONTAINERS_STEP);

    ecs_fini(world);
}

void Containers_scroll_bar_clipped_by_page(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);
    float page[4], notes[4], bar[4], knob[4];

    /* The notes view runs out of the bottom of the page */
    gallery_rect(world, PAGE, page);
    containers_page_top(world, s, NOTES, page[3] - 100);
    containers_rect(world, NOTES, notes);
    test_assert(notes[1] < page[3] && notes[3] > page[3] + 20);
    test_assert(ecs_lookup(world, NOTES ".scrollbars") != 0);
    const LayoutFloat *overlay = ecs_get(
        world, ecs_lookup(world, NOTES ".scrollbars"), LayoutFloat);
    test_assert(overlay != NULL);
    test_bool(overlay->clip, true);
    test_bool(ecs_has(world, ecs_lookup(world, NOTES ".scrollbars.vscroll"), LayoutFloat), false);

    /* The track is drawn inside the page and clipped below it */
    containers_shape(world, NOTES ".scrollbars.vscroll", bar);
    test_assert(bar[3] > page[3] + 10);
    float x = (bar[0] + bar[2]) * 0.5f;
    test_bool(containers_clip_keeps(world, NOTES ".scrollbars.vscroll", x, page[3] - 20), true);
    test_bool(containers_clip_keeps(world, NOTES ".scrollbars.vscroll", x, page[3] + 5), false);

    /* At the end of its range the thumb lies below the page: not drawn, not picked */
    float max_y = containers_content(world, NOTES)->max_y;
    ecs_os_free(gallery_run(world, s, "set " NOTES " box.widgets.ScrollView.mut.y %.1f",
        (double)max_y));
    gallery_settle(world, s);
    float at_end = containers_content(world, NOTES)->y;
    test_assert(fabsf(at_end - max_y) < 0.1f);
    containers_shape(world, NOTES ".scrollbars.vscroll.knob", knob);
    float y = page[3] + 6;
    test_assert(knob[3] > y + 2);
    test_bool(containers_clip_keeps(world, NOTES ".scrollbars.vscroll.knob", x, y), false);

    char *handler = containers_handler_at(world, s, x, y);
    test_assert(strcmp(handler, "\"" NOTES "\"") != 0);
    ecs_os_free(handler);

    /* A press there does not grab the thumb */
    ecs_os_free(gallery_run(world, s, "mouse_click left [%.1f, %.1f]", (double)x, (double)y));
    gallery_settle(world, s);
    containers_expect(world, s, NOTES, "ScrollView", "drag", "0");
    test_flt(containers_content(world, NOTES)->y, at_end);

    /* Scrolled past the top of the page, the bar does not draw or pick over the header. The
     * window is low enough for the page to scroll the view that far. */
    ecs_os_free(gallery_run(world, s, "set " NOTES " box.widgets.ScrollView.mut.y 0"));
    gallery_settle(world, s);
    containers_resize(world, 1280, 400);
    gallery_rect(world, PAGE, page);
    containers_page_top(world, s, NOTES, page[1] - 100);
    containers_rect(world, NOTES, notes);
    test_assert(notes[1] < page[1] - 20 && notes[3] > page[1]);
    containers_shape(world, NOTES ".scrollbars.vscroll", bar);
    test_assert(bar[1] < page[1] - 10);
    test_bool(containers_clip_keeps(world, NOTES ".scrollbars.vscroll", x, page[1] - 5), false);
    test_bool(containers_clip_keeps(world, NOTES ".scrollbars.vscroll", x, page[1] + 20), true);
    handler = containers_handler_at(world, s, x, page[1] - 5);
    test_assert(strcmp(handler, "\"" NOTES "\"") != 0);
    ecs_os_free(handler);
    gallery_expect(world, s, "logs warning 100", "");

    ecs_fini(world);
}

void Containers_scroll_bar_inside_border(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = containers_load(&s);
    float notes[4], bar[4], files[4];

    /* The bar of the notes view keeps clear of its border: one point inside the right, top and
     * bottom edges */
    containers_rect(world, NOTES, notes);
    containers_shape(world, NOTES ".scrollbars.vscroll", bar);
    gallery_expect_at_most(bar[2], notes[2] - CONTAINERS_EDGE);
    gallery_expect_at_least(bar[0], notes[2] - CONTAINERS_EDGE - CONTAINERS_BAR);
    gallery_expect_at_least(bar[1], notes[1] + CONTAINERS_EDGE);
    gallery_expect_at_most(bar[3], notes[3] - CONTAINERS_EDGE);
    test_assert(bar[2] - bar[0] > CONTAINERS_BAR - 0.01f);

    /* Its rounded corners follow the inside of the rounded border */
    gallery_expect(world, s, "get " NOTES ".scrollbars.vscroll box.geometry.Border.radius", "3");

    /* The same for the list view */
    containers_rect(world, FILES, files);
    containers_shape(world, FILES ".scrollbars.vscroll", bar);
    gallery_expect_at_most(bar[2], files[2] - CONTAINERS_EDGE);
    gallery_expect_at_least(bar[1], files[1] + CONTAINERS_EDGE);
    gallery_expect_at_most(bar[3], files[3] - CONTAINERS_EDGE);

    /* The rows of the list stay inside its border and left of its bar */
    float row[4];
    containers_rect(world, FILES ".selected_row", row);
    gallery_expect_at_least(row[0], files[0] + CONTAINERS_EDGE);
    gallery_expect_at_most(row[2], bar[0]);

    ecs_fini(world);
}

void Containers_scroll_bar_opaque(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = containers_load(&s);

    /* The track has a color of its own, the surface, and hides the content under it */
    gallery_expect(world, s, "get " NOTES " box.material.Rgb",
        "{\"Rgb\":{\"r\":38, \"g\":41, \"b\":48}}");
    gallery_expect(world, s, "get " NOTES ".scrollbars.vscroll box.material.Rgb",
        "{\"Rgb\":{\"r\":46, \"g\":50, \"b\":58}}");
    gallery_expect(world, s, "get " FILES ".scrollbars.vscroll box.material.Rgb",
        "{\"Rgb\":{\"r\":46, \"g\":50, \"b\":58}}");
    const Opacity *o = ecs_get(world, ecs_lookup(world, NOTES ".scrollbars.vscroll"), Opacity);
    test_assert(o == NULL || o->value == 1);
    o = ecs_get(world, ecs_lookup(world, FILES ".scrollbars.vscroll"), Opacity);
    test_assert(o == NULL || o->value == 1);

    ecs_fini(world);
}

void Containers_scroll_bar_follows_scroll(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = containers_load(&s);
    float before[4], after[4], knob_before[4], knob_after[4], notes[4];

    /* The bars sit in a clipped overlay, which the content offset does not move; the bar stays
     * where it is and only its thumb moves */
    containers_rect(world, NOTES, notes);
    containers_shape(world, NOTES ".scrollbars.vscroll", before);
    containers_shape(world, NOTES ".scrollbars.vscroll.knob", knob_before);
    gallery_expect_at(knob_before[1], notes[1] + CONTAINERS_EDGE + 2);
    gallery_hover(world, s, NOTES, -0.99f, 0, NOTES);
    containers_wheel(world, s, 0, -2);
    test_flt(containers_content(world, NOTES)->y, 2 * CONTAINERS_STEP);
    containers_shape(world, NOTES ".scrollbars.vscroll", after);
    containers_shape(world, NOTES ".scrollbars.vscroll.knob", knob_after);
    gallery_expect_at(after[0], before[0]);
    gallery_expect_at(after[1], before[1]);
    gallery_expect_at(after[3], before[3]);
    test_assert(knob_after[1] > knob_before[1] + 1);

    /* At the end the thumb ends at the end of the track */
    containers_wheel(world, s, 0, -50);
    containers_shape(world, NOTES ".scrollbars.vscroll", after);
    containers_shape(world, NOTES ".scrollbars.vscroll.knob", knob_after);
    gallery_expect_at(after[1], before[1]);
    gallery_expect_at(knob_after[3], notes[3] - CONTAINERS_EDGE - 2);

    /* The scroll bars are a clipped overlay of the view, drawn over the content */
    ecs_entity_t bars = ecs_lookup(world, NOTES ".scrollbars");
    test_assert(bars != 0);
    test_assert(ecs_get_parent(world, bars) == ecs_lookup(world, NOTES));
    const LayoutFloat *overlay = ecs_get(world, bars, LayoutFloat);
    test_assert(overlay != NULL);
    test_bool(overlay->clip, true);
    ecs_entities_t children = ecs_get_ordered_children(world, ecs_lookup(world, NOTES));
    test_assert(children.count > 1);
    int32_t i;
    for (i = 0; i < children.count; i ++) {
        if (children.ids[i] != bars) {
            test_bool(box_pick_drawn_after(world, bars, children.ids[i]), true);
        }
    }

    ecs_fini(world);
}

void Containers_scroll_corner(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);
    float page[4], v[4], h[4];

    /* At 800x600 the page scrolls both ways; the tracks meet in the corner and cover it */
    containers_resize(world, 800, 600);
    gallery_rect(world, PAGE, page);
    test_assert(ecs_lookup(world, PAGE ".scrollbars.vscroll") != 0);
    test_assert(ecs_lookup(world, PAGE ".scrollbars.hscroll") != 0);
    containers_shape(world, PAGE ".scrollbars.vscroll", v);
    containers_shape(world, PAGE ".scrollbars.hscroll", h);
    gallery_expect_at(v[2], page[2] - CONTAINERS_EDGE);
    gallery_expect_at(v[3], page[3] - CONTAINERS_EDGE);
    gallery_expect_at(h[2], page[2] - CONTAINERS_EDGE);
    gallery_expect_at(h[3], page[3] - CONTAINERS_EDGE);
    float cx = page[2] - CONTAINERS_EDGE - CONTAINERS_BAR * 0.5f;
    float cy = page[3] - CONTAINERS_EDGE - CONTAINERS_BAR * 0.5f;
    test_assert(cx > v[0] && cx < v[2] && cy > v[1] && cy < v[3]);
    test_assert(cx > h[0] && cx < h[2] && cy > h[1] && cy < h[3]);

    /* The corner belongs to the page and pages neither way */
    float x = containers_content(world, PAGE)->x, y = containers_content(world, PAGE)->y;
    char *handler = containers_handler_at(world, s, cx, cy);
    test_str(handler, "\"" PAGE "\"");
    ecs_os_free(handler);
    ecs_os_free(gallery_run(world, s, "mouse_click left [%.1f, %.1f]", (double)cx, (double)cy));
    gallery_settle(world, s);
    test_flt(containers_content(world, PAGE)->x, x);
    test_flt(containers_content(world, PAGE)->y, y);

    /* The horizontal thumb moves along the track left of the vertical bar */
    containers_shape(world, PAGE ".scrollbars.hscroll.knob", h);
    gallery_expect_at_least(h[0], page[0] + CONTAINERS_EDGE);
    gallery_expect_at_most(h[2], v[0]);
    gallery_expect(world, s, "logs warning 100", "");

    ecs_fini(world);
}

void Containers_scroll_gutter(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);

    /* At 1280x800 the page only scrolls down: no horizontal bar and no room for it */
    test_flt(containers_content(world, PAGE)->max_x, 0);
    test_assert(containers_content(world, PAGE)->max_y > 0);
    test_assert(ecs_lookup(world, PAGE ".scrollbars.vscroll") != 0);
    test_assert(ecs_lookup(world, PAGE ".scrollbars.hscroll") == 0);
    const Layout *l = ecs_get(world, ecs_lookup(world, PAGE), Layout);
    test_flt(l->padding.right, 8 + CONTAINERS_BAR);
    test_flt(l->padding.bottom, 8);

    /* Where everything fits there are no bars and no gutter */
    containers_resize(world, 2400, 1800);
    test_flt(containers_content(world, PAGE)->max_x, 0);
    test_flt(containers_content(world, PAGE)->max_y, 0);
    test_assert(ecs_lookup(world, PAGE ".scrollbars") == 0);
    l = ecs_get(world, ecs_lookup(world, PAGE), Layout);
    test_flt(l->padding.right, 8);
    test_flt(l->padding.bottom, 8);

    /* Where nothing fits both bars show, each with its room */
    containers_resize(world, 800, 600);
    test_assert(ecs_lookup(world, PAGE ".scrollbars.vscroll") != 0);
    test_assert(ecs_lookup(world, PAGE ".scrollbars.hscroll") != 0);
    l = ecs_get(world, ecs_lookup(world, PAGE), Layout);
    test_flt(l->padding.right, 8 + CONTAINERS_BAR);
    test_flt(l->padding.bottom, 8 + CONTAINERS_BAR);
    gallery_expect(world, s, "logs warning 100", "");

    ecs_fini(world);
}

void Containers_scroll_thumb_hover(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = containers_load(&s);
    char point[64];

    /* Over the thumb it lightens, elsewhere it does not */
    containers_expect(world, s, NOTES, "ScrollView", "over", "0");
    test_bool(ecs_has(world, ecs_lookup(world, NOTES ".scrollbars.vscroll.knob"), Tint), false);
    containers_thumb(world, NOTES, 0.5f, point, sizeof(point));
    ecs_os_free(gallery_run(world, s, "mouse_move %s", point));
    gallery_settle(world, s);
    gallery_expect(world, s, "get $ box.picking.PickStatus.handler", "\"" NOTES "\"");
    containers_expect(world, s, NOTES, "ScrollView", "over", "1");
    test_bool(ecs_has(world, ecs_lookup(world, NOTES ".scrollbars.vscroll.knob"), Tint), true);

    /* Below the thumb on the track it does not */
    gallery_hover(world, s, NOTES, 1.0f - CONTAINERS_BAR / 300.0f, 0.95f, NOTES);
    containers_expect(world, s, NOTES, "ScrollView", "over", "0");
    test_bool(ecs_has(world, ecs_lookup(world, NOTES ".scrollbars.vscroll.knob"), Tint), false);

    /* Leaving the view ends the hover */
    containers_thumb(world, NOTES, 0.5f, point, sizeof(point));
    ecs_os_free(gallery_run(world, s, "mouse_move %s", point));
    gallery_settle(world, s);
    containers_expect(world, s, NOTES, "ScrollView", "over", "1");
    gallery_hover(world, s, DETAILS ".head", 0, 0, DETAILS);
    containers_expect(world, s, NOTES, "ScrollView", "over", "0");

    /* The list view has it too */
    gallery_hover(world, s, FILES, 1.0f - CONTAINERS_BAR / 300.0f, -0.9f, FILES);
    containers_expect(world, s, FILES, "ListView", "over", "1");
    test_bool(ecs_has(world, ecs_lookup(world, FILES ".scrollbars.vscroll.knob"), Tint), true);

    ecs_fini(world);
}

void Containers_panel_title_clipped(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = gallery_load(&s);
    float page[4], heading[4];

    /* The title of a panel is part of its column, not a float that leaves the clip of the page */
    ecs_entity_t e = ecs_lookup(world, CONTAINERS ".heading");
    test_assert(e != 0);
    test_bool(ecs_has(world, e, LayoutFloat), false);
    ecs_entities_t children = ecs_get_ordered_children(world, ecs_lookup(world, CONTAINERS));
    test_assert(children.count > 1);
    test_assert(children.ids[0] == e);

    /* It keeps its place above the first widget of the panel */
    float panel[4], tabs[4];
    containers_rect(world, CONTAINERS, panel);
    containers_rect(world, CONTAINERS ".heading", heading);
    containers_rect(world, TABS, tabs);
    gallery_expect_at(heading[0], panel[0] + 12);
    gallery_expect_at(heading[1], panel[1] + 12);
    gallery_expect_at(tabs[1], heading[3] + 8);

    /* Scrolled above the top of the page it is clipped away, not drawn over the header. The
     * window is low enough for the page to scroll the panel that far. */
    containers_resize(world, 1280, 600);
    gallery_rect(world, PAGE, page);
    containers_page_top(world, s, CONTAINERS, page[1] - 40);
    containers_rect(world, CONTAINERS ".heading", heading);
    test_assert(heading[3] < page[1]);
    test_bool(containers_clip_keeps(world, CONTAINERS ".heading",
        (heading[0] + heading[2]) * 0.5f, (heading[1] + heading[3]) * 0.5f), false);

    ecs_fini(world);
}

void Containers_list_click_selects(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = containers_load(&s);

    gallery_click(world, s, FILES ".rows.row_2", 0, 0, FILES);
    containers_expect(world, s, FILES, "ListView", "current", "2");
    containers_expect(world, s, FILES, "ListView", "hover", "2");
    test_int(gallery_changes(world, s), 1);
    gallery_expect(world, s, "get gallery Gallery.mut.last", "\"files: 2\"");
    gallery_expect_focus(world, s, FILES);
    gallery_expect(world, s, "get " FILES ".selected_row box.layout.Item.y", "-56");
    gallery_expect(world, s, "get " FILES ".selected_row box.material.Rgb",
        "{\"Rgb\":{\"r\":60, \"g\":104, \"b\":84}}");

    /* Hovering another row marks it */
    gallery_hover(world, s, FILES ".rows.row_1", 0.5f, 0.5f, FILES);
    containers_expect(world, s, FILES, "ListView", "hover", "1");
    gallery_expect(world, s, "get " FILES ".hover_row box.material.Rgb",
        "{\"Rgb\":{\"r\":60, \"g\":65, \"b\":75}}");

    /* A click on the selected row changes nothing, one on its caption selects as the row */
    gallery_click(world, s, FILES ".rows.row_2", 0.6f, 0.6f, FILES);
    test_int(gallery_changes(world, s), 1);
    gallery_click(world, s, FILES ".rows.row_0.caption", 0, 0, FILES);
    containers_expect(world, s, FILES, "ListView", "current", "0");
    test_int(gallery_changes(world, s), 2);

    ecs_fini(world);
}

void Containers_list_keys(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = containers_load(&s);

    containers_focus(world, s, FILES);
    gallery_key(world, s, "Down");
    containers_expect(world, s, FILES, "ListView", "current", "1");
    gallery_key(world, s, "Down");
    gallery_key(world, s, "Down");
    containers_expect(world, s, FILES, "ListView", "current", "3");
    gallery_key(world, s, "Up");
    containers_expect(world, s, FILES, "ListView", "current", "2");
    gallery_key(world, s, "PageDown");
    containers_expect(world, s, FILES, "ListView", "current", "5");
    gallery_key(world, s, "PageUp");
    containers_expect(world, s, FILES, "ListView", "current", "2");
    gallery_key(world, s, "End");
    containers_expect(world, s, FILES, "ListView", "current", "199");
    gallery_key(world, s, "Home");
    containers_expect(world, s, FILES, "ListView", "current", "0");
    test_int(gallery_changes(world, s), 8);
    gallery_expect(world, s, "get gallery Gallery.mut.last", "\"files: 0\"");

    /* The keys of the list do not scroll the view around it */
    test_flt(containers_content(world, NOTES)->y, 0);
    gallery_expect_focus(world, s, FILES);

    ecs_fini(world);
}

void Containers_list_keeps_selection_in_view(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = containers_load(&s);
    float list[4], row[4];
    int32_t i;

    containers_focus(world, s, FILES);
    for (i = 0; i < 6; i ++) {
        gallery_key(world, s, "Down");
    }

    /* Row 6 ends 4 + 7 rows down, the bottom of the view shows it and the inset below it */
    containers_expect(world, s, FILES, "ListView", "current", "6");
    containers_rect(world, FILES, list);
    test_flt(containers_content(world, FILES)->y,
        CONTAINERS_INSET * 2 + 7 * CONTAINERS_ROW - (list[3] - list[1]));
    containers_rect(world, FILES ".rows.row_6", row);
    test_assert(row[1] >= list[1] && row[3] <= list[3]);

    gallery_key(world, s, "End");
    test_flt(containers_content(world, FILES)->y, containers_content(world, FILES)->max_y);
    containers_rect(world, FILES ".rows.row_199", row);
    test_assert(row[1] >= list[1] && row[3] <= list[3]);

    /* Up from the end keeps the view, which shows that row already */
    float at_end = containers_content(world, FILES)->y;
    gallery_key(world, s, "Up");
    test_flt(containers_content(world, FILES)->y, at_end);

    gallery_key(world, s, "Home");
    test_flt(containers_content(world, FILES)->y, 0);

    /* Scrolled away with the wheel, the next key brings the selection back into view */
    gallery_hover(world, s, FILES ".rows.row_1", 0, 0, FILES);
    containers_wheel(world, s, 0, -10);
    gallery_key(world, s, "Down");
    containers_expect(world, s, FILES, "ListView", "current", "1");
    test_flt(containers_content(world, FILES)->y, CONTAINERS_ROW);

    ecs_fini(world);
}

void Containers_list_change_once(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = containers_load(&s);

    gallery_click(world, s, FILES ".rows.row_0", 0, 0, FILES);
    test_int(gallery_changes(world, s), 0);
    gallery_key(world, s, "Up");
    gallery_key(world, s, "Home");
    gallery_key(world, s, "PageUp");
    test_int(gallery_changes(world, s), 0);
    gallery_key(world, s, "Down");
    test_int(gallery_changes(world, s), 1);
    gallery_key(world, s, "End");
    test_int(gallery_changes(world, s), 2);
    gallery_key(world, s, "End");
    gallery_key(world, s, "Down");
    gallery_key(world, s, "PageDown");
    test_int(gallery_changes(world, s), 2);
    gallery_click(world, s, FILES ".rows.row_197", 0, 0, FILES);
    test_int(gallery_changes(world, s), 3);
    gallery_expect(world, s, "get gallery Gallery.mut.last", "\"files: 197\"");

    ecs_fini(world);
}

void Containers_list_200_items(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = containers_load(&s);
    const LayoutContent *c = containers_content(world, FILES);
    float list[4];

    test_assert(ecs_lookup(world, FILES ".rows.row_199") != 0);
    test_assert(ecs_lookup(world, FILES ".rows.row_200") == 0);
    gallery_expect(world, s, "get " FILES ".rows.row_199.caption box.text.Text.value",
        "\"prefab_200.flecs\"");
    containers_rect(world, FILES, list);
    test_flt(c->height, 200 * CONTAINERS_ROW + 2 * CONTAINERS_INSET);
    test_flt(c->max_y, c->height - (list[3] - list[1]));

    /* The thumb of the list is as short as it gets */
    test_flt(ecs_get(world, ecs_lookup(world, FILES ".scrollbars.vscroll.knob"), Rect)->height, 20 - 4);

    /* The wheel reaches the last row, which takes a click */
    gallery_hover(world, s, FILES ".rows.row_1", 0, 0, FILES);
    containers_wheel(world, s, 0, -200);
    test_flt(containers_content(world, FILES)->y, c->max_y);
    gallery_click(world, s, FILES ".rows.row_199", 0, 0, FILES);
    containers_expect(world, s, FILES, "ListView", "current", "199");

    ecs_fini(world);
}

void Containers_collapsible_click(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = containers_load(&s);
    float closed[4], open[4], below[4];

    containers_rect(world, DETAILS, closed);
    containers_rect(world, ADVANCED, below);
    test_flt(closed[3] - closed[1], 32);
    gallery_expect(world, s, "get " DETAILS ".head.chevron.first box.geometry.Line",
        "{\"Line\":{\"x1\":-2, \"y1\":4, \"x2\":2, \"y2\":0, \"width\":1.5}}");

    gallery_click(world, s, DETAILS ".head", 0, 0, DETAILS);
    containers_expect(world, s, DETAILS, "Collapsible", "current", "true");
    test_int(gallery_changes(world, s), 1);
    gallery_expect(world, s, "get gallery Gallery.mut.last", "\"details: true\"");
    gallery_expect_focus(world, s, DETAILS);
    containers_rect(world, DETAILS, open);
    test_assert(open[3] - open[1] > 60);
    float moved[4];
    containers_rect(world, ADVANCED, moved);
    test_flt(moved[1] - below[1], open[3] - closed[3]);
    gallery_expect(world, s, "get " DETAILS ".head.chevron.first box.geometry.Line",
        "{\"Line\":{\"x1\":-4, \"y1\":2, \"x2\":0, \"y2\":-2, \"width\":1.5}}");

    gallery_click(world, s, DETAILS ".head.caption", 0, 0, DETAILS);
    containers_expect(world, s, DETAILS, "Collapsible", "current", "false");
    test_int(gallery_changes(world, s), 2);
    containers_rect(world, DETAILS, open);
    test_flt(open[3] - open[1], 32);

    ecs_fini(world);
}

void Containers_collapsible_keys(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = containers_load(&s);

    containers_focus(world, s, DETAILS);
    gallery_expect(world, s, "get " DETAILS " box.geometry.Border.width", "2");
    gallery_key(world, s, "Enter");
    containers_expect(world, s, DETAILS, "Collapsible", "current", "true");
    gallery_key(world, s, "Space");
    containers_expect(world, s, DETAILS, "Collapsible", "current", "false");
    ecs_os_free(gallery_run(world, s, "key_down Space"));
    gallery_settle(world, s);
    containers_expect(world, s, DETAILS, "Collapsible", "down", "true");
    containers_expect(world, s, DETAILS, "Collapsible", "current", "false");
    ecs_os_free(gallery_run(world, s, "key_up Space"));
    gallery_settle(world, s);
    containers_expect(world, s, DETAILS, "Collapsible", "down", "false");
    containers_expect(world, s, DETAILS, "Collapsible", "current", "true");
    test_int(gallery_changes(world, s), 3);
    gallery_expect_focus(world, s, DETAILS);

    ecs_fini(world);
}

void Containers_collapsible_hides_content(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = containers_load(&s);
    char point[64];

    /* Closed, the check box under the header is not picked where it lies */
    gallery_point(world, DETAILS ".verbose", -0.8f, 0, point, sizeof(point));
    ecs_os_free(gallery_run(world, s, "mouse_move %s", point));
    gallery_settle(world, s);
    char *handler = gallery_run(world, s, "get $ box.picking.PickStatus.handler");
    test_assert(strcmp(handler, "\"" DETAILS ".verbose\"") != 0);
    ecs_os_free(handler);

    /* Tab goes from the section to the next one */
    containers_focus(world, s, DETAILS);
    gallery_key(world, s, "Tab");
    gallery_expect_focus(world, s, ADVANCED);
    containers_focus(world, s, DETAILS);
    test_bool(containers_tab_reaches(world, s, DETAILS ".verbose", DETAILS), false);

    /* Open, it takes the pointer and the focus */
    containers_focus(world, s, DETAILS);
    gallery_key(world, s, "Enter");
    gallery_key(world, s, "Tab");
    gallery_expect_focus(world, s, DETAILS ".verbose");
    gallery_click(world, s, DETAILS ".verbose", -0.8f, 0, DETAILS ".verbose");
    containers_expect(world, s, DETAILS ".verbose", "Checkbox", "current", "true");

    /* Closing it drops the focus of the content on the next Tab */
    gallery_click(world, s, DETAILS ".head", 0, 0, DETAILS);
    gallery_key(world, s, "Tab");
    gallery_expect_focus(world, s, ADVANCED);

    ecs_fini(world);
}

void Containers_collapsible_content_activation(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = containers_load(&s);

    /* A widget in the content takes its click, the section stays open */
    gallery_click(world, s, ADVANCED ".experimental", -0.8f, 0, ADVANCED ".experimental");
    containers_expect(world, s, ADVANCED ".experimental", "Switch", "current", "true");
    containers_expect(world, s, ADVANCED, "Collapsible", "current", "true");
    test_int(gallery_changes(world, s), 1);
    gallery_expect(world, s, "get gallery Gallery.mut.last", "\"experimental: true\"");

    /* Keys of the widget stay with it */
    containers_focus(world, s, ADVANCED ".experimental");
    gallery_key(world, s, "Space");
    containers_expect(world, s, ADVANCED ".experimental", "Switch", "current", "false");
    containers_expect(world, s, ADVANCED, "Collapsible", "current", "true");
    test_int(gallery_changes(world, s), 2);

    ecs_fini(world);
}

void Containers_tabs_inside_border(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = containers_load(&s);
    float view[4], bar[4], page[4], header[4];

    /* The bar and the page sit inside the border of the tab view, so it shows all around */
    containers_rect(world, TABS, view);
    containers_rect(world, TABS ".bar", bar);
    containers_rect(world, TABS ".general", page);
    containers_rect(world, TABS ".bar.tab_0", header);
    gallery_expect_at(bar[0], view[0] + 1);
    gallery_expect_at(bar[1], view[1] + 1);
    gallery_expect_at(bar[2], view[2] - 1);
    gallery_expect_at(header[0], view[0] + 1);
    gallery_expect_at(page[0], view[0] + 1);
    gallery_expect_at(page[2], view[2] - 1);
    gallery_expect_at(page[3], view[3] - 1);
    gallery_expect_at(page[1], bar[3]);

    /* The height of the view still gives the page its height */
    gallery_expect_at(page[3] - page[1], 100);

    /* The corners of the page and the headers follow the inside of the border */
    gallery_expect(world, s, "get " TABS ".general box.geometry.Border.radius", "3");
    gallery_expect(world, s, "get " TABS ".bar.tab_0 box.geometry.Border.radius", "3");

    ecs_fini(world);
}

void Containers_scroll_ring_under_bar(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = containers_load(&s);
    float notes[4], edge[4];

    /* Without the focus ring the bar shows no ring color */
    test_assert(ecs_lookup(world, NOTES ".scrollbars.vscroll.ring_edge") == 0);

    /* With it, the outer point of the track, over the inner point of the ring, takes the ring
     * color, so the ring keeps its width along the bar */
    gallery_tab_to(world, s, NOTES);
    gallery_settle(world, s);
    gallery_expect(world, s, "get " NOTES " box.geometry.Border.width", "2");
    ecs_entity_t e = ecs_lookup(world, NOTES ".scrollbars.vscroll.ring_edge");
    test_assert(e != 0);
    containers_rect(world, NOTES, notes);
    containers_shape(world, NOTES ".scrollbars.vscroll.ring_edge", edge);
    gallery_expect_at(edge[2], notes[2] - CONTAINERS_EDGE);
    gallery_expect_at(edge[0], notes[2] - 2);
    gallery_expect_at(edge[1], notes[1] + CONTAINERS_EDGE);
    gallery_expect_at(edge[3], notes[3] - CONTAINERS_EDGE);
    gallery_expect(world, s, "get " NOTES ".scrollbars.vscroll.ring_edge box.material.Rgb",
        "{\"Rgb\":{\"r\":120, \"g\":172, \"b\":240}}");

    /* The list view does the same */
    gallery_tab_to(world, s, FILES);
    gallery_settle(world, s);
    test_assert(ecs_lookup(world, NOTES ".scrollbars.vscroll.ring_edge") == 0);
    test_assert(ecs_lookup(world, FILES ".scrollbars.vscroll.ring_edge") != 0);

    ecs_fini(world);
}

/** Render the window at its full size into a PNG and read it back.
 *
 * @param world The world
 * @param w Receives the width in pixels
 * @param h Receives the height in pixels
 * @return The RGBA pixels, freed with stbi_image_free
 */
static unsigned char* containers_capture(
    ecs_world_t *world,
    int32_t *w,
    int32_t *h)
{
    ecs_entity_t window = ecs_lookup(world, "window");
    int32_t i, n = 0;
    Surface *surface = ecs_get_mut(world, window, Surface);
    if (!surface->write_to_file) {
        surface->write_to_file = ecs_os_strdup(CONTAINERS_CAPTURE);
    }

    surface->resolution_scale = 1.0f;
    ecs_modified(world, window, Surface);
    for (i = 0; i < 3; i ++) {
        ecs_progress(world, 0);
    }

    box_render_sync(world);
    char *path = flecs_asprintf("%s/%s", ecs_singleton_get(world, AppStatus)->tmp_dir,
        CONTAINERS_CAPTURE);
    unsigned char *result = stbi_load(path, w, h, &n, 4);
    test_assert(result != NULL);
    const SurfaceStatus *ss = ecs_get(world, window, SurfaceStatus);
    test_int(*w, ss->width);
    test_int(*h, ss->height);
    ecs_os_free(path);

    return result;
}

void Containers_scroll_content_inside_border(void) {
    // widgets/containers
    ecs_entity_t s = 0;
    ecs_world_t *world = containers_load(&s);
    ecs_entity_t window = ecs_lookup(world, "window");
    float notes[4], files[4];
    int32_t w = 0, h = 0, w2 = 0, h2 = 0, x, checked = 0, differ = 0;

    /* The border of the notes view before it scrolls, with nothing under its top edge */
    test_flt(containers_content(world, NOTES)->y, 0);
    gallery_expect(world, s, "get " NOTES " box.geometry.Border.width", "1");
    unsigned char *before = containers_capture(world, &w, &h);

    /* Scroll the view so that the list view inside it lies across its top edge */
    containers_rect(world, NOTES, notes);
    containers_rect(world, FILES, files);
    const LayoutContent *c = containers_content(world, NOTES);
    float y = files[1] - notes[1] + (files[3] - files[1]) * 0.5f;
    y = y > c->max_y ? c->max_y : y;
    test_assert(y > 0);
    ecs_os_free(gallery_run(world, s, "set " NOTES " box.widgets.ScrollView.mut.y %.1f",
        (double)y));
    gallery_settle(world, s);
    containers_rect(world, NOTES, notes);
    containers_rect(world, FILES, files);
    test_assert(files[1] < notes[1]);
    test_assert(files[3] > notes[1] + 2);
    unsigned char *after = containers_capture(world, &w2, &h2);
    test_int(w2, w);
    test_int(h2, h);

    /* The pixels of the top border over the list view keep the color of the border: the list
     * is clipped inside it. The corners and the scroll bar are left out. */
    const SurfaceStatus *ss = ecs_get(world, window, SurfaceStatus);
    float scale = (float)w / (float)ss->point_width;
    float left = files[0] > notes[0] + 8 ? files[0] : notes[0] + 8;
    float right = files[2] < notes[2] - 8 - CONTAINERS_BAR ? files[2] :
        notes[2] - 8 - CONTAINERS_BAR;
    int32_t x0 = (int32_t)(left * scale), x1 = (int32_t)(right * scale);
    int32_t row = (int32_t)((notes[1] + 0.5f) * scale);
    test_assert(x1 > x0);
    test_assert(row >= 0 && row < h);
    for (x = x0; x < x1; x ++) {
        const unsigned char *p = &before[(row * w + x) * 4];
        const unsigned char *q = &after[(row * w + x) * 4];
        bool same = abs(p[0] - q[0]) <= 6 && abs(p[1] - q[1]) <= 6 && abs(p[2] - q[2]) <= 6;
        if (!same && !differ) {
            printf("border pixel %d, %d was %d %d %d, now %d %d %d\n", x, row, p[0], p[1],
                p[2], q[0], q[1], q[2]);
        }

        differ += !same;
        checked ++;
    }

    test_assert(checked > 10);
    test_int(differ, 0);
    stbi_image_free(before);
    stbi_image_free(after);

    ecs_fini(world);
}

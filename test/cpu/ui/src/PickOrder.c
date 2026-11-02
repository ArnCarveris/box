/**
 * @file PickOrder.c
 * @brief Picking walks the views in descending View.order, views of the same order the last
 * created first, so the view drawn on top is hit first where views overlap.
 */

#include <box.h>
#include <bake_test.h>
#include "picking.h"

/** Modules of the scenes */
#define ORDER_PRELUDE \
    "using box.transform\n" \
    "using box.geometry\n" \
    "using box.camera\n" \
    "using box.view\n" \
    "using box.picking\n" \
    "using box.input\n"

/** A screen UI scene with a button at the center of the surface, 640, 360 */
#define ORDER_HUD(name) \
    name " {\n" \
    "  Scene: {}\n" \
    "  camera {\n" \
    "    Position: {0, 0, 100}\n" \
    "    Camera: {ortho: true, ortho_scale: 1}\n" \
    "  }\n" \
    "  button { Position: {0, 0, 0}\n" \
    "    Rect: {120, 32}\n" \
    "    Interactive\n" \
    "  }\n" \
    "}\n"

/** Two overlapping screen UI scenes, a and b, the view of a created first */
#define ORDER_TWO \
    ORDER_HUD("a") \
    ORDER_HUD("b")

/** Create a world with the prelude and a scene, and run two frames.
 *
 * @param scene Script code after the prelude
 * @return The world
 */
static ecs_world_t* order_world(
    const char *scene)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    char *code = flecs_asprintf("%s%s", ORDER_PRELUDE, scene);
    test_assert(ecs_script(world, { .code = code }) != 0);
    ecs_os_free(code);
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    return world;
}

/** Move the pointer to the center of the surface, run a frame and get the target.
 *
 * @param world The world
 * @return The target of the pointer events
 */
static ecs_entity_t order_pick(
    ecs_world_t *world)
{
    box_input_mouse_move(world, 640, 360, false);
    ecs_progress(world, 0);

    return ecs_singleton_get(world, PickStatus)->target;
}

/** Look up an entity that must exist.
 *
 * @param world The world
 * @param path Its path
 * @return The entity
 */
static ecs_entity_t order_e(
    ecs_world_t *world,
    const char *path)
{
    ecs_entity_t result = ecs_lookup(world, path);
    test_assert(result != 0);

    return result;
}

/** Set the order of a view.
 *
 * @param world The world
 * @param view Path of the view
 * @param order The order
 */
static void order_set(
    ecs_world_t *world,
    const char *view,
    int32_t order)
{
    ecs_entity_t e = order_e(world, view);
    ecs_get_mut(world, e, View)->order = order;
    ecs_modified(world, e, View);
}

void PickOrder_ties_last_created_first(void) {
    // ui/picking
    ecs_world_t *world = order_world(ORDER_TWO
        "a_view { View: {camera: a.camera} }\n"
        "b_view { View: {camera: b.camera} }\n");
    test_uint(order_pick(world), order_e(world, "b.button"));
    test_uint(ecs_singleton_get(world, PickStatus)->view, order_e(world, "b_view"));
    ecs_fini(world);
}

void PickOrder_highest_order_first(void) {
    // ui/picking
    ecs_world_t *world = order_world(ORDER_TWO
        "a_view { View: {camera: a.camera, order: 1} }\n"
        "b_view { View: {camera: b.camera} }\n");
    test_uint(order_pick(world), order_e(world, "a.button"));
    test_uint(ecs_singleton_get(world, PickStatus)->view, order_e(world, "a_view"));
    ecs_fini(world);
}

void PickOrder_negative_order_below(void) {
    // ui/picking
    ecs_world_t *world = order_world(ORDER_TWO
        "a_view { View: {camera: a.camera} }\n"
        "b_view { View: {camera: b.camera, order: -1} }\n");
    test_uint(order_pick(world), order_e(world, "a.button"));
    ecs_fini(world);
}

void PickOrder_runtime_change(void) {
    // ui/picking
    ecs_world_t *world = order_world(ORDER_TWO
        "a_view { View: {camera: a.camera} }\n"
        "b_view { View: {camera: b.camera} }\n");
    test_uint(order_pick(world), order_e(world, "b.button"));

    order_set(world, "a_view", 2);
    test_uint(order_pick(world), order_e(world, "a.button"));

    order_set(world, "b_view", 3);
    test_uint(order_pick(world), order_e(world, "b.button"));

    order_set(world, "b_view", 0);
    test_uint(order_pick(world), order_e(world, "a.button"));
    ecs_fini(world);
}

void PickOrder_console_order_over_editor(void) {
    // ui/picking
    ecs_world_t *world = order_world(ORDER_TWO
        "a_view { View: {camera: a.camera} }\n"
        "b_view { View: {camera: b.camera} }\n");
    order_set(world, "a_view", BOX_VIEW_ORDER_CONSOLE);
    order_set(world, "b_view", BOX_VIEW_ORDER_EDITOR);
    test_uint(order_pick(world), order_e(world, "a.button"));
    ecs_fini(world);
}

/** Views over b besides the console view over a, more than BOX_PICK_VIEW_MAX */
#define ORDER_MANY (BOX_PICK_VIEW_MAX + 6)

/** Create many views of b and one view of a at BOX_VIEW_ORDER_CONSOLE, check that the console
 * view is the first input view and gets the pick where it overlaps the others.
 * The other views tie on order 0, so the last created of them follow it.
 *
 * The scene is built in a buffer: the OS API of flecs is not set up before the world.
 *
 * @param console_last Whether the console view is created after the others
 */
static void order_many(
    bool console_last)
{
    char scene[8192];
    int len = ecs_os_snprintf(scene, sizeof(scene), "%s", ORDER_TWO);
    for (int i = 0; i <= ORDER_MANY; i ++) {
        bool console = console_last ? i == ORDER_MANY : i == 0;
        int v = console_last ? i : i - 1;
        if (console) {
            len += ecs_os_snprintf(scene + len, sizeof(scene) - (size_t)len,
                "console { View: {camera: a.camera, order: %d} }\n", BOX_VIEW_ORDER_CONSOLE);
        } else {
            len += ecs_os_snprintf(scene + len, sizeof(scene) - (size_t)len,
                "v%d { View: {camera: b.camera} }\n", v);
        }
    }

    test_assert(len < (int)sizeof(scene));
    ecs_world_t *world = order_world(scene);
    ecs_entity_t console = order_e(world, "console");
    test_assert(console_last ? console > order_e(world, "v0") : console < order_e(world, "v0"));

    const PickingState *ps = ecs_singleton_get(world, PickingState);
    test_assert(ps != NULL);
    ecs_entity_t views[BOX_PICK_VIEW_MAX];
    int32_t count = box_pick_input_views(world, ps->state, views);
    test_int(count, BOX_PICK_VIEW_MAX);
    test_uint(views[0], console);

    char name[16];
    ecs_os_snprintf(name, sizeof(name), "v%d", ORDER_MANY - 1);
    test_uint(views[1], order_e(world, name));
    ecs_os_snprintf(name, sizeof(name), "v%d", ORDER_MANY - BOX_PICK_VIEW_MAX + 1);
    test_uint(views[BOX_PICK_VIEW_MAX - 1], order_e(world, name));

    test_uint(order_pick(world), order_e(world, "a.button"));
    test_uint(ecs_singleton_get(world, PickStatus)->view, console);
    ecs_fini(world);
}

void PickOrder_many_views_keep_top_created_first(void) {
    // ui/picking
    order_many(false);
}

void PickOrder_many_views_keep_top_created_last(void) {
    // ui/picking
    order_many(true);
}

/** A screen UI scene with nested clips at the center of the surface: outer, a 100 by 100
 * ClipChildren column, and inner, an 80 by 300 ClipChildren column below its top left corner
 * filled by content. Its first child overlay, 20 points wide and as high as inner, sits 5 points
 * right of its right edge: in world space it spans 15..35 on x, the part right of 30 outside
 * inner, and -250..50 on y, the part below -50 outside outer. The argument is appended to the
 * LayoutFloat of overlay. */
#define ORDER_CLIPS(fl) \
    "using box.layout\n" \
    "hud {\n" \
    "  Scene: {}\n" \
    "  camera {\n" \
    "    Position: {0, 0, 100}\n" \
    "    Camera: {ortho: true, ortho_scale: 1}\n" \
    "  }\n" \
    "  outer {\n" \
    "    Position: {0, 0, 0}\n" \
    "    Layout: {direction: Column}\n" \
    "    LayoutItem: {width: {mode: Fixed, value: 100}, height: {mode: Fixed, value: 100}}\n" \
    "    Rect: {1, 1}\n" \
    "    Interactive\n" \
    "    ClipChildren\n" \
    "    inner {\n" \
    "      Layout: {direction: Column}\n" \
    "      LayoutItem: {width: {mode: Fixed, value: 80}, height: {mode: Fixed, value: 300}}\n" \
    "      ClipChildren\n" \
    "      overlay {\n" \
    "        LayoutFloat: {h: Right, v: Middle, anchor_h: Right, anchor_v: Middle, x: 5" fl "}\n" \
    "        LayoutItem: {width: {mode: Fixed, value: 20}, height: {mode: Grow}}\n" \
    "        Rect: {1, 1}\n" \
    "        Interactive\n" \
    "      }\n" \
    "      content {\n" \
    "        LayoutItem: {width: {mode: Fixed, value: 80}, height: {mode: Fixed, value: 300}}\n" \
    "        Rect: {1, 1}\n" \
    "        Interactive\n" \
    "      }\n" \
    "    }\n" \
    "  }\n" \
    "}\n" \
    "hud_view { View: {camera: hud.camera} }\n"

/** Move the pointer to a world position of the screen UI scene, run a frame and get the hit.
 *
 * @param world The world
 * @param x World x, 0 at the center of the surface
 * @param y World y, up
 * @return The nearest entity the pointer hit, 0 for none
 */
static ecs_entity_t order_hit_at(
    ecs_world_t *world,
    float x,
    float y)
{
    box_input_mouse_move(world, 640 + x, 360 - y, false);
    ecs_progress(world, 0);

    return ecs_singleton_get(world, PickStatus)->hit;
}

void PickOrder_clip_overlay_over_siblings(void) {
    // ui/picking
    ecs_world_t *world = order_world(ORDER_CLIPS(", clip: true"));
    ecs_entity_t overlay = order_e(world, "hud.outer.inner.overlay");
    ecs_entity_t content = order_e(world, "hud.outer.inner.content");

    /* The first child, it still draws and picks over the content, which it does not float
     * above the rest of the scene for */
    ecs_entities_t children = ecs_get_ordered_children(world, order_e(world, "hud.outer.inner"));
    test_int(children.count, 2);
    test_uint(children.ids[0], overlay);
    test_bool(box_pick_drawn_after(world, overlay, content), true);
    test_bool(box_pick_drawn_after(world, content, overlay), false);
    test_bool(box_pick_drawn_after(world, overlay, order_e(world, "hud.outer")), true);
    test_uint(order_hit_at(world, 20, 0), overlay);
    test_uint(order_hit_at(world, -20, 0), content);

    /* A sibling of a higher z still draws over it */
    ecs_set(world, content, Position, {0, 0, 1});
    ecs_progress(world, 0);
    test_bool(box_pick_drawn_after(world, content, overlay), true);

    ecs_fini(world);
}

void PickOrder_clip_overlay_not_picked_outside_clips(void) {
    // ui/picking
    ecs_world_t *world = order_world(ORDER_CLIPS(", clip: true"));
    ecs_entity_t overlay = order_e(world, "hud.outer.inner.overlay");

    /* Inside both clips it is picked; right of inner the pointer goes to outer under it, below
     * outer to nothing */
    test_uint(order_hit_at(world, 20, 0), overlay);
    test_uint(order_hit_at(world, 32, 0), order_e(world, "hud.outer"));
    test_uint(order_hit_at(world, 20, -80), 0);
    test_uint(order_hit_at(world, 32, -80), 0);

    ecs_fini(world);
}

void PickOrder_float_picked_outside_clips(void) {
    // ui/picking
    ecs_world_t *world = order_world(ORDER_CLIPS(""));
    ecs_entity_t overlay = order_e(world, "hud.outer.inner.overlay");

    /* A float without clip leaves the clips and draws over every non floating element */
    test_uint(order_hit_at(world, 20, 0), overlay);
    test_uint(order_hit_at(world, 32, 0), overlay);
    test_uint(order_hit_at(world, 20, -80), overlay);
    test_uint(order_hit_at(world, 32, -80), overlay);

    ecs_fini(world);
}

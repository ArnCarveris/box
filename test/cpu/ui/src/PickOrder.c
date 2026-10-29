/**
 * @file PickOrder.c
 * @brief Picking walks the views in descending View.order, views of the same order the last
 * created first, so the view drawn on top is hit first where views overlap.
 */

#include <box.h>
#include <bake_test.h>

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

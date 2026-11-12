/**
 * @file PickBare.c
 * @brief A UI element that draws nothing and handles nothing, such as a full-window layout root,
 * does not swallow the pointer: picking tries the UI elements under it in the same view.
 */

#include <box.h>
#include <bake_test.h>
#include "picking.h"

/** A screen UI scene with two full-window layout roots, main below and top above. main centers
 * a tab, 120 by 32 points, that handles pointer events. The argument is the content of top. */
#define BARE_HUD(top) \
    "using box.transform\n" \
    "using box.geometry\n" \
    "using box.camera\n" \
    "using box.view\n" \
    "using box.picking\n" \
    "using box.input\n" \
    "using box.layout\n" \
    "using box.surface\n" \
    "window { Surface: {width: 1280, height: 720} }\n" \
    "hud {\n" \
    "  Scene: {}\n" \
    "  camera {\n" \
    "    Position: {0, 0, 100}\n" \
    "    Camera: {ortho: true, ortho_scale: 1}\n" \
    "  }\n" \
    "  main {\n" \
    "    Position: {0, 0, 10}\n" \
    "    LayoutFill: {surface: window}\n" \
    "    Layout: {direction: Column, h: Center, v: Middle}\n" \
    "    tab {\n" \
    "      LayoutItem: {width: {mode: Fixed, value: 120}, height: {mode: Fixed, value: 32}}\n" \
    "      Rect: {1, 1}\n" \
    "      Interactive\n" \
    "    }\n" \
    "  }\n" \
    "  top {\n" \
    "    Position: {0, 0, 20}\n" \
    "    LayoutFill: {surface: window}\n" \
    "    Layout: {}\n" \
    top \
    "  }\n" \
    "}\n" \
    "hud_view { View: {camera: hud.camera} }\n"

/** Create a world with a scene and run two frames.
 *
 * @param scene Script code
 * @return The world
 */
static ecs_world_t* bare_world(
    const char *scene)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script(world, { .code = scene }) != 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    return world;
}

/** Look up an entity that must exist.
 *
 * @param world The world
 * @param path Its path
 * @return The entity
 */
static ecs_entity_t bare_e(
    ecs_world_t *world,
    const char *path)
{
    ecs_entity_t result = ecs_lookup(world, path);
    test_assert(result != 0);

    return result;
}

/** Move the pointer to the center of the surface, over the tab, and run a frame.
 *
 * @param world The world
 * @return The pick status
 */
static const PickStatus* bare_move(
    ecs_world_t *world)
{
    box_input_mouse_move(world, 640, 360, false);
    ecs_progress(world, 0);

    return ecs_singleton_get(world, PickStatus);
}

/** Press the left button at the center of the surface and run frames while it is held.
 *
 * @param world The world
 * @return The target of the press
 */
static ecs_entity_t bare_press(
    ecs_world_t *world)
{
    float pos[2] = {640, 360};
    box_input_mouse_click(world, KeyMouseLeft, pos, false, 4);
    ecs_progress(world, 0);
    ecs_progress(world, 0);

    return ecs_singleton_get(world, PickStatus)->pressed;
}

void PickBare_bare_root_passes_to_lower_root(void) {
    // ui/picking
    ecs_world_t *world = bare_world(BARE_HUD(""));
    ecs_entity_t tab = bare_e(world, "hud.main.tab");

    const PickStatus *status = bare_move(world);
    test_uint(status->hit, tab);
    test_uint(status->target, tab);
    test_uint(status->handler, tab);
    test_uint(bare_press(world), tab);

    ecs_fini(world);
}

void PickBare_rect_root_blocks(void) {
    // ui/picking
    ecs_world_t *world = bare_world(BARE_HUD("    Rect: {1, 1}\n"));
    ecs_entity_t top = bare_e(world, "hud.top");

    /* A background is drawn over the tab and keeps the pointer from it */
    const PickStatus *status = bare_move(world);
    test_uint(status->hit, top);
    test_uint(status->handler, 0);
    test_uint(bare_press(world), 0);

    ecs_fini(world);
}

void PickBare_input_blocker_root_blocks(void) {
    // ui/picking
    ecs_world_t *world = bare_world(BARE_HUD("    InputBlocker\n"));
    ecs_entity_t top = bare_e(world, "hud.top");

    const PickStatus *status = bare_move(world);
    test_uint(status->hit, top);
    test_uint(status->target, top);
    test_uint(status->handler, top);
    test_assert(bare_press(world) != bare_e(world, "hud.main.tab"));

    ecs_fini(world);
}

void PickBare_bare_child_delivers_to_parent(void) {
    // ui/picking
    ecs_world_t *world = bare_world(BARE_HUD(
        "    button {\n"
        "      Position: {0, 0, 1}\n"
        "      Layout: {direction: Row}\n"
        "      LayoutItem: {width: {mode: Fixed, value: 1280}, height: {mode: Fixed, value: 720}}\n"
        "      Rect: {1, 1}\n"
        "      Interactive\n"
        "      row {\n"
        "        LayoutItem: {width: {mode: Fixed, value: 1280}, height: {mode: Fixed, value: 720}}\n"
        "      }\n"
        "    }\n"));
    ecs_entity_t button = bare_e(world, "hud.top.button");

    /* The bare row is inside the button, which takes the pointer by its own area */
    const PickStatus *status = bare_move(world);
    test_assert(status->hit == button || status->hit == bare_e(world, "hud.top.button.row"));
    test_uint(status->handler, button);
    ecs_entity_t target = status->target;
    test_assert(target != 0);
    test_uint(bare_press(world), target);

    ecs_fini(world);
}

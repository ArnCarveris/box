/**
 * @file PickRange.c
 * @brief PickRange limits how far from the camera an element and the elements under it are hit:
 * a hit further away is no hit and the ray goes on to what is behind it.
 */

#include <box.h>
#include <bake_test.h>
#include "picking.h"

/** A world-space panel 1 m up the -z axis with a tab in its middle, in front of a wall at z -4,
 * and a camera at the origin looking along -z. The arguments are the content of the panel and
 * of the tab. */
#define RANGE_SCENE(panel, tab) \
    "using box.transform\n" \
    "using box.geometry\n" \
    "using box.camera\n" \
    "using box.view\n" \
    "using box.picking\n" \
    "using box.layout\n" \
    "using box.surface\n" \
    "window { Surface: {width: 1280, height: 720} }\n" \
    "camera {\n" \
    "  Position: {0, 0, 0}\n" \
    "  Camera: {}\n" \
    "}\n" \
    "view {\n" \
    "  View: {camera: camera}\n" \
    "}\n" \
    "wall {\n" \
    "  Position: {0, 0, -4}\n" \
    "  Box: {4, 4, 0.2}\n" \
    "  Interactive\n" \
    "}\n" \
    "panel {\n" \
    "  Position: {0, 0, -1}\n" \
    "  Scale: {0.01, 0.01, 0.01}\n" \
    "  Layout: {direction: Column, h: Center, v: Middle}\n" \
    "  Item: {width: {mode: Fixed, value: 60}, height: {mode: Fixed, value: 40}}\n" \
    "  Rect: {1, 1}\n" \
    panel \
    "  tab {\n" \
    "    Item: {width: {mode: Fixed, value: 20}, height: {mode: Fixed, value: 10}}\n" \
    "    Rect: {1, 1}\n" \
    "    Interactive\n" \
    tab \
    "  }\n" \
    "}\n"

/** Create a world with a scene, move the cursor to the middle of the view and run frames.
 *
 * @param scene Script code
 * @return The world
 */
static ecs_world_t* range_world(
    const char *scene)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script(world, { .code = scene }) != 0);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    box_input_mouse_move(world, 0.5f, 0.5f, true);
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
static ecs_entity_t range_e(
    ecs_world_t *world,
    const char *path)
{
    ecs_entity_t result = ecs_lookup(world, path);
    test_assert(result != 0);

    return result;
}

/** Get the entity the cursor hits.
 *
 * @param world The world
 * @return PickStatus.hit
 */
static ecs_entity_t range_hit(
    ecs_world_t *world)
{
    return ecs_singleton_get(world, PickStatus)->hit;
}

void PickRange_within_range(void) {
    // ui/picking
    ecs_world_t *world = range_world(RANGE_SCENE("  PickRange: {distance: 2}\n", ""));

    test_uint(range_hit(world), range_e(world, "panel.tab"));
    test_uint(ecs_singleton_get(world, PickStatus)->handler, range_e(world, "panel.tab"));

    ecs_fini(world);
}

void PickRange_beyond_range_passes(void) {
    // ui/picking
    ecs_world_t *world = range_world(RANGE_SCENE("  PickRange: {distance: 0.5}\n", ""));

    /* The panel is 1 m away: the ray goes past it to the wall */
    test_uint(range_hit(world), range_e(world, "wall"));
    test_uint(ecs_singleton_get(world, PickStatus)->handler, range_e(world, "wall"));

    ecs_fini(world);
}

void PickRange_child_of_range(void) {
    // ui/picking
    ecs_world_t *world = range_world(RANGE_SCENE("  PickRange: {distance: 0.5}\n", ""));

    /* The camera comes within range: the tab, a child of the panel, is hit again */
    ecs_set(world, range_e(world, "camera"), Position, {0, 0, -0.6f});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_uint(range_hit(world), range_e(world, "panel.tab"));

    ecs_fini(world);
}

void PickRange_nearest_range_counts(void) {
    // ui/picking
    ecs_world_t *world = range_world(RANGE_SCENE(
        "  PickRange: {distance: 0.5}\n",
        "    PickRange: {distance: 2}\n"));

    /* The range of the tab counts for the tab, the one of the panel for the panel */
    test_uint(range_hit(world), range_e(world, "panel.tab"));

    ecs_set(world, range_e(world, "camera"), Position, {0.2f, 0, 0});
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_uint(range_hit(world), range_e(world, "wall"));

    ecs_fini(world);
}

void PickRange_zero_has_no_limit(void) {
    // ui/picking
    ecs_world_t *world = range_world(RANGE_SCENE("  PickRange: {distance: 0}\n", ""));

    test_uint(range_hit(world), range_e(world, "panel.tab"));

    ecs_fini(world);
}

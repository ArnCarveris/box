/**
 * @file ClipBorder.c
 * @brief ClipChildren clips the children of an element inside its Border, with the inner radius.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"

/** A 100 by 100 ClipChildren view at the origin, spanning -50..50 on x and y, with a red border
 * and a child 140 by 40 that is wider than it. The argument is the Border of the view. */
#define CLIP_BORDER_VIEW(border) \
    "using box.layout\n" \
    "using box.geometry\n" \
    "using box.material\n" \
    "using box.transform\n" \
    "view {\n" \
    "  Position: {0, 0, 0}\n" \
    "  Layout: {}\n" \
    "  Item: {width: {mode: Fixed, value: 100}, height: {mode: Fixed, value: 100}}\n" \
    "  Rect: {1, 1}\n" \
    "  Rgb: {40, 40, 40}\n" \
    "  Border: " border "\n" \
    "  ClipChildren\n" \
    "  child {\n" \
    "    Item: {width: {mode: Fixed, value: 140}, height: {mode: Fixed, value: 40}}\n" \
    "    Rect: {1, 1}\n" \
    "    Rgb: {0, 0, 255}\n" \
    "  }\n" \
    "}\n"

/** Create a world that runs a script and lays it out.
 *
 * @param code The script
 * @return The world
 */
static ecs_world_t* clip_border_world(
    const char *code)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    test_assert(ecs_script_run(world, "clip", code, NULL) == 0);
    int32_t i;
    for (i = 0; i < 4; i ++) {
        ecs_progress(world, 0);
    }

    return world;
}

/** Check whether the clip of the view keeps a world point of its child.
 *
 * @param world The world
 * @param x World x of the point
 * @param y World y of the point
 * @return Whether the view does not clip the point away
 */
static bool clip_border_keeps(
    ecs_world_t *world,
    float x,
    float y)
{
    ecs_entity_t e = ecs_lookup(world, "view.child");
    test_assert(e != 0);
    float point[3] = { x, y, 0 };

    return box_render_clip_contains(world, e, point);
}

/** Get the clip area of the view.
 *
 * @param world The world
 * @param half Receives the half extents of the area
 * @return The corner radius of the area
 */
static float clip_border_area(
    ecs_world_t *world,
    float half[2])
{
    ecs_entity_t e = ecs_lookup(world, "view");
    test_assert(e != 0);
    test_flt(ecs_get(world, e, LayoutBounds)->width, 100);
    test_flt(ecs_get(world, e, LayoutBounds)->height, 100);

    return box_render_clip_area(world, e, half);
}

void ClipBorder_point_on_border_clipped(void) {
    ecs_world_t *world = clip_border_world(
        CLIP_BORDER_VIEW("{width: 4, radius: 8, color: {255, 0, 0, 255}}"));

    /* The border covers -50..-46 on the left: points on it are outside the clip */
    test_bool(clip_border_keeps(world, -48, 0), false);
    test_bool(clip_border_keeps(world, 48, 0), false);
    test_bool(clip_border_keeps(world, 0, -49), false);
    test_bool(clip_border_keeps(world, 0, 46.5f), false);

    /* Just inside the inner edge of the border the child is kept */
    test_bool(clip_border_keeps(world, -45.5f, 0), true);
    test_bool(clip_border_keeps(world, 45.5f, 0), true);
    test_bool(clip_border_keeps(world, 0, -45.5f), true);
    test_bool(clip_border_keeps(world, 0, 0), true);

    /* The area is the inside of the border, its radius the outer radius less the width */
    float half[2];
    test_flt(clip_border_area(world, half), 4);
    test_flt(half[0], 46);
    test_flt(half[1], 46);

    /* The entity's own bounds stay its size: the child is not hidden */
    test_bool(box_render_clip_hides(world, ecs_lookup(world, "view.child")), false);

    ecs_fini(world);
}

void ClipBorder_inner_radius(void) {
    ecs_world_t *world = clip_border_world(
        CLIP_BORDER_VIEW("{width: 4, radius: 8, color: {255, 0, 0, 255}}"));

    /* The inner corner is centered on (42, 42) with radius 4. A point 3.5 from the center on
     * both axes would be outside an inner radius of 8 (centered on 38, 38), inside one of 4 */
    test_bool(clip_border_keeps(world, 44.5f, 44.5f), true);
    test_bool(clip_border_keeps(world, -44.5f, 44.5f), true);
    test_bool(clip_border_keeps(world, -44.5f, -44.5f), true);
    test_bool(clip_border_keeps(world, 44.5f, -44.5f), true);

    /* Beyond the inner radius in the corner it is clipped, though inside the square area */
    test_bool(clip_border_keeps(world, 45.5f, 45.5f), false);
    test_bool(clip_border_keeps(world, -45.5f, -45.5f), false);

    ecs_fini(world);
}

void ClipBorder_border_wider_than_radius(void) {
    ecs_world_t *world = clip_border_world(
        CLIP_BORDER_VIEW("{width: 10, radius: 8, color: {255, 0, 0, 255}}"));

    /* The inner edge is at 40 with square corners */
    float half[2];
    test_flt(clip_border_area(world, half), 0);
    test_flt(half[0], 40);
    test_flt(half[1], 40);
    test_bool(clip_border_keeps(world, 39.5f, 39.5f), true);
    test_bool(clip_border_keeps(world, -39.5f, -39.5f), true);
    test_bool(clip_border_keeps(world, 40.5f, 0), false);
    test_bool(clip_border_keeps(world, 0, -40.5f), false);
    test_bool(clip_border_keeps(world, 45, 0), false);

    ecs_fini(world);
}

void ClipBorder_border_wider_than_half(void) {
    ecs_world_t *world = clip_border_world(
        CLIP_BORDER_VIEW("{width: 60, radius: 8, color: {255, 0, 0, 255}}"));

    /* The border fills the element: the area is empty and hides everything below it */
    float half[2];
    test_flt(clip_border_area(world, half), 0);
    test_flt(half[0], 0);
    test_flt(half[1], 0);
    test_bool(clip_border_keeps(world, 0, 0), false);
    test_bool(clip_border_keeps(world, 10, 10), false);
    test_bool(box_render_clip_hides(world, ecs_lookup(world, "view.child")), true);

    ecs_fini(world);
}

void ClipBorder_no_border_width(void) {
    ecs_world_t *world = clip_border_world(
        CLIP_BORDER_VIEW("{width: 0, radius: 8, color: {255, 0, 0, 255}}"));

    /* Without a border width the area is the element, rounded by the radius */
    float half[2];
    test_flt(clip_border_area(world, half), 8);
    test_flt(half[0], 50);
    test_flt(half[1], 50);
    test_bool(clip_border_keeps(world, -48, 0), true);
    test_bool(clip_border_keeps(world, -50.5f, 0), false);

    ecs_fini(world);
}

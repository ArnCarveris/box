/**
 * @file PointerShape.c
 * @brief The pointer cursor shape box.input tracks and tells a backend when it changes.
 */

#include <box.h>
#include <bake_test.h>
#include "input.h"

/** Times the fake backend was told a shape */
static int32_t shape_calls = 0;

/** Shape the fake backend was last told */
static PointerCursor shape_last = CursorDefault;

/** Record a shape, the cursor callback of the fake backend.
 *
 * @param world The world
 * @param shape The shape
 * @param ctx Unused
 */
static void ShapeRecord(
    ecs_world_t *world,
    PointerCursor shape,
    void *ctx)
{
    (void)world;
    (void)ctx;
    shape_calls ++;
    shape_last = shape;
}

/** Reset the fake backend */
static void shape_reset(void)
{
    shape_calls = 0;
    shape_last = CursorDefault;
}

void PointerShape_default_shape(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    test_int(box_input_cursor_get(world), CursorDefault);
    test_assert(ecs_lookup(world, "box.input.PointerCursor") == ecs_id(PointerCursor));
    test_assert(ecs_lookup(world, "box.input.PointerCursor.CursorNotAllowed") != 0);
    ecs_progress(world, 0);
    test_int(box_input_cursor_get(world), CursorDefault);

    ecs_fini(world);
}

void PointerShape_headless_tracks(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    box_input_cursor_shape(world, CursorText);
    test_int(box_input_cursor_get(world), CursorText);
    box_input_cursor_shape(world, CursorMove);
    test_int(box_input_cursor_get(world), CursorMove);

    ecs_fini(world);
}

void PointerShape_platform_on_change(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    shape_reset();
    box_input_platform_set(world, &(box_input_platform_t){ .cursor = ShapeRecord });
    test_int(shape_calls, 0);
    box_input_cursor_shape(world, CursorDefault);
    test_int(shape_calls, 0);

    box_input_cursor_shape(world, CursorPointer);
    test_int(shape_calls, 1);
    test_int(shape_last, CursorPointer);
    box_input_cursor_shape(world, CursorPointer);
    test_int(shape_calls, 1);

    box_input_cursor_shape(world, CursorResizeH);
    test_int(shape_calls, 2);
    test_int(shape_last, CursorResizeH);
    box_input_cursor_shape(world, CursorDefault);
    test_int(shape_calls, 3);
    test_int(shape_last, CursorDefault);
    ecs_progress(world, 0);
    test_int(shape_calls, 3);

    ecs_fini(world);
}

void PointerShape_invalid_shape(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    shape_reset();
    box_input_platform_set(world, &(box_input_platform_t){ .cursor = ShapeRecord });
    box_input_cursor_shape(world, CursorText);
    box_input_cursor_shape(world, (PointerCursor)42);
    test_int(box_input_cursor_get(world), CursorDefault);
    test_int(shape_calls, 2);
    test_int(shape_last, CursorDefault);

    ecs_fini(world);
}

void PointerShape_new_backend_told(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    shape_reset();
    box_input_cursor_shape(world, CursorCrosshair);
    box_input_platform_set(world, &(box_input_platform_t){ .cursor = ShapeRecord });
    test_int(shape_calls, 1);
    test_int(shape_last, CursorCrosshair);

    box_input_platform_set(world, &(box_input_platform_t){ .cursor = ShapeRecord });
    test_int(shape_calls, 2);
    test_int(shape_last, CursorCrosshair);

    ecs_fini(world);
}

void PointerShape_new_backend_default_not_told(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    shape_reset();
    box_input_platform_set(world, &(box_input_platform_t){ .cursor = ShapeRecord });
    test_int(shape_calls, 0);

    box_input_platform_set(world, NULL);
    box_input_cursor_shape(world, CursorNotAllowed);
    test_int(shape_calls, 0);
    test_int(box_input_cursor_get(world), CursorNotAllowed);

    ecs_fini(world);
}

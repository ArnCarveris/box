/**
 * @file Cursor.c
 * @brief The Cursor of the element under the pointer sets the pointer cursor shape.
 */

#include <box.h>
#include <bake_test.h>

/** Modules and a screen UI scene */
#define CURSOR_PRELUDE \
    "using box.transform\n" \
    "using box.geometry\n" \
    "using box.camera\n" \
    "using box.view\n" \
    "using box.picking\n" \
    "using box.input\n" \
    "hud {\n" \
    "  Scene: {}\n" \
    "  camera {\n" \
    "    Position: {0, 0, 100}\n" \
    "    Camera: {ortho: true, ortho_scale: 1}\n" \
    "  }\n" \
    "}\n" \
    "hud_view {\n" \
    "  View: {camera: hud.camera}\n" \
    "}\n"

/** A button with a hand at 440, 360 and a field with an I-beam at 840, 360 */
#define CURSOR_TWO \
    "hud {\n" \
    "  button { Position: {-200, 0, 0}\n" \
    "    Rect: {120, 32}\n" \
    "    Interactive\n" \
    "    Cursor: {shape: CursorPointer}\n" \
    "  }\n" \
    "  field { Position: {200, 0, 0}\n" \
    "    Rect: {120, 32}\n" \
    "    Interactive\n" \
    "    Cursor: {shape: CursorText}\n" \
    "  }\n" \
    "}\n"

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

/** Times the fake backend was asked to capture or release the cursor */
static int32_t capture_calls = 0;

/** Capture the cursor at once, the capture callback of the fake backend.
 *
 * @param world The world
 * @param surface The surface
 * @param captured Whether the cursor is captured
 * @param ctx Unused
 * @return Whether the cursor is captured
 */
static bool CaptureRecord(
    ecs_world_t *world,
    ecs_entity_t surface,
    bool captured,
    void *ctx)
{
    (void)world;
    (void)surface;
    (void)ctx;
    capture_calls ++;

    return captured;
}

/** Create a world with the prelude and a scene, and run two frames.
 *
 * @param scene Script code after the prelude
 * @return The world
 */
static ecs_world_t* cursor_world(
    const char *scene)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    char *code = flecs_asprintf("%s%s", CURSOR_PRELUDE, scene);
    test_assert(ecs_script(world, { .code = code }) != 0);
    ecs_os_free(code);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    shape_calls = 0;
    shape_last = CursorDefault;
    capture_calls = 0;

    return world;
}

/** Move the pointer and run a frame.
 *
 * @param world The world
 * @param x Cursor x in window points
 * @param y Cursor y in window points
 * @return The shape shown after the frame
 */
static PointerCursor cursor_at(
    ecs_world_t *world,
    float x,
    float y)
{
    box_input_mouse_move(world, x, y, false);
    ecs_progress(world, 0);
    test_int(ecs_singleton_get(world, PickStatus)->shape, box_input_cursor_get(world));

    return box_input_cursor_get(world);
}

/** Press or release the left button and run a frame.
 *
 * @param world The world
 * @param down Whether the button goes down
 * @return The shape shown after the frame
 */
static PointerCursor cursor_button(
    ecs_world_t *world,
    bool down)
{
    box_input_key(world, KeyMouseLeft, down);
    ecs_progress(world, 0);
    test_int(ecs_singleton_get(world, PickStatus)->shape, box_input_cursor_get(world));

    return box_input_cursor_get(world);
}

void Cursor_script_component(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = cursor_world(CURSOR_TWO);
    test_assert(ecs_lookup(world, "box.picking.Cursor") == ecs_id(Cursor));
    const Cursor *c = ecs_get(world, ecs_lookup(world, "hud.button"), Cursor);
    test_assert(c != NULL);
    test_int(c->shape, CursorPointer);
    c = ecs_get(world, ecs_lookup(world, "hud.field"), Cursor);
    test_assert(c != NULL);
    test_int(c->shape, CursorText);

    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, Cursor);
    test_int(ecs_get(world, e, Cursor)->shape, CursorDefault);
    test_int(ecs_singleton_get(world, PickStatus)->shape, CursorDefault);

    ecs_fini(world);
}

void Cursor_follows_element(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = cursor_world(CURSOR_TWO);
    test_int(cursor_at(world, 440, 360), CursorPointer);
    test_uint(ecs_singleton_get(world, PickStatus)->target, ecs_lookup(world, "hud.button"));
    test_int(cursor_at(world, 450, 355), CursorPointer);
    test_int(cursor_at(world, 840, 360), CursorText);
    test_int(cursor_at(world, 440, 360), CursorPointer);

    ecs_fini(world);
}

void Cursor_bubbles_from_child(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = cursor_world(
        "hud {\n"
        "  slider { Position: {0, 0, 0}\n"
        "    Rect: {300, 40}\n"
        "    Interactive\n"
        "    Cursor: {shape: CursorResizeH}\n"
        "    thumb { Position: {0, 0, 1}\n"
        "      Rect: {20, 30}\n"
        "    }\n"
        "  }\n"
        "}\n");
    test_int(cursor_at(world, 640, 360), CursorResizeH);
    test_uint(ecs_singleton_get(world, PickStatus)->target,
        ecs_lookup(world, "hud.slider.thumb"));
    test_uint(ecs_singleton_get(world, PickStatus)->handler, ecs_lookup(world, "hud.slider"));
    test_int(cursor_at(world, 540, 360), CursorResizeH);
    test_uint(ecs_singleton_get(world, PickStatus)->target, ecs_lookup(world, "hud.slider"));

    ecs_fini(world);
}

void Cursor_nearest_cursor_wins(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = cursor_world(
        "hud {\n"
        "  panel { Position: {0, 0, 0}\n"
        "    Rect: {300, 200}\n"
        "    Interactive\n"
        "    Cursor: {shape: CursorMove}\n"
        "    label { Position: {0, 0, 1}\n"
        "      Rect: {120, 32}\n"
        "      Cursor: {shape: CursorText}\n"
        "    }\n"
        "  }\n"
        "}\n");
    test_int(cursor_at(world, 640, 360), CursorText);
    test_int(cursor_at(world, 520, 300), CursorMove);

    ecs_fini(world);
}

void Cursor_not_above_handler(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = cursor_world(
        "hud {\n"
        "  panel { Position: {0, 0, 0}\n"
        "    Rect: {300, 200}\n"
        "    Interactive\n"
        "    Cursor: {shape: CursorMove}\n"
        "    button { Position: {0, 0, 1}\n"
        "      Rect: {120, 32}\n"
        "      Interactive\n"
        "    }\n"
        "  }\n"
        "}\n");
    test_int(cursor_at(world, 520, 300), CursorMove);
    test_int(cursor_at(world, 640, 360), CursorDefault);
    test_uint(ecs_singleton_get(world, PickStatus)->handler,
        ecs_lookup(world, "hud.panel.button"));

    ecs_fini(world);
}

void Cursor_default_over_nothing(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = cursor_world(CURSOR_TWO);
    test_int(cursor_at(world, 440, 360), CursorPointer);
    test_int(cursor_at(world, 100, 100), CursorDefault);
    test_uint(ecs_singleton_get(world, PickStatus)->target, 0);
    test_int(cursor_at(world, 840, 360), CursorText);
    test_int(cursor_at(world, 640, 600), CursorDefault);

    ecs_fini(world);
}

void Cursor_default_without_cursor(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = cursor_world(
        "hud {\n"
        "  plain { Position: {0, 0, 0}\n"
        "    Rect: {120, 32}\n"
        "    Interactive\n"
        "  }\n"
        "  label { Position: {200, 0, 0}\n"
        "    Rect: {120, 32}\n"
        "    Cursor: {shape: CursorText}\n"
        "  }\n"
        "}\n");
    test_int(cursor_at(world, 640, 360), CursorDefault);
    test_uint(ecs_singleton_get(world, PickStatus)->target, ecs_lookup(world, "hud.plain"));
    test_int(cursor_at(world, 840, 360), CursorDefault);

    ecs_fini(world);
}

void Cursor_stays_on_capture(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = cursor_world(CURSOR_TWO);
    test_int(cursor_at(world, 440, 360), CursorPointer);
    test_int(cursor_button(world, true), CursorPointer);
    test_uint(ecs_singleton_get(world, PickStatus)->captured, ecs_lookup(world, "hud.button"));
    test_int(cursor_at(world, 840, 360), CursorPointer);
    test_int(cursor_at(world, 100, 100), CursorPointer);
    ecs_progress(world, 0);
    test_int(box_input_cursor_get(world), CursorPointer);
    test_int(cursor_at(world, 840, 360), CursorPointer);

    test_int(cursor_button(world, false), CursorPointer);
    test_uint(ecs_singleton_get(world, PickStatus)->captured, 0);
    ecs_progress(world, 0);
    test_int(box_input_cursor_get(world), CursorText);
    test_uint(ecs_singleton_get(world, PickStatus)->target, ecs_lookup(world, "hud.field"));

    ecs_fini(world);
}

void Cursor_returns_after_capture(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = cursor_world(CURSOR_TWO);
    test_int(cursor_at(world, 840, 360), CursorText);
    test_int(cursor_button(world, true), CursorText);
    test_int(cursor_at(world, 100, 100), CursorText);
    test_int(cursor_button(world, false), CursorText);
    ecs_progress(world, 0);
    test_int(box_input_cursor_get(world), CursorDefault);

    test_int(cursor_button(world, true), CursorDefault);
    test_uint(ecs_singleton_get(world, PickStatus)->captured, 0);
    test_int(cursor_at(world, 440, 360), CursorPointer);
    test_int(cursor_button(world, false), CursorPointer);

    ecs_fini(world);
}

void Cursor_component_changed(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = cursor_world(CURSOR_TWO);
    ecs_entity_t button = ecs_lookup(world, "hud.button");
    test_int(cursor_at(world, 440, 360), CursorPointer);
    ecs_set(world, button, Cursor, { .shape = CursorNotAllowed });
    ecs_progress(world, 0);
    test_int(box_input_cursor_get(world), CursorNotAllowed);
    test_int(ecs_singleton_get(world, PickStatus)->shape, CursorNotAllowed);

    ecs_fini(world);
}

void Cursor_component_removed(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = cursor_world(CURSOR_TWO);
    ecs_entity_t field = ecs_lookup(world, "hud.field");
    test_int(cursor_at(world, 840, 360), CursorText);
    ecs_remove(world, field, Cursor);
    ecs_progress(world, 0);
    test_int(box_input_cursor_get(world), CursorDefault);

    ecs_add(world, field, Cursor);
    ecs_set(world, field, Cursor, { .shape = CursorCrosshair });
    ecs_progress(world, 0);
    test_int(box_input_cursor_get(world), CursorCrosshair);

    ecs_delete(world, field);
    ecs_progress(world, 0);
    test_int(box_input_cursor_get(world), CursorDefault);

    ecs_fini(world);
}

void Cursor_platform_called_on_changes(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = cursor_world(CURSOR_TWO);
    box_input_platform_set(world, &(box_input_platform_t){ .cursor = ShapeRecord });
    ecs_progress(world, 0);
    test_int(shape_calls, 0);

    cursor_at(world, 440, 360);
    test_int(shape_calls, 1);
    test_int(shape_last, CursorPointer);
    cursor_at(world, 450, 365);
    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_int(shape_calls, 1);

    cursor_at(world, 840, 360);
    test_int(shape_calls, 2);
    test_int(shape_last, CursorText);
    cursor_at(world, 100, 100);
    test_int(shape_calls, 3);
    test_int(shape_last, CursorDefault);
    cursor_at(world, 200, 600);
    test_int(shape_calls, 3);

    box_input_mouse_move(world, 440, 360, false);
    box_input_mouse_move(world, 100, 100, false);
    ecs_progress(world, 0);
    test_int(shape_calls, 3);

    box_input_mouse_move(world, 840, 360, false);
    box_input_key(world, KeyMouseLeft, true);
    box_input_mouse_move(world, 440, 360, false);
    box_input_key(world, KeyMouseLeft, false);
    box_input_mouse_move(world, 100, 100, false);
    ecs_progress(world, 0);
    test_int(shape_calls, 3);
    test_int(box_input_cursor_get(world), CursorDefault);

    ecs_fini(world);
}

void Cursor_captured_keeps_shape(void) {
    // widgets/clipboard_cursor
    ecs_world_t *world = cursor_world(CURSOR_TWO);
    box_input_platform_set(world, &(box_input_platform_t){
        .capture = CaptureRecord, .cursor = ShapeRecord });
    test_int(cursor_at(world, 440, 360), CursorPointer);
    test_int(shape_calls, 1);

    ecs_singleton_get_mut(world, Input)->capture = true;
    ecs_singleton_modified(world, Input);
    ecs_progress(world, 0);
    test_bool(ecs_singleton_get(world, Input)->mouse.captured, true);
    test_int(capture_calls, 1);
    test_int(shape_calls, 1);
    test_int(box_input_cursor_get(world), CursorPointer);

    ecs_singleton_get_mut(world, Input)->capture = false;
    ecs_singleton_modified(world, Input);
    ecs_progress(world, 0);
    test_bool(ecs_singleton_get(world, Input)->mouse.captured, false);
    test_int(capture_calls, 2);
    test_int(shape_calls, 1);
    test_int(shape_last, CursorPointer);
    test_int(cursor_at(world, 840, 360), CursorText);
    test_int(shape_calls, 2);

    ecs_fini(world);
}

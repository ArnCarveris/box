/**
 * @file PortalScreen.c
 * @brief The access control screen of test/assets/portal/doors_screen.flecs on two doors: it
 * lists them, locks and unlocks them through VisPortal.locked with clicks aimed like a walker's
 * crosshair at the middle of the view, and takes no clicks from beyond its range.
 */

#include "portal_test.h"
#include <string.h>

/** A window, two doors into a room behind them, door a asked to be open, the screen 1.35 m up facing +z and a camera looking at it
 * along -z from 2 m. The screen is 1.92 by 0.72 m, 960 by 360 virtual pixels. */
#define SCREEN_SCENE \
    "using box.transform\n" \
    "using box.geometry\n" \
    "using box.camera\n" \
    "using box.view\n" \
    "using box.portal\n" \
    "using box.surface\n" \
    "window { Surface: {width: 1280, height: 720} }\n" \
    "back {\n" \
    "  VisArea: {shape: [{-15, -20}, {15, -20}, {15, -10}, {-15, -10}], y: 0, height: 3}\n" \
    "}\n" \
    "doors {\n" \
    "  a {\n" \
    "    Position: {-10, 1.2, -10}\n" \
    "    VisPortal: {kind: Door, size: {2, 2.4}, normal: {0, 0, 1}}\n" \
    "    PortalDoor: {open: true}\n" \
    "  }\n" \
    "  b {\n" \
    "    Position: {10, 1.2, -10}\n" \
    "    VisPortal: {kind: Door, size: {2, 2.4}, normal: {0, 0, 1}}\n" \
    "    PortalDoor: {}\n" \
    "  }\n" \
    "}\n" \
    "console {\n" \
    "  PortalDoorsScreen screen(\n" \
    "    width: 1.92, height: 0.72, doors: [doors.a, doors.b], names: [\"DOOR A\", \"DOOR B\"])\n" \
    "  {\n" \
    "    Position: {0, 1.35, 0}\n" \
    "  }\n" \
    "}\n" \
    "camera {\n" \
    "  Position: {0, 1.35, 2}\n" \
    "  Camera: {}\n" \
    "}\n" \
    "view {\n" \
    "  View: {camera: camera}\n" \
    "}\n"

/** Path of the row of door a, in the first column */
#define SCREEN_ROW_A "console.screen.list.first.door_0"

/** Path of the row of door b, in the second column */
#define SCREEN_ROW_B "console.screen.list.second.door_1"

/** Run a script file of the test assets.
 *
 * @param world The world
 * @param file Path of the file from the repository root
 * @param name Name of the entity that holds the script, NULL for none
 */
static void screen_load(
    ecs_world_t *world,
    const char *file,
    const char *name)
{
    char *path = box_app_locate(file);
    test_assert(path != NULL);

    ecs_entity_t holder = 0;
    if (name) {
        holder = ecs_entity(world, { .name = name });
    }

    test_assert(ecs_script(world, { .entity = holder, .filename = path }) != 0);
    ecs_os_free(path);
}

/** Run frames of 0.1 s.
 *
 * @param world The world
 * @param frames Number of frames
 */
static void screen_run(
    ecs_world_t *world,
    int32_t frames)
{
    int32_t i;

    for (i = 0; i < frames; i ++) {
        ecs_progress(world, 0.1f);
    }
}

/** Create the world of SCREEN_SCENE with the widgets, the door and the screen templates, and
 * let the templates, the layout and the door settle.
 *
 * @return The world
 */
static ecs_world_t* screen_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    screen_load(world, "test/assets/ui/widgets.flecs", "box.ui.widgets");
    screen_load(world, "test/assets/portal/door.flecs", NULL);
    screen_load(world, "test/assets/portal/screen.flecs", NULL);
    screen_load(world, "test/assets/portal/doors_screen.flecs", NULL);
    test_assert(ecs_script(world, { .code = SCREEN_SCENE }) != 0);
    screen_run(world, 20);

    return world;
}

/** Look up an entity that must exist.
 *
 * @param world The world
 * @param path Its path
 * @return The entity
 */
static ecs_entity_t screen_e(
    ecs_world_t *world,
    const char *path)
{
    ecs_entity_t result = ecs_lookup(world, path);
    test_assert(result != 0);

    return result;
}

/** Get the text of an element.
 *
 * @param world The world
 * @param path Path of the element with the Text
 * @return The text
 */
static const char* screen_text(
    ecs_world_t *world,
    const char *path)
{
    const Text *text = ecs_get(world, screen_e(world, path), Text);
    test_assert(text != NULL);
    test_assert(text->value != NULL);

    return text->value;
}

/** Get whether the portal of a door is locked.
 *
 * @param world The world
 * @param path Path of the door
 * @return VisPortal.locked
 */
static bool screen_locked(
    ecs_world_t *world,
    const char *path)
{
    return ecs_get(world, screen_e(world, path), VisPortal)->locked;
}

/** Aim the camera along -z at the middle of an element from a distance, as a walker aims its
 * crosshair, and click there: the middle of the view.
 *
 * @param world The world
 * @param path Path of the element
 * @param distance Distance of the camera from the plane of the screen
 */
static void screen_click(
    ecs_world_t *world,
    const char *path,
    float distance)
{
    WorldTransform buf;
    const WorldTransform *t = box_transform_world(world, screen_e(world, path), &buf);
    test_assert(t != NULL);

    ecs_set(world, screen_e(world, "camera"), Position, {
        t->m[3][0], t->m[3][1], t->m[3][2] + distance
    });
    screen_run(world, 2);

    float middle[2] = {0.5f, 0.5f};
    box_input_mouse_click(world, KeyMouseLeft, middle, true, 2);
    screen_run(world, 6);
}

void PortalScreen_lists_doors(void) {
    ecs_world_t *world = screen_world();

    test_str(screen_text(world, SCREEN_ROW_A ".about.title"), "DOOR A");
    test_str(screen_text(world, SCREEN_ROW_B ".about.title"), "DOOR B");
    test_str(screen_text(world, SCREEN_ROW_A ".about.status.word"), "OPEN");
    test_str(screen_text(world, SCREEN_ROW_B ".about.status.word"), "CLOSED");
    test_str(screen_text(world, SCREEN_ROW_A ".lock.caption"), "LOCK");
    test_str(screen_text(world, "console.screen.header.band.caption"), "ACCESS CONTROL");
    test_str(screen_text(world, "console.screen.header.band.state"), "0/2 LOCKED");

    ecs_fini(world);
}

void PortalScreen_lock_shuts_door(void) {
    ecs_world_t *world = screen_world();
    ecs_entity_t a = screen_e(world, "doors.a");
    test_assert(ecs_get(world, a, VisPortal)->open > 0.98f);

    screen_click(world, SCREEN_ROW_A ".lock", 2);
    test_bool(screen_locked(world, "doors.a"), true);
    test_bool(screen_locked(world, "doors.b"), false);

    /* The door is asked to be open, but a locked door shuts and stays shut */
    screen_run(world, 20);
    test_assert(ecs_get(world, a, VisPortal)->open < 0.02f);
    test_str(screen_text(world, SCREEN_ROW_A ".about.status.word"), "LOCKED");
    test_str(screen_text(world, SCREEN_ROW_A ".lock.caption"), "UNLOCK");
    test_str(screen_text(world, "console.screen.header.band.state"), "1/2 LOCKED");

    ecs_fini(world);
}

void PortalScreen_unlock_opens_again(void) {
    ecs_world_t *world = screen_world();
    ecs_entity_t a = screen_e(world, "doors.a");

    screen_click(world, SCREEN_ROW_A ".lock", 2);
    screen_run(world, 20);
    test_assert(ecs_get(world, a, VisPortal)->open < 0.02f);

    screen_click(world, SCREEN_ROW_A ".lock", 2);
    test_bool(screen_locked(world, "doors.a"), false);
    screen_run(world, 20);
    test_assert(ecs_get(world, a, VisPortal)->open > 0.98f);
    test_str(screen_text(world, SCREEN_ROW_A ".lock.caption"), "LOCK");

    ecs_fini(world);
}

void PortalScreen_lockdown_and_release(void) {
    ecs_world_t *world = screen_world();

    screen_click(world, "console.screen.footer.lockdown", 2);
    test_bool(screen_locked(world, "doors.a"), true);
    test_bool(screen_locked(world, "doors.b"), true);
    screen_run(world, 2);
    test_str(screen_text(world, "console.screen.header.band.state"), "2/2 LOCKED");

    screen_click(world, "console.screen.footer.release", 2);
    test_bool(screen_locked(world, "doors.a"), false);
    test_bool(screen_locked(world, "doors.b"), false);

    ecs_fini(world);
}

void PortalScreen_out_of_range(void) {
    ecs_world_t *world = screen_world();

    /* Beyond the range of 3 m the click goes past the screen */
    screen_click(world, SCREEN_ROW_A ".lock", 4);
    test_bool(screen_locked(world, "doors.a"), false);

    screen_click(world, SCREEN_ROW_A ".lock", 2.5f);
    test_bool(screen_locked(world, "doors.a"), true);

    ecs_fini(world);
}

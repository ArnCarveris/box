/**
 * @file PortalFacility.c
 * @brief The facility overview screen of test/assets/portal/facility_screen.flecs on two rooms
 * and two doors: it counts the doors open and locked and the areas with power, shows the room
 * the viewer is in, and follows a locked door, a dark area and a viewer that walks on.
 */

#include "screen_test.h"

/** A hall and a vault side by side, areas of 10 by 10 m (the map shows the vault 8 m wide), a door a (asked to be open) and a
 * door b between and beyond them, a camera as the viewer in the hall, and the screen. */
#define FACILITY_SCENE \
    "using box.transform\n" \
    "using box.portal\n" \
    "hall {\n" \
    "  PortalBreaker: {demand: 20}\n" \
    "  VisArea: {shape: [{0, 0}, {10, 0}, {10, 10}, {0, 10}], y: 0, height: 3}\n" \
    "}\n" \
    "vault {\n" \
    "  PortalBreaker: {demand: 30}\n" \
    "  VisArea: {shape: [{10, 0}, {20, 0}, {20, 10}, {10, 10}], y: 0, height: 3}\n" \
    "}\n" \
    "doors {\n" \
    "  a {\n" \
    "    Position: {10, 1.2, 5}\n" \
    "    VisPortal: {kind: Door, size: {2, 2.4}, normal: {1, 0, 0}}\n" \
    "    PortalDoor: {open: true}\n" \
    "  }\n" \
    "  b {\n" \
    "    Position: {20, 1.2, 5}\n" \
    "    VisPortal: {kind: Door, size: {2, 2.4}, normal: {1, 0, 0}}\n" \
    "    PortalDoor: {}\n" \
    "  }\n" \
    "}\n" \
    "viewer { Position: {5, 1.5, 5} }\n" \
    "console {\n" \
    "  PortalFacilityScreen facility(\n" \
    "    levels: [{\"GROUND\", 20, 10, 20, 10}],\n" \
    "    rooms: [{hall, \"HALL\", 0, 0, 0, 10, 10}, {vault, \"VAULT\", 0, 12, 0, 8, 10}],\n" \
    "    doors: [doors.a, doors.b], viewer: viewer)\n" \
    "}\n"

/** Path of the readout of the doors that are open */
#define FACILITY_OPEN "console.facility.body.side.open.amount"

/** Path of the readout of the doors that are locked */
#define FACILITY_LOCKED "console.facility.body.side.locked.amount"

/** Path of the readout of the areas with power */
#define FACILITY_POWER "console.facility.body.side.power.amount"

/** Path of the readout of the room of the viewer */
#define FACILITY_HERE "console.facility.body.side.you.amount"

/** Create the world of FACILITY_SCENE, and let the doors open and the viewer be found.
 *
 * @return The world
 */
static ecs_world_t* facility_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    screen_load(world, "test/assets/ui/widgets.flecs", "box.ui.widgets");
    screen_load(world, "test/assets/portal/door.flecs", NULL);
    screen_load(world, "test/assets/portal/screen.flecs", NULL);
    screen_load(world, "test/assets/portal/power.flecs", NULL);
    screen_load(world, "test/assets/portal/facility_screen.flecs", NULL);
    test_assert(ecs_script(world, { .code = FACILITY_SCENE }) != 0);
    screen_run(world, 20);

    return world;
}

/** Get the width of the border of a room of the map.
 *
 * @param world The world
 * @param room Index of the room
 * @return The width, wider for the room of the viewer
 */
static float facility_border(
    ecs_world_t *world,
    int32_t room)
{
    char path[64];
    snprintf(path, sizeof(path), "console.facility.body.level_0.map.room_%d", room);

    const Border *border = ecs_get(world, screen_e(world, path), Border);
    test_assert(border != NULL);

    return border->width;
}

void PortalFacility_counts_doors_and_areas(void) {
    ecs_world_t *world = facility_world();

    test_str(screen_text(world, "console.facility.header.band.caption"), "FACILITY // OVERVIEW");
    test_str(screen_text(world, "console.facility.header.band.state"),
        "1/2 DOORS OPEN " MIDDLE_DOT " 2/2 AREAS POWERED");
    test_str(screen_text(world, FACILITY_OPEN), "1 of 2");
    test_str(screen_text(world, FACILITY_LOCKED), "0");
    test_str(screen_text(world, FACILITY_POWER), "2 of 2");

    /* A room, its caption, and a dot for each door */
    test_str(screen_text(world, "console.facility.body.level_0.caption"), "GROUND");
    test_str(screen_text(world, "console.facility.body.level_0.map.room_1.caption"), "VAULT");
    test_assert(ecs_lookup(world, "console.facility.body.level_0.map.door_0") != 0);
    test_assert(ecs_lookup(world, "console.facility.body.level_0.map.door_1") != 0);

    ecs_fini(world);
}

void PortalFacility_follows_doors_and_power(void) {
    ecs_world_t *world = facility_world();

    /* A locked door shuts, the dark vault is not powered */
    VisPortal *portal = ecs_get_mut(world, screen_e(world, "doors.a"), VisPortal);
    test_assert(portal != NULL);
    portal->locked = true;
    ecs_modified(world, screen_e(world, "doors.a"), VisPortal);
    screen_set(world, "vault", "PortalBreaker", "dark", true);
    screen_run(world, 20);

    test_str(screen_text(world, FACILITY_OPEN), "0 of 2");
    test_str(screen_text(world, FACILITY_LOCKED), "1");
    test_str(screen_text(world, FACILITY_POWER), "1 of 2");
    test_str(screen_text(world, "console.facility.header.band.state"),
        "0/2 DOORS OPEN " MIDDLE_DOT " 1/2 AREAS POWERED");

    ecs_fini(world);
}

void PortalFacility_shows_viewer_room(void) {
    ecs_world_t *world = facility_world();
    ecs_entity_t viewer = screen_e(world, "viewer");

    test_str(screen_text(world, FACILITY_HERE), "HALL");
    test_assert(facility_border(world, 0) > facility_border(world, 1));

    /* The viewer walks into the vault, then out of the facility */
    ecs_set(world, viewer, Position, {16, 1.5, 5});
    screen_run(world, 20);
    test_str(screen_text(world, FACILITY_HERE), "VAULT");
    test_assert(facility_border(world, 1) > facility_border(world, 0));

    ecs_set(world, viewer, Position, {60, 1.5, 5});
    screen_run(world, 20);
    test_str(screen_text(world, FACILITY_HERE), "OUTSIDE");
    test_flt(facility_border(world, 0), facility_border(world, 1));

    ecs_fini(world);
}

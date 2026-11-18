/**
 * @file PortalDoor.c
 * @brief Doors of a portal world: the PortalDoor template of test/assets/portal/door.flecs shuts
 * its portal (VisPortal.open 0) while shut, opens when asked or for an actor near it, slides its
 * panel along the plane of the portal and writes the open state of the portal as it moves.
 *
 * The door is the one of test/assets/portal/rooms.flecs, on the portal between the store and the
 * vault. Its portal faces +x, so its width runs along -z.
 */

#include "portal_test.h"

/** Run frames of a tenth of a second.
 *
 * @param world The world
 * @param frames Frames to run
 */
static void portal_door_run(
    ecs_world_t *world,
    int32_t frames)
{
    int32_t i;

    for (i = 0; i < frames; i ++) {
        ecs_progress(world, 0.1f);
    }
}

/** Get the open state of a portal.
 *
 * @param world The world
 * @param portal The portal
 * @return VisPortal.open
 */
static float portal_door_open(
    ecs_world_t *world,
    ecs_entity_t portal)
{
    return ecs_get(world, portal, VisPortal)->open;
}

void PortalDoor_starts_shut(void) {
    ecs_world_t *world = portal_test_world();
    ecs_entity_t portal = ecs_lookup(world, "rooms.store_vault");
    ecs_entity_t panel = ecs_lookup(world, "rooms.store_vault.frame.panel");
    const Position *p = ecs_get(world, panel, Position);

    test_assert(panel != 0);
    test_flt(portal_door_open(world, portal), 0);
    test_bool(ecs_has_id(world, panel, DynamicTransform), true);
    test_flt(p->x, 0);
    test_flt(p->y, 0);
    test_flt(p->z, 0);

    portal_door_run(world, 10);
    test_flt(portal_door_open(world, portal), 0);

    ecs_fini(world);
}

void PortalDoor_open_portal_without_door(void) {
    ecs_world_t *world = portal_test_world();
    ecs_entity_t portal = ecs_lookup(world, "rooms.hall_store");

    test_assert(portal != 0);
    test_flt(portal_door_open(world, portal), 1);

    ecs_fini(world);
}

void PortalDoor_opens_when_asked(void) {
    ecs_world_t *world = portal_test_world();
    ecs_entity_t portal = ecs_lookup(world, "rooms.store_vault");

    portal_test_door_set(world, portal, "open", 1);
    portal_door_run(world, 3);
    test_assert(portal_door_open(world, portal) > 0);
    test_assert(portal_door_open(world, portal) < 1);

    portal_door_run(world, 8);
    test_flt(portal_door_open(world, portal), 1);

    ecs_fini(world);
}

void PortalDoor_shuts_again(void) {
    ecs_world_t *world = portal_test_world();
    ecs_entity_t portal = ecs_lookup(world, "rooms.store_vault");

    portal_test_door_set(world, portal, "open", 1);
    portal_door_run(world, 12);
    test_flt(portal_door_open(world, portal), 1);

    portal_test_door_set(world, portal, "open", 0);
    portal_door_run(world, 3);
    test_assert(portal_door_open(world, portal) < 1);

    portal_door_run(world, 9);
    test_flt(portal_door_open(world, portal), 0);

    ecs_fini(world);
}

void PortalDoor_panel_slides(void) {
    ecs_world_t *world = portal_test_world();
    ecs_entity_t portal = ecs_lookup(world, "rooms.store_vault");
    ecs_entity_t panel = ecs_lookup(world, "rooms.store_vault.frame.panel");
    WorldTransform buf;
    const WorldTransform *t;

    portal_test_door_set(world, portal, "open", 1);
    portal_door_run(world, 12);

    /* The panel slid 0.97 of the width of 2 along the width, which runs along -z */
    test_flt(ecs_get(world, panel, Position)->x, 1.94f);
    t = box_transform_world(world, panel, &buf);
    test_assert(t != NULL);
    test_assert(fabsf(t->m[3][0] - 15) < 1e-3f);
    test_assert(fabsf(t->m[3][1] - 1.2f) < 1e-3f);
    test_assert(fabsf(t->m[3][2] - (5 - 1.94f)) < 1e-3f);

    ecs_fini(world);
}

void PortalDoor_traversal_follows(void) {
    ecs_world_t *world = portal_test_world();
    ecs_entity_t portal = ecs_lookup(world, "rooms.store_vault");
    int32_t vault = portal_test_area(world, "rooms.vault");
    box_portal_vis_t vis;
    box_portal_vis_init(&vis);

    portal_test_traverse(world, &vis, 10, 1.5f, 5, PORTAL_TEST_EAST);
    test_int(portal_test_entries(&vis, vault), 0);
    test_bool(portal_test_sees(world, &vis, 20, 1, 5, 0.25f), false);

    portal_test_door_set(world, portal, "open", 1);
    portal_door_run(world, 2);
    portal_test_traverse(world, &vis, 10, 1.5f, 5, PORTAL_TEST_EAST);
    test_int(portal_test_entries(&vis, vault), 1);
    test_bool(portal_test_sees(world, &vis, 20, 1, 5, 0.25f), true);

    portal_test_door_set(world, portal, "open", 0);
    portal_door_run(world, 12);
    portal_test_traverse(world, &vis, 10, 1.5f, 5, PORTAL_TEST_EAST);
    test_int(portal_test_entries(&vis, vault), 0);

    box_portal_vis_fini(&vis);
    ecs_fini(world);
}

void PortalDoor_open_state_without_door(void) {
    ecs_world_t *world = portal_test_world();
    ecs_entity_t portal = ecs_lookup(world, "rooms.hall_store");
    int32_t store = portal_test_area(world, "rooms.store");
    box_portal_vis_t vis;
    box_portal_vis_init(&vis);

    portal_test_traverse(world, &vis, 0, 1.5f, 5, PORTAL_TEST_EAST);
    test_assert(portal_test_entries(&vis, store) > 0);

    ecs_get_mut(world, portal, VisPortal)->open = 0;
    ecs_modified(world, portal, VisPortal);
    portal_test_traverse(world, &vis, 0, 1.5f, 5, PORTAL_TEST_EAST);
    test_int(portal_test_entries(&vis, store), 0);

    box_portal_vis_fini(&vis);
    ecs_fini(world);
}

void PortalDoor_auto_opens_for_actor(void) {
    ecs_world_t *world = portal_test_world();
    ecs_entity_t portal = ecs_lookup(world, "rooms.store_vault");
    ecs_entity_t actor = ecs_new(world);

    ecs_add(world, actor, PortalActor);
    ecs_set(world, actor, Position, {10, 1.5f, 5});
    portal_test_door_set(world, portal, "auto_open", 1);
    portal_door_run(world, 12);
    test_flt(portal_door_open(world, portal), 0);
    test_bool(ecs_get(world, portal, PortalSensor)->near, false);

    ecs_set(world, actor, Position, {14, 1.5f, 5});
    portal_door_run(world, 12);
    test_bool(ecs_get(world, portal, PortalSensor)->near, true);
    test_flt(portal_door_open(world, portal), 1);

    ecs_set(world, actor, Position, {10, 1.5f, 5});
    portal_door_run(world, 12);
    test_bool(ecs_get(world, portal, PortalSensor)->near, false);
    test_flt(portal_door_open(world, portal), 0);

    ecs_fini(world);
}

void PortalDoor_auto_radius(void) {
    ecs_world_t *world = portal_test_world();
    ecs_entity_t portal = ecs_lookup(world, "rooms.store_vault");
    ecs_entity_t actor = ecs_new(world);

    ecs_add(world, actor, PortalActor);
    ecs_set(world, actor, Position, {12, 1.5f, 5});
    portal_test_door_set(world, portal, "auto_open", 1);
    portal_door_run(world, 12);
    test_flt(portal_door_open(world, portal), 0);

    portal_test_door_set(world, portal, "radius", 3.5);
    portal_door_run(world, 12);
    test_flt(portal_door_open(world, portal), 1);

    ecs_fini(world);
}

void PortalDoor_locked_stays_shut(void) {
    ecs_world_t *world = portal_test_world();
    ecs_entity_t portal = ecs_lookup(world, "rooms.store_vault");

    ecs_get_mut(world, portal, VisPortal)->locked = true;
    ecs_modified(world, portal, VisPortal);
    portal_test_door_set(world, portal, "open", 1);
    portal_door_run(world, 12);
    test_flt(portal_door_open(world, portal), 0);

    ecs_get_mut(world, portal, VisPortal)->locked = false;
    ecs_modified(world, portal, VisPortal);
    portal_door_run(world, 12);
    test_flt(portal_door_open(world, portal), 1);

    ecs_fini(world);
}

void PortalDoor_slides_down_with_lift(void) {
    ecs_world_t *world = portal_test_world();
    ecs_entity_t portal = ecs_lookup(world, "rooms.store_vault");
    ecs_entity_t panel = ecs_lookup(world, "rooms.store_vault.frame.panel");
    const Position *p;

    portal_test_door_set(world, portal, "slide", PortalSlideDown);
    portal_test_door_set(world, portal, "lift", 0.1);
    portal_test_door_set(world, portal, "open", 1);
    portal_door_run(world, 12);

    test_int(ecs_get(world, portal, VisPortal)->slide, PortalSlideDown);
    p = ecs_get(world, panel, Position);
    test_flt(p->x, 0);
    test_flt(p->y, -2.4f * 0.97f);
    test_flt(p->z, 0.1f);

    ecs_fini(world);
}

void PortalDoor_speed(void) {
    ecs_world_t *world = portal_test_world();
    ecs_entity_t portal = ecs_lookup(world, "rooms.store_vault");

    portal_test_door_set(world, portal, "speed", 2.5);
    portal_test_door_set(world, portal, "open", 1);
    portal_door_run(world, 5);
    test_flt(portal_door_open(world, portal), 1);

    ecs_fini(world);
}

void PortalDoor_ship_style(void) {
    ecs_world_t *world = portal_test_world();
    ecs_entity_t portal = ecs_lookup(world, "rooms.store_vault");

    portal_test_door_set_string(world, portal, "style", "Ship");
    portal_door_run(world, 1);
    test_assert(ecs_lookup(world, "rooms.store_vault.frame.panel.front.top") != 0);
    test_assert(ecs_lookup(world, "rooms.store_vault.frame.panel.back.dog_bottom") != 0);

    ecs_fini(world);
}

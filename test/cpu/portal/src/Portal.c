/**
 * @file Portal.c
 * @brief Portal index and traversal: the area of a point, the sides of a portal, closed
 * portals, frustum narrowing, the outdoors, occluders and membership across a portal.
 *
 * The rooms are those of test/assets/portal/rooms.flecs. The camera has a horizontal half
 * angle of about 45.7 degrees, so the expected results are worked out by hand from the plan.
 */

#include "portal_test.h"

void Portal_area_at(void) {
    ecs_world_t *world = portal_test_world();
    ecs_entity_t hall = ecs_lookup(world, "rooms.hall");
    ecs_entity_t store = ecs_lookup(world, "rooms.store");
    ecs_entity_t vault = ecs_lookup(world, "rooms.vault");
    ecs_entity_t side = ecs_lookup(world, "rooms.side");

    test_assert(hall != 0);
    test_assert(box_portal_area_at(world, 0, (vec3){0, 1.5f, 5}) == hall);
    test_assert(box_portal_area_at(world, 0, (vec3){-4.9f, 0.1f, 0.1f}) == hall);
    test_assert(box_portal_area_at(world, 0, (vec3){10, 1.5f, 5}) == store);
    test_assert(box_portal_area_at(world, 0, (vec3){20, 1.5f, 5}) == vault);
    test_assert(box_portal_area_at(world, 0, (vec3){0, 1.5f, 15}) == side);

    ecs_fini(world);
}

void Portal_area_at_outdoors(void) {
    ecs_world_t *world = portal_test_world();

    test_assert(box_portal_area_at(world, 0, (vec3){0, 1.5f, -5}) == 0);
    test_assert(box_portal_area_at(world, 0, (vec3){-6, 1.5f, 5}) == 0);
    test_assert(box_portal_area_at(world, 0, (vec3){0, 3.5f, 5}) == 0);
    test_assert(box_portal_area_at(world, 0, (vec3){0, -0.5f, 5}) == 0);
    test_assert(box_portal_area_at(world, 0, (vec3){30, 1.5f, 5}) == 0);

    ecs_fini(world);
}

void Portal_area_at_bend(void) {
    ecs_world_t *world = portal_test_world();
    ecs_entity_t bend = ecs_lookup(world, "rooms.bend");

    test_assert(box_portal_area_at(world, 0, (vec3){47, 1.5f, 2}) == bend);
    test_assert(box_portal_area_at(world, 0, (vec3){42, 1.5f, 8}) == bend);
    test_assert(box_portal_area_at(world, 0, (vec3){47, 1.5f, 7}) == 0);

    ecs_fini(world);
}

void Portal_area_boxes(void) {
    ecs_world_t *world = portal_test_world();
    const box_portal_index_t *index = box_portal_index(world);
    const box_portal_area_t *hall = ecs_vec_get_t(
        &index->areas, box_portal_area_t, portal_test_area(world, "rooms.hall"));
    const box_portal_area_t *bend = ecs_vec_get_t(
        &index->areas, box_portal_area_t, portal_test_area(world, "rooms.bend"));

    test_int(hall->box_count, 1);
    test_bool(hall->exact, true);
    test_int(bend->box_count, 2);
    test_bool(bend->exact, true);

    const box_portal_box_t *low = ecs_vec_get_t(&index->boxes, box_portal_box_t, bend->box_first);
    const box_portal_box_t *high = low + 1;
    test_flt(low->min[0], 40);
    test_flt(low->max[0], 50);
    test_flt(low->min[2], 0);
    test_flt(low->max[2], 4);
    test_flt(high->min[0], 40);
    test_flt(high->max[0], 44);
    test_flt(high->min[2], 4);
    test_flt(high->max[2], 10);
    test_flt(high->min[1], 0);
    test_flt(high->max[1], 3);

    ecs_fini(world);
}

void Portal_portal_sides(void) {
    ecs_world_t *world = portal_test_world();
    const box_portal_index_t *index = box_portal_index(world);
    int32_t hall = portal_test_area(world, "rooms.hall");
    int32_t store = portal_test_area(world, "rooms.store");
    int32_t between = box_portal_index_portal(index, ecs_lookup(world, "rooms.hall_store"));
    int32_t entry = box_portal_index_portal(index, ecs_lookup(world, "rooms.entry"));

    test_assert(between >= 0);
    test_assert(entry >= 0);
    test_int(ecs_vec_count(&index->portals), 4);

    const box_portal_portal_t *p = ecs_vec_get_t(&index->portals, box_portal_portal_t, between);
    test_int(p->front, hall);
    test_int(p->back, store);
    test_flt(p->center[0], 5);
    test_flt(p->center[1], 1.2f);
    test_flt(p->center[2], 5);
    test_flt(p->min[2], 4);
    test_flt(p->max[2], 6);
    test_flt(p->min[1], 0);
    test_flt(p->max[1], 2.4f);

    p = ecs_vec_get_t(&index->portals, box_portal_portal_t, entry);
    test_int(p->front, hall);
    test_int(p->back, 0);

    ecs_fini(world);
}

void Portal_camera_area_is_root(void) {
    ecs_world_t *world = portal_test_world();
    box_portal_vis_t vis;
    box_portal_vis_init(&vis);

    portal_test_traverse(world, &vis, 10, 1.5f, 5, PORTAL_TEST_EAST);
    test_int(vis.root, portal_test_area(world, "rooms.store"));

    const box_portal_entry_t *root = ecs_vec_get_t(&vis.entries, box_portal_entry_t, 0);
    test_int(root->area, vis.root);
    test_int(root->parent, -1);
    test_int(root->via, -1);
    test_int(root->depth, 0);
    test_bool(root->whole, true);

    portal_test_traverse(world, &vis, 0, 1.5f, -8, PORTAL_TEST_NORTH);
    test_int(vis.root, 0);

    box_portal_vis_fini(&vis);
    ecs_fini(world);
}

void Portal_closed_portal_blocks(void) {
    ecs_world_t *world = portal_test_world();
    ecs_entity_t portal = ecs_lookup(world, "rooms.store_vault");
    int32_t vault = portal_test_area(world, "rooms.vault");
    int32_t store = portal_test_area(world, "rooms.store");
    box_portal_vis_t vis;
    box_portal_vis_init(&vis);

    test_flt(ecs_get(world, portal, VisPortal)->open, 0);
    portal_test_traverse(world, &vis, -4, 1.5f, 5, PORTAL_TEST_EAST);
    test_int(portal_test_entries(&vis, store), 1);
    test_int(portal_test_entries(&vis, vault), 0);
    test_int(vis.closed, 1);
    test_bool(portal_test_sees(world, &vis, 10, 1, 5, 0.25f), true);
    test_bool(portal_test_sees(world, &vis, 20, 1, 5, 0.25f), false);

    portal_test_open(world, portal, 1);
    test_flt(ecs_get(world, portal, VisPortal)->open, 1);
    portal_test_traverse(world, &vis, -4, 1.5f, 5, PORTAL_TEST_EAST);
    test_int(portal_test_entries(&vis, vault), 1);
    test_int(vis.closed, 0);
    test_bool(portal_test_sees(world, &vis, 20, 1, 5, 0.25f), true);

    box_portal_vis_fini(&vis);
    ecs_fini(world);
}

void Portal_closed_room_sees_itself(void) {
    ecs_world_t *world = portal_test_world();
    box_portal_vis_t vis;
    box_portal_vis_init(&vis);

    portal_test_traverse(world, &vis, 20, 1.5f, 5, PORTAL_TEST_WEST);
    test_int(ecs_vec_count(&vis.entries), 1);
    test_int(vis.root, portal_test_area(world, "rooms.vault"));
    test_int(vis.tested, 1);
    test_int(vis.passed, 0);
    test_int(vis.closed, 1);
    test_bool(vis.sky, false);
    test_bool(portal_test_sees(world, &vis, 20, 1, 8, 0.25f), true);
    test_bool(portal_test_sees(world, &vis, 10, 1, 5, 0.25f), false);
    test_bool(portal_test_sees(world, &vis, 0, 1, 5, 0.25f), false);
    test_bool(portal_test_sees(world, &vis, 30, 1, 5, 0.25f), false);

    box_portal_vis_fini(&vis);
    ecs_fini(world);
}

void Portal_frustum_narrows(void) {
    ecs_world_t *world = portal_test_world();
    int32_t store = portal_test_area(world, "rooms.store");
    box_portal_vis_t vis;
    box_portal_vis_init(&vis);

    portal_test_traverse(world, &vis, -4, 1.5f, 3, PORTAL_TEST_EAST);
    test_int(portal_test_entries(&vis, store), 1);

    /* The opening spans z 4 to 6 at x 5, 9 from the eye: at x 10 it shows z 4.56 to 7.67. */
    test_bool(portal_test_sees(world, &vis, 10, 1, 6, 0.25f), true);
    test_bool(portal_test_sees(world, &vis, 10, 1, 2, 0.25f), false);
    test_bool(portal_test_sees(world, &vis, 10, 1, 9, 0.25f), false);
    test_bool(portal_test_sees(world, &vis, 6, 1, 9, 0.25f), false);
    test_bool(portal_test_sees(world, &vis, 0, 1, 9, 0.25f), true);

    int32_t i;
    for (i = 0; i < ecs_vec_count(&vis.entries); i ++) {
        const box_portal_entry_t *e = ecs_vec_get_t(&vis.entries, box_portal_entry_t, i);
        if (e->area == store) {
            test_bool(e->whole, false);
            test_int(e->depth, 1);
            test_int(e->hull_count, 4);
            test_int(e->plane_count, 7);
            test_bool(e->from_front, true);
        }
    }

    box_portal_vis_fini(&vis);
    ecs_fini(world);
}

void Portal_frustum_narrows_twice(void) {
    ecs_world_t *world = portal_test_world();
    int32_t vault = portal_test_area(world, "rooms.vault");
    box_portal_vis_t vis;
    box_portal_vis_init(&vis);

    portal_test_open(world, ecs_lookup(world, "rooms.store_vault"), 1);
    portal_test_traverse(world, &vis, -4, 1.5f, 5, PORTAL_TEST_EAST);
    test_int(portal_test_entries(&vis, vault), 1);

    /* The second opening, 19 from the eye, shows z 3.74 to 6.26 at x 20; the first alone
     * would show z 2.33 to 7.67. */
    test_bool(portal_test_sees(world, &vis, 20, 1, 5, 0.25f), true);
    test_bool(portal_test_sees(world, &vis, 20, 1, 7, 0.25f), false);
    test_bool(portal_test_sees(world, &vis, 20, 1, 3, 0.25f), false);
    test_bool(portal_test_sees(world, &vis, 10, 1, 7, 0.25f), false);
    test_bool(portal_test_sees(world, &vis, 10, 1, 6, 0.25f), true);

    int32_t i;
    for (i = 0; i < ecs_vec_count(&vis.entries); i ++) {
        const box_portal_entry_t *e = ecs_vec_get_t(&vis.entries, box_portal_entry_t, i);
        if (e->area == vault) {
            test_int(e->depth, 2);
        }
    }

    box_portal_vis_fini(&vis);
    ecs_fini(world);
}

void Portal_portal_out_of_frustum(void) {
    ecs_world_t *world = portal_test_world();
    box_portal_vis_t vis;
    box_portal_vis_init(&vis);

    portal_test_traverse(world, &vis, 0, 1.5f, 2, PORTAL_TEST_NORTH);
    test_int(portal_test_entries(&vis, portal_test_area(world, "rooms.side")), 1);
    test_int(portal_test_entries(&vis, portal_test_area(world, "rooms.store")), 0);
    test_int(portal_test_entries(&vis, 0), 0);
    test_int(ecs_vec_count(&vis.entries), 2);
    test_bool(portal_test_sees(world, &vis, 10, 1, 5, 0.25f), false);
    test_bool(portal_test_sees(world, &vis, 0, 1, 15, 0.25f), true);

    portal_test_traverse(world, &vis, 0, 1.5f, 5, PORTAL_TEST_WEST);
    test_int(ecs_vec_count(&vis.entries), 1);
    test_int(vis.tested, 3);
    test_int(vis.passed, 0);

    box_portal_vis_fini(&vis);
    ecs_fini(world);
}

void Portal_outdoors_through_portal(void) {
    ecs_world_t *world = portal_test_world();
    box_portal_vis_t vis;
    box_portal_vis_init(&vis);

    portal_test_traverse(world, &vis, 0, 1.5f, 5, PORTAL_TEST_SOUTH);
    test_int(portal_test_entries(&vis, 0), 1);
    test_bool(vis.sky, true);

    /* The opening spans x -1 to 1 at z 0, 5 from the eye: at z -5 it shows x -2 to 2. */
    test_bool(portal_test_sees(world, &vis, 0, 1, -5, 0.25f), true);
    test_bool(portal_test_sees(world, &vis, 6, 1, -5, 0.25f), false);
    test_bool(portal_test_sees(world, &vis, 0, 1, 25, 0.25f), false);
    test_bool(portal_test_sees(world, &vis, -8, 1, 5, 0.25f), false);

    box_portal_vis_fini(&vis);
    ecs_fini(world);
}

void Portal_from_outdoors(void) {
    ecs_world_t *world = portal_test_world();
    int32_t hall = portal_test_area(world, "rooms.hall");
    box_portal_vis_t vis;
    box_portal_vis_init(&vis);

    portal_test_traverse(world, &vis, 0, 1.5f, -8, PORTAL_TEST_NORTH);
    test_int(vis.root, 0);
    test_int(portal_test_entries(&vis, hall), 1);
    test_int(portal_test_entries(&vis, portal_test_area(world, "rooms.side")), 1);
    test_int(portal_test_entries(&vis, portal_test_area(world, "rooms.store")), 0);
    test_bool(portal_test_sees(world, &vis, 30, 1, -5, 0.25f), true);
    test_bool(portal_test_sees(world, &vis, 0, 1, 5, 0.25f), true);
    test_bool(portal_test_sees(world, &vis, -4.5f, 1, 1, 0.25f), false);
    test_bool(portal_test_sees(world, &vis, 10, 1, 5, 0.25f), false);

    int32_t i;
    for (i = 0; i < ecs_vec_count(&vis.entries); i ++) {
        const box_portal_entry_t *e = ecs_vec_get_t(&vis.entries, box_portal_entry_t, i);
        if (e->area == hall) {
            test_bool(e->from_front, false);
        }
    }

    box_portal_vis_fini(&vis);
    ecs_fini(world);
}

void Portal_wrong_side_not_entered(void) {
    ecs_world_t *world = portal_test_world();
    int32_t hall = portal_test_area(world, "rooms.hall");
    box_portal_vis_t vis;
    box_portal_vis_init(&vis);

    /* From the side room the hall is entered once, never back through the same opening. */
    portal_test_traverse(world, &vis, 0, 1.5f, 15, PORTAL_TEST_SOUTH);
    test_int(portal_test_entries(&vis, hall), 1);
    test_int(portal_test_entries(&vis, portal_test_area(world, "rooms.side")), 1);
    test_int(portal_test_entries(&vis, 0), 1);

    box_portal_vis_fini(&vis);
    ecs_fini(world);
}

void Portal_eye_in_opening(void) {
    ecs_world_t *world = portal_test_world();
    int32_t store = portal_test_area(world, "rooms.store");
    box_portal_vis_t vis;
    int32_t i, found = 0;
    box_portal_vis_init(&vis);

    portal_test_traverse(world, &vis, 4.8f, 1.5f, 5, PORTAL_TEST_WEST);
    for (i = 0; i < ecs_vec_count(&vis.entries); i ++) {
        const box_portal_entry_t *e = ecs_vec_get_t(&vis.entries, box_portal_entry_t, i);
        if (e->area == store) {
            test_bool(e->whole, true);
            found ++;
        }
    }

    test_int(found, 1);
    test_bool(portal_test_sees(world, &vis, 10, 1, 9, 0.25f), true);

    box_portal_vis_fini(&vis);
    ecs_fini(world);
}

void Portal_pass_through_keeps_frustum(void) {
    ecs_world_t *world = portal_test_world();
    ecs_entity_t portal = ecs_lookup(world, "rooms.hall_store");
    box_portal_vis_t vis;
    box_portal_vis_init(&vis);

    portal_test_traverse(world, &vis, -4, 1.5f, 3, PORTAL_TEST_EAST);
    test_bool(portal_test_sees(world, &vis, 10, 1, 2, 0.25f), false);

    VisPortal *data = ecs_get_mut(world, portal, VisPortal);
    data->pass_through = true;
    ecs_modified(world, portal, VisPortal);

    portal_test_traverse(world, &vis, -4, 1.5f, 3, PORTAL_TEST_EAST);
    test_bool(portal_test_sees(world, &vis, 10, 1, 2, 0.25f), true);

    box_portal_vis_fini(&vis);
    ecs_fini(world);
}

void Portal_sky_only(void) {
    ecs_world_t *world = portal_test_world();
    ecs_entity_t portal = ecs_lookup(world, "rooms.entry");
    box_portal_vis_t vis;
    box_portal_vis_init(&vis);

    VisPortal *data = ecs_get_mut(world, portal, VisPortal);
    data->sky_only = true;
    ecs_modified(world, portal, VisPortal);

    portal_test_traverse(world, &vis, 0, 1.5f, 5, PORTAL_TEST_SOUTH);
    test_bool(vis.sky, true);
    test_int(portal_test_entries(&vis, 0), 1);
    test_bool(portal_test_sees(world, &vis, 0, 1, -5, 0.25f), false);
    test_bool(portal_test_sees(world, &vis, 0, 1, 5, 0.25f), true);

    box_portal_vis_fini(&vis);
    ecs_fini(world);
}

void Portal_occluder_hides_portal(void) {
    ecs_world_t *world = portal_test_world();
    int32_t store = portal_test_area(world, "rooms.store");
    box_portal_vis_t vis;
    box_portal_vis_init(&vis);

    portal_test_traverse(world, &vis, -4, 1.5f, 5, PORTAL_TEST_EAST);
    test_int(portal_test_entries(&vis, store), 1);
    test_int(vis.occluded, 0);

    ecs_entity_t wall = ecs_new_w_pair(world, EcsChildOf, ecs_lookup(world, "rooms"));
    ecs_set(world, wall, Position, { 2, 1.5f, 5 });
    ecs_set(world, wall, VisOccluder, { .size = {6, 3}, .normal = {1, 0, 0} });
    ecs_progress(world, 0.1f);

    portal_test_traverse(world, &vis, -4, 1.5f, 5, PORTAL_TEST_EAST);
    test_int(portal_test_entries(&vis, store), 0);
    test_int(vis.occluded, 1);
    test_bool(portal_test_sees(world, &vis, 10, 1, 5, 0.25f), false);

    /* Past the occluder nothing hides the opening. */
    portal_test_traverse(world, &vis, 3, 1.5f, 5, PORTAL_TEST_EAST);
    test_int(portal_test_entries(&vis, store), 1);

    box_portal_vis_fini(&vis);
    ecs_fini(world);
}

void Portal_members(void) {
    ecs_world_t *world = portal_test_world();
    ecs_entity_t hall = ecs_lookup(world, "rooms.hall");
    ecs_entity_t store = ecs_lookup(world, "rooms.store");
    ecs_vec_t areas;
    bool outdoors = false;
    ecs_vec_init_t(NULL, &areas, ecs_entity_t, 0);

    box_portal_members(world, 0, (vec3){-0.5f, 0.5f, 4.5f}, (vec3){0.5f, 1.5f, 5.5f},
        &areas, &outdoors);
    test_int(ecs_vec_count(&areas), 1);
    test_assert(*ecs_vec_get_t(&areas, ecs_entity_t, 0) == hall);
    test_bool(outdoors, false);

    ecs_vec_clear(&areas);
    box_portal_members(world, 0, (vec3){-0.5f, 0.5f, -5.5f}, (vec3){0.5f, 1.5f, -4.5f},
        &areas, &outdoors);
    test_int(ecs_vec_count(&areas), 0);
    test_bool(outdoors, true);

    /* The outer layer of a wall touches the hall but does not reach into it; the inner one
     * is all inside. */
    ecs_vec_clear(&areas);
    box_portal_members(world, 0, (vec3){-5.1f, 0, 2}, (vec3){-5, 3, 8}, &areas, &outdoors);
    test_int(ecs_vec_count(&areas), 0);
    test_bool(outdoors, true);

    ecs_vec_clear(&areas);
    box_portal_members(world, 0, (vec3){-5, 0, 2}, (vec3){-4.9f, 3, 8}, &areas, &outdoors);
    test_int(ecs_vec_count(&areas), 1);
    test_assert(*ecs_vec_get_t(&areas, ecs_entity_t, 0) == hall);
    test_bool(outdoors, false);

    (void)store;
    ecs_vec_fini_t(NULL, &areas, ecs_entity_t);
    ecs_fini(world);
}

void Portal_members_across_portal(void) {
    ecs_world_t *world = portal_test_world();
    ecs_entity_t hall = ecs_lookup(world, "rooms.hall");
    ecs_entity_t store = ecs_lookup(world, "rooms.store");
    ecs_vec_t areas;
    bool outdoors = true;
    ecs_vec_init_t(NULL, &areas, ecs_entity_t, 0);

    /* A frame in the opening between the hall and the store belongs to both. */
    box_portal_members(world, 0, (vec3){4.85f, 0, 3.9f}, (vec3){5.15f, 2.4f, 4},
        &areas, &outdoors);
    test_int(ecs_vec_count(&areas), 2);
    test_assert(*ecs_vec_get_t(&areas, ecs_entity_t, 0) == hall);
    test_assert(*ecs_vec_get_t(&areas, ecs_entity_t, 1) == store);
    test_bool(outdoors, false);

    /* A frame in the entry belongs to the hall and to the outdoors. */
    ecs_vec_clear(&areas);
    box_portal_members(world, 0, (vec3){-1.1f, 0, -0.15f}, (vec3){-1, 2.4f, 0.15f},
        &areas, &outdoors);
    test_int(ecs_vec_count(&areas), 1);
    test_assert(*ecs_vec_get_t(&areas, ecs_entity_t, 0) == hall);
    test_bool(outdoors, true);

    ecs_vec_fini_t(NULL, &areas, ecs_entity_t);
    ecs_fini(world);
}

void Portal_member_of_seen_area_is_seen(void) {
    ecs_world_t *world = portal_test_world();
    ecs_vec_t block;
    box_portal_vis_t vis;
    box_portal_vis_init(&vis);
    ecs_vec_init_t(NULL, &block, vec4, 0);

    /* From the closed vault only the vault is seen. The panel of its door stands in the
     * opening, half in the store, and is drawn; what is all in the store is not. */
    portal_test_traverse(world, &vis, 20, 1.5f, 5, PORTAL_TEST_WEST);
    test_bool(box_portal_block_pack(box_portal_index(world), &vis, &block,
        BOX_PORTAL_BLOCK_MAX), true);

    const vec4 *data = ecs_vec_first_t(&block, vec4);
    test_bool(box_portal_block_sees(data, (vec3){14.97f, 0, 4}, (vec3){15.03f, 2.4f, 6}), true);
    test_bool(box_portal_block_sees(data, (vec3){14.5f, 0, 4}, (vec3){14.9f, 2.4f, 6}), false);

    /* From the hall, looking away from the store, the frame of the opening to the store is
     * still a member of the hall. */
    ecs_vec_clear(&block);
    portal_test_traverse(world, &vis, 0, 1.5f, 5, PORTAL_TEST_WEST);
    test_bool(box_portal_block_pack(box_portal_index(world), &vis, &block,
        BOX_PORTAL_BLOCK_MAX), true);

    data = ecs_vec_first_t(&block, vec4);
    test_bool(box_portal_block_sees(data, (vec3){4.85f, 0, 3.9f}, (vec3){5.15f, 2.4f, 4}), true);
    test_bool(box_portal_block_sees(data, (vec3){5.5f, 0, 3.9f}, (vec3){5.8f, 2.4f, 4}), false);

    ecs_vec_fini_t(NULL, &block, vec4);
    box_portal_vis_fini(&vis);
    ecs_fini(world);
}

void Portal_bend_notch_is_outdoors(void) {
    ecs_world_t *world = portal_test_world();
    ecs_entity_t bend = ecs_lookup(world, "rooms.bend");
    ecs_vec_t areas;
    bool outdoors = false;
    ecs_vec_init_t(NULL, &areas, ecs_entity_t, 0);

    box_portal_members(world, 0, (vec3){46.5f, 0.5f, 6.5f}, (vec3){47.5f, 1.5f, 7.5f},
        &areas, &outdoors);
    test_int(ecs_vec_count(&areas), 0);
    test_bool(outdoors, true);

    box_portal_members(world, 0, (vec3){41.5f, 0.5f, 6.5f}, (vec3){42.5f, 1.5f, 7.5f},
        &areas, &outdoors);
    test_int(ecs_vec_count(&areas), 1);
    test_assert(*ecs_vec_get_t(&areas, ecs_entity_t, 0) == bend);
    test_bool(outdoors, false);

    /* Across the two boxes of the bend it is still all inside. */
    ecs_vec_clear(&areas);
    box_portal_members(world, 0, (vec3){41.5f, 0.5f, 3.5f}, (vec3){42.5f, 1.5f, 4.5f},
        &areas, &outdoors);
    test_int(ecs_vec_count(&areas), 1);
    test_bool(outdoors, false);

    ecs_vec_fini_t(NULL, &areas, ecs_entity_t);
    ecs_fini(world);
}

void Portal_block_layout(void) {
    ecs_world_t *world = portal_test_world();
    ecs_vec_t block;
    box_portal_vis_t vis;
    box_portal_vis_init(&vis);
    ecs_vec_init_t(NULL, &block, vec4, 0);

    portal_test_traverse(world, &vis, 20, 1.5f, 5, PORTAL_TEST_WEST);
    test_bool(box_portal_block_pack(box_portal_index(world), &vis, &block,
        BOX_PORTAL_BLOCK_MAX), true);

    /* Five areas, the bend with two boxes; only the vault is seen, whole. */
    const vec4 *data = ecs_vec_first_t(&block, vec4);
    test_flt(data[0][0], 6);
    test_flt(data[0][1], 5);
    test_flt(data[0][3], 0);
    test_int(ecs_vec_count(&block), 1 + 12 + 5);

    int32_t i, whole = 0, unseen = 0;
    for (i = 0; i < 5; i ++) {
        whole += data[13 + i][1] < 0;
        unseen += data[13 + i][1] == 0;
    }

    test_int(whole, 1);
    test_int(unseen, 4);

    /* A block that does not fit is not written. */
    ecs_vec_clear(&block);
    test_bool(box_portal_block_pack(box_portal_index(world), &vis, &block, 10), false);
    test_int(ecs_vec_count(&block), 0);

    ecs_vec_fini_t(NULL, &block, vec4);
    box_portal_vis_fini(&vis);
    ecs_fini(world);
}

void Portal_block_without_areas(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_progress(world, 0.1f);

    ecs_vec_t block;
    box_portal_vis_t vis;
    box_portal_vis_init(&vis);
    ecs_vec_init_t(NULL, &block, vec4, 0);

    test_bool(box_portal_index_has(box_portal_index(world), 0), false);
    portal_test_traverse(world, &vis, 0, 1.5f, 5, PORTAL_TEST_WEST);
    test_int(ecs_vec_count(&vis.entries), 1);
    test_int(vis.root, 0);
    test_bool(box_portal_block_pack(box_portal_index(world), &vis, &block,
        BOX_PORTAL_BLOCK_MAX), false);

    ecs_vec_fini_t(NULL, &block, vec4);
    box_portal_vis_fini(&vis);
    ecs_fini(world);
}

void Portal_scene_scoping(void) {
    ecs_world_t *world = portal_test_world();

    test_int(ecs_script_run(world, NULL,
        "using box.view\n"
        "using box.portal\n"
        "other {\n"
        "  Scene: {}\n"
        "  room { VisArea: {shape: [{100, 0}, {110, 0}, {110, 10}, {100, 10}], height: 3} }\n"
        "}\n", NULL), 0);
    ecs_progress(world, 0.1f);

    ecs_entity_t other = ecs_lookup(world, "other");
    ecs_entity_t room = ecs_lookup(world, "other.room");
    const box_portal_index_t *index = box_portal_index(world);

    test_assert(room != 0);
    test_assert(box_portal_area_at(world, 0, (vec3){105, 1.5f, 5}) == 0);
    test_assert(box_portal_area_at(world, other, (vec3){105, 1.5f, 5}) == room);
    test_assert(box_portal_area_at(world, other, (vec3){0, 1.5f, 5}) == 0);
    test_bool(box_portal_index_has(index, 0), true);
    test_bool(box_portal_index_has(index, other), true);

    ecs_fini(world);
}

void Portal_index_follows_changes(void) {
    ecs_world_t *world = portal_test_world();
    ecs_entity_t portal = ecs_lookup(world, "rooms.hall_store");
    ecs_entity_t side = ecs_lookup(world, "rooms.side");
    const box_portal_index_t *index = box_portal_index(world);
    int32_t builds = index->builds;

    /* An unchanged frame does not build the index again. */
    ecs_progress(world, 0.1f);
    index = box_portal_index(world);
    test_int(index->builds, builds);

    ecs_set(world, portal, Position, { 5, 1.2f, 8 });
    ecs_progress(world, 0.1f);
    index = box_portal_index(world);
    test_assert(index->builds > builds);

    int32_t at = box_portal_index_portal(index, portal);
    test_assert(at >= 0);
    test_flt(ecs_vec_get_t(&index->portals, box_portal_portal_t, at)->center[2], 8);

    /* Without the side room its opening leads from the hall to the outdoors. */
    ecs_remove(world, side, VisArea);
    index = box_portal_index(world);
    test_assert(box_portal_area_at(world, 0, (vec3){0, 1.5f, 15}) == 0);

    at = box_portal_index_portal(index, ecs_lookup(world, "rooms.hall_side"));
    test_assert(at >= 0);
    test_int(ecs_vec_get_t(&index->portals, box_portal_portal_t, at)->back, 0);

    ecs_fini(world);
}

void Portal_parent_transform(void) {
    ecs_world_t *world = portal_test_world();
    ecs_entity_t rooms = ecs_lookup(world, "rooms");
    ecs_entity_t hall = ecs_lookup(world, "rooms.hall");

    ecs_set(world, rooms, Position, { 100, 10, 0 });
    ecs_progress(world, 0.1f);

    test_assert(box_portal_area_at(world, 0, (vec3){0, 1.5f, 5}) == 0);
    test_assert(box_portal_area_at(world, 0, (vec3){100, 11.5f, 5}) == hall);

    const box_portal_index_t *index = box_portal_index(world);
    int32_t at = box_portal_index_portal(index, ecs_lookup(world, "rooms.hall_store"));
    test_assert(at >= 0);

    const box_portal_portal_t *p = ecs_vec_get_t(&index->portals, box_portal_portal_t, at);
    test_flt(p->center[0], 105);
    test_flt(p->center[1], 11.2f);
    test_int(p->front, portal_test_area(world, "rooms.hall"));
    test_int(p->back, portal_test_area(world, "rooms.store"));

    ecs_fini(world);
}

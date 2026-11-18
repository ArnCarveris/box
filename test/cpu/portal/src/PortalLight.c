/**
 * @file PortalLight.c
 * @brief Light through portals and the shading block: openings, doors and glass, the area of a
 * light, lights and the sun through portals, light culling, the area of a point, the records,
 * fog chains and lights through portals of the block, and the parts of a ship door.
 *
 * The rooms are those of test/assets/portal/lights.flecs.
 */

#include "portal_test.h"
#include "light.h"
#include "shading.h"

/** Create a world with the rooms of test/assets/portal/lights.flecs and run two frames.
 *
 * @return The world
 */
static ecs_world_t* portal_light_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *path = box_app_locate("test/assets/portal/lights.flecs");
    test_assert(path != NULL);
    test_int(ecs_script_run_file(world, path), 0);
    ecs_os_free(path);
    ecs_progress(world, 0.1f);
    ecs_progress(world, 0.1f);

    return world;
}

/** Find the index of a portal of an entity.
 *
 * @param world The world
 * @param path Path of the entity with the VisPortal
 * @return Index of the portal
 */
static int32_t portal_light_portal(
    ecs_world_t *world,
    const char *path)
{
    int32_t result = box_portal_index_portal(box_portal_index(world), ecs_lookup(world, path));

    test_assert(result >= 0);

    return result;
}

/** Get the lights of the world as the portals see them, in the order of the lamps.
 *
 * @param world The world
 * @param out Receives the lights of lamp_hall, lamp_store, lamp_vault, lamp_side and
 * lamp_outside
 */
static void portal_light_lamps(
    ecs_world_t *world,
    box_portal_light_t *out)
{
    const char *names[5] = {
        "lit.lamp_hall", "lit.lamp_store", "lit.lamp_vault", "lit.lamp_side", "lit.lamp_outside"
    };
    int32_t i;

    for (i = 0; i < 5; i ++) {
        ecs_entity_t e = ecs_lookup(world, names[i]);
        const Position *p = ecs_get(world, e, Position);
        const PointLight *l = ecs_get(world, e, PointLight);

        out[i] = (box_portal_light_t){
            .position = { p->x, p->y, p->z },
            .range = l->range,
            .intensity = l->intensity,
            .area = box_portal_light_area(box_portal_index(world), 0, (vec3){ p->x, p->y, p->z })
        };
    }
}

/** Find a light among those that shine into an area.
 *
 * @param through The lights
 * @param count Lights
 * @param light Index of the light, -1 for the sun
 * @return The light, NULL when it is not among them
 */
static const box_portal_through_t* portal_light_find(
    const box_portal_through_t *through,
    int32_t count,
    int32_t light)
{
    const box_portal_through_t *result = NULL;
    int32_t i;

    for (i = 0; i < count; i ++) {
        if (through[i].light == light) {
            result = &through[i];
        }
    }

    return result;
}

void PortalLight_apertures(void) {
    ecs_world_t *world = portal_light_world();
    const box_portal_index_t *index = box_portal_index(world);
    ecs_vec_t apertures;

    ecs_vec_init_t(NULL, &apertures, box_portal_aperture_t, 0);
    box_portal_apertures(world, index, &apertures);
    test_int(ecs_vec_count(&apertures), ecs_vec_count(&index->portals));

    const box_portal_aperture_t *entry = ecs_vec_get_t(&apertures, box_portal_aperture_t,
        portal_light_portal(world, "lit.entry"));
    const box_portal_aperture_t *window = ecs_vec_get_t(&apertures, box_portal_aperture_t,
        portal_light_portal(world, "lit.hall_side"));
    const box_portal_aperture_t *vault = ecs_vec_get_t(&apertures, box_portal_aperture_t,
        portal_light_portal(world, "lit.store_vault"));

    test_bool(entry->open, true);
    test_flt(glm_vec3_norm(ECS_CONST_CAST(float*, entry->right)), 1);
    test_flt(glm_vec3_norm(ECS_CONST_CAST(float*, entry->up)), 1.2f);
    test_flt(entry->center[2], 0);
    test_flt(entry->pass[0], 1);
    test_flt(entry->pass[1], 1);

    test_bool(window->open, true);
    test_flt(window->pass[0], 1);
    test_flt(window->pass[1], 0.5f);
    test_flt(window->pass[2], 0.5f);

    test_bool(vault->open, false);

    ecs_vec_fini_t(NULL, &apertures, box_portal_aperture_t);
    ecs_fini(world);
}

void PortalLight_door_opening(void) {
    ecs_world_t *world = portal_light_world();
    ecs_entity_t door = ecs_lookup(world, "lit.store_vault");
    int32_t portal = portal_light_portal(world, "lit.store_vault");
    ecs_vec_t apertures;
    const box_portal_aperture_t *a;
    float half;

    ecs_vec_init_t(NULL, &apertures, box_portal_aperture_t, 0);
    portal_test_door_set(world, door, "open", 1);
    ecs_progress(world, 0.3f);
    box_portal_apertures(world, box_portal_index(world), &apertures);
    a = ecs_vec_get_t(&apertures, box_portal_aperture_t, portal);
    half = glm_vec3_norm(ECS_CONST_CAST(float*, a->right));

    /* Partly open: light passes the part the panel left, on the side it slid away from. */
    test_bool(a->open, true);
    test_assert(half > 0.05f);
    test_assert(half < 0.9f);
    test_flt(glm_vec3_norm(ECS_CONST_CAST(float*, a->up)), 1.2f);
    test_assert(fabsf(a->center[2] - 5) > 0.1f);

    ecs_progress(world, 1.0f);
    ecs_progress(world, 1.0f);
    box_portal_apertures(world, box_portal_index(world), &apertures);
    a = ecs_vec_get_t(&apertures, box_portal_aperture_t, portal);
    test_assert(glm_vec3_norm(ECS_CONST_CAST(float*, a->right)) > 0.95f);

    ecs_vec_fini_t(NULL, &apertures, box_portal_aperture_t);
    ecs_fini(world);
}

void PortalLight_light_area(void) {
    ecs_world_t *world = portal_light_world();
    const box_portal_index_t *index = box_portal_index(world);
    int32_t hall = portal_test_area(world, "lit.hall");
    int32_t vault = portal_test_area(world, "lit.vault");

    test_int(box_portal_light_area(index, 0, (vec3){0, 2, 5}), hall);
    test_int(box_portal_light_area(index, 0, (vec3){20, 2, 5}), vault);
    test_int(box_portal_light_area(index, 0, (vec3){0, 2, -5}), 0);

    /* A lamp just above the ceiling of the hall belongs to the hall. */
    test_int(box_portal_light_area(index, 0, (vec3){0, 3.1f, 5}), hall);
    test_int(box_portal_light_area(index, 0, (vec3){0, 4, 5}), 0);

    ecs_fini(world);
}

void PortalLight_through_neighbours(void) {
    ecs_world_t *world = portal_light_world();
    const box_portal_index_t *index = box_portal_index(world);
    int32_t store = portal_test_area(world, "lit.store");
    box_portal_through_t through[BOX_PORTAL_THROUGH_MAX];
    box_portal_light_t lamps[5];
    ecs_vec_t apertures;
    const box_portal_through_t *t;
    int32_t count;

    ecs_vec_init_t(NULL, &apertures, box_portal_aperture_t, 0);
    box_portal_apertures(world, index, &apertures);
    portal_light_lamps(world, lamps);
    count = box_portal_through(index, &apertures, store, lamps, 5, NULL, (vec3){10, 1.7f, 5},
        through, BOX_PORTAL_THROUGH_MAX);

    /* The hall lamp comes in through the doorway, the side lamp through the window and the
     * doorway, red; the vault lamp is behind the shut door, the store lamp is the store's own
     * and the outdoor lamp is out of range of the entry. */
    t = portal_light_find(through, count, 0);
    test_assert(t != NULL);
    test_int(t->count, 1);
    test_int(t->portals[0], portal_light_portal(world, "lit.hall_store"));
    test_flt(t->pass[1], 1);

    t = portal_light_find(through, count, 3);
    test_assert(t != NULL);
    test_int(t->count, 2);
    test_int(t->portals[0], portal_light_portal(world, "lit.hall_side"));
    test_int(t->portals[1], portal_light_portal(world, "lit.hall_store"));
    test_flt(t->pass[0], 1);
    test_flt(t->pass[1], 0.5f);

    test_assert(portal_light_find(through, count, 1) == NULL);
    test_assert(portal_light_find(through, count, 2) == NULL);
    test_assert(portal_light_find(through, count, 4) == NULL);
    test_assert(portal_light_find(through, count, -1) == NULL);
    test_int(count, 2);
    test_assert(through[0].weight >= through[1].weight);

    ecs_vec_fini_t(NULL, &apertures, box_portal_aperture_t);
    ecs_fini(world);
}

void PortalLight_through_sun(void) {
    ecs_world_t *world = portal_light_world();
    const box_portal_index_t *index = box_portal_index(world);
    int32_t hall = portal_test_area(world, "lit.hall");
    int32_t store = portal_test_area(world, "lit.store");
    box_portal_through_t through[BOX_PORTAL_THROUGH_MAX];
    vec3 south = { 0, 0.7071f, -0.7071f }, north = { 0, 0.7071f, 0.7071f };
    vec3 low = { -0.6f, 0.6f, -0.5292f };
    ecs_vec_t apertures;
    int32_t count;

    ecs_vec_init_t(NULL, &apertures, box_portal_aperture_t, 0);
    box_portal_apertures(world, index, &apertures);

    /* A sun in the south shines into the hall through the entry, one in the north does not. */
    count = box_portal_through(index, &apertures, hall, NULL, 0, south, (vec3){0, 1.7f, 5},
        through, BOX_PORTAL_THROUGH_MAX);
    test_int(count, 1);
    test_int(through[0].light, -1);
    test_int(through[0].portals[0], portal_light_portal(world, "lit.entry"));

    count = box_portal_through(index, &apertures, hall, NULL, 0, north, (vec3){0, 1.7f, 5},
        through, BOX_PORTAL_THROUGH_MAX);
    test_int(count, 0);

    /* Rays through the entry along the south sun run parallel to the store doorway, and those
     * of a sun from the south west reach the floor of the hall before the doorway. */
    count = box_portal_through(index, &apertures, store, NULL, 0, south, (vec3){10, 1.7f, 5},
        through, BOX_PORTAL_THROUGH_MAX);
    test_int(count, 0);
    count = box_portal_through(index, &apertures, store, NULL, 0, low, (vec3){10, 1.7f, 5},
        through, BOX_PORTAL_THROUGH_MAX);
    test_int(count, 0);

    ecs_vec_fini_t(NULL, &apertures, box_portal_aperture_t);
    ecs_fini(world);
}

void PortalLight_reaches(void) {
    ecs_world_t *world = portal_light_world();
    const box_portal_index_t *index = box_portal_index(world);
    int32_t count = ecs_vec_count(&index->areas);
    bool *seen = ecs_os_calloc_n(bool, count);
    box_portal_light_t lamps[5];
    ecs_vec_t apertures;

    ecs_vec_init_t(NULL, &apertures, box_portal_aperture_t, 0);
    box_portal_apertures(world, index, &apertures);
    portal_light_lamps(world, lamps);
    seen[portal_test_area(world, "lit.hall")] = true;

    /* Seen is only the hall: its own lamp, the store lamp through the doorway and the side
     * lamp through the window light it; the vault lamp is behind the shut door and the outdoor
     * lamp too far from the entry, until its range grows. */
    test_bool(box_portal_light_reaches(index, &apertures, &lamps[0], seen), true);
    test_bool(box_portal_light_reaches(index, &apertures, &lamps[1], seen), true);
    test_bool(box_portal_light_reaches(index, &apertures, &lamps[2], seen), false);
    test_bool(box_portal_light_reaches(index, &apertures, &lamps[3], seen), true);
    test_bool(box_portal_light_reaches(index, &apertures, &lamps[4], seen), false);
    lamps[4].range = 8;
    test_bool(box_portal_light_reaches(index, &apertures, &lamps[4], seen), true);

    /* The vault lamp reaches the hall through the open door and the store, two portals. */
    portal_test_door_set(world, ecs_lookup(world, "lit.store_vault"), "open", 1);
    ecs_progress(world, 1.0f);
    ecs_progress(world, 1.0f);
    box_portal_apertures(world, index, &apertures);
    lamps[2].range = 16;
    test_bool(box_portal_light_reaches(index, &apertures, &lamps[2], seen), true);
    lamps[2].range = 6;
    test_bool(box_portal_light_reaches(index, &apertures, &lamps[2], seen), false);

    ecs_os_free(seen);
    ecs_vec_fini_t(NULL, &apertures, box_portal_aperture_t);
    ecs_fini(world);
}

/** Pack the shading block of a camera in the hall looking south at the entry.
 *
 * @param world The world
 * @param vis The traversal, initialized by the caller, NULL to pack without one
 * @param out Receives the block
 * @param apertures Receives the openings
 * @param lamps Receives the lights
 */
static void portal_light_pack(
    ecs_world_t *world,
    box_portal_vis_t *vis,
    ecs_vec_t *out,
    ecs_vec_t *apertures,
    box_portal_light_t *lamps)
{
    const box_portal_index_t *index = box_portal_index(world);
    vec3 eye = { 0, 1.5f, 8 };
    vec3 sun = { 0, 0.7071f, -0.7071f };

    if (vis) {
        portal_test_traverse(world, vis, eye[0], eye[1], eye[2], PORTAL_TEST_SOUTH);
    }

    box_portal_apertures(world, index, apertures);
    portal_light_lamps(world, lamps);
    box_portal_shade_desc_t desc = {
        .index = index,
        .scene = 0,
        .vis = vis,
        .apertures = apertures,
        .lights = lamps,
        .light_count = 5,
        .sun = sun,
        .eye = eye
    };
    test_bool(box_portal_shade_pack(world, &desc, out, BOX_PORTAL_SHADE_MAX), true);
}

void PortalLight_shade_areas(void) {
    ecs_world_t *world = portal_light_world();
    int32_t hall = portal_test_area(world, "lit.hall");
    int32_t store = portal_test_area(world, "lit.store");
    int32_t vault = portal_test_area(world, "lit.vault");
    box_portal_light_t lamps[5];
    ecs_vec_t block, apertures;
    box_portal_vis_t vis;
    const vec4 *b;

    ecs_vec_init_t(NULL, &block, vec4, 0);
    ecs_vec_init_t(NULL, &apertures, box_portal_aperture_t, 0);
    box_portal_vis_init(&vis);
    portal_light_pack(world, &vis, &block, &apertures, lamps);
    b = ecs_vec_first_t(&block, vec4);

    test_flt(b[0][3], 1);
    test_int((int32_t)b[0][1], ecs_vec_count(&box_portal_index(world)->areas));

    /* The hall is seen and the store is its neighbour: both are boxed. The vault, behind the
     * store, is not, so a point in it falls to the outdoors. */
    test_int(box_portal_shade_area(b, (vec3){0, 0.1f, 5}), hall);
    test_int(box_portal_shade_area(b, (vec3){10, 2.9f, 5}), store);
    test_int(box_portal_shade_area(b, (vec3){0, 1.5f, -3}), 0);
    test_int(box_portal_shade_area(b, (vec3){20, 1.5f, 5}), 0);
    test_assert(vault > 0);

    const float *outdoors = b[(int32_t)b[0][2]];
    const float *rec = b[(int32_t)b[0][2] + BOX_PORTAL_SHADE_RECORD * hall];
    const float *store_rec = b[(int32_t)b[0][2] + BOX_PORTAL_SHADE_RECORD * store];
    test_flt(outdoors[0], -1);
    test_flt(rec[0], 0.02f);
    test_flt(rec[2], 0.04f);
    test_flt(rec[3], 0);
    test_flt(rec[4], 0.3f);
    test_flt(rec[7], 0.05f);
    test_flt(store_rec[3], 0.25f);
    test_flt(store_rec[7], 0);

    box_portal_vis_fini(&vis);
    ecs_vec_fini_t(NULL, &apertures, box_portal_aperture_t);
    ecs_vec_fini_t(NULL, &block, vec4);
    ecs_fini(world);
}

void PortalLight_shade_fog_chain(void) {
    ecs_world_t *world = portal_light_world();
    int32_t hall = portal_test_area(world, "lit.hall");
    box_portal_light_t lamps[5];
    ecs_vec_t block, apertures;
    box_portal_vis_t vis;
    const vec4 *b;

    ecs_vec_init_t(NULL, &block, vec4, 0);
    ecs_vec_init_t(NULL, &apertures, box_portal_aperture_t, 0);
    box_portal_vis_init(&vis);
    portal_light_pack(world, &vis, &block, &apertures, lamps);
    b = ecs_vec_first_t(&block, vec4);

    /* The hall is seen whole: one entry, no planes, no chain. */
    const float *hall_slot = b[(int32_t)b[0][2] + BOX_PORTAL_SHADE_RECORD * hall + 2];
    test_int((int32_t)hall_slot[1], 1);
    test_flt(b[(int32_t)hall_slot[0]][0], 0);
    test_flt(b[(int32_t)hall_slot[0]][1], 0);

    /* The outdoors is seen through the entry: planes, then one link, the plane of the entry
     * with the fog of the hall in front of it. */
    const float *out_slot = b[(int32_t)b[0][2] + 2];
    const float *entry;
    int32_t planes, link;

    test_int((int32_t)out_slot[1], 1);
    entry = b[(int32_t)out_slot[0]];
    planes = (int32_t)entry[0];
    test_assert(planes > 0);
    test_int((int32_t)entry[1], 1);
    link = (int32_t)out_slot[0] + 1 + planes;
    test_flt(fabsf(b[link][2]), 1);
    test_flt(b[link][0], 0);
    test_flt(b[link + 1][0], 0.3f);
    test_flt(b[link + 1][3], 0.05f);

    /* A point outside in front of the entry is inside the planes of that entry. */
    for (int32_t k = 0; k < planes; k ++) {
        const float *p = b[(int32_t)out_slot[0] + 1 + k];
        test_assert(p[0] * 0 + p[1] * 1.2f + p[2] * -6 + p[3] >= 0);
    }

    box_portal_vis_fini(&vis);
    ecs_vec_fini_t(NULL, &apertures, box_portal_aperture_t);
    ecs_vec_fini_t(NULL, &block, vec4);
    ecs_fini(world);
}

void PortalLight_shade_throughs(void) {
    ecs_world_t *world = portal_light_world();
    int32_t hall = portal_test_area(world, "lit.hall");
    box_portal_light_t lamps[5];
    ecs_vec_t block, apertures;
    box_portal_vis_t vis;
    const vec4 *b;
    bool sun = false, store = false;
    int32_t i;

    ecs_vec_init_t(NULL, &block, vec4, 0);
    ecs_vec_init_t(NULL, &apertures, box_portal_aperture_t, 0);
    box_portal_vis_init(&vis);
    portal_light_pack(world, &vis, &block, &apertures, lamps);
    b = ecs_vec_first_t(&block, vec4);

    /* The hall gets the sun through the entry and the store lamp through the doorway. */
    const float *slot = b[(int32_t)b[0][2] + BOX_PORTAL_SHADE_RECORD * hall + 2];
    test_assert((int32_t)slot[3] >= 2);

    for (i = 0; i < (int32_t)slot[3]; i ++) {
        const vec4 *rec = &b[(int32_t)slot[2] + i * BOX_PORTAL_SHADE_THROUGH];
        if (rec[0][0] < 0) {
            sun = true;
            test_flt(rec[0][1], 1);
            test_flt(rec[0][2], BOX_PORTAL_SUN_SOFT * BOX_PORTAL_SUN_FAR);
            test_flt(rec[2][2], 0);
        } else if ((int32_t)rec[0][0] == 1) {
            store = true;
            test_flt(rec[0][2], BOX_PORTAL_LIGHT_SIZE);
            test_flt(rec[2][0], 5);
            test_flt(rec[1][0], 1);
        }
    }

    test_bool(sun, true);
    test_bool(store, true);

    box_portal_vis_fini(&vis);
    ecs_vec_fini_t(NULL, &apertures, box_portal_aperture_t);
    ecs_vec_fini_t(NULL, &block, vec4);
    ecs_fini(world);
}

void PortalLight_shade_without_traversal(void) {
    ecs_world_t *world = portal_light_world();
    int32_t hall = portal_test_area(world, "lit.hall");
    int32_t vault = portal_test_area(world, "lit.vault");
    box_portal_light_t lamps[5];
    ecs_vec_t block, apertures;
    const vec4 *b;

    ecs_vec_init_t(NULL, &block, vec4, 0);
    ecs_vec_init_t(NULL, &apertures, box_portal_aperture_t, 0);
    portal_light_pack(world, NULL, &block, &apertures, lamps);
    b = ecs_vec_first_t(&block, vec4);

    /* Every area is boxed and none has entries; the nearest get their lights. */
    test_int(box_portal_shade_area(b, (vec3){20, 1.5f, 5}), vault);
    test_int(box_portal_shade_area(b, (vec3){0, 1.5f, 5}), hall);

    const float *slot = b[(int32_t)b[0][2] + BOX_PORTAL_SHADE_RECORD * hall + 2];
    test_int((int32_t)slot[1], 0);
    test_assert((int32_t)slot[3] >= 1);

    ecs_vec_fini_t(NULL, &apertures, box_portal_aperture_t);
    ecs_vec_fini_t(NULL, &block, vec4);
    ecs_fini(world);
}

void PortalLight_shade_without_areas(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_progress(world, 0.1f);
    ecs_vec_t block, apertures;
    vec3 eye = { 0, 0, 0 };

    ecs_vec_init_t(NULL, &block, vec4, 0);
    ecs_vec_init_t(NULL, &apertures, box_portal_aperture_t, 0);
    box_portal_shade_desc_t desc = {
        .index = box_portal_index(world),
        .apertures = &apertures,
        .eye = eye
    };
    test_bool(box_portal_shade_pack(world, &desc, &block, BOX_PORTAL_SHADE_MAX), false);
    test_int(ecs_vec_count(&block), 0);

    ecs_vec_fini_t(NULL, &apertures, box_portal_aperture_t);
    ecs_vec_fini_t(NULL, &block, vec4);
    ecs_fini(world);
}

void PortalLight_ship_door(void) {
    ecs_world_t *world = portal_light_world();
    ecs_entity_t door = ecs_lookup(world, "lit.store_vault");
    ecs_entity_t panel = ecs_lookup(world, "lit.store_vault.frame.panel");
    ecs_entity_t top = ecs_lookup(world, "lit.store_vault.frame.panel.front.top");
    ecs_entity_t back = ecs_lookup(world, "lit.store_vault.frame.panel.back.top");
    ecs_entity_t spoke = ecs_lookup(world, "lit.store_vault.frame.panel.front.spoke_along");
    static const char *names[] = {
        "bottom", "top", "left", "right", "spoke_across", "spoke_along", "dog_top", "dog_bottom"
    };
    int32_t i, parts = 0;

    /* Eight parts on each face of the panel, in the space of the panel: x along the width of
     * the portal, y up and z along its normal. */
    for (i = 0; i < 16; i ++) {
        char name[96];
        ecs_os_snprintf(name, ECS_SIZEOF(name), "lit.store_vault.frame.panel.%s.%s",
            i < 8 ? "front" : "back", names[i % 8]);
        parts += ecs_lookup(world, name) && ecs_has(world, ecs_lookup(world, name), Box) ? 1 : 0;
    }

    test_int(parts, 16);
    test_assert(ecs_get_parent(world, ecs_get_parent(world, top)) == panel);

    const Box *bar = ecs_get(world, top, Box);
    const Position *at = ecs_get(world, top, Position);
    test_flt(bar->x, 1.92f);
    test_flt(bar->y, 0.08f);
    test_flt(bar->z, 0.04f);
    test_flt(at->y, 1.12f);
    test_flt(at->z, 0.06f);
    test_flt(ecs_get(world, back, Position)->z, -at->z);

    const Box *wheel = ecs_get(world, spoke, Box);
    test_flt(wheel->x, 0.05f);
    test_flt(wheel->y, 0.44f);

    /* The parts ride with the panel as it opens: the width of the portal runs along z. */
    portal_test_door_set(world, door, "open", 1);
    ecs_progress(world, 1.0f);
    ecs_progress(world, 1.0f);
    WorldTransform buf;
    const WorldTransform *t = box_transform_world(world, top, &buf);
    test_assert(t != NULL);
    test_assert(fabsf(t->m[3][2] - 5) > 1.5f);

    ecs_fini(world);
}

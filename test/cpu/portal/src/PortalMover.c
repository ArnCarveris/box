#include "portal_test.h"
#include "light.h"
#include "shading.h"

/** Load the test rooms under a mover: rooms owns DynamicTransform at the origin.
 *
 * @return The world
 */
static ecs_world_t* portal_mover_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_entity_t rooms = ecs_entity(world, { .name = "rooms" });
    ecs_set(world, rooms, Position, {0, 0, 0});
    ecs_set(world, rooms, Rotation, {0, 0, 0});
    ecs_add(world, rooms, DynamicTransform);

    char *path = box_app_locate("test/assets/portal/rooms.flecs");
    test_assert(path != NULL);
    test_int(ecs_script_run_file(world, path), 0);
    ecs_os_free(path);
    ecs_progress(world, 0.1f);
    ecs_progress(world, 0.1f);

    return world;
}

/** Move and turn the rooms and let the transforms follow.
 *
 * @param world The world
 * @param x X of the rooms
 * @param z Z of the rooms
 * @param yaw Their yaw
 * @param out Receives their world transform
 */
static void portal_mover_place(
    ecs_world_t *world,
    float x,
    float z,
    float yaw,
    mat4 out)
{
    ecs_entity_t rooms = ecs_lookup(world, "rooms");
    WorldTransform buf;

    ecs_set(world, rooms, Position, {x, 0, z});
    ecs_set(world, rooms, Rotation, {0, yaw, 0});
    ecs_progress(world, 0.1f);
    glm_mat4_copy(ECS_CONST_CAST(vec4*, box_transform_world(world, rooms, &buf)->m), out);
}

/** Get the area that holds a point of the rooms' own frame where the rooms are.
 *
 * @param world The world
 * @param m The world transform of the rooms
 * @param x X in the rooms' frame
 * @param y Y in the rooms' frame
 * @param z Z in the rooms' frame
 * @return The area entity, 0 for the outdoors
 */
static ecs_entity_t portal_mover_area_at(
    ecs_world_t *world,
    mat4 m,
    float x,
    float y,
    float z)
{
    vec3 p;

    glm_mat4_mulv3(m, (vec3){x, y, z}, 1.0f, p);

    return box_portal_area_at(world, 0, p);
}

void PortalMover_area_at_turned(void) {
    ecs_world_t *world = portal_mover_world();
    ecs_entity_t hall = ecs_lookup(world, "rooms.hall");
    ecs_entity_t vault = ecs_lookup(world, "rooms.vault");
    ecs_entity_t bend = ecs_lookup(world, "rooms.bend");
    mat4 m;

    portal_mover_place(world, 100, 50, 0.3f, m);
    test_assert(portal_mover_area_at(world, m, 0, 1.5f, 5) == hall);
    test_assert(portal_mover_area_at(world, m, 4.8f, 1.5f, 9.8f) == hall);
    test_assert(portal_mover_area_at(world, m, 20, 1.5f, 5) == vault);
    test_assert(portal_mover_area_at(world, m, 47, 1.5f, 2) == bend);
    test_assert(portal_mover_area_at(world, m, 47, 1.5f, 7) == 0);
    test_assert(portal_mover_area_at(world, m, 0, 1.5f, -0.3f) == 0);
    test_assert(portal_mover_area_at(world, m, -5.3f, 1.5f, 5) == 0);
    test_assert(box_portal_area_at(world, 0, (vec3){0, 1.5f, 5}) == 0);

    ecs_fini(world);
}

void PortalMover_area_at_follows_moves(void) {
    ecs_world_t *world = portal_mover_world();
    ecs_entity_t store = ecs_lookup(world, "rooms.store");
    mat4 m;
    int32_t placements = box_portal_index(world)->placements;

    portal_mover_place(world, -30, 10, 1.0f, m);
    test_assert(portal_mover_area_at(world, m, 10, 1.5f, 5) == store);
    test_assert(box_portal_index(world)->placements > placements);

    portal_mover_place(world, 60, -20, -2.0f, m);
    test_assert(portal_mover_area_at(world, m, 10, 1.5f, 5) == store);
    test_assert(portal_mover_area_at(world, m, 10, 1.5f, -1) == 0);

    portal_mover_place(world, 0, 0, 0, m);
    test_assert(box_portal_area_at(world, 0, (vec3){10, 1.5f, 5}) == store);

    ecs_fini(world);
}

void PortalMover_traverse_turned(void) {
    ecs_world_t *world = portal_mover_world();
    int32_t hall = portal_test_area(world, "rooms.hall");
    int32_t store = portal_test_area(world, "rooms.store");
    int32_t vault = portal_test_area(world, "rooms.vault");
    box_portal_vis_t vis;
    vec3 eye, cube;
    mat4 m;

    box_portal_vis_init(&vis);
    portal_mover_place(world, 40, 30, GLM_PIf, m);

    glm_mat4_mulv3(m, (vec3){0, 1.5f, -8}, 1.0f, eye);
    portal_test_traverse(world, &vis, eye[0], eye[1], eye[2], PORTAL_TEST_SOUTH);
    test_int(vis.root, 0);
    test_assert(portal_test_entries(&vis, hall) > 0);
    test_int(portal_test_entries(&vis, vault), 0);

    glm_mat4_mulv3(m, (vec3){-4, 1.5f, 5}, 1.0f, eye);
    portal_test_traverse(world, &vis, eye[0], eye[1], eye[2], PORTAL_TEST_WEST);
    test_int(vis.root, hall);
    test_assert(portal_test_entries(&vis, store) > 0);
    test_int(portal_test_entries(&vis, vault), 0);

    glm_mat4_mulv3(m, (vec3){10, 1, 5}, 1.0f, cube);
    test_bool(portal_test_sees(world, &vis, cube[0], cube[1], cube[2], 0.25f), true);
    glm_mat4_mulv3(m, (vec3){20, 1, 5}, 1.0f, cube);
    test_bool(portal_test_sees(world, &vis, cube[0], cube[1], cube[2], 0.25f), false);

    box_portal_vis_fini(&vis);
    ecs_fini(world);
}

/** Find the areas a cube of the rooms' own frame is a member of where the rooms are.
 *
 * @param world The world
 * @param m The world transform of the rooms
 * @param x X of its center in the rooms' frame
 * @param z Z of its center in the rooms' frame
 * @param areas Receives the areas, cleared first
 * @return Whether it is a member of the outdoors
 */
static bool portal_mover_members(
    ecs_world_t *world,
    mat4 m,
    float x,
    float z,
    ecs_vec_t *areas)
{
    vec3 center, min, max;
    bool outdoors = false;

    glm_mat4_mulv3(m, (vec3){x, 1.5f, z}, 1.0f, center);
    glm_vec3_subs(center, 0.2f, min);
    glm_vec3_adds(center, 0.2f, max);
    ecs_vec_clear(areas);
    box_portal_members(world, 0, min, max, areas, &outdoors);

    return outdoors;
}

void PortalMover_members_turned(void) {
    ecs_world_t *world = portal_mover_world();
    ecs_entity_t hall = ecs_lookup(world, "rooms.hall");
    ecs_entity_t store = ecs_lookup(world, "rooms.store");
    ecs_vec_t areas;
    mat4 m;

    ecs_vec_init_t(NULL, &areas, ecs_entity_t, 0);
    portal_mover_place(world, 100, 50, 0.3f, m);

    /* Turned by 0.3 the world box of the hall reaches about 3 m past its walls: near a wall a
     * cube still belongs to its own room only */
    test_bool(portal_mover_members(world, m, 4.5f, 5, &areas), false);
    test_int(ecs_vec_count(&areas), 1);
    test_assert(*ecs_vec_first_t(&areas, ecs_entity_t) == hall);

    test_bool(portal_mover_members(world, m, 5.5f, 5, &areas), false);
    test_int(ecs_vec_count(&areas), 1);
    test_assert(*ecs_vec_first_t(&areas, ecs_entity_t) == store);

    test_bool(portal_mover_members(world, m, 0, -0.6f, &areas), true);
    test_int(ecs_vec_count(&areas), 0);

    ecs_vec_fini_t(NULL, &areas, ecs_entity_t);
    ecs_fini(world);
}

void PortalMover_block_sees_turned(void) {
    ecs_world_t *world = portal_mover_world();
    box_portal_vis_t vis;
    vec3 eye, cube;
    mat4 m;

    box_portal_vis_init(&vis);
    portal_mover_place(world, 100, 50, 0.3f, m);
    glm_mat4_mulv3(m, (vec3){0, 1.5f, 8}, 1.0f, eye);
    portal_test_traverse(world, &vis, eye[0], eye[1], eye[2], PORTAL_TEST_SOUTH);

    /* The hall holds the eye and is seen whole. A cube of the store by the hall's corner lies in
     * the hall's turned world box, but not in its box at rest, and no door shows it */
    glm_mat4_mulv3(m, (vec3){4.4f, 1, 0.6f}, 1.0f, cube);
    test_bool(portal_test_sees(world, &vis, cube[0], cube[1], cube[2], 0.25f), true);
    glm_mat4_mulv3(m, (vec3){5.6f, 1, 0.6f}, 1.0f, cube);
    test_bool(portal_test_sees(world, &vis, cube[0], cube[1], cube[2], 0.25f), false);

    box_portal_vis_fini(&vis);
    ecs_fini(world);
}

void PortalMover_shade_area_turned(void) {
    ecs_world_t *world = portal_mover_world();
    const box_portal_index_t *index = box_portal_index(world);
    int32_t hall = portal_test_area(world, "rooms.hall");
    int32_t store = portal_test_area(world, "rooms.store");
    vec3 sun = { 0, 0.7071f, -0.7071f }, eye, at;
    ecs_vec_t block, apertures;
    mat4 m;

    ecs_vec_init_t(NULL, &block, vec4, 0);
    ecs_vec_init_t(NULL, &apertures, box_portal_aperture_t, 0);
    portal_mover_place(world, 100, 50, 0.3f, m);
    glm_mat4_mulv3(m, (vec3){0, 1.5f, 5}, 1.0f, eye);
    box_portal_apertures(world, index, &apertures);

    box_portal_shade_desc_t desc = {
        .index = index,
        .scene = 0,
        .apertures = &apertures,
        .sun = sun,
        .eye = eye
    };
    test_bool(box_portal_shade_pack(world, &desc, &block, BOX_PORTAL_SHADE_MAX), true);
    const vec4 *b = ecs_vec_first_t(&block, vec4);

    glm_mat4_mulv3(m, (vec3){4.9f, 1.5f, 5}, 1.0f, at);
    test_int(box_portal_shade_area(b, at), hall);
    glm_mat4_mulv3(m, (vec3){5.1f, 1.5f, 5}, 1.0f, at);
    test_int(box_portal_shade_area(b, at), store);
    glm_mat4_mulv3(m, (vec3){0, 1.5f, -0.2f}, 1.0f, at);
    test_int(box_portal_shade_area(b, at), 0);
    glm_mat4_mulv3(m, (vec3){4.9f, 2.9f, 9.9f}, 1.0f, at);
    test_int(box_portal_shade_area(b, at), hall);

    ecs_vec_fini_t(NULL, &apertures, box_portal_aperture_t);
    ecs_vec_fini_t(NULL, &block, vec4);
    ecs_fini(world);
}

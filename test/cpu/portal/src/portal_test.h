/**
 * @file portal_test.h
 * @brief Helpers of the box.portal tests: the world of test/assets/portal/rooms.flecs, the props
 * of a door, a camera at a position and yaw, and what its traversal sees.
 */

#ifndef PORTAL_TEST_H
#define PORTAL_TEST_H

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "portal.h"

/** Yaw of a camera that looks along -z */
#define PORTAL_TEST_SOUTH (0.0f)

/** Yaw of a camera that looks along +z */
#define PORTAL_TEST_NORTH (GLM_PIf)

/** Yaw of a camera that looks along +x */
#define PORTAL_TEST_EAST (-GLM_PI_2f)

/** Yaw of a camera that looks along -x */
#define PORTAL_TEST_WEST (GLM_PI_2f)

/** Create a world with the rooms of test/assets/portal/rooms.flecs and run two frames, so the
 * transforms are known and the door closed its portal.
 *
 * @return The world
 */
static ecs_world_t* portal_test_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    char *path = box_app_locate("test/assets/portal/rooms.flecs");
    test_assert(path != NULL);
    test_int(ecs_script_run_file(world, path), 0);
    ecs_os_free(path);
    ecs_progress(world, 0.1f);
    ecs_progress(world, 0.1f);

    return world;
}

/** Copy the PortalDoor template instance of an entity and point a cursor at one of its props.
 *
 * Template instances cannot be written in place (they have an on_replace hook): the copy is set
 * back with portal_test_door_commit.
 *
 * @param world The world
 * @param e The entity with the VisPortal and the door
 * @param member Name of the prop
 * @param cur Receives the cursor, at the prop
 * @return The copy
 */
static ecs_value_t portal_test_door_copy(
    ecs_world_t *world,
    ecs_entity_t e,
    const char *member,
    ecs_meta_cursor_t *cur)
{
    ecs_entity_t door = ecs_lookup(world, "PortalDoor");
    ecs_value_t current = { .type = door, .ptr = NULL };
    ecs_value_t copy;

    test_assert(door != 0);
    current.ptr = ECS_CONST_CAST(void*, ecs_get_id(world, e, door));
    test_assert(current.ptr != NULL);
    copy = ecs_value_new(world, door);
    ecs_value_copy(world, &copy, &current);
    *cur = ecs_meta_cursor(world, door, copy.ptr);
    test_int(ecs_meta_push(cur), 0);
    test_int(ecs_meta_member(cur, member), 0);

    return copy;
}

/** Set the copy of a PortalDoor template instance back on its entity and free it.
 *
 * @param world The world
 * @param e The entity with the door
 * @param copy The copy, freed
 */
static void portal_test_door_commit(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_value_t *copy)
{
    ecs_entity_t door = ecs_lookup(world, "PortalDoor");
    const ecs_type_info_t *ti = ecs_get_type_info(world, door);

    test_assert(ti != NULL);
    ecs_set_id(world, e, door, (size_t)ti->size, copy->ptr);
    ecs_value_fini(world, copy);
}

/** Set a number or bool prop of the PortalDoor template instance of an entity, as script would.
 *
 * @param world The world
 * @param e The entity with the VisPortal and the door
 * @param member Name of the prop
 * @param value Its value
 */
static void portal_test_door_set(
    ecs_world_t *world,
    ecs_entity_t e,
    const char *member,
    double value)
{
    ecs_meta_cursor_t cur;
    ecs_value_t copy = portal_test_door_copy(world, e, member, &cur);

    test_int(ecs_meta_set_float(&cur, value), 0);
    portal_test_door_commit(world, e, &copy);
}

/** Set a string prop of the PortalDoor template instance of an entity, as script would.
 *
 * @param world The world
 * @param e The entity with the VisPortal and the door
 * @param member Name of the prop
 * @param value Its value
 */
static void portal_test_door_set_string(
    ecs_world_t *world,
    ecs_entity_t e,
    const char *member,
    const char *value)
{
    ecs_meta_cursor_t cur;
    ecs_value_t copy = portal_test_door_copy(world, e, member, &cur);

    test_int(ecs_meta_set_string(&cur, value), 0);
    portal_test_door_commit(world, e, &copy);
}

/** Open or close a portal by writing its open state, as game code would.
 *
 * @param world The world
 * @param portal The entity with the VisPortal
 * @param open How far it is open, 0 shut to 1 open
 */
static void portal_test_open(
    ecs_world_t *world,
    ecs_entity_t portal,
    float open)
{
    VisPortal *p = ecs_get_mut(world, portal, VisPortal);

    test_assert(p != NULL);
    p->open = open;
    ecs_modified(world, portal, VisPortal);
}

/** Set a camera with a vertical field of view of 60 degrees on a 16 by 9 surface.
 *
 * @param out Receives the eye and the frustum planes
 * @param x X of the eye
 * @param y Y of the eye
 * @param z Z of the eye
 * @param yaw Yaw of the camera, one of PORTAL_TEST_SOUTH, _NORTH, _EAST and _WEST
 */
static void portal_test_camera(
    box_portal_camera_t *out,
    float x,
    float y,
    float z,
    float yaw)
{
    Camera camera = { .fov = 60, .near_ = 0.1f, .far_ = 500 };
    Position position = { x, y, z };
    Rotation rotation = { 0, yaw, 0 };
    vec3 eye = { x, y, z };
    mat4 transform, view, proj, view_proj;

    box_transform_local(&position, &rotation, NULL, transform);
    glm_mat4_inv(transform, view);
    box_camera_projection(&camera, 16, 9, proj);
    glm_mat4_mul(proj, view, view_proj);
    box_portal_camera_init(out, view_proj, eye);
}

/** Find the index of the area of an entity.
 *
 * @param world The world
 * @param path Path of the entity with the VisArea
 * @return Index of the area in the index, -1 when the index has none for it
 */
static int32_t portal_test_area(
    ecs_world_t *world,
    const char *path)
{
    const box_portal_index_t *index = box_portal_index(world);
    ecs_entity_t e = ecs_lookup(world, path);
    int32_t i, result = -1;

    for (i = 1; e && i < ecs_vec_count(&index->areas); i ++) {
        if (ecs_vec_get_t(&index->areas, box_portal_area_t, i)->entity == e) {
            result = i;
        }
    }

    return result;
}

/** Count the entries of a traversal that see an area.
 *
 * @param vis The traversal
 * @param area Index of the area, 0 for the outdoors
 * @return Entries of the area
 */
static int32_t portal_test_entries(
    const box_portal_vis_t *vis,
    int32_t area)
{
    int32_t i, result = 0;

    for (i = 0; i < ecs_vec_count(&vis->entries); i ++) {
        if (ecs_vec_get_t(&vis->entries, box_portal_entry_t, i)->area == area) {
            result ++;
        }
    }

    return result;
}

/** Traverse the default scene of a world from a camera.
 *
 * @param world The world
 * @param vis Receives what the camera sees, initialized by the caller
 * @param x X of the eye
 * @param y Y of the eye
 * @param z Z of the eye
 * @param yaw Yaw of the camera
 */
static void portal_test_traverse(
    ecs_world_t *world,
    box_portal_vis_t *vis,
    float x,
    float y,
    float z,
    float yaw)
{
    box_portal_camera_t camera;

    portal_test_camera(&camera, x, y, z, yaw);
    box_portal_traverse(box_portal_index(world), 0, &camera, vis);
}

/** Check whether a traversal sees a cube, as the cull pass decides it.
 *
 * @param world The world
 * @param vis The traversal
 * @param x X of the center of the cube
 * @param y Y of the center of the cube
 * @param z Z of the center of the cube
 * @param half Half the size of the cube
 * @return Whether the packed traversal sees the cube
 */
static bool portal_test_sees(
    ecs_world_t *world,
    const box_portal_vis_t *vis,
    float x,
    float y,
    float z,
    float half)
{
    vec3 min = { x - half, y - half, z - half };
    vec3 max = { x + half, y + half, z + half };
    ecs_vec_t block;
    bool result = false;

    ecs_vec_init_t(NULL, &block, vec4, 0);
    test_assert(box_portal_block_pack(box_portal_index(world), vis, &block,
        BOX_PORTAL_BLOCK_MAX));
    result = box_portal_block_sees(ecs_vec_first_t(&block, vec4), min, max);
    ecs_vec_fini_t(NULL, &block, vec4);

    return result;
}

#endif

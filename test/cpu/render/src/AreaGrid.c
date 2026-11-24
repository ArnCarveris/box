/**
 * @file AreaGrid.c
 * @brief Tests of the probe grid of an area with LightBake that a mover carries: it covers the
 * area in the frame of the mover, stays as it is while the mover moves and turns, which only
 * sends the frame of the grid, and is placed again when the area changes.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "render.h"
#include "bake.h"

/** Create a world that renders with a null backend: a ship at (100, 0, 50), turned, that
 * carries a room of 6 by 3 by 4 with LightBake, a door, a camera and a number of boxes.
 *
 * @param null Receives the backend
 * @param children Boxes the ship carries besides the camera
 * @param ship Receives the ship
 * @param room Receives the room
 * @return The world
 */
static ecs_world_t* ag_world(
    box_render_null_t **null,
    int32_t children,
    ecs_entity_t *ship,
    ecs_entity_t *room)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(*null);
    box_render_set_backend(world, &backend);
    ecs_singleton_set(world, Scene, {.baked = true});
    test_int(ecs_script_run(world, "ship",
        "using box.transform\n"
        "using box.portal\n"
        "ship {\n"
        "  DynamicTransform\n"
        "  Position: {100, 0, 50}\n"
        "  Rotation: {0, 0.4, 0}\n"
        "  room {\n"
        "    VisArea: {shape: [{-3, -2}, {3, -2}, {3, 2}, {-3, 2}], y: 0, height: 3}\n"
        "  }\n"
        "  door {\n"
        "    Position: {0, 1.2, 2}\n"
        "    VisPortal: {kind: Door, size: {2, 2.4}, normal: {0, 0, 1}}\n"
        "  }\n"
        "}\n", NULL), 0);
    *ship = ecs_lookup(world, "ship");
    *room = ecs_lookup(world, "ship.room");
    ecs_set(world, *room, LightBake, {.probe_spacing = 1, .bounces = 1, .capture_size = 8,
        .budget_ms = 2, .compute = true});

    ecs_entity_t camera = ecs_new_w_pair(world, EcsChildOf, *ship);
    ecs_set(world, camera, Position, {0, 1.6f, 1});
    ecs_set(world, camera, Camera, {.fov = 60, .near_ = 0.1f});
    ecs_entity_t view = ecs_new(world);
    ecs_set(world, view, View, {.camera = camera, .lighting.enabled = true,
        .lighting.baked = true});

    if (children) {
        ecs_bulk_desc_t desc = {
            .count = children,
            .ids = { ecs_pair(EcsChildOf, *ship), ecs_id(Position), ecs_id(Box) }
        };
        const ecs_entity_t *boxes = ecs_bulk_init(world, &desc);
        for (int32_t i = 0; i < children; i ++) {
            ecs_set(world, boxes[i], Position, {(float)(i % 100) * 0.1f, 0.5f,
                -(float)(i / 100) * 0.1f});
            ecs_set(world, boxes[i], Box, {0.05f, 0.05f, 0.05f});
        }
    }

    return world;
}

/** Run a frame and wait until the backend has its packet.
 *
 * @param world The world
 */
static void ag_frame(
    ecs_world_t *world)
{
    ecs_progress(world, 1.0f / 60);
    box_render_sync(world);
}

/** Get the area grid of a room.
 *
 * @param world The world
 * @param room The room
 * @return The volume
 */
static const box_bake_volume_t* ag_volume(
    ecs_world_t *world,
    ecs_entity_t room)
{
    const box_bake_volume_t *result = box_bake_find_area(box_bake_state(world), room);
    test_assert(result != NULL);

    return result;
}

/** Count the descriptions of probe volumes of the last packet and their bytes.
 *
 * @param null The backend
 * @param frames Receives the descriptions that only send the frame of an area grid
 * @param bytes Receives the bytes of every description
 * @return The descriptions
 */
static int32_t ag_volume_resources(
    const box_render_null_t *null,
    int32_t *frames,
    int64_t *bytes)
{
    int32_t i, result = 0;
    *frames = 0;
    *bytes = 0;
    for (i = 0; i < ecs_vec_count(&null->last.resources); i ++) {
        const box_render_resource_t *r = ecs_vec_get_t(
            &null->last.resources, box_render_resource_t, i);
        if (r->kind != BOX_RENDER_VOLUME) {
            continue;
        }

        const box_render_volume_desc_t *desc = r->size
            ? box_render_packet_data(&null->last, r->data)
            : NULL;
        *frames += desc && desc->op == BOX_RENDER_VOLUME_FRAME ? 1 : 0;
        *bytes += r->size;
        result ++;
    }

    return result;
}

/** Move and turn the ship.
 *
 * @param world The world
 * @param ship The ship
 * @param i Step of the voyage
 */
static void ag_sail(
    ecs_world_t *world,
    ecs_entity_t ship,
    int32_t i)
{
    ecs_set(world, ship, Position, {100 + 20.0f * (float)i, 0, 50 - 7.0f * (float)i});
    ecs_set(world, ship, Rotation, {0, 0.4f + 0.3f * (float)i, 0});
}

void AreaGrid_grid_from_area(void) {
    // baked_lighting/area_grid
    /* The grid of the room lies on the lattice of the probe spacing in the frame of the ship,
     * over the outline of the room, every probe valid; its frame is the ship and the scene gets
     * no volume of its own. */
    box_render_null_t *null;
    ecs_entity_t ship, room;
    ecs_world_t *world = ag_world(&null, 0, &ship, &room);
    ag_frame(world);
    ag_frame(world);

    const box_bake_volume_t *v = ag_volume(world, room);
    test_assert(box_bake_find(box_bake_state(world), 0) == NULL);
    test_uint(v->mover, ship);
    test_flt(v->grid.spacing, 1);
    test_int(v->grid.size[0], 6);
    test_int(v->grid.size[1], 3);
    test_int(v->grid.size[2], 4);
    test_flt(v->grid.origin[0], -2.5f);
    test_flt(v->grid.origin[1], 0.5f);
    test_flt(v->grid.origin[2], -1.5f);
    test_int(v->probes, 72);
    test_int(v->valid_count, 72);
    test_int(ecs_get(world, room, LightBakeStatus)->probes, 72);
    test_assert(ecs_get(world, room, LightBakeStatus)->error == NULL);

    const WorldTransform *t = ecs_get(world, ship, WorldTransform);
    vec3 grid = {-2.5f, 0.5f, -1.5f}, placed, expect, back;
    glm_mat4_mulv3(ECS_CONST_CAST(vec4*, v->frame), grid, 1.0f, placed);
    glm_mat4_mulv3(ECS_CONST_CAST(vec4*, t->m), grid, 1.0f, expect);
    glm_mat4_mulv3(ECS_CONST_CAST(vec4*, v->rows), placed, 1.0f, back);
    test_assert(glm_vec3_distance(placed, expect) < 1e-4f);
    test_assert(glm_vec3_distance(back, grid) < 1e-4f);

    ecs_fini(world);
    box_render_null_free(null);
}

void AreaGrid_mover_moves_grid_kept(void) {
    // baked_lighting/area_grid
    /* While the ship moves and turns the grid, its validity and its handle stay as they are and
     * each frame sends only the frame of the grid, rows that take the ship to the origin. */
    box_render_null_t *null;
    ecs_entity_t ship, room;
    ecs_world_t *world = ag_world(&null, 0, &ship, &room);
    ag_frame(world);
    ag_frame(world);
    ag_frame(world);
    int32_t frames = 0;
    int64_t bytes = 0;
    test_int(ag_volume_resources(null, &frames, &bytes), 0);

    const box_bake_volume_t *v = ag_volume(world, room);
    box_bake_grid_t grid = v->grid;
    uint32_t handle = v->handle;
    int32_t regrids = v->regrids, valid = v->valid_count;
    for (int32_t i = 1; i <= 8; i ++) {
        ag_sail(world, ship, i);
        ag_frame(world);
        v = ag_volume(world, room);
        test_assert(!ecs_os_memcmp_t(&grid, &v->grid, box_bake_grid_t));
        test_uint(v->handle, handle);
        test_int(v->regrids, regrids);
        test_int(v->valid_count, valid);
        test_bool(v->sent, true);
        test_int(ag_volume_resources(null, &frames, &bytes), 1);
        test_int(frames, 1);

        const WorldTransform *t = ecs_get(world, ship, WorldTransform);
        vec3 origin;
        glm_mat4_mulv3(ECS_CONST_CAST(vec4*, v->rows), ECS_CONST_CAST(float*, t->m[3]), 1.0f,
            origin);
        test_assert(glm_vec3_norm(origin) < 1e-3f);
    }

    ag_frame(world);
    test_int(ag_volume_resources(null, &frames, &bytes), 0);
    ecs_fini(world);
    box_render_null_free(null);
}

void AreaGrid_area_edit_regrids(void) {
    // baked_lighting/area_grid
    /* A wider outline of the room places the grid again over it, on the same lattice. */
    box_render_null_t *null;
    ecs_entity_t ship, room;
    ecs_world_t *world = ag_world(&null, 0, &ship, &room);
    ag_frame(world);
    ag_frame(world);
    test_int(ag_volume(world, room)->probes, 72);

    test_int(ecs_script_run(world, "wider",
        "using box.portal\n"
        "ship {\n"
        "  room {\n"
        "    VisArea: {shape: [{-4, -2}, {4, -2}, {4, 2}, {-4, 2}], y: 0, height: 3}\n"
        "  }\n"
        "}\n", NULL), 0);
    ag_frame(world);
    const box_bake_volume_t *v = ag_volume(world, room);
    test_int(v->grid.size[0], 8);
    test_int(v->probes, 96);
    test_int(v->valid_count, 96);
    test_flt(v->grid.origin[0], -3.5f);
    ecs_fini(world);
    box_render_null_free(null);
}

/** Measure what a frame of a sailing ship costs with a number of children.
 *
 * @param children Boxes the ship carries
 * @param bytes Receives the bytes of the volume descriptions and the uploads of the draws of
 * the last frame
 * @param ms Receives the milliseconds of a frame, the median of 9
 */
static void ag_cost(
    int32_t children,
    int64_t *bytes,
    float *ms)
{
    box_render_null_t *null;
    ecs_entity_t ship, room;
    float times[9];
    int32_t frames = 0, i, j;
    int64_t volume = 0;
    ecs_world_t *world = ag_world(&null, children, &ship, &room);
    ag_frame(world);
    ag_frame(world);
    for (i = 0; i < 9; i ++) {
        ag_sail(world, ship, i + 1);
        ecs_time_t t = {0};
        ecs_time_measure(&t);
        ag_frame(world);
        times[i] = (float)(ecs_time_measure(&t) * 1000.0);
    }

    for (i = 0; i < 9; i ++) {
        for (j = i + 1; j < 9; j ++) {
            if (times[j] < times[i]) {
                float tmp = times[i];
                times[i] = times[j];
                times[j] = tmp;
            }
        }
    }

    box_render_draws_stats_t stats;
    box_render_draws_stats(world, &stats);
    test_int(ag_volume_resources(null, &frames, &volume), 1);
    test_int(frames, 1);
    *bytes = stats.upload_bytes + volume;
    *ms = times[4];
    test_int(ag_volume(world, room)->regrids, 0);
    ecs_fini(world);
    box_render_null_free(null);
}

void AreaGrid_cost_per_child(void) {
    // baked_lighting/area_grid
    /* A sailing ship costs its area grid one frame description per frame, whatever the number
     * of shapes it carries. */
    int64_t bytes[2];
    float ms[2];
    ag_cost(1000, &bytes[0], &ms[0]);
    ag_cost(10000, &bytes[1], &ms[1]);
    printf("sailing area grid every frame: 1k children %lld bytes %.3f ms, "
        "10k children %lld bytes %.3f ms\n",
        (long long)bytes[0], ms[0], (long long)bytes[1], ms[1]);
    test_assert(bytes[0] == bytes[1]);
}

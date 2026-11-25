/**
 * @file TerrainWalk.c
 * @brief Controllers on the height field of a Terrain, driven by synthetic input: a first person
 * controller walks up a slope, stops at a cliff steeper than it can stand on and lands on the
 * terrain instead of its ground plane, and a colliding camera does not pass through it.
 *
 * The null backend computes no image, so the tests store the copies of the query tiles of the
 * terrain themselves every frame, the values of a known height function at their texels.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "texture.h"
#include "terrain.h"

/** A height function of x and z */
typedef float (*tw_fn_t)(
    double x,
    double z);

/** Flat up to z = -2, then rising half a meter per meter towards -z.
 *
 * @param x X
 * @param z Z
 * @return The height
 */
static float tw_slope(
    double x,
    double z)
{
    (void)x;
    return (float)(0.5 * fmax(0.0, -z - 2.0));
}

/** Flat up to z = -4, then a cliff 6 m high rising 4 m per meter towards -z.
 *
 * @param x X
 * @param z Z
 * @return The height
 */
static float tw_cliff(
    double x,
    double z)
{
    (void)x;
    return (float)fmin(fmax((-z - 4.0) * 4.0, 0.0), 6.0);
}

/** A flat ground at 3 m.
 *
 * @param x X
 * @param z Z
 * @return The height
 */
static float tw_flat(
    double x,
    double z)
{
    (void)x;
    (void)z;
    return 3.0f;
}

/** The function of the terrain of a test */
static tw_fn_t tw_fn;

/** Create a terrain of a constant height node whose copies tw_fill stores.
 *
 * @param world The world
 * @param fn The height function
 * @return The terrain
 */
static ecs_entity_t tw_terrain(
    ecs_world_t *world,
    tw_fn_t fn)
{
    ecs_entity_t node = ecs_new(world);
    ecs_set(world, node, TexConstant, {.value = {0, 0, 0, 0}});
    ecs_entity_t terrain = ecs_new(world);
    ecs_set(world, terrain, Terrain, {.height = node, .tile_size = 16, .resolution = 17,
        .levels = 2, .budget = 1000, .query_radius = 24});
    tw_fn = fn;

    return terrain;
}

/** Store the copies of the query tiles of a terrain that have none.
 *
 * @param world The world
 * @param terrain The terrain
 */
static void tw_fill(
    ecs_world_t *world,
    ecs_entity_t terrain)
{
    int32_t count, i, j, k;
    box_terrain_query_tile_t *const *tiles = box_terrain_query_tiles(world, terrain, &count);
    for (k = 0; k < count; k ++) {
        const box_terrain_query_tile_t *t = tiles[k];
        box_texture_copy_t copy;
        int32_t n = t->texels;
        if (box_texture_readback(world, t->heights, &copy)) {
            continue;
        }

        float *data = ecs_os_malloc_n(float, n * n);
        for (j = 0; j < n; j ++) {
            for (i = 0; i < n; i ++) {
                data[j * n + i] = tw_fn(t->window[0] + (double)i * t->window[2] / (n - 1),
                    t->window[1] + (double)j * t->window[2] / (n - 1));
            }
        }

        box_texture_readback_store(box_texture_readback_ensure(box_textures_get(world),
            t->heights), data, n, n, ReadbackR32Float, 1, 0);
    }
}

/** Run frames of 0.1 s, storing the copies of new query tiles after each.
 *
 * @param world The world
 * @param terrain The terrain
 * @param frames Number of frames
 */
static void tw_run(
    ecs_world_t *world,
    ecs_entity_t terrain,
    int32_t frames)
{
    int32_t i;
    for (i = 0; i < frames; i ++) {
        ecs_progress(world, 0.1f);
        tw_fill(world, terrain);
    }
}

/** Create a first person controller that the terrain follows.
 *
 * @param world The world
 * @param y Height of the eye
 * @return The controlled entity
 */
static ecs_entity_t tw_walker(
    ecs_world_t *world,
    float y)
{
    ecs_entity_t fp = ecs_new(world);
    ecs_set(world, fp, Position, { 0, y, 0 });
    ecs_add(world, fp, FirstPersonController);
    ecs_add_id(world, fp, WorldFocus);

    return fp;
}

void TerrainWalk_walks_up_slope(void) {
    // controllers/collision
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t terrain = tw_terrain(world, tw_slope);
    ecs_entity_t fp = tw_walker(world, 1.7f);
    tw_run(world, terrain, 2);

    box_input_key(world, KeyW, true);
    tw_run(world, terrain, 20);
    const Position *p = ecs_get(world, fp, Position);
    float h = tw_slope(p->x, p->z);
    test_assert(p->z < -6.0f);
    test_assert(h > 2.0f);
    test_assert(fabs(p->y - 1.7f - h) < 0.1f);
    test_bool(ecs_get(world, fp, FirstPersonController)->grounded, true);

    box_input_key(world, KeyW, false);
    tw_run(world, terrain, 5);
    float y = ecs_get(world, fp, Position)->y;
    tw_run(world, terrain, 5);
    test_assert(fabs(ecs_get(world, fp, Position)->y - y) < 0.01f);

    ecs_fini(world);
}

void TerrainWalk_stops_at_cliff(void) {
    // controllers/collision
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t terrain = tw_terrain(world, tw_cliff);
    ecs_entity_t fp = tw_walker(world, 1.7f);
    tw_run(world, terrain, 2);

    box_input_key(world, KeyW, true);
    tw_run(world, terrain, 30);
    const Position *p = ecs_get(world, fp, Position);
    test_assert(p->z > -4.3f);
    test_assert(p->z < -3.0f);
    test_assert(p->y < 2.2f);
    test_bool(ecs_get(world, fp, FirstPersonController)->grounded, true);

    ecs_fini(world);
}

void TerrainWalk_climbs_cliff(void) {
    // controllers/collision
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t terrain = tw_terrain(world, tw_cliff);
    ecs_entity_t fp = tw_walker(world, 1.7f);
    ecs_get_mut(world, fp, FirstPersonController)->climb = 20;
    tw_run(world, terrain, 2);

    box_input_key(world, KeyW, true);
    for (int32_t i = 0; i < 600; i ++) {
        box_input_key(world, KeySpace, (i % 2) == 0);
        ecs_progress(world, 1.0f / 60.0f);
        tw_fill(world, terrain);
    }

    box_input_key(world, KeySpace, false);
    tw_run(world, terrain, 5);
    const Position *p = ecs_get(world, fp, Position);
    test_assert(p->z < -6.0f);
    test_assert(fabs(p->y - 7.7f) < 0.1f);
    const FirstPersonController *c = ecs_get(world, fp, FirstPersonController);
    test_bool(c->grounded, true);
    test_int(c->climbed, 0);

    ecs_fini(world);
}

void TerrainWalk_lands_on_terrain(void) {
    // controllers/collision
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t terrain = tw_terrain(world, tw_flat);
    ecs_entity_t fp = tw_walker(world, 20);
    tw_run(world, terrain, 40);
    const Position *p = ecs_get(world, fp, Position);
    test_assert(fabsf(p->y - 4.7f) < 0.05f);
    test_bool(ecs_get(world, fp, FirstPersonController)->grounded, true);

    ecs_fini(world);
}

void TerrainWalk_camera_collides(void) {
    // controllers/collision
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t terrain = tw_terrain(world, tw_flat);
    ecs_entity_t cam = ecs_new(world);
    ecs_set(world, cam, Position, { 0, 6, 0 });
    ecs_add(world, cam, CameraController);
    ecs_get_mut(world, cam, CameraController)->collide = true;
    ecs_add_id(world, cam, WorldFocus);
    ecs_entity_t ghost = ecs_new(world);
    ecs_set(world, ghost, Position, { 5, 6, 0 });
    ecs_add(world, ghost, CameraController);
    tw_run(world, terrain, 2);

    box_input_key(world, KeyQ, true);
    tw_run(world, terrain, 30);
    const CameraController *cc = ecs_get(world, cam, CameraController);
    const Position *p = ecs_get(world, cam, Position);
    test_assert(p->y > 3 + cc->radius - 0.01f);
    test_assert(p->y < 3 + cc->radius + 0.1f);
    test_assert(ecs_get(world, ghost, Position)->y < 3);

    ecs_fini(world);
}

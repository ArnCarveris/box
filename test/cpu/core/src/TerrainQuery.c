/**
 * @file TerrainQuery.c
 * @brief Tests for the CPU queries of a Terrain without a GPU: the query tiles around the focus
 * and their windows, heights, normals, slopes and masks read from synthetic copies of a known
 * function, queries that fail where nothing is resident, the coarse level beyond the radius,
 * bounded terrains, requests beyond the radius, the frame of a terrain with a Position, rays
 * and the script functions.
 *
 * The null backend computes no image, so the tests store the copy of each query tile
 * themselves with box_texture_readback_store, the values of the function at its texels.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include "texture.h"
#include "terrain.h"

/** Difference a queried value may have from its expected value */
#define TQ_EPS (1e-3f)

/** A height function of x and z */
typedef float (*tq_fn_t)(
    double x,
    double z);

/** A tilted plane: 0.25 x + 0.5 z + 3, which bilinear sampling reproduces exactly.
 *
 * @param x X
 * @param z Z
 * @return The height
 */
static float tq_plane(
    double x,
    double z)
{
    return (float)(0.25 * x + 0.5 * z + 3.0);
}

/** A flat ground at 3 m.
 *
 * @param x X
 * @param z Z
 * @return The height
 */
static float tq_flat(
    double x,
    double z)
{
    (void)x;
    (void)z;
    return 3.0f;
}

/** Create a world with a focus at the origin and a terrain of a constant height node.
 *
 * @param terrain Receives the terrain
 * @param masks Whether the terrain has a masks node
 * @param extent Half size of a bounded terrain, 0 for unbounded
 * @return The world
 */
static ecs_world_t* tq_world(
    ecs_entity_t *terrain,
    bool masks,
    float extent)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t focus = ecs_new(world);
    ecs_set(world, focus, Position, {0, 10, 0});
    ecs_add_id(world, focus, WorldFocus);
    ecs_entity_t node = ecs_new(world);
    ecs_set(world, node, TexConstant, {.value = {5, 5, 5, 5}});
    ecs_entity_t mask = 0;
    if (masks) {
        mask = ecs_new(world);
        ecs_set(world, mask, TexConstant, {.value = {0.1f, 0.2f, 0.3f, 0.4f}});
    }

    *terrain = ecs_new(world);
    ecs_set(world, *terrain, Terrain, {.height = node, .tile_size = 16, .resolution = 17,
        .levels = 2, .budget = 1000, .masks = mask, .query_spacing = 0.5f,
        .query_radius = 40, .extent = extent});

    return world;
}

/** Store the copy of a window of a query tile: the height function in a R32Float image, or
 * the masks (x / 100, z / 100, 0.5, 1) in a RGBA32Float image.
 *
 * @param world The world
 * @param t The tile
 * @param masks Store the masks window
 * @param fn The height function
 */
static void tq_store(
    ecs_world_t *world,
    const box_terrain_query_tile_t *t,
    bool masks,
    tq_fn_t fn)
{
    int32_t n = t->texels, i, j, c = masks ? 4 : 1;
    float *data = ecs_os_malloc_n(float, n * n * c);
    for (j = 0; j < n; j ++) {
        for (i = 0; i < n; i ++) {
            double x = t->window[0] + (double)i * t->window[2] / (double)(n - 1);
            double z = t->window[1] + (double)j * t->window[2] / (double)(n - 1);
            float *v = &data[(j * n + i) * c];
            if (masks) {
                v[0] = (float)(x / 100.0);
                v[1] = (float)(z / 100.0);
                v[2] = 0.5f;
                v[3] = 1.0f;
            } else {
                v[0] = fn(x, z);
            }
        }
    }

    box_texture_readback_t *rb = box_texture_readback_ensure(box_textures_get(world),
        masks ? t->masks : t->heights);
    box_texture_readback_store(rb, data, n, n,
        masks ? ReadbackRGBA32Float : ReadbackR32Float, 1, 0);
}

/** Store the copies of every query tile of a terrain, optionally only those of a level.
 *
 * @param world The world
 * @param terrain The terrain
 * @param level The level, -1 for all
 * @param fn The height function
 */
static void tq_fill(
    ecs_world_t *world,
    ecs_entity_t terrain,
    int32_t level,
    tq_fn_t fn)
{
    int32_t count, i;
    box_terrain_query_tile_t *const *tiles = box_terrain_query_tiles(world, terrain, &count);
    for (i = 0; i < count; i ++) {
        if (level >= 0 && tiles[i]->level != level) {
            continue;
        }

        tq_store(world, tiles[i], false, fn);
        if (tiles[i]->masks) {
            tq_store(world, tiles[i], true, fn);
        }
    }
}

void TerrainQuery_tiles_around_focus(void) {
    ecs_entity_t terrain;
    ecs_world_t *world = tq_world(&terrain, false, 0);
    ecs_progress(world, 0);
    int32_t count, i, fine = 0;
    box_terrain_query_tile_t *const *tiles = box_terrain_query_tiles(world, terrain, &count);
    test_assert(tiles != NULL);
    for (i = 0; i < count; i ++) {
        const box_terrain_query_tile_t *t = tiles[i];
        double edge = 0.5 * BOX_TERRAIN_QUERY_CELLS * (t->level ? BOX_TERRAIN_QUERY_SCALE : 1);
        double reach = 40.0 * (t->level ? BOX_TERRAIN_QUERY_SCALE : 1);
        double x0 = t->ix * edge, z0 = t->iz * edge;
        double dx = fmax(fmax(x0, -(x0 + edge)), 0), dz = fmax(fmax(z0, -(z0 + edge)), 0);
        test_assert(sqrt(dx * dx + dz * dz) <= reach);
        test_assert(t->masks == 0);
        test_int(t->texels, BOX_TERRAIN_QUERY_CELLS + 1 + 2 * BOX_TERRAIN_QUERY_BORDER);
        const TextureGraph *g = ecs_get(world, t->heights, TextureGraph);
        test_assert(g != NULL);
        test_int(g->domain, DomainWorld);
        test_int(g->format, GraphFloat);
        test_int(g->width, t->texels);
        test_assert(fabs(g->window.x - (x0 - edge / BOX_TERRAIN_QUERY_CELLS)) < 1e-9);
        test_assert(fabs(g->window.z - (z0 - edge / BOX_TERRAIN_QUERY_CELLS)) < 1e-9);
        test_assert(fabs(g->window.width - edge * (BOX_TERRAIN_QUERY_CELLS + 2) /
            BOX_TERRAIN_QUERY_CELLS) < 1e-4);
        test_assert(ecs_has(world, t->heights, TextureReadback));
        fine += t->level == 0;
    }

    /* A 32 m tile within 40 m of the origin: columns and rows -2..1 less the corners */
    test_int(fine, 12);
    const TerrainQueryStatus *status = ecs_get(world, terrain, TerrainQueryStatus);
    test_assert(status != NULL);
    test_int(status->tiles, count);
    test_int(status->resident, 0);
    test_int(status->pending, count);
    ecs_fini(world);
}

void TerrainQuery_unknown_fails(void) {
    ecs_entity_t terrain;
    ecs_world_t *world = tq_world(&terrain, false, 0);
    float h = 7;
    vec3 n;
    test_bool(box_terrain_height(world, terrain, 1, 1, &h), false);
    ecs_progress(world, 0);
    test_bool(box_terrain_height(world, terrain, 1, 1, &h), false);
    test_flt(h, 0);
    test_bool(box_terrain_normal(world, terrain, 1, 1, n), false);
    test_flt(n[1], 1);
    test_bool(box_terrain_resident(world, terrain, 0, 0, 1, 1), false);
    test_bool(box_terrain_height(world, 0, 1, 1, &h), false);
    ecs_fini(world);
}

void TerrainQuery_height_bilinear(void) {
    ecs_entity_t terrain;
    ecs_world_t *world = tq_world(&terrain, false, 0);
    ecs_progress(world, 0);
    tq_fill(world, terrain, -1, tq_plane);
    static const double points[][2] = {
        {0, 0}, {1.3, -2.7}, {-31.99, 5.25}, {32, 32}, {-0.01, 0.01}, {15.75, -17.125}
    };
    int32_t i;
    for (i = 0; i < 6; i ++) {
        float h = 0;
        test_bool(box_terrain_height(world, terrain, points[i][0], points[i][1], &h), true);
        test_assert(fabsf(h - tq_plane(points[i][0], points[i][1])) < TQ_EPS);
    }

    vec3 n, e = { -0.25f, 1, -0.5f };
    glm_vec3_normalize(e);
    test_bool(box_terrain_normal(world, terrain, 3.3, -4.1, n), true);
    test_assert(fabsf(n[0] - e[0]) < TQ_EPS);
    test_assert(fabsf(n[1] - e[1]) < TQ_EPS);
    test_assert(fabsf(n[2] - e[2]) < TQ_EPS);
    float slope = 0;
    test_bool(box_terrain_slope(world, terrain, 3.3, -4.1, &slope), true);
    test_assert(fabsf(slope - acosf(e[1])) < TQ_EPS);
    test_bool(box_terrain_resident(world, terrain, -20, -20, 20, 20), true);
    vec4 m;
    test_bool(box_terrain_mask(world, terrain, 1, 1, m), false);

    ecs_progress(world, 0);
    const TerrainQueryStatus *status = ecs_get(world, terrain, TerrainQueryStatus);
    test_int(status->resident, status->tiles);
    test_int(status->pending, 0);
    test_assert(status->cpu_bytes > 0);
    ecs_fini(world);
}

void TerrainQuery_coarse_beyond_radius(void) {
    ecs_entity_t terrain;
    ecs_world_t *world = tq_world(&terrain, false, 0);
    ecs_progress(world, 0);
    tq_fill(world, terrain, 1, tq_plane);
    float h = 0;
    test_bool(box_terrain_height(world, terrain, 200, -100, &h), true);
    test_assert(fabsf(h - tq_plane(200, -100)) < TQ_EPS);
    test_bool(box_terrain_resident(world, terrain, 199, -101, 201, -99), false);
    test_bool(box_terrain_height(world, terrain, 2000, 0, &h), false);

    /* Only the coarse level answers near the focus too until the fine one is resident */
    test_bool(box_terrain_resident(world, terrain, 0, 0, 1, 1), false);
    test_bool(box_terrain_height(world, terrain, 1, 1, &h), true);
    tq_fill(world, terrain, 0, tq_flat);
    test_bool(box_terrain_height(world, terrain, 1, 1, &h), true);
    test_flt(h, 3);
    test_bool(box_terrain_resident(world, terrain, 0, 0, 1, 1), true);
    ecs_fini(world);
}

void TerrainQuery_masks(void) {
    ecs_entity_t terrain;
    ecs_world_t *world = tq_world(&terrain, true, 0);
    ecs_progress(world, 0);
    int32_t count, i;
    box_terrain_query_tile_t *const *tiles = box_terrain_query_tiles(world, terrain, &count);
    test_assert(count > 0);
    for (i = 0; i < count; i ++) {
        test_assert(tiles[i]->masks != 0);
        const TextureGraph *g = ecs_get(world, tiles[i]->masks, TextureGraph);
        test_assert(g != NULL);
        test_int(g->format, GraphFloat4);
        test_assert(ecs_has(world, tiles[i]->masks, TextureReadback));
    }

    /* Heights alone are not resident while the masks are not */
    for (i = 0; i < count; i ++) {
        tq_store(world, tiles[i], false, tq_flat);
    }

    test_bool(box_terrain_resident(world, terrain, 0, 0, 1, 1), false);
    vec4 m;
    test_bool(box_terrain_mask(world, terrain, 10, -20, m), false);
    tq_fill(world, terrain, -1, tq_flat);
    test_bool(box_terrain_resident(world, terrain, 0, 0, 1, 1), true);
    test_bool(box_terrain_mask(world, terrain, 10, -20, m), true);
    test_assert(fabsf(m[0] - 0.1f) < TQ_EPS);
    test_assert(fabsf(m[1] + 0.2f) < TQ_EPS);
    test_assert(fabsf(m[2] - 0.5f) < TQ_EPS);
    test_assert(fabsf(m[3] - 1.0f) < TQ_EPS);
    ecs_fini(world);
}

void TerrainQuery_bounded(void) {
    ecs_entity_t terrain;
    ecs_world_t *world = tq_world(&terrain, false, 20);
    ecs_progress(world, 0);
    int32_t count, i;
    box_terrain_query_tile_t *const *tiles = box_terrain_query_tiles(world, terrain, &count);
    for (i = 0; i < count; i ++) {
        test_assert(tiles[i]->level == 0 || tiles[i]->level == 1);
        double edge = 0.5 * BOX_TERRAIN_QUERY_CELLS *
            (tiles[i]->level ? BOX_TERRAIN_QUERY_SCALE : 1);
        test_assert(tiles[i]->ix * edge < 20 && (tiles[i]->ix + 1) * edge > -20);
        test_assert(tiles[i]->iz * edge < 20 && (tiles[i]->iz + 1) * edge > -20);
    }

    tq_fill(world, terrain, -1, tq_flat);
    float h = 0;
    test_bool(box_terrain_height(world, terrain, 19.5, -19.5, &h), true);
    test_flt(h, 3);
    test_bool(box_terrain_height(world, terrain, 20.5, 0, &h), false);

    /* Outside the extent there is nothing to wait for */
    test_bool(box_terrain_resident(world, terrain, 30, 30, 40, 40), true);
    test_bool(box_terrain_resident(world, terrain, 10, 10, 40, 40), true);
    ecs_fini(world);
}

void TerrainQuery_request_beyond_radius(void) {
    ecs_entity_t terrain;
    ecs_world_t *world = tq_world(&terrain, false, 0);
    ecs_progress(world, 0);
    test_bool(box_terrain_resident(world, terrain, 300, 300, 310, 310), false);
    box_terrain_request(world, terrain, 300, 300, 310, 310);
    ecs_progress(world, 0);
    const TerrainQueryStatus *status = ecs_get(world, terrain, TerrainQueryStatus);
    test_int(status->requested, 1);
    int32_t count, i;
    const box_terrain_query_tile_t *found = NULL;
    box_terrain_query_tile_t *const *tiles = box_terrain_query_tiles(world, terrain, &count);
    for (i = 0; i < count; i ++) {
        if (tiles[i]->level == 0 && tiles[i]->ix == 9 && tiles[i]->iz == 9) {
            found = tiles[i];
        }
    }

    test_assert(found != NULL);
    tq_store(world, found, false, tq_plane);
    test_bool(box_terrain_resident(world, terrain, 300, 300, 310, 310), true);
    float h = 0;
    test_bool(box_terrain_height(world, terrain, 305, 301, &h), true);
    test_assert(fabsf(h - tq_plane(305, 301)) < TQ_EPS);

    /* Without new requests the tile goes */
    for (i = 0; i < 12; i ++) {
        ecs_progress(world, 0);
    }

    test_bool(box_terrain_resident(world, terrain, 300, 300, 310, 310), false);
    status = ecs_get(world, terrain, TerrainQueryStatus);
    test_int(status->requested, 0);
    ecs_fini(world);
}

void TerrainQuery_terrain_position(void) {
    ecs_entity_t terrain;
    ecs_world_t *world = tq_world(&terrain, false, 0);
    ecs_set(world, terrain, Position, {100, 2, -50});
    ecs_progress(world, 0);
    tq_fill(world, terrain, -1, tq_plane);
    double o[3];
    box_terrain_origin(world, terrain, o);
    test_assert(fabs(o[0] - 100) < 1e-9);
    test_assert(fabs(o[1] - 2) < 1e-9);
    test_assert(fabs(o[2] + 50) < 1e-9);
    float y = 0;
    vec3 n;
    test_bool(box_terrain_ground(world, terrain, 105, -48, &y, n), true);
    test_assert(fabsf(y - (2 + tq_plane(5, 2))) < TQ_EPS);
    test_assert(n[1] > 0.8f && n[1] < 0.9f);
    ecs_fini(world);
}

void TerrainQuery_ray(void) {
    ecs_entity_t terrain;
    ecs_world_t *world = tq_world(&terrain, false, 0);
    ecs_progress(world, 0);
    ecs_entity_t hit = 0;
    float t = 0;
    test_bool(box_terrain_ray(world, 0, (vec3){5, 20, 5}, (vec3){0, -1, 0}, 0, &hit, &t),
        false);
    tq_fill(world, terrain, -1, tq_flat);
    test_bool(box_terrain_ray(world, 0, (vec3){5, 20, 5}, (vec3){0, -2, 0}, 0, &hit, &t), true);
    test_uint(hit, terrain);
    test_assert(fabsf(t - 17) < TQ_EPS);

    /* A slanted ray hits where it crosses 3 m */
    vec3 dir = {1, -1, 0};
    test_bool(box_terrain_ray(world, 0, (vec3){-10, 13, 2}, dir, 0, &hit, &t), true);
    test_assert(fabsf(t - 10.0f * sqrtf(2.0f)) < 0.01f);

    /* Too short, pointing up and from another scene miss */
    test_bool(box_terrain_ray(world, 0, (vec3){5, 20, 5}, (vec3){0, -1, 0}, 10, &hit, &t),
        false);
    test_bool(box_terrain_ray(world, 0, (vec3){5, 20, 5}, (vec3){0, 1, 0}, 0, &hit, &t), false);
    ecs_entity_t scene = ecs_new(world);
    ecs_add(world, scene, Scene);
    test_bool(box_terrain_ray(world, scene, (vec3){5, 20, 5}, (vec3){0, -1, 0}, 0, &hit, &t),
        false);

    /* A ray from below comes out first */
    test_bool(box_terrain_ray(world, 0, (vec3){5, 0, 5}, (vec3){0, 1, 0}, 0, &hit, &t), false);
    ecs_fini(world);
}

void TerrainQuery_script(void) {
    ecs_entity_t terrain;
    ecs_world_t *world = tq_world(&terrain, true, 0);
    ecs_set_name(world, terrain, "hills");
    ecs_progress(world, 0);
    tq_fill(world, terrain, -1, tq_plane);
    test_assert(ecs_script_run(world, "terrain_query_script",
        "using box.transform\n"
        "probe {\n"
        "  Position: {\n"
        "    x: box.terrain.height(hills, 4, -2),\n"
        "    y: box.terrain.normal_y(hills, 4, -2),\n"
        "    z: box.terrain.mask(hills, 10, 30, 1)\n"
        "  }\n"
        "  Scale: {\n"
        "    x: box.terrain.slope(hills, 4, -2),\n"
        "    y: box.terrain.height(hills, 5000, 0),\n"
        "    z: box.terrain.normal_x(hills, 4, -2)\n"
        "  }\n"
        "}\n"
        "known {\n"
        "  box.particle.Collision: {\n"
        "    ground: box.terrain.resident(hills, 1, 1),\n"
        "    die: box.terrain.known(hills, 5000, 0),\n"
        "    height: box.terrain.normal_z(hills, 4, -2)\n"
        "  }\n"
        "}\n", NULL) == 0);
    vec3 e = { -0.25f, 1, -0.5f };
    glm_vec3_normalize(e);
    const Position *p = ecs_get(world, ecs_lookup(world, "probe"), Position);
    test_assert(p != NULL);
    test_assert(fabsf(p->x - tq_plane(4, -2)) < TQ_EPS);
    test_assert(fabsf(p->y - e[1]) < TQ_EPS);
    test_assert(fabsf(p->z - 0.3f) < TQ_EPS);
    const Scale *s = ecs_get(world, ecs_lookup(world, "probe"), Scale);
    test_assert(s != NULL);
    test_assert(fabsf(s->x - acosf(e[1])) < TQ_EPS);
    test_flt(s->y, 0);
    test_assert(fabsf(s->z - e[0]) < TQ_EPS);
    const ParticleCollision *k = ecs_get(world, ecs_lookup(world, "known"),
        ParticleCollision);
    test_assert(k != NULL);
    test_bool(k->ground, true);
    test_bool(k->die, false);
    test_assert(fabsf(k->height - e[2]) < TQ_EPS);
    ecs_fini(world);
}

void TerrainQuery_remove_deletes_tiles(void) {
    ecs_entity_t terrain;
    ecs_world_t *world = tq_world(&terrain, true, 0);
    ecs_progress(world, 0);
    int32_t count;
    box_terrain_query_tile_t *const *tiles = box_terrain_query_tiles(world, terrain, &count);
    test_assert(count > 0);
    ecs_entity_t heights = tiles[0]->heights, masks = tiles[0]->masks;
    ecs_remove(world, terrain, Terrain);
    test_assert(!ecs_is_alive(world, heights));
    test_assert(!ecs_is_alive(world, masks));
    test_assert(box_terrain_query_tiles(world, terrain, &count) == NULL);
    test_int(count, 0);
    test_assert(!ecs_has(world, terrain, TerrainQueryStatus));
    ecs_fini(world);
}

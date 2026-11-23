/**
 * @file Scatter.c
 * @brief Scatter: counts that follow the density, the spacing, the mask, slope and height
 * filters, cells that join without seams or duplicates, determinism, avoid, re-evaluation, the
 * bulk created instances, ChunkGrid cells from script and a Terrain through its queries.
 *
 * The terrains are synthetic: the null backend computes no image, so the tests store the copy
 * of each query tile themselves, the values of a height and a mask function at its texels.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "texture.h"
#include "terrain.h"

/** Instances a test collects at most */
#define SC_MAX (65536)

/** An instance of a scatter in absolute coordinates */
typedef struct sc_inst_t {
    double x;              /**< Absolute X */
    double y;              /**< Absolute Y */
    double z;              /**< Absolute Z */
    Rotation r;            /**< Rotation */
    Scale s;               /**< Scale */
    ecs_entity_t prefab;   /**< The prefab it is an instance of */
} sc_inst_t;

/** A height or mask function of x and z */
typedef float (*sc_fn_t)(
    double x,
    double z);

/** Height function of the synthetic terrain */
static sc_fn_t sc_height_fn;

/** Function of mask channel 0 of the synthetic terrain, the others 0 */
static sc_fn_t sc_mask_fn;

/** Create a world with box and the prefabs and template of the tests.
 *
 * @return The world
 */
static ecs_world_t* sc_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    char *path = box_app_locate("test/assets/world/scatter.flecs");
    test_assert(path != NULL);
    test_assert(ecs_script_run_file(world, path) == 0);
    ecs_os_free(path);

    return world;
}

/** Get a prefab of the test asset.
 *
 * @param world The world
 * @param name Name of the prefab
 * @return The prefab
 */
static ecs_entity_t sc_prefab(
    ecs_world_t *world,
    const char *name)
{
    ecs_entity_t result = ecs_lookup(world, name);
    test_assert(result != 0);

    return result;
}

/** Create a scatter over an explicit extent at a position.
 *
 * @param world The world
 * @param parent The parent, 0 for a root
 * @param x X of the corner
 * @param y Y of the entity
 * @param z Z of the corner
 * @param config The scatter
 * @return The scatter entity
 */
static ecs_entity_t sc_scatter(
    ecs_world_t *world,
    ecs_entity_t parent,
    float x,
    float y,
    float z,
    const Scatter *config)
{
    ecs_entity_t e = parent ? ecs_new_w_pair(world, EcsChildOf, parent) : ecs_new(world);
    ecs_set(world, e, Position, {x, y, z});
    ecs_set_ptr(world, e, Scatter, config);

    return e;
}

/** Create a cell entity as a ChunkGrid does, with a scatter child at its corner.
 *
 * @param world The world
 * @param cx X of the cell
 * @param cz Z of the cell
 * @param size Edge of the cell
 * @param config The scatter
 * @param scatter Receives the scatter entity
 * @return The cell entity
 */
static ecs_entity_t sc_cell(
    ecs_world_t *world,
    int32_t cx,
    int32_t cz,
    float size,
    const Scatter *config,
    ecs_entity_t *scatter)
{
    ecs_entity_t cell = ecs_new(world);
    ecs_set(world, cell, ChunkGridCell, {.cell = {cx, 0, cz}, .size = {size, 0, size}});
    ecs_set(world, cell, Position, {(float)cx * size, 0, (float)cz * size});
    *scatter = sc_scatter(world, cell, 0, 0, 0, config);

    return cell;
}

/** Get a scatter config with defaults and one or two prefabs.
 *
 * @param a The first prefab
 * @param b The second prefab, 0 for none
 * @param density The density
 * @param spacing The spacing
 * @param seed The seed
 * @return The config
 */
static Scatter sc_config(
    ecs_entity_t a,
    ecs_entity_t b,
    float density,
    float spacing,
    int32_t seed)
{
    Scatter result;
    ecs_os_memset_t(&result, 0, Scatter);
    result.items[0] = (ScatterItem){ a, 1 };
    result.items[1] = (ScatterItem){ b, b ? 1.0f : 0.0f };
    result.density = density;
    result.spacing = spacing;
    result.seed = seed;
    result.mask_channel = -1;
    result.mask_min = 0.5f;
    result.mask_max = 1;
    result.slope_max = 90;
    result.yaw = 360;
    result.scale_min = 1;
    result.scale_max = 1;

    return result;
}

/** Collect the instances of a scatter in absolute coordinates.
 *
 * @param world The world
 * @param scatter The scatter entity
 * @param ax Absolute X of the scatter entity
 * @param ay Absolute Y of the scatter entity
 * @param az Absolute Z of the scatter entity
 * @param out Receives the instances after count of them
 * @param count Instances in out, incremented
 */
static void sc_collect(
    ecs_world_t *world,
    ecs_entity_t scatter,
    double ax,
    double ay,
    double az,
    sc_inst_t *out,
    int32_t *count)
{
    int32_t i;
    ecs_iter_t it = ecs_children(world, scatter);
    while (ecs_children_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            ecs_entity_t e = it.entities[i];
            const Position *p = ecs_get(world, e, Position);
            const Rotation *r = ecs_get(world, e, Rotation);
            const Scale *s = ecs_get(world, e, Scale);
            test_assert(p != NULL && r != NULL && s != NULL);
            test_assert(*count < SC_MAX);
            out[*count] = (sc_inst_t){
                .x = ax + p->x, .y = ay + p->y, .z = az + p->z, .r = *r, .s = *s,
                .prefab = ecs_get_target(world, e, EcsIsA, 0)
            };
            (*count) ++;
        }
    }
}

/** Order instances by X, then Z.
 *
 * @param a The first instance
 * @param b The second instance
 * @return Order of a relative to b
 */
static int sc_compare(
    const void *a,
    const void *b)
{
    const sc_inst_t *ia = a, *ib = b;
    int result = (ia->x > ib->x) - (ia->x < ib->x);
    if (!result) {
        result = (ia->z > ib->z) - (ia->z < ib->z);
    }

    return result;
}

/** Get the least horizontal distance between two instances, brute force.
 *
 * @param a The instances
 * @param count Number of them
 * @return The distance, INFINITY for fewer than two
 */
static double sc_min_distance(
    const sc_inst_t *a,
    int32_t count)
{
    double result = INFINITY;
    int32_t i, j;
    for (i = 0; i < count; i ++) {
        for (j = i + 1; j < count; j ++) {
            double dx = a[i].x - a[j].x, dz = a[i].z - a[j].z;
            double d = sqrt(dx * dx + dz * dz);
            result = d < result ? d : result;
        }
    }

    return result;
}

/** Count the children of an entity.
 *
 * @param world The world
 * @param e The entity
 * @return Number of children
 */
static int32_t sc_children(
    ecs_world_t *world,
    ecs_entity_t e)
{
    int32_t result = 0;
    ecs_iter_t it = ecs_children(world, e);
    while (ecs_children_next(&it)) {
        result += it.count;
    }

    return result;
}

/** Create a focus and a terrain of a constant height node, with a masks node, whose query
 * tiles the tests fill with sc_height_fn and sc_mask_fn.
 *
 * @param world The world
 * @return The terrain
 */
static ecs_entity_t sc_terrain(
    ecs_world_t *world)
{
    ecs_entity_t focus = ecs_new(world);
    ecs_set(world, focus, Position, {0, 10, 0});
    ecs_add_id(world, focus, WorldFocus);
    ecs_entity_t node = ecs_new(world);
    ecs_set(world, node, TexConstant, {.value = {0, 0, 0, 0}});
    ecs_entity_t mask = ecs_new(world);
    ecs_set(world, mask, TexConstant, {.value = {0, 0, 0, 0}});
    ecs_entity_t result = ecs_new(world);
    ecs_set(world, result, Terrain, {.height = node, .masks = mask, .tile_size = 16,
        .resolution = 17, .levels = 2, .budget = 1000, .query_spacing = 0.5f,
        .query_radius = 40});

    return result;
}

/** Store the copy of a window of a query tile: the height function in a R32Float image, or
 * the mask function in channel 0 of a RGBA32Float image.
 *
 * @param world The world
 * @param t The tile
 * @param masks Store the masks window
 */
static void sc_store(
    ecs_world_t *world,
    const box_terrain_query_tile_t *t,
    bool masks)
{
    int32_t n = t->texels, i, j, c = masks ? 4 : 1;
    float *data = ecs_os_calloc_n(float, n * n * c);
    for (j = 0; j < n; j ++) {
        for (i = 0; i < n; i ++) {
            double x = t->window[0] + (double)i * t->window[2] / (double)(n - 1);
            double z = t->window[1] + (double)j * t->window[2] / (double)(n - 1);
            float *v = &data[(j * n + i) * c];
            if (masks) {
                v[0] = sc_mask_fn ? sc_mask_fn(x, z) : 0.0f;
            } else {
                v[0] = sc_height_fn ? sc_height_fn(x, z) : 0.0f;
            }
        }
    }

    box_texture_readback_t *rb = box_texture_readback_ensure(box_textures_get(world),
        masks ? t->masks : t->heights);
    box_texture_readback_store(rb, data, n, n,
        masks ? ReadbackRGBA32Float : ReadbackR32Float, 1, 0);
}

/** Store the copies of the query tiles of a terrain that have none.
 *
 * @param world The world
 * @param terrain The terrain
 */
static void sc_fill(
    ecs_world_t *world,
    ecs_entity_t terrain)
{
    int32_t count, i;
    box_texture_copy_t copy;
    box_terrain_query_tile_t *const *tiles = box_terrain_query_tiles(world, terrain, &count);
    for (i = 0; i < count; i ++) {
        if (!box_texture_readback(world, tiles[i]->heights, &copy)) {
            sc_store(world, tiles[i], false);
        }

        if (tiles[i]->masks && !box_texture_readback(world, tiles[i]->masks, &copy)) {
            sc_store(world, tiles[i], true);
        }
    }
}

/** Run frames, filling the query tiles of a terrain before each.
 *
 * @param world The world
 * @param terrain The terrain
 * @param frames Number of frames
 */
static void sc_progress_filled(
    ecs_world_t *world,
    ecs_entity_t terrain,
    int32_t frames)
{
    int32_t i;
    for (i = 0; i < frames; i ++) {
        sc_fill(world, terrain);
        ecs_progress(world, 0);
    }
}

/** A tilted plane rising a quarter meter per meter along X.
 *
 * @param x X
 * @param z Z
 * @return The height
 */
static float sc_ramp(
    double x,
    double z)
{
    (void)z;
    return (float)(0.25 * x);
}

/** Flat for negative X, rising a meter per meter beyond: 45 degrees.
 *
 * @param x X
 * @param z Z
 * @return The height
 */
static float sc_crease(
    double x,
    double z)
{
    (void)z;
    return (float)(x > 0 ? x : 0);
}

/** A plane rising half a meter per meter along X.
 *
 * @param x X
 * @param z Z
 * @return The height
 */
static float sc_slope(
    double x,
    double z)
{
    (void)z;
    return (float)(0.5 * x);
}

/** A mask rising from 0 at X -20 to 1 at X 20.
 *
 * @param x X
 * @param z Z
 * @return The mask
 */
static float sc_mask_x(
    double x,
    double z)
{
    (void)z;
    return (float)(x / 40.0 + 0.5);
}

void Scatter_defaults(void) {
    // scenes/scatter
    ecs_world_t *world = sc_world();
    ecs_entity_t e = ecs_new(world);
    ecs_add(world, e, Scatter);
    const Scatter *s = ecs_get(world, e, Scatter);
    test_flt(s->density, 1);
    test_flt(s->spacing, 0);
    test_int(s->mask_channel, -1);
    test_flt(s->mask_min, 0.5f);
    test_flt(s->mask_max, 1);
    test_flt(s->slope_max, 90);
    test_flt(s->yaw, 360);
    test_flt(s->scale_min, 1);
    test_flt(s->scale_max, 1);
    test_assert(ecs_has(world, e, ScatterStatus));
    ecs_progress(world, 0);
    test_int(ecs_get(world, e, ScatterStatus)->instances, 0);
    test_int(sc_children(world, e), 0);

    ecs_fini(world);
}

void Scatter_density_counts(void) {
    // scenes/scatter
    ecs_world_t *world = sc_world();
    ecs_entity_t tree = sc_prefab(world, "ScatterTree");
    Scatter c = sc_config(tree, 0, 1, 0, 1);
    c.extent[0] = 100;
    c.extent[1] = 100;
    ecs_entity_t a = sc_scatter(world, 0, 0, 3, 0, &c);
    c.density = 4;
    ecs_entity_t b = sc_scatter(world, 0, 200, 0, 0, &c);
    c.density = 2;
    c.spacing = 3;
    ecs_entity_t d = sc_scatter(world, 0, 400, 0, 0, &c);
    ecs_progress(world, 0);

    /* A jittered grid as large as the density asks has one candidate per lattice cell */
    const ScatterStatus *s = ecs_get(world, a, ScatterStatus);
    test_int(s->instances, 100);
    test_int(sc_children(world, a), 100);
    test_int(ecs_get(world, b, ScatterStatus)->instances, 400);
    s = ecs_get(world, d, ScatterStatus);
    test_assert(s->instances >= 180 && s->instances <= 220);
    test_int(s->evaluations, 1);

    /* On the plane Y 0 of the parent, here the world, so at -3 under the entity */
    ecs_iter_t it = ecs_children(world, a);
    int32_t i;
    while (ecs_children_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            const Position *p = ecs_get(world, it.entities[i], Position);
            test_flt(p->y, -3);
            test_assert(p->x >= 0 && p->x < 100 && p->z >= 0 && p->z < 100);
        }
    }

    /* Poisson disk: thinned to the density below its densest packing */
    c = sc_config(tree, 0, 5, 2, 1);
    c.mode = ScatterPoisson;
    c.extent[0] = 100;
    c.extent[1] = 100;
    ecs_entity_t p = sc_scatter(world, 0, 0, 0, 200, &c);
    ecs_progress(world, 0);
    s = ecs_get(world, p, ScatterStatus);
    test_assert(s->instances >= 400 && s->instances <= 600);
    test_assert(s->thinned > 0);

    ecs_fini(world);
}

void Scatter_spacing_holds(void) {
    // scenes/scatter
    ecs_world_t *world = sc_world();
    ecs_entity_t tree = sc_prefab(world, "ScatterTree");
    sc_inst_t *insts = ecs_os_malloc_n(sc_inst_t, SC_MAX);
    int32_t mode;
    for (mode = 0; mode < 2; mode ++) {
        Scatter c = sc_config(tree, 0, mode ? 20 : 2, mode ? 2.5f : 5, 3);
        c.mode = mode ? ScatterPoisson : ScatterJittered;
        c.extent[0] = 60;
        c.extent[1] = 60;
        ecs_entity_t e = sc_scatter(world, 0, -30, 0, -30, &c);
        ecs_progress(world, 0);
        int32_t count = 0;
        sc_collect(world, e, -30, 0, -30, insts, &count);
        test_assert(count > 50);
        test_int(count, ecs_get(world, e, ScatterStatus)->instances);
        test_assert(sc_min_distance(insts, count) >= c.spacing - 1e-4);
        ecs_delete(world, e);
    }

    ecs_os_free(insts);
    ecs_fini(world);
}

void Scatter_cells_seamless(void) {
    // scenes/scatter
    ecs_world_t *world = sc_world();
    ecs_entity_t tree = sc_prefab(world, "ScatterTree");
    ecs_entity_t bush = sc_prefab(world, "ScatterBush");
    sc_inst_t *cells = ecs_os_malloc_n(sc_inst_t, SC_MAX);
    sc_inst_t *whole = ecs_os_malloc_n(sc_inst_t, SC_MAX);
    int32_t mode, i;
    for (mode = 0; mode < 2; mode ++) {
        Scatter c = sc_config(tree, bush, 6, 2, 11);
        c.mode = mode ? ScatterPoisson : ScatterJittered;
        c.scale_min = 0.5f;
        c.scale_max = 2;
        ecs_entity_t sa, sb;
        ecs_entity_t ca = sc_cell(world, -1, 2, 20, &c, &sa);
        ecs_entity_t cb = sc_cell(world, 0, 2, 20, &c, &sb);
        c.extent[0] = 40;
        c.extent[1] = 20;
        ecs_entity_t big = sc_scatter(world, 0, -20, 0, 40, &c);
        ecs_progress(world, 0);

        int32_t nc = 0, nw = 0;
        sc_collect(world, sa, -20, 0, 40, cells, &nc);
        sc_collect(world, sb, 0, 0, 40, cells, &nc);
        sc_collect(world, big, -20, 0, 40, whole, &nw);
        test_assert(nc > 30);
        test_int(nc, nw);

        /* The cells together are the region that covers both: no seam, no duplicate */
        qsort(cells, (size_t)nc, sizeof(sc_inst_t), sc_compare);
        qsort(whole, (size_t)nw, sizeof(sc_inst_t), sc_compare);
        for (i = 0; i < nc; i ++) {
            test_assert(fabs(cells[i].x - whole[i].x) < 1e-3);
            test_assert(fabs(cells[i].z - whole[i].z) < 1e-3);
            test_flt(cells[i].r.y, whole[i].r.y);
            test_flt(cells[i].s.x, whole[i].s.x);
            test_assert(cells[i].prefab == whole[i].prefab);
            test_assert(cells[i].x >= -20 && cells[i].x < 20);
            test_assert(cells[i].z >= 40 && cells[i].z < 60);
        }

        test_assert(sc_min_distance(cells, nc) >= 2 - 1e-3);
        ecs_delete(world, ca);
        ecs_delete(world, cb);
        ecs_delete(world, big);
    }

    ecs_os_free(cells);
    ecs_os_free(whole);
    ecs_fini(world);
}

void Scatter_deterministic(void) {
    // scenes/scatter
    ecs_world_t *world = sc_world();
    ecs_entity_t tree = sc_prefab(world, "ScatterTree");
    ecs_entity_t bush = sc_prefab(world, "ScatterBush");
    sc_inst_t *first = ecs_os_malloc_n(sc_inst_t, SC_MAX);
    sc_inst_t *again = ecs_os_malloc_n(sc_inst_t, SC_MAX);
    Scatter c = sc_config(tree, bush, 3, 1.5f, 42);
    c.scale_min = 0.8f;
    c.scale_max = 1.3f;
    ecs_entity_t s;
    ecs_entity_t cell = sc_cell(world, 3, -4, 32, &c, &s);
    ecs_progress(world, 0);
    int32_t n1 = 0, n2 = 0, i;
    sc_collect(world, s, 96, 0, -128, first, &n1);
    test_assert(n1 > 0);

    /* Revisit the cell after other entities were made: the same instances */
    ecs_delete(world, cell);
    for (i = 0; i < 100; i ++) {
        ecs_new(world);
    }

    cell = sc_cell(world, 3, -4, 32, &c, &s);
    ecs_progress(world, 0);
    sc_collect(world, s, 96, 0, -128, again, &n2);
    test_int(n1, n2);
    qsort(first, (size_t)n1, sizeof(sc_inst_t), sc_compare);
    qsort(again, (size_t)n2, sizeof(sc_inst_t), sc_compare);
    for (i = 0; i < n1; i ++) {
        test_assert(first[i].x == again[i].x && first[i].z == again[i].z);
        test_assert(first[i].y == again[i].y);
        test_assert(first[i].r.y == again[i].r.y);
        test_assert(first[i].s.x == again[i].s.x);
        test_assert(first[i].prefab == again[i].prefab);
    }

    /* Another seed places them elsewhere */
    c.seed = 43;
    ecs_set_ptr(world, s, Scatter, &c);
    ecs_progress(world, 0);
    n2 = 0;
    sc_collect(world, s, 96, 0, -128, again, &n2);
    qsort(again, (size_t)n2, sizeof(sc_inst_t), sc_compare);
    test_assert(n2 > 0);
    test_assert(n1 != n2 || first[0].x != again[0].x || first[0].z != again[0].z);

    ecs_os_free(first);
    ecs_os_free(again);
    ecs_fini(world);
}

void Scatter_avoid(void) {
    // scenes/scatter
    ecs_world_t *world = sc_world();
    ecs_entity_t tree = sc_prefab(world, "ScatterTree");
    ecs_entity_t bush = sc_prefab(world, "ScatterBush");
    sc_inst_t *trees = ecs_os_malloc_n(sc_inst_t, SC_MAX);
    sc_inst_t *bushes = ecs_os_malloc_n(sc_inst_t, SC_MAX);
    Scatter tc = sc_config(tree, 0, 1, 6, 1);
    Scatter bc = sc_config(bush, 0, 100, 0, 2);
    bc.avoid_radius = 2;
    int32_t nt = 0, nb = 0, cx, i, j, avoided = 0;
    for (cx = 0; cx < 2; cx ++) {
        ecs_entity_t ts, bs;
        ecs_entity_t cell = sc_cell(world, cx, 0, 20, &tc, &ts);
        bc.avoid[0] = ts;
        bs = sc_scatter(world, cell, 0, 0, 0, &bc);
        ecs_progress(world, 0);
        sc_collect(world, ts, cx * 20, 0, 0, trees, &nt);
        sc_collect(world, bs, cx * 20, 0, 0, bushes, &nb);
        avoided += ecs_get(world, bs, ScatterStatus)->avoided;
    }

    test_assert(nt > 0);
    test_assert(nb > 100);
    test_assert(avoided > 0);

    /* Bushes keep clear of the trees of their cell and of the next one */
    for (i = 0; i < nb; i ++) {
        for (j = 0; j < nt; j ++) {
            double dx = bushes[i].x - trees[j].x, dz = bushes[i].z - trees[j].z;
            test_assert(dx * dx + dz * dz >= 4 - 1e-3);
        }
    }

    ecs_os_free(trees);
    ecs_os_free(bushes);
    ecs_fini(world);
}

void Scatter_reevaluate_on_change(void) {
    // scenes/scatter
    ecs_world_t *world = sc_world();
    ecs_entity_t tree = sc_prefab(world, "ScatterTree");
    Scatter c = sc_config(tree, 0, 1, 0, 1);
    c.extent[0] = 50;
    c.extent[1] = 50;
    ecs_entity_t e = sc_scatter(world, 0, 0, 0, 0, &c);
    ecs_progress(world, 0);
    test_int(ecs_get(world, e, ScatterStatus)->instances, 25);
    ecs_progress(world, 0);
    test_int(ecs_get(world, e, ScatterStatus)->evaluations, 1);

    c.density = 4;
    ecs_set_ptr(world, e, Scatter, &c);
    ecs_progress(world, 0);
    const ScatterStatus *s = ecs_get(world, e, ScatterStatus);
    test_int(s->instances, 100);
    test_int(s->evaluations, 2);
    test_int(sc_children(world, e), 100);

    c.items[0].prefab = 0;
    ecs_set_ptr(world, e, Scatter, &c);
    ecs_progress(world, 0);
    test_int(ecs_get(world, e, ScatterStatus)->instances, 0);
    test_int(sc_children(world, e), 0);

    ecs_fini(world);
}

void Scatter_bulk_instances(void) {
    // scenes/scatter
    ecs_world_t *world = sc_world();
    ecs_entity_t tree = sc_prefab(world, "ScatterTree");
    ecs_entity_t bush = sc_prefab(world, "ScatterBush");
    Scatter c = sc_config(tree, bush, 100, 0, 5);
    c.items[1].weight = 3;
    c.extent[0] = 100;
    c.extent[1] = 100;
    c.spatial_ignore = true;
    ecs_entity_t e = sc_scatter(world, 0, 0, 0, 0, &c);
    ecs_progress(world, 0);

    const ScatterStatus *s = ecs_get(world, e, ScatterStatus);
    test_int(s->instances, 10000);
    test_assert(s->time_ms > 0);
    test_assert(s->create_ms > 0);
    test_assert(s->per_ms > 0);
    printf("Scatter: %d instances in %.3f ms (%.3f ms created), %.0f per ms\n",
        s->instances, (double)s->time_ms, (double)s->create_ms, (double)s->per_ms);

    int32_t trees = 0, bushes = 0, i;
    ecs_iter_t it = ecs_children(world, e);
    while (ecs_children_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            ecs_entity_t inst = it.entities[i];
            trees += ecs_has_pair(world, inst, EcsIsA, tree);
            bushes += ecs_has_pair(world, inst, EcsIsA, bush);
            test_assert(ecs_owns(world, inst, Position));
            test_assert(ecs_owns(world, inst, Rotation));
            test_assert(ecs_owns(world, inst, Scale));
            test_assert(ecs_has(world, inst, WorldTransform));
            test_assert(ecs_has_id(world, inst, SpatialIgnore));
        }
    }

    test_int(trees + bushes, 10000);
    test_assert(bushes > trees * 2 && bushes < trees * 4);

    ecs_fini(world);
}

void Scatter_filter_mask(void) {
    // scenes/scatter
    ecs_world_t *world = sc_world();
    ecs_entity_t tree = sc_prefab(world, "ScatterTree");
    ecs_entity_t terrain = sc_terrain(world);
    sc_height_fn = NULL;
    sc_mask_fn = sc_mask_x;
    sc_inst_t *insts = ecs_os_malloc_n(sc_inst_t, SC_MAX);
    Scatter c = sc_config(tree, 0, 25, 0, 9);
    c.ground = terrain;
    c.extent[0] = 40;
    c.extent[1] = 40;
    c.mask_channel = 0;
    c.mask_min = 0.75f;
    c.mask_max = 1;
    ecs_entity_t hard = sc_scatter(world, 0, -20, 0, -20, &c);
    c.mask_edge = 0.25f;
    ecs_entity_t soft = sc_scatter(world, 0, -20, 0, -20, &c);
    sc_progress_filled(world, terrain, 6);

    int32_t nh = 0, ns = 0, i, inner = 0;
    sc_collect(world, hard, -20, 0, -20, insts, &nh);
    test_assert(nh > 0);
    test_assert(ecs_get(world, hard, ScatterStatus)->filtered > 0);
    for (i = 0; i < nh; i ++) {
        test_assert(insts[i].x >= 10 - 1e-2);
    }

    /* The soft edge thins the density from X 10 down to X 0 */
    sc_collect(world, soft, -20, 0, -20, insts, &ns);
    test_assert(ns > nh);
    for (i = 0; i < ns; i ++) {
        test_assert(insts[i].x >= -1e-2);
        inner += insts[i].x < 10;
    }

    test_assert(inner > 0 && inner < 100);
    test_int(inner, ns - nh);

    ecs_os_free(insts);
    ecs_fini(world);
}

void Scatter_filter_slope(void) {
    // scenes/scatter
    ecs_world_t *world = sc_world();
    ecs_entity_t tree = sc_prefab(world, "ScatterTree");
    ecs_entity_t terrain = sc_terrain(world);
    sc_height_fn = sc_crease;
    sc_mask_fn = NULL;
    sc_inst_t *insts = ecs_os_malloc_n(sc_inst_t, SC_MAX);
    Scatter c = sc_config(tree, 0, 25, 0, 4);
    c.ground = terrain;
    c.extent[0] = 40;
    c.extent[1] = 40;
    c.slope_max = 30;
    ecs_entity_t flat = sc_scatter(world, 0, -20, 0, -20, &c);
    c.slope_min = 40;
    c.slope_max = 50;
    ecs_entity_t steep = sc_scatter(world, 0, -20, 0, -20, &c);
    sc_progress_filled(world, terrain, 6);

    int32_t n = 0, i;
    sc_collect(world, flat, -20, 0, -20, insts, &n);
    test_assert(n > 50);
    test_assert(ecs_get(world, flat, ScatterStatus)->filtered > 50);
    for (i = 0; i < n; i ++) {
        test_assert(insts[i].x < 1);
        test_assert(fabs(insts[i].y - sc_crease(insts[i].x, insts[i].z)) < 1e-2);
    }

    n = 0;
    sc_collect(world, steep, -20, 0, -20, insts, &n);
    test_assert(n > 50);
    for (i = 0; i < n; i ++) {
        test_assert(insts[i].x > -1);
    }

    ecs_os_free(insts);
    ecs_fini(world);
}

void Scatter_filter_height(void) {
    // scenes/scatter
    ecs_world_t *world = sc_world();
    ecs_entity_t tree = sc_prefab(world, "ScatterTree");
    ecs_entity_t terrain = sc_terrain(world);
    sc_height_fn = sc_ramp;
    sc_mask_fn = NULL;
    sc_inst_t *insts = ecs_os_malloc_n(sc_inst_t, SC_MAX);
    Scatter c = sc_config(tree, 0, 25, 0, 6);
    c.ground = terrain;
    c.extent[0] = 40;
    c.extent[1] = 40;
    c.height_min = 0;
    c.height_max = 2.5f;
    c.sink = 0.5f;
    ecs_entity_t e = sc_scatter(world, 0, -20, 0, -20, &c);
    sc_progress_filled(world, terrain, 6);

    int32_t n = 0, i;
    sc_collect(world, e, -20, 0, -20, insts, &n);
    test_assert(n > 50);
    test_assert(ecs_get(world, e, ScatterStatus)->filtered > n);
    for (i = 0; i < n; i ++) {
        test_assert(insts[i].x >= -1e-2 && insts[i].x <= 10 + 1e-2);
        test_assert(fabs(insts[i].y - (sc_ramp(insts[i].x, insts[i].z) - 0.5)) < 1e-2);
        test_flt(insts[i].r.x, 0);
        test_flt(insts[i].r.z, 0);
    }

    ecs_os_free(insts);
    ecs_fini(world);
}

void Scatter_terrain_waits_and_aligns(void) {
    // scenes/scatter
    ecs_world_t *world = sc_world();
    ecs_entity_t tree = sc_prefab(world, "ScatterTree");
    ecs_entity_t terrain = sc_terrain(world);
    sc_height_fn = sc_slope;
    sc_mask_fn = NULL;
    sc_inst_t *insts = ecs_os_malloc_n(sc_inst_t, SC_MAX);
    Scatter c = sc_config(tree, 0, 4, 0, 8);
    c.ground = terrain;
    c.align = 1;
    ecs_entity_t s;
    sc_cell(world, 0, 0, 20, &c, &s);
    ecs_progress(world, 0);

    /* Nothing is known of the terrain yet: the scatter waits for it */
    const ScatterStatus *st = ecs_get(world, s, ScatterStatus);
    test_bool(st->waiting, true);
    test_int(st->instances, 0);
    test_int(sc_children(world, s), 0);

    sc_progress_filled(world, terrain, 6);
    st = ecs_get(world, s, ScatterStatus);
    test_bool(st->waiting, false);
    test_int(st->instances, 16);
    test_int(st->evaluations, 1);

    int32_t n = 0, i;
    sc_collect(world, s, 0, 0, 0, insts, &n);
    test_int(n, 16);
    vec3 normal = {-0.5f, 1, 0};
    glm_vec3_normalize(normal);
    for (i = 0; i < n; i ++) {
        mat4 m;
        test_assert(fabs(insts[i].y - sc_slope(insts[i].x, insts[i].z)) < 1e-2);
        glm_euler_yxz((vec3){insts[i].r.x, insts[i].r.y, insts[i].r.z}, m);
        test_assert(fabsf(m[1][0] - normal[0]) < 1e-3f);
        test_assert(fabsf(m[1][1] - normal[1]) < 1e-3f);
        test_assert(fabsf(m[1][2] - normal[2]) < 1e-3f);
    }

    ecs_os_free(insts);
    ecs_fini(world);
}

void Scatter_chunk_grid_script(void) {
    // scenes/scatter
    ecs_world_t *world = sc_world();
    ecs_entity_t tree = sc_prefab(world, "ScatterTree");
    ecs_entity_t bush = sc_prefab(world, "ScatterBush");
    ecs_entity_t grid = ecs_new(world);
    ecs_set(world, grid, ChunkGrid, {
        .template = ecs_lookup(world, "ScatterCell"),
        .cell_size = {20, 0, 20},
        .levels = {15, 40},
        .hysteresis = 2
    });
    ecs_entity_t focus = ecs_new(world);
    ecs_set(world, focus, Position, {10, 0, 10});
    ecs_add(world, focus, WorldFocus);
    ecs_progress(world, 0);

    int32_t level = -1, i;
    ecs_entity_t cell = box_chunk_grid_cell(world, grid, (int32_t[3]){0, 0, 0}, &level);
    test_assert(cell != 0);
    test_int(level, 0);
    const ChunkGridCell *gc = ecs_get(world, cell, ChunkGridCell);
    test_assert(gc != NULL);
    test_int(gc->cell[0], 0);
    test_int(gc->level, 0);
    test_flt(gc->size[0], 20);
    ecs_entity_t ground = ecs_lookup_child(world, cell, "ground");
    test_assert(ground != 0);
    const ScatterStatus *s = ecs_get(world, ground, ScatterStatus);
    test_assert(s != NULL);
    test_int(s->instances, 16);
    ecs_iter_t it = ecs_children(world, ground);
    while (ecs_children_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            test_assert(ecs_has_pair(world, it.entities[i], EcsIsA, tree) ||
                ecs_has_pair(world, it.entities[i], EcsIsA, bush));
        }
    }

    /* At level 1 the template scatters half as densely */
    ecs_set(world, focus, Position, {10, 0, 45});
    ecs_progress(world, 0);
    test_assert(box_chunk_grid_cell(world, grid, (int32_t[3]){0, 0, 0}, &level) == cell);
    test_int(level, 1);
    test_int(ecs_get(world, cell, ChunkGridCell)->level, 1);
    ground = ecs_lookup_child(world, cell, "ground");
    test_assert(ground != 0);
    s = ecs_get(world, ground, ScatterStatus);
    test_assert(s->instances > 0 && s->instances < 16);
    test_int(sc_children(world, ground), s->instances);

    ecs_fini(world);
}

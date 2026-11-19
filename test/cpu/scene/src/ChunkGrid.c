/**
 * @file ChunkGrid.c
 * @brief ChunkGrid: cells of a template in rings around the foci with their props, loads and
 * unloads as a focus moves with hysteresis, the budget nearest first, levels, 3D grids, world
 * origin shifts, gates and deleting the grid.
 *
 * The templates are in test/assets/world/chunk_grid.flecs. Most cases use 10 m cells and a
 * focus at the center of cell 0 0, so distances to the nearest point of a cell are multiples of 5.
 */

#include <box.h>
#include <bake_test.h>
#include <math.h>

/** Create a world with box and the templates of the tests.
 *
 * @return The world
 */
static ecs_world_t* chunk_world(void)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    char *path = box_app_locate("test/assets/world/chunk_grid.flecs");
    test_assert(path != NULL);
    test_assert(ecs_script_run_file(world, path) == 0);
    ecs_os_free(path);

    return world;
}

/** Create a 2D grid of 10 m cells with up to two rings.
 *
 * @param world The world
 * @param template Name of the template
 * @param r0 Radius of level 0
 * @param r1 Radius of level 1, 0 for one level
 * @param hysteresis The hysteresis
 * @param budget The budget, 0 for no limit
 * @return The grid entity
 */
static ecs_entity_t chunk_grid(
    ecs_world_t *world,
    const char *template,
    float r0,
    float r1,
    float hysteresis,
    int32_t budget)
{
    ecs_entity_t t = ecs_lookup(world, template);
    test_assert(t != 0);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, ChunkGrid, {
        .template = t,
        .cell_size = {10, 0, 10},
        .levels = {r0, r1},
        .hysteresis = hysteresis,
        .budget = budget
    });

    return e;
}

/** Create a root focus entity.
 *
 * @param world The world
 * @param x X position
 * @param y Y position
 * @param z Z position
 * @return The focus
 */
static ecs_entity_t chunk_focus(
    ecs_world_t *world,
    float x,
    float y,
    float z)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Position, {x, y, z});
    ecs_add(world, e, WorldFocus);

    return e;
}

/** Move a focus.
 *
 * @param world The world
 * @param focus The focus
 * @param x X position
 * @param z Z position
 */
static void chunk_move(
    ecs_world_t *world,
    ecs_entity_t focus,
    float x,
    float z)
{
    ecs_set(world, focus, Position, {x, 0, z});
}

/** Get a numeric prop of the template on a cell.
 *
 * @param world The world
 * @param cell The cell
 * @param template Name of the template
 * @param name The prop
 * @return The value
 */
static double chunk_prop(
    ecs_world_t *world,
    ecs_entity_t cell,
    const char *template,
    const char *name)
{
    ecs_entity_t t = ecs_lookup(world, template);
    const void *ptr = ecs_get_id(world, cell, t);
    test_assert(ptr != NULL);
    ecs_meta_cursor_t cur = ecs_meta_cursor(world, t, ECS_CONST_CAST(void*, ptr));
    test_assert(ecs_meta_push(&cur) == 0);
    test_assert(ecs_meta_member(&cur, name) == 0);

    return ecs_meta_get_float(&cur);
}

/** Get the distance of the nearest point of a 2D 10 m cell from a point in XZ.
 *
 * @param x X of the point
 * @param z Z of the point
 * @param cx X of the cell
 * @param cz Z of the cell
 * @return The distance
 */
static double chunk_distance(
    double x,
    double z,
    int32_t cx,
    int32_t cz)
{
    double lx = cx * 10.0, lz = cz * 10.0;
    double dx = x < lx ? lx - x : x > lx + 10 ? x - lx - 10 : 0;
    double dz = z < lz ? lz - z : z > lz + 10 ? z - lz - 10 : 0;

    return sqrt(dx * dx + dz * dz);
}

/** Get the loaded cell of a 2D grid.
 *
 * @param world The world
 * @param grid The grid
 * @param cx X of the cell
 * @param cz Z of the cell
 * @param level Receives the level, NULL to ignore it
 * @return The cell entity, 0 when not loaded
 */
static ecs_entity_t chunk_cell(
    ecs_world_t *world,
    ecs_entity_t grid,
    int32_t cx,
    int32_t cz,
    int32_t *level)
{
    return box_chunk_grid_cell(world, grid, (int32_t[3]){cx, 0, cz}, level);
}

/** Count the children of an entity.
 *
 * @param world The world
 * @param e The entity
 * @return Number of children
 */
static int32_t chunk_children(
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

/** Check that the cells of a 2D grid are those in range of a focus with their levels and props.
 *
 * @param world The world
 * @param grid The grid
 * @param x X of the focus
 * @param z Z of the focus
 * @param r0 Radius of level 0
 * @param r1 Radius of level 1
 * @return Number of cells
 */
static int32_t chunk_check_rings(
    ecs_world_t *world,
    ecs_entity_t grid,
    double x,
    double z,
    double r0,
    double r1)
{
    int32_t cx, cz, level, result = 0;
    for (cx = -8; cx <= 8; cx ++) {
        for (cz = -8; cz <= 8; cz ++) {
            double d = chunk_distance(x, z, cx, cz);
            int32_t expect = d <= r0 ? 0 : d <= r1 ? 1 : -1;
            ecs_entity_t cell = chunk_cell(world, grid, cx, cz, &level);
            if (expect < 0) {
                test_assert(cell == 0);
                continue;
            }

            test_assert(cell != 0);
            test_int(level, expect);
            test_assert(ecs_get_parent(world, cell) == grid);
            test_int((int32_t)chunk_prop(world, cell, "Chunk", "cell_x"), cx);
            test_int((int32_t)chunk_prop(world, cell, "Chunk", "cell_y"), 0);
            test_int((int32_t)chunk_prop(world, cell, "Chunk", "cell_z"), cz);
            test_int((int32_t)chunk_prop(world, cell, "Chunk", "level"), expect);
            test_flt(chunk_prop(world, cell, "Chunk", "size"), 10);
            const Position *p = ecs_get(world, cell, Position);
            test_flt(p->x, cx * 10);
            test_flt(p->y, 0);
            test_flt(p->z, cz * 10);
            test_assert(ecs_lookup_child(world, cell, "marker") != 0);
            test_bool(ecs_lookup_child(world, cell, "detail") != 0, expect == 0);
            result ++;
        }
    }

    return result;
}

/** Keep cells with a negative cell X back, the gate of the gate test.
 *
 * @param world The world
 * @param grid The grid
 * @param cell The cell
 * @param level The level
 * @param ctx Counts the calls
 * @return Whether the cell goes ahead
 */
static bool chunk_gate_positive_x(
    ecs_world_t *world,
    ecs_entity_t grid,
    const int32_t cell[3],
    int32_t level,
    void *ctx)
{
    (void)world;
    (void)grid;
    (void)level;
    (*(int32_t*)ctx) ++;

    return cell[0] >= 0;
}

void ChunkGrid_rings(void) {
    // scenes/chunk_grid
    ecs_world_t *world = chunk_world();
    ecs_entity_t grid = chunk_grid(world, "Chunk", 15, 30, 2, 0);
    chunk_focus(world, 5, 0, 5);
    ecs_progress(world, 0);

    int32_t count = chunk_check_rings(world, grid, 5, 5, 15, 30);
    test_assert(count > 0);
    test_int(chunk_children(world, grid), count);
    const ChunkGridStatus *s = ecs_get(world, grid, ChunkGridStatus);
    test_assert(s != NULL);
    test_int(s->cells, count);
    test_int(s->created, count);
    test_int(s->loaded[0] + s->loaded[1], count);
    test_assert(s->loaded[0] > 0 && s->loaded[1] > 0);
    test_int(s->pending, 0);
    test_int(s->deleted, 0);

    ecs_progress(world, 0);
    s = ecs_get(world, grid, ChunkGridStatus);
    test_int(s->created, 0);
    test_int(s->deleted, 0);
    test_int(s->cells, count);

    ecs_fini(world);
}

void ChunkGrid_props_declared_only(void) {
    // scenes/chunk_grid
    ecs_world_t *world = chunk_world();
    ecs_entity_t grid = chunk_grid(world, "PlainChunk", 15, 0, 0, 0);
    chunk_focus(world, 5, 0, 5);
    ecs_progress(world, 0);

    ecs_entity_t cell = chunk_cell(world, grid, 1, -1, NULL);
    test_assert(cell != 0);
    test_int((int32_t)chunk_prop(world, cell, "PlainChunk", "cell_x"), 1);
    test_int((int32_t)chunk_prop(world, cell, "PlainChunk", "cell_z"), -1);
    test_assert(ecs_lookup_child(world, cell, "marker") != 0);
    test_int(ecs_get(world, grid, ChunkGridStatus)->cells, chunk_children(world, grid));

    ecs_fini(world);
}

void ChunkGrid_move_loads_unloads(void) {
    // scenes/chunk_grid
    ecs_world_t *world = chunk_world();
    ecs_entity_t grid = chunk_grid(world, "Chunk", 20, 0, 5, 0);
    ecs_entity_t focus = chunk_focus(world, 5, 0, 5);
    ecs_progress(world, 0);
    bool before[30][17] = {{false}};
    int32_t cx, cz;
    for (cx = -8; cx < 22; cx ++) {
        for (cz = -8; cz <= 8; cz ++) {
            before[cx + 8][cz + 8] = chunk_cell(world, grid, cx, cz, NULL) != 0;
        }
    }

    chunk_move(world, focus, 105, 5);
    ecs_progress(world, 0);
    const ChunkGridStatus *s = ecs_get(world, grid, ChunkGridStatus);
    test_assert(s->created > 0);
    test_assert(s->deleted > 0);
    for (cx = -8; cx < 22; cx ++) {
        for (cz = -8; cz <= 8; cz ++) {
            double d = chunk_distance(105, 5, cx, cz);
            bool loaded = chunk_cell(world, grid, cx, cz, NULL) != 0;
            if (d <= 20) {
                test_assert(loaded);
            } else if (d > 25) {
                test_assert(!loaded);
            } else {
                test_bool(loaded, before[cx + 8][cz + 8]);
            }
        }
    }

    test_int(chunk_children(world, grid), s->cells);

    ecs_fini(world);
}

void ChunkGrid_hysteresis_no_thrash(void) {
    // scenes/chunk_grid
    ecs_world_t *world = chunk_world();
    ecs_entity_t grid = chunk_grid(world, "Chunk", 20, 0, 4, 0);
    ecs_entity_t focus = chunk_focus(world, 5, 0, 5);
    ecs_progress(world, 0);
    test_assert(chunk_cell(world, grid, 3, 0, NULL) == 0);

    chunk_move(world, focus, 10.5f, 5);
    ecs_progress(world, 0);
    ecs_entity_t cell = chunk_cell(world, grid, 3, 0, NULL);
    test_assert(cell != 0);

    int32_t i;
    for (i = 0; i < 10; i ++) {
        chunk_move(world, focus, 9, 5);
        ecs_progress(world, 0);
        test_assert(chunk_cell(world, grid, 3, 0, NULL) == cell);
        test_int(ecs_get(world, grid, ChunkGridStatus)->deleted, 0);
        chunk_move(world, focus, 10.5f, 5);
        ecs_progress(world, 0);
        test_assert(chunk_cell(world, grid, 3, 0, NULL) == cell);
        test_int(ecs_get(world, grid, ChunkGridStatus)->created, 0);
    }

    chunk_move(world, focus, 5, 5);
    ecs_progress(world, 0);
    test_assert(chunk_cell(world, grid, 3, 0, NULL) == 0);
    test_assert(!ecs_is_alive(world, cell));

    ecs_fini(world);
}

void ChunkGrid_hysteresis_levels(void) {
    // scenes/chunk_grid
    ecs_world_t *world = chunk_world();
    ecs_entity_t grid = chunk_grid(world, "Chunk", 10, 40, 3, 0);
    ecs_entity_t focus = chunk_focus(world, 5, 0, 5);
    ecs_progress(world, 0);
    int32_t level;
    ecs_entity_t cell = chunk_cell(world, grid, 2, 0, &level);
    test_assert(cell != 0);
    test_int(level, 1);

    chunk_move(world, focus, 13, 5);
    ecs_progress(world, 0);
    chunk_cell(world, grid, 2, 0, &level);
    test_int(level, 0);

    int32_t i;
    for (i = 0; i < 10; i ++) {
        chunk_move(world, focus, 9, 5);
        ecs_progress(world, 0);
        test_assert(chunk_cell(world, grid, 2, 0, &level) == cell);
        test_int(level, 0);
        test_int(ecs_get(world, grid, ChunkGridStatus)->changed, 0);
        chunk_move(world, focus, 11, 5);
        ecs_progress(world, 0);
        test_assert(chunk_cell(world, grid, 2, 0, &level) == cell);
        test_int(level, 0);
        test_int(ecs_get(world, grid, ChunkGridStatus)->changed, 0);
    }

    chunk_move(world, focus, 5, 5);
    ecs_progress(world, 0);
    test_assert(chunk_cell(world, grid, 2, 0, &level) == cell);
    test_int(level, 1);

    ecs_fini(world);
}

void ChunkGrid_budget_nearest_first(void) {
    // scenes/chunk_grid
    ecs_world_t *world = chunk_world();
    ecs_entity_t grid = chunk_grid(world, "Chunk", 35, 0, 0, 3);
    chunk_focus(world, 5, 0, 5);
    int32_t total = 0, cx, cz, frames = 0, loaded = 0;
    for (cx = -8; cx <= 8; cx ++) {
        for (cz = -8; cz <= 8; cz ++) {
            total += chunk_distance(5, 5, cx, cz) <= 35;
        }
    }

    while (loaded < total && frames < 1000) {
        ecs_progress(world, 0);
        frames ++;
        const ChunkGridStatus *s = ecs_get(world, grid, ChunkGridStatus);
        test_assert(s->created <= 3);
        test_int(s->created, total - loaded < 3 ? total - loaded : 3);
        loaded += s->created;
        test_int(s->cells, loaded);
        test_int(s->pending, total - loaded);

        double far_loaded = 0, near_missing = INFINITY;
        for (cx = -8; cx <= 8; cx ++) {
            for (cz = -8; cz <= 8; cz ++) {
                double d = chunk_distance(5, 5, cx, cz);
                if (d > 35) {
                    continue;
                }

                if (chunk_cell(world, grid, cx, cz, NULL)) {
                    far_loaded = d > far_loaded ? d : far_loaded;
                } else {
                    near_missing = d < near_missing ? d : near_missing;
                }
            }
        }

        test_assert(far_loaded <= near_missing);
    }

    test_int(loaded, total);
    test_int(frames, (total + 2) / 3);
    test_assert(chunk_cell(world, grid, 0, 0, NULL) != 0);

    ecs_fini(world);
}

void ChunkGrid_budget_more_detail_first(void) {
    // scenes/chunk_grid
    ecs_world_t *world = chunk_world();
    ecs_entity_t grid = chunk_grid(world, "Chunk", 15, 60, 0, 0);
    ecs_entity_t focus = chunk_focus(world, 5, 0, 5);
    ecs_progress(world, 0);
    test_int(ecs_get(world, grid, ChunkGridStatus)->pending, 0);

    ChunkGrid *g = ecs_get_mut(world, grid, ChunkGrid);
    g->budget = 2;
    ecs_modified(world, grid, ChunkGrid);
    chunk_move(world, focus, 45, 5);
    ecs_progress(world, 0);

    const ChunkGridStatus *s = ecs_get(world, grid, ChunkGridStatus);
    test_int(s->changed, 2);
    test_int(s->created, 0);
    test_assert(s->pending > 0);

    ecs_fini(world);
}

void ChunkGrid_levels_update(void) {
    // scenes/chunk_grid
    ecs_world_t *world = chunk_world();
    ecs_entity_t grid = chunk_grid(world, "Chunk", 15, 40, 2, 0);
    ecs_entity_t focus = chunk_focus(world, 5, 0, 5);
    ecs_progress(world, 0);
    int32_t level;
    ecs_entity_t cell = chunk_cell(world, grid, 3, 0, &level);
    test_assert(cell != 0);
    test_int(level, 1);
    test_int((int32_t)chunk_prop(world, cell, "Chunk", "level"), 1);
    test_assert(ecs_lookup_child(world, cell, "detail") == 0);

    chunk_move(world, focus, 25, 5);
    ecs_progress(world, 0);
    test_assert(chunk_cell(world, grid, 3, 0, &level) == cell);
    test_int(level, 0);
    test_int((int32_t)chunk_prop(world, cell, "Chunk", "level"), 0);
    test_assert(ecs_lookup_child(world, cell, "detail") != 0);
    test_assert(ecs_lookup_child(world, cell, "marker") != 0);
    test_assert(ecs_get(world, grid, ChunkGridStatus)->changed > 0);

    chunk_move(world, focus, 5, 5);
    ecs_progress(world, 0);
    test_assert(chunk_cell(world, grid, 3, 0, &level) == cell);
    test_int(level, 1);
    test_int((int32_t)chunk_prop(world, cell, "Chunk", "level"), 1);
    test_assert(ecs_lookup_child(world, cell, "detail") == 0);

    ecs_fini(world);
}

void ChunkGrid_grid_3d(void) {
    // scenes/chunk_grid
    ecs_world_t *world = chunk_world();
    ecs_entity_t t = ecs_lookup(world, "Voxel");
    ecs_entity_t grid = ecs_new(world);
    ecs_set(world, grid, ChunkGrid, {
        .template = t, .cell_size = {10, 10, 10}, .levels = {15}
    });
    chunk_focus(world, 5, 5, 5);
    ecs_progress(world, 0);

    int32_t x, y, z, count = 0;
    for (x = -4; x <= 4; x ++) {
        for (y = -4; y <= 4; y ++) {
            for (z = -4; z <= 4; z ++) {
                double c[3] = { x * 10.0, y * 10.0, z * 10.0 }, sum = 0;
                int32_t a;
                for (a = 0; a < 3; a ++) {
                    double d = 5 < c[a] ? c[a] - 5 : 5 > c[a] + 10 ? 5 - c[a] - 10 : 0;
                    sum += d * d;
                }

                ecs_entity_t cell = box_chunk_grid_cell(world, grid, (int32_t[3]){x, y, z},
                    NULL);
                test_bool(cell != 0, sqrt(sum) <= 15);
                if (!cell) {
                    continue;
                }

                test_int((int32_t)chunk_prop(world, cell, "Voxel", "cell_y"), y);
                const Position *p = ecs_get(world, cell, Position);
                test_flt(p->x, x * 10);
                test_flt(p->y, y * 10);
                test_flt(p->z, z * 10);
                test_assert(ecs_get_id(world, cell, t) != NULL);
                count ++;
            }
        }
    }

    test_assert(box_chunk_grid_cell(world, grid, (int32_t[3]){0, 1, 0}, NULL) != 0);
    test_assert(box_chunk_grid_cell(world, grid, (int32_t[3]){0, 3, 0}, NULL) == 0);
    ecs_entity_t cell = box_chunk_grid_cell(world, grid, (int32_t[3]){0, -1, 0}, NULL);
    ecs_meta_cursor_t cur = ecs_meta_cursor(world, t,
        ECS_CONST_CAST(void*, ecs_get_id(world, cell, t)));
    test_assert(ecs_meta_push(&cur) == 0);
    test_assert(ecs_meta_member(&cur, "size") == 0);
    test_assert(ecs_meta_push(&cur) == 0);
    test_flt(ecs_meta_get_float(&cur), 10);
    test_assert(ecs_meta_next(&cur) == 0);
    test_flt(ecs_meta_get_float(&cur), 10);
    test_int(ecs_get(world, grid, ChunkGridStatus)->cells, count);

    ecs_fini(world);
}

void ChunkGrid_origin_shift_no_reload(void) {
    // scenes/chunk_grid
    ecs_world_t *world = chunk_world();
    ecs_singleton_set(world, WorldOrigin, { .threshold = 100, .step = 50, .enabled = true });
    ecs_entity_t grid = chunk_grid(world, "Chunk", 30, 0, 0, 0);
    ecs_entity_t focus = chunk_focus(world, 95, 0, 5);
    ecs_progress(world, 0);
    ecs_entity_t c9 = chunk_cell(world, grid, 9, 0, NULL);
    ecs_entity_t c10 = chunk_cell(world, grid, 10, 0, NULL);
    test_assert(c9 != 0 && c10 != 0);
    test_flt(ecs_get(world, c10, WorldTransform)->m[3][0], 100);

    chunk_move(world, focus, 105, 5);
    ecs_progress(world, 0);
    test_int(ecs_singleton_get(world, WorldOriginStatus)->shifts, 1);
    test_assert(ecs_singleton_get(world, WorldOrigin)->x == 100);
    test_flt(ecs_get(world, focus, Position)->x, 5);
    test_assert(chunk_cell(world, grid, 9, 0, NULL) == c9);
    test_assert(chunk_cell(world, grid, 10, 0, NULL) == c10);
    test_int((int32_t)chunk_prop(world, c10, "Chunk", "cell_x"), 10);
    test_flt(ecs_get(world, c9, WorldTransform)->m[3][0], -10);
    test_flt(ecs_get(world, c10, WorldTransform)->m[3][0], 0);

    ecs_entity_t c13 = chunk_cell(world, grid, 13, 0, NULL);
    test_assert(c13 != 0);
    test_int((int32_t)chunk_prop(world, c13, "Chunk", "cell_x"), 13);
    test_flt(ecs_get(world, c13, WorldTransform)->m[3][0], 30);
    test_assert(chunk_cell(world, grid, 6, 0, NULL) == 0);
    test_int(ecs_get(world, grid, ChunkGridStatus)->cells, chunk_children(world, grid));

    ecs_fini(world);
}

void ChunkGrid_delete_grid(void) {
    // scenes/chunk_grid
    ecs_world_t *world = chunk_world();
    ecs_entity_t grid = chunk_grid(world, "Chunk", 15, 30, 2, 0);
    chunk_focus(world, 5, 0, 5);
    ecs_progress(world, 0);
    ecs_entity_t a = chunk_cell(world, grid, 0, 0, NULL);
    ecs_entity_t b = chunk_cell(world, grid, 2, -2, NULL);
    test_assert(a != 0 && b != 0);
    ecs_entity_t marker = ecs_lookup_child(world, a, "marker");
    test_assert(marker != 0);

    ecs_delete(world, grid);
    test_assert(!ecs_is_alive(world, a));
    test_assert(!ecs_is_alive(world, b));
    test_assert(!ecs_is_alive(world, marker));
    ecs_progress(world, 0);

    ecs_fini(world);
}

void ChunkGrid_multiple_foci(void) {
    // scenes/chunk_grid
    ecs_world_t *world = chunk_world();
    ecs_entity_t grid = chunk_grid(world, "Chunk", 15, 0, 0, 0);
    chunk_focus(world, 5, 0, 5);
    chunk_focus(world, 205, 0, 5);
    ecs_progress(world, 0);

    int32_t cx, cz, count = 0;
    for (cx = -5; cx <= 25; cx ++) {
        for (cz = -5; cz <= 5; cz ++) {
            double d0 = chunk_distance(5, 5, cx, cz), d1 = chunk_distance(205, 5, cx, cz);
            bool loaded = chunk_cell(world, grid, cx, cz, NULL) != 0;
            test_bool(loaded, d0 <= 15 || d1 <= 15);
            count += loaded;
        }
    }

    test_assert(chunk_cell(world, grid, 0, 0, NULL) != 0);
    test_assert(chunk_cell(world, grid, 20, 0, NULL) != 0);
    test_assert(chunk_cell(world, grid, 10, 0, NULL) == 0);
    test_int(ecs_get(world, grid, ChunkGridStatus)->cells, count);
    test_int(chunk_children(world, grid), count);

    ecs_fini(world);
}

void ChunkGrid_extent(void) {
    // scenes/chunk_grid
    ecs_world_t *world = chunk_world();
    ecs_entity_t grid = ecs_new(world);
    ecs_set(world, grid, ChunkGrid, {
        .template = ecs_lookup(world, "Chunk"),
        .cell_size = {10, 0, 10},
        .levels = {100},
        .extent = {2, 0, 2}
    });
    chunk_focus(world, 5, 0, 5);
    ecs_progress(world, 0);

    test_int(ecs_get(world, grid, ChunkGridStatus)->cells, 16);
    test_assert(chunk_cell(world, grid, -2, -2, NULL) != 0);
    test_assert(chunk_cell(world, grid, 1, 1, NULL) != 0);
    test_assert(chunk_cell(world, grid, 2, 0, NULL) == 0);
    test_assert(chunk_cell(world, grid, -3, 0, NULL) == 0);

    ecs_fini(world);
}

void ChunkGrid_gate(void) {
    // scenes/chunk_grid
    ecs_world_t *world = chunk_world();
    ecs_entity_t grid = chunk_grid(world, "Chunk", 15, 30, 2, 0);
    int32_t calls = 0;
    ecs_set(world, grid, ChunkGridGate, { .ready = chunk_gate_positive_x, .ctx = &calls });
    chunk_focus(world, 5, 0, 5);
    ecs_progress(world, 0);

    test_assert(calls > 0);
    const ChunkGridStatus *s = ecs_get(world, grid, ChunkGridStatus);
    test_assert(s->held > 0);
    test_int(s->pending, s->held);
    test_assert(chunk_cell(world, grid, 0, 0, NULL) != 0);
    test_assert(chunk_cell(world, grid, -1, 0, NULL) == 0);

    ecs_progress(world, 0);
    test_assert(chunk_cell(world, grid, -1, 0, NULL) == 0);
    test_assert(ecs_get(world, grid, ChunkGridStatus)->held > 0);

    ecs_remove(world, grid, ChunkGridGate);
    ecs_progress(world, 0);
    s = ecs_get(world, grid, ChunkGridStatus);
    test_int(s->held, 0);
    test_int(s->pending, 0);
    test_assert(chunk_cell(world, grid, -1, 0, NULL) != 0);
    chunk_check_rings(world, grid, 5, 5, 15, 30);

    ecs_fini(world);
}

void ChunkGrid_template_change(void) {
    // scenes/chunk_grid
    ecs_world_t *world = chunk_world();
    ecs_entity_t grid = chunk_grid(world, "Chunk", 15, 0, 0, 0);
    chunk_focus(world, 5, 0, 5);
    ecs_progress(world, 0);
    ecs_entity_t before = chunk_cell(world, grid, 0, 0, NULL);
    int32_t count = ecs_get(world, grid, ChunkGridStatus)->cells;
    test_assert(before != 0);

    ChunkGrid *g = ecs_get_mut(world, grid, ChunkGrid);
    g->template = ecs_lookup(world, "PlainChunk");
    ecs_modified(world, grid, ChunkGrid);
    ecs_progress(world, 0);

    test_assert(!ecs_is_alive(world, before));
    ecs_entity_t after = chunk_cell(world, grid, 0, 0, NULL);
    test_assert(after != 0);
    test_assert(ecs_has_id(world, after, ecs_lookup(world, "PlainChunk")));
    const ChunkGridStatus *s = ecs_get(world, grid, ChunkGridStatus);
    test_int(s->deleted, count);
    test_int(s->created, count);
    test_int(chunk_children(world, grid), count);

    ecs_fini(world);
}

void ChunkGrid_script(void) {
    // scenes/chunk_grid
    ecs_world_t *world = chunk_world();
    char *path = box_app_locate("test/assets/world/chunk_grid_script.flecs");
    test_assert(path != NULL);
    test_assert(ecs_script_run_file(world, path) == 0);
    ecs_os_free(path);
    ecs_entity_t grid = ecs_lookup(world, "script_grid");
    test_assert(grid != 0);
    const ChunkGrid *g = ecs_get(world, grid, ChunkGrid);
    test_assert(g != NULL);
    test_flt(g->levels[1], 30);
    test_int(g->seed, 7);
    chunk_focus(world, 5, 0, 5);
    ecs_progress(world, 0);

    int32_t count = chunk_check_rings(world, grid, 5, 5, 15, 30);
    test_int(ecs_get(world, grid, ChunkGridStatus)->cells, count);
    ecs_entity_t cell = chunk_cell(world, grid, 1, 1, NULL);
    test_int((int32_t)chunk_prop(world, cell, "Chunk", "seed"), 7);

    ecs_fini(world);
}

void ChunkGrid_endless_walk(void) {
    // scenes/chunk_grid
    ecs_world_t *world = chunk_world();
    ecs_singleton_set(world, WorldOrigin, { .threshold = 200, .step = 100, .enabled = true });
    ecs_entity_t grid = chunk_grid(world, "Chunk", 15, 30, 2, 8);
    ecs_entity_t focus = chunk_focus(world, 5, 0, 5);
    int32_t i;
    for (i = 0; i < 2000; i ++) {
        const Position *p = ecs_get(world, focus, Position);
        chunk_move(world, focus, p->x + 5, p->z);
        ecs_progress(world, 0);
        const ChunkGridStatus *s = ecs_get(world, grid, ChunkGridStatus);
        test_assert(s->created + s->changed <= 8);
        test_assert(s->cells <= 60);
    }

    for (i = 0; i < 10; i ++) {
        ecs_progress(world, 0);
    }

    const WorldOrigin *o = ecs_singleton_get(world, WorldOrigin);
    double x = o->x + (double)ecs_get(world, focus, Position)->x;
    test_assert(fabs(x - 10005) < 0.01);
    int32_t cx = (int32_t)floor(x / 10);
    int32_t level;
    ecs_entity_t cell = chunk_cell(world, grid, cx, 0, &level);
    test_assert(cell != 0);
    test_int(level, 0);
    test_int((int32_t)chunk_prop(world, cell, "Chunk", "cell_x"), cx);
    test_flt(ecs_get(world, cell, WorldTransform)->m[3][0], (float)(cx * 10.0 - o->x));
    test_int(chunk_children(world, grid), ecs_get(world, grid, ChunkGridStatus)->cells);

    ecs_fini(world);
}

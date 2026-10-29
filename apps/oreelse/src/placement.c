/**
 * @file placement.c
 * @brief Placement: the building selected for placement (OrePlacement), the click and the drag
 * of the Place action that place it, the pipette, and the preview of the cells it would go on.
 *
 * A press of Place anchors a drag at its tile once, as the camera follows the astronaut. The
 * release places the anchor when the cursor did not move, else a line, an L or (with the run key
 * held) the rectangle to the release tile, cells stepping by the footprint.
 *
 * The preview shows the tile markers of every tile of a cell of the current region and a ghost
 * of the building, good or bad, pooled children of OrePreview.root.
 */

#include "game.h"

/** Tiles of the cells of a preview it shows markers on at most */
#define ORE_PREVIEW_TILES (ORE_DRAG_MAX * 9)

/** A tile with the point inside it: the anchor or the release of a drag */
typedef struct ore_cell_t {
    int32_t row; /**< Row of the tile */
    int32_t col; /**< Column of the tile */
    float fx;    /**< Fraction of the point inside the tile along x */
    float fz;    /**< Fraction of the point inside the tile along z */
} ore_cell_t;

/** The region of a placement, the original's ore_region_t: the bounding box of the cells from
 * the anchor to the release tile, stepped by the footprint, and the shape of the cells in it.
 */
typedef struct ore_region_t {
    int32_t r0;       /**< First row of the box */
    int32_t r1;       /**< Last row of the box */
    int32_t c0;       /**< First column of the box */
    int32_t c1;       /**< Last column of the box */
    int32_t anchor_r; /**< Row of the anchor */
    int32_t anchor_c; /**< Column of the anchor */
    int32_t dest_r;   /**< Row of the cells of the release, stepped from the anchor */
    int32_t dest_c;   /**< Column of the cells of the release, stepped from the anchor */
    bool rect;        /**< Every cell of the box */
    bool horizontal;  /**< The L runs along the anchor column, then the release row */
} ore_region_t;

/** What the placement checks read, gathered once per call */
typedef struct ore_place_ctx_t {
    const ecs_world_t *world; /**< The world */
    OreMap *map;              /**< The tile grid */
    ore_grid_t grid;          /**< The grid of cfg */
    float reach;              /**< cfg.reach */
    bool player;              /**< There is an astronaut; without one everything is in reach */
    float px;                 /**< X of the astronaut */
    float pz;                 /**< Z of the astronaut */
} ore_place_ctx_t;

/** The markers and ghosts a preview shows */
typedef struct ore_preview_t {
    float good[ORE_PREVIEW_TILES][2]; /**< Tile centers (x, z) of the good markers */
    int32_t good_count;               /**< Good markers */
    float bad[ORE_PREVIEW_TILES][2];  /**< Tile centers (x, z) of the bad markers */
    int32_t bad_count;                /**< Bad markers */
    float ghost[ORE_DRAG_MAX][2];     /**< Footprint centers (x, z) of the ghosts */
    bool ghost_ok[ORE_DRAG_MAX];      /**< The cell of the ghost can be placed */
    int32_t ghost_count;              /**< Ghosts */
} ore_preview_t;

/** Gather what the placement checks read.
 *
 * @param world The world
 * @param map The tile grid
 * @param ctx Receives the context
 */
static void ore_place_ctx_init(
    const ecs_world_t *world,
    OreMap *map,
    ore_place_ctx_t *ctx)
{
    ecs_iter_t it = ecs_each_id(world, ecs_id(OrePlayer));
    const Position *p;
    *ctx = (ore_place_ctx_t){
        .world = world,
        .map = map,
        .grid = ore_grid_get(world),
        .reach = ORE_CFG(world, "reach")
    };

    while (ecs_each_next(&it)) {
        p = !ctx->player && it.count ? ecs_get(world, it.entities[0], Position) : NULL;
        if (p) {
            ctx->player = true;
            ctx->px = p->x;
            ctx->pz = p->z;
        }
    }
}

/** Check the reach of a cell: the XZ distance from the astronaut to the nearest point of the
 * rectangle of its tile centers is at most cfg.reach.
 *
 * @param ctx The context
 * @param row Row of the top left tile
 * @param col Column of the top left tile
 * @param w Columns of the footprint
 * @param h Rows of the footprint
 * @return Whether the cell is in reach
 */
static bool ore_in_reach(
    const ore_place_ctx_t *ctx,
    int32_t row,
    int32_t col,
    int32_t w,
    int32_t h)
{
    float x = glm_clamp(ctx->px, ore_tile_x(&ctx->grid, col), ore_tile_x(&ctx->grid, col + w - 1));
    float z = glm_clamp(ctx->pz, ore_tile_z(&ctx->grid, row), ore_tile_z(&ctx->grid, row + h - 1));
    float dx = x - ctx->px, dz = z - ctx->pz;

    return !ctx->player || dx * dx + dz * dz <= ctx->reach * ctx->reach;
}

/** Check the tiles of a cell: all on the grid, none with a building, and each with a deposit
 * for a building that goes on deposits, without one for every other building.
 *
 * @param ctx The context
 * @param prefab The building
 * @param row Row of the top left tile
 * @param col Column of the top left tile
 * @return Whether the tiles are free for the building
 */
static bool ore_cell_free(
    const ore_place_ctx_t *ctx,
    ecs_entity_t prefab,
    int32_t row,
    int32_t col)
{
    const OreFootprint *fp = ecs_get(ctx->world, prefab, OreFootprint);
    bool wants = fp && fp->deposit;
    bool result;
    int32_t r, c, w, h;
    ore_footprint(ctx->world, prefab, &w, &h);
    result = ore_on_grid(&ctx->grid, row, col) &&
        ore_on_grid(&ctx->grid, row + h - 1, col + w - 1);
    for (r = row; result && r < row + h; r ++) {
        for (c = col; result && c < col + w; c ++) {
            ecs_entity_t *building = ore_map_building(ctx->map, r, c);
            ecs_entity_t *deposit = ore_map_deposit(ctx->map, r, c);
            result = building && !*building && deposit && (*deposit != 0) == wants;
        }
    }

    return result;
}

/** Count the placed buildings of a prefab: its instances with OreBuilding.
 *
 * @param world The world
 * @param prefab The building prefab
 * @return The count
 */
static int32_t ore_build_count(
    const ecs_world_t *world,
    ecs_entity_t prefab)
{
    const ecs_world_t *real = ecs_get_world(world);
    ecs_iter_t it = ecs_each_id(world, ecs_pair(EcsIsA, prefab));
    int32_t result = 0;
    while (ecs_each_next(&it)) {
        if (!ecs_table_has_id(real, it.table, EcsPrefab) &&
            ecs_table_has_id(real, it.table, ecs_id(OreBuilding)))
        {
            result += it.count;
        }
    }

    return result;
}

/** Check whether a building can be placed at a cell: its tiles are free for it, it is in reach,
 * its build limit is not reached and the inventory holds one.
 *
 * @param ctx The context
 * @param prefab The building
 * @param row Row of the top left tile
 * @param col Column of the top left tile
 * @return Whether it can be placed
 */
static bool ore_can_place(
    const ore_place_ctx_t *ctx,
    ecs_entity_t prefab,
    int32_t row,
    int32_t col)
{
    const OreBuildLimit *limit = ecs_get(ctx->world, prefab, OreBuildLimit);
    int32_t w, h;
    ore_footprint(ctx->world, prefab, &w, &h);

    return ore_cell_free(ctx, prefab, row, col) && ore_in_reach(ctx, row, col, w, h) &&
        (!limit || limit->max <= 0 || ore_build_count(ctx->world, prefab) < limit->max) &&
        ore_inventory_count(ctx->world, prefab) > 0;
}

/** Compute the region of a placement, the original's oreRegion.
 *
 * @param anchor The anchor of a drag, NULL for a single cell at dest
 * @param dest The release tile, or the single cell
 * @param step_r Rows of the footprint
 * @param step_c Columns of the footprint
 * @param rect Fill the bounding box of a drag
 * @return The region
 */
static ore_region_t ore_region(
    const ore_cell_t *anchor,
    const ore_cell_t *dest,
    int32_t step_r,
    int32_t step_c,
    bool rect)
{
    int32_t row = dest->row, col = dest->col, ar, ac;
    float across, along;
    ore_region_t rg = {
        .r0 = row, .r1 = row, .c0 = col, .c1 = col,
        .anchor_r = row, .anchor_c = col, .dest_r = row, .dest_c = col
    };

    if (!anchor) {
        goto done;
    }

    ar = anchor->row;
    ac = anchor->col;
    rg.rect = rect;
    rg.r0 = ar < row ? ar : row;
    rg.r1 = ar > row ? ar : row;
    rg.c0 = ac < col ? ac : col;
    rg.c1 = ac > col ? ac : col;
    rg.anchor_r = ar;
    rg.anchor_c = ac;
    if (col == ac) {
        rg.horizontal = false;
    } else if (row == ar) {
        rg.horizontal = true;
    } else {
        across = col > ac ? dest->fx : 1.0f - dest->fx;
        along = row > ar ? dest->fz : 1.0f - dest->fz;
        rg.horizontal = across <= along;
    }

    if (rg.c0 < ac) {
        rg.c0 = ac - ((ac - rg.c0 + step_c - 1) / step_c) * step_c;
    }

    if (rg.r0 < ar) {
        rg.r0 = ar - ((ar - rg.r0 + step_r - 1) / step_r) * step_r;
    }

    rg.dest_c = col < ac ? rg.c0 : ac + ((col - ac) / step_c) * step_c;
    rg.dest_r = row < ar ? rg.r0 : ar + ((row - ar) / step_r) * step_r;

done:
    return rg;
}

/** Check whether a cell of the bounding box belongs to the region.
 *
 * @param rg The region
 * @param row Row of the cell
 * @param col Column of the cell
 * @return Whether it does: every cell of a rectangle, else those of the L
 */
static bool ore_region_has(
    const ore_region_t *rg,
    int32_t row,
    int32_t col)
{
    bool l = rg->horizontal ? (col == rg->anchor_c || row == rg->dest_r) :
        (row == rg->anchor_r || col == rg->dest_c);

    return rg->rect || l;
}

/** List the cells of a region in row major order of its box, at most max.
 *
 * @param rg The region
 * @param step_r Rows of the footprint
 * @param step_c Columns of the footprint
 * @param max Most cells, up to ORE_DRAG_MAX
 * @param rows Receives the rows of the cells
 * @param cols Receives the columns of the cells
 * @return Number of cells
 */
static int32_t ore_region_cells(
    const ore_region_t *rg,
    int32_t step_r,
    int32_t step_c,
    int32_t max,
    int32_t *rows,
    int32_t *cols)
{
    int32_t n = 0, r, c;
    for (r = rg->r0; r <= rg->r1 && n < max; r += step_r) {
        for (c = rg->c0; c <= rg->c1 && n < max; c += step_c) {
            if (ore_region_has(rg, r, c)) {
                rows[n] = r;
                cols[n] = c;
                n ++;
            }
        }
    }

    return n;
}

/** Get the most cells of a drag, cfg.dragMax within 1 and ORE_DRAG_MAX.
 *
 * @param world The world
 * @return The most cells
 */
static int32_t ore_drag_max(
    const ecs_world_t *world)
{
    int32_t result = ORE_CFG_I32(world, "dragMax");

    return result < 1 ? 1 : (result > ORE_DRAG_MAX ? ORE_DRAG_MAX : result);
}

/** Place the selected building at a cell when it can be, spending one, with a PoofBurst
 * cfg.placePoofY above the center of its footprint; the selection clears when none is left.
 *
 * @param world The world
 * @param ctx The context
 * @param pl The placement
 * @param row Row of the top left tile
 * @param col Column of the top left tile
 * @return Whether it was placed
 */
static bool ore_place_at(
    ecs_world_t *world,
    const ore_place_ctx_t *ctx,
    OrePlacement *pl,
    int32_t row,
    int32_t col)
{
    ecs_entity_t prefab = pl->selected;
    const OreEffects *fx = ecs_singleton_get(world, OreEffects);
    bool result = prefab && ore_can_place(ctx, prefab, row, col);
    int32_t w, h;
    if (result) {
        ore_footprint(world, prefab, &w, &h);
        vec3 at = {
            ore_tile_x(&ctx->grid, col) + (float)(w - 1) * ctx->grid.tile * 0.5f,
            ORE_CFG(world, "placePoofY"),
            ore_tile_z(&ctx->grid, row) + (float)(h - 1) * ctx->grid.tile * 0.5f
        };
        ore_building_place(world, prefab, row, col);
        ore_burst(world, fx ? fx->poof_burst : 0, at, 1.0f);
        ore_inventory_remove(world, prefab, 1);
        if (ore_inventory_count(world, prefab) <= 0) {
            pl->selected = 0;
        }
    }

    return result;
}

/** Place the selected building on the cells of a region, in row major order, one item each,
 * until it runs out; cells that cannot be placed are skipped. When nothing was placed and a
 * cell failed for its reach, the warning Too far away shows.
 *
 * @param world The world
 * @param pl The placement
 * @param anchor The anchor of a drag, NULL for a single cell
 * @param dest The release tile or the single cell
 * @param rect Fill the bounding box of a drag
 * @return Whether a building was placed
 */
static bool ore_place_region(
    ecs_world_t *world,
    OrePlacement *pl,
    const ore_cell_t *anchor,
    const ore_cell_t *dest,
    bool rect)
{
    OreMap *map = ecs_singleton_get_mut(world, OreMap);
    int32_t rows[ORE_DRAG_MAX], cols[ORE_DRAG_MAX];
    ore_place_ctx_t ctx;
    ore_region_t rg;
    bool placed = false, far = false;
    int32_t i, n, w, h;
    if (!map) {
        goto done;
    }

    ore_place_ctx_init(world, map, &ctx);
    ore_footprint(world, pl->selected, &w, &h);
    rg = ore_region(anchor, dest, h, w, rect);
    n = ore_region_cells(&rg, h, w, ore_drag_max(world), rows, cols);
    for (i = 0; i < n && pl->selected; i ++) {
        if (ore_place_at(world, &ctx, pl, rows[i], cols[i])) {
            placed = true;
        } else if (!far && ore_on_grid(&ctx.grid, rows[i] + h - 1, cols[i] + w - 1) &&
            !ore_in_reach(&ctx, rows[i], cols[i], w, h))
        {
            far = true;
        }
    }

    if (far && !placed) {
        ore_toast(world, true, "Too far away");
    }

done:
    return placed;
}

/** Find the tile under the cursor now, from the cursor ray of the world view.
 *
 * @param world The world
 * @param cell Receives the tile and the point inside it
 * @return Whether the cursor is on a tile of the grid
 */
static bool ore_cursor_cell(
    const ecs_world_t *world,
    ore_cell_t *cell)
{
    const OreView *view = ecs_singleton_get(world, OreView);
    const OreHover *last = ecs_singleton_get(world, OreHover);
    OreHover hover = last ? *last : (OreHover){ 0 };
    bool result = view && ore_hover_find(world, view, &hover);
    *cell = (ore_cell_t){ hover.row, hover.col, hover.fx, hover.fz };

    return result;
}

/** Find the building that goes on deposits, the drill: a prefab whose footprint wants them.
 *
 * @param world The world
 * @return The prefab, 0 for none
 */
static ecs_entity_t ore_drill_prefab(
    const ecs_world_t *world)
{
    const ecs_world_t *real = ecs_get_world(world);
    ecs_iter_t it = ecs_each_id(world, ecs_id(OreFootprint));
    ecs_entity_t result = 0;
    int32_t i;
    while (ecs_each_next(&it)) {
        const OreFootprint *fp = ecs_field(&it, OreFootprint, 0);
        for (i = 0; i < it.count && !result; i ++) {
            if (fp[i].deposit && ecs_table_has_id(real, it.table, EcsPrefab)) {
                result = it.entities[i];
            }
        }
    }

    return result;
}

bool ore_placement_cancel_drag(
    ecs_world_t *world)
{
    OrePlacement *pl = ecs_singleton_get_mut(world, OrePlacement);
    bool result = pl && pl->dragging && ore_state_is(world, OreGameStatePlaying);
    if (result) {
        pl->dragging = false;
        ecs_singleton_modified(world, OrePlacement);
        ore_toast(world, false, "Placement cancelled");
    }

    return result;
}

bool ore_placement_deselect(
    ecs_world_t *world)
{
    OrePlacement *pl = ecs_singleton_get_mut(world, OrePlacement);
    bool result = pl && pl->selected && ore_state_is(world, OreGameStatePlaying);
    if (result) {
        pl->selected = 0;
        ecs_singleton_modified(world, OrePlacement);
    }

    return result;
}

/** Select a building for placement with a key: the selected building again clears the
 * selection, a building the inventory holds none of does nothing.
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv The key: an action with OreSelectKey, or the building prefab itself
 * @param future Resolves with whether the selection changed
 */
static void ore_select_function(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    ecs_world_t *world = ctx->world;
    ecs_entity_t key = *(ecs_entity_t*)argv[0].ptr;
    const OreSelectKey *k = key && ecs_is_alive(world, key) ?
        ecs_get(world, key, OreSelectKey) : NULL;
    ecs_entity_t building = k ? k->building : key;
    OrePlacement *pl = ecs_singleton_get_mut(world, OrePlacement);
    bool result = false;
    (void)argc;

    if (pl && building && ecs_is_alive(world, building) &&
        ecs_get(world, building, OreFootprint) && ore_state_is(world, OreGameStatePlaying))
    {
        if (pl->selected == building) {
            pl->selected = 0;
            result = true;
        } else if (ore_inventory_count(world, building) > 0) {
            pl->selected = building;
            result = true;
        }
    }

    if (result) {
        ecs_singleton_modified(world, OrePlacement);
    }

    ore_resolve(future, result);
}

/** The pipette: the building under the cursor selects its type, else a deposit selects the
 * drill, else the selection clears. A type the inventory holds none of shows a warning and
 * keeps the selection.
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv No arguments
 * @param future Resolves with whether a building is selected
 */
static void ore_pipette_function(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    ecs_world_t *world = ctx->world;
    OrePlacement *pl = ecs_singleton_get_mut(world, OrePlacement);
    OreMap *map = ecs_singleton_get_mut(world, OreMap);
    ecs_entity_t *building = NULL, *deposit = NULL, prefab = 0;
    ore_cell_t cell;
    (void)argc;
    (void)argv;

    if (!pl || !map || !ore_state_is(world, OreGameStatePlaying)) {
        goto done;
    }

    if (ore_cursor_cell(world, &cell)) {
        building = ore_map_building(map, cell.row, cell.col);
        deposit = ore_map_deposit(map, cell.row, cell.col);
    }

    if (building && *building && ecs_is_alive(world, *building)) {
        prefab = ore_building_prefab(world, *building);
    }

    if (!prefab && deposit && *deposit) {
        prefab = ore_drill_prefab(world);
    }

    if (!prefab) {
        if (pl->selected) {
            pl->selected = 0;
            ecs_singleton_modified(world, OrePlacement);
        }
    } else if (ore_inventory_count(world, prefab) <= 0) {
        ore_toast(world, true, "No %s in inventory", ore_item_name(world, prefab));
    } else if (pl->selected != prefab) {
        pl->selected = prefab;
        ecs_singleton_modified(world, OrePlacement);
    }

done:
    ore_resolve(future, pl && pl->selected != 0);
}

/** The press of Place: with a building selected and the cursor on a tile, a drag starts,
 * anchored at the tile once.
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv No arguments
 * @param future Resolves with whether a drag started
 */
static void ore_place_start_function(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    ecs_world_t *world = ctx->world;
    OrePlacement *pl = ecs_singleton_get_mut(world, OrePlacement);
    ore_cell_t cell;
    bool result = pl && pl->selected && ore_state_is(world, OreGameStatePlaying) &&
        ore_cursor_cell(world, &cell);
    (void)argc;
    (void)argv;

    if (result) {
        pl->dragging = true;
        pl->anchor_row = cell.row;
        pl->anchor_col = cell.col;
        ecs_singleton_modified(world, OrePlacement);
    }

    ore_resolve(future, result);
}

/** The release of Place: ends the drag in flight and places its region, the anchor alone when
 * the cursor did not move since the press, else from the anchor to the tile under the cursor,
 * the rectangle when the run key is held.
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv No arguments
 * @param future Resolves with whether a building was placed
 */
static void ore_place_end_function(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    ecs_world_t *world = ctx->world;
    OrePlacement *pl = ecs_singleton_get_mut(world, OrePlacement);
    const OreActions *a = ecs_singleton_get(world, OreActions);
    const ActionState *place = a && a->place ? ecs_get(world, a->place, ActionState) : NULL;
    bool moved = place && (place->drag[0] != 0 || place->drag[1] != 0);
    bool result = false;
    ore_cell_t anchor, dest;
    (void)argc;
    (void)argv;

    if (!pl || !pl->dragging) {
        goto done;
    }

    pl->dragging = false;
    anchor = (ore_cell_t){ pl->anchor_row, pl->anchor_col, 0.5f, 0.5f };
    if (pl->selected && ore_state_is(world, OreGameStatePlaying)) {
        if (!moved) {
            result = ore_place_region(world, pl, NULL, &anchor, false);
        } else if (ore_cursor_cell(world, &dest)) {
            result = ore_place_region(world, pl, &anchor, &dest,
                ore_action_down(world, a->run));
        }
    }

    ecs_singleton_modified(world, OrePlacement);
done:
    ore_resolve(future, result);
}

/** A press of the Mine action throws the drag in flight away.
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv No arguments
 * @param future Resolves with whether a drag was in flight
 */
static void ore_cancel_drag_function(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    (void)argc;
    (void)argv;
    ore_resolve(future, ore_placement_cancel_drag(ctx->world));
}

/** Add a tile center to a list of preview markers, when it has room.
 *
 * @param xz The list, x and z of every entry in turn
 * @param count The entries in it
 * @param x X of the tile center
 * @param z Z of the tile center
 */
static void ore_preview_add(
    float *xz,
    int32_t *count,
    float x,
    float z)
{
    if (*count < ORE_PREVIEW_TILES) {
        xz[*count * 2] = x;
        xz[*count * 2 + 1] = z;
        (*count) ++;
    }
}

/** Compute the preview of the selected building: the cells of the region it would be placed
 * on now, each good when it can be placed counting the items the cells before it would use.
 *
 * @param ctx The context
 * @param pl The placement
 * @param hover The tile under the cursor, on the grid
 * @param rect Fill the bounding box of a drag
 * @param out Receives the markers and ghosts
 */
static void ore_preview_compute(
    const ore_place_ctx_t *ctx,
    const OrePlacement *pl,
    const OreHover *hover,
    bool rect,
    ore_preview_t *out)
{
    const ore_grid_t *grid = &ctx->grid;
    ore_cell_t anchor = { pl->anchor_row, pl->anchor_col, 0.5f, 0.5f };
    ore_cell_t dest = { hover->row, hover->col, hover->fx, hover->fz };
    int32_t rows[ORE_DRAG_MAX], cols[ORE_DRAG_MAX];
    int32_t stock = ore_inventory_count(ctx->world, pl->selected), used = 0;
    int32_t i, n, r, c, w, h;
    ore_region_t rg;
    bool ok;
    ore_footprint(ctx->world, pl->selected, &w, &h);
    rg = ore_region(pl->dragging ? &anchor : NULL, &dest, h, w, rect);
    n = ore_region_cells(&rg, h, w, ore_drag_max(ctx->world), rows, cols);
    for (i = 0; i < n; i ++) {
        ok = used < stock && ore_can_place(ctx, pl->selected, rows[i], cols[i]);
        used += ok;
        for (r = rows[i]; r < rows[i] + h; r ++) {
            for (c = cols[i]; c < cols[i] + w; c ++) {
                if (ore_on_grid(grid, r, c)) {
                    ore_preview_add(ok ? out->good[0] : out->bad[0],
                        ok ? &out->good_count : &out->bad_count,
                        ore_tile_x(grid, c), ore_tile_z(grid, r));
                }
            }
        }

        if (ore_on_grid(grid, rows[i] + h - 1, cols[i] + w - 1)) {
            out->ghost[out->ghost_count][0] =
                ore_tile_x(grid, cols[i]) + (float)(w - 1) * grid->tile * 0.5f;
            out->ghost[out->ghost_count][1] =
                ore_tile_z(grid, rows[i]) + (float)(h - 1) * grid->tile * 0.5f;
            out->ghost_ok[out->ghost_count] = ok;
            out->ghost_count ++;
        }
    }
}

/** The look of a ghost, read from cfg once per run of the preview */
typedef struct ore_ghost_look_t {
    Tint good;     /**< Tint of a ghost of a cell that can be placed, cfg.ghostGood */
    Tint bad;      /**< Tint of a ghost of a cell that cannot, cfg.ghostBad */
    float opacity; /**< cfg.ghostOpacity */
    float scale;   /**< cfg.ghostScale */
    float y;       /**< Height of a ghost, cfg.ghostY */
    float marker;  /**< Height of a marker, cfg.previewMarkerY */
} ore_ghost_look_t;

/** Move a ghost and tint it; written only when it changes.
 *
 * @param world The world
 * @param stage The stage of the system
 * @param ghost The ghost
 * @param look The look of the ghosts
 * @param at Footprint center (x, z)
 * @param ok The cell can be placed
 */
static void ore_ghost_show(
    const ecs_world_t *world,
    ecs_world_t *stage,
    ecs_entity_t ghost,
    const ore_ghost_look_t *look,
    const float *at,
    bool ok)
{
    const Position *p = ecs_get(world, ghost, Position);
    const Tint *tint = ecs_get(world, ghost, Tint);
    const Tint *want = ok ? &look->good : &look->bad;
    if (!p || p->x != at[0] || p->y != look->y || p->z != at[1]) {
        ecs_set(stage, ghost, Position, { at[0], look->y, at[1] });
    }

    if (!tint || tint->r != want->r || tint->g != want->g || tint->b != want->b ||
        tint->a != want->a)
    {
        ecs_set_ptr(stage, ghost, Tint, want);
    }
}

/** Create a ghost of a building: an instance of its prefab, translucent in the tint of its
 * cell, without shadows or emission (OreGhostDim).
 *
 * @param stage The stage of the system
 * @param root Parent of the preview
 * @param prefab The building prefab
 * @param look The look of the ghosts
 * @param at Footprint center (x, z)
 * @param ok The cell can be placed
 */
static void ore_ghost_create(
    ecs_world_t *stage,
    ecs_entity_t root,
    ecs_entity_t prefab,
    const ore_ghost_look_t *look,
    const float *at,
    bool ok)
{
    ecs_entity_t e = ecs_new_w_pair(stage, EcsChildOf, root);
    ecs_add_id(stage, e, OreGhost);
    ecs_add_id(stage, e, NoShadow);
    ecs_add_id(stage, e, DynamicTransform);
    ecs_add_pair(stage, e, EcsIsA, prefab);
    ecs_set(stage, e, Position, { at[0], look->y, at[1] });
    ecs_set(stage, e, Scale, { look->scale, look->scale, look->scale });
    ecs_set_ptr(stage, e, Tint, ok ? &look->good : &look->bad);
    ecs_set(stage, e, Opacity, { look->opacity });
}

/** Create a tile marker of the preview.
 *
 * @param stage The stage of the system
 * @param root Parent of the preview
 * @param prefab TileMarker or TileMarkerBad
 * @param x X of the tile center
 * @param y Height of the marker
 * @param z Z of the tile center
 */
static void ore_preview_marker_create(
    ecs_world_t *stage,
    ecs_entity_t root,
    ecs_entity_t prefab,
    float x,
    float y,
    float z)
{
    ecs_entity_t e = ecs_new_w_pair(stage, EcsChildOf, root);
    ecs_add_id(stage, e, DynamicTransform);
    ecs_add_pair(stage, e, EcsIsA, prefab);
    ecs_set(stage, e, Position, { x, y, z });
}

/** Show a preview with the pooled children of its root: the markers move to the tiles and the
 * extra ones hide, the ghosts move to the cells and the extra ones (or those of another
 * building) are deleted; what the pool lacks is created.
 *
 * @param world The world
 * @param stage The stage of the system
 * @param preview The preview singleton
 * @param prefab The selected building, 0 for none
 * @param out The markers and ghosts to show
 */
static void ore_preview_apply(
    const ecs_world_t *world,
    ecs_world_t *stage,
    const OrePreview *preview,
    ecs_entity_t prefab,
    const ore_preview_t *out)
{
    ore_ghost_look_t look = {
        .good = ecs_const_var_get_t(world, "cfg.ghostGood", Tint),
        .bad = ecs_const_var_get_t(world, "cfg.ghostBad", Tint),
        .opacity = ORE_CFG(world, "ghostOpacity"),
        .scale = ORE_CFG(world, "ghostScale"),
        .y = ORE_CFG(world, "ghostY"),
        .marker = ORE_CFG(world, "previewMarkerY")
    };
    ecs_entity_t root = preview->root;
    int32_t gi = 0, bi = 0, ki = 0, i;
    ecs_iter_t it;
    bool show;
    if (!root || !ecs_is_alive(world, root)) {
        goto done;
    }

    it = ecs_children(world, root);
    while (ecs_children_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            ecs_entity_t e = it.entities[i];
            if (ecs_has_id(world, e, OreGhost)) {
                if (prefab && ki < out->ghost_count && ecs_has_pair(world, e, EcsIsA, prefab)) {
                    ore_ghost_show(world, stage, e, &look, out->ghost[ki], out->ghost_ok[ki]);
                    ki ++;
                } else {
                    ecs_delete(stage, e);
                }
            } else if (preview->good && ecs_has_pair(world, e, EcsIsA, preview->good)) {
                show = gi < out->good_count;
                ore_marker_show(world, stage, e, show, show ? out->good[gi][0] : 0, look.marker,
                    show ? out->good[gi][1] : 0);
                gi += show;
            } else if (preview->bad && ecs_has_pair(world, e, EcsIsA, preview->bad)) {
                show = bi < out->bad_count;
                ore_marker_show(world, stage, e, show, show ? out->bad[bi][0] : 0, look.marker,
                    show ? out->bad[bi][1] : 0);
                bi += show;
            }
        }
    }

    for (; preview->good && gi < out->good_count; gi ++) {
        ore_preview_marker_create(stage, root, preview->good, out->good[gi][0], look.marker,
            out->good[gi][1]);
    }

    for (; preview->bad && bi < out->bad_count; bi ++) {
        ore_preview_marker_create(stage, root, preview->bad, out->bad[bi][0], look.marker,
            out->bad[bi][1]);
    }

    for (; prefab && ki < out->ghost_count; ki ++) {
        ore_ghost_create(stage, root, prefab, &look, out->ghost[ki], out->ghost_ok[ki]);
    }

done:
    return;
}

/** Show the preview of the selected building on the cells of the current region while the
 * cursor is on the grid.
 *
 * @param it Iterator with OrePlacement, OreHover, OreMap, OrePreview and OreActions, while
 * playing
 */
static void OrePlacePreview(
    ecs_iter_t *it)
{
    const OrePlacement *pl = ecs_field(it, OrePlacement, 0);
    const OreHover *hover = ecs_field(it, OreHover, 1);
    OreMap *map = ecs_field(it, OreMap, 2);
    const OrePreview *preview = ecs_field(it, OrePreview, 3);
    const OreActions *a = ecs_field(it, OreActions, 4);
    ecs_entity_t prefab = pl->selected && ecs_is_alive(it->world, pl->selected) ?
        pl->selected : 0;
    ore_preview_t *out = ecs_os_calloc_t(ore_preview_t);
    ore_place_ctx_t ctx;
    if (prefab && hover->on_grid) {
        ore_place_ctx_init(it->world, map, &ctx);
        ore_preview_compute(&ctx, pl, hover, ore_action_down(it->world, a->run), out);
    }

    ore_preview_apply(it->world, it->stage, preview, prefab, out);
    ecs_os_free(out);
}

/** Hide the preview outside of play.
 *
 * @param it Iterator with OrePreview, while not playing
 */
static void OrePlacePreviewHide(
    ecs_iter_t *it)
{
    const OrePreview *preview = ecs_field(it, OrePreview, 0);
    ore_preview_t *out = ecs_os_calloc_t(ore_preview_t);
    ore_preview_apply(it->world, it->stage, preview, 0, out);
    ecs_os_free(out);
}

void ore_placement_import(
    ecs_world_t *world)
{
    ecs_entity_t module = ecs_get_scope(world);
    ecs_entity_t state = ecs_id(OreGameState);
    ecs_id_t playing = ore_state_pair(world, OreGameStatePlaying);
    struct {
        const char *name;
        ecs_entity_t param;
        ecs_async_function_callback_t callback;
    } functions[] = {
        { "select", ecs_id(ecs_entity_t), ore_select_function },
        { "pipette", 0, ore_pipette_function },
        { "placeStart", 0, ore_place_start_function },
        { "placeEnd", 0, ore_place_end_function },
        { "cancelDrag", 0, ore_cancel_drag_function }
    };
    int32_t i;

    ecs_singleton_ensure(world, OrePlacement);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OrePlacePreview" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OrePlacement), .src.id = ecs_id(OrePlacement), .inout = EcsIn },
            { .id = ecs_id(OreHover), .src.id = ecs_id(OreHover), .inout = EcsIn },
            { .id = ecs_id(OreMap), .src.id = ecs_id(OreMap), .inout = EcsIn },
            { .id = ecs_id(OrePreview), .src.id = ecs_id(OrePreview), .inout = EcsIn },
            { .id = ecs_id(OreActions), .src.id = ecs_id(OreActions), .inout = EcsIn },
            { .id = playing, .src.id = state }
        },
        .callback = OrePlacePreview
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OrePlacePreviewHide" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OrePreview), .src.id = ecs_id(OrePreview), .inout = EcsIn },
            { .id = playing, .src.id = state, .oper = EcsNot }
        },
        .callback = OrePlacePreviewHide
    });

    for (i = 0; i < (int32_t)(sizeof(functions) / sizeof(functions[0])); i ++) {
        ecs_async_function(world, {
            .name = functions[i].name,
            .parent = module,
            .return_type = ecs_id(ecs_bool_t),
            .params = {{ functions[i].param ? "key" : NULL, functions[i].param }},
            .callback = functions[i].callback
        });
    }
}

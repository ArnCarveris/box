/**
 * @file buildings.c
 * @brief The buildings: placed buildings register in the tile grid, the connector rules join
 * neighbors, destroyed ones are deleted, and a demolished one goes back to the inventory.
 *
 * A building is an instance of its prefab (etc/buildings) with OreBuilding, its top left tile;
 * every tile of its footprint maps to it in OreMap while it is set.
 *
 * OreConnectorSync places the connectors of the tiles whose building changed by the rules of
 * rules.flecs, the auto tiling of the original's rules.c, under the run's root of OrePlacement.
 */

#include "game.h"
#include <math.h>

void ore_footprint(
    const ecs_world_t *world,
    ecs_entity_t building,
    int32_t *w,
    int32_t *h)
{
    const OreFootprint *fp = building ? ecs_get(world, building, OreFootprint) : NULL;
    *w = fp && fp->w > 0 ? fp->w : 1;
    *h = fp && fp->h > 0 ? fp->h : 1;
}

ecs_entity_t ore_building_prefab(
    const ecs_world_t *world,
    ecs_entity_t building)
{
    return building ? ecs_get_target(world, building, EcsIsA, 0) : 0;
}

ecs_entity_t ore_building_place(
    ecs_world_t *world,
    ecs_entity_t prefab,
    int32_t row,
    int32_t col)
{
    const OrePlacement *pl = ecs_singleton_get(world, OrePlacement);
    ore_grid_t grid = ore_grid_get(world);
    ecs_entity_t root = pl && pl->root && ecs_is_alive(world, pl->root) ? pl->root : 0;
    ecs_entity_t e = root ? ecs_new_w_pair(world, EcsChildOf, root) : ecs_new(world);
    int32_t w, h;
    ore_footprint(world, prefab, &w, &h);
    ecs_add_pair(world, e, EcsIsA, prefab);
    ecs_set(world, e, Position, {
        ore_tile_x(&grid, col) + (float)(w - 1) * grid.tile * 0.5f,
        0,
        ore_tile_z(&grid, row) + (float)(h - 1) * grid.tile * 0.5f
    });
    ecs_set(world, e, OreBuilding, { .row = row, .col = col });

    return e;
}

/** Salvage a damaged building: for every input of its recipe trunc(amount * value / max) goes
 * into the inventory, with the info toast of what came back. Refused with a warning when the
 * new kinds do not fit.
 *
 * @param world The world or the stage of a system
 * @param recipe The recipe of the building
 * @param health The health of the building, value below max
 * @return Whether the salvage went into the inventory
 */
static bool ore_building_salvage(
    ecs_world_t *world,
    const OreRecipe *recipe,
    const OreHealth *health)
{
    OreStack stacks[ORE_RECIPE_INPUTS];
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    float frac = health->value / health->max;
    int32_t i, count = 0, given = 0;
    bool result = false;
    char *text;
    frac = frac < 0 ? 0 : frac;
    for (i = 0; i < ORE_RECIPE_INPUTS && recipe->inputs[i].item; i ++) {
        if (recipe->inputs[i].amount > 0) {
            stacks[count].item = recipe->inputs[i].item;
            stacks[count].amount = (int32_t)((float)recipe->inputs[i].amount * frac);
            count ++;
        }
    }

    if (!ore_inventory_fits(world, stacks, count)) {
        ore_toast(world, true, "Inventory full - can't salvage");
        goto done;
    }

    ecs_strbuf_appendstr(&buf, "Salvaged ");
    for (i = 0; i < count; i ++) {
        if (stacks[i].amount > 0 && ore_inventory_add(world, stacks[i].item, stacks[i].amount)) {
            ecs_strbuf_append(&buf, "%s%d %s", given ? ", " : "", stacks[i].amount,
                ore_item_name(world, stacks[i].item));
            given ++;
        }
    }

    if (!given) {
        ecs_strbuf_appendstr(&buf, "nothing");
    }

    ecs_strbuf_append(&buf, " (%d%%)", (int32_t)(frac * 100.0f + 0.5f));
    text = ecs_strbuf_get(&buf);
    ore_toast(world, false, "%s", text);
    ecs_os_free(text);
    result = true;
done:
    return result;
}

bool ore_building_demolish(
    ecs_world_t *world,
    ecs_entity_t building)
{
    ecs_entity_t prefab = ore_building_prefab(world, building);
    const OreHealth *health = ecs_get(world, building, OreHealth);
    const OreRecipe *recipe = prefab ? ecs_get(world, prefab, OreRecipe) : NULL;
    bool damaged = health && health->max > 0 && health->value < health->max && recipe;
    bool result;
    if (damaged) {
        result = ore_building_salvage(world, recipe, health);
    } else {
        result = !prefab || ore_inventory_add(world, prefab, 1);
    }

    if (result) {
        ecs_delete(world, building);
    }

    return result;
}

/** Register buildings in the tiles of their footprints and mark the tiles for the connectors.
 *
 * @param it Iterator over the buildings whose OreBuilding was set
 */
static void OreBuildingRegister(
    ecs_iter_t *it)
{
    const OreBuilding *b = ecs_field(it, OreBuilding, 0);
    OreMap *map = ecs_singleton_get_mut(it->world, OreMap);
    ore_grid_t grid = ore_grid_get(it->world);
    int32_t i, r, c, w, h;
    ecs_entity_t *cell;
    if (map) {
        ore_map_fit(map, &grid);
    }

    for (i = 0; map && i < it->count; i ++) {
        ore_footprint(it->world, it->entities[i], &w, &h);
        for (r = b[i].row; r < b[i].row + h; r ++) {
            for (c = b[i].col; c < b[i].col + w; c ++) {
                cell = ore_map_building(map, r, c);
                if (cell) {
                    *cell = it->entities[i];
                    ore_map_touch(map, r, c);
                }
            }
        }
    }
}

/** Free the tiles of buildings that are removed and mark them for the connectors.
 *
 * @param it Iterator over the buildings that lose OreBuilding
 */
static void OreBuildingUnregister(
    ecs_iter_t *it)
{
    const OreBuilding *b = ecs_field(it, OreBuilding, 0);
    OreMap *map = ecs_singleton_get_mut(it->world, OreMap);
    int32_t i, r, c, w, h;
    ecs_entity_t *cell;
    for (i = 0; map && i < it->count; i ++) {
        ore_footprint(it->world, it->entities[i], &w, &h);
        for (r = b[i].row; r < b[i].row + h; r ++) {
            for (c = b[i].col; c < b[i].col + w; c ++) {
                cell = ore_map_building(map, r, c);
                if (cell && *cell == it->entities[i]) {
                    *cell = 0;
                    ore_map_touch(map, r, c);
                }
            }
        }
    }
}

/** Clear the edges of connectors that are removed.
 *
 * @param it Iterator over the connectors that lose OreConnector
 */
static void OreConnectorUnregister(
    ecs_iter_t *it)
{
    const OreConnector *k = ecs_field(it, OreConnector, 0);
    OreMap *map = ecs_singleton_get_mut(it->world, OreMap);
    int32_t i;
    for (i = 0; map && i < it->count; i ++) {
        ecs_entity_t *cell = ore_map_link(map, k[i].row, k[i].col, k[i].vertical);
        if (cell && *cell == it->entities[i]) {
            *cell = 0;
        }
    }
}

/** Check a slot of a rule against the building of a tile: an empty slot accepts any tile, else
 * the building must be an instance of one of its prefabs.
 *
 * @param world The world
 * @param slot The prefabs of the slot, 0 after the last
 * @param building The building of the tile, 0 for none
 * @return Whether the slot accepts the tile
 */
static bool ore_rule_slot(
    const ecs_world_t *world,
    const ecs_entity_t *slot,
    ecs_entity_t building)
{
    bool any = false, result = false;
    int32_t i;
    for (i = 0; i < ORE_RULE_SLOT && slot[i]; i ++) {
        any = true;
        result |= building && ecs_is_alive(world, building) &&
            ecs_has_pair(world, building, EcsIsA, slot[i]);
    }

    return !any || result;
}

/** Check whether a slot of a rule names any building.
 *
 * @param slot The prefabs of the slot
 * @return Whether it is not empty
 */
static bool ore_rule_named(
    const ecs_entity_t *slot)
{
    return slot[0] != 0;
}

/** Match a rule against the buildings of the two tiles of an edge, as the original's
 * oreRuleMatches.
 *
 * @param world The world
 * @param rule The rule
 * @param first Building of the west or north tile
 * @param second Building of the east or south tile
 * @param vertical The tiles are in one column
 * @return Whether the rule joins them
 */
static bool ore_rule_matches(
    const ecs_world_t *world,
    const OreConnectRule *rule,
    ecs_entity_t first,
    ecs_entity_t second,
    bool vertical)
{
    bool horizontal_pattern = ore_rule_named(rule->left) || ore_rule_named(rule->right);
    bool vertical_pattern = ore_rule_named(rule->top) || ore_rule_named(rule->bottom);
    const ecs_entity_t *a = NULL, *b = NULL;
    if (!vertical) {
        if (horizontal_pattern) {
            a = rule->left;
            b = rule->right;
        }
    } else if (vertical_pattern) {
        a = rule->top;
        b = rule->bottom;
    } else if (rule->rotate && horizontal_pattern) {
        a = rule->left;
        b = rule->right;
    }

    return a && ((ore_rule_slot(world, a, first) && ore_rule_slot(world, b, second)) ||
        (rule->rotate && ore_rule_slot(world, b, first) && ore_rule_slot(world, a, second)));
}

/** Find the connector of the first rule that joins the buildings of an edge.
 *
 * @param world The world
 * @param first Building of the west or north tile
 * @param second Building of the east or south tile
 * @param vertical The tiles are in one column
 * @return The connector prefab, 0 for none
 */
static ecs_entity_t ore_rule_out(
    const ecs_world_t *world,
    ecs_entity_t first,
    ecs_entity_t second,
    bool vertical)
{
    ecs_iter_t it = ecs_each_id(world, ecs_id(OreConnectRule));
    ecs_entity_t result = 0;
    int32_t i;
    while (ecs_each_next(&it)) {
        const OreConnectRule *rule = ecs_field(&it, OreConnectRule, 0);
        for (i = 0; i < it.count && !result; i ++) {
            if (rule[i].out && ecs_is_alive(world, rule[i].out) &&
                ore_rule_matches(world, &rule[i], first, second, vertical))
            {
                result = rule[i].out;
            }
        }
    }

    return result;
}

/** Place, keep or remove the connector of the edge between a tile and its east or south
 * neighbor by the rules.
 *
 * @param world The world
 * @param stage The stage of the system
 * @param map The tile grid
 * @param grid The grid of cfg
 * @param root Parent of the connectors, 0 for none
 * @param row Row of the west or north tile
 * @param col Column of the west or north tile
 * @param vertical The edge to the south neighbor
 */
static void ore_connector_sync(
    const ecs_world_t *world,
    ecs_world_t *stage,
    OreMap *map,
    const ore_grid_t *grid,
    ecs_entity_t root,
    int32_t row,
    int32_t col,
    bool vertical)
{
    ecs_entity_t *link = ore_map_link(map, row, col, vertical);
    ecs_entity_t *first = ore_map_building(map, row, col);
    ecs_entity_t *second = ore_map_building(map, row + vertical, col + !vertical);
    ecs_entity_t out, instance, e;
    float half = grid->tile * 0.5f;
    if (!link || !first || !second) {
        goto done;
    }

    out = ore_rule_out(world, *first, *second, vertical);
    instance = *link;
    if (instance && ecs_is_alive(world, instance) && out &&
        ecs_has_pair(world, instance, EcsIsA, out))
    {
        goto done;
    }

    if (instance) {
        if (ecs_is_alive(world, instance)) {
            ecs_delete(stage, instance);
        }

        *link = 0;
    }

    if (out) {
        e = root ? ecs_new_w_pair(stage, EcsChildOf, root) : ecs_new(stage);
        ecs_add_pair(stage, e, EcsIsA, out);
        ecs_set(stage, e, Position, {
            ore_tile_x(grid, col) + (vertical ? 0 : half),
            0,
            ore_tile_z(grid, row) + (vertical ? half : 0)
        });
        ecs_set(stage, e, Rotation, { 0, vertical ? GLM_PI_2f : 0, 0 });
        ecs_set(stage, e, OreConnector, { .row = row, .col = col, .vertical = vertical });
        *link = e;
    }

done:
    return;
}

/** Sync the connectors of the edges of every tile whose building changed since the previous
 * frame. Runs in every state, so a restart's deletions and the scenes' buildings are covered.
 *
 * @param it Iterator with the OreMap singleton
 */
static void OreConnectorSync(
    ecs_iter_t *it)
{
    OreMap *map = ecs_field(it, OreMap, 0);
    const OrePlacement *pl = ecs_singleton_get(it->world, OrePlacement);
    ecs_entity_t root = pl && pl->root && ecs_is_alive(it->world, pl->root) ? pl->root : 0;
    ore_grid_t grid = ore_grid_get(it->world);
    int32_t i, row, col, count = ecs_vec_count(&map->dirty);
    for (i = 0; map->cols > 0 && i < count; i ++) {
        int32_t index = *ecs_vec_get_t(&map->dirty, int32_t, i);
        row = index / map->cols;
        col = index % map->cols;
        ore_connector_sync(it->world, it->stage, map, &grid, root, row, col - 1, false);
        ore_connector_sync(it->world, it->stage, map, &grid, root, row, col, false);
        ore_connector_sync(it->world, it->stage, map, &grid, root, row - 1, col, true);
        ore_connector_sync(it->world, it->stage, map, &grid, root, row, col, true);
    }

    if (count) {
        ecs_vec_clear(&map->dirty);
    }
}

/** Delete the buildings whose health dropped to 0 or below.
 *
 * @param it Iterator over the buildings with their own health, while playing
 */
static void OreBuildingDestroy(
    ecs_iter_t *it)
{
    const OreHealth *health = ecs_field(it, OreHealth, 1);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        if (health[i].value <= 0) {
            ecs_delete(it->stage, it->entities[i]);
        }
    }
}

/** Turn off the emission of the parts of a ghost, so it shows in its tint only.
 *
 * @param it Iterator over the emissive parts below an OreGhost
 */
static void OreGhostDim(
    ecs_iter_t *it)
{
    const Emissive *em = ecs_field(it, Emissive, 0);
    bool self = ecs_field_is_self(it, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        const Emissive *cur = self ? &em[i] : em;
        if (cur->strength != 0) {
            ecs_set(it->stage, it->entities[i], Emissive, { .strength = 0, .color = cur->color });
        }
    }
}

/** Turn off the point lights of the parts of a ghost.
 *
 * @param it Iterator over the lit parts below an OreGhost
 */
static void OreGhostDark(
    ecs_iter_t *it)
{
    const PointLight *light = ecs_field(it, PointLight, 0);
    bool self = ecs_field_is_self(it, 0);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        const PointLight *cur = self ? &light[i] : light;
        if (cur->intensity != 0) {
            PointLight next = *cur;
            next.intensity = 0;
            ecs_set_ptr(it->stage, it->entities[i], PointLight, &next);
        }
    }
}

void ore_buildings_import(
    ecs_world_t *world)
{
    ecs_entity_t state = ecs_id(OreGameState);
    ecs_id_t playing = ore_state_pair(world, OreGameStatePlaying);

    ecs_add_pair(world, ecs_id(OreFootprint), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(OreBuildLimit), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(OrePowerConsumer), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(OrePowerProducer), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(OrePowerCategory), EcsOnInstantiate, EcsInherit);

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "OreBuildingRegister" }),
        .query.terms = {{ .id = ecs_id(OreBuilding), .src.id = EcsSelf }},
        .events = { EcsOnSet },
        .callback = OreBuildingRegister
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "OreBuildingUnregister" }),
        .query.terms = {{ .id = ecs_id(OreBuilding), .src.id = EcsSelf, .inout = EcsIn }},
        .events = { EcsOnRemove },
        .callback = OreBuildingUnregister
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "OreConnectorUnregister" }),
        .query.terms = {{ .id = ecs_id(OreConnector), .src.id = EcsSelf, .inout = EcsIn }},
        .events = { EcsOnRemove },
        .callback = OreConnectorUnregister
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreConnectorSync" }),
        .phase = EcsOnLoad,
        .query.terms = {
            { .id = ecs_id(OreMap), .src.id = ecs_id(OreMap), .inout = EcsInOut }
        },
        .callback = OreConnectorSync
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreBuildingDestroy" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreBuilding), .src.id = EcsSelf, .inout = EcsInOutNone },
            { .id = ecs_id(OreHealth), .src.id = EcsSelf, .inout = EcsIn },
            { .id = playing, .src.id = state }
        },
        .callback = OreBuildingDestroy
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreGhostDim" }),
        .phase = EcsPostUpdate,
        .query.terms = {
            { .id = ecs_id(Emissive), .src.id = EcsSelf | EcsUp, .trav = EcsIsA,
              .inout = EcsIn },
            { .id = OreGhost, .src.id = EcsUp, .trav = EcsChildOf }
        },
        .callback = OreGhostDim
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreGhostDark" }),
        .phase = EcsPostUpdate,
        .query.terms = {
            { .id = ecs_id(PointLight), .src.id = EcsSelf | EcsUp, .trav = EcsIsA,
              .inout = EcsIn },
            { .id = OreGhost, .src.id = EcsUp, .trav = EcsChildOf }
        },
        .callback = OreGhostDark
    });
}

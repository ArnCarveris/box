/**
 * @file inventory_screen.c
 * @brief The inventory screen (E): whether it is open and its tab (OreInventoryScreen), the item
 * of its info panel (OreItemInfo), the cancel step that closes it and the script functions
 * oreelse.inventory_toggle(), oreelse.inventory_tab(), oreelse.inventory_pick(),
 * oreelse.inventory_order() and oreelse.inventory_hover().
 *
 * The screen is game state, reset with the run by run.flecs, and opens and closes only while
 * playing. While open the right button does not mine, the left one does not place, the pipette
 * does nothing and the preview hides (mining.c and placement.c ask ore_inventory_screen_open).
 *
 * The UI (etc/ui/inventory.flecs) draws it and tells the game what the pointer is over.
 */

#include "game.h"

bool ore_inventory_screen_open(
    const ecs_world_t *world)
{
    const OreInventoryScreen *screen = ecs_singleton_get(world, OreInventoryScreen);

    return screen && screen->open && ore_state_is(world, OreGameStatePlaying);
}

/** Forget what the pointer was over, the elements of the screen being gone: the item of the
 * info panel follows with OreItemInfoUpdate.
 *
 * @param world The world
 * @param slots Forget the stock slot
 * @param buttons Forget the slot button
 */
static void ore_item_info_forget(
    ecs_world_t *world,
    bool slots,
    bool buttons)
{
    OreItemInfo *info = ecs_singleton_get_mut(world, OreItemInfo);
    bool changed = false;
    if (info && slots && info->slot != -1) {
        info->slot = -1;
        changed = true;
    }

    if (info && buttons && info->button) {
        info->button = 0;
        changed = true;
    }

    if (changed) {
        ecs_singleton_modified(world, OreItemInfo);
    }
}

bool ore_inventory_screen_set(
    ecs_world_t *world,
    bool open)
{
    OreInventoryScreen *screen = ecs_singleton_get_mut(world, OreInventoryScreen);
    bool result = screen && screen->open != open && ore_state_is(world, OreGameStatePlaying);
    if (result) {
        if (open) {
            ore_placement_cancel_drag(world);
        }

        screen->open = open;
        ecs_singleton_modified(world, OreInventoryScreen);
        ore_item_info_forget(world, true, true);
    }

    return result;
}

bool ore_inventory_screen_cancel(
    ecs_world_t *world)
{
    return ore_inventory_screen_open(world) && ore_inventory_screen_set(world, false);
}

/** E and the Inventory (E) button: open the screen, or close it while open.
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv No arguments
 * @param future Resolves with whether the screen opened or closed
 */
static void ore_inventory_toggle_function(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    const OreInventoryScreen *screen = ecs_singleton_get(ctx->world, OreInventoryScreen);
    (void)argc;
    (void)argv;

    ore_resolve(future, screen && ore_inventory_screen_set(ctx->world, !screen->open));
}

/** Select a tab of the construct section while the screen is open: Tab (-1) flips to the
 * other one, a click on a tab button selects its tab.
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv The tab, an i32: 0 Buildings, 1 Rocket, -1 the next one
 * @param future Resolves with whether the tab changed
 */
static void ore_inventory_tab_function(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    ecs_world_t *world = ctx->world;
    OreInventoryScreen *screen = ecs_singleton_get_mut(world, OreInventoryScreen);
    int32_t tab = *(int32_t*)argv[0].ptr;
    bool result = false;
    (void)argc;

    if (screen && ore_inventory_screen_open(world)) {
        tab = tab < 0 ? (screen->tab + 1) % ORE_INVENTORY_TABS : tab;
        result = tab < ORE_INVENTORY_TABS && tab != screen->tab;
    }

    if (result) {
        screen->tab = tab;
        ecs_singleton_modified(world, OreInventoryScreen);
        ore_item_info_forget(world, false, true);
    }

    ore_resolve(future, result);
}

/** A click on a stock slot: a building there toggles its selection for placement (the selected
 * one again clears it) and closes the screen; any other item does nothing.
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv The slot, an i32
 * @param future Resolves with whether the selection changed
 */
static void ore_inventory_pick_function(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    ecs_world_t *world = ctx->world;
    const OreInventory *inv = ecs_singleton_get(world, OreInventory);
    OrePlacement *pl = ecs_singleton_get_mut(world, OrePlacement);
    int32_t slot = *(int32_t*)argv[0].ptr;
    ecs_entity_t item = 0;
    bool result = false;
    (void)argc;

    if (inv && pl && slot >= 0 && slot < ORE_INVENTORY_SLOTS && inv->slots[slot].amount > 0 &&
        ore_inventory_screen_open(world))
    {
        item = inv->slots[slot].item;
        result = item && ecs_is_alive(world, item) && ecs_get(world, item, OreFootprint);
    }

    if (result) {
        pl->selected = pl->selected == item ? 0 : item;
        ecs_singleton_modified(world, OrePlacement);
        ore_inventory_screen_set(world, false);
    }

    ore_resolve(future, result);
}

/** A click on a slot button of the construct section: order one of its item, a batch of
 * cfg.craftBatch with Shift held at the click. The screen stays open.
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv The item, an entity, and whether Shift was held, a bool
 * @param future Resolves with whether a unit was ordered
 */
static void ore_inventory_order_function(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    ecs_world_t *world = ctx->world;
    ecs_entity_t item = *(ecs_entity_t*)argv[0].ptr;
    bool batch = *(bool*)argv[1].ptr;
    int32_t count = batch ? ORE_CFG_I32(world, "craftBatch") : 1;
    (void)argc;

    ore_resolve(future, item && ecs_is_alive(world, item) &&
        ore_inventory_screen_open(world) && ore_craft_order(world, item, count) > 0);
}

/** The pointer came over or left an element of the screen with an item: a stock slot (slot 0
 * and up) or a slot button (slot -1 and its item). Leaving forgets the element only when it is
 * the one the pointer was last over.
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv The slot, an i32, the item of a slot button, an entity, and whether the pointer
 * came over it, a bool
 * @param future Resolves with whether what the pointer is over changed
 */
static void ore_inventory_hover_function(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    ecs_world_t *world = ctx->world;
    OreItemInfo *info = ecs_singleton_get_mut(world, OreItemInfo);
    int32_t slot = *(int32_t*)argv[0].ptr;
    ecs_entity_t item = *(ecs_entity_t*)argv[1].ptr;
    bool over = *(bool*)argv[2].ptr;
    OreItemInfo next;
    bool result = false;
    (void)argc;

    if (info) {
        next = *info;
        slot = slot < ORE_INVENTORY_SLOTS ? slot : -1;
        if (over) {
            next.slot = slot >= 0 ? slot : -1;
            next.button = slot >= 0 ? 0 : item;
        } else if (slot >= 0 && next.slot == slot) {
            next.slot = -1;
        } else if (slot < 0 && item && next.button == item) {
            next.button = 0;
        }

        result = next.slot != info->slot || next.button != info->button;
    }

    if (result) {
        info->slot = next.slot;
        info->button = next.button;
        ecs_singleton_modified(world, OreItemInfo);
    }

    ore_resolve(future, result);
}

/** Derive the item of the info panel while the screen is open: the item of the stock slot under
 * the pointer while it holds one, else that of the slot button, and whether it is a building in
 * a stock slot.
 *
 * When it changes OreNeeds names it for its needs, unless it is a building to place or has no
 * recipe.
 *
 * @param it Iterator with OreItemInfo, OreInventoryScreen, OreInventory and OreNeeds, while
 * playing
 */
static void OreItemInfoUpdate(
    ecs_iter_t *it)
{
    OreItemInfo *info = ecs_field(it, OreItemInfo, 0);
    const OreInventoryScreen *screen = ecs_field(it, OreInventoryScreen, 1);
    const OreInventory *inv = ecs_field(it, OreInventory, 2);
    OreNeeds *needs = ecs_field(it, OreNeeds, 3);
    ecs_entity_t item = 0, want = 0;
    bool place = false;
    if (screen->open && info->slot >= 0 && info->slot < ORE_INVENTORY_SLOTS &&
        inv->slots[info->slot].item && inv->slots[info->slot].amount > 0)
    {
        item = inv->slots[info->slot].item;
        place = ecs_get(it->world, item, OreFootprint) != NULL;
    } else if (screen->open && info->button && ecs_is_alive(it->world, info->button)) {
        item = info->button;
    }

    if (item != info->item || place != info->place) {
        info->item = item;
        info->place = place;
        ecs_singleton_modified(it->stage, OreItemInfo);

        want = item && !place && ecs_get(it->world, item, OreRecipe) ? item : 0;
        if (needs->item != want) {
            needs->item = want;
            ecs_singleton_modified(it->stage, OreNeeds);
        }
    }
}

void ore_inventory_screen_import(
    ecs_world_t *world)
{
    ecs_entity_t module = ecs_get_scope(world);
    ecs_entity_t state = ecs_id(OreGameState);
    ecs_id_t playing = ore_state_pair(world, OreGameStatePlaying);

    ecs_singleton_ensure(world, OreInventoryScreen);
    ecs_singleton_set(world, OreItemInfo, { .slot = -1 });
    ecs_singleton_ensure(world, OreNeeds);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreItemInfoUpdate" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreItemInfo), .src.id = ecs_id(OreItemInfo), .inout = EcsInOut },
            { .id = ecs_id(OreInventoryScreen), .src.id = ecs_id(OreInventoryScreen),
              .inout = EcsIn },
            { .id = ecs_id(OreInventory), .src.id = ecs_id(OreInventory), .inout = EcsIn },
            { .id = ecs_id(OreNeeds), .src.id = ecs_id(OreNeeds), .inout = EcsInOut },
            { .id = playing, .src.id = state }
        },
        .callback = OreItemInfoUpdate
    });

    ecs_async_function(world, {
        .name = "inventory_toggle",
        .parent = module,
        .return_type = ecs_id(ecs_bool_t),
        .callback = ore_inventory_toggle_function
    });

    ecs_async_function(world, {
        .name = "inventory_tab",
        .parent = module,
        .return_type = ecs_id(ecs_bool_t),
        .params = {{ .name = "tab", .type = ecs_id(ecs_i32_t) }},
        .callback = ore_inventory_tab_function
    });

    ecs_async_function(world, {
        .name = "inventory_pick",
        .parent = module,
        .return_type = ecs_id(ecs_bool_t),
        .params = {{ .name = "slot", .type = ecs_id(ecs_i32_t) }},
        .callback = ore_inventory_pick_function
    });

    ecs_async_function(world, {
        .name = "inventory_order",
        .parent = module,
        .return_type = ecs_id(ecs_bool_t),
        .params = {
            { .name = "item", .type = ecs_id(ecs_entity_t) },
            { .name = "batch", .type = ecs_id(ecs_bool_t) }
        },
        .callback = ore_inventory_order_function
    });

    ecs_async_function(world, {
        .name = "inventory_hover",
        .parent = module,
        .return_type = ecs_id(ecs_bool_t),
        .params = {
            { .name = "slot", .type = ecs_id(ecs_i32_t) },
            { .name = "item", .type = ecs_id(ecs_entity_t) },
            { .name = "over", .type = ecs_id(ecs_bool_t) }
        },
        .callback = ore_inventory_hover_function
    });
}

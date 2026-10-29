/**
 * @file inventory.c
 * @brief The inventory: the player's pack of OreInventory, any amount of an item kind and at
 * most cfg.inventorySlots kinds, and the components of the items in resources.flecs.
 *
 * A new kind takes the first free slot and keeps it until its count drops to 0, which frees
 * the slot. Scripts change it with oreelse.inventory_add() and oreelse.inventory_remove().
 */

#include "game.h"

/** Get the most kinds the inventory holds, cfg.inventorySlots up to ORE_INVENTORY_SLOTS.
 *
 * @param world The world or a stage
 * @return The kinds
 */
static int32_t ore_inventory_limit(
    const ecs_world_t *world)
{
    int32_t result = ORE_CFG_I32(world, "inventorySlots");

    return result < 0 ? 0 : (result > ORE_INVENTORY_SLOTS ? ORE_INVENTORY_SLOTS : result);
}

/** Find the slot of an item.
 *
 * @param inv The inventory
 * @param item The item
 * @return The index of its slot, -1 when the inventory does not hold it
 */
static int32_t ore_inventory_slot(
    const OreInventory *inv,
    ecs_entity_t item)
{
    int32_t i, result = -1;
    for (i = 0; item && i < ORE_INVENTORY_SLOTS && result < 0; i ++) {
        if (inv->slots[i].item == item) {
            result = i;
        }
    }

    return result;
}

int32_t ore_inventory_count(
    const ecs_world_t *world,
    ecs_entity_t item)
{
    const OreInventory *inv = ecs_singleton_get(world, OreInventory);
    int32_t slot = inv ? ore_inventory_slot(inv, item) : -1;

    return slot < 0 ? 0 : inv->slots[slot].amount;
}

int32_t ore_inventory_kinds(
    const ecs_world_t *world)
{
    const OreInventory *inv = ecs_singleton_get(world, OreInventory);
    int32_t i, result = 0;
    for (i = 0; inv && i < ORE_INVENTORY_SLOTS; i ++) {
        result += inv->slots[i].item != 0;
    }

    return result;
}

bool ore_inventory_fits(
    const ecs_world_t *world,
    const OreStack *stacks,
    int32_t count)
{
    int32_t i, j, kinds = ore_inventory_kinds(world);
    bool seen;
    for (i = 0; i < count; i ++) {
        if (stacks[i].amount <= 0 || !stacks[i].item ||
            ore_inventory_count(world, stacks[i].item) > 0)
        {
            continue;
        }

        seen = false;
        for (j = 0; j < i && !seen; j ++) {
            seen = stacks[j].item == stacks[i].item && stacks[j].amount > 0;
        }

        kinds += !seen;
    }

    return kinds <= ore_inventory_limit(world);
}

const char* ore_item_name(
    const ecs_world_t *world,
    ecs_entity_t item)
{
    const OreItem *it = item ? ecs_get(world, item, OreItem) : NULL;
    const char *name = it && it->name ? it->name : (item ? ecs_get_name(world, item) : NULL);

    return name ? name : "";
}

void ore_inventory_full(
    ecs_world_t *world)
{
    ore_toast(world, true, "Inventory full - %d item kinds is the limit",
        ore_inventory_limit(world));
}

bool ore_inventory_add(
    ecs_world_t *world,
    ecs_entity_t item,
    int32_t amount)
{
    OreInventory *inv = ecs_singleton_get_mut(world, OreInventory);
    int32_t slot = inv ? ore_inventory_slot(inv, item) : -1;
    int32_t i;
    bool result = inv && item;
    if (!result || amount <= 0) {
        goto done;
    }

    if (slot < 0 && ore_inventory_kinds(world) < ore_inventory_limit(world)) {
        for (i = 0; slot < 0 && i < ORE_INVENTORY_SLOTS; i ++) {
            if (!inv->slots[i].item) {
                slot = i;
            }
        }
    }

    if (slot < 0) {
        ore_inventory_full(world);
        result = false;
    } else if (inv->slots[slot].amount > INT32_MAX - amount) {
        result = false;
    } else {
        inv->slots[slot].item = item;
        inv->slots[slot].amount += amount;
        ecs_singleton_modified(world, OreInventory);
    }

done:
    return result;
}

bool ore_inventory_remove(
    ecs_world_t *world,
    ecs_entity_t item,
    int32_t amount)
{
    OreInventory *inv = ecs_singleton_get_mut(world, OreInventory);
    int32_t slot = inv ? ore_inventory_slot(inv, item) : -1;
    bool result = amount <= 0 || (slot >= 0 && inv->slots[slot].amount >= amount);
    if (result && amount > 0) {
        inv->slots[slot].amount -= amount;
        if (inv->slots[slot].amount <= 0) {
            inv->slots[slot] = (OreStack){ 0 };
        }

        ecs_singleton_modified(world, OreInventory);
    }

    return result;
}

/** Add an amount of an item to the inventory from a script, see ore_inventory_add.
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv The item, an entity, and the amount, an i32
 * @param result Receives whether the amount was added, a bool
 */
static void ore_inventory_add_function(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    *(bool*)result->ptr = ore_inventory_add(ctx->world,
        *(ecs_entity_t*)argv[0].ptr, *(int32_t*)argv[1].ptr);
}

/** Remove an amount of an item from the inventory from a script, see ore_inventory_remove.
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv The item, an entity, and the amount, an i32
 * @param result Receives whether the amount was removed, a bool
 */
static void ore_inventory_remove_function(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    *(bool*)result->ptr = ore_inventory_remove(ctx->world,
        *(ecs_entity_t*)argv[0].ptr, *(int32_t*)argv[1].ptr);
}

void ore_inventory_import(
    ecs_world_t *world)
{
    ecs_entity_t module = ecs_get_scope(world);

    ecs_add_pair(world, ecs_id(OreItem), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(OreIcon), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(OreMined), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(OreRecipe), EcsOnInstantiate, EcsInherit);
    ecs_singleton_ensure(world, OreInventory);

    ecs_function(world, {
        .name = "inventory_add",
        .parent = module,
        .return_type = ecs_id(ecs_bool_t),
        .params = {
            { .name = "item", .type = ecs_id(ecs_entity_t) },
            { .name = "amount", .type = ecs_id(ecs_i32_t) }
        },
        .callback = ore_inventory_add_function
    });

    ecs_function(world, {
        .name = "inventory_remove",
        .parent = module,
        .return_type = ecs_id(ecs_bool_t),
        .params = {
            { .name = "item", .type = ecs_id(ecs_entity_t) },
            { .name = "amount", .type = ecs_id(ecs_i32_t) }
        },
        .callback = ore_inventory_remove_function
    });
}

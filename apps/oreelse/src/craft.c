/**
 * @file craft.c
 * @brief The order book: orders paid when placed and planned down to the raw ore, each with a
 * pool of its own, the workers (OreCrafter) that craft their entries oldest order first, cancel
 * and refunds, OreCanOrder on the item prefabs with its flips (OreCanOrderFlips) and the raw ore
 * of OreNeeds.
 */

#include "game.h"

/** Workers whose crafts the order book shows at once */
#define ORE_CRAFT_WATCH (32)

/** Seconds of a craft whose recipe has no time */
#define ORE_CRAFT_TIME (0.5f)

/** The limits of the order book, read from cfg once per system run or call */
typedef struct ore_craft_limits_t {
    int32_t orders;  /**< Orders at most, cfg.craftOrders */
    int32_t entries; /**< Entries of an order at most, cfg.craftEntries */
    int32_t hold;    /**< Item kinds an order holds at most, cfg.craftHold */
    int32_t rows;    /**< Rows of the queue at most, cfg.craftRows */
    int32_t depth;   /**< Levels of recipes planned at most, cfg.craftDepth */
} ore_craft_limits_t;

/** The plan of the demand of an order */
typedef struct ore_plan_t {
    ecs_entity_t items[ORE_CRAFT_ENTRIES]; /**< Crafted items, inputs before their parent */
    int32_t crafts[ORE_CRAFT_ENTRIES];     /**< Crafts of every item */
    int32_t count;                         /**< Items planned */
    OreCraftBag avail;                     /**< What the plan draws on first: the pool, the
                                            *   outputs of running crafts and surplus yield */
    OreCraftBag draw;                      /**< What the plan borrows from the inventory */
    OreCraftBag incoming;                  /**< Outputs of the crafts running */
    OreCraftBag lack;                      /**< Raw items missing, in a tolerant plan */
    bool borrow;                           /**< The plan borrows from the inventory */
    bool tolerant;                         /**< Missing raw items go to lack instead of
                                            *   failing the plan */
} ore_plan_t;

/** A worker on a craft, as the order book shows it */
typedef struct ore_watch_t {
    ecs_entity_t item; /**< Item of the entry */
    int32_t order;     /**< Id of the order */
    float progress;    /**< Progress of the craft, 0 to 1 */
    bool live;         /**< Work is left, it is not waiting to deliver */
} ore_watch_t;

/** Clamp a limit of cfg to the size of its array.
 *
 * @param value The limit
 * @param max The size of the array
 * @return value within 0 and max
 */
static int32_t ore_craft_clamp(
    int32_t value,
    int32_t max)
{
    return value < 0 ? 0 : (value > max ? max : value);
}

/** Read the limits of the order book from cfg.
 *
 * @param world The world or a stage
 * @return The limits
 */
static ore_craft_limits_t ore_craft_limits(
    const ecs_world_t *world)
{
    ore_craft_limits_t result = {
        .orders = ore_craft_clamp(ORE_CFG_I32(world, "craftOrders"), ORE_CRAFT_ORDERS),
        .entries = ore_craft_clamp(ORE_CFG_I32(world, "craftEntries"), ORE_CRAFT_ENTRIES),
        .hold = ore_craft_clamp(ORE_CFG_I32(world, "craftHold"), ORE_CRAFT_BAG),
        .rows = ore_craft_clamp(ORE_CFG_I32(world, "craftRows"), ORE_CRAFT_ROWS),
        .depth = ORE_CFG_I32(world, "craftDepth")
    };

    return result;
}

/** Get the items a craft of a recipe yields.
 *
 * @param recipe The recipe
 * @return The yield, 1 when the recipe gives none
 */
static int32_t ore_recipe_yield(
    const OreRecipe *recipe)
{
    return recipe->yield > 0 ? recipe->yield : 1;
}

/** Check whether a recipe crafts something: it has inputs.
 *
 * @param recipe The recipe, NULL for none
 * @return Whether it has an input
 */
static bool ore_recipe_crafts(
    const OreRecipe *recipe)
{
    return recipe && recipe->inputs[0].item != 0;
}

/** Get the amount of an item in a bag.
 *
 * @param bag The bag
 * @param item The item
 * @return The amount, 0 when the bag does not hold it
 */
static int32_t ore_bag_get(
    const OreCraftBag *bag,
    ecs_entity_t item)
{
    int32_t i, result = 0;
    for (i = 0; i < bag->count; i ++) {
        if (bag->stacks[i].item == item) {
            result = bag->stacks[i].amount;
        }
    }

    return result;
}

/** Add an amount of an item to a bag, or take it off for a negative amount. A kind that drops
 * to 0 leaves the bag, the last kind takes its place.
 *
 * @param bag The bag
 * @param item The item
 * @param amount How many, negative to take off
 * @return Whether it was added: false when a new kind does not fit the bag
 */
static bool ore_bag_add(
    OreCraftBag *bag,
    ecs_entity_t item,
    int32_t amount)
{
    int32_t i, slot = -1;
    bool result = true;
    for (i = 0; i < bag->count && slot < 0; i ++) {
        if (bag->stacks[i].item == item) {
            slot = i;
        }
    }

    if (!amount || (slot < 0 && amount < 0)) {
        result = true;
    } else if (slot >= 0) {
        bag->stacks[slot].amount += amount;
        if (bag->stacks[slot].amount <= 0) {
            bag->count --;
            bag->stacks[slot] = bag->stacks[bag->count];
            bag->stacks[bag->count] = (OreStack){ 0 };
        }
    } else if (bag->count >= ORE_CRAFT_BAG) {
        result = false;
    } else {
        bag->stacks[bag->count] = (OreStack){ .item = item, .amount = amount };
        bag->count ++;
    }

    return result;
}

/** Take up to an amount of an item off a bag.
 *
 * @param bag The bag
 * @param item The item
 * @param need How many are needed
 * @return How many are still needed after taking what the bag holds
 */
static int32_t ore_bag_draw(
    OreCraftBag *bag,
    ecs_entity_t item,
    int32_t need)
{
    int32_t have = ore_bag_get(bag, item);
    int32_t take = have < need ? have : need;
    ore_bag_add(bag, item, -take);

    return need - take;
}

/** Check whether a bag holds the inputs of one craft of a recipe.
 *
 * @param bag The bag
 * @param recipe The recipe
 * @return Whether every input is there
 */
static bool ore_bag_affords(
    const OreCraftBag *bag,
    const OreRecipe *recipe)
{
    int32_t i;
    bool result = true;
    for (i = 0; i < ORE_RECIPE_INPUTS && recipe->inputs[i].item; i ++) {
        if (recipe->inputs[i].amount > 0 &&
            ore_bag_get(bag, recipe->inputs[i].item) < recipe->inputs[i].amount)
        {
            result = false;
        }
    }

    return result;
}

/** Add the inputs of crafts of a recipe to a bag, or take them off.
 *
 * @param bag The bag
 * @param recipe The recipe
 * @param times Crafts, negative to take the inputs off
 */
static void ore_bag_recipe(
    OreCraftBag *bag,
    const OreRecipe *recipe,
    int32_t times)
{
    int32_t i;
    for (i = 0; i < ORE_RECIPE_INPUTS && recipe->inputs[i].item; i ++) {
        if (recipe->inputs[i].amount > 0) {
            ore_bag_add(bag, recipe->inputs[i].item, recipe->inputs[i].amount * times);
        }
    }
}

/** Find an order by its id.
 *
 * @param book The order book
 * @param id The id
 * @return The index of the order, -1 when there is none
 */
static int32_t ore_order_index(
    const OreCraftBook *book,
    int32_t id)
{
    int32_t i, result = -1;
    for (i = 0; id && i < book->count && result < 0; i ++) {
        if (book->orders[i].id == id) {
            result = i;
        }
    }

    return result;
}

/** Find the entry of an item in an order.
 *
 * @param order The order
 * @param item The item
 * @return The index of the entry, -1 when there is none
 */
static int32_t ore_order_entry(
    const OreCraftOrder *order,
    ecs_entity_t item)
{
    int32_t i, result = -1;
    for (i = 0; i < order->entry_count && result < 0; i ++) {
        if (order->entries[i].item == item) {
            result = i;
        }
    }

    return result;
}

/** Collect the crafts running for an order per item.
 *
 * @param order The order
 * @param bag Receives the running crafts per item
 */
static void ore_order_running(
    const OreCraftOrder *order,
    OreCraftBag *bag)
{
    int32_t i;
    ecs_os_memset_t(bag, 0, OreCraftBag);
    for (i = 0; i < order->entry_count; i ++) {
        if (order->entries[i].running > 0) {
            ore_bag_add(bag, order->entries[i].item, order->entries[i].running);
        }
    }
}

/** Get the queue rows an order counts.
 *
 * @param order The order
 * @return max(1, entries)
 */
static int32_t ore_order_rows(
    const OreCraftOrder *order)
{
    return order->entry_count > 0 ? order->entry_count : 1;
}

/** Get the queue rows of every order.
 *
 * @param book The order book
 * @return The sum of the rows the orders count
 */
static int32_t ore_book_rows(
    const OreCraftBook *book)
{
    int32_t i, result = 0;
    for (i = 0; i < book->count; i ++) {
        result += ore_order_rows(&book->orders[i]);
    }

    return result;
}

/** Add crafts of an item to a plan.
 *
 * @param plan The plan
 * @param item The item
 * @param count Crafts
 * @return Whether they were added: false when the plan holds as many items as it can
 */
static bool ore_plan_push(
    ore_plan_t *plan,
    ecs_entity_t item,
    int32_t count)
{
    int32_t i, slot = -1;
    bool result = true;
    for (i = 0; i < plan->count && slot < 0; i ++) {
        if (plan->items[i] == item) {
            slot = i;
        }
    }

    if (slot >= 0) {
        plan->crafts[slot] += count;
    } else if (plan->count >= ORE_CRAFT_ENTRIES) {
        result = false;
    } else {
        plan->items[plan->count] = item;
        plan->crafts[plan->count] = count;
        plan->count ++;
    }

    return result;
}

static bool ore_plan_provide(
    const ecs_world_t *world,
    ore_plan_t *plan,
    ecs_entity_t item,
    int32_t need,
    int32_t depth);

/** Plan the inputs of crafts of a recipe.
 *
 * @param world The world or a stage
 * @param plan The plan
 * @param recipe The recipe
 * @param times Crafts
 * @param depth Levels of recipes still allowed below
 * @return Whether every input was planned
 */
static bool ore_plan_inputs(
    const ecs_world_t *world,
    ore_plan_t *plan,
    const OreRecipe *recipe,
    int32_t times,
    int32_t depth)
{
    int32_t i;
    bool result = true;
    for (i = 0; i < ORE_RECIPE_INPUTS && recipe->inputs[i].item && result; i ++) {
        if (recipe->inputs[i].amount > 0) {
            result = ore_plan_provide(world, plan, recipe->inputs[i].item,
                recipe->inputs[i].amount * times, depth);
        }
    }

    return result;
}

/** Plan the crafts that make an amount of an item: ceil(need / yield) crafts, their inputs
 * first, the surplus yield to the plan's avail.
 *
 * @param world The world or a stage
 * @param plan The plan
 * @param item The item
 * @param recipe Its recipe
 * @param need How many are needed
 * @param depth Levels of recipes still allowed below
 * @return Whether the crafts were planned
 */
static bool ore_plan_craft(
    const ecs_world_t *world,
    ore_plan_t *plan,
    ecs_entity_t item,
    const OreRecipe *recipe,
    int32_t need,
    int32_t depth)
{
    int32_t yield = ore_recipe_yield(recipe);
    int32_t crafts = (need + yield - 1) / yield;

    return ore_plan_inputs(world, plan, recipe, crafts, depth) &&
        ore_bag_add(&plan->avail, item, crafts * yield - need) &&
        ore_plan_push(plan, item, crafts);
}

/** Plan an amount of an item: from avail first, then borrowed from the inventory when the plan
 * borrows, then crafted. A raw item that is still missing fails the plan, or goes to lack.
 *
 * @param world The world or a stage
 * @param plan The plan
 * @param item The item
 * @param need How many are needed
 * @param depth Levels of recipes still allowed
 * @return Whether the amount was planned
 */
static bool ore_plan_provide(
    const ecs_world_t *world,
    ore_plan_t *plan,
    ecs_entity_t item,
    int32_t need,
    int32_t depth)
{
    const OreRecipe *recipe = NULL;
    int32_t stock, take;
    bool result = true;
    need = ore_bag_draw(&plan->avail, item, need);
    if (need > 0 && plan->borrow) {
        stock = ore_inventory_count(world, item) - ore_bag_get(&plan->draw, item);
        take = stock < need ? stock : need;
        if (take > 0) {
            result = ore_bag_add(&plan->draw, item, take);
            need -= take;
        }
    }

    if (result && need > 0) {
        recipe = ecs_get(world, item, OreRecipe);
        if (depth > 0 && ore_recipe_crafts(recipe)) {
            result = ore_plan_craft(world, plan, item, recipe, need, depth - 1);
        } else {
            result = plan->tolerant && ore_bag_add(&plan->lack, item, need);
        }
    }

    return result;
}

/** Plan the demand of an order: the outputs of its running crafts and its pool are drawn on
 * first, the rest is borrowed or crafted.
 *
 * @param world The world or a stage
 * @param head The ordered item
 * @param pool The pool of the order
 * @param running The crafts running per item
 * @param demand Units to plan
 * @param borrow Whether the plan borrows from the inventory
 * @param tolerant Whether missing raw items go to lack instead of failing
 * @param depth Levels of recipes allowed
 * @param plan Receives the plan
 * @return Whether the demand was planned
 */
static bool ore_plan_order(
    const ecs_world_t *world,
    ecs_entity_t head,
    const OreCraftBag *pool,
    const OreCraftBag *running,
    int32_t demand,
    bool borrow,
    bool tolerant,
    int32_t depth,
    ore_plan_t *plan)
{
    const OreRecipe *recipe = NULL;
    int32_t i, need, made;
    bool result = true;
    ecs_os_memset_t(plan, 0, ore_plan_t);
    plan->avail = *pool;
    plan->borrow = borrow;
    plan->tolerant = tolerant;
    for (i = 0; i < running->count && result; i ++) {
        recipe = ecs_get(world, running->stacks[i].item, OreRecipe);
        if (running->stacks[i].amount <= 0) {
            continue;
        }

        made = recipe ? running->stacks[i].amount * ore_recipe_yield(recipe) : 0;
        result = recipe &&
            ore_bag_add(&plan->incoming, running->stacks[i].item, made) &&
            ore_bag_add(&plan->avail, running->stacks[i].item, made);
    }

    if (result && demand > 0) {
        recipe = ecs_get(world, head, OreRecipe);
        need = ore_recipe_crafts(recipe) ? ore_bag_draw(&plan->avail, head, demand) : 0;
        result = ore_recipe_crafts(recipe) &&
            (need <= 0 || ore_plan_craft(world, plan, head, recipe, need, depth));
    }

    return result;
}

/** Count the entries an order would hold with a plan: the planned items and the items of
 * running crafts the plan no longer needs.
 *
 * @param plan The plan
 * @param running The crafts running per item
 * @return The entries
 */
static int32_t ore_plan_entries(
    const ore_plan_t *plan,
    const OreCraftBag *running)
{
    int32_t i, j, result = plan->count;
    bool found;
    for (i = 0; i < running->count; i ++) {
        found = running->stacks[i].amount <= 0;
        for (j = 0; j < plan->count && !found; j ++) {
            found = plan->items[j] == running->stacks[i].item;
        }

        result += !found;
    }

    return result;
}

/** Check that an order with a plan holds few enough item kinds: its pool, what it borrows and
 * the items it plans.
 *
 * @param plan The plan
 * @param pool The pool of the order
 * @param hold Kinds at most
 * @return Whether the kinds are within hold
 */
static bool ore_plan_holds(
    const ore_plan_t *plan,
    const OreCraftBag *pool,
    int32_t hold)
{
    OreCraftBag kinds;
    int32_t i;
    bool result = true;
    ecs_os_memset_t(&kinds, 0, OreCraftBag);
    for (i = 0; i < pool->count && result; i ++) {
        result = pool->stacks[i].amount <= 0 || ore_bag_add(&kinds, pool->stacks[i].item, 1);
    }

    for (i = 0; i < plan->draw.count && result; i ++) {
        result = plan->draw.stacks[i].amount <= 0 ||
            ore_bag_add(&kinds, plan->draw.stacks[i].item, 1);
    }

    for (i = 0; i < plan->count && result; i ++) {
        result = ore_bag_add(&kinds, plan->items[i], 1);
    }

    return result && kinds.count <= hold;
}

/** Set the entries of an order to a plan, keeping the entries of the crafts still running.
 * An entry that stays keeps its marks.
 *
 * @param order The order
 * @param plan The plan
 * @param running The crafts running per item
 */
static void ore_order_apply(
    OreCraftOrder *order,
    const ore_plan_t *plan,
    const OreCraftBag *running)
{
    OreCraftEntry next[ORE_CRAFT_ENTRIES];
    int32_t i, j, n = 0, old;
    bool found;
    ecs_os_memset(next, 0, ECS_SIZEOF(next));
    for (i = 0; i < plan->count && n < ORE_CRAFT_ENTRIES; i ++) {
        next[n].item = plan->items[i];
        next[n].running = ore_bag_get(running, plan->items[i]);
        next[n].count = plan->crafts[i] + next[n].running;
        n ++;
    }

    for (i = 0; i < running->count && n < ORE_CRAFT_ENTRIES; i ++) {
        found = running->stacks[i].amount <= 0;
        for (j = 0; j < n && !found; j ++) {
            found = next[j].item == running->stacks[i].item;
        }

        if (!found) {
            next[n].item = running->stacks[i].item;
            next[n].running = running->stacks[i].amount;
            next[n].count = running->stacks[i].amount;
            n ++;
        }
    }

    for (i = 0; i < n; i ++) {
        old = ore_order_entry(order, next[i].item);
        if (old >= 0) {
            next[i].active = order->entries[old].active;
            next[i].progress = order->entries[old].progress;
            next[i].full = order->entries[old].full;
            next[i].stalled = order->entries[old].stalled;
        }
    }

    ecs_os_memset(order->entries, 0, ECS_SIZEOF(order->entries));
    ecs_os_memcpy(order->entries, next, ECS_SIZEOF(OreCraftEntry) * n);
    order->entry_count = n;
}

/** Remove an order, the younger ones move up.
 *
 * @param book The order book
 * @param index The index of the order
 */
static void ore_order_drop(
    OreCraftBook *book,
    int32_t index)
{
    int32_t i;
    for (i = index + 1; i < book->count; i ++) {
        book->orders[i - 1] = book->orders[i];
    }

    book->count --;
    ecs_os_memset_t(&book->orders[book->count], 0, OreCraftOrder);
}

/** Remove an entry of an order, the later ones move up.
 *
 * @param order The order
 * @param index The index of the entry
 */
static void ore_order_drop_entry(
    OreCraftOrder *order,
    int32_t index)
{
    int32_t i;
    for (i = index + 1; i < order->entry_count; i ++) {
        order->entries[i - 1] = order->entries[i];
    }

    order->entry_count --;
    ecs_os_memset_t(&order->entries[order->entry_count], 0, OreCraftEntry);
}

/** Make a worker idle; it keeps its speed and whether it is paused.
 *
 * @param crafter The worker
 */
static void ore_crafter_stop(
    OreCrafter *crafter)
{
    crafter->item = 0;
    crafter->order = 0;
    crafter->left = 0;
    crafter->total = 0;
}

/** Check whether a worker has a craft, in progress or waiting to deliver.
 *
 * @param crafter The worker
 * @return Whether it has a craft
 */
static bool ore_crafter_busy(
    const OreCrafter *crafter)
{
    return crafter->item != 0 && crafter->total > 0;
}

/** Plan one more unit of an item: of a new order, or merged into the youngest order when it
 * orders the same item, and check the limits.
 *
 * @param world The world or a stage
 * @param book The order book
 * @param limits The limits
 * @param item The item
 * @param merge Whether the unit merges into the youngest order of the item
 * @param tolerant Whether missing raw items go to lack instead of failing, skipping the limits
 * @param plan Receives the plan
 * @param demand Receives the demand of the order with the unit
 * @return Whether the unit can be ordered
 */
static bool ore_craft_context(
    const ecs_world_t *world,
    const OreCraftBook *book,
    const ore_craft_limits_t *limits,
    ecs_entity_t item,
    bool merge,
    bool tolerant,
    ore_plan_t *plan,
    int32_t *demand)
{
    const OreCraftOrder *order = NULL;
    OreCraftBag pool, running;
    int32_t entries, rows;
    bool result;
    ecs_os_memset_t(&pool, 0, OreCraftBag);
    ecs_os_memset_t(&running, 0, OreCraftBag);
    *demand = 1;
    if (merge && book->count > 0 && book->orders[book->count - 1].item == item) {
        order = &book->orders[book->count - 1];
        pool = order->pool;
        ore_order_running(order, &running);
        *demand = order->demand + 1;
    }

    result = ore_plan_order(world, item, &pool, &running, *demand, true, tolerant,
        limits->depth, plan);
    if (result && !tolerant) {
        entries = ore_plan_entries(plan, &running);
        rows = ore_book_rows(book) + entries - (order ? ore_order_rows(order) : 0);
        result = entries <= limits->entries && ore_plan_holds(plan, &pool, limits->hold) &&
            (order || book->count < limits->orders) && rows <= limits->rows;
    }

    return result;
}

/** Place one unit of an order: plan it, move what it borrows from the inventory to the pool
 * of the order and set the entries.
 *
 * @param world The world
 * @param book The order book
 * @param limits The limits
 * @param item The item
 * @param merge Whether the unit merges into the youngest order of the item
 * @return Whether the unit was ordered
 */
static bool ore_craft_enqueue(
    ecs_world_t *world,
    OreCraftBook *book,
    const ore_craft_limits_t *limits,
    ecs_entity_t item,
    bool merge)
{
    OreCraftOrder *order = NULL;
    OreCraftBag running;
    ore_plan_t plan;
    int32_t i, demand = 0;
    bool result = ore_craft_context(world, book, limits, item, merge, false, &plan, &demand);
    if (!result) {
        goto done;
    }

    if (merge && book->count > 0 && book->orders[book->count - 1].item == item) {
        order = &book->orders[book->count - 1];
    } else if (book->count < ORE_CRAFT_ORDERS) {
        order = &book->orders[book->count];
        ecs_os_memset_t(order, 0, OreCraftOrder);
        book->next_id ++;
        order->id = book->next_id;
        order->item = item;
        book->count ++;
    } else {
        result = false;
        goto done;
    }

    for (i = 0; i < plan.draw.count; i ++) {
        if (plan.draw.stacks[i].amount > 0) {
            ore_inventory_remove(world, plan.draw.stacks[i].item, plan.draw.stacks[i].amount);
            ore_bag_add(&order->pool, plan.draw.stacks[i].item, plan.draw.stacks[i].amount);
        }
    }

    ore_order_running(order, &running);
    ore_order_apply(order, &plan, &running);
    order->demand = demand;

done:
    return result;
}

/** Collect the workers that have a craft.
 *
 * @param world The world or a stage
 * @param watch Receives the workers, at most ORE_CRAFT_WATCH
 * @return The workers collected
 */
static int32_t ore_craft_watch(
    const ecs_world_t *world,
    ore_watch_t *watch)
{
    ecs_iter_t it = ecs_each_id(world, ecs_id(OreCrafter));
    const OreCrafter *crafter;
    int32_t i, result = 0;
    while (ecs_each_next(&it)) {
        crafter = ecs_field(&it, OreCrafter, 0);
        for (i = 0; i < it.count && result < ORE_CRAFT_WATCH; i ++) {
            if (ore_crafter_busy(&crafter[i])) {
                watch[result] = (ore_watch_t){
                    .item = crafter[i].item,
                    .order = crafter[i].order,
                    .progress = 1.0f - crafter[i].left / crafter[i].total,
                    .live = crafter[i].left > 0
                };
                result ++;
            }
        }
    }

    return result;
}

/** Check whether a worker can start an entry of an order: it has crafts left, the pool holds
 * the inputs of one craft and the ordered item fits the inventory.
 *
 * @param world The world or a stage
 * @param order The order
 * @param entry The entry
 * @param blocked Receives whether only a full inventory keeps it from starting, may be NULL
 * @return Whether it can start
 */
static bool ore_craft_startable(
    const ecs_world_t *world,
    const OreCraftOrder *order,
    const OreCraftEntry *entry,
    bool *blocked)
{
    const OreRecipe *recipe = ecs_get(world, entry->item, OreRecipe);
    OreStack out = { .item = entry->item, .amount = 1 };
    bool result = entry->count - entry->running > 0 && recipe &&
        ore_bag_affords(&order->pool, recipe);
    bool full = result && entry->item == order->item && !ore_inventory_fits(world, &out, 1);
    if (blocked) {
        *blocked = full;
    }

    return result && !full;
}

/** Start the first startable entry of the oldest order on an idle worker: the inputs of the
 * craft leave the pool.
 *
 * @param world The world or a stage
 * @param book The order book
 * @param crafter The worker
 * @return Whether a craft started
 */
static bool ore_craft_begin(
    const ecs_world_t *world,
    OreCraftBook *book,
    OreCrafter *crafter)
{
    const OreRecipe *recipe;
    OreCraftOrder *order;
    OreCraftEntry *entry;
    int32_t oi, ei;
    bool result = false;
    for (oi = 0; oi < book->count && !result; oi ++) {
        order = &book->orders[oi];
        for (ei = 0; ei < order->entry_count && !result; ei ++) {
            entry = &order->entries[ei];
            if (!ore_craft_startable(world, order, entry, NULL)) {
                continue;
            }

            recipe = ecs_get(world, entry->item, OreRecipe);
            ore_bag_recipe(&order->pool, recipe, -1);
            entry->running ++;
            crafter->item = entry->item;
            crafter->order = order->id;
            crafter->total = recipe->time > 0 ? recipe->time : ORE_CRAFT_TIME;
            crafter->left = crafter->total;
            result = true;
        }
    }

    return result;
}

/** Work on the craft of a worker. When it completes, an intermediate goes to the pool of the
 * order and the ordered item to the inventory; one that does not fit waits with the worker.
 *
 * @param world The world or the stage of a system
 * @param book The order book
 * @param crafter The worker
 * @param advance Work done this frame
 * @return Whether the order book changed
 */
static bool ore_craft_work(
    ecs_world_t *world,
    OreCraftBook *book,
    OreCrafter *crafter,
    float advance)
{
    int32_t index = ore_order_index(book, crafter->order);
    OreCraftOrder *order = index >= 0 ? &book->orders[index] : NULL;
    int32_t e = order ? ore_order_entry(order, crafter->item) : -1;
    OreCraftEntry *entry = e >= 0 ? &order->entries[e] : NULL;
    const OreRecipe *recipe = entry ? ecs_get(world, entry->item, OreRecipe) : NULL;
    OreStack out = { .item = crafter->item, .amount = 1 };
    int32_t i;
    bool head, result = true;
    if (!entry || entry->running <= 0 || !recipe) {
        ore_crafter_stop(crafter);
        goto done;
    }

    if (crafter->left > 0) {
        crafter->left -= advance;
        crafter->left = crafter->left > 0 ? crafter->left : 0;
    }

    head = entry->item == order->item;
    out.amount = ore_recipe_yield(recipe);
    if (crafter->left > 0) {
        result = false;
    } else if (head) {
        result = ore_inventory_fits(world, &out, 1) &&
            ore_inventory_add(world, out.item, out.amount);
    } else {
        result = ore_bag_add(&order->pool, out.item, out.amount);
    }

    if (!result) {
        goto done;
    }

    entry->count --;
    entry->running --;
    if (head && order->demand > 0) {
        order->demand --;
    }

    ore_crafter_stop(crafter);
    if (entry->count <= 0) {
        ore_order_drop_entry(order, e);
    }

    for (i = order->entry_count - 1; order->demand <= 0 && i >= 0; i --) {
        if (!order->entries[i].running) {
            ore_order_drop_entry(order, i);
        }
    }

done:
    return result;
}

/** Move the leftover pool of an order into the inventory, each kind only if it fits.
 *
 * @param world The world or the stage of a system
 * @param order The order
 */
static void ore_order_flush(
    ecs_world_t *world,
    OreCraftOrder *order)
{
    OreStack stack;
    int32_t i;
    bool blocked = false;
    for (i = order->pool.count - 1; i >= 0; i --) {
        stack = order->pool.stacks[i];
        if (ore_inventory_fits(world, &stack, 1) &&
            ore_inventory_add(world, stack.item, stack.amount))
        {
            ore_bag_add(&order->pool, stack.item, -stack.amount);
        } else {
            blocked = true;
        }
    }

    order->full = blocked;
}

/** Wrap up the orders without entries: their leftover pool goes into the inventory, and an
 * order whose pool is empty is removed.
 *
 * @param world The world or the stage of a system
 * @param book The order book
 */
static void ore_craft_settle(
    ecs_world_t *world,
    OreCraftBook *book)
{
    int32_t i;
    for (i = book->count - 1; i >= 0; i --) {
        if (book->orders[i].entry_count) {
            continue;
        }

        ore_order_flush(world, &book->orders[i]);
        if (!book->orders[i].pool.count) {
            ore_order_drop(book, i);
        }
    }
}

/** Mark the entries and orders: which workers are on them and their progress, what a full
 * inventory blocks and what is stalled.
 *
 * @param world The world or a stage
 * @param book The order book
 * @param watch The workers with a craft
 * @param watched Number of workers
 */
static void ore_craft_marks(
    const ecs_world_t *world,
    OreCraftBook *book,
    const ore_watch_t *watch,
    int32_t watched)
{
    OreCraftOrder *order;
    OreCraftEntry *entry;
    int32_t i, j, w, head;
    bool live, work, start, full, blocked;
    book->stalled = false;
    for (i = 0; i < book->count; i ++) {
        order = &book->orders[i];
        live = work = start = false;
        full = order->full;
        for (w = 0; w < watched; w ++) {
            live = live || (watch[w].order == order->id && watch[w].live);
        }

        for (j = 0; j < order->entry_count; j ++) {
            entry = &order->entries[j];
            work = work || entry->count - entry->running > 0;
            start = ore_craft_startable(world, order, entry, &blocked) || start;
            entry->active = false;
            entry->progress = 0;
            for (w = 0; w < watched; w ++) {
                if (watch[w].order != order->id || watch[w].item != entry->item) {
                    continue;
                }

                blocked = blocked || !watch[w].live;
                entry->active = true;
                entry->progress = watch[w].progress > entry->progress ?
                    watch[w].progress : entry->progress;
            }

            entry->full = blocked;
            full = full || blocked;
        }

        order->stalled = !live && !start && (work || full);
        for (j = 0; j < order->entry_count; j ++) {
            entry = &order->entries[j];
            entry->stalled = order->stalled &&
                (entry->count - entry->running > 0 || entry->full);
        }

        head = ore_order_entry(order, order->item);
        order->count = head >= 0 ? order->entries[head].count : 0;
        order->wrapping = order->entry_count == 0;
        book->stalled = book->stalled || order->stalled;
    }
}

/** List the rows of the queue panel: a header per order, then a row per entry other than the
 * ordered item.
 *
 * @param book The order book
 */
static void ore_craft_rows(
    OreCraftBook *book)
{
    const OreCraftOrder *order;
    const OreCraftEntry *entry;
    OreCraftRow *row;
    int32_t i, j, head, n = 0;
    ecs_os_memset(book->rows, 0, ECS_SIZEOF(book->rows));
    for (i = 0; i < book->count && n < ORE_CRAFT_ROWS; i ++) {
        order = &book->orders[i];
        head = ore_order_entry(order, order->item);
        row = &book->rows[n ++];
        row->order = order->id;
        row->item = order->item;
        row->count = order->count;
        row->stalled = order->stalled;
        row->wrapping = order->wrapping;
        row->cancel = order->demand > 0;
        row->full = head >= 0 ? order->entries[head].full : order->full;
        row->active = head >= 0 && order->entries[head].active;
        row->progress = head >= 0 ? order->entries[head].progress : 0;
        for (j = 0; j < order->entry_count && n < ORE_CRAFT_ROWS; j ++) {
            entry = &order->entries[j];
            if (entry->item == order->item) {
                continue;
            }

            row = &book->rows[n ++];
            row->order = order->id;
            row->item = entry->item;
            row->sub = true;
            row->count = entry->count;
            row->active = entry->active;
            row->progress = entry->progress;
            row->full = entry->full;
            row->stalled = entry->stalled;
        }
    }

    book->row_count = n;
}

/** Update what the order book shows: the marks of its orders and entries and the rows.
 *
 * @param world The world or a stage
 * @param book The order book
 */
static void ore_craft_refresh(
    const ecs_world_t *world,
    OreCraftBook *book)
{
    ore_watch_t watch[ORE_CRAFT_WATCH];
    int32_t watched = ore_craft_watch(world, watch);
    ore_craft_marks(world, book, watch, watched);
    ore_craft_rows(book);
}

int32_t ore_craft_order(
    ecs_world_t *world,
    ecs_entity_t item,
    int32_t count)
{
    OreCraftBook *book = ecs_singleton_get_mut(world, OreCraftBook);
    ore_craft_limits_t limits = ore_craft_limits(world);
    int32_t result = 0;
    while (book && item && result < count &&
        ore_craft_enqueue(world, book, &limits, item, result > 0))
    {
        result ++;
    }

    if (result) {
        ore_craft_refresh(world, book);
        ecs_singleton_modified(world, OreCraftBook);
    }

    return result;
}

/** Stop the workers on an order; their progress is lost.
 *
 * @param world The world
 * @param id Id of the order
 */
static void ore_craft_stop_order(
    ecs_world_t *world,
    int32_t id)
{
    ecs_iter_t it = ecs_each_id(world, ecs_id(OreCrafter));
    ecs_entity_t stopped[ORE_CRAFT_WATCH];
    OreCrafter *crafter;
    int32_t i, count = 0;
    while (ecs_each_next(&it)) {
        crafter = ecs_field(&it, OreCrafter, 0);
        for (i = 0; i < it.count; i ++) {
            if (crafter[i].order == id) {
                ore_crafter_stop(&crafter[i]);
                stopped[count] = it.entities[i];
                count += count < ORE_CRAFT_WATCH - 1;
            }
        }
    }

    for (i = 0; i < count; i ++) {
        ecs_modified(world, stopped[i], OreCrafter);
    }
}

/** Compute the refund of a cancel: everything in the pool of a plan that the remaining demand
 * does not need, not counting the outputs of crafts still running.
 *
 * @param plan The plan of the remaining demand
 * @param refund Receives the refund
 */
static void ore_craft_refund(
    const ore_plan_t *plan,
    OreCraftBag *refund)
{
    int32_t i, left;
    ecs_os_memset_t(refund, 0, OreCraftBag);
    for (i = 0; i < plan->avail.count; i ++) {
        left = plan->avail.stacks[i].amount -
            ore_bag_get(&plan->incoming, plan->avail.stacks[i].item);
        if (left > 0) {
            ore_bag_add(refund, plan->avail.stacks[i].item, left);
        }
    }
}

bool ore_craft_cancel(
    ecs_world_t *world,
    int32_t id)
{
    OreCraftBook *book = ecs_singleton_get_mut(world, OreCraftBook);
    int32_t i, index = book ? ore_order_index(book, id) : -1;
    OreCraftOrder *order = index >= 0 ? &book->orders[index] : NULL;
    const OreRecipe *recipe;
    OreCraftBag pool, running, refund;
    ore_plan_t plan;
    int32_t demand = order ? order->demand - 1 : -1;
    bool result = demand >= 0;
    if (!result) {
        goto done;
    }

    pool = order->pool;
    ore_order_running(order, &running);
    for (i = 0; !demand && i < running.count; i ++) {
        recipe = ecs_get(world, running.stacks[i].item, OreRecipe);
        if (recipe && running.stacks[i].amount > 0) {
            ore_bag_recipe(&pool, recipe, running.stacks[i].amount);
        }
    }

    if (!demand) {
        ecs_os_memset_t(&running, 0, OreCraftBag);
    }

    result = ore_plan_order(world, order->item, &pool, &running, demand, false, false,
        ORE_CFG_I32(world, "craftDepth"), &plan);
    if (!result) {
        goto done;
    }

    ore_craft_refund(&plan, &refund);
    if (!ore_inventory_fits(world, refund.stacks, refund.count)) {
        ore_toast(world, true, "No room for the refund");
        result = false;
        goto done;
    }

    if (!demand) {
        ore_craft_stop_order(world, id);
    }

    for (i = 0; i < refund.count; i ++) {
        ore_inventory_add(world, refund.stacks[i].item, refund.stacks[i].amount);
        ore_bag_add(&pool, refund.stacks[i].item, -refund.stacks[i].amount);
    }

    order->pool = pool;
    ore_order_apply(order, &plan, &running);
    order->demand = demand;
    if (!order->entry_count && !order->pool.count) {
        ore_order_drop(book, index);
    }

    ore_craft_refresh(world, book);
    ecs_singleton_modified(world, OreCraftBook);

done:
    return result;
}

/** Get the raw ore of one unit of an item: per ore the larger of its full raw cost with
 * nothing in stock and what is missing given the inventory, and what of it is there.
 *
 * @param world The world or a stage
 * @param book The order book
 * @param limits The limits
 * @param item The item
 * @param ores Receives the ores with a need above 0 in the order of OreMined.rank
 * @return The ores listed, at most ORE_RAW_ORES
 */
static int32_t ore_craft_needs(
    const ecs_world_t *world,
    const OreCraftBook *book,
    const ore_craft_limits_t *limits,
    ecs_entity_t item,
    OreNeed *ores)
{
    OreCraftBag empty, need;
    ore_plan_t total, marginal;
    const OreMined *mined;
    OreNeed next, list[ORE_CRAFT_BAG];
    int32_t i, j, n = 0, demand, missing, result = 0;
    int32_t rank[ORE_CRAFT_BAG];
    ecs_os_memset_t(&empty, 0, OreCraftBag);
    ecs_os_memset_t(&need, 0, OreCraftBag);
    if (!item || !ore_recipe_crafts(ecs_get(world, item, OreRecipe)) ||
        !ore_plan_order(world, item, &empty, &empty, 1, false, true, limits->depth, &total) ||
        !ore_craft_context(world, book, limits, item, false, true, &marginal, &demand))
    {
        goto done;
    }

    need = total.lack;
    for (i = 0; i < marginal.lack.count; i ++) {
        next.item = marginal.lack.stacks[i].item;
        missing = marginal.lack.stacks[i].amount - ore_bag_get(&need, next.item);
        if (missing > 0) {
            ore_bag_add(&need, next.item, missing);
        }
    }

    for (i = 0; i < need.count; i ++) {
        missing = ore_bag_get(&marginal.lack, need.stacks[i].item);
        missing = missing < need.stacks[i].amount ? missing : need.stacks[i].amount;
        mined = ecs_get(world, need.stacks[i].item, OreMined);
        next = (OreNeed){
            .item = need.stacks[i].item,
            .need = need.stacks[i].amount,
            .have = need.stacks[i].amount - missing
        };
        for (j = n; j > 0 && rank[j - 1] > (mined ? mined->rank : INT32_MAX); j --) {
            list[j] = list[j - 1];
            rank[j] = rank[j - 1];
        }

        list[j] = next;
        rank[j] = mined ? mined->rank : INT32_MAX;
        n ++;
    }

    for (i = 0; i < n && result < ORE_RAW_ORES; i ++) {
        if (list[i].need > 0) {
            ores[result ++] = list[i];
        }
    }

done:
    return result;
}

/** Return the inputs of the craft of a worker that is removed (a factory demolished or
 * destroyed) to the pool of its order; another worker can pick the entry up.
 *
 * The OreCraftBook term makes the world delete the observer before the singleton when it is
 * deleted itself, so the observer never sees a book that is gone.
 *
 * @param it Iterator over the workers that lose OreCrafter, with OreCraftBook
 */
static void OreCrafterRemove(
    ecs_iter_t *it)
{
    const OreCrafter *crafter = ecs_field(it, OreCrafter, 0);
    OreCraftBook *book = ecs_field(it, OreCraftBook, 1);
    const OreRecipe *recipe;
    OreCraftOrder *order;
    int32_t i, index, entry;
    bool changed = false;
    for (i = 0; i < it->count; i ++) {
        index = ore_crafter_busy(&crafter[i]) ? ore_order_index(book, crafter[i].order) : -1;
        if (index < 0) {
            continue;
        }

        order = &book->orders[index];
        recipe = ecs_get(it->world, crafter[i].item, OreRecipe);
        if (recipe) {
            ore_bag_recipe(&order->pool, recipe, 1);
        }

        entry = ore_order_entry(order, crafter[i].item);
        if (entry >= 0 && order->entries[entry].running > 0) {
            order->entries[entry].running --;
        }

        changed = true;
    }

    if (changed) {
        ecs_singleton_modified(it->world, OreCraftBook);
    }
}

/** Work the crafts of the workers that are not paused, and start one on every idle worker.
 *
 * @param it Iterator over the workers, with OreCraftBook, while playing
 */
static void OreCraftWork(
    ecs_iter_t *it)
{
    OreCrafter *crafter = ecs_field(it, OreCrafter, 0);
    OreCraftBook *book = ecs_field(it, OreCraftBook, 1);
    OreCrafter before;
    float speed;
    int32_t i;
    bool changed = false;
    for (i = 0; i < it->count; i ++) {
        before = crafter[i];
        speed = crafter[i].speed > 0 ? crafter[i].speed : 1.0f;
        if (crafter[i].paused) {
            continue;
        }

        if (ore_crafter_busy(&crafter[i])) {
            changed = ore_craft_work(it->stage, book, &crafter[i], it->delta_time * speed) ||
                changed;
        } else {
            ore_crafter_stop(&crafter[i]);
            changed = (book->count && ore_craft_begin(it->world, book, &crafter[i])) ||
                changed;
        }

        if (ecs_os_memcmp_t(&before, &crafter[i], OreCrafter)) {
            ecs_modified(it->stage, it->entities[i], OreCrafter);
        }
    }

    if (changed) {
        ecs_singleton_modified(it->stage, OreCraftBook);
    }
}

/** Wrap up the orders without entries and update what the order book shows.
 *
 * @param it Iterator with OreCraftBook, while playing
 */
static void OreCraftUpdate(
    ecs_iter_t *it)
{
    OreCraftBook *book = ecs_field(it, OreCraftBook, 0);
    OreCraftBook before = *book;
    ore_craft_settle(it->stage, book);
    ore_craft_refresh(it->world, book);
    if (ecs_os_memcmp_t(&before, book, OreCraftBook)) {
        ecs_singleton_modified(it->stage, OreCraftBook);
    }
}

/** Check for every item prefab with a recipe whether ordering one unit would succeed now, and
 * count the flips in OreCanOrderFlips.
 *
 * @param it Iterator over the items with a recipe, with OreCraftBook and OreCanOrderFlips,
 * while playing
 */
static void OreCanOrderUpdate(
    ecs_iter_t *it)
{
    OreCanOrder *can = ecs_field(it, OreCanOrder, 0);
    const OreCraftBook *book = ecs_field(it, OreCraftBook, 2);
    OreCanOrderFlips *flips = ecs_field(it, OreCanOrderFlips, 5);
    ore_craft_limits_t limits = ore_craft_limits(it->world);
    ore_plan_t plan;
    int32_t i, demand, count = flips->count;
    bool value;
    for (i = 0; i < it->count; i ++) {
        value = ore_craft_context(it->world, book, &limits, it->entities[i], false, false,
            &plan, &demand);
        if (can[i].value != value) {
            can[i].value = value;
            ecs_modified(it->stage, it->entities[i], OreCanOrder);
            flips->count ++;
        }
    }

    if (flips->count != count) {
        ecs_singleton_modified(it->stage, OreCanOrderFlips);
    }
}

/** Compute the raw ore of the item OreNeeds names.
 *
 * @param it Iterator with OreNeeds and OreCraftBook, while playing
 */
static void OreNeedsUpdate(
    ecs_iter_t *it)
{
    OreNeeds *needs = ecs_field(it, OreNeeds, 0);
    const OreCraftBook *book = ecs_field(it, OreCraftBook, 1);
    ore_craft_limits_t limits = ore_craft_limits(it->world);
    OreNeed ores[ORE_RAW_ORES];
    int32_t count;
    ecs_os_memset(ores, 0, ECS_SIZEOF(ores));
    count = ore_craft_needs(it->world, book, &limits, needs->item, ores);
    if (count != needs->count || ecs_os_memcmp(ores, needs->ores, ECS_SIZEOF(ores))) {
        ecs_os_memcpy(needs->ores, ores, ECS_SIZEOF(ores));
        needs->count = count;
        ecs_singleton_modified(it->stage, OreNeeds);
    }
}

/** Order units of an item from a script, see ore_craft_order; only while playing.
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv The item, an entity, and the units, an i32
 * @param result Receives the units ordered, an i32
 */
static void ore_craft_order_function(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    *(int32_t*)result->ptr = ore_state_is(ctx->world, OreGameStatePlaying) ?
        ore_craft_order(ctx->world, *(ecs_entity_t*)argv[0].ptr, *(int32_t*)argv[1].ptr) : 0;
}

/** Take one unit off an order from a script, see ore_craft_cancel; only while playing.
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv The id of the order, an i32
 * @param result Receives whether a unit was taken off, a bool
 */
static void ore_craft_cancel_function(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    *(bool*)result->ptr = ore_state_is(ctx->world, OreGameStatePlaying) &&
        ore_craft_cancel(ctx->world, *(int32_t*)argv[0].ptr);
}

void ore_craft_import(
    ecs_world_t *world)
{
    ecs_entity_t module = ecs_get_scope(world);
    ecs_entity_t state = ecs_id(OreGameState);
    ecs_id_t playing = ore_state_pair(world, OreGameStatePlaying);

    ecs_add_pair(world, ecs_id(OreCanOrder), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(OreRecipe), EcsWith, ecs_id(OreCanOrder));
    ecs_singleton_ensure(world, OreCraftBook);
    ecs_singleton_ensure(world, OreCanOrderFlips);
    ecs_singleton_ensure(world, OreNeeds);

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "OreCrafterRemove" }),
        .query.terms = {
            { .id = ecs_id(OreCrafter) },
            { .id = ecs_id(OreCraftBook), .src.id = ecs_id(OreCraftBook), .inout = EcsInOut }
        },
        .events = { EcsOnRemove },
        .callback = OreCrafterRemove
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreCraftWork" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreCrafter), .src.id = EcsSelf },
            { .id = ecs_id(OreCraftBook), .src.id = ecs_id(OreCraftBook), .inout = EcsInOut },
            { .id = playing, .src.id = state }
        },
        .callback = OreCraftWork
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreCraftUpdate" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreCraftBook), .src.id = ecs_id(OreCraftBook), .inout = EcsInOut },
            { .id = playing, .src.id = state }
        },
        .callback = OreCraftUpdate
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreCanOrderUpdate" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreCanOrder), .src.id = EcsSelf },
            { .id = ecs_id(OreRecipe), .src.id = EcsSelf, .inout = EcsIn },
            { .id = ecs_id(OreCraftBook), .src.id = ecs_id(OreCraftBook), .inout = EcsIn },
            { .id = EcsPrefab, .oper = EcsOptional },
            { .id = playing, .src.id = state },
            { .id = ecs_id(OreCanOrderFlips), .src.id = ecs_id(OreCanOrderFlips),
              .inout = EcsInOut }
        },
        .callback = OreCanOrderUpdate
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreNeedsUpdate" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreNeeds), .src.id = ecs_id(OreNeeds), .inout = EcsInOut },
            { .id = ecs_id(OreCraftBook), .src.id = ecs_id(OreCraftBook), .inout = EcsIn },
            { .id = playing, .src.id = state }
        },
        .callback = OreNeedsUpdate
    });

    ecs_function(world, {
        .name = "craft_order",
        .parent = module,
        .return_type = ecs_id(ecs_i32_t),
        .params = {
            { .name = "item", .type = ecs_id(ecs_entity_t) },
            { .name = "count", .type = ecs_id(ecs_i32_t) }
        },
        .callback = ore_craft_order_function
    });

    ecs_function(world, {
        .name = "craft_cancel",
        .parent = module,
        .return_type = ecs_id(ecs_bool_t),
        .params = {{ .name = "order", .type = ecs_id(ecs_i32_t) }},
        .callback = ore_craft_cancel_function
    });
}

/**
 * @file game.h
 * @brief The oreelse module: components of the game and the functions its areas share.
 *
 * The scene (apps/oreelse/etc) holds the constants cfg.*, the models, the items, the singletons
 * and the entities of a run; the systems of each area (flow.c, map.c, player.c, ..., critters.c,
 * critter_fx.c, buildings.c, drones.c, placement.c, look.c, particles.c) play the game.
 */

#ifndef OREELSE_GAME_H
#define OREELSE_GAME_H

#include <oreelse.h>

#ifdef OREELSE_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

/** Zoom levels of the camera, the size of the arrays of OreZoom */
#define ORE_ZOOM_LEVELS (3)

/** Slots of the inventory, the size of OreInventory.slots and the most kinds it holds */
#define ORE_INVENTORY_SLOTS (15)

/** Inputs a recipe can have, the size of OreRecipe.inputs */
#define ORE_RECIPE_INPUTS (6)

/** Orders the order book holds, the size of OreCraftBook.orders and the most cfg.craftOrders */
#define ORE_CRAFT_ORDERS (8)

/** Entries an order holds, the size of OreCraftOrder.entries and the most cfg.craftEntries */
#define ORE_CRAFT_ENTRIES (8)

/** Item kinds a pool of an order holds, the size of OreCraftBag.stacks */
#define ORE_CRAFT_BAG (24)

/** Rows of the queue, the size of OreCraftBook.rows and the most cfg.craftRows */
#define ORE_CRAFT_ROWS (16)

/** Raw ores, the size of OreNeeds.ores */
#define ORE_RAW_ORES (4)

/** Buildings a slot of a connector rule names, the size of the arrays of OreConnectRule */
#define ORE_RULE_SLOT (4)

/** Cells a placement drag covers at most, the most cfg.dragMax */
#define ORE_DRAG_MAX (64)

/** Categories of the power allocation (OrePowerKind), the size of OrePowerAlloc.level */
#define ORE_POWER_KINDS (3)

/** Levels of a category of the power allocation, the bars of its column */
#define ORE_POWER_LEVELS (4)

/** Read the float constant cfg.<name> of the scene (etc/config.flecs) */
#define ORE_CFG(world, name) ecs_const_var_get_t(world, "cfg." name, ecs_f32_t)

/** Read the integer constant cfg.<name> of the scene (etc/config.flecs) */
#define ORE_CFG_I32(world, name) ecs_const_var_get_t(world, "cfg." name, ecs_i32_t)

/** State of the game, an exclusive relationship on its own singleton entity: scripts test it
 * with `OreGameState?[(OreGameState, Paused)]`. The game boots into Title and restart()
 * starts a new run in Playing. Gameplay systems match (OreGameState, Playing).
 */
ECS_ENUM(OreGameState, {
    OreGameStateTitle,   /**< The title screen, any key or a left or right click starts */
    OreGameStatePlaying, /**< The game runs */
    OreGameStatePaused,  /**< Everything stands still until cancel() resumes */
    OreGameStateWon,     /**< The astronaut left on a rocket with life support */
    OreGameStateLost     /**< The swarm killed the astronaut */
});

/** Seed of the session, a singleton main.c sets from --seed or the time. Run k after the start
 * draws from a seed derived from seed and k, see ore_seed_get.
 */
ECS_STRUCT(OreSeed, {
    uint64_t seed; /**< Seed of the session */
    int32_t run;   /**< Runs started before the current one, restart() counts it up */
});

/** The generator of the random choices C makes, a splitmix64 state that run.flecs seeds with
 * oreelse.seed(0) at the start of every run and every draw of ore_random01 advances.
 */
ECS_STRUCT(OreRandom, {
    uint64_t state; /**< State of the generator */
});

/** Play time of the run, counted while playing; run.flecs sets it to 0 */
ECS_STRUCT(OreClock, {
    float elapsed; /**< Seconds played, the survival time of the end screen */
});

/** Blink signals of the UI and the building lamps, computed from OreClock.elapsed, so they
 * freeze while paused. Modified only when one of them flips.
 */
ECS_STRUCT(OreBlink, {
    bool fast; /**< sin(elapsed * cfg.blinkFast) > 0 */
    bool slow; /**< sin(elapsed * cfg.blinkSlow) > 0 */
    bool lamp; /**< On for the first half of every 1 / cfg.lampRate seconds */
});

/** The one line message at the bottom of the screen, shown with ore_toast. Modified when a
 * message starts and when it ends, not while it counts.
 */
ECS_STRUCT(OreToast, {
    char *text; /**< The message, empty when none is shown */
    bool warn;  /**< The message is a warning, shown cfg.warnToast instead of cfg.infoToast */
    float age;  /**< Seconds the message was shown while playing */
});

/** The zoom levels of the camera, the type of cfg.zoom: level i sits height above the
 * astronaut and back behind it, looking at look above it.
 */
ECS_STRUCT(OreZoom, {
    float height[3]; /**< Height of the camera above the ground per level */
    float back[3];   /**< Distance of the camera behind the astronaut along +Z per level */
    float look[3];   /**< Height of the point the camera looks at per level */
});

/** The camera that follows the astronaut, a singleton scene.flecs sets. A restart keeps it,
 * and so the zoom level. Modified when the level or the glide changes.
 */
ECS_STRUCT(OreView, {
    ecs_entity_t camera; /**< Camera entity the view moves */
    ecs_entity_t view;   /**< The world view of the camera, whose cursor ray gives OreHover */
    int32_t level;       /**< Zoom level, 0 to ORE_ZOOM_LEVELS - 1, the wheel steps it */
    float height;        /**< Height of the camera, gliding to the level's; 0 snaps to it */
    float back;          /**< Distance behind the astronaut, gliding to the level's */
    float look;          /**< Height of the look point, gliding to the level's */
});

/** The held actions the C systems read, a singleton input.flecs sets */
ECS_STRUCT(OreActions, {
    ecs_entity_t forward; /**< Walk toward -Z */
    ecs_entity_t back;    /**< Walk toward +Z */
    ecs_entity_t left;    /**< Walk toward -X */
    ecs_entity_t right;   /**< Walk toward +X */
    ecs_entity_t run;     /**< Run at cfg.runMultiplier times the walk speed; held at the release
                           *   of a placement drag it fills the rectangle */
    ecs_entity_t mine;    /**< Mine the target on the hover tile, or demolish the building there */
    ecs_entity_t place;   /**< Place the selected building; its drag (ActionState.drag) tells a
                           *   click from a drag */
});

/** The astronaut the player walks with WASD. Modified when a member changes */
ECS_STRUCT(OrePlayer, {
    bool moving;  /**< A walk key is held while playing */
    bool running; /**< Moving with the run key held */
    float hover;  /**< Height of the jetpack above the ground, its Position.y: it rises to
                   *   cfg.hoverHeight over a tile with a building and falls back to 0 */
    bool airborne; /**< hover is above cfg.airborne, the jetpack fires */
});

/** A limb pivot of a model; its Rotation.x swings with the walk (OrePlayerFx for the astronaut,
 * OreCritterLimbs for a critter's legs)
 */
ECS_STRUCT(OreLimb, {
    float phase; /**< Offset of the limb in the walk cycle */
    float swing; /**< Largest angle of the swing in radians */
});

/** The mining of the astronaut: the target of the swing and its progress, which the HUD shows.
 * Modified when the progress or the target changes.
 */
ECS_STRUCT(OreMining, {
    ecs_entity_t target; /**< Entity the swing is keyed to, 0 for none */
    float left;          /**< Seconds left of the swing */
    float progress;      /**< Progress of the swing from 0 to 1, 1 - left / time */
});

/** The tile markers of mining, a singleton oreelse.flecs sets: an instance of each marker
 * prefab, disabled while it is not shown.
 */
ECS_STRUCT(OreMineMarker, {
    ecs_entity_t good; /**< The TileMarker, shown on a target in reach */
    ecs_entity_t bad;  /**< The TileMarkerBad, shown on a target out of reach */
});

/** An amount of an item: an input of a recipe or a slot of the inventory */
ECS_STRUCT(OreStack, {
    ecs_entity_t item; /**< Item prefab, 0 for none */
    int32_t amount;    /**< How many */
});

/** An item, on its prefab in resources.flecs (or a building's): what the UI shows of it.
 * Instances inherit it.
 */
ECS_STRUCT(OreItem, {
    char *name;   /**< Display name */
    char *detail; /**< One line of detail text */
});

/** A mined item, on its prefab: a swing at a deposit of it yields 1 */
ECS_STRUCT(OreMined, {
    float time;   /**< Seconds a swing takes */
    int32_t rank; /**< Place of the ore in lists of raw ore (OreNeeds), Iron first */
});

/** A crafted item, on its prefab: the inputs a craft takes and what it yields */
ECS_STRUCT(OreRecipe, {
    OreStack inputs[6]; /**< Items and amounts a craft takes, item 0 after the last */
    int32_t yield;      /**< Items one craft makes */
    float time;         /**< Seconds one craft takes */
});

/** The player's pack, a singleton run.flecs sets to the starting crate: any amount of an item
 * kind, at most cfg.inventorySlots kinds. Modified when an amount changes.
 *
 * A new kind takes the first free slot and keeps it until its count drops to 0, so the slot
 * order is the order the inventory screen shows.
 */
ECS_STRUCT(OreInventory, {
    OreStack slots[15]; /**< The slots in order, item 0 for a free one, else amount above 0 */
});

/** A worker of the order book: the astronaut, later every factory. An idle worker starts the
 * first startable entry of the oldest order; a craft takes the recipe's time divided by speed.
 */
ECS_STRUCT(OreCrafter, {
    ecs_entity_t item; /**< Item of the entry it crafts, 0 when idle */
    int32_t order;     /**< Id of the order of the entry, 0 when idle */
    float left;        /**< Work left in seconds at speed 1; 0 with an item: done, waiting to
                        *   deliver it to a full inventory */
    float total;       /**< Work of the craft in seconds at speed 1, the recipe's time */
    float speed;       /**< Work done per second, 1 for 0 or less */
    bool paused;       /**< Stopped: it neither works nor starts and keeps its craft */
});

/** Amounts of item kinds, the pool of an order: any amount of at most ORE_CRAFT_BAG kinds */
ECS_STRUCT(OreCraftBag, {
    OreStack stacks[24]; /**< The kinds, the first count of them with an amount above 0 */
    int32_t count;       /**< Kinds held */
});

/** A crafted item of an order and its crafts */
ECS_STRUCT(OreCraftEntry, {
    ecs_entity_t item; /**< The crafted item */
    int32_t count;     /**< Crafts left, those running included */
    int32_t running;   /**< Crafts workers are on */
    bool active;       /**< A worker is on it */
    float progress;    /**< Progress of the furthest worker on it, 0 to 1 */
    bool full;         /**< Blocked by a full inventory: it cannot start or deliver */
    bool stalled;      /**< The order is stalled and this entry has work or is blocked */
});

/** An order of the order book: the demand of one item, paid when placed, with its own pool */
ECS_STRUCT(OreCraftOrder, {
    int32_t id;              /**< Stable id, never reused within a run */
    ecs_entity_t item;       /**< The ordered item */
    int32_t demand;          /**< Units still to deliver, cancel takes one off */
    int32_t count;           /**< Crafts of the ordered item left plus running */
    int32_t entry_count;     /**< Entries held */
    OreCraftEntry entries[8]; /**< Crafted items, inputs before their parent, the ordered item
                               *   last */
    OreCraftBag pool;        /**< Materials the order paid and the surplus of its crafts */
    bool stalled;            /**< No worker live on it, nothing can start, and there is work
                              *   left or delivery is blocked */
    bool full;               /**< Its leftover pool does not fit the inventory */
    bool wrapping;           /**< No entries left, the leftover pool still to deliver */
});

/** A row of the queue panel: a header per order, then one per entry other than the ordered
 * item. (order, item) identifies a row.
 */
ECS_STRUCT(OreCraftRow, {
    int32_t order;     /**< Id of the order */
    ecs_entity_t item; /**< The ordered item for a header, else the entry's item */
    bool sub;          /**< An entry row, indented under its header */
    int32_t count;     /**< Crafts left plus running of the item */
    bool active;       /**< A worker is on it */
    float progress;    /**< Progress of the furthest worker on it, 0 to 1 */
    bool full;         /**< Blocked by a full inventory */
    bool stalled;      /**< Stalled, see OreCraftOrder and OreCraftEntry */
    bool wrapping;     /**< A header of an order wrapping up */
    bool cancel;       /**< A header of an order cancel can take a unit off */
});

/** The order book, a singleton run.flecs resets: the orders oldest first and the rows of the
 * queue panel. Modified when a value changes.
 */
ECS_STRUCT(OreCraftBook, {
    OreCraftOrder orders[8]; /**< The orders oldest first */
    int32_t count;           /**< Orders held */
    int32_t next_id;         /**< Id of the last order placed */
    OreCraftRow rows[16];    /**< The rows of the queue panel in order */
    int32_t row_count;       /**< Rows held */
    bool stalled;            /**< An order is stalled */
});

/** Whether ordering one unit of a crafted item now would succeed: paid from the inventory and
 * within every limit. On every item prefab with a recipe, updated while playing.
 */
ECS_STRUCT(OreCanOrder, {
    bool value; /**< Ordering one unit would succeed */
});

/** The raw ore of one unit of an item, a row of OreNeeds */
ECS_STRUCT(OreNeed, {
    ecs_entity_t item; /**< The raw ore */
    int32_t need;      /**< The larger of its full raw cost and what is missing */
    int32_t have;      /**< need minus what is missing, parts in the inventory count as ore */
});

/** The raw ore of the item the UI shows, a singleton: the UI sets item, the game the rest.
 * Modified when a value changes.
 */
ECS_STRUCT(OreNeeds, {
    ecs_entity_t item; /**< The item, set by the UI, 0 for none */
    OreNeed ores[4];   /**< The ores with a need above 0 in the order Iron, Copper, Stone,
                        *   Luminite (OreMined.rank) */
    int32_t count;     /**< Ores listed */
});

/** The footprint of a building, on its prefab and inherited: it covers w columns and h rows from
 * its top left tile (the lowest row and column). 1x1 without it.
 */
ECS_STRUCT(OreFootprint, {
    int32_t w;    /**< Columns it covers */
    int32_t h;    /**< Rows it covers */
    bool deposit; /**< It only goes on tiles with a deposit (the drill); every other building
                   *   only goes on tiles without one */
});

/** The most instances of a building the map holds, on its prefab and inherited */
ECS_STRUCT(OreBuildLimit, {
    int32_t max; /**< Most placed buildings of the prefab, 0 for no limit */
});

/** Hit points of a building (on its prefab, copied to every instance), the astronaut or a
 * critter. A building at 0 or below is destroyed.
 */
ECS_STRUCT(OreHealth, {
    float value; /**< Hit points left */
    float max;   /**< Hit points of the undamaged entity */
});

/** Power a building draws all the time it stands, on its prefab and inherited (OrePower) */
ECS_STRUCT(OrePowerConsumer, {
    float watts; /**< Watts it draws */
});

/** Power a building generates, on its prefab and inherited (OrePower) */
ECS_STRUCT(OrePowerProducer, {
    float watts; /**< Watts it generates */
});

/** Categories of the power allocation, the type of OrePowerCategory.kind */
ECS_ENUM(OrePowerKind, {
    OrePowerKindMining,      /**< Drills */
    OrePowerKindWeapons,     /**< Turrets */
    OrePowerKindConstruction /**< Factories */
});

/** The power category of a building, on its prefab and inherited (OrePower) */
ECS_STRUCT(OrePowerCategory, {
    OrePowerKind kind; /**< Category its power comes out of */
});

/** The power allocation, a singleton run.flecs sets to [2, 2, 2]: the level of every category,
 * 0 to ORE_POWER_LEVELS, at most cfg.powerBars in total. Modified when a level changes.
 */
ECS_STRUCT(OrePowerAlloc, {
    int32_t level[3]; /**< Level of Mining, Weapons and Construction, indexed by OrePowerKind */
});

/** The power of the base, a singleton run.flecs resets, for the HUD and the systems that stop
 * in a blackout. Modified when a value changes.
 */
ECS_STRUCT(OrePowerState, {
    float production; /**< Watts the producers generate */
    float demand;     /**< Watts of the consumers times the bars assigned / cfg.powerBars */
    float ratio;      /**< production / demand capped at 1, 1 without demand */
    bool blackout;    /**< demand above 0 and production below it: every consumer stops */
});

/** The mining of a drill, on its prefab and copied to every instance */
ECS_STRUCT(OreDrill, {
    float timer;  /**< Seconds of mining toward the next ore, kept through a blackout */
    bool running; /**< It mines: a live deposit on its tile, power and Mining above 0 */
});

/** A building that is a worker of the order book (the factory), on its prefab and inherited:
 * placed, it gets an OreCrafter of speed times the multiplier of its category.
 */
ECS_STRUCT(OreWorker, {
    float speed; /**< Speed of its crafter at multiplier 1 */
});

/** The drones of a drone pad, on its prefab and inherited: placed, the pad gets count
 * instances of drone as its children, docked over its bays. Drone b docks at the angle
 * (b + 0.5) * 2 PI / count, radius from the pad's center and dock above the ground.
 */
ECS_STRUCT(OreDroneBay, {
    ecs_entity_t drone; /**< Prefab of the drones, RepairDrone */
    int32_t count;      /**< Drones of the pad */
    float radius;       /**< Distance of the bays from the pad's center */
    float dock;         /**< Height of a docked drone above the ground */
    ecs_entity_t cost;  /**< Item the repairs are paid in, Iron */
});

/** States of a repair drone, the type of OreDrone.state */
ECS_ENUM(OreDroneState, {
    OreDroneStateDocked,    /**< Parked over its bay, looking for a target every cfg.droneScan */
    OreDroneStateFlying,    /**< Flying straight to cfg.droneCruise above its target */
    OreDroneStateRepairing, /**< Within cfg.droneArrive of its target, welding it */
    OreDroneStateReturning  /**< Flying back to its bay, looking for a target every
                             *   cfg.droneScan */
});

/** A repair drone of a drone pad, on the instance: a child of the pad, so it goes with it. Its
 * Position is relative to the pad. Drones have no health: critters never target them.
 */
ECS_STRUCT(OreDrone, {
    ecs_entity_t pad;    /**< The pad, its parent */
    ecs_entity_t target; /**< Building it repairs or flies to, 0 for none; never the target of
                          *   another drone of its pad */
    OreDroneState state; /**< What it does */
    int32_t bay;         /**< Its bay, 0 to OreDroneBay.count - 1 */
    float scan;          /**< Seconds to the next look for a target while docked or returning */
    float credit;        /**< Hit points repaired not yet paid for, below cfg.droneHpPerIron; it
                          *   carries over between targets */
});

/** The bulb of a status lamp, which the Light template (etc/props/light.flecs) sets with the
 * colors of its props: OrePower colors it by the power of the building it is part of.
 */
ECS_STRUCT(OreStatusLamp, {
    Rgb live;            /**< Color at level 3 and 4, and without a category */
    Rgb dead;            /**< Color in a blackout (a consumer) or at level 0 */
    Rgb warn;            /**< Color at level 1 */
    Rgb ok;              /**< Color at level 2 */
    float strength;      /**< Emissive strength while lit */
    float light;         /**< Intensity of the bulb's point light while lit */
    ecs_entity_t owner;  /**< The building, found by OrePower, 0 until then */
    bool consumer;       /**< The building draws power, so a blackout kills the lamp */
});

/** The status lamps to update, a singleton: observers count version up when the power state,
 * the allocation, the blink signals, a building or a lamp change, and OreStatusLamps updates
 * every lamp while applied lags behind.
 */
ECS_STRUCT(OreLamps, {
    int32_t version; /**< Changes counted */
    int32_t applied; /**< The version the lamps show */
});

/** A placed building, on the instance: its footprint starts at (row, col), every tile of it maps
 * to the building in the tile grid of OreMap while it is set. Its Position is the center of the
 * footprint.
 */
ECS_STRUCT(OreBuilding, {
    int32_t row; /**< Row of its top left tile */
    int32_t col; /**< Column of its top left tile */
});

/** An auto tiling rule of rules.flecs, the BuildingRule2x1 of the original: out joins two
 * buildings on orthogonally neighboring tiles.
 *
 * A slot names the buildings it accepts, 0 after the last; an empty slot accepts any tile. left
 * and right match a pair in a row, top and bottom one in a column; with rotate the pair matches
 * swapped too, and a rule with only left and right also matches a column, turned by PI / 2.
 */
ECS_STRUCT(OreConnectRule, {
    ecs_entity_t left[4];   /**< Buildings of the west tile of a pair in a row */
    ecs_entity_t right[4];  /**< Buildings of the east tile of a pair in a row */
    ecs_entity_t top[4];    /**< Buildings of the north tile of a pair in a column */
    ecs_entity_t bottom[4]; /**< Buildings of the south tile of a pair in a column */
    ecs_entity_t out;       /**< Prefab of the connector placed on the shared edge */
    bool rotate;            /**< The rule matches either order and turns for a column */
});

/** A connector a rule placed, on the instance: it sits on the edge between (row, col) and the
 * tile east of it, or south of it when vertical, and is registered in OreMap. Purely visual.
 */
ECS_STRUCT(OreConnector, {
    int32_t row;   /**< Row of the west or north tile of the edge */
    int32_t col;   /**< Column of the west or north tile of the edge */
    bool vertical; /**< The edge is between (row, col) and (row + 1, col) */
});

/** Placement, a singleton run.flecs resets: the building selected for placement and the drag in
 * flight. Modified when a value changes, so the inventory screen can show the selection.
 */
ECS_STRUCT(OrePlacement, {
    ecs_entity_t root;     /**< Parent of the placed buildings and connectors, an entity of the
                            *   run, so a restart deletes them */
    ecs_entity_t selected; /**< Building prefab selected for placement, 0 for none */
    bool dragging;         /**< A press of Place on a tile with a selection, until its release */
    int32_t anchor_row;    /**< Row of the tile the drag was pressed on */
    int32_t anchor_col;    /**< Column of the tile the drag was pressed on */
});

/** The preview of placement, a singleton oreelse.flecs sets: the markers and the ghosts of the
 * cells the selected building would go on are pooled children of root.
 */
ECS_STRUCT(OrePreview, {
    ecs_entity_t root; /**< Parent of the markers and ghosts of the preview */
    ecs_entity_t good; /**< Marker prefab of a tile of a cell that can be placed, TileMarker */
    ecs_entity_t bad;  /**< Marker prefab of a tile of a cell that cannot, TileMarkerBad */
});

/** Selects a building for placement, on the actions of the digit keys (input.flecs) */
ECS_STRUCT(OreSelectKey, {
    ecs_entity_t building; /**< Building prefab the key selects */
});

/** The tile under the cursor, a singleton: where the cursor ray of the world view (OreView)
 * meets the ground y = 0. Mining, placement and the pipette read it. Modified when it changes.
 */
ECS_STRUCT(OreHover, {
    bool on_grid; /**< The ray meets the ground on a tile of the grid; else there is no tile */
    int32_t row;  /**< Row of the tile, -1 without one */
    int32_t col;  /**< Column of the tile, -1 without one */
    float fx;     /**< Fraction of the point inside the tile along x, 0 to 1 */
    float fz;     /**< Fraction of the point inside the tile along z, 0 to 1 */
    vec3 point;   /**< Point on the ground, kept when the ray misses it */
});

/** An ore deposit on one tile, registered in the tile grid of OreMap when it is set */
ECS_STRUCT(OreDeposit, {
    ecs_entity_t resource; /**< Item prefab the deposit yields (resources.flecs) */
    int32_t amount;        /**< Ore left, the deposit is deleted at 0 */
    int32_t capacity;      /**< Ore of the full deposit */
    int32_t row;           /**< Row of its tile, set when it registers, -1 off the grid */
    int32_t col;           /**< Column of its tile, set when it registers, -1 off the grid */
});

/** The tile grid, a singleton: the deposit and the building on every tile. A deposit and a
 * building can share a tile (a drill sits on a deposit). The cells clear themselves when their
 * entities are deleted, so a restart that deletes the run's entities empties it.
 */
ECS_STRUCT(OreMap, {
    int32_t rows;              /**< Rows of the grid, cfg.rows once a cell was registered */
    int32_t cols;              /**< Columns of the grid, cfg.cols once a cell was registered */
ECS_PRIVATE                    /**< The cells are not reflected */
    ecs_entity_t *deposits;    /**< Deposit of every tile, row major, 0 for none */
    ecs_entity_t *buildings;   /**< Building of every tile, row major, 0 for none */
    ecs_entity_t *links;       /**< Connector of the east (2 i) and south (2 i + 1) edge of
                                *   every tile i, 0 for none */
    ecs_vec_t rejected;        /**< Deposits that registered on a taken tile, deleted by
                                *   ore_map_purge: an entity cannot be deleted while the
                                *   script that creates it still sets it */
    ecs_vec_t dirty;           /**< Tiles (row major indices) whose building changed, the
                                *   connectors of their edges to sync */
});

/** Render quality tiers, the type of OreQuality.tier. The pause screen's button cycles
 * them down, High to Medium to Low and back to High (oreelse.cycleQuality()).
 */
ECS_ENUM(OreRenderQuality, {
    OreRenderQualityLow,    /**< Half resolution, small and short shadows, no Gtao or Bloom */
    OreRenderQualityMedium, /**< Full resolution, smaller shadow map, no Gtao */
    OreRenderQualityHigh    /**< Full resolution and every effect, the tier at every start */
});

/** Number of render quality tiers, the size of the arrays of OreQualityTiers */
#define ORE_QUALITY_TIERS (3)

/** The render quality tier, a singleton oreelse.flecs sets to High. A restart keeps it.
 * Modified when cycleQuality() changes it.
 */
ECS_STRUCT(OreQuality, {
    OreRenderQuality tier; /**< The tier the look applies */
});

/** The settings of every render quality tier, the type of cfg.quality, indexed by
 * OreRenderQuality.
 */
ECS_STRUCT(OreQualityTiers, {
    float resolution_scale[3];    /**< Surface.resolution_scale of the surfaces */
    int32_t shadow_resolution[3]; /**< ShadowSettings.resolution of the world view */
    float shadow_distance[3];     /**< ShadowSettings.max_distance of the world view */
    bool gtao[3];                 /**< The Gtao effect of the world view runs */
    bool bloom[3];                /**< The Bloom effect of the world view runs */
});

/** What the render quality tier applies to, a singleton oreelse.flecs sets: the world view and
 * its effects. Every surface that does not render into a texture takes the resolution scale.
 */
ECS_STRUCT(OreLook, {
    ecs_entity_t view;  /**< The world view, its shadows and effects follow the tier */
    ecs_entity_t gtao;  /**< Gtao effect of the world view */
    ecs_entity_t bloom; /**< Bloom effect of the world view */
});

/** The flash light of a play of an explosion or a combust, on the root of the instance.
 *
 * ore_explosion and ore_combust set it at every play, and the light of the instance (its child
 * with an EffectFlash and a PointLight) takes the peak and the range of the play.
 */
ECS_STRUCT(OreBlastFlash, {
    float peak;  /**< Intensity the flash starts at, the EffectFlash.intensity of the light */
    float range; /**< Range of the light */
});

/** Tiers of critters the waves spawn, the size of the arrays of OreArsenal, OreWaveMix and
 * OreWaveState.spawn: mite, skitter, brute and behemoth
 */
#define ORE_WAVE_TIERS (4)

/** Neighbors a critter keeps for the crowd separation, the most cfg.crowdNeighbors */
#define ORE_CROWD_NEAR (12)

/** Walking speed of a critter in units per second, on its prefab and copied */
ECS_STRUCT(OreSpeed, {
    float value; /**< Units per second */
});

/** Damage of a critter, on its prefab and copied: per second, applied as value * dt while its
 * target is in range
 */
ECS_STRUCT(OreDamage, {
    float value; /**< Hit points per second */
});

/** Attack range of a critter, on its prefab and copied: the XZ distance between the centers of
 * the critter and its target at which it attacks
 */
ECS_STRUCT(OreRange, {
    float value; /**< Units */
});

/** Body radius of a critter, on its prefab and copied: the crowd separation keeps two critters
 * the sum of their radii apart, and its mass is radius squared
 */
ECS_STRUCT(OreRadius, {
    float value; /**< Units */
});

/** What a critter costs a wave, on its prefab and inherited */
ECS_STRUCT(OreWaveTier, {
    float cost;   /**< Budget a critter of the tier takes */
    int32_t wave; /**< First wave that can spawn the tier */
});

/** The walk of a critter, on its prefab and inherited: its walk cycle advances step radians per
 * unit walked and its body bobs up to bob units over it (OreCritterFx)
 */
ECS_STRUCT(OreGait, {
    float step; /**< Radians of the walk cycle per unit walked */
    float bob;  /**< Height of the bob in units, |sin(anim)| * bob */
});

/** The effect a critter dies in, on its prefab and inherited: ore_combust plays it at the
 * critter's radius; a critter without one, or with burst 0, dies in OreArsenal.splat
 */
ECS_STRUCT(OreDeathBurst, {
    ecs_entity_t burst; /**< The effect template, CombustBurst, 0 for the splat */
});

/** The critter prefabs of the tiers and the effects of their deaths, a singleton oreelse.flecs
 * sets: Mite, Skitter, Brute and Behemoth, in the order of the tiers of OreWaveMix and
 * OreWaveState.spawn
 */
ECS_STRUCT(OreArsenal, {
    ecs_entity_t critters[4]; /**< Prefab of every tier */
    ecs_entity_t splat;       /**< Effect of a death without OreDeathBurst, SplatBurst */
    ecs_entity_t embers;      /**< Template that smolders on a corpse, CorpseEmbers */
});

/** A share of every tier, the type of the style mixes cfg.mixedMix, cfg.swarmMix, cfg.eliteMix */
ECS_STRUCT(OreWaveMix, {
    float tier[4]; /**< Share of the tier in the wave's budget */
});

/** Styles of a wave, one draw per wave: the mix of tiers its budget is spent on */
ECS_ENUM(OreWaveStyle, {
    OreWaveStyleMixed, /**< Every tier alike, cfg.mixedMix */
    OreWaveStyleSwarm, /**< Mostly mites, cfg.swarmMix */
    OreWaveStyleElite  /**< Mostly brutes and behemoths, cfg.eliteMix */
});

/** The waves of a run, a singleton run.flecs resets. Modified every frame of play, the HUD
 * shows the wave, frac and kills.
 */
ECS_STRUCT(OreWaveState, {
    ecs_entity_t root;   /**< Parent of the critters, an entity of the run, so a restart
                          *   deletes them */
    int32_t wave;        /**< The last wave that started, 0 before the first */
    float timer;         /**< Seconds to the next wave, counted down only while left is 0 */
    float gap;           /**< Seconds the timer started from: cfg.firstWaveAt, then
                          *   cfg.waveGap */
    float frac;          /**< Part of the gap to the next wave that passed, 1 - timer / gap,
                          *   1 while a wave spawns */
    int32_t left;        /**< Critters of the wave still to spawn */
    int32_t spawn[4];    /**< Critters of every tier still to spawn */
    float spawn_timer;   /**< Seconds to the next spawn of the wave */
    float stagger;       /**< Seconds between two spawns of the wave */
    float budget;        /**< Budget of the last wave */
    OreWaveStyle style;  /**< Style of the last wave */
    int32_t side;        /**< Side of the map the last critter spawned on: 0 north, 1 south,
                          *   2 west, 3 east, -1 for none */
    int32_t kills;       /**< Critters killed in the run */
});

/** A critter of a wave, on the instance: the instance of a critter prefab that walks to its
 * target and chews it. Its OreTarget and OreVelocity come with it.
 */
ECS_STRUCT(OreCritter, {
    float retarget;  /**< Seconds to the next acquisition of a target */
    float jitter;    /**< Heading jitter from -1 to 1, drawn at the spawn: the critter walks
                      *   jitter * cfg.critterJitter radians off the direction to its goal */
    bool attacking;  /**< Its target is in range and it chews it */
    float anim;      /**< Phase of the walk cycle in radians, drawn from U(0, 2 PI) at the spawn
                      *   and advanced while it walks (OreCritterFx) */
});

/** A dead critter's corpse, on the corpse: an instance of the critter's prefab that is no critter
 * (no OreCritter, so no AI, no target, no separation and not in OreCritterIndex), tipped over and
 * charred, with the CorpseEmbers on it.
 *
 * It lies cfg.corpseTime seconds of play, sinks at cfg.corpseSink units a second and is deleted
 * depth below the ground; at most cfg.corpseCap corpses, the oldest goes first (OreCorpses).
 */
ECS_STRUCT(OreCorpse, {
    float age;   /**< Seconds of play since the critter died */
    float depth; /**< Depth below the ground at which it is deleted, 2 r + 1 of its radius r */
});

/** What a critter goes for: the astronaut or a building */
ECS_STRUCT(OreTarget, {
    ecs_entity_t value; /**< The target, 0 for none */
});

/** Velocity of a critter on the ground in units per second, 0 while it attacks: the gun turret
 * leads its shots with it
 */
ECS_STRUCT(OreVelocity, {
    float x; /**< Along X */
    float z; /**< Along Z */
});

/** The time since the astronaut was last hurt, on the astronaut: after cfg.playerRegenDelay
 * seconds it regenerates cfg.playerRegen hit points a second
 */
ECS_STRUCT(OreHurt, {
    float since; /**< Seconds of play since the last damage */
});

/** The critter spatial index, a singleton: the critters with hit points left in a uniform grid of
 * cells of cfg.crowdCell over the map. OreCrowd rebuilds it every frame of play after the
 * separation, so the systems after it find critters near a point (ore_critter_nearest).
 */
ECS_STRUCT(OreCritterIndex, {
    int32_t count;          /**< Critters indexed */
    int32_t dim_x;          /**< Cells along X */
    int32_t dim_z;          /**< Cells along Z */
    float cell;             /**< Edge of a cell, cfg.crowdCell */
    float x0;               /**< X of the west edge of the map, the first cell */
    float z0;               /**< Z of the north edge of the map, the first cell */
ECS_PRIVATE                 /**< The cells are not reflected */
    ecs_entity_t *entities; /**< Critter of every slot */
    float *x;               /**< X of every slot */
    float *z;               /**< Z of every slot */
    float *r;               /**< Radius of every slot */
    float *px;              /**< Push of the separation along X of every slot */
    float *pz;              /**< Push of the separation along Z of every slot */
    Position **pos;         /**< Position of the critter of every slot, valid within OreCrowd */
    int32_t *cell_of;       /**< Cell of every slot, row major */
    int32_t *order;         /**< Slots sorted by cell */
    int32_t *start;         /**< First entry of order of every cell, and the end after the last */
    int32_t cap;            /**< Slots allocated */
    int32_t cells;          /**< Entries of start allocated */
});

/** Marks an entity created by a script that sets up a run: restart() reloads the script of
 * every entity with it, which deletes the entities of the run and creates those of a new one
 */
extern ECS_TAG_DECLARE(OreRunScript);

/** Marks the root of a ghost, the translucent preview of a building: the parts below it draw
 * without emission (OreGhostDim)
 */
extern ECS_TAG_DECLARE(OreGhost);

/** The geometry of the tile grid, read from cfg once per system run */
typedef struct ore_grid_t {
    int32_t rows; /**< Rows, cfg.rows */
    int32_t cols; /**< Columns, cfg.cols */
    float tile;   /**< Edge of a tile, cfg.tile */
    float x0;     /**< X of the center of column 0, cfg.x0 */
    float z0;     /**< Z of the center of row 0, cfg.z0 */
} ore_grid_t;

/** Check whether the game is in a state.
 *
 * @param world The world
 * @param state The state
 * @return Whether (OreGameState, state) is on the singleton
 */
bool ore_state_is(
    const ecs_world_t *world,
    OreGameState state);

/** Switch the game to a state.
 *
 * @param world The world or a stage
 * @param state The state
 */
void ore_state_set(
    ecs_world_t *world,
    OreGameState state);

/** Get the pair of a state, for a query term with OreGameState as its source.
 *
 * @param world The world
 * @param state The state
 * @return The pair (OreGameState, state)
 */
ecs_id_t ore_state_pair(
    const ecs_world_t *world,
    OreGameState state);

/** Complete the future of an async script function with a bool result.
 *
 * @param future The future
 * @param value The result
 */
void ore_resolve(
    ecs_script_future_t *future,
    bool value);

/** Derive a seed for the current run from the session seed (OreSeed): run k of a session
 * draws from a seed of its own, and every stream of a run is independent.
 *
 * @param world The world
 * @param stream Stream of the run: 0 the C generator, others the scripts (oreelse.seed)
 * @return The seed, never 0
 */
uint64_t ore_seed_get(
    const ecs_world_t *world,
    int32_t stream);

/** Draw the next number of the generator of the run (OreRandom).
 *
 * @param world The world
 * @return A number from 0 up to but not including 1
 */
float ore_random01(
    ecs_world_t *world);

/** Show a one line message at the bottom of the screen, replacing the current one.
 *
 * Warnings last cfg.warnToast seconds of play, other messages cfg.infoToast.
 *
 * @param world The world, or the stage of a system
 * @param warn Whether the message is a warning
 * @param fmt printf format of the message
 * @param ... Arguments of the format
 */
void ore_toast(
    ecs_world_t *world,
    bool warn,
    const char *fmt,
    ...);

/** Check whether an action is held.
 *
 * @param world The world
 * @param action The action entity, 0 for none
 * @return Whether the action is down this frame
 */
bool ore_action_down(
    const ecs_world_t *world,
    ecs_entity_t action);

/** Read the geometry of the tile grid from cfg.
 *
 * @param world The world
 * @return The grid
 */
ore_grid_t ore_grid_get(
    const ecs_world_t *world);

/** Get the x of the center of a column.
 *
 * @param grid The grid
 * @param col The column
 * @return x0 + col * tile
 */
float ore_tile_x(
    const ore_grid_t *grid,
    int32_t col);

/** Get the z of the center of a row.
 *
 * @param grid The grid
 * @param row The row
 * @return z0 + row * tile
 */
float ore_tile_z(
    const ore_grid_t *grid,
    int32_t row);

/** Get the column of the tile an x lies in.
 *
 * @param grid The grid
 * @param x World x
 * @return floor((x - x0) / tile + 0.5), possibly off the grid
 */
int32_t ore_col_at(
    const ore_grid_t *grid,
    float x);

/** Get the row of the tile a z lies in.
 *
 * @param grid The grid
 * @param z World z
 * @return floor((z - z0) / tile + 0.5), possibly off the grid
 */
int32_t ore_row_at(
    const ore_grid_t *grid,
    float z);

/** Check whether a tile is on the grid.
 *
 * @param grid The grid
 * @param row The row
 * @param col The column
 * @return Whether row and col are within the rows and columns
 */
bool ore_on_grid(
    const ore_grid_t *grid,
    int32_t row,
    int32_t col);

/** Get the deposit cell of a tile.
 *
 * @param map The tile grid
 * @param row The row
 * @param col The column
 * @return The cell, NULL off the grid or before the first registration
 */
ecs_entity_t* ore_map_deposit(
    OreMap *map,
    int32_t row,
    int32_t col);

/** Get the building cell of a tile.
 *
 * @param map The tile grid
 * @param row The row
 * @param col The column
 * @return The cell, NULL off the grid or before the first registration
 */
ecs_entity_t* ore_map_building(
    OreMap *map,
    int32_t row,
    int32_t col);

/** Get the connector cell of the edge between a tile and its east or south neighbor.
 *
 * @param map The tile grid
 * @param row The row
 * @param col The column
 * @param vertical The edge to the south neighbor, else to the east one
 * @return The cell, NULL off the grid or before the first registration
 */
ecs_entity_t* ore_map_link(
    OreMap *map,
    int32_t row,
    int32_t col,
    bool vertical);

/** Give the map the cells of a grid, empty, unless it has them already.
 *
 * @param map The tile grid
 * @param grid The grid of cfg
 */
void ore_map_fit(
    OreMap *map,
    const ore_grid_t *grid);

/** Mark a tile whose building changed, so the connectors of its edges are synced.
 *
 * @param map The tile grid
 * @param row The row
 * @param col The column
 */
void ore_map_touch(
    OreMap *map,
    int32_t row,
    int32_t col);

/** Find the tile under the cursor: where the cursor ray of the world view meets the ground y = 0.
 *
 * @param world The world
 * @param view The view, whose cursor ray it follows
 * @param hover Receives the tile; its point is kept when the ray misses the ground
 * @return Whether the cursor is on a tile of the grid
 */
bool ore_hover_find(
    const ecs_world_t *world,
    const OreView *view,
    OreHover *hover);

/** Delete the deposits that registered on a tile another deposit holds. The OnLoad phase
 * does it every frame, restart() right after it reloaded the scripts of the run.
 *
 * @param world The world or a stage
 */
void ore_map_purge(
    ecs_world_t *world);

/** Get the amount of an item in the inventory.
 *
 * @param world The world or a stage
 * @param item The item
 * @return The amount, 0 when the inventory does not hold the item
 */
int32_t ore_inventory_count(
    const ecs_world_t *world,
    ecs_entity_t item);

/** Get the kinds of items the inventory holds, the slots that are taken.
 *
 * @param world The world or a stage
 * @return The kinds, at most ORE_INVENTORY_SLOTS
 */
int32_t ore_inventory_kinds(
    const ecs_world_t *world);

/** Check whether a set of additions fits: the kinds the inventory would hold after adding
 * every new kind with a positive amount are at most ORE_INVENTORY_SLOTS.
 *
 * @param world The world or a stage
 * @param stacks The items and amounts to add, an item may appear more than once
 * @param count Number of stacks
 * @return Whether the additions fit
 */
bool ore_inventory_fits(
    const ecs_world_t *world,
    const OreStack *stacks,
    int32_t count);

/** Add an amount of an item to the inventory. A new kind takes the first free slot; when every
 * slot is taken nothing changes and the inventory full warning shows.
 *
 * @param world The world or the stage of a system
 * @param item The item
 * @param amount How many, nothing changes for 0 or less
 * @return Whether the amount was added, true for 0
 */
bool ore_inventory_add(
    ecs_world_t *world,
    ecs_entity_t item,
    int32_t amount);

/** Remove an amount of an item from the inventory. At 0 the slot of the item frees up.
 *
 * @param world The world or the stage of a system
 * @param item The item
 * @param amount How many, nothing changes for 0 or less
 * @return Whether the amount was removed: false, and nothing changes, when the inventory holds
 * less of the item, true for 0
 */
bool ore_inventory_remove(
    ecs_world_t *world,
    ecs_entity_t item,
    int32_t amount);

/** Get the display name of an item.
 *
 * @param world The world or a stage
 * @param item The item
 * @return Its OreItem name, else its entity name, never NULL
 */
const char* ore_item_name(
    const ecs_world_t *world,
    ecs_entity_t item);

/** Show the warning that the inventory holds as many kinds as it can.
 *
 * @param world The world or the stage of a system
 */
void ore_inventory_full(
    ecs_world_t *world);

/** Show a tile marker at a position or hide it; it is only written when that changes.
 *
 * @param world The world
 * @param stage The stage of the system
 * @param marker The marker entity, 0 for none
 * @param show Whether to show it
 * @param x X of the tile center
 * @param y Height of the marker
 * @param z Z of the tile center
 */
void ore_marker_show(
    const ecs_world_t *world,
    ecs_world_t *stage,
    ecs_entity_t marker,
    bool show,
    float x,
    float y,
    float z);

/** Mine 1 off a deposit into the inventory: at 0 the deposit is deleted. When its item is a
 * new kind and the inventory is full nothing is mined and the full warning shows.
 *
 * @param world The world or the stage of a system
 * @param deposit The deposit
 * @return Whether the deposit is still there to mine, false once it is deleted or gone
 */
bool ore_deposit_mine(
    ecs_world_t *world,
    ecs_entity_t deposit);

/** Play a one shot effect template at a position, the oreBurst of the original: its sizes,
 * speeds, spawn volumes and layer offsets at scale, unturned.
 *
 * @param world The world, or the stage of a system before EcsPreStore
 * @param effect The effect template (etc/particles)
 * @param position World position of the play
 * @param scale Scale of the play, 0 or less for 1
 * @return The root of the instance that plays, 0 when none does
 */
ecs_entity_t ore_burst(
    ecs_world_t *world,
    ecs_entity_t effect,
    const vec3 position,
    float scale);

/** Play a one shot effect template turned about Y, at its size: the launch of a missile
 * (MissileLaunch, authored along -Z) toward the aim of its turret, as the original's
 * oreLaunchFx turns the velocities and offsets of the bursts.
 *
 * @param world The world, or the stage of a system before EcsPreStore
 * @param effect The effect template
 * @param position World position of the play
 * @param aim Angle about Y in radians the template is turned by
 * @return The root of the instance that plays, 0 when none does
 */
ecs_entity_t ore_burst_aimed(
    ecs_world_t *world,
    ecs_entity_t effect,
    const vec3 position,
    float aim);

/** Play an explosion at a position, the oreExplosion of the original: the effect (BoomBurst)
 * at scale, and its flash light at cfg.explosionPeak and cfg.explosionRange times the scale.
 *
 * @param world The world, or the stage of a system before EcsPreStore
 * @param effect The effect template, BoomBurst
 * @param position World position of the blast
 * @param scale Scale of the blast, 0 or less for 1
 * @return The root of the instance that plays, 0 when none does
 */
ecs_entity_t ore_explosion(
    ecs_world_t *world,
    ecs_entity_t effect,
    const vec3 position,
    float scale);

/** Play the combust of a critter that died, the oreCombust of the original.
 *
 * The effect (CombustBurst, authored for radius 1) plays cfg.combustLift * r above the position,
 * its sizes and speeds at sqrt(r) and its spawn volumes and offsets, the scale of the root, at r.
 * The radius is clamped to cfg.combustRadiusMin and cfg.combustRadiusMax.
 *
 * Its flash light peaks at cfg.combustPeak * sqrt(r) and reaches cfg.combustRange +
 * cfg.combustRangeRadius * r.
 *
 * @param world The world, or the stage of a system before EcsPreStore
 * @param effect The effect template, CombustBurst
 * @param position World position of the critter
 * @param radius Body radius of the critter
 * @return The root of the instance that plays, 0 when none does
 */
ecs_entity_t ore_combust(
    ecs_world_t *world,
    ecs_entity_t effect,
    const vec3 position,
    float radius);

/** Get the footprint of a building or its prefab, 1x1 without OreFootprint.
 *
 * @param world The world or a stage
 * @param building The building or its prefab
 * @param w Receives the columns it covers
 * @param h Receives the rows it covers
 */
void ore_footprint(
    const ecs_world_t *world,
    ecs_entity_t building,
    int32_t *w,
    int32_t *h);

/** Get the building prefab of a placed building, its IsA target with OreFootprint or OreItem.
 *
 * @param world The world or a stage
 * @param building The building
 * @return The prefab, 0 for none
 */
ecs_entity_t ore_building_prefab(
    const ecs_world_t *world,
    ecs_entity_t building);

/** Place a building: an instance of its prefab under the root of OrePlacement, with its
 * footprint from (row, col) registered in the tile grid. Nothing is checked or spent.
 *
 * @param world The world or a stage
 * @param prefab The building prefab
 * @param row Row of the top left tile
 * @param col Column of the top left tile
 * @return The building
 */
ecs_entity_t ore_building_place(
    ecs_world_t *world,
    ecs_entity_t prefab,
    int32_t row,
    int32_t col);

/** Demolish a building into the inventory: an undamaged one as its item, a damaged one as the
 * salvage of its recipe at its health. Refused with a warning when the inventory has no room,
 * else the building is deleted.
 *
 * @param world The world or the stage of a system
 * @param building The building
 * @return Whether the building was demolished
 */
bool ore_building_demolish(
    ecs_world_t *world,
    ecs_entity_t building);

/** Cancel step: throw away the placement drag in flight with the toast Placement cancelled.
 *
 * @param world The world
 * @return Whether a drag was in flight
 */
bool ore_placement_cancel_drag(
    ecs_world_t *world);

/** Cancel step: clear the selection of a building for placement.
 *
 * @param world The world
 * @return Whether a building was selected
 */
bool ore_placement_deselect(
    ecs_world_t *world);

/** Spawn a critter: an instance of its prefab under the root of OreWaveState at full health,
 * without a target, its retarget timer at cfg.retargetInterval * U(0, 1), its heading jitter
 * drawn from U(-1, 1) and its walk cycle from U(0, 2 PI).
 *
 * @param world The world or a stage
 * @param prefab The critter prefab (Mite, Skitter, Brute, Behemoth)
 * @param x X of the critter
 * @param z Z of the critter
 * @return The critter, 0 when the prefab is no critter
 */
ecs_entity_t ore_critter_spawn(
    ecs_world_t *world,
    ecs_entity_t prefab,
    float x,
    float z);

/** Kill a critter: the kill counter of OreWaveState goes up, its death plays and leaves its corpse
 * (ore_critter_remains) and the critter is deleted. The one place a critter dies,
 * OreCritterDeath calls it for the critters at 0 hit points or below.
 *
 * @param world The world or the stage of a system
 * @param critter The critter
 */
void ore_critter_kill(
    ecs_world_t *world,
    ecs_entity_t critter);

/** Play the death of a critter and leave its corpse (ore_corpse_spawn), the original's
 * oreDamageCritter at 0 hit points: its OreDeathBurst at its position and radius (ore_combust),
 * or OreArsenal.splat cfg.splatLift above it without one. ore_critter_kill deletes it after.
 *
 * @param world The world or the stage of a system
 * @param critter The critter
 */
void ore_critter_remains(
    ecs_world_t *world,
    ecs_entity_t critter);

/** Lay the corpse of a critter: a new instance of its prefab with OreCorpse under the root of
 * OreWaveState, on the ground, tipped over in the original's pose turned by the critter's heading,
 * squashed to cfg.corpseSquash and charred with the Tint cfg.corpseColor (OreCorpseChar).
 *
 * An instance of OreArsenal.embers on it is scaled by its radius. Nothing is laid when
 * cfg.corpseTime is 0 or less.
 *
 * @param world The world or the stage of a system
 * @param prefab The critter prefab
 * @param x X of the corpse
 * @param z Z of the corpse
 * @param yaw Heading of the critter, its Rotation.y
 * @return The corpse, 0 for none
 */
ecs_entity_t ore_corpse_spawn(
    ecs_world_t *world,
    ecs_entity_t prefab,
    float x,
    float z,
    float yaw);

/** Find the nearest critter of the spatial index within a range of a point on the ground.
 *
 * @param index The index, OreCrowd rebuilt it this frame
 * @param x X of the point
 * @param z Z of the point
 * @param range Largest distance on XZ
 * @return The critter, 0 for none
 */
ecs_entity_t ore_critter_nearest(
    const OreCritterIndex *index,
    float x,
    float z,
    float range);

/** Register the critters: the waves that spawn them, their targets, attacks and moves, the
 * crowd separation and the spatial index, their deaths, the astronaut's health and its regen,
 * and the script function oreelse.spawn_critter(prefab, x, z).
 *
 * @param world The world
 */
void ore_critters_import(
    ecs_world_t *world);

/** Register the critters' animation, deaths and corpses: the walk, bob and waddle, the legs,
 * the corpses' char, aging, sinking and cap, and the script function oreelse.corpse(prefab,
 * position, yaw).
 *
 * @param world The world
 */
void ore_critter_fx_import(
    ecs_world_t *world);

/** Register the game flow: states, clock, blink, toast, cancel() and restart().
 *
 * @param world The world
 */
void ore_flow_import(
    ecs_world_t *world);

/** Register the map: the tile grid and the deposits that register in it.
 *
 * @param world The world
 */
void ore_map_import(
    ecs_world_t *world);

/** Register the player: walking and the camera that follows the astronaut.
 *
 * @param world The world
 */
void ore_player_import(
    ecs_world_t *world);

/** Register the inventory, the item components and the script functions
 * oreelse.inventory_add(item, amount) and oreelse.inventory_remove(item, amount).
 *
 * @param world The world
 */
void ore_inventory_import(
    ecs_world_t *world);

/** Register mining: the swing at the deposit or the building on the hover tile and its
 * marker.
 *
 * @param world The world
 */
void ore_mining_import(
    ecs_world_t *world);

/** Place an order for units of an item: the first unit creates a new order, every further
 * unit merges into it, until a unit cannot be paid or breaks a limit. Every unit is paid at
 * once: what the order borrows leaves the inventory for its pool.
 *
 * @param world The world
 * @param item The crafted item
 * @param count Units to order
 * @return Units ordered, 0 when the order was refused
 */
int32_t ore_craft_order(
    ecs_world_t *world,
    ecs_entity_t item,
    int32_t count);

/** Take one unit off an order and refund what the rest does not need. Refused with a warning
 * when the refund does not fit the inventory.
 *
 * @param world The world
 * @param id Id of the order
 * @return Whether a unit was taken off
 */
bool ore_craft_cancel(
    ecs_world_t *world,
    int32_t id);

/** Register crafting: the order book, the workers, OreCanOrder, OreNeeds and the script
 * functions oreelse.craft_order(item, count) and oreelse.craft_cancel(order).
 *
 * @param world The world
 */
void ore_craft_import(
    ecs_world_t *world);

/** Register the buildings: their registration in the tile grid, the connectors of the rules,
 * destroyed buildings and the look of the ghosts.
 *
 * @param world The world
 */
void ore_buildings_import(
    ecs_world_t *world);

/** Register the repair drones: the drones a placed drone pad parks and the system that flies
 * them to the damaged buildings and welds them, paid from the inventory.
 *
 * @param world The world
 */
void ore_drones_import(
    ecs_world_t *world);

/** Register placement: the selection, the click and drag placement, the preview and the script
 * functions oreelse.select(), oreelse.pipette(), oreelse.placeStart(), oreelse.placeEnd() and
 * oreelse.cancelDrag().
 *
 * @param world The world
 */
void ore_placement_import(
    ecs_world_t *world);

/** Get the level of the power category of a building.
 *
 * @param world The world or a stage
 * @param building The building or its prefab
 * @return The level of its category in OrePowerAlloc, -1 without a category
 */
int32_t ore_power_level(
    const ecs_world_t *world,
    ecs_entity_t building);

/** Get the power multiplier of a building: the level of its category times cfg.powerStep. For
 * one building; a system over many reads OrePowerAlloc and cfg.powerStep once.
 *
 * @param world The world or a stage
 * @param building The building or its prefab
 * @return The multiplier, 1 without a category, 0 at level 0
 */
float ore_power_mul(
    const ecs_world_t *world,
    ecs_entity_t building);

/** Click bar level of the column of a category: the topmost lit bar steps the category down
 * one level, any other sets it to level, an increase capped by the free bars. When nothing can
 * be raised the warning that the bars are all assigned shows.
 *
 * @param world The world
 * @param kind The category, an OrePowerKind
 * @param level The bar clicked, 1 to ORE_POWER_LEVELS
 * @return Whether the level of the category changed
 */
bool ore_power_set(
    ecs_world_t *world,
    int32_t kind,
    int32_t level);

/** Register power: production, demand and the blackout, the drills, the factories' crafters,
 * the status lamps and the script function oreelse.power_set(category, level).
 *
 * @param world The world
 */
void ore_power_import(
    ecs_world_t *world);

/** Register the look: the render quality tiers and oreelse.cycleQuality().
 *
 * @param world The world
 */
void ore_look_import(
    ecs_world_t *world);

/** Register the particle effects: the flash lights of the explosions and combusts and the
 * script functions oreelse.explosion() and oreelse.combust().
 *
 * @param world The world
 */
void ore_particles_import(
    ecs_world_t *world);

/** Register the oreelse module.
 *
 * Registers the components, the systems of every area and the script functions
 * oreelse.seed(), oreelse.cancel(), oreelse.restart(), oreelse.cycleQuality(),
 * oreelse.explosion(), oreelse.combust(), oreelse.spawn_critter() and oreelse.corpse().
 *
 * @param world The world
 */
void OreelseImport(
    ecs_world_t *world);

#undef ECS_META_IMPL

#endif

/**
 * @file game.h
 * @brief The oreelse module: components of the game and the functions its areas share.
 *
 * The scene (apps/oreelse/etc) holds the constants cfg.*, the models, the items, the singletons
 * and the entities of a run; the systems of each area (flow.c, map.c, player.c, inventory.c,
 * mining.c, look.c) play the game.
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
    ecs_entity_t run;     /**< Run at cfg.runMultiplier times the walk speed */
    ecs_entity_t mine;    /**< Mine the target on the hover tile */
});

/** The astronaut the player walks with WASD. Modified when a member changes */
ECS_STRUCT(OrePlayer, {
    bool moving;  /**< A walk key is held while playing */
    bool running; /**< Moving with the run key held */
});

/** A limb pivot of a model; its Rotation.x swings with the walk (OrePlayerFx) */
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
    float time; /**< Seconds a swing takes */
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
    ecs_vec_t rejected;        /**< Deposits that registered on a taken tile, deleted by
                                *   ore_map_purge: an entity cannot be deleted while the
                                *   script that creates it still sets it */
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

/** Marks an entity created by a script that sets up a run: restart() reloads the script of
 * every entity with it, which deletes the entities of the run and creates those of a new one
 */
extern ECS_TAG_DECLARE(OreRunScript);

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

/** Show the warning that the inventory holds as many kinds as it can.
 *
 * @param world The world or the stage of a system
 */
void ore_inventory_full(
    ecs_world_t *world);

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

/** Register mining: the swing at the deposit on the hover tile and its marker.
 *
 * @param world The world
 */
void ore_mining_import(
    ecs_world_t *world);

/** Register the look: the render quality tiers and oreelse.cycleQuality().
 *
 * @param world The world
 */
void ore_look_import(
    ecs_world_t *world);

/** Register the oreelse module.
 *
 * Registers the components, the systems of every area and the script functions
 * oreelse.seed(), oreelse.cancel(), oreelse.restart() and oreelse.cycleQuality().
 *
 * @param world The world
 */
void OreelseImport(
    ecs_world_t *world);

#undef ECS_META_IMPL

#endif

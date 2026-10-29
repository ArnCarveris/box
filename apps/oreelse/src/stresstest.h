/**
 * @file stresstest.h
 * @brief The stress test mode: its reflected components and the functions it shares, included
 * by game.h, which holds the rest of the game's.
 */

#ifndef OREELSE_STRESSTEST_H
#define OREELSE_STRESSTEST_H

/** The stress test of the session, a singleton main.c sets from --stresstest[=N]. Without it
 * the game plays normally. With it every run starts as a late game base (src/stresstest.c).
 */
ECS_STRUCT(OreStressTest, {
    float minutes; /**< Minutes of play every run starts at, 0 or less for cfg.stressMinutes */
    int32_t run;   /**< OreSeed.run the layout was last built for, -1 before the first */
});

/** A building the stress test places at the first free cell near a tile */
ECS_STRUCT(OreStressSite, {
    ecs_entity_t building; /**< Building prefab, 0 for none */
    int32_t row;           /**< Row of the tile the search starts at */
    int32_t col;           /**< Column of the tile the search starts at */
});

/** The layout of the stress test, a singleton etc/stresstest.flecs sets: the buildings of its
 * rings, the buildings it places near a tile and the stock it adds to the inventory.
 */
ECS_STRUCT(OreStressKit, {
    ecs_entity_t wall;       /**< The building of the two wall rings */
    ecs_entity_t drill;      /**< The building on every deposit */
    ecs_entity_t solar;      /**< The building of the solar farm */
    ecs_entity_t turrets[4]; /**< The turrets the turret ring cycles, 0 for none */
    OreStressSite sites[8];  /**< The buildings placed near a tile, in order */
    OreStack stock[16];      /**< Items added to the starting crate, item 0 for none */
});

/** Place a building for free and out of reach: an instance of its prefab when its tiles are
 * free for it (on the grid, without a building, on deposits for a drill and off them for every
 * other building) and its build limit is not reached. Nothing is spent and nothing plays.
 *
 * @param world The world, not a stage
 * @param prefab The building prefab
 * @param row Row of the top left tile
 * @param col Column of the top left tile
 * @return The building, 0 when it cannot be placed there
 */
ecs_entity_t ore_placement_build(
    ecs_world_t *world,
    ecs_entity_t prefab,
    int32_t row,
    int32_t col);

/** Register the stress test: the layout every run of a session with OreStressTest starts with.
 *
 * @param world The world
 */
void ore_stresstest_import(
    ecs_world_t *world);

#endif

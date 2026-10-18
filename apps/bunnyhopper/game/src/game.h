/**
 * @file game.h
 * @brief Private declarations of the bunnyhopper module shared by its sources.
 */

#ifndef BUNNYHOPPER_GAME_PRIVATE_H
#define BUNNYHOPPER_GAME_PRIVATE_H

#include <bunnyhopper_game.h>

/** The constants cfg.* of the scene, with the values of the original game when one is missing */
typedef struct bunny_cfg_t {
    float ground_y;              /**< Height of the ground */
    float spawn_x;               /**< X at which carrots spawn */
    float despawn_x;             /**< X below which carrots are deleted and counted */
    float gravity;               /**< Vertical acceleration of the bunny */
    float jump_velocity;         /**< Vertical velocity of a jump and of a bounce */
    float hard_landing_velocity; /**< Downward speed above which a landing is hard */
    int32_t max_jumps;           /**< Jumps before the bunny has to land */
    float invuln_time;           /**< Seconds a hit bunny cannot be hit again */
    float dead_angle;            /**< Roll of the dead bunny */
    float dead_height;           /**< Height above the ground of the dead bunny's origin */
    float large_carrot_chance;   /**< Chance that a carrot is large once 5 carrots passed */
    int32_t max_lives;           /**< Lives of a run */
    float initial_speed;         /**< Speed of a new run */
} bunny_cfg_t;

/** Module state of a world, a private singleton */
typedef struct BunnyRuntime {
    ecs_entity_t playing; /**< Constant BunnyGameState.Playing */
    ecs_entity_t over;    /**< Constant BunnyGameState.Over */
    uint64_t rng;         /**< State of the carrot generator */
    uint64_t rng_seed;    /**< Seed the generator was started from */
    bool rng_started;     /**< The generator was started for the current run */
    bunny_cfg_t cfg;      /**< Constants of the scene, read at the start of every frame */
} BunnyRuntime;

extern ECS_COMPONENT_DECLARE(BunnyRuntime);

/** Read the constants cfg.* of the scene.
 *
 * @param world The world
 * @param cfg Receives the constants
 */
void bunny_cfg_read(
    const ecs_world_t *world,
    bunny_cfg_t *cfg);

/** Check whether the run is in a state.
 *
 * @param world The world
 * @param state The state
 * @return Whether (BunnyGameState, state) is on the singleton
 */
bool bunny_state_is(
    const ecs_world_t *world,
    BunnyGameState state);

/** Change the state of the run.
 *
 * @param world The world
 * @param state The new state
 */
void bunny_state_set(
    ecs_world_t *world,
    BunnyGameState state);

/** Draw the next number of the carrot generator, started from BunnyGame.seed for each run.
 *
 * @param world The world
 * @return A number from 0 up to but not including 1
 */
float bunny_random01(
    ecs_world_t *world);

/** Register the systems of the module.
 *
 * @param world The world
 */
void bunny_systems_import(
    ecs_world_t *world);

/** Register the async script functions jump and restart in the scope of the module.
 *
 * @param world The world
 * @param module The module entity
 */
void bunny_functions_import(
    ecs_world_t *world,
    ecs_entity_t module);

#endif

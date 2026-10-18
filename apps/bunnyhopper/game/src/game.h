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

/** Plays of effects a frame queues at most */
#define BUNNY_PLAYS_MAX (32)

/** A play of an effect template the systems of a frame queued */
typedef struct bunny_play_t {
    ecs_entity_t effect;  /**< The template */
    vec3 position;        /**< World position of the play */
} bunny_play_t;

/** Module state of a world, a private singleton */
typedef struct BunnyRuntime {
    ecs_entity_t playing; /**< Constant BunnyGameState.Playing */
    ecs_entity_t over;    /**< Constant BunnyGameState.Over */
    uint64_t rng;         /**< State of the carrot generator */
    uint64_t rng_seed;    /**< Seed the generator was started from */
    bool rng_started;     /**< The generator was started for the current run */
    bunny_cfg_t cfg;      /**< Constants of the scene, read at the start of every frame */
    bunny_play_t plays[BUNNY_PLAYS_MAX]; /**< Plays queued in this frame */
    int32_t play_count;   /**< Number of queued plays */
    uint32_t play_index;  /**< Plays so far, which offsets the seeds of the layers */
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

/** Queue a play of an effect template, which BunnyEffects plays at the end of the frame.
 *
 * @param world The world
 * @param effect The template, 0 for none
 * @param x X of the play
 * @param y Y of the play
 */
void bunny_effect_queue(
    ecs_world_t *world,
    ecs_entity_t effect,
    float x,
    float y);

/** Shift the velocities of an effect layer, made for a world that stands still, by a world that
 * scrolls toward -x at a speed: the cone of directions and the speed range around the velocity
 * the layer adds to (-speed, 0, 0) cover the velocities of the layer moved by it.
 *
 * The cone of the layer is taken as a center and a half size per axis: along its direction the
 * speed range and the shortening of the tilted draws, across it the widest draw.
 *
 * Moved by the world, the center gives the direction, the size along it the speed range and the
 * widest size across it the spread.
 *
 * @param src The layer of the template
 * @param speed The world speed, 0 or less for none
 * @param dst Receives the layer, src with its direction, spread and speed shifted
 */
void bunny_effect_shift(
    const ParticleEmitter *src,
    float speed,
    ParticleEmitter *dst);

/** Register BunnyEffects, which keeps the wind of the effects at the world speed and plays the
 * queued effects, after the systems of bunny_systems_import.
 *
 * @param world The world
 */
void bunny_effects_import(
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

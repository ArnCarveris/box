/**
 * @file game.h
 * @brief The bunnyhopper module: components, systems and script functions of the endless runner.
 *
 * The scene (apps/bunnyhopper/etc) holds the constants cfg.*, the assets and the BunnyGame
 * singleton; the systems of EcsOnUpdate move the bunny, the carrots and the scenery while the
 * state is (BunnyGameState, Playing) and stop the world when the last life is lost.
 *
 * Landings, hits and passing carrots play the effect templates of BunnyGame, which scroll with
 * the world.
 */

#ifndef BUNNYHOPPER_GAME_H
#define BUNNYHOPPER_GAME_H

#include <bunnyhopper.h>

#ifdef BUNNYHOPPER_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

/** Distance a new run scrolls before its first carrot spawns */
#define BUNNY_FIRST_SPAWN_DISTANCE (6.0f)

/** Carrots that pass the bunny before the run gets faster */
#define BUNNY_SPEED_UP_CARROTS (5)

/** Speed a run gains every BUNNY_SPEED_UP_CARROTS carrots */
#define BUNNY_SPEED_UP (2.0f)

/** State of a run, an exclusive relationship on its own singleton entity: scripts test it with
 * `$?[(BunnyGameState, Playing)]`. The module starts a world in Playing.
 */
ECS_ENUM(BunnyGameState, {
    BunnyGameStatePlaying, /**< The bunny runs, carrots spawn and the world scrolls */
    BunnyGameStateOver     /**< No lives left: the world stands still until restart() */
});

/** State of the game, a singleton the scene sets. The systems signal every change (OnSet), so
 * reactive scripts follow it.
 */
ECS_STRUCT(BunnyGame, {
    int32_t carrots;            /**< Carrots that passed the bunny in this run */
    int32_t max_lives;          /**< Lives of a new run, default 3 */
    float speed;                /**< World units per second the world scrolls, default 12,
                                 *   every 5th carrot adds 2 */
    float distance_since_spawn; /**< Distance the world scrolled since the last carrot */
    float next_spawn_distance;  /**< Distance at which the next carrot spawns, default 6 */
    uint64_t seed;              /**< Seed of the generator that sizes and spaces carrots */
    ecs_entity_t player;        /**< The bunny, with Bunny, BunnyPlayer, BunnyJump and
                                 *   BunnyLives */
    ecs_entity_t carrot_parent; /**< Parent of the spawned carrots, 0 spawns them at the root */
    ecs_entity_t small_carrot;  /**< Prefab of a small carrot, with BunnyCarrot */
    ecs_entity_t large_carrot;  /**< Prefab of a large carrot that needs a double jump */
    ecs_entity_t dust;          /**< Effect template of a landing after a jump, 0 for none */
    ecs_entity_t dust_high;     /**< Effect template of a hard landing, 0 plays dust */
    ecs_entity_t sparkle;       /**< Effect template of a carrot that passed, 0 for none */
    ecs_entity_t hit;           /**< Effect template of a carrot that hit the bunny, 0 for none */
    ecs_entity_t effect_wind;   /**< Entity with the ParticleForces the effect layers name, whose
                                 *   wind the systems keep at (-speed, 0, 0), 0 for none */
});

/** Look of the bunny: the scene's bunny template follows it */
ECS_STRUCT(Bunny, {
    float speed; /**< Speed of the run, which tilts the ears back */
    bool dead;   /**< The bunny lost its last life: crossed out eyes */
});

/** The bunny the player controls */
ECS_STRUCT(BunnyPlayer, {
    float run_phase; /**< Angle of the run cycle of the legs, advances on the ground */
});

/** Vertical motion of the bunny */
ECS_STRUCT(BunnyJump, {
    float v;         /**< Vertical velocity in units per second */
    int32_t jumps;   /**< Jumps since the bunny last stood on the ground */
});

/** Lives of the bunny */
ECS_STRUCT(BunnyLives, {
    int32_t lives_left; /**< Carrots the bunny may still touch; 0 ends the run */
});

/** A bunny that just touched a carrot and cannot be hit again yet */
ECS_STRUCT(BunnyInvulnerable, {
    float time_left; /**< Seconds until the bunny can be hit again */
});

/** A leg pivot of the bunny, below an entity with BunnyPlayer; its Rotation.z swings */
ECS_STRUCT(BunnyLeg, {
    float phase;      /**< Offset of the leg in the run cycle */
    float air_target; /**< Angle the leg reaches for while the bunny is in the air */
});

/** A carrot that scrolls toward the bunny; its origin is on the ground */
ECS_STRUCT(BunnyCarrot, {
    float half_width; /**< Half the width of the carrot */
    float height;     /**< Height of the carrot above the ground */
});

/** Scenery that scrolls with the world and wraps around */
ECS_STRUCT(BunnyScenery, {
    float parallax;  /**< Fraction of the world speed it scrolls at */
    float wrap_min;  /**< X below which it moves forward by wrap_span */
    float wrap_span; /**< Distance it moves forward when it wraps */
});

/** Register the bunnyhopper module.
 *
 * Registers the components, the systems of EcsOnUpdate and the async script functions
 * bunnyhopper.jump() and bunnyhopper.restart(), and starts the world in Playing.
 *
 * @param world The world
 */
void BunnyhopperImport(
    ecs_world_t *world);

#undef ECS_META_IMPL

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

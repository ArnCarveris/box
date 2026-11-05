/**
 * @file game.h
 * @brief The bunnyhopper module: components of the endless runner and its import function.
 *
 * The scene (apps/bunnyhopper/etc) holds the constants cfg.*, the assets, the BunnyGame
 * singleton and the state of the run; the systems of EcsOnUpdate play the game.
 */

#ifndef BUNNYHOPPER_GAME_H
#define BUNNYHOPPER_GAME_H

#include <bunnyhopper.h>

#ifdef BUNNYHOPPER_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

/** State of a run, an exclusive relationship on its own singleton entity: scripts test it with
 * `BunnyGameState?[(BunnyGameState, Over)]`. run.flecs starts every run in Playing.
 */
ECS_ENUM(BunnyGameState, {
    BunnyGameStatePlaying, /**< The bunny runs, carrots spawn and the world scrolls */
    BunnyGameStateOver     /**< No lives left: the world stands still until restart() */
});

/** State of the game, a singleton run.flecs sets at the start of every run. The systems signal
 * a passed carrot and a spawn (OnSet), so reactive scripts follow the count of carrots.
 */
ECS_STRUCT(BunnyGame, {
    int32_t carrots;            /**< Carrots that passed the bunny in this run */
    int32_t max_lives;          /**< Lives of a new run */
    float speed;                /**< World units per second the world scrolls, every 5th
                                 *   carrot adds 2 */
    float distance_since_spawn; /**< Distance the world scrolled since the last carrot */
    float next_spawn_distance;  /**< Distance at which the next carrot spawns */
    uint64_t seed;              /**< State of the generator that sizes and spaces carrots,
                                 *   which every draw advances */
    ecs_entity_t player;        /**< The bunny, with Bunny, BunnyPlayer, BunnyJump and
                                 *   BunnyLives */
    ecs_entity_t carrot_parent; /**< Parent of the spawned carrots, created by the script
                                 *   that restart() reloads */
    ecs_entity_t small_carrot;  /**< Prefab of a small carrot, with BunnyCarrot */
    ecs_entity_t large_carrot;  /**< Prefab of a large carrot that needs a double jump */
    ecs_entity_t dust;          /**< Effect template of a landing after a jump, 0 for none */
    ecs_entity_t dust_high;     /**< Effect template of a hard landing, 0 for none */
    ecs_entity_t sparkle;       /**< Effect template of a carrot that passed, 0 for none */
    ecs_entity_t hit;           /**< Effect template of a carrot that hit the bunny, 0 for none */
    ecs_entity_t effect_drift;  /**< Entity with the ParticleForces the effect layers name,
                                 *   whose drift the systems keep at (-speed, 0, 0) while
                                 *   playing and at 0 once the game is over */
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
 * bunnyhopper.jump() and bunnyhopper.restart().
 *
 * @param world The world
 */
void BunnyhopperImport(
    ecs_world_t *world);

#undef ECS_META_IMPL

#endif

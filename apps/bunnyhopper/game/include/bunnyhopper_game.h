/**
 * @file bunnyhopper_game.h
 * @brief The bunnyhopper module: components, systems and script functions of the endless runner.
 *
 * The scene (etc/bunnyhopper) holds the constants cfg.*, the assets and the BunnyGame singleton;
 * the systems of EcsOnUpdate move the bunny, the carrots and the scenery while the state is
 * (BunnyGameState, Playing) and stop the world when the last life is lost.
 *
 * Landings, hits and passing carrots play the effect templates of BunnyGame, which scroll with
 * the world.
 */

#ifndef BUNNYHOPPER_GAME_H
#define BUNNYHOPPER_GAME_H

/* This generated file contains includes for project dependencies */
#include "bunnyhopper_game/bake_config.h"

#ifdef BUNNYHOPPER_GAME_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
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

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif

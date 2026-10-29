/**
 * @file game.h
 * @brief The yeon module: components of the kite fight and the import of its systems.
 *
 * The scene (apps/yeon/etc) holds the constants cfg.*, the kites, the scenery and the state of a
 * run; the systems in yeon.c fly the kite, spawn rivals and lanterns and simulate the strings.
 */

#ifndef YEON_GAME_H
#define YEON_GAME_H

#include <yeon.h>

#ifdef YEON_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

/** State of the game, an exclusive relationship on its own singleton entity. Gameplay systems
 * match (YeonGameState, Playing); restart() reloads the run once the game is over.
 */
ECS_ENUM(YeonGameState, {
    YeonGameStatePlaying, /**< The kite flies and the village scrolls */
    YeonGameStateOver     /**< The last string was cut and the kite falls */
});

/** The state of a run, a singleton run.flecs sets and a restart resets */
ECS_STRUCT(YeonGame, {
    int32_t lanterns;             /**< Lanterns caught this run */
    int32_t max_lives;            /**< Strings the kite starts with, shown by the HUD */
    float speed;                  /**< Scroll speed of the village, raised by cfg.speedUp */
    float time;                   /**< Seconds played, drives the sway of kites and lanterns */
    float distance_since_rival;   /**< Distance scrolled since the last rival spawned */
    float next_rival_distance;    /**< Distance at which the next rival spawns */
    float distance_since_lantern; /**< Distance scrolled since the last lantern spawned */
    float next_lantern_distance;  /**< Distance at which the next lantern spawns */
    uint64_t seed;                /**< State of the splitmix64 generator of the spawns */
    ecs_entity_t player;          /**< The kite of the player */
    ecs_entity_t sky_parent;      /**< Parent of the spawned rivals and lanterns */
    ecs_entity_t rival_red;       /**< Prefab of the red rival kite */
    ecs_entity_t rival_blue;      /**< Prefab of the blue rival kite */
    ecs_entity_t rival_yellow;    /**< Prefab of the yellow rival kite */
    ecs_entity_t lantern;         /**< Prefab of the lantern */
    ecs_entity_t sparkle;         /**< Particle effect played where a lantern is caught */
    ecs_entity_t cut;             /**< Particle effect played where a string is cut */
    ecs_entity_t effect_drift;    /**< Particle forces whose drift follows the scroll speed */
});

/** The held actions the flight reads, a singleton input.flecs sets */
ECS_STRUCT(YeonControls, {
    ecs_entity_t up;   /**< Pull the string to climb */
    ecs_entity_t down; /**< Let out string to dive */
});

/** Points of the simulated string of a kite */
#define YEON_ROPE_POINTS (16)

/** The vertical motion of the player kite */
ECS_STRUCT(YeonKite, {
    float v;    /**< Vertical velocity */
    float spin; /**< Roll speed while the kite falls after the game is over */
});

/** The orientation of a kite, which its body parts follow through YeonBody */
ECS_STRUCT(YeonPose, {
    float pitch; /**< Rotation around X */
    float roll;  /**< Rotation around Z */
});

/** A body part of a kite that takes the YeonPose of its parent */
ECS_STRUCT(YeonBody, {
    float roll_scale; /**< Factor applied to the roll of the pose */
});

/** The string of a kite, a verlet chain from the kite to an anchor on the ground */
ECS_STRUCT(YeonRope, {
    float anchor_dx;  /**< X offset of the anchor from the kite */
    float anchor_y;   /**< Height of the anchor */
    float attach_y;   /**< Y offset of the point where the string attaches to the kite */
    float slack;      /**< Extra length of the string relative to the distance it spans */
    float gravity;    /**< Downward acceleration of the points */
    float segment;    /**< Rest length between two points, computed each frame */
    bool cut;         /**< The string is cut and no longer attached to the kite */
    bool ready;       /**< The points are initialized */
    float x[16];      /**< X of the points, from the kite to the anchor */
    float y[16];      /**< Y of the points */
    float prev_x[16]; /**< X of the points in the previous step */
    float prev_y[16]; /**< Y of the points in the previous step */
});

/** A drawn segment of the string of its parent kite */
ECS_STRUCT(YeonRopeSegment, {
    int32_t index; /**< Index of the first of the two points the segment spans */
});

/** The strings the player kite has left */
ECS_STRUCT(YeonLives, {
    int32_t lives_left; /**< Strings left, the game is over at 0 */
});

/** The player kite cannot be cut while this component is present */
ECS_STRUCT(YeonInvulnerable, {
    float time_left; /**< Seconds until the component is removed */
});

/** A rival kite that sways up and down while it drifts toward the player */
ECS_STRUCT(YeonRival, {
    float base_y;    /**< Height around which the kite sways */
    float amplitude; /**< Height of the sway */
    float frequency; /**< Speed of the sway */
    float phase;     /**< Phase of the sway */
    float drift;     /**< Factor applied to the scroll speed */
});

/** A lantern that bobs while it scrolls toward the player */
ECS_STRUCT(YeonLantern, {
    float base_y; /**< Height around which the lantern bobs */
    float phase;  /**< Phase of the bob */
});

/** Scenery that scrolls with the village and wraps around */
ECS_STRUCT(YeonScenery, {
    float parallax;  /**< Factor applied to the scroll speed */
    float wrap_min;  /**< X below which the scenery wraps */
    float wrap_span; /**< Distance added to X when the scenery wraps */
});

/** Import the yeon module: register its components, systems and the restart() function.
 *
 * @param world The world
 */
void YeonImport(
    ecs_world_t *world);

#undef ECS_META_IMPL

#endif

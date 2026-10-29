#ifndef YEON_GAME_H
#define YEON_GAME_H

#include <yeon.h>

#ifdef YEON_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

ECS_ENUM(YeonGameState, {
    YeonGameStatePlaying,
    YeonGameStateOver
});

ECS_STRUCT(YeonGame, {
    int32_t lanterns;
    int32_t max_lives;
    float speed;
    float time;
    float distance_since_rival;
    float next_rival_distance;
    float distance_since_lantern;
    float next_lantern_distance;
    uint64_t seed;
    ecs_entity_t player;
    ecs_entity_t sky_parent;
    ecs_entity_t rival_red;
    ecs_entity_t rival_blue;
    ecs_entity_t rival_yellow;
    ecs_entity_t lantern;
    ecs_entity_t sparkle;
    ecs_entity_t cut;
    ecs_entity_t effect_drift;
});

ECS_STRUCT(YeonControls, {
    ecs_entity_t up;
    ecs_entity_t down;
});

#define YEON_ROPE_POINTS (16)

ECS_STRUCT(YeonKite, {
    float v;
    float spin;
});

ECS_STRUCT(YeonPose, {
    float pitch;
    float roll;
});

ECS_STRUCT(YeonBody, {
    float roll_scale;
});

ECS_STRUCT(YeonRope, {
    float anchor_dx;
    float anchor_y;
    float attach_y;
    float slack;
    float gravity;
    float segment;
    bool cut;
    bool ready;
    float x[16];
    float y[16];
    float prev_x[16];
    float prev_y[16];
});

ECS_STRUCT(YeonRopeSegment, {
    int32_t index;
});

ECS_STRUCT(YeonLives, {
    int32_t lives_left;
});

ECS_STRUCT(YeonInvulnerable, {
    float time_left;
});

ECS_STRUCT(YeonRival, {
    float base_y;
    float amplitude;
    float frequency;
    float phase;
    float drift;
});

ECS_STRUCT(YeonLantern, {
    float base_y;
    float phase;
});

ECS_STRUCT(YeonScenery, {
    float parallax;
    float wrap_min;
    float wrap_span;
});

void YeonImport(
    ecs_world_t *world);

#undef ECS_META_IMPL

#endif

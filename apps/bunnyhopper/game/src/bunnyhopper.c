/**
 * @file bunnyhopper.c
 * @brief Registers the bunnyhopper module: components, state, scene constants and generator.
 */

#define BUNNYHOPPER_GAME_IMPL
#include "game.h"
#include <stddef.h>

ECS_COMPONENT_DECLARE(BunnyRuntime);

/** A constant of the scene read into bunny_cfg_t */
typedef struct bunny_cfg_var_t {
    const char *name; /**< Path of the exported constant */
    size_t offset;    /**< Offset of the member in bunny_cfg_t */
    bool integer;     /**< The member is an int32_t, else a float */
    float fallback;   /**< Value of the original game, used when the scene has none */
} bunny_cfg_var_t;

/** The constants cfg.* of the scene */
static const bunny_cfg_var_t bunny_cfg_vars[] = {
    { "cfg.groundY", offsetof(bunny_cfg_t, ground_y), false, 0.0f },
    { "cfg.spawnX", offsetof(bunny_cfg_t, spawn_x), false, 24.0f },
    { "cfg.despawnX", offsetof(bunny_cfg_t, despawn_x), false, -8.0f },
    { "cfg.gravity", offsetof(bunny_cfg_t, gravity), false, -32.0f },
    { "cfg.jumpVelocity", offsetof(bunny_cfg_t, jump_velocity), false, 13.0f },
    { "cfg.hardLandingVelocity", offsetof(bunny_cfg_t, hard_landing_velocity), false, 16.0f },
    { "cfg.maxJumps", offsetof(bunny_cfg_t, max_jumps), true, 2.0f },
    { "cfg.invulnTime", offsetof(bunny_cfg_t, invuln_time), false, 0.5f },
    { "cfg.deadAngle", offsetof(bunny_cfg_t, dead_angle), false, 3.14159265f },
    { "cfg.deadHeight", offsetof(bunny_cfg_t, dead_height), false, 1.5f },
    { "cfg.largeCarrotChance", offsetof(bunny_cfg_t, large_carrot_chance), false, 0.18f },
    { "cfg.maxLives", offsetof(bunny_cfg_t, max_lives), true, 3.0f },
    { "cfg.initialSpeed", offsetof(bunny_cfg_t, initial_speed), false, 12.0f }
};

/** Give a new BunnyGame the values of a new run.
 *
 * @param ptr The components
 * @param count Number of components
 * @param type_info Type of the component
 */
static void bunny_game_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *type_info)
{
    BunnyGame *g = ptr;
    (void)type_info;
    for (int32_t i = 0; i < count; i ++) {
        g[i] = (BunnyGame){
            .max_lives = 3,
            .speed = 12.0f,
            .next_spawn_distance = BUNNY_FIRST_SPAWN_DISTANCE
        };
    }
}

void bunny_cfg_read(
    const ecs_world_t *world,
    bunny_cfg_t *cfg)
{
    int32_t i, count = (int32_t)(sizeof(bunny_cfg_vars) / sizeof(bunny_cfg_vars[0]));
    for (i = 0; i < count; i ++) {
        const bunny_cfg_var_t *var = &bunny_cfg_vars[i];
        void *dst = ECS_OFFSET(cfg, var->offset);
        ecs_entity_t e = ecs_lookup(world, var->name);
        bool found = e && ecs_has(world, e, EcsScriptConstVar);
        if (var->integer) {
            *(int32_t*)dst = found
                ? ecs_const_var_get_t(world, var->name, ecs_i32_t)
                : (int32_t)var->fallback;
        } else {
            *(float*)dst = found
                ? ecs_const_var_get_t(world, var->name, ecs_f32_t)
                : var->fallback;
        }
    }
}

bool bunny_state_is(
    const ecs_world_t *world,
    BunnyGameState state)
{
    const BunnyRuntime *rt = ecs_singleton_get(world, BunnyRuntime);
    ecs_entity_t target = state == BunnyGameStateOver ? rt->over : rt->playing;

    return ecs_has_pair(world, ecs_id(BunnyGameState), ecs_id(BunnyGameState), target);
}

void bunny_state_set(
    ecs_world_t *world,
    BunnyGameState state)
{
    const BunnyRuntime *rt = ecs_singleton_get(world, BunnyRuntime);
    ecs_entity_t target = state == BunnyGameStateOver ? rt->over : rt->playing;
    ecs_add_pair(world, ecs_id(BunnyGameState), ecs_id(BunnyGameState), target);
}

/** Advance a splitmix64 generator.
 *
 * @param state The state of the generator
 * @return The next 64 bits
 */
static uint64_t bunny_splitmix(
    uint64_t *state)
{
    uint64_t z = (*state += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;

    return z ^ (z >> 31);
}

float bunny_random01(
    ecs_world_t *world)
{
    BunnyRuntime *rt = ecs_singleton_get_mut(world, BunnyRuntime);
    const BunnyGame *g = ecs_singleton_get(world, BunnyGame);
    uint64_t seed = g ? g->seed : 0;
    if (!rt->rng_started || rt->rng_seed != seed) {
        rt->rng = seed;
        rt->rng_seed = seed;
        rt->rng_started = true;
    }

    return (float)(bunny_splitmix(&rt->rng) >> 40) / (float)(1u << 24);
}

void BunnyhopperImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, Bunnyhopper);
    ECS_IMPORT(world, Box);

    ECS_META_COMPONENT(world, BunnyGameState);
    ECS_META_COMPONENT(world, BunnyGame);
    ECS_META_COMPONENT(world, Bunny);
    ECS_META_COMPONENT(world, BunnyPlayer);
    ECS_META_COMPONENT(world, BunnyJump);
    ECS_META_COMPONENT(world, BunnyLives);
    ECS_META_COMPONENT(world, BunnyInvulnerable);
    ECS_META_COMPONENT(world, BunnyLeg);
    ECS_META_COMPONENT(world, BunnyCarrot);
    ECS_META_COMPONENT(world, BunnyScenery);
    ECS_COMPONENT_DEFINE(world, BunnyRuntime);

    ecs_set_hooks(world, BunnyGame, { .ctor = bunny_game_ctor });
    ecs_add_id(world, ecs_id(BunnyGameState), EcsExclusive);
    ecs_add_id(world, ecs_id(BunnyGameState), EcsPairIsTag);

    ecs_add_pair(world, ecs_id(BunnyPlayer), EcsWith, ecs_id(Position));
    ecs_add_pair(world, ecs_id(BunnyPlayer), EcsWith, ecs_id(Rotation));
    ecs_add_pair(world, ecs_id(BunnyPlayer), EcsWith, ecs_id(BunnyJump));
    ecs_add_pair(world, ecs_id(BunnyLeg), EcsWith, ecs_id(Rotation));
    ecs_add_pair(world, ecs_id(BunnyPlayer), EcsWith, DynamicTransform);
    ecs_add_pair(world, ecs_id(BunnyCarrot), EcsWith, DynamicTransform);
    ecs_add_pair(world, ecs_id(BunnyScenery), EcsWith, DynamicTransform);

    BunnyRuntime rt = {
        .playing = ecs_lookup_child(world, ecs_id(BunnyGameState), "Playing"),
        .over = ecs_lookup_child(world, ecs_id(BunnyGameState), "Over")
    };
    bunny_cfg_read(world, &rt.cfg);
    ecs_singleton_set_ptr(world, BunnyRuntime, &rt);
    bunny_state_set(world, BunnyGameStatePlaying);

    bunny_systems_import(world);
    bunny_functions_import(world, ecs_id(Bunnyhopper));
}

/**
 * @file functions.c
 * @brief The async script functions bunnyhopper.jump() and bunnyhopper.restart().
 */

#include "game.h"

/** Complete the future of a call with a bool result.
 *
 * @param future The future
 * @param value The result
 */
static void bunny_resolve(
    ecs_script_future_t *future,
    bool value)
{
    ecs_script_future_resolve(future, &(ecs_value_t){
        .type = ecs_id(ecs_bool_t), .ptr = &value });
    ecs_script_future_release(future);
}

/** Jump, or double jump in the air, while playing and below cfg.maxJumps jumps.
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv No arguments
 * @param future Resolves with whether the bunny jumped
 */
static void bunny_jump(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    ecs_world_t *world = ctx->world;
    const BunnyGame *g = ecs_singleton_get(world, BunnyGame);
    BunnyJump *j = g && g->player ? ecs_get_mut(world, g->player, BunnyJump) : NULL;
    bool result = false;
    bunny_cfg_t cfg;
    (void)argc;
    (void)argv;

    bunny_cfg_read(world, &cfg);
    if (j && bunny_state_is(world, BunnyGameStatePlaying) && j->jumps < cfg.max_jumps) {
        j->v = cfg.jump_velocity;
        j->jumps ++;
        ecs_modified(world, g->player, BunnyJump);
        result = true;
    }

    bunny_resolve(future, result);
}

/** Delete every carrot of the world.
 *
 * @param world The world
 */
static void bunny_delete_carrots(
    ecs_world_t *world)
{
    ecs_query_t *q = ecs_query(world, {
        .terms = {{ .id = ecs_id(BunnyCarrot), .src.id = EcsSelf, .inout = EcsInOutNone }}
    });
    ecs_vec_t carrots;
    ecs_vec_init_t(NULL, &carrots, ecs_entity_t, 0);
    ecs_iter_t it = ecs_query_iter(world, q);
    while (ecs_query_next(&it)) {
        for (int32_t i = 0; i < it.count; i ++) {
            ecs_vec_append_t(NULL, &carrots, ecs_entity_t)[0] = it.entities[i];
        }
    }

    ecs_entity_t *e = ecs_vec_first_t(&carrots, ecs_entity_t);
    for (int32_t i = 0; i < ecs_vec_count(&carrots); i ++) {
        ecs_delete(world, e[i]);
    }

    ecs_vec_fini_t(NULL, &carrots, ecs_entity_t);
    ecs_query_fini(q);
}

/** Put the bunny back on the ground with the speed, lives and look of a new run.
 *
 * @param world The world
 * @param g The game
 * @param cfg The constants of the scene
 */
static void bunny_reset_player(
    ecs_world_t *world,
    const BunnyGame *g,
    const bunny_cfg_t *cfg)
{
    ecs_entity_t player = g->player;
    const Position *p = ecs_get(world, player, Position);
    if (p) {
        ecs_set(world, player, Position, { p->x, cfg->ground_y, p->z });
    }

    const Rotation *r = ecs_get(world, player, Rotation);
    if (r) {
        ecs_set(world, player, Rotation, { r->x, r->y, 0.0f });
    }

    ecs_set(world, player, Bunny, { .speed = cfg->initial_speed, .dead = false });
    ecs_set(world, player, BunnyJump, { 0 });
    ecs_set(world, player, BunnyPlayer, { 0 });
    ecs_set(world, player, BunnyLives, { g->max_lives });
    ecs_remove(world, player, BunnyInvulnerable);
}

/** Start a new run once the game is over.
 *
 * Deletes the carrots and restores the bunny, the counters, the speed, the spawn distances
 * and the generator, then returns to Playing. The scenery stays where it is.
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv No arguments
 * @param future Resolves with whether a new run started
 */
static void bunny_restart(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    ecs_world_t *world = ctx->world;
    BunnyGame *g = ecs_singleton_get_mut(world, BunnyGame);
    bool result = false;
    bunny_cfg_t cfg;
    (void)argc;
    (void)argv;
    if (!g || !bunny_state_is(world, BunnyGameStateOver)) {
        goto done;
    }

    bunny_cfg_read(world, &cfg);
    bunny_delete_carrots(world);
    if (g->player) {
        bunny_reset_player(world, g, &cfg);
    }

    g->carrots = 0;
    g->speed = cfg.initial_speed;
    g->distance_since_spawn = 0.0f;
    g->next_spawn_distance = BUNNY_FIRST_SPAWN_DISTANCE;
    ecs_singleton_modified(world, BunnyGame);
    ecs_singleton_get_mut(world, BunnyRuntime)->rng_started = false;
    bunny_state_set(world, BunnyGameStatePlaying);
    result = true;
done:
    bunny_resolve(future, result);
}

void bunny_functions_import(
    ecs_world_t *world,
    ecs_entity_t module)
{
    ecs_async_function(world, {
        .name = "jump",
        .parent = module,
        .return_type = ecs_id(ecs_bool_t),
        .callback = bunny_jump
    });

    ecs_async_function(world, {
        .name = "restart",
        .parent = module,
        .return_type = ecs_id(ecs_bool_t),
        .callback = bunny_restart
    });
}

/**
 * @file particle/functions.c
 * @brief Script functions that drive the playback of an emitter.
 *
 * The functions live under the module, so a script that uses box calls them as
 * particle.start(fountain). Each one is the component write a scene would make
 * by hand: they add a ParticlePlayback to an entity that has an emitter and set
 * the state or the reset trigger on it, and they answer whether the entity is
 * an emitter at all, so a script can branch on a name it was handed.
 */

#include "particle_internal.h"

/** Write a state, a reset trigger or both onto the playback of an entity.
 *
 * @param world The world
 * @param e The entity
 * @param state The state to put it in, below zero to leave the state alone
 * @param reset Raise the reset trigger of the emitter
 * @return true when the entity holds an emitter
 */
static bool box_particle_control(
    ecs_world_t *world,
    ecs_entity_t e,
    int32_t state,
    bool reset)
{
    bool result = false;
    if (!e || !ecs_is_alive(world, e) || !ecs_has(world, e, ParticleEmitter)) {
        goto done;
    }

    ParticlePlayback *playback = ecs_ensure(world, e, ParticlePlayback);
    if (state >= 0) {
        playback->state = state;
    }

    if (reset) {
        playback->reset = true;
    }

    ecs_modified(world, e, ParticlePlayback);
    result = true;
done:
    return result;
}

/** particle.start: play an emitter, from the top when it was stopped.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments: the emitter entity
 * @param result Receives whether the entity holds an emitter
 */
static void box_particle_fn_start(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;

    *(ecs_bool_t*)result->ptr = box_particle_control(ctx->world,
        *(ecs_entity_t*)argv[0].ptr, ParticlePlaying, false);
}

/** particle.pause: freeze the pool of an emitter where it is.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments: the emitter entity
 * @param result Receives whether the entity holds an emitter
 */
static void box_particle_fn_pause(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;

    *(ecs_bool_t*)result->ptr = box_particle_control(ctx->world,
        *(ecs_entity_t*)argv[0].ptr, ParticlePaused, false);
}

/** particle.stop: end the emission and let the live particles finish.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments: the emitter entity
 * @param result Receives whether the entity holds an emitter
 */
static void box_particle_fn_stop(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;

    *(ecs_bool_t*)result->ptr = box_particle_control(ctx->world,
        *(ecs_entity_t*)argv[0].ptr, ParticleStopped, false);
}

/** particle.reset: empty the pool of an emitter and rewind its clock.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments: the emitter entity
 * @param result Receives whether the entity holds an emitter
 */
static void box_particle_fn_reset(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;

    *(ecs_bool_t*)result->ptr = box_particle_control(ctx->world,
        *(ecs_entity_t*)argv[0].ptr, -1, true);
}

/** particle.time: the seconds an emitter has been playing.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments: the emitter entity
 * @param result Receives the clock of the emitter, 0 when it has no playback
 */
static void box_particle_fn_time(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_entity_t e = *(ecs_entity_t*)argv[0].ptr;
    const ParticlePlayback *playback = e && ecs_is_alive(ctx->world, e) ?
        ecs_get(ctx->world, e, ParticlePlayback) : NULL;

    *(ecs_f64_t*)result->ptr = playback ? playback->time : 0.0;
}

/** Register one playback function that takes an emitter and answers a boolean.
 *
 * @param world The world
 * @param module The module the function is registered under
 * @param name Name of the function
 * @param callback The implementation
 */
static void box_particle_function(
    ecs_world_t *world,
    ecs_entity_t module,
    const char *name,
    ecs_function_callback_t callback)
{
    ecs_function(world, {
        .name = name,
        .parent = module,
        .return_type = ecs_id(ecs_bool_t),
        .params = {
            { .name = "emitter", .type = ecs_id(ecs_entity_t) }
        },
        .callback = callback
    });
}

void box_particle_functions_init(
    ecs_world_t *world,
    ecs_entity_t module)
{
    box_particle_function(world, module, "start", box_particle_fn_start);
    box_particle_function(world, module, "pause", box_particle_fn_pause);
    box_particle_function(world, module, "stop", box_particle_fn_stop);
    box_particle_function(world, module, "reset", box_particle_fn_reset);

    ecs_function(world, {
        .name = "time",
        .parent = module,
        .return_type = ecs_id(ecs_f64_t),
        .params = {
            { .name = "emitter", .type = ecs_id(ecs_entity_t) }
        },
        .callback = box_particle_fn_time
    });
}

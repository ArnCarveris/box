/**
 * @file flow.c
 * @brief The game flow: states, the clock, the blink signals, the toast, the seed of a run and
 * the script functions oreelse.seed(), oreelse.cancel() and oreelse.restart().
 *
 * The game boots into Title, where any key or a left or right click starts it. ESC calls
 * cancel(), one step back of a chain that ends in pausing; R calls restart(), which once the
 * run is won or lost reloads the scripts of the run and plays again.
 */

#include "game.h"
#include <math.h>
#include <stdarg.h>

/** Increment of the splitmix64 generators, the golden ratio in 64 bits */
#define ORE_GOLDEN (0x9e3779b97f4a7c15ull)

/** One step of the cancel chain: back out of one thing.
 *
 * @param world The world
 * @return Whether the step applied, which ends the chain
 */
typedef bool (*ore_cancel_step_t)(
    ecs_world_t *world);

bool ore_state_is(
    const ecs_world_t *world,
    OreGameState state)
{
    ecs_entity_t s = ecs_constant_to_entity(world, OreGameState, state);

    return s && ecs_has_pair(world, ecs_id(OreGameState), ecs_id(OreGameState), s);
}

void ore_state_set(
    ecs_world_t *world,
    OreGameState state)
{
    ecs_add_pair(world, ecs_id(OreGameState), ecs_id(OreGameState),
        ecs_constant_to_entity(world, OreGameState, state));
}

ecs_id_t ore_state_pair(
    const ecs_world_t *world,
    OreGameState state)
{
    return ecs_pair(ecs_id(OreGameState), ecs_constant_to_entity(world, OreGameState, state));
}

void ore_resolve(
    ecs_script_future_t *future,
    bool value)
{
    ecs_script_future_resolve(future, &(ecs_value_t){
        .type = ecs_id(ecs_bool_t), .ptr = &value });
    ecs_script_future_release(future);
}

/** Mix a number into a well spread one, the output function of splitmix64.
 *
 * @param x The number
 * @return The mixed number
 */
static uint64_t ore_mix(
    uint64_t x)
{
    uint64_t z = x + ORE_GOLDEN;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;

    return z ^ (z >> 31);
}

uint64_t ore_seed_get(
    const ecs_world_t *world,
    int32_t stream)
{
    const OreSeed *s = ecs_singleton_get(world, OreSeed);
    uint64_t seed = s ? s->seed : 0;
    uint64_t run = s ? (uint64_t)s->run : 0;
    uint64_t result = ore_mix(ore_mix(ore_mix(seed) + run) + (uint64_t)stream);

    return result ? result : 1;
}

float ore_random01(
    ecs_world_t *world)
{
    OreRandom *r = ecs_singleton_get_mut(world, OreRandom);
    uint64_t z = 0;
    if (r) {
        z = ore_mix(r->state);
        r->state += ORE_GOLDEN;
    }

    return (float)(z >> 40) / (float)(1u << 24);
}

void ore_toast(
    ecs_world_t *world,
    bool warn,
    const char *fmt,
    ...)
{
    OreToast *toast = ecs_singleton_get_mut(world, OreToast);
    va_list args;
    if (toast) {
        va_start(args, fmt);
        ecs_os_free(toast->text);
        toast->text = flecs_vasprintf(fmt, args);
        va_end(args);
        toast->warn = warn;
        toast->age = 0;
        ecs_singleton_modified(world, OreToast);
    }
}

/** Count the play time.
 *
 * @param it Iterator with the OreClock singleton, while playing
 */
static void OreClockUpdate(
    ecs_iter_t *it)
{
    OreClock *clock = ecs_field(it, OreClock, 0);
    if (it->delta_time > 0) {
        clock->elapsed += it->delta_time;
        ecs_singleton_modified(it->stage, OreClock);
    }
}

/** Compute the blink signals from the play time and signal those that flipped.
 *
 * @param it Iterator with OreClock and OreBlink, while playing
 */
static void OreBlinkUpdate(
    ecs_iter_t *it)
{
    const OreClock *clock = ecs_field(it, OreClock, 0);
    OreBlink *blink = ecs_field(it, OreBlink, 1);
    float t = clock->elapsed;
    float lamp = t * ORE_CFG(it->world, "lampRate");
    OreBlink next = {
        .fast = sinf(t * ORE_CFG(it->world, "blinkFast")) > 0.0f,
        .slow = sinf(t * ORE_CFG(it->world, "blinkSlow")) > 0.0f,
        .lamp = lamp - floorf(lamp) < 0.5f
    };

    if (next.fast != blink->fast || next.slow != blink->slow || next.lamp != blink->lamp) {
        *blink = next;
        ecs_singleton_modified(it->stage, OreBlink);
    }
}

/** Age the toast and clear it once it was shown cfg.warnToast (a warning) or cfg.infoToast
 * seconds of play.
 *
 * @param it Iterator with the OreToast singleton, while playing
 */
static void OreToastUpdate(
    ecs_iter_t *it)
{
    OreToast *toast = ecs_field(it, OreToast, 0);
    float duration = 0;
    if (toast->text && toast->text[0]) {
        duration = ecs_const_var_get_t(it->world,
            toast->warn ? "cfg.warnToast" : "cfg.infoToast", ecs_f32_t);
        toast->age += it->delta_time;
        if (toast->age >= duration) {
            ecs_os_free(toast->text);
            toast->text = ecs_os_strdup("");
            ecs_singleton_modified(it->stage, OreToast);
        }
    }
}

/** Start the game on the title screen when any key or the left or right mouse button went
 * down. It runs after the gameplay systems and the actions, so the key does nothing else in
 * the frame it starts the game.
 *
 * @param it Iterator with the Input singleton, on the title screen
 */
static void OreTitleStart(
    ecs_iter_t *it)
{
    const Input *input = ecs_field(it, Input, 0);
    bool pressed = false;
    int32_t k;
    for (k = KeyNone + 1; k < BOX_KEY_COUNT && !pressed; k ++) {
        pressed = k != KeyMouseMiddle && input->keys[k].pressed;
    }

    if (pressed) {
        ore_state_set(it->stage, OreGameStatePlaying);
    }
}

/** Cancel step: resume a paused game.
 *
 * @param world The world
 * @return Whether the game was paused
 */
static bool ore_cancel_resume(
    ecs_world_t *world)
{
    bool result = ore_state_is(world, OreGameStatePaused);
    if (result) {
        ore_state_set(world, OreGameStatePlaying);
    }

    return result;
}

/** Cancel step, the last one: pause the game while it is playing.
 *
 * @param world The world
 * @return Whether the game was playing
 */
static bool ore_cancel_pause(
    ecs_world_t *world)
{
    bool result = ore_state_is(world, OreGameStatePlaying);
    if (result) {
        ore_state_set(world, OreGameStatePaused);
    }

    return result;
}

/** The cancel chain in order; the first step that applies ends it: resume, cancel the
 * placement drag in flight, close the inventory screen, clear the selection of a building,
 * pause.
 */
static const ore_cancel_step_t ore_cancel_steps[] = {
    ore_cancel_resume,
    ore_placement_cancel_drag,
    ore_inventory_screen_cancel,
    ore_placement_deselect,
    ore_cancel_pause
};

/** Back out of one thing: the first step of the cancel chain that applies.
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv No arguments
 * @param future Resolves with whether a step applied
 */
static void ore_cancel(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    int32_t i, count = (int32_t)(sizeof(ore_cancel_steps) / sizeof(ore_cancel_steps[0]));
    bool result = false;
    (void)argc;
    (void)argv;

    for (i = 0; i < count && !result; i ++) {
        result = ore_cancel_steps[i](ctx->world);
    }

    ore_resolve(future, result);
}

/** Check whether a list of entities holds one.
 *
 * @param list The entities
 * @param e The entity
 * @return Whether e is in the list
 */
static bool ore_list_has(
    ecs_vec_t *list,
    ecs_entity_t e)
{
    int32_t i, count = ecs_vec_count(list);
    bool result = false;
    for (i = 0; i < count && !result; i ++) {
        result = *ecs_vec_get_t(list, ecs_entity_t, i) == e;
    }

    return result;
}

/** Reload the scripts that created the entities with OreRunScript: each reload deletes the
 * entities of its script and creates them again for the run of the current seed.
 *
 * @param world The world
 * @return 0 when every script reloaded, nonzero when one failed
 */
static int ore_run_reload(
    ecs_world_t *world)
{
    ecs_vec_t scripts;
    ecs_iter_t it = ecs_each_id(world, OreRunScript);
    int32_t i;
    int result = 0;
    ecs_vec_init_t(NULL, &scripts, ecs_entity_t, 0);
    while (ecs_each_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            ecs_entity_t s = ecs_get_target(world, it.entities[i], ecs_id(EcsScript), 0);
            if (s && !ore_list_has(&scripts, s)) {
                ecs_vec_append_t(NULL, &scripts, ecs_entity_t)[0] = s;
            }
        }
    }

    for (i = 0; i < ecs_vec_count(&scripts); i ++) {
        if (ecs_script_reload(world, *ecs_vec_get_t(&scripts, ecs_entity_t, i), NULL)) {
            result = -1;
        }
    }

    ecs_vec_fini_t(NULL, &scripts, ecs_entity_t);

    return result;
}

/** Start a new run once the game is won or lost: count the run in OreSeed, so it draws from a
 * seed of its own, reload the scripts of the run and play. The camera keeps its zoom level.
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv No arguments
 * @param future Resolves with whether a new run started
 */
static void ore_restart(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    ecs_world_t *world = ctx->world;
    OreSeed *seed = ecs_singleton_get_mut(world, OreSeed);
    bool result = seed &&
        (ore_state_is(world, OreGameStateWon) || ore_state_is(world, OreGameStateLost));
    (void)argc;
    (void)argv;

    if (result) {
        seed->run ++;
        ecs_singleton_modified(world, OreSeed);
        result = !ore_run_reload(world);
        ore_map_purge(world);
    }

    if (result) {
        ore_state_set(world, OreGameStatePlaying);
    }

    ore_resolve(future, result);
}

/** The seed of a stream of the current run, for the Rng of the scripts.
 *
 * @param ctx Call context
 * @param argc Number of arguments
 * @param argv The stream, an i32
 * @param result Receives the seed, a u64
 */
static void ore_seed_function(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    *(uint64_t*)result->ptr = ore_seed_get(ctx->world, *(int32_t*)argv[0].ptr);
}

void ore_flow_import(
    ecs_world_t *world)
{
    ecs_entity_t module = ecs_get_scope(world);
    ecs_entity_t state = ecs_id(OreGameState);
    ecs_id_t playing = ore_state_pair(world, OreGameStatePlaying);
    ecs_id_t title = ore_state_pair(world, OreGameStateTitle);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreClockUpdate" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreClock), .src.id = ecs_id(OreClock), .inout = EcsInOut },
            { .id = playing, .src.id = state }
        },
        .callback = OreClockUpdate
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreBlinkUpdate" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreClock), .src.id = ecs_id(OreClock), .inout = EcsIn },
            { .id = ecs_id(OreBlink), .src.id = ecs_id(OreBlink), .inout = EcsInOut },
            { .id = playing, .src.id = state }
        },
        .callback = OreBlinkUpdate
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreToastUpdate" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(OreToast), .src.id = ecs_id(OreToast), .inout = EcsInOut },
            { .id = playing, .src.id = state }
        },
        .callback = OreToastUpdate
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "OreTitleStart" }),
        .phase = EcsPostUpdate,
        .query.terms = {
            { .id = ecs_id(Input), .src.id = ecs_id(Input), .inout = EcsIn },
            { .id = title, .src.id = state }
        },
        .callback = OreTitleStart
    });

    ecs_function(world, {
        .name = "seed",
        .parent = module,
        .return_type = ecs_id(ecs_u64_t),
        .params = {{ .name = "stream", .type = ecs_id(ecs_i32_t) }},
        .callback = ore_seed_function
    });

    ecs_async_function(world, {
        .name = "cancel",
        .parent = module,
        .return_type = ecs_id(ecs_bool_t),
        .callback = ore_cancel
    });

    ecs_async_function(world, {
        .name = "restart",
        .parent = module,
        .return_type = ecs_id(ecs_bool_t),
        .callback = ore_restart
    });
}

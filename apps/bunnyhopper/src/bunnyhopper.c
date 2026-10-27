/**
 * @file bunnyhopper.c
 * @brief The bunnyhopper module: components, systems and script functions of the endless runner.
 *
 * The systems of EcsOnUpdate run in the order they are registered; those that only run while
 * playing match (BunnyGameState, Playing). restart() reloads run.flecs to start a new run.
 */

#define BUNNYHOPPER_IMPL
#include "game.h"
#include <math.h>

/** Carrots that pass the bunny before the run gets faster */
#define BUNNY_SPEED_UP_CARROTS (5)

/** Speed a run gains every BUNNY_SPEED_UP_CARROTS carrots */
#define BUNNY_SPEED_UP (2.0f)

/** Back of the bunny's collision box from its x, the box runs from x - 0.55 to x + 0.8 */
#define BUNNY_BOX_BACK (0.55f)

/** Front of the bunny's collision box from its x */
#define BUNNY_BOX_FRONT (0.8f)

/** Bottom of the bunny's collision box above its y */
#define BUNNY_BOX_BOTTOM (0.05f)

/** Top of the bunny's collision box above its y */
#define BUNNY_BOX_TOP (1.3f)

/** Fraction of a carrot's width that collides */
#define BUNNY_CARROT_HIT_WIDTH (0.85f)

/** Fraction of a carrot's height that collides */
#define BUNNY_CARROT_HIT_HEIGHT (0.9f)

/** Read the float constant cfg.<name> of the scene (etc/config.flecs) */
#define BUNNY_CFG(world, name) ecs_const_var_get_t(world, "cfg." name, ecs_f32_t)

/** Check whether the run is in a state.
 *
 * @param world The world
 * @param state Name of the constant of BunnyGameState, "Playing" or "Over"
 * @return Whether (BunnyGameState, state) is on the singleton
 */
static bool bunny_state_is(
    const ecs_world_t *world,
    const char *state)
{
    ecs_entity_t s = ecs_lookup_child(world, ecs_id(BunnyGameState), state);

    return ecs_has_pair(world, ecs_id(BunnyGameState), ecs_id(BunnyGameState), s);
}

/** Draw the next number of the carrot generator, a splitmix64 step of BunnyGame.seed.
 *
 * @param g The game, whose seed run.flecs sets at the start of every run
 * @return A number from 0 up to but not including 1
 */
static float bunny_random01(
    BunnyGame *g)
{
    uint64_t z = (g->seed += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;

    return (float)((z ^ (z >> 31)) >> 40) / (float)(1u << 24);
}

/** Move the bunny with gravity, land it on the ground and tilt it with its velocity. A landing
 * after a jump while playing plays the dust at its feet, the high dust when it was hard; once
 * the game is over the bunny lies on its back.
 *
 * @param it Iterator over the players, with BunnyGame and whether the run is playing
 */
static void BunnyPhysics(
    ecs_iter_t *it)
{
    Position *p = ecs_field(it, Position, 0);
    Rotation *r = ecs_field(it, Rotation, 1);
    BunnyJump *j = ecs_field(it, BunnyJump, 2);
    BunnyPlayer *pl = ecs_field(it, BunnyPlayer, 3);
    const BunnyGame *g = ecs_field(it, BunnyGame, 4);
    bool playing = ecs_field_is_set(it, 5);
    float ground_y = BUNNY_CFG(it->world, "groundY");
    float ground = ground_y + (playing ? 0.0f : BUNNY_CFG(it->world, "deadHeight"));
    float gravity = BUNNY_CFG(it->world, "gravity");
    float hard = BUNNY_CFG(it->world, "hardLandingVelocity");
    float dt = it->delta_time;

    for (int32_t i = 0; i < it->count; i ++) {
        j[i].v += gravity * dt;
        p[i].y += j[i].v * dt;
        if (p[i].y <= ground) {
            if (playing && j[i].jumps > 0) {
                ecs_entity_t dust = -j[i].v > hard ? g->dust_high : g->dust;
                box_particle_burst_at(it->world, dust, (vec3){ p[i].x, ground_y, 0.0f }, 0.0f);
            }

            p[i].y = ground;
            j[i].v = 0.0f;
            j[i].jumps = 0;
            pl[i].run_phase += playing ? dt * (4.0f + g->speed * 0.9f) : 0.0f;
        }

        r[i].z = playing
            ? glm_clamp(j[i].v * 0.025f, -0.35f, 0.35f)
            : BUNNY_CFG(it->world, "deadAngle");
    }
}

/** Swing the legs of the bunny toward the run cycle, the jump pose or the rest pose.
 *
 * @param it Iterator over the legs, with the player and its jump of their ancestor
 */
static void BunnyLegs(
    ecs_iter_t *it)
{
    const BunnyLeg *leg = ecs_field(it, BunnyLeg, 0);
    Rotation *r = ecs_field(it, Rotation, 1);
    const BunnyPlayer *pl = ecs_field(it, BunnyPlayer, 2);
    const BunnyJump *j = ecs_field(it, BunnyJump, 3);
    bool playing = ecs_field_is_set(it, 4);
    float alpha = fminf(it->delta_time * 14.0f, 1.0f);

    for (int32_t i = 0; i < it->count; i ++) {
        float target = 0.0f;
        if (playing && j->jumps > 0) {
            target = leg[i].air_target;
        } else if (playing) {
            target = sinf(pl->run_phase + leg[i].phase) * 0.55f;
        }

        r[i].z += (target - r[i].z) * alpha;
    }
}

/** Count down the invulnerability of a hit bunny and remove it at 0.
 *
 * @param it Iterator over invulnerable entities
 */
static void BunnyInvulnerability(
    ecs_iter_t *it)
{
    BunnyInvulnerable *inv = ecs_field(it, BunnyInvulnerable, 0);

    for (int32_t i = 0; i < it->count; i ++) {
        inv[i].time_left -= it->delta_time;
        if (inv[i].time_left <= 0.0f) {
            ecs_remove(it->stage, it->entities[i], BunnyInvulnerable);
        }
    }
}

/** Scroll the carrots toward the bunny, delete and count those that passed it, playing the
 * sparkle where each one passed, and speed the run up every 5th carrot.
 *
 * @param it Iterator over the carrots, with BunnyGame
 */
static void BunnyCarrotScroll(
    ecs_iter_t *it)
{
    Position *p = ecs_field(it, Position, 0);
    BunnyGame *g = ecs_field(it, BunnyGame, 2);
    float despawn_x = BUNNY_CFG(it->world, "despawnX");
    float step = g->speed * it->delta_time;

    for (int32_t i = 0; i < it->count; i ++) {
        p[i].x -= step;
        if (p[i].x < despawn_x) {
            box_particle_burst_at(it->world, g->sparkle, (vec3){ p[i].x, p[i].y, 0.0f }, 0.0f);
            ecs_delete(it->stage, it->entities[i]);
            g->carrots ++;
            if (!(g->carrots % BUNNY_SPEED_UP_CARROTS)) {
                g->speed += BUNNY_SPEED_UP;
                ecs_set(it->stage, g->player, Bunny, { .speed = g->speed });
            }

            ecs_singleton_modified(it->stage, BunnyGame);
        }
    }
}

/** Scroll the scenery with its parallax and wrap it around.
 *
 * @param it Iterator over the scenery, with BunnyGame
 */
static void BunnySceneryScroll(
    ecs_iter_t *it)
{
    Position *p = ecs_field(it, Position, 0);
    const BunnyScenery *s = ecs_field(it, BunnyScenery, 1);
    const BunnyGame *g = ecs_field(it, BunnyGame, 2);

    for (int32_t i = 0; i < it->count; i ++) {
        p[i].x -= g->speed * s[i].parallax * it->delta_time;
        if (p[i].x < s[i].wrap_min) {
            p[i].x += s[i].wrap_span;
        }
    }
}

/** Spawn a carrot at cfg.spawnX, as a dynamic child of the carrot parent, every time the world
 * scrolled the spawn distance. Once 5 carrots passed, some are large.
 *
 * @param it Iterator with BunnyGame
 */
static void BunnySpawn(
    ecs_iter_t *it)
{
    BunnyGame *g = ecs_field(it, BunnyGame, 0);
    g->distance_since_spawn += g->speed * it->delta_time;
    if (g->distance_since_spawn >= g->next_spawn_distance) {
        bool large = g->carrots >= BUNNY_SPEED_UP_CARROTS &&
            bunny_random01(g) < BUNNY_CFG(it->world, "largeCarrotChance");
        ecs_entity_t e = ecs_new_w_pair(it->stage, EcsChildOf, g->carrot_parent);
        ecs_add(it->stage, e, DynamicTransform);
        ecs_add_pair(it->stage, e, EcsIsA, large ? g->large_carrot : g->small_carrot);
        ecs_set(it->stage, e, Position, {
            BUNNY_CFG(it->world, "spawnX"), BUNNY_CFG(it->world, "groundY"), 0.0f });
        g->distance_since_spawn = 0.0f;
        g->next_spawn_distance = g->speed * (1.0f + bunny_random01(g));
    }

    ecs_singleton_modified(it->stage, BunnyGame);
}

/** Hit the bunny: play the hit effect at it, take a life, then end the run or bounce and
 * protect the bunny.
 *
 * @param world The stage the changes go through
 * @param g The game
 */
static void bunny_hit(
    ecs_world_t *world,
    const BunnyGame *g)
{
    const Position *p = ecs_get(world, g->player, Position);
    BunnyLives *lives = ecs_get_mut(world, g->player, BunnyLives);
    box_particle_burst_at(world, g->hit, (vec3){ p->x, p->y, 0.0f }, 0.0f);
    lives->lives_left --;
    ecs_modified(world, g->player, BunnyLives);
    if (lives->lives_left <= 0) {
        ecs_add_pair(world, ecs_id(BunnyGameState), ecs_id(BunnyGameState),
            ecs_lookup_child(world, ecs_id(BunnyGameState), "Over"));
        ecs_set(world, g->player, Bunny, { .speed = 0.0f, .dead = true });
    } else {
        ecs_set(world, g->player, BunnyJump, { BUNNY_CFG(world, "jumpVelocity"), 1 });
        ecs_set(world, g->player, BunnyInvulnerable, { BUNNY_CFG(world, "invulnTime") });
    }
}

/** Check whether the bunny at a position overlaps a carrot.
 *
 * @param bunny Position of the bunny
 * @param carrot Position of the carrot
 * @param c The carrot
 * @return Whether the boxes overlap
 */
static bool bunny_overlaps(
    const Position *bunny,
    const Position *carrot,
    const BunnyCarrot *c)
{
    float half = c->half_width * BUNNY_CARROT_HIT_WIDTH;
    bool x = bunny->x + BUNNY_BOX_FRONT > carrot->x - half &&
        bunny->x - BUNNY_BOX_BACK < carrot->x + half;
    bool y = bunny->y + BUNNY_BOX_TOP > carrot->y &&
        bunny->y + BUNNY_BOX_BOTTOM < carrot->y + c->height * BUNNY_CARROT_HIT_HEIGHT;

    return x && y;
}

/** Test the bunny, unless it is invulnerable, against the carrots and hit it with the first one
 * it touches.
 *
 * @param it Iterator over the carrots
 */
static void BunnyCollision(
    ecs_iter_t *it)
{
    const BunnyGame *g = ecs_singleton_get(it->world, BunnyGame);
    const Position *bp = ecs_get(it->world, g->player, Position);
    bool done = ecs_has(it->world, g->player, BunnyInvulnerable);

    while (!done && ecs_iter_next(it)) {
        const Position *p = ecs_field(it, Position, 0);
        const BunnyCarrot *c = ecs_field(it, BunnyCarrot, 1);
        for (int32_t i = 0; i < it->count && !done; i ++) {
            done = bunny_overlaps(bp, &p[i], ecs_field_is_self(it, 1) ? &c[i] : c);
        }

        if (done) {
            bunny_hit(it->stage, g);
        }
    }

    if (done) {
        ecs_iter_fini(it);
    }
}

/** Move the particles of the effects with the scrolling track: keep the drift of
 * BunnyGame.effect_drift at (-speed, 0, 0) while playing and at 0 once the game is over.
 *
 * @param it Iterator with BunnyGame and whether the run is playing
 */
static void BunnyDrift(
    ecs_iter_t *it)
{
    const BunnyGame *g = ecs_field(it, BunnyGame, 0);
    float drift = ecs_field_is_set(it, 1) ? -g->speed : 0.0f;
    const ParticleForces *f = ecs_get(it->world, g->effect_drift, ParticleForces);
    if (f->drift[0] != drift) {
        ParticleForces forces = *f;
        forces.drift[0] = drift;
        ecs_set_ptr(it->stage, g->effect_drift, ParticleForces, &forces);
    }
}

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
    ecs_entity_t player = ecs_singleton_get(world, BunnyGame)->player;
    BunnyJump *j = ecs_get_mut(world, player, BunnyJump);
    bool result = bunny_state_is(world, "Playing") &&
        j->jumps < ecs_const_var_get_t(world, "cfg.maxJumps", ecs_i32_t);
    (void)argc;
    (void)argv;

    if (result) {
        j->v = BUNNY_CFG(world, "jumpVelocity");
        j->jumps ++;
        ecs_modified(world, player, BunnyJump);
    }

    bunny_resolve(future, result);
}

/** Start a new run once the game is over by reloading the script that created the carrot
 * parent (run.flecs): the reload deletes the carrots with their parent and sets the bunny,
 * BunnyGame and the state of a new run. The scenery stays where it is.
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
    ecs_entity_t parent = ecs_singleton_get(world, BunnyGame)->carrot_parent;
    bool result = bunny_state_is(world, "Over");
    (void)argc;
    (void)argv;

    if (result) {
        ecs_entity_t script = ecs_get_target(world, parent, ecs_id(EcsScript), 0);
        result = script && !ecs_script_reload(world, script, NULL);
    }

    bunny_resolve(future, result);
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

    ecs_add_id(world, ecs_id(BunnyGameState), EcsExclusive);
    ecs_add_id(world, ecs_id(BunnyGameState), EcsPairIsTag);
    ecs_add_pair(world, ecs_id(BunnyPlayer), EcsWith, ecs_id(Position));
    ecs_add_pair(world, ecs_id(BunnyPlayer), EcsWith, ecs_id(Rotation));
    ecs_add_pair(world, ecs_id(BunnyPlayer), EcsWith, ecs_id(BunnyJump));
    ecs_add_pair(world, ecs_id(BunnyLeg), EcsWith, ecs_id(Rotation));
    ecs_add_pair(world, ecs_id(BunnyPlayer), EcsWith, DynamicTransform);
    ecs_add_pair(world, ecs_id(BunnyCarrot), EcsWith, DynamicTransform);
    ecs_add_pair(world, ecs_id(BunnyScenery), EcsWith, DynamicTransform);

    ecs_entity_t state = ecs_id(BunnyGameState), game = ecs_id(BunnyGame);
    ecs_id_t playing = ecs_pair(state, ecs_lookup_child(world, state, "Playing"));

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BunnyPhysics" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(Position) }, { .id = ecs_id(Rotation) },
            { .id = ecs_id(BunnyJump) }, { .id = ecs_id(BunnyPlayer) },
            { .id = game, .src.id = game, .inout = EcsIn },
            { .id = playing, .src.id = state, .oper = EcsOptional }
        },
        .callback = BunnyPhysics
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BunnyLegs" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(BunnyLeg), .inout = EcsIn }, { .id = ecs_id(Rotation) },
            { .id = ecs_id(BunnyPlayer), .src.id = EcsUp, .trav = EcsChildOf, .inout = EcsIn },
            { .id = ecs_id(BunnyJump), .src.id = EcsUp, .trav = EcsChildOf, .inout = EcsIn },
            { .id = playing, .src.id = state, .oper = EcsOptional }
        },
        .callback = BunnyLegs
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BunnyInvulnerability" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(BunnyInvulnerable) }, { .id = playing, .src.id = state }
        },
        .callback = BunnyInvulnerability
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BunnyCarrotScroll" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(Position), .src.id = EcsSelf },
            { .id = ecs_id(BunnyCarrot), .inout = EcsIn },
            { .id = game, .src.id = game, .inout = EcsInOut }, { .id = playing, .src.id = state }
        },
        .callback = BunnyCarrotScroll
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BunnySceneryScroll" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(Position), .src.id = EcsSelf },
            { .id = ecs_id(BunnyScenery), .src.id = EcsSelf, .inout = EcsIn },
            { .id = game, .src.id = game, .inout = EcsIn }, { .id = playing, .src.id = state }
        },
        .callback = BunnySceneryScroll
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BunnySpawn" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = game, .src.id = game, .inout = EcsInOut }, { .id = playing, .src.id = state }
        },
        .callback = BunnySpawn
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BunnyCollision" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(Position), .src.id = EcsSelf, .inout = EcsIn },
            { .id = ecs_id(BunnyCarrot), .inout = EcsIn }, { .id = playing, .src.id = state }
        },
        .run = BunnyCollision
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BunnyDrift" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = game, .src.id = game, .inout = EcsIn },
            { .id = playing, .src.id = state, .oper = EcsOptional }
        },
        .callback = BunnyDrift
    });

    ecs_async_function(world, {
        .name = "jump",
        .parent = ecs_id(Bunnyhopper),
        .return_type = ecs_id(ecs_bool_t),
        .callback = bunny_jump
    });

    ecs_async_function(world, {
        .name = "restart",
        .parent = ecs_id(Bunnyhopper),
        .return_type = ecs_id(ecs_bool_t),
        .callback = bunny_restart
    });
}

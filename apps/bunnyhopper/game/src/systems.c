/**
 * @file systems.c
 * @brief Systems of the bunnyhopper module, all in EcsOnUpdate in the order they are registered.
 */

#include "game.h"
#include <math.h>

/** Half of the bunny's collision box toward -x, the box runs from x - 0.55 to x + 0.8 */
#define BUNNY_BOX_BACK (0.55f)

/** Front of the bunny's collision box from its x */
#define BUNNY_BOX_FRONT (0.8f)

/** Bottom of the bunny's collision box above its y */
#define BUNNY_BOX_BOTTOM (0.05f)

/** Top of the bunny's collision box above its y */
#define BUNNY_BOX_TOP (1.3f)

/** Fraction of a carrot's size that collides */
#define BUNNY_CARROT_HIT_WIDTH (0.85f)

/** Fraction of a carrot's height that collides */
#define BUNNY_CARROT_HIT_HEIGHT (0.9f)

/** Clamp a value to a range.
 *
 * @param v The value
 * @param lo Lower bound
 * @param hi Upper bound
 * @return The clamped value
 */
static float bunny_clamp(
    float v,
    float lo,
    float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/** Get the constants of the scene read at the start of the frame.
 *
 * @param world The world
 * @return The constants
 */
static const bunny_cfg_t* bunny_cfg(
    const ecs_world_t *world)
{
    return &ecs_singleton_get(world, BunnyRuntime)->cfg;
}

/** Get the world speed of the run.
 *
 * @param world The world
 * @return BunnyGame.speed, 0 without a BunnyGame
 */
static float bunny_speed(
    const ecs_world_t *world)
{
    const BunnyGame *g = ecs_singleton_get(world, BunnyGame);

    return g ? g->speed : 0.0f;
}

/** Read the constants of the scene for the systems of this frame.
 *
 * @param it Iterator of the system, without entities
 */
static void BunnyConfig(
    ecs_iter_t *it)
{
    BunnyRuntime *rt = ecs_singleton_get_mut(it->world, BunnyRuntime);
    bunny_cfg_read(it->world, &rt->cfg);
}

/** Move the bunny with gravity, land it on the ground and tilt it with its velocity.
 *
 * @param it Iterator over the players
 */
static void BunnyPhysics(
    ecs_iter_t *it)
{
    Position *p = ecs_field(it, Position, 0);
    Rotation *r = ecs_field(it, Rotation, 1);
    BunnyJump *j = ecs_field(it, BunnyJump, 2);
    BunnyPlayer *pl = ecs_field(it, BunnyPlayer, 3);
    const bunny_cfg_t *cfg = bunny_cfg(it->world);
    bool playing = bunny_state_is(it->world, BunnyGameStatePlaying);
    float speed = bunny_speed(it->world);
    float dt = it->delta_time;
    float ground = cfg->ground_y + (playing ? 0.0f : cfg->dead_height);

    for (int32_t i = 0; i < it->count; i ++) {
        bool grounded = false;
        j[i].v += cfg->gravity * dt;
        p[i].y += j[i].v * dt;
        if (p[i].y <= ground) {
            p[i].y = ground;
            j[i].v = 0.0f;
            j[i].jumps = 0;
            grounded = true;
        }

        if (playing && grounded) {
            pl[i].run_phase += dt * (4.0f + speed * 0.9f);
        }

        r[i].z = playing ? bunny_clamp(j[i].v * 0.025f, -0.35f, 0.35f) : cfg->dead_angle;
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
    bool playing = bunny_state_is(it->world, BunnyGameStatePlaying);
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
    if (!bunny_state_is(it->world, BunnyGameStatePlaying)) {
        goto done;
    }

    for (int32_t i = 0; i < it->count; i ++) {
        inv[i].time_left -= it->delta_time;
        if (inv[i].time_left <= 0.0f) {
            ecs_remove(it->stage, it->entities[i], BunnyInvulnerable);
        }
    }

done:
    return;
}

/** Count a carrot that passed the bunny and speed the run up every 5th carrot.
 *
 * @param world The stage the changes go through
 * @param g The game
 */
static void bunny_carrot_passed(
    ecs_world_t *world,
    BunnyGame *g)
{
    g->carrots ++;
    if (g->carrots % BUNNY_SPEED_UP_CARROTS) {
        goto done;
    }

    g->speed += BUNNY_SPEED_UP;
    const Bunny *b = g->player ? ecs_get(world, g->player, Bunny) : NULL;
    if (b) {
        ecs_set(world, g->player, Bunny, { .speed = g->speed, .dead = b->dead });
    }

done:
    return;
}

/** Scroll the carrots toward the bunny, delete and count those that passed it.
 *
 * @param it Iterator over the carrots
 */
static void BunnyCarrotScroll(
    ecs_iter_t *it)
{
    Position *p = ecs_field(it, Position, 0);
    BunnyGame *g = ecs_singleton_get_mut(it->world, BunnyGame);
    if (!g || !bunny_state_is(it->world, BunnyGameStatePlaying)) {
        goto done;
    }

    const bunny_cfg_t *cfg = bunny_cfg(it->world);
    float step = g->speed * it->delta_time;
    bool passed = false;
    for (int32_t i = 0; i < it->count; i ++) {
        p[i].x -= step;
        if (p[i].x < cfg->despawn_x) {
            ecs_delete(it->stage, it->entities[i]);
            bunny_carrot_passed(it->stage, g);
            passed = true;
        }
    }

    if (passed) {
        ecs_singleton_modified(it->stage, BunnyGame);
    }

done:
    return;
}

/** Scroll the scenery with its parallax and wrap it around.
 *
 * @param it Iterator over the scenery
 */
static void BunnySceneryScroll(
    ecs_iter_t *it)
{
    Position *p = ecs_field(it, Position, 0);
    const BunnyScenery *s = ecs_field(it, BunnyScenery, 1);
    if (!bunny_state_is(it->world, BunnyGameStatePlaying)) {
        goto done;
    }

    float speed = bunny_speed(it->world);
    for (int32_t i = 0; i < it->count; i ++) {
        p[i].x -= speed * s[i].parallax * it->delta_time;
        if (p[i].x < s[i].wrap_min) {
            p[i].x += s[i].wrap_span;
        }
    }

done:
    return;
}

/** Spawn a carrot at the spawn point as a dynamic child of the carrot parent.
 *
 * @param world The stage the changes go through
 * @param g The game
 * @param prefab The carrot prefab
 * @param cfg The constants of the scene
 */
static void bunny_spawn(
    ecs_world_t *world,
    const BunnyGame *g,
    ecs_entity_t prefab,
    const bunny_cfg_t *cfg)
{
    ecs_entity_t e = ecs_new(world);
    if (g->carrot_parent) {
        ecs_add_pair(world, e, EcsChildOf, g->carrot_parent);
    }

    ecs_add(world, e, DynamicTransform);
    if (prefab) {
        ecs_add_pair(world, e, EcsIsA, prefab);
    }

    ecs_set(world, e, Position, { cfg->spawn_x, cfg->ground_y, 0.0f });
}

/** Spawn a carrot every time the world scrolled the spawn distance.
 *
 * @param it Iterator of the system, without entities
 */
static void BunnySpawn(
    ecs_iter_t *it)
{
    BunnyGame *g = ecs_singleton_get_mut(it->world, BunnyGame);
    if (!g || !bunny_state_is(it->world, BunnyGameStatePlaying)) {
        goto done;
    }

    const bunny_cfg_t *cfg = bunny_cfg(it->world);
    g->distance_since_spawn += g->speed * it->delta_time;
    if (g->distance_since_spawn >= g->next_spawn_distance) {
        bool large = g->carrots >= BUNNY_SPEED_UP_CARROTS &&
            bunny_random01(it->world) < cfg->large_carrot_chance;
        bunny_spawn(it->stage, g, large ? g->large_carrot : g->small_carrot, cfg);
        g->distance_since_spawn = 0.0f;
        g->next_spawn_distance = g->speed * (1.0f + bunny_random01(it->world));
    }

    ecs_singleton_modified(it->stage, BunnyGame);
done:
    return;
}

/** Hit the bunny: take a life, then end the run or bounce and protect the bunny.
 *
 * @param world The stage the changes go through
 * @param player The bunny
 * @param cfg The constants of the scene
 */
static void bunny_hit(
    ecs_world_t *world,
    ecs_entity_t player,
    const bunny_cfg_t *cfg)
{
    BunnyLives *lives = ecs_get_mut(world, player, BunnyLives);
    lives->lives_left --;
    ecs_modified(world, player, BunnyLives);
    if (lives->lives_left <= 0) {
        bunny_state_set(world, BunnyGameStateOver);
        ecs_set(world, player, Bunny, { .speed = 0.0f, .dead = true });
        goto done;
    }

    BunnyJump *j = ecs_get_mut(world, player, BunnyJump);
    if (j) {
        j->v = cfg->jump_velocity;
        j->jumps = 1;
        ecs_modified(world, player, BunnyJump);
    }

    ecs_set(world, player, BunnyInvulnerable, { cfg->invuln_time });
done:
    return;
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

/** Test the bunny against the carrots and hit it with the first one it touches.
 *
 * @param it Iterator over the carrots
 */
static void BunnyCollision(
    ecs_iter_t *it)
{
    const BunnyGame *g = ecs_singleton_get(it->world, BunnyGame);
    ecs_entity_t player = g ? g->player : 0;
    const Position *bp = player ? ecs_get(it->world, player, Position) : NULL;
    bool hit = false;
    if (!bp || !ecs_has(it->world, player, BunnyLives) ||
        ecs_has(it->world, player, BunnyInvulnerable) ||
        !bunny_state_is(it->world, BunnyGameStatePlaying))
    {
        ecs_iter_fini(it);
        goto done;
    }

    while (!hit && ecs_iter_next(it)) {
        const Position *p = ecs_field(it, Position, 0);
        const BunnyCarrot *c = ecs_field(it, BunnyCarrot, 1);
        for (int32_t i = 0; i < it->count && !hit; i ++) {
            hit = bunny_overlaps(bp, &p[i], ecs_field_is_self(it, 1) ? &c[i] : c);
        }

        if (hit) {
            bunny_hit(it->stage, player, bunny_cfg(it->world));
            ecs_iter_fini(it);
        }
    }

done:
    return;
}

void bunny_systems_import(
    ecs_world_t *world)
{
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BunnyConfig" }),
        .phase = EcsOnUpdate,
        .callback = BunnyConfig
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BunnyPhysics" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(Position), .inout = EcsInOut },
            { .id = ecs_id(Rotation), .inout = EcsInOut },
            { .id = ecs_id(BunnyJump), .inout = EcsInOut },
            { .id = ecs_id(BunnyPlayer), .inout = EcsInOut }
        },
        .callback = BunnyPhysics
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BunnyLegs" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(BunnyLeg), .inout = EcsIn },
            { .id = ecs_id(Rotation), .inout = EcsInOut },
            { .id = ecs_id(BunnyPlayer), .src.id = EcsUp, .trav = EcsChildOf, .inout = EcsIn },
            { .id = ecs_id(BunnyJump), .src.id = EcsUp, .trav = EcsChildOf, .inout = EcsIn }
        },
        .callback = BunnyLegs
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BunnyInvulnerability" }),
        .phase = EcsOnUpdate,
        .query.terms = {{ .id = ecs_id(BunnyInvulnerable), .inout = EcsInOut }},
        .callback = BunnyInvulnerability
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BunnyCarrotScroll" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(Position), .src.id = EcsSelf, .inout = EcsInOut },
            { .id = ecs_id(BunnyCarrot), .inout = EcsIn }
        },
        .callback = BunnyCarrotScroll
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BunnySceneryScroll" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(Position), .src.id = EcsSelf, .inout = EcsInOut },
            { .id = ecs_id(BunnyScenery), .src.id = EcsSelf, .inout = EcsIn }
        },
        .callback = BunnySceneryScroll
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BunnySpawn" }),
        .phase = EcsOnUpdate,
        .callback = BunnySpawn
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BunnyCollision" }),
        .phase = EcsOnUpdate,
        .query.terms = {
            { .id = ecs_id(Position), .src.id = EcsSelf, .inout = EcsIn },
            { .id = ecs_id(BunnyCarrot), .inout = EcsIn }
        },
        .run = BunnyCollision
    });
}

/**
 * @file Effects.c
 * @brief The particle effects of the bunny hopper scene: which template a landing, a hit and a
 * passing carrot play, the effect counters, the wind of the scrolling world and the velocities
 * of the plays, which keep the particles moving with the track.
 *
 * The scene loads without its Surface, so no GPU backend starts; the extract still accounts the
 * particles of every play, so plays retire and ParticleStats counts them. The motion of the
 * particles is checked with the C twin of the particle simulation.
 */

#include <bunnyhopper_game.h>
#include <bake_test.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "simulate.h"

/** Path of the game scene in the repository */
#define GAME_SCENE "etc/bunnyhopper/bunnyhopper.flecs"

/** Path of this file in the repository, which locates the repository root */
#define GAME_SOURCE "test/cpu/bunnyhopper/src/"

/** Fixed delta of a frame */
#define GAME_DT (1.0f / 60.0f)

/** Tolerance of positions */
#define GAME_EPS (1e-3f)

/** The constants of the scene */
#define GROUND_Y (0.0f)
#define DESPAWN_X (-8.0f)
#define HARD_LANDING_VELOCITY (16.0f)
#define INITIAL_SPEED (12.0f)

/** Frames after which every play of the scene retired: the longest lifetime is 0.75 seconds */
#define RETIRE_FRAMES (60)

/** Get the path of a file of the repository.
 *
 * @param file Path of the file in the repository
 * @return The absolute path, owned by the caller
 */
static char* repo_path(
    const char *file)
{
    const char *source = strstr(__FILE__, GAME_SOURCE);
    int32_t root = source ? (int32_t)(source - __FILE__) : 0;

    return flecs_asprintf("%.*s%s", root, __FILE__, file);
}

/** Read a file into a string.
 *
 * @param path Path of the file
 * @return The contents, owned by the caller, NULL when the file cannot be read
 */
static char* file_read(
    const char *path)
{
    char *result = NULL;
    FILE *f = fopen(path, "rb");
    if (!f) {
        goto done;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    result = ecs_os_malloc((ecs_size_t)size + 1);
    size_t read = fread(result, 1, (size_t)size, f);
    result[read] = '\0';
    fclose(f);
done:
    return result;
}

/** Blank every line of a text that contains a string, keeping the line breaks.
 *
 * @param text The text, edited in place
 * @param match The string a line to blank contains
 */
static void lines_blank(
    char *text,
    const char *match)
{
    char *at = strstr(text, match);
    while (at) {
        char *start = at;
        while (start > text && start[-1] != '\n') {
            start --;
        }

        char *end = strchr(at, '\n');
        if (!end) {
            end = start + strlen(start);
        }

        ecs_os_memset(start, ' ', (ecs_size_t)(end - start));
        at = strstr(end, match);
    }
}

/** Step the world by frames of the fixed delta.
 *
 * @param world The world
 * @param frames Number of frames
 */
static void step(
    ecs_world_t *world,
    int32_t frames)
{
    for (int32_t i = 0; i < frames; i ++) {
        ecs_progress(world, GAME_DT);
    }
}

/** Load the game scene without its Surface as boxrun does, and run its first frame.
 *
 * @return The world
 */
static ecs_world_t* game_new(void)
{
    setenv("BOX_TEST", "1", 1);
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ECS_IMPORT(world, Bunnyhopper);

    char *path = repo_path(GAME_SCENE);
    char *text = file_read(path);
    test_assert(text != NULL);
    lines_blank(text, "Surface:");

    ecs_entity_t script = ecs_new_from_path_w_sep(world, 0, path, "/", "/");
    EcsScript *s = ecs_ensure(world, script, EcsScript);
    ecs_os_strset(&s->filename, path);
    test_assert(ecs_script_update(world, script, 0, text) == 0);
    ecs_os_free(text);
    ecs_os_free(path);

    step(world, 1);

    return world;
}

/** Get the game singleton.
 *
 * @param world The world
 * @return The game
 */
static const BunnyGame* game(
    ecs_world_t *world)
{
    const BunnyGame *g = ecs_singleton_get(world, BunnyGame);
    test_assert(g != NULL);

    return g;
}

/** Get the bunny.
 *
 * @param world The world
 * @return The player entity
 */
static ecs_entity_t player(
    ecs_world_t *world)
{
    ecs_entity_t e = game(world)->player;
    test_assert(e != 0);

    return e;
}

/** Look up an entity of the scene.
 *
 * @param world The world
 * @param name Its path
 * @return The entity
 */
static ecs_entity_t lookup(
    ecs_world_t *world,
    const char *name)
{
    ecs_entity_t e = ecs_lookup(world, name);
    test_assert(e != 0);

    return e;
}

/** Check the state of the run.
 *
 * @param world The world
 * @param name Name of the state constant
 * @return Whether (BunnyGameState, name) is on the singleton
 */
static bool state_is(
    ecs_world_t *world,
    const char *name)
{
    ecs_entity_t c = ecs_lookup_child(world, ecs_id(BunnyGameState), name);
    test_assert(c != 0);

    return ecs_has_pair(world, ecs_id(BunnyGameState), ecs_id(BunnyGameState), c);
}

/** Run a command in a new terminal session and check that it succeeds.
 *
 * @param world The world
 * @param line The command line
 */
static void command(
    ecs_world_t *world,
    const char *line)
{
    ecs_entity_t session = box_terminal_new(world);
    test_assert(session != 0);
    bool error = true;
    char *out = box_terminal_run(world, session, line, &error);
    test_bool(error, false);
    ecs_os_free(out);
}

/** Move the bunny out of the way of the carrots and stop new carrots, so a run never ends and
 * only the carrots of a test pass the bunny.
 *
 * @param world The world
 */
static void clear_path(
    ecs_world_t *world)
{
    ecs_entity_t p = player(world);
    const Position *pos = ecs_get(world, p, Position);
    ecs_set(world, p, Position, { -100.0f, pos->y, pos->z });
    BunnyGame *g = ecs_singleton_get_mut(world, BunnyGame);
    g->next_spawn_distance = 1e9f;
    ecs_singleton_modified(world, BunnyGame);
}

/** Change the world speed.
 *
 * @param world The world
 * @param speed The speed
 */
static void speed_set(
    ecs_world_t *world,
    float speed)
{
    BunnyGame *g = ecs_singleton_get_mut(world, BunnyGame);
    g->speed = speed;
    ecs_singleton_modified(world, BunnyGame);
}

/** Place a small carrot on the ground at an x.
 *
 * @param world The world
 * @param x The x
 * @return The carrot
 */
static ecs_entity_t carrot_new(
    ecs_world_t *world,
    float x)
{
    ecs_entity_t e = ecs_new_w_pair(world, EcsIsA, lookup(world, "SmallCarrot"));
    ecs_add_pair(world, e, EcsChildOf, game(world)->carrot_parent);
    ecs_set(world, e, Position, { x, GROUND_Y, 0.0f });

    return e;
}

/** Count the plays of a template that play, and get the last one.
 *
 * @param world The world
 * @param effect The template
 * @param root Receives the root of a play, 0 for none, NULL to skip
 * @return Enabled instance roots whose ParticleEffect names the template
 */
static int32_t plays(
    ecs_world_t *world,
    ecs_entity_t effect,
    ecs_entity_t *root)
{
    int32_t result = 0;
    if (root) {
        *root = 0;
    }

    ecs_query_t *q = ecs_query(world, {
        .terms = {{ .id = ecs_id(ParticleEffect), .inout = EcsIn }}
    });
    ecs_iter_t it = ecs_query_iter(world, q);
    while (ecs_query_next(&it)) {
        const ParticleEffect *e = ecs_field(&it, ParticleEffect, 0);
        for (int32_t i = 0; i < it.count; i ++) {
            if (e[i].effect == effect) {
                result ++;
                if (root) {
                    *root = it.entities[i];
                }
            }
        }
    }

    ecs_query_fini(q);

    return result;
}

/** Count the plays of every effect template of the game.
 *
 * @param world The world
 * @return The plays of dust, high dust, hit and sparkle
 */
static int32_t plays_all(
    ecs_world_t *world)
{
    const BunnyGame *g = game(world);

    return plays(world, g->dust, NULL) + plays(world, g->dust_high, NULL) +
        plays(world, g->hit, NULL) + plays(world, g->sparkle, NULL);
}

/** Get the effects ParticleStats counts.
 *
 * @param world The world
 * @return ParticleStats.effects
 */
static int32_t stats_effects(
    ecs_world_t *world)
{
    const ParticleStats *s = ecs_singleton_get(world, ParticleStats);
    test_assert(s != NULL);

    return s->effects;
}

/** Jump and step until the bunny is back on the ground.
 *
 * @param world The world
 * @param twice Whether to jump again at the top of the jump
 * @return Frames until the landing
 */
static int32_t jump_and_land(
    ecs_world_t *world,
    bool twice)
{
    ecs_entity_t p = player(world);
    int32_t frames = 0;
    command(world, "key.press Space");
    step(world, 1);
    test_int(ecs_get(world, p, BunnyJump)->jumps, 1);
    if (twice) {
        while (ecs_get(world, p, BunnyJump)->v > 0 && frames < 600) {
            step(world, 1);
            frames ++;
        }

        command(world, "key.press Space");
        step(world, 1);
        test_int(ecs_get(world, p, BunnyJump)->jumps, 2);
    }

    while (ecs_get(world, p, BunnyJump)->jumps > 0 && frames < 600) {
        test_int(plays_all(world), 0);
        step(world, 1);
        frames ++;
    }

    test_assert(frames < 600);
    test_flt(ecs_get(world, p, Position)->y, GROUND_Y);

    return frames;
}

/** End the run: the bunny on its last life touches a carrot.
 *
 * @param world The world
 */
static void game_over(
    ecs_world_t *world)
{
    ecs_entity_t p = player(world);
    ecs_set(world, p, BunnyLives, { 1 });
    carrot_new(world, ecs_get(world, p, Position)->x);
    step(world, 1);
    test_assert(state_is(world, "Over"));
}

/** Get the layer of an instance or a template.
 *
 * @param world The world
 * @param root The root
 * @return The emitter of its particles layer
 */
static const ParticleEmitter* layer(
    ecs_world_t *world,
    ecs_entity_t root)
{
    ecs_entity_t e = ecs_lookup_child(world, root, "particles");
    test_assert(e != 0);
    const ParticleEmitter *result = ecs_get(world, e, ParticleEmitter);
    test_assert(result != NULL);

    return result;
}

/** Get the sum of the forces on the particles of a layer.
 *
 * @param world The world
 * @param root The root of the instance
 * @param out Receives the forces
 */
static void layer_forces(
    ecs_world_t *world,
    ecs_entity_t root,
    box_particle_forces_t *out)
{
    ecs_entity_t e = ecs_lookup_child(world, root, "particles");
    const ParticleEmitter *emitter = layer(world, root);
    const ParticleForces *own = ecs_get(world, e, ParticleForces);
    const ParticleForces *named = ecs_get(world, emitter->forces, ParticleForces);
    test_assert(own != NULL);
    test_assert(named != NULL);
    test_assert(ecs_singleton_get(world, ParticleForces) == NULL);

    ecs_os_memset_t(out, 0, box_particle_forces_t);
    for (int32_t i = 0; i < 3; i ++) {
        out->acceleration[i] = emitter->acceleration[i] + own->gravity[i] + named->gravity[i];
        out->wind[i] = own->wind[i] + named->wind[i];
    }

    out->drag = own->drag + named->drag;
    out->scale = 1.0f;
}

/** Particles of a layer the drift of a burst is averaged over */
#define DRIFT_SAMPLES (256)

/** Simulate the particles of a burst of a layer and get how far they moved along x from the
 * track below their spawn point, which scrolls at a speed.
 *
 * @param emitter The layer
 * @param count Particles of the burst
 * @param forces The forces on them
 * @param speed The world speed the track scrolls at
 * @param seconds Seconds to simulate
 * @return The mean x of the particles relative to the track
 */
static float drift(
    const ParticleEmitter *emitter,
    int32_t count,
    const box_particle_forces_t *forces,
    float speed,
    float seconds)
{
    int32_t frames = (int32_t)(seconds / GAME_DT + 0.5f);
    float sum = 0.0f;
    for (int32_t i = 0; i < count; i ++) {
        box_particle_sample_t s;
        box_particle_sample(emitter, (uint32_t)i, &s);
        float p[3] = {0, 0, 0}, v[3];
        for (int32_t c = 0; c < 3; c ++) {
            v[c] = s.direction[c] * s.speed;
        }

        for (int32_t f = 0; f < frames; f ++) {
            box_particle_integrate(p, v, forces, GAME_DT);
        }

        sum += p[0] + speed * (float)frames * GAME_DT;
    }

    return sum / (float)count;
}

void Effects_templates(void) {
    // bunnyhopper/effects
    ecs_world_t *world = game_new();
    const BunnyGame *g = game(world);

    test_uint(g->dust, lookup(world, "LandingDust"));
    test_uint(g->dust_high, lookup(world, "HardLandingDust"));
    test_uint(g->hit, lookup(world, "HitBurst"));
    test_uint(g->sparkle, lookup(world, "CarrotSparkle"));
    test_uint(g->effect_wind, lookup(world, "effect_wind"));

    ecs_entity_t templates[] = { g->dust, g->dust_high, g->hit, g->sparkle };
    int32_t counts[] = { 11, 23, 28, 18 };
    for (int32_t i = 0; i < 4; i ++) {
        test_assert(ecs_has_id(world, templates[i], EcsPrefab));
        ecs_entity_t e = ecs_lookup_child(world, templates[i], "particles");
        test_assert(e != 0);
        const ParticleEmitter *emitter = layer(world, templates[i]);
        test_flt(emitter->rate, 0.0f);
        test_assert(emitter->seed != 0);
        test_assert(emitter->max >= counts[i]);
        test_uint(emitter->forces, g->effect_wind);
        const ParticleBurst *burst = ecs_get(world, e, ParticleBurst);
        test_assert(burst != NULL);
        test_int(burst->count, counts[i]);
        test_flt(burst->at, 0.0f);
        test_int(burst->repeat, 0);
        const ParticleCurves *curves = ecs_get(world, e, ParticleCurves);
        test_assert(curves != NULL);
        test_assert(curves->opacity.start < curves->opacity.early);
        test_flt(curves->opacity.end, 0.0f);
    }

    test_int(layer(world, g->dust)->color.r, 225);
    test_int(layer(world, g->dust)->color.g, 210);
    test_int(layer(world, g->dust)->color.b, 185);
    test_int(layer(world, g->hit)->color.r, 255);
    test_int(layer(world, g->hit)->color.g, 0);
    test_int(layer(world, g->sparkle)->color.g, 220);
    test_assert(layer(world, g->dust_high)->speed.max > layer(world, g->dust)->speed.max);

    const ParticleAppearance *look = ecs_get(world,
        ecs_lookup_child(world, g->sparkle, "particles"), ParticleAppearance);
    test_assert(look != NULL);
    test_int(look->blend, ParticleAdditive);

    const ParticleSettings *settings = ecs_singleton_get(world, ParticleSettings);
    test_assert(settings != NULL);
    test_assert(settings->effect_max >= 4);
    test_assert(settings->effect_total >= 4 * settings->effect_max);
    test_int(settings->effect_overflow, EffectRecycle);

    const ParticleForces *wind = ecs_get(world, g->effect_wind, ParticleForces);
    test_assert(wind != NULL);
    test_flt(wind->wind[0], -INITIAL_SPEED);
    test_flt(wind->gravity[1], 0.0f);
    test_flt(wind->drag, 0.0f);

    test_int(plays_all(world), 0);
    test_int(stats_effects(world), 0);

    ecs_fini(world);
}

void Effects_landing_plays_dust(void) {
    // bunnyhopper/effects
    ecs_world_t *world = game_new();
    clear_path(world);
    const BunnyGame *g = game(world);

    jump_and_land(world, false);
    ecs_entity_t root;
    test_int(plays(world, g->dust, &root), 1);
    test_int(plays(world, g->dust_high, NULL), 0);
    test_int(plays_all(world), 1);
    const Position *p = ecs_get(world, root, Position);
    test_flt(p->x, ecs_get(world, player(world), Position)->x);
    test_flt(p->y, GROUND_Y);
    test_flt(p->z, 0.0f);

    step(world, 10);
    test_int(plays(world, g->dust, NULL), 1);
    test_int(plays_all(world), 1);

    ecs_fini(world);
}

void Effects_hard_landing_plays_high_dust(void) {
    // bunnyhopper/effects
    ecs_world_t *world = game_new();
    clear_path(world);
    const BunnyGame *g = game(world);

    jump_and_land(world, true);
    ecs_entity_t root;
    test_int(plays(world, g->dust_high, &root), 1);
    test_int(plays(world, g->dust, NULL), 0);
    test_int(plays_all(world), 1);
    test_flt(ecs_get(world, root, Position)->y, GROUND_Y);

    ecs_fini(world);
}

void Effects_hit_plays_hit(void) {
    // bunnyhopper/effects
    ecs_world_t *world = game_new();
    const BunnyGame *g = game(world);
    ecs_entity_t p = player(world);
    float x = ecs_get(world, p, Position)->x;

    carrot_new(world, x);
    step(world, 1);
    test_int(ecs_get(world, p, BunnyLives)->lives_left, 2);
    ecs_entity_t root;
    test_int(plays(world, g->hit, &root), 1);
    test_int(plays_all(world), 1);
    const Position *rp = ecs_get(world, root, Position);
    test_flt(rp->x, x);
    test_flt(rp->y, GROUND_Y);
    test_flt(rp->z, 0.0f);

    step(world, 5);
    test_int(plays(world, g->hit, NULL), 1);

    ecs_fini(world);
}

void Effects_carrot_plays_sparkle(void) {
    // bunnyhopper/effects
    ecs_world_t *world = game_new();
    clear_path(world);
    const BunnyGame *g = game(world);

    ecs_entity_t c = carrot_new(world, DESPAWN_X + 0.5f);
    float last = DESPAWN_X + 0.5f;
    int32_t frames = 0;
    while (ecs_is_alive(world, c) && frames < 60) {
        test_int(plays(world, g->sparkle, NULL), 0);
        last = ecs_get(world, c, Position)->x;
        step(world, 1);
        frames ++;
    }

    test_assert(!ecs_is_alive(world, c));
    test_int(game(world)->carrots, 1);
    ecs_entity_t root;
    test_int(plays(world, g->sparkle, &root), 1);
    test_int(plays_all(world), 1);
    const Position *p = ecs_get(world, root, Position);
    test_assert(fabsf(p->x - (last - INITIAL_SPEED * GAME_DT)) < GAME_EPS);
    test_assert(p->x < DESPAWN_X);
    test_flt(p->y, GROUND_Y);

    ecs_fini(world);
}

void Effects_stats_follow(void) {
    // bunnyhopper/effects
    ecs_world_t *world = game_new();
    clear_path(world);
    const BunnyGame *g = game(world);

    test_int(stats_effects(world), 0);
    jump_and_land(world, false);
    test_int(stats_effects(world), 1);

    ecs_entity_t p = player(world);
    ecs_set(world, p, Position, { -7.0f, GROUND_Y, 0.0f });
    carrot_new(world, -7.0f);
    step(world, 1);
    test_int(plays(world, g->hit, NULL), 1);
    test_int(plays(world, g->sparkle, NULL), 0);
    test_int(stats_effects(world), 2);
    test_int(ecs_singleton_get(world, ParticleStats)->dropped, 0);

    int32_t frames = 0;
    while (stats_effects(world) > 0 && frames < RETIRE_FRAMES) {
        step(world, 1);
        frames ++;
    }

    test_int(stats_effects(world), 0);
    test_int(plays_all(world), 0);

    while (ecs_get(world, p, BunnyJump)->jumps > 0 && frames < 600) {
        step(world, 1);
        frames ++;
    }

    test_int(plays(world, g->dust, NULL), 1);
    test_int(stats_effects(world), 1);

    ecs_fini(world);
}

void Effects_rapid_landings_keep_dust(void) {
    // bunnyhopper/effects
    ecs_world_t *world = game_new();
    clear_path(world);
    const BunnyGame *g = game(world);
    ecs_entity_t p = player(world);
    const ParticleSettings *settings = ecs_singleton_get(world, ParticleSettings);
    int32_t max = settings->effect_max;

    for (int32_t i = 0; i < max + 4; i ++) {
        ecs_set(world, p, Position, { -100.0f, 0.01f, 0.0f });
        ecs_set(world, p, BunnyJump, { .v = -1.0f, .jumps = 1 });
        step(world, 1);
        const ParticleStats *s = ecs_singleton_get(world, ParticleStats);
        test_int(s->dropped, 0);
        test_assert(plays(world, g->dust, NULL) == (i + 1 < max ? i + 1 : max));
    }

    ecs_fini(world);
}

void Effects_no_plays_while_over(void) {
    // bunnyhopper/effects
    ecs_world_t *world = game_new();
    const BunnyGame *g = game(world);
    ecs_entity_t p = player(world);

    game_over(world);
    ecs_entity_t root;
    test_int(plays(world, g->hit, &root), 1);
    test_int(plays_all(world), 1);
    const ParticleEmitter *played = layer(world, root);
    const ParticleEmitter *tmpl = layer(world, g->hit);
    test_flt(played->direction[0], tmpl->direction[0]);
    test_flt(played->direction[1], tmpl->direction[1]);
    test_flt(played->spread, tmpl->spread);
    test_flt(played->speed.min, tmpl->speed.min);
    test_flt(played->speed.max, tmpl->speed.max);

    step(world, RETIRE_FRAMES);
    test_int(stats_effects(world), 0);

    ecs_set(world, p, Position, { -7.0f, 4.0f, 0.0f });
    ecs_set(world, p, BunnyJump, { .v = -20.0f, .jumps = 2 });
    carrot_new(world, DESPAWN_X - 1.0f);
    for (int32_t i = 0; i < 60; i ++) {
        step(world, 1);
        test_int(plays_all(world), 0);
        test_int(stats_effects(world), 0);
    }

    test_assert(state_is(world, "Over"));
    test_int(ecs_get(world, p, BunnyJump)->jumps, 0);

    ecs_fini(world);
}

void Effects_wind_follows_speed(void) {
    // bunnyhopper/effects
    ecs_world_t *world = game_new();
    clear_path(world);
    ecs_entity_t wind = game(world)->effect_wind;

    test_flt(ecs_get(world, wind, ParticleForces)->wind[0], -INITIAL_SPEED);
    speed_set(world, 20.0f);
    step(world, 1);
    test_flt(ecs_get(world, wind, ParticleForces)->wind[0], -20.0f);
    test_flt(ecs_get(world, wind, ParticleForces)->wind[1], 0.0f);
    test_flt(ecs_get(world, wind, ParticleForces)->wind[2], 0.0f);
    test_flt(ecs_get(world, wind, ParticleForces)->gravity[1], 0.0f);

    game_over(world);
    test_flt(ecs_get(world, wind, ParticleForces)->wind[0], 0.0f);
    step(world, 10);
    test_flt(ecs_get(world, wind, ParticleForces)->wind[0], 0.0f);

    ecs_entity_t script = ecs_script(world, {
        .code =
            "async {\n"
            "  await bunnyhopper.restart()\n"
            "}\n"
    });
    test_assert(script != 0);
    step(world, 2);
    test_assert(state_is(world, "Playing"));
    test_flt(ecs_get(world, wind, ParticleForces)->wind[0], -INITIAL_SPEED);

    ecs_fini(world);
}

void Effects_dust_moves_with_world(void) {
    // bunnyhopper/effects
    ecs_world_t *world = game_new();
    clear_path(world);
    const BunnyGame *g = game(world);
    const float speeds[] = { INITIAL_SPEED, 20.0f };
    box_particle_forces_t forces;

    for (int32_t s = 0; s < 2; s ++) {
        speed_set(world, speeds[s]);
        step(world, RETIRE_FRAMES);
        test_int(plays_all(world), 0);
        jump_and_land(world, false);
        ecs_entity_t root;
        test_int(plays(world, g->dust, &root), 1);

        const ParticleEmitter *e = layer(world, root);
        test_assert(e->direction[0] < -0.9f);
        test_assert(e->speed.min < speeds[s]);
        test_assert(e->speed.max > speeds[s]);
        test_assert(e->spread < 30.0f);

        layer_forces(world, root, &forces);
        test_flt(forces.wind[0], -speeds[s]);
        test_flt(forces.acceleration[1], -5.0f);
        test_flt(forces.drag, 2.5f);
        box_particle_forces_t still = forces;
        still.wind[0] = 0.0f;
        for (int32_t t = 1; t <= 3; t ++) {
            float seconds = 0.25f * (float)t;
            float moved = drift(e, DRIFT_SAMPLES, &forces, speeds[s], seconds);
            float rest = drift(layer(world, g->dust), DRIFT_SAMPLES, &still, 0.0f, seconds);
            test_assert(fabsf(moved) < 0.25f);
            test_assert(fabsf(moved - rest) < 0.25f);

            ParticleEmitter unshifted = *layer(world, g->dust);
            test_assert(drift(&unshifted, DRIFT_SAMPLES, &forces, speeds[s], seconds) > 1.0f);
        }
    }

    ecs_fini(world);
}

void Effects_sparkle_moves_with_world(void) {
    // bunnyhopper/effects
    ecs_world_t *world = game_new();
    clear_path(world);
    const BunnyGame *g = game(world);
    box_particle_forces_t forces;

    ecs_entity_t c = carrot_new(world, DESPAWN_X + 0.1f);
    step(world, 2);
    test_assert(!ecs_is_alive(world, c));
    ecs_entity_t root;
    test_int(plays(world, g->sparkle, &root), 1);
    layer_forces(world, root, &forces);
    test_flt(forces.drag, 0.0f);
    test_flt(forces.acceleration[1], 0.0f);

    const ParticleEmitter *e = layer(world, root);
    float moved = drift(e, DRIFT_SAMPLES, &forces, INITIAL_SPEED, 0.5f);
    test_assert(fabsf(moved) < 0.5f);

    ecs_entity_t glow = ecs_lookup_child(world, root, "glow");
    test_assert(glow != 0);
    const ParticleEmitter *ge = ecs_get(world, glow, ParticleEmitter);
    test_assert(ge != NULL);
    test_uint(ge->seed, e->seed);
    test_flt(ge->direction[0], e->direction[0]);
    test_flt(ge->direction[1], e->direction[1]);
    test_flt(ge->speed.min, e->speed.min);
    test_flt(ge->speed.max, e->speed.max);
    test_flt(ge->spread, e->spread);

    ecs_fini(world);
}

/** Check that two plays drew the same layer.
 *
 * @param a A layer
 * @param b The other
 */
static void same_layer(
    const ParticleEmitter *a,
    const ParticleEmitter *b)
{
    test_uint(a->seed, b->seed);
    test_flt(a->direction[0], b->direction[0]);
    test_flt(a->direction[1], b->direction[1]);
    test_flt(a->direction[2], b->direction[2]);
    test_flt(a->spread, b->spread);
    test_flt(a->speed.min, b->speed.min);
    test_flt(a->speed.max, b->speed.max);
}

void Effects_same_run_same_plays(void) {
    // bunnyhopper/effects
    ParticleEmitter first[2];
    for (int32_t run = 0; run < 2; run ++) {
        ecs_world_t *world = game_new();
        clear_path(world);
        const BunnyGame *g = game(world);
        ecs_entity_t root;

        jump_and_land(world, false);
        test_int(plays(world, g->dust, &root), 1);
        ParticleEmitter a = *layer(world, root);
        step(world, RETIRE_FRAMES);
        jump_and_land(world, false);
        test_int(plays(world, g->dust, &root), 1);
        ParticleEmitter b = *layer(world, root);

        test_assert(a.seed != b.seed);
        if (run == 0) {
            first[0] = a;
            first[1] = b;
        } else {
            same_layer(&first[0], &a);
            same_layer(&first[1], &b);
        }

        ecs_fini(world);
    }
}

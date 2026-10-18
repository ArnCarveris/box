/**
 * @file Game.c
 * @brief Gameplay of the bunny hopper scene, stepped with a fixed delta and driven by commands.
 *
 * The scene loads without its Surface, so no GPU backend starts. Most tests move the bunny out
 * of the way of the carrots or stop the world speed to place carrots by hand.
 */

#include <bunnyhopper_game.h>
#include <bake_test.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Path of the game scene in the repository */
#define GAME_SCENE "etc/bunnyhopper/bunnyhopper.flecs"

/** Path of this file in the repository, which locates the repository root */
#define GAME_SOURCE "test/cpu/bunnyhopper/src/"

/** Fixed delta of a frame */
#define GAME_DT (1.0f / 60.0f)

/** Tolerance of positions and velocities accumulated over frames */
#define GAME_EPS (1e-3f)

/** The constants of the scene */
#define GROUND_Y (0.0f)
#define SPAWN_X (24.0f)
#define DESPAWN_X (-8.0f)
#define GRAVITY (-32.0f)
#define JUMP_VELOCITY (13.0f)
#define INVULN_TIME (0.5f)
#define DEAD_HEIGHT (1.5f)
#define MAX_LIVES (3)
#define INITIAL_SPEED (12.0f)

/** Carrots a spawn record holds at most */
#define SPAWNS_MAX (256)

/** A carrot as it spawned */
typedef struct spawn_t {
    ecs_entity_t carrot; /**< The carrot */
    int32_t frame;       /**< Frame it spawned in */
    int32_t passed;      /**< Carrots that passed the bunny when it spawned */
    bool large;          /**< It is a large carrot */
} spawn_t;

/** The carrots of a run in the order they spawned */
typedef struct spawns_t {
    spawn_t items[SPAWNS_MAX]; /**< The carrots */
    int32_t count;             /**< Number of carrots */
} spawns_t;

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

/** Load the game scene without its Surface as boxrun does, and run its first frame, which
 * starts the input tasks.
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

/** Move the bunny out of the way of the carrots, so a run never ends.
 *
 * @param world The world
 */
static void clear_path(
    ecs_world_t *world)
{
    ecs_entity_t p = player(world);
    const Position *pos = ecs_get(world, p, Position);
    ecs_set(world, p, Position, { -100.0f, pos->y, pos->z });
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

/** Get a prefab of the scene.
 *
 * @param world The world
 * @param name Name of the prefab
 * @return The prefab
 */
static ecs_entity_t prefab(
    ecs_world_t *world,
    const char *name)
{
    ecs_entity_t e = ecs_lookup(world, name);
    test_assert(e != 0);

    return e;
}

/** Place a carrot on the ground at an x.
 *
 * @param world The world
 * @param name Name of the carrot prefab
 * @param x The x
 * @return The carrot
 */
static ecs_entity_t carrot_new(
    ecs_world_t *world,
    const char *name,
    float x)
{
    ecs_entity_t e = ecs_new_w_pair(world, EcsIsA, prefab(world, name));
    ecs_add_pair(world, e, EcsChildOf, game(world)->carrot_parent);
    ecs_set(world, e, Position, { x, GROUND_Y, 0.0f });

    return e;
}

/** Count the carrots of the world.
 *
 * @param world The world
 * @return Children of the carrot parent with BunnyCarrot
 */
static int32_t carrots_count(
    ecs_world_t *world)
{
    int32_t result = 0;
    ecs_iter_t it = ecs_children(world, game(world)->carrot_parent);
    while (ecs_children_next(&it)) {
        for (int32_t i = 0; i < it.count; i ++) {
            result += ecs_has(world, it.entities[i], BunnyCarrot);
        }
    }

    return result;
}

/** Get the first carrot of the world.
 *
 * @param world The world
 * @return A child of the carrot parent with BunnyCarrot, 0 for none
 */
static ecs_entity_t carrot_first(
    ecs_world_t *world)
{
    ecs_entity_t result = 0;
    ecs_iter_t it = ecs_children(world, game(world)->carrot_parent);
    while (ecs_children_next(&it)) {
        for (int32_t i = 0; i < it.count && !result; i ++) {
            if (ecs_has(world, it.entities[i], BunnyCarrot)) {
                result = it.entities[i];
            }
        }
    }

    return result;
}

/** Record the carrots that spawned since the last call.
 *
 * @param world The world
 * @param spawns The record
 * @param frame The frame that ran last
 */
static void spawns_update(
    ecs_world_t *world,
    spawns_t *spawns,
    int32_t frame)
{
    ecs_entity_t large = prefab(world, "LargeCarrot");
    ecs_iter_t it = ecs_children(world, game(world)->carrot_parent);
    while (ecs_children_next(&it)) {
        for (int32_t i = 0; i < it.count; i ++) {
            ecs_entity_t e = it.entities[i];
            bool known = false;
            for (int32_t s = 0; s < spawns->count && !known; s ++) {
                known = spawns->items[s].carrot == e;
            }

            if (!known && spawns->count < SPAWNS_MAX) {
                spawns->items[spawns->count ++] = (spawn_t){
                    .carrot = e,
                    .frame = frame,
                    .passed = game(world)->carrots,
                    .large = ecs_has_pair(world, e, EcsIsA, large)
                };
            }
        }
    }
}

/** Run a world with the bunny out of the way and record its carrots.
 *
 * @param world The world
 * @param frames Frames to run
 * @param spawns The record
 */
static void spawns_run(
    ecs_world_t *world,
    int32_t frames,
    spawns_t *spawns)
{
    clear_path(world);
    spawns->count = 0;
    for (int32_t f = 0; f < frames; f ++) {
        step(world, 1);
        spawns_update(world, spawns, f);
    }
}

/** Await a bunnyhopper function from a script and get what it resolved with.
 *
 * @param world The world
 * @param function Name of the function, jump or restart
 * @return 1 for true, 0 for false, -1 when it did not resolve
 */
static int32_t call_answer(
    ecs_world_t *world,
    const char *function)
{
    char *code = flecs_asprintf(
        "probe_true {}\n"
        "probe_false {}\n"
        "template Probe {\n"
        "  mut answered: bool = false\n"
        "  mut value: bool = false\n"
        "  async {\n"
        "    const r = await bunnyhopper.%s()\n"
        "    value = r\n"
        "    answered = true\n"
        "  }\n"
        "  if answered {\n"
        "    if value {\n"
        "      probe_true\n"
        "    } else {\n"
        "      probe_false\n"
        "    }\n"
        "  }\n"
        "}\n"
        "probe {\n"
        "  Probe: {}\n"
        "}\n", function);
    ecs_entity_t script = ecs_script(world, { .code = code });
    test_assert(script != 0);
    ecs_os_free(code);
    step(world, 2);

    ecs_entity_t probe = ecs_lookup(world, "probe");
    test_assert(probe != 0);
    int32_t result = -1;
    if (ecs_has_id(world, probe, ecs_lookup(world, "probe_true"))) {
        result = 1;
    } else if (ecs_has_id(world, probe, ecs_lookup(world, "probe_false"))) {
        result = 0;
    }

    ecs_delete(world, script);
    ecs_delete(world, probe);

    return result;
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
    carrot_new(world, "SmallCarrot", ecs_get(world, p, Position)->x);
    step(world, 1);
    test_assert(state_is(world, "Over"));
}

void Game_scene_loads(void) {
    // bunnyhopper/scene
    ecs_world_t *world = game_new();

    int32_t scripts = 0;
    ecs_iter_t it = ecs_each(world, EcsScript);
    while (ecs_each_next(&it)) {
        EcsScript *s = ecs_field(&it, EcsScript, 0);
        for (int32_t i = 0; i < it.count; i ++) {
            test_assert(s[i].script != NULL);
            test_assert(s[i].error == NULL);
            scripts += s[i].filename != NULL;
        }
    }

    test_int(scripts, 8);
    test_assert(ecs_lookup(world, "BunnyModel") != 0);
    test_assert(ecs_has_id(world, prefab(world, "SmallCarrot"), EcsPrefab));
    test_assert(ecs_has_id(world, prefab(world, "LargeCarrot"), EcsPrefab));
    test_assert(ecs_lookup(world, "scenery.tuft_11") != 0);
    test_assert(ecs_lookup(world, "scenery.tree_4") != 0);
    test_assert(ecs_lookup(world, "scenery.hill_3") != 0);
    test_assert(ecs_lookup(world, "player.leg_back_right") != 0);
    test_assert(ecs_lookup(world, "window") != 0);

    const BunnyGame *g = game(world);
    test_int(g->carrots, 0);
    test_int(g->max_lives, MAX_LIVES);
    test_flt(g->speed, INITIAL_SPEED);
    test_uint(g->player, ecs_lookup(world, "player"));
    test_uint(g->carrot_parent, ecs_lookup(world, "carrots"));
    test_uint(g->small_carrot, prefab(world, "SmallCarrot"));
    test_uint(g->large_carrot, prefab(world, "LargeCarrot"));

    const BunnyCarrot *small = ecs_get(world, g->small_carrot, BunnyCarrot);
    test_flt(small->half_width, 0.325f);
    test_flt(small->height, 1.1f);
    const BunnyCarrot *large = ecs_get(world, g->large_carrot, BunnyCarrot);
    test_flt(large->half_width, 0.5f);
    test_flt(large->height, 3.2f);

    ecs_fini(world);
}

void Game_actions_and_bindings(void) {
    // bunnyhopper/input
    ecs_world_t *world = game_new();

    ecs_entity_t jump = ecs_lookup(world, "actions.Jump");
    ecs_entity_t restart = ecs_lookup(world, "actions.Restart");
    test_assert(jump != 0);
    test_assert(restart != 0);
    test_assert(ecs_get(world, jump, Action)->description != NULL);
    test_assert(ecs_get(world, restart, Action)->description != NULL);
    test_assert(ecs_has(world, jump, ActionState));
    test_int(ecs_get(world, jump, Binding)->key, KeySpace);
    test_int(ecs_get(world, restart, Binding)->key, KeyR);

    command(world, "key.press Space");
    step(world, 1);
    test_bool(ecs_get(world, jump, ActionState)->pressed, true);

    ecs_fini(world);
}

void Game_starts_playing(void) {
    // bunnyhopper/state
    ecs_world_t *world = game_new();

    test_assert(state_is(world, "Playing"));
    test_assert(!state_is(world, "Over"));
    test_assert(ecs_has_id(world, ecs_id(BunnyGameState), EcsExclusive));

    ecs_entity_t p = player(world);
    test_flt(ecs_get(world, p, Position)->x, -7.0f);
    test_flt(ecs_get(world, p, Position)->y, GROUND_Y);
    test_flt(ecs_get(world, p, Bunny)->speed, INITIAL_SPEED);
    test_bool(ecs_get(world, p, Bunny)->dead, false);
    test_int(ecs_get(world, p, BunnyLives)->lives_left, MAX_LIVES);
    test_int(ecs_get(world, p, BunnyJump)->jumps, 0);
    test_assert(ecs_has_id(world, p, DynamicTransform));
    test_assert(ecs_lookup(world, "player.eye_left") != 0);
    test_assert(ecs_lookup(world, "player.eye_left_bar_a") == 0);

    test_assert(ecs_script_run(world, "check",
        "using bunnyhopper\n"
        "if $?[(BunnyGameState, Playing)] {\n"
        "  playing_seen {}\n"
        "}\n", NULL) == 0);
    test_assert(ecs_lookup(world, "playing_seen") != 0);

    ecs_fini(world);
}

void Game_jump_lands(void) {
    // bunnyhopper/jump
    ecs_world_t *world = game_new();
    ecs_entity_t p = player(world);
    clear_path(world);

    command(world, "key.press Space");
    step(world, 1);
    test_assert(ecs_get(world, p, Position)->y > GROUND_Y);
    test_int(ecs_get(world, p, BunnyJump)->jumps, 1);

    int32_t frames = 1;
    while (ecs_get(world, p, Position)->y > GROUND_Y && frames < 600) {
        step(world, 1);
        frames ++;
    }

    float airtime = 2.0f * JUMP_VELOCITY / -GRAVITY;
    test_assert(fabsf((float)frames * GAME_DT - airtime) <= GAME_DT);
    test_flt(ecs_get(world, p, Position)->y, GROUND_Y);
    test_flt(ecs_get(world, p, BunnyJump)->v, 0.0f);
    test_int(ecs_get(world, p, BunnyJump)->jumps, 0);
    test_int(call_answer(world, "jump"), 1);

    ecs_fini(world);
}

void Game_double_jump(void) {
    // bunnyhopper/jump
    ecs_world_t *world = game_new();
    ecs_entity_t p = player(world);
    clear_path(world);

    command(world, "key.press Space");
    step(world, 20);
    float y = ecs_get(world, p, Position)->y;
    float v = ecs_get(world, p, BunnyJump)->v;
    test_assert(y > GROUND_Y);
    test_assert(v < JUMP_VELOCITY);
    test_int(ecs_get(world, p, BunnyJump)->jumps, 1);

    command(world, "key.press Space");
    step(world, 1);
    test_int(ecs_get(world, p, BunnyJump)->jumps, 2);
    test_assert(fabsf(ecs_get(world, p, BunnyJump)->v -
        (JUMP_VELOCITY + GRAVITY * GAME_DT)) < GAME_EPS);
    test_assert(ecs_get(world, p, Position)->y > y);

    ecs_fini(world);
}

void Game_third_jump_ignored(void) {
    // bunnyhopper/jump
    ecs_world_t *world = game_new();
    ecs_entity_t p = player(world);
    clear_path(world);

    command(world, "key.press Space");
    step(world, 10);
    command(world, "key.press Space");
    step(world, 10);
    test_int(ecs_get(world, p, BunnyJump)->jumps, 2);
    float v = ecs_get(world, p, BunnyJump)->v;

    command(world, "key.press Space");
    step(world, 1);
    test_int(ecs_get(world, p, BunnyJump)->jumps, 2);
    test_assert(fabsf(ecs_get(world, p, BunnyJump)->v - (v + GRAVITY * GAME_DT)) < GAME_EPS);
    test_int(call_answer(world, "jump"), 0);

    ecs_fini(world);
}

void Game_carrot_spawns_at_spawn_x(void) {
    // bunnyhopper/carrots
    ecs_world_t *world = game_new();
    clear_path(world);

    int32_t frames = 0;
    while (!carrot_first(world) && frames < 600) {
        step(world, 1);
        frames ++;
    }

    ecs_entity_t c = carrot_first(world);
    test_assert(c != 0);
    test_assert(fabsf((float)frames * GAME_DT * INITIAL_SPEED - 6.0f) <=
        INITIAL_SPEED * GAME_DT * 2);
    test_flt(ecs_get(world, c, Position)->x, SPAWN_X);
    test_flt(ecs_get(world, c, Position)->y, GROUND_Y);
    test_assert(ecs_has_pair(world, c, EcsIsA, prefab(world, "SmallCarrot")));
    test_assert(ecs_has_id(world, c, DynamicTransform));
    test_int(carrots_count(world), 1);

    const BunnyGame *g = game(world);
    test_flt(g->distance_since_spawn, 0.0f);
    test_assert(g->next_spawn_distance >= INITIAL_SPEED);
    test_assert(g->next_spawn_distance < 2 * INITIAL_SPEED);

    ecs_fini(world);
}

void Game_carrot_scrolls_at_speed(void) {
    // bunnyhopper/carrots
    ecs_world_t *world = game_new();
    clear_path(world);

    while (!carrot_first(world)) {
        step(world, 1);
    }

    ecs_entity_t c = carrot_first(world);
    step(world, 30);
    test_assert(fabsf(ecs_get(world, c, Position)->x -
        (SPAWN_X - 30 * GAME_DT * INITIAL_SPEED)) < GAME_EPS);

    speed_set(world, 30.0f);
    step(world, 10);
    test_assert(fabsf(ecs_get(world, c, Position)->x -
        (SPAWN_X - 30 * GAME_DT * INITIAL_SPEED - 10 * GAME_DT * 30.0f)) < GAME_EPS);

    ecs_fini(world);
}

void Game_carrot_despawns_counted(void) {
    // bunnyhopper/carrots
    ecs_world_t *world = game_new();
    clear_path(world);

    while (!carrot_first(world)) {
        step(world, 1);
    }

    ecs_entity_t c = carrot_first(world);
    float last = SPAWN_X;
    int32_t frames = 0;
    while (ecs_is_alive(world, c) && frames < 1200) {
        last = ecs_get(world, c, Position)->x;
        test_int(game(world)->carrots, 0);
        step(world, 1);
        frames ++;
    }

    test_assert(!ecs_is_alive(world, c));
    test_assert(last >= DESPAWN_X);
    test_assert(last - GAME_DT * INITIAL_SPEED < DESPAWN_X);
    test_int(game(world)->carrots, 1);

    ecs_fini(world);
}

void Game_speed_up_every_fifth(void) {
    // bunnyhopper/speed
    ecs_world_t *world = game_new();
    ecs_entity_t p = player(world);
    clear_path(world);

    int32_t frames = 0;
    while (game(world)->carrots < 4 && frames < 6000) {
        step(world, 1);
        frames ++;
    }

    test_int(game(world)->carrots, 4);
    test_flt(game(world)->speed, INITIAL_SPEED);
    test_flt(ecs_get(world, p, Bunny)->speed, INITIAL_SPEED);
    ecs_entity_t ear = ecs_lookup(world, "player.ear_left");
    test_assert(ear != 0);
    test_flt(ecs_get(world, ear, Rotation)->z, 0.0f);

    while (game(world)->carrots < 5 && frames < 6000) {
        step(world, 1);
        frames ++;
    }

    test_int(game(world)->carrots, 5);
    test_flt(game(world)->speed, INITIAL_SPEED + 2);
    test_flt(ecs_get(world, p, Bunny)->speed, INITIAL_SPEED + 2);
    test_assert(fabsf(ecs_get(world, ear, Rotation)->z - 0.1f) < GAME_EPS);

    while (game(world)->carrots < 10 && frames < 12000) {
        step(world, 1);
        frames ++;
    }

    test_int(game(world)->carrots, 10);
    test_flt(game(world)->speed, INITIAL_SPEED + 4);
    test_flt(ecs_get(world, p, Bunny)->speed, INITIAL_SPEED + 4);

    ecs_fini(world);
}

void Game_large_after_five(void) {
    // bunnyhopper/carrots
    ecs_world_t *world = game_new();
    static spawns_t spawns;

    spawns_run(world, 6000, &spawns);
    test_assert(spawns.count > 20);

    int32_t large = 0;
    for (int32_t i = 0; i < spawns.count; i ++) {
        if (spawns.items[i].passed < 5) {
            test_bool(spawns.items[i].large, false);
        }

        large += spawns.items[i].large;
    }

    test_assert(large > 0);
    test_assert(large < spawns.count);

    ecs_fini(world);
}

void Game_collision_costs_life(void) {
    // bunnyhopper/collision
    ecs_world_t *world = game_new();
    ecs_entity_t p = player(world);
    speed_set(world, 0.0f);

    carrot_new(world, "SmallCarrot", -7.0f);
    step(world, 1);

    test_int(ecs_get(world, p, BunnyLives)->lives_left, MAX_LIVES - 1);
    test_flt(ecs_get(world, p, BunnyJump)->v, JUMP_VELOCITY);
    test_int(ecs_get(world, p, BunnyJump)->jumps, 1);
    const BunnyInvulnerable *inv = ecs_get(world, p, BunnyInvulnerable);
    test_assert(inv != NULL);
    test_flt(inv->time_left, INVULN_TIME);
    test_assert(state_is(world, "Playing"));

    step(world, 1);
    test_assert(ecs_get(world, p, Position)->y > GROUND_Y);
    test_assert(ecs_get(world, p, BunnyInvulnerable)->time_left < INVULN_TIME);

    ecs_fini(world);
}

void Game_invulnerable_no_second_hit(void) {
    // bunnyhopper/collision
    ecs_world_t *world = game_new();
    ecs_entity_t p = player(world);
    speed_set(world, 0.0f);

    carrot_new(world, "LargeCarrot", -7.0f);
    step(world, 1);
    test_int(ecs_get(world, p, BunnyLives)->lives_left, MAX_LIVES - 1);

    int32_t frames = 0;
    while (ecs_has(world, p, BunnyInvulnerable) && frames < 600) {
        test_int(ecs_get(world, p, BunnyLives)->lives_left, MAX_LIVES - 1);
        step(world, 1);
        frames ++;
    }

    test_assert(fabsf((float)frames * GAME_DT - INVULN_TIME) <= GAME_DT);
    step(world, 1);
    test_int(ecs_get(world, p, BunnyLives)->lives_left, MAX_LIVES - 2);
    test_assert(ecs_has(world, p, BunnyInvulnerable));

    ecs_fini(world);
}

void Game_last_life_game_over(void) {
    // bunnyhopper/game_over
    ecs_world_t *world = game_new();
    ecs_entity_t p = player(world);

    game_over(world);
    test_assert(!state_is(world, "Playing"));
    test_int(ecs_get(world, p, BunnyLives)->lives_left, 0);
    test_flt(ecs_get(world, p, Bunny)->speed, 0.0f);
    test_bool(ecs_get(world, p, Bunny)->dead, true);
    test_assert(!ecs_has(world, p, BunnyInvulnerable));

    step(world, 1);
    test_flt(ecs_get(world, p, Position)->y, GROUND_Y + DEAD_HEIGHT);
    test_assert(fabsf(ecs_get(world, p, Rotation)->z - 3.14159265f) < GAME_EPS);
    test_assert(ecs_lookup(world, "player.eye_left") == 0);
    test_assert(ecs_lookup(world, "player.eye_left_bar_a") != 0);
    test_assert(ecs_lookup(world, "player.eye_right_bar_b") != 0);

    ecs_fini(world);
}

void Game_game_over_world_stops(void) {
    // bunnyhopper/game_over
    ecs_world_t *world = game_new();

    step(world, 60);
    ecs_entity_t c = carrot_first(world);
    test_assert(c != 0);
    game_over(world);

    ecs_entity_t tuft = ecs_lookup(world, "scenery.tuft_0");
    ecs_entity_t hill = ecs_lookup(world, "scenery.hill_0");
    float cx = ecs_get(world, c, Position)->x;
    float tx = ecs_get(world, tuft, Position)->x;
    float hx = ecs_get(world, hill, Position)->x;
    int32_t count = carrots_count(world);
    BunnyGame g = *game(world);

    step(world, 120);
    test_flt(ecs_get(world, c, Position)->x, cx);
    test_flt(ecs_get(world, tuft, Position)->x, tx);
    test_flt(ecs_get(world, hill, Position)->x, hx);
    test_int(carrots_count(world), count);
    test_int(game(world)->carrots, g.carrots);
    test_flt(game(world)->distance_since_spawn, g.distance_since_spawn);
    test_assert(state_is(world, "Over"));

    ecs_fini(world);
}

void Game_game_over_jump_false(void) {
    // bunnyhopper/game_over
    ecs_world_t *world = game_new();
    ecs_entity_t p = player(world);

    game_over(world);
    step(world, 1);
    command(world, "key.press Space");
    step(world, 1);
    test_flt(ecs_get(world, p, Position)->y, GROUND_Y + DEAD_HEIGHT);
    test_flt(ecs_get(world, p, BunnyJump)->v, 0.0f);
    test_int(ecs_get(world, p, BunnyJump)->jumps, 0);
    test_int(call_answer(world, "jump"), 0);

    ecs_fini(world);
}

void Game_restart_while_playing(void) {
    // bunnyhopper/restart
    ecs_world_t *world = game_new();
    ecs_entity_t p = player(world);
    clear_path(world);

    step(world, 60);
    ecs_entity_t c = carrot_first(world);
    test_assert(c != 0);
    ecs_set(world, p, BunnyLives, { 2 });

    command(world, "key.press R");
    step(world, 1);
    test_assert(state_is(world, "Playing"));
    test_assert(ecs_is_alive(world, c));
    test_int(ecs_get(world, p, BunnyLives)->lives_left, 2);
    test_assert(game(world)->distance_since_spawn > 0);
    test_int(call_answer(world, "restart"), 0);
    test_assert(ecs_is_alive(world, c));

    ecs_fini(world);
}

void Game_restart_after_game_over(void) {
    // bunnyhopper/restart
    ecs_world_t *world = game_new();
    ecs_entity_t p = player(world);

    step(world, 60);
    BunnyGame *g = ecs_singleton_get_mut(world, BunnyGame);
    g->carrots = 7;
    g->speed = 14.0f;
    g->next_spawn_distance = 20.0f;
    ecs_singleton_modified(world, BunnyGame);
    game_over(world);
    test_assert(carrots_count(world) > 0);
    step(world, 1);

    command(world, "key.press R");
    step(world, 1);
    test_assert(state_is(world, "Playing"));
    test_int(carrots_count(world), 0);
    test_flt(ecs_get(world, p, Position)->x, -7.0f);
    test_flt(ecs_get(world, p, Position)->y, GROUND_Y);
    test_flt(ecs_get(world, p, Rotation)->z, 0.0f);
    test_flt(ecs_get(world, p, Bunny)->speed, INITIAL_SPEED);
    test_bool(ecs_get(world, p, Bunny)->dead, false);
    test_flt(ecs_get(world, p, BunnyJump)->v, 0.0f);
    test_int(ecs_get(world, p, BunnyJump)->jumps, 0);
    test_int(ecs_get(world, p, BunnyLives)->lives_left, MAX_LIVES);
    test_assert(!ecs_has(world, p, BunnyInvulnerable));
    test_int(game(world)->carrots, 0);
    test_flt(game(world)->speed, INITIAL_SPEED);
    test_flt(game(world)->next_spawn_distance, 6.0f);
    test_assert(fabsf(game(world)->distance_since_spawn - INITIAL_SPEED * GAME_DT) < GAME_EPS);
    test_assert(ecs_lookup(world, "player.eye_left") != 0);
    test_assert(ecs_lookup(world, "player.eye_left_bar_a") == 0);

    command(world, "key.press Space");
    step(world, 1);
    test_int(ecs_get(world, p, BunnyJump)->jumps, 1);

    ecs_fini(world);
}

void Game_scenery_wraps(void) {
    // bunnyhopper/scenery
    ecs_world_t *world = game_new();
    clear_path(world);

    ecs_entity_t tuft = ecs_lookup(world, "scenery.tuft_0");
    ecs_entity_t tree = ecs_lookup(world, "scenery.tree_0");
    test_assert(tuft != 0);
    test_assert(tree != 0);
    const BunnyScenery *s = ecs_get(world, tuft, BunnyScenery);
    test_flt(s->parallax, 1.0f);
    test_flt(s->wrap_min, -32.0f);
    test_flt(s->wrap_span, 64.0f);

    float tree_x = ecs_get(world, tree, Position)->x;
    step(world, 1);
    test_assert(fabsf(ecs_get(world, tree, Position)->x -
        (tree_x - 0.75f * INITIAL_SPEED * GAME_DT)) < GAME_EPS);

    int32_t wraps = 0;
    for (int32_t f = 0; f < 600; f ++) {
        float x = ecs_get(world, tuft, Position)->x;
        step(world, 1);
        float nx = ecs_get(world, tuft, Position)->x;
        float step_x = game(world)->speed * GAME_DT;
        test_assert(nx >= -32.0f);
        test_assert(nx < 32.0f);
        if (nx > x) {
            test_assert(fabsf(nx - (x - step_x + 64.0f)) < GAME_EPS);
            wraps ++;
        } else {
            test_assert(fabsf(nx - (x - step_x)) < GAME_EPS);
        }
    }

    test_assert(wraps >= 1);

    ecs_fini(world);
}

void Game_same_seed_same_carrots(void) {
    // bunnyhopper/random
    static spawns_t first, second, other;
    ecs_world_t *worlds[3];
    spawns_t *runs[3] = { &first, &second, &other };
    uint64_t seeds[3] = { 42, 42, 43 };

    for (int32_t w = 0; w < 3; w ++) {
        worlds[w] = game_new();
        BunnyGame *g = ecs_singleton_get_mut(worlds[w], BunnyGame);
        g->seed = seeds[w];
        ecs_singleton_modified(worlds[w], BunnyGame);
        spawns_run(worlds[w], 3000, runs[w]);
        ecs_fini(worlds[w]);
    }

    test_assert(first.count > 10);
    test_int(first.count, second.count);
    bool same_other = first.count == other.count;
    for (int32_t i = 0; i < first.count; i ++) {
        test_int(first.items[i].frame, second.items[i].frame);
        test_bool(first.items[i].large, second.items[i].large);
        if (same_other && first.items[i].frame != other.items[i].frame) {
            same_other = false;
        }
    }

    test_bool(same_other, false);
}

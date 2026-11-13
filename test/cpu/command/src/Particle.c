/**
 * @file Particle.c
 * @brief Tests for driving a particle emitter through its playback states with the set command
 * and reading its clock and the live particles back with get.
 */

#include <box.h>
#include <bake_test.h>
#include <string.h>
#include "render.h"

/** Frame delta of the test, exact in a float and in microseconds */
#define DELTA (0.25f)

/** Run a command and check that it succeeds with an output that contains a text.
 *
 * @param world The world
 * @param s The session
 * @param cmd The command
 * @param expect Text the output should contain
 */
static void run(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *cmd,
    const char *expect)
{
    bool error = true;
    char *out = box_command_run(world, s, cmd, &error);
    test_bool(error, false);
    test_assert(strstr(out, expect) != NULL);
    ecs_os_free(out);
}

/** Run frames on the fixed clock of the test.
 *
 * @param world The world
 * @param count Frames to run
 */
static void frames(
    ecs_world_t *world,
    int32_t count)
{
    int32_t i;
    for (i = 0; i < count; i ++) {
        ecs_progress(world, DELTA);
        box_render_sync(world);
    }
}

void Particle_playback_states(void) {
    // particle/playback
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    box_render_null_t *null = box_render_null_new();
    box_render_backend_t backend = box_render_null_backend(null);
    box_render_set_backend(world, &backend);

    test_assert(ecs_script_run(world, "fx",
        "using box.particle\n"
        "using box.transform\n"
        "fx {\n"
        "  Position: {0, 0, 0}\n"
        "  Emitter: {rate: 8, lifetime: {0.75, 0.75}}\n"
        "  Playback: {}\n"
        "}\n", NULL) == 0);
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    const char *playback = "get fx box.particle.Playback";
    const char *live = "get $ box.particle.Stats";

    frames(world, 2);
    run(world, s, playback, "\"time\":0.5");
    run(world, s, live, "\"particles\":4");

    run(world, s, "set fx box.particle.Playback {state: Paused}", "");
    frames(world, 3);
    run(world, s, playback, "\"time\":0.5");
    run(world, s, live, "\"particles\":4");
    run(world, s, playback, "\"state\":\"Paused\"");

    run(world, s, "set fx box.particle.Playback {state: Playing}", "");
    frames(world, 1);
    run(world, s, playback, "\"time\":0.75");
    run(world, s, live, "\"particles\":6");

    run(world, s, "set fx box.particle.Playback {state: Stopped}", "");
    frames(world, 1);
    run(world, s, playback, "\"time\":0.75");
    run(world, s, live, "\"particles\":4");
    frames(world, 2);
    run(world, s, live, "\"particles\":0");

    run(world, s, "set fx box.particle.Playback {state: Playing}", "");
    frames(world, 1);
    run(world, s, playback, "\"time\":0.25");
    run(world, s, live, "\"particles\":2");

    run(world, s, "set fx box.particle.Playback {reset: true}", "");
    frames(world, 1);
    run(world, s, playback, "\"time\":0");
    run(world, s, live, "\"particles\":0");
    run(world, s, playback, "\"reset\":false");

    ecs_fini(world);
    box_render_null_free(null);
}

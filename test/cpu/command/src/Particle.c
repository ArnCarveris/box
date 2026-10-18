/**
 * @file Particle.c
 * @brief Tests for driving a particle emitter through its playback states with the set command
 * and reading its clock and the live particles back with get.
 */

#include <box.h>
#include <bake_test.h>
#include "render.h"

/** Frame delta of the test, exact in a float and in microseconds */
#define DELTA (0.25f)

/** Run a command and check that it succeeds with an output.
 *
 * @param world The world
 * @param s The session
 * @param cmd The command
 * @param expect The output it should print
 */
static void run(
    ecs_world_t *world,
    ecs_entity_t s,
    const char *cmd,
    const char *expect)
{
    bool error = true;
    char *out = box_terminal_run(world, s, cmd, &error);
    test_bool(error, false);
    test_str(out, expect);
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
        "fx {\n"
        "  ParticleEmitter: {rate: 8, lifetime: {0.75, 0.75}}\n"
        "  ParticlePlayback: {}\n"
        "}\n", NULL) == 0);
    ecs_entity_t s = box_terminal_new(world);
    const char *time = "get fx ParticlePlayback.time";
    const char *live = "get /box.particle.ParticleStats ParticleStats.particles";

    frames(world, 2);
    run(world, s, time, "0.5");
    run(world, s, live, "4");

    run(world, s, "set fx ParticlePlayback.state ParticlePaused", "");
    frames(world, 3);
    run(world, s, time, "0.5");
    run(world, s, live, "4");
    run(world, s, "get fx ParticlePlayback.state", "ParticlePaused");

    run(world, s, "set fx ParticlePlayback {state: ParticlePlaying}", "");
    frames(world, 1);
    run(world, s, time, "0.75");
    run(world, s, live, "6");

    run(world, s, "set fx ParticlePlayback.state ParticleStopped", "");
    frames(world, 1);
    run(world, s, time, "0.75");
    run(world, s, live, "4");
    frames(world, 2);
    run(world, s, live, "0");

    run(world, s, "set fx ParticlePlayback.state ParticlePlaying", "");
    frames(world, 1);
    run(world, s, time, "0.25");
    run(world, s, live, "2");

    run(world, s, "set fx ParticlePlayback.reset true", "");
    frames(world, 1);
    run(world, s, time, "0");
    run(world, s, live, "0");
    run(world, s, "get fx ParticlePlayback.reset", "false");

    ecs_fini(world);
    box_render_null_free(null);
}

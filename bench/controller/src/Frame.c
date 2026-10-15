/**
 * @file Frame.c
 * @brief Benchmarks for a frame of a world with one first person controller and one camera.
 *
 * The default case reads the default action scopes, the custom case scopes of its own that hold
 * every action by name, so the difference is what reading a custom scope costs per frame.
 */

#include <box.h>
#include <bake_bench.h>

/** Seconds of a frame */
#define BENCH_FRAME_DT (1.0f / 60.0f)

/** Names of the first person actions, a superset of the camera actions */
static const char *bench_frame_actions[] = {
    "Forward", "Back", "Left", "Right", "Up", "Down", "Fast", "Look", "Jump", "Crouch", "Fly",
    "TurnLeft", "TurnRight", "TurnUp", "TurnDown"
};

/** Run frames of a world with a first person controller and a camera controller.
 *
 * @param b The bench
 * @param custom Point both controllers at a scope of their own
 */
static void bench_frame_run(
    bench_t *b,
    bool custom)
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);
    ecs_entity_t fp = ecs_new(world);
    ecs_entity_t cam = ecs_new(world);
    double run = 0;
    int32_t i;
    ecs_add(world, fp, FirstPersonController);
    ecs_add(world, cam, CameraController);
    if (custom) {
        ecs_entity_t scope = ecs_entity(world, { .name = "keys" });
        for (i = 0; i < 15; i ++) {
            ecs_entity_t a = ecs_entity(world, { .name = bench_frame_actions[i], .parent = scope });
            char *name = ECS_CONST_CAST(char*, bench_frame_actions[i]);
            ecs_set(world, a, Action, { .description = name });
        }

        ecs_get_mut(world, fp, FirstPersonController)->actions = scope;
        ecs_get_mut(world, cam, CameraController)->actions = scope;
    }

    ecs_progress(world, BENCH_FRAME_DT);
    while (bench_iter(b)) {
        ecs_progress(world, BENCH_FRAME_DT);
        run ++;
    }

    bench_counter(b, "frames_run", run);
    ecs_fini(world);
}

void Frame_default_scope(
    bench_t *b)
{
    // controller_fixes/controller_minor
    bench_frame_run(b, false);
}

void Frame_custom_scope(
    bench_t *b)
{
    // controller_fixes/controller_minor
    bench_frame_run(b, true);
}

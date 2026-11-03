/**
 * @file Animator.c
 * @brief Benchmarks for a frame of Animator playback, each animator with one Position.y track.
 */

#include <box.h>
#include <bake_bench.h>

/** Create a world with count animators that play one Position.y track and run a frame.
 *
 * @param world The world
 * @param count Number of animators
 */
static void bench_animators(
    ecs_world_t *world,
    int32_t count)
{
    ECS_IMPORT(world, Box);
    ecs_entity_t clip = ecs_entity(world, { .name = "bob" });
    ecs_set(world, clip, AnimationClip, { 0 });
    AnimationTrack t = {
        .component = ecs_id(Position),
        .member = (char*)"y",
        .curve = { .kind = CurveCatmullRom }
    };
    CurveKey keys[] = {{0, 0}, {0.5f, 1}, {1, 0}};
    int32_t i;
    for (i = 0; i < 3; i ++) {
        *ecs_vec_append_t(NULL, &t.curve.keys, CurveKey) = keys[i];
    }

    ecs_entity_t track = ecs_entity(world, { .name = "y", .parent = clip });
    ecs_set_ptr(world, track, AnimationTrack, &t);
    ecs_vec_fini_t(NULL, &t.curve.keys, CurveKey);

    for (i = 0; i < count; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {(float)i, 0, 0});
        ecs_set(world, e, Animator, {
            .clip = clip,
            .time = (float)i * 0.001f,
            .speed = 1,
            .mode = AnimationLoop
        });
    }

    ecs_progress(world, 0.016f);
}

/** Run frames of count animators, reported per animator.
 *
 * @param b The bench
 * @param count Number of animators
 */
static void bench_animators_run(
    bench_t *b,
    int32_t count)
{
    ecs_world_t *world = ecs_init();
    bench_animators(world, count);
    bench_set_items(b, count);

    while (bench_iter(b)) {
        ecs_progress(world, 0.016f);
    }

    ecs_fini(world);
}

void Animator_position_y_1k(
    bench_t *b)
{
    // engine_core/animation
    bench_animators_run(b, 1000);
}

void Animator_position_y_10k(
    bench_t *b)
{
    // engine_core/animation
    bench_animators_run(b, 10000);
}

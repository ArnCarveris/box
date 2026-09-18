/**
 * @file core/run.c
 * @brief Frame loop with stop conditions and CPU budget pacing.
 */

#include <box.h>

int box_run(
    ecs_world_t *world)
{
    const App *app = ecs_singleton_get(world, App);
    ecs_assert(app != NULL, ECS_INVALID_PARAMETER, NULL);

    ecs_set_target_fps(world, (float)app->target_fps);

    ecs_time_t start;
    ecs_os_get_time(&start);

    while (true) {
        if (!ecs_progress(world, 0)) {
            break;
        }

        App *state = ecs_singleton_ensure(world, App);
        state->frame ++;

        if (state->frames && state->frame >= state->frames) {
            break;
        }

        if (state->timeout > 0 && ecs_time_measure(&start) >= state->timeout) {
            break;
        }
    }

    return 0;
}

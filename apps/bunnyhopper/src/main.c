/**
 * @file main.c
 * @brief Runs the bunny hopper game with the settings of the command line.
 */

#include <bunnyhopper.h>

/** Set the game scene as the default, parse the command line into the App singleton, where a
 * scene argument replaces the default, and run the world.
 *
 * @param argc Argument count
 * @param argv Argument array
 * @return 0 when the run completed, nonzero when it was refused or failed
 */
int main(
    int argc,
    char *argv[])
{
    ecs_world_t *world = ecs_init();
    int result = 0;

    ECS_IMPORT(world, Box);
    ECS_IMPORT(world, Bunnyhopper);

    App *app = ecs_singleton_get_mut(world, App);
    ecs_os_free(app->scene);
    app->scene = ecs_os_strdup(BUNNYHOPPER_SCENE);
    ecs_singleton_modified(world, App);

    result = box_app_parse(world, argc, argv);
    if (!result) {
        result = box_app_run(world);
    }

    ecs_fini(world);

    return result ? 1 : 0;
}

/**
 * @file main.c
 * @brief Runs a box world with the settings of the command line.
 */

#include <boxrun.h>

/** Parse the command line into the App singleton and run the world.
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

    result = box_app_parse(world, argc, argv);
    if (!result) {
        result = box_app_run(world);
    }

    ecs_fini(world);

    return result ? 1 : 0;
}

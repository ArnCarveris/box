/**
 * @file main.c
 * @brief Entry point of the box application.
 */

#include <box.h>

/** Start the engine, load the scene passed on the command line and run it.
 *
 * @param argc Argument count
 * @param argv Argument array
 * @return 0 if success, nonzero if not successful
 */
int main(
    int argc,
    char *argv[])
{
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, box_core);

    if (box_parse_args(world, argc, argv)) {
        goto error;
    }

    if (box_run(world)) {
        goto error;
    }

    return ecs_fini(world);
error:
    ecs_fini(world);
    return -1;
}

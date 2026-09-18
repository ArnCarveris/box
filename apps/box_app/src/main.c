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
    ECS_IMPORT(world, BoxCore);
    ECS_IMPORT(world, BoxSurface);
    ECS_IMPORT(world, BoxTransform);
    ECS_IMPORT(world, BoxCamera);
    ECS_IMPORT(world, BoxLight);
    ECS_IMPORT(world, BoxMaterial);
    ECS_IMPORT(world, BoxRender);
    ECS_IMPORT(world, BoxGeometry);

    if (box_parse_args(world, argc, argv)) {
        goto error;
    }

    if (!ecs_singleton_get(world, Surface)) {
        Surface *s = ecs_singleton_ensure(world, Surface);
        s->width = 1280;
        s->height = 720;
        s->vsync = true;
        s->title = ecs_os_strdup("box");
        ecs_singleton_modified(world, Surface);
    }

    if (box_run(world)) {
        goto error;
    }

    return ecs_fini(world);
error:
    ecs_fini(world);

    return -1;
}

/**
 * @file main.c
 * @brief Entry point of the box application.
 */

#include <box.h>

/** Load the default scene when the command line left the world without a camera.
 *
 * @param world The world
 */
static void box_load_default_scene(
    ecs_world_t *world)
{
    if (!ecs_count_id(world, ecs_id(Camera))) {
        ecs_script(world, { .filename = "etc/scenes/default.flecs" });
    }
}

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
    ECS_IMPORT(world, BoxCommand);
    ECS_IMPORT(world, BoxSurface);
    ECS_IMPORT(world, BoxTransform);
    ECS_IMPORT(world, BoxCamera);
    ECS_IMPORT(world, BoxView);
    ECS_IMPORT(world, BoxLight);
    ECS_IMPORT(world, BoxMaterial);
    ECS_IMPORT(world, BoxRender);
    ECS_IMPORT(world, BoxGeometry);
    ECS_IMPORT(world, BoxTexture);
    ECS_IMPORT(world, BoxMovement);

    if (box_parse_args(world, argc, argv)) {
        goto error;
    }

    box_load_default_scene(world);

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

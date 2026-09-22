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
    ECS_IMPORT(world, Box);
    if (!box_script_load(world, BOX_EFFECTS_BUILTINS) ||
        !box_script_load(world, BOX_MATERIAL_LAYERS_BUILTINS) ||
        !box_script_load(world, BOX_LIGHT_MODULATORS_BUILTINS))
    {
        goto error;
    }

    ecs_singleton_ensure(world, ScriptSettings)->enabled = true;
    ecs_singleton_modified(world, ScriptSettings);

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

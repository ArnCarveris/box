/**
 * @file main.c
 * @brief Runs Yeon with the settings of the command line.
 */

#include "game.h"

int main(
    int argc,
    char *argv[])
{
    ecs_world_t *world = ecs_init();
    int result = 0;

    ECS_IMPORT(world, Box);
    ECS_IMPORT(world, Yeon);

    App *app = ecs_singleton_get_mut(world, App);
    ecs_os_free(app->scene);
    app->scene = ecs_os_strdup(YEON_SCENE);
    ecs_singleton_modified(world, App);

    result = box_app_parse(world, argc, argv);
    if (!result) {
        result = box_app_run(world);
    }

    ecs_fini(world);

    return result ? 1 : 0;
}

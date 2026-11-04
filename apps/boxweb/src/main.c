#include <box.h>

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

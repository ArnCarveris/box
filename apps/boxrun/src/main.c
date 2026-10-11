#include <boxrun.h>

int main(int argc, char *argv[]) {
    ecs_world_t *world = ecs_init();

    ECS_IMPORT(world, Box);

    ecs_log_set_level(0);

    ecs_singleton_set(world, EcsRest, {});

    return ecs_app_run(world, &(ecs_app_desc_t){
        .enable_stats = true,
        .target_fps = 60
    });
}

/**
 * @file Restore.c
 * @brief Benchmarks for the time a restore of a large scene takes.
 */

#include <box.h>
#include <bake_bench.h>

/** Entities the scene of the benchmark holds */
#define BENCH_RESTORE_ENTITIES (500)

void Restore_scene_500(bench_t *b)
{
    // checkpoints/restore
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, BoxCheckpoint);
    ECS_IMPORT(world, BoxTransform);

    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    ecs_strbuf_appendstr(&buf, "using box.transform\n\n");

    int32_t i;
    for (i = 0; i < BENCH_RESTORE_ENTITIES; i ++) {
        ecs_strbuf_append(&buf,
            "e%d {\n  Position: {%d, 0, 0}\n  Rotation: {0, 0, 0}\n}\n", i, i);
    }

    char *code = ecs_strbuf_get(&buf);
    ecs_entity_t script = ecs_script(world, {
        .entity = ecs_entity(world, { .name = "scene" }),
        .code = code
    });

    ecs_os_free(code);
    bench_keep(script);

    char *folder = box_checkpoint_write(world, "bench", NULL);
    bench_keep(folder[0]);
    bench_set_items(b, BENCH_RESTORE_ENTITIES);

    while (bench_iter(b)) {
        char *restored = box_checkpoint_restore(world, "bench", 0, NULL);
        bench_keep(restored[0]);
        ecs_os_free(restored);
    }

    ecs_os_free(folder);
    ecs_fini(world);
}

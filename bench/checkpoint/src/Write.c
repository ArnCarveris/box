/**
 * @file Write.c
 * @brief Benchmarks for the time a checkpoint of a large scene takes.
 */

#include <box.h>
#include <bake_bench.h>

/** Entities the scene of the benchmarks holds */
#define BENCH_CHECKPOINT_ENTITIES (5000)

void Write_scene_5000(
    bench_t *b)
{
    // checkpoints/checkpoint_write
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    ecs_strbuf_appendstr(&buf, "using box.transform\n\n");

    int32_t i;
    for (i = 0; i < BENCH_CHECKPOINT_ENTITIES; i ++) {
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
    bench_set_items(b, BENCH_CHECKPOINT_ENTITIES);

    while (bench_iter(b)) {
        char *folder = box_checkpoint_write(world, "bench", NULL);
        bench_keep(folder[0]);
        ecs_os_free(folder);
    }

    ecs_fini(world);
}

void Write_report_5000(
    bench_t *b)
{
    // checkpoints/checkpoint_write
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    int32_t i;
    for (i = 0; i < BENCH_CHECKPOINT_ENTITIES; i ++) {
        ecs_entity_t e = ecs_new(world);
        ecs_set(world, e, Position, {(float)i, 0, 0});
    }

    bench_set_items(b, BENCH_CHECKPOINT_ENTITIES);

    while (bench_iter(b)) {
        char *report = NULL;
        char *folder = box_checkpoint_write(world, "bench", &report);
        bench_keep(report[0]);
        ecs_os_free(report);
        ecs_os_free(folder);
    }

    ecs_fini(world);
}

void Write_scene_500(
    bench_t *b)
{
    // checkpoints/edit_perf
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    ecs_strbuf_appendstr(&buf, "using box.transform\n\n");

    int32_t i;
    for (i = 0; i < 500; i ++) {
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
    bench_set_items(b, 500);

    while (bench_iter(b)) {
        char *folder = box_checkpoint_write(world, "bench", NULL);
        bench_keep(folder[0]);
        ecs_os_free(folder);
    }

    ecs_fini(world);
}

void Write_scene_50000(
    bench_t *b)
{
    // checkpoints/edit_perf
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, Box);

    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    ecs_strbuf_appendstr(&buf, "using box.transform\n\n");

    int32_t i;
    for (i = 0; i < 50000; i ++) {
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
    bench_set_items(b, 50000);

    while (bench_iter(b)) {
        char *folder = box_checkpoint_write(world, "bench", NULL);
        bench_keep(folder[0]);
        ecs_os_free(folder);
    }

    ecs_fini(world);
}

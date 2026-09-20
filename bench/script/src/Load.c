/**
 * @file Load.c
 * @brief Benchmarks for the time a scene script takes to evaluate.
 */

#include <box.h>
#include <bake_bench.h>

/** Entities the small scene of the benchmarks holds */
#define BENCH_LOAD_ENTITIES (10000)

/** Entities the large scene of the benchmarks holds */
#define BENCH_LOAD_ENTITIES_LARGE (50000)

void Load_scene_10k(bench_t *b)
{
    // responsiveness/script_eval_perf
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    ecs_strbuf_appendstr(&buf, "using box.transform\n\n");

    int32_t i;
    for (i = 0; i < BENCH_LOAD_ENTITIES; i ++) {
        ecs_strbuf_append(&buf,
            "e%d {\n  Position: {%d, 0, 0}\n  Rotation: {0, 0, 0}\n}\n", i, i);
    }

    char *code = ecs_strbuf_get(&buf);
    bench_set_items(b, BENCH_LOAD_ENTITIES);

    while (bench_iter(b)) {
        bench_pause(b);
        ecs_world_t *world = ecs_init();
        ECS_IMPORT(world, Box);
        ecs_entity_t scene = ecs_entity(world, { .name = "scene" });
        bench_resume(b);

        ecs_entity_t script = ecs_script(world, {
            .entity = scene,
            .code = code
        });

        bench_pause(b);
        bench_keep(script);
        ecs_fini(world);
        bench_resume(b);
    }

    ecs_os_free(code);
}

void Load_scene_50k(bench_t *b)
{
    // responsiveness/script_eval_perf
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    ecs_strbuf_appendstr(&buf, "using box.transform\n\n");

    int32_t i;
    for (i = 0; i < BENCH_LOAD_ENTITIES_LARGE; i ++) {
        ecs_strbuf_append(&buf,
            "e%d {\n  Position: {%d, 0, 0}\n  Rotation: {0, 0, 0}\n}\n", i, i);
    }

    char *code = ecs_strbuf_get(&buf);
    bench_set_items(b, BENCH_LOAD_ENTITIES_LARGE);

    while (bench_iter(b)) {
        bench_pause(b);
        ecs_world_t *world = ecs_init();
        ECS_IMPORT(world, Box);
        ecs_entity_t scene = ecs_entity(world, { .name = "scene" });
        bench_resume(b);

        ecs_entity_t script = ecs_script(world, {
            .entity = scene,
            .code = code
        });

        bench_pause(b);
        bench_keep(script);
        ecs_fini(world);
        bench_resume(b);
    }

    ecs_os_free(code);
}

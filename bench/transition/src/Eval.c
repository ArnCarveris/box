/**
 * @file Eval.c
 * @brief Benchmark for the interpolation itself, the work a frame repeats.
 */

#include <box.h>
#include <bake_bench.h>
#include "../../../src/transition/transition_internal.h"
#include "../../../src/renderer/packet.h"

/** Records a scene of ten thousand entities with ten transitions each holds */
#define BENCH_RECORD_COUNT (100000)

void Eval_eval_100k_records(
    bench_t *b)
{
    // transitions/transition_params
    ecs_os_init();

    box_transition_t *records = ecs_os_malloc_n(box_transition_t, BENCH_RECORD_COUNT);
    int32_t i;
    for (i = 0; i < BENCH_RECORD_COUNT; i ++) {
        int32_t j;

        memset(&records[i], 0, sizeof(box_transition_t));
        records[i].start = (double)(i % 60) / 60.0;
        records[i].duration = 1.0f;
        records[i].timing = i % 2 ? EaseInOut : Linear;
        records[i].count = 3;
        for (j = 0; j < 3; j ++) {
            records[i].from[j] = (float)j;
            records[i].to[j] = (float)(j + 10);
            records[i].slots[j].offset = (uint16_t)(j * 4);
            records[i].slots[j].kind = BoxTransitionF32;
        }
    }

    double time = 0;

    bench_set_items(b, BENCH_RECORD_COUNT);

    while (bench_iter(b)) {
        float out[BOX_TRANSITION_MAX];
        float sum = 0;

        time += 1.0 / 60.0;
        for (i = 0; i < BENCH_RECORD_COUNT; i ++) {
            box_transition_eval(&records[i], time, out);
            sum += out[0];
        }

        bench_keep((int32_t)sum);
    }

    ecs_os_free(records);
}

/** Instances a scene of ten thousand transitioning entities holds */
#define BENCH_INSTANCE_COUNT (10000)

/** Channels of an instance the benchmark moves at once */
#define BENCH_CHANNEL_COUNT (6)

/** Instances the churn of the benchmark scene restarts every frame */
#define BENCH_RESTART_COUNT (167)

void Eval_fill_10k_instances(
    bench_t *b)
{
    // transitions/renderer_eval
    ecs_os_init();

    box_transition_cmd_t *cmds = ecs_os_malloc_n(box_transition_cmd_t, BENCH_INSTANCE_COUNT);
    box_transition_rec_t *records = ecs_os_malloc_n(box_transition_rec_t,
        BENCH_INSTANCE_COUNT * BENCH_CHANNEL_COUNT);
    box_instance_t *instances = ecs_os_malloc_n(box_instance_t, BENCH_INSTANCE_COUNT);
    int32_t i;
    for (i = 0; i < BENCH_INSTANCE_COUNT; i ++) {
        box_transition_cmd_t *cmd = &cmds[i];
        int32_t channel;

        memset(cmd, 0, sizeof(box_transition_cmd_t));
        memset(&instances[i], 0, sizeof(box_instance_t));
        cmd->count = BENCH_CHANNEL_COUNT;
        cmd->first = (uint32_t)(i * BENCH_CHANNEL_COUNT);
        cmd->posed = 1;
        cmd->present = (1u << BENCH_CHANNEL_COUNT) - 1u;
        cmd->parent[0] = cmd->parent[5] = cmd->parent[10] = cmd->parent[15] = 1.0f;
        cmd->mesh_scale[0] = cmd->mesh_scale[1] = cmd->mesh_scale[2] = 1.0f;
        cmd->bounds_max[0] = cmd->bounds_max[1] = cmd->bounds_max[2] = 0.5f;
        cmd->bounds_min[0] = cmd->bounds_min[1] = cmd->bounds_min[2] = -0.5f;
        for (channel = 0; channel < BENCH_CHANNEL_COUNT; channel ++) {
            box_transition_rec_t *rec = &records[cmd->first + channel];
            int32_t j;

            memset(rec, 0, sizeof(box_transition_rec_t));
            rec->channel = (uint32_t)channel;
            rec->count = 3;
            rec->duration = 1.0f;
            rec->timing = (i % 2) ? BOX_TIMING_EASE_IN_OUT : BOX_TIMING_LINEAR;
            rec->start = (double)(i % 60) / 60.0;
            for (j = 0; j < 3; j ++) {
                rec->from[j] = (float)j;
                rec->to[j] = (float)(j + 10);
            }
        }
    }

    double time = 0;

    bench_set_items(b, BENCH_INSTANCE_COUNT);

    while (bench_iter(b)) {
        int32_t running = 0;

        time += 1.0 / 60.0;
        for (i = 0; i < BENCH_INSTANCE_COUNT; i ++) {
            running += box_transition_fill(&cmds[i], &records[cmds[i].first], time,
                &instances[i]);
        }

        bench_keep(running);
    }

    ecs_os_free(instances);
    ecs_os_free(records);
    ecs_os_free(cmds);
}

/** Build the command and the records of one instance of the pool benchmark.
 *
 * @param index The instance
 * @param cmd Receives the command
 * @param records Receives BENCH_CHANNEL_COUNT records
 */
static void Eval_pool_instance(
    int32_t index,
    box_transition_cmd_t *cmd,
    box_transition_rec_t *records)
{
    memset(cmd, 0, sizeof(box_transition_cmd_t));
    cmd->count = BENCH_CHANNEL_COUNT;
    cmd->posed = 1;
    cmd->present = (1u << BENCH_CHANNEL_COUNT) - 1u;
    cmd->parent[0] = cmd->parent[5] = cmd->parent[10] = cmd->parent[15] = 1.0f;
    cmd->mesh_scale[0] = cmd->mesh_scale[1] = cmd->mesh_scale[2] = 1.0f;
    cmd->bounds_max[0] = cmd->bounds_max[1] = cmd->bounds_max[2] = 0.5f;
    cmd->bounds_min[0] = cmd->bounds_min[1] = cmd->bounds_min[2] = -0.5f;

    int32_t channel;
    for (channel = 0; channel < BENCH_CHANNEL_COUNT; channel ++) {
        box_transition_rec_t *rec = &records[channel];
        int32_t j;

        memset(rec, 0, sizeof(box_transition_rec_t));
        rec->channel = (uint32_t)channel;
        rec->count = 3;
        rec->duration = 8.0f;
        rec->delay = (float)(index % 60) / 60.0f;
        rec->timing = (index % 2) ? BOX_TIMING_EASE_IN_OUT : BOX_TIMING_LINEAR;
        rec->start = 0;
        for (j = 0; j < 3; j ++) {
            rec->from[j] = (float)j;
            rec->to[j] = (float)(j + 10);
        }
    }
}

void Eval_pool_10k_instances(
    bench_t *b)
{
    // transitions/scale
    ecs_os_init();

    box_transition_pool_t pool;
    box_transition_cmd_t cmd;
    box_transition_rec_t records[BENCH_CHANNEL_COUNT];
    box_instance_t base;

    memset(&base, 0, sizeof(base));
    box_transition_pool_init(&pool, (uint32_t)sizeof(box_instance_t));

    int32_t i;
    for (i = 0; i < BENCH_INSTANCE_COUNT; i ++) {
        Eval_pool_instance(i, &cmd, records);
        box_transition_pool_start(&pool, BOX_TRANSITION_NONE, &cmd, records, &base);
    }

    double time = 0;

    bench_set_items(b, BENCH_INSTANCE_COUNT);

    while (bench_iter(b)) {
        uint32_t running = 0;

        time += 1.0 / 60.0;
        box_transition_pool_eval(&pool, time);

        uint32_t j;
        for (j = 0; j < pool.entries.count; j ++) {
            running += box_transition_pool_compose(&pool, j);
        }

        bench_keep((int32_t)running);
    }

    box_transition_pool_fini(&pool);
}

void Eval_pool_restart_167_of_10k(
    bench_t *b)
{
    // transitions/scale
    ecs_os_init();

    box_transition_pool_t pool;
    box_transition_cmd_t cmd;
    box_transition_rec_t records[BENCH_CHANNEL_COUNT];
    box_instance_t base;

    memset(&base, 0, sizeof(base));
    box_transition_pool_init(&pool, (uint32_t)sizeof(box_instance_t));

    int32_t i;
    for (i = 0; i < BENCH_INSTANCE_COUNT; i ++) {
        Eval_pool_instance(i, &cmd, records);
        box_transition_pool_start(&pool, BOX_TRANSITION_NONE, &cmd, records, &base);
    }

    uint32_t next = 0;

    bench_set_items(b, BENCH_RESTART_COUNT);

    while (bench_iter(b)) {
        uint32_t j;
        for (j = 0; j < BENCH_RESTART_COUNT; j ++) {
            next = (next + 137) % BENCH_INSTANCE_COUNT;
            Eval_pool_instance((int32_t)next, &cmd, records);
            box_transition_pool_start(&pool, next, &cmd, records, &base);
        }

        bench_keep((int32_t)pool.entries.count);
    }

    box_transition_pool_fini(&pool);
}

void Eval_pool_eval_60k_records(
    bench_t *b)
{
    // transitions/scale
    ecs_os_init();

    box_transition_pool_t pool;
    box_transition_cmd_t cmd;
    box_transition_rec_t records[BENCH_CHANNEL_COUNT];
    box_instance_t base;

    memset(&base, 0, sizeof(base));
    box_transition_pool_init(&pool, (uint32_t)sizeof(box_instance_t));

    int32_t i;
    for (i = 0; i < BENCH_INSTANCE_COUNT; i ++) {
        Eval_pool_instance(i, &cmd, records);
        box_transition_pool_start(&pool, BOX_TRANSITION_NONE, &cmd, records, &base);
    }

    double time = 0;

    bench_set_items(b, BENCH_INSTANCE_COUNT * BENCH_CHANNEL_COUNT);

    while (bench_iter(b)) {
        time += 1.0 / 60.0;
        box_transition_pool_eval(&pool, time);
        bench_keep((int32_t)box_transition_pool_entry(&pool, 0)->running);
    }

    box_transition_pool_fini(&pool);
}

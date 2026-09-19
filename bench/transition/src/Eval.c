/**
 * @file Eval.c
 * @brief Benchmark for the interpolation itself, the work a frame repeats.
 */

#include <box.h>
#include <bake_bench.h>
#include "../../../src/transition/transition_internal.h"

/** Records a scene of ten thousand entities with ten transitions each holds */
#define BENCH_RECORD_COUNT (100000)

void Eval_eval_100k_records(bench_t *b)
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

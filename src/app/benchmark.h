/**
 * @file app/benchmark.h
 * @brief The benchmark of App.benchmark: steady state frame times and thread loads of a scene.
 */

#ifndef BOX_APP_BENCHMARK_H
#define BOX_APP_BENCHMARK_H

#include <box.h>

/** Register the system that settles, measures and reports the benchmark of App.benchmark.
 *
 * @param world The world
 */
void box_app_benchmark_init(
    ecs_world_t *world);

/** Check how the benchmark of a run ended.
 *
 * @param world The world
 * @return 0 without a benchmark or when it reported, nonzero when it failed or did not finish
 */
int box_app_benchmark_end(
    ecs_world_t *world);

#endif

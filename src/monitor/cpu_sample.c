/**
 * @file monitor/cpu_sample.c
 * @brief Publishes the CPU measurement primitives of box.core as the CpuMonitor.
 *
 * The primitives live in src/core/cpu.c because the CPU budget uses them too,
 * and this file turns a pair of readings into the moving averages of the
 * component.
 */

#include "monitor_internal.h"
#include <math.h>

/** Blend a reading into a moving average that covers a window.
 *
 * @param avg The average so far
 * @param value The reading
 * @param dt Seconds since the previous reading
 * @param window Seconds the average covers
 * @return The updated average
 */
static double box_cpu_blend(
    double avg,
    double value,
    double dt,
    double window)
{
    double alpha = 1.0;
    if (window > 0) {
        alpha = 1.0 - exp(-dt / window);
    }

    return avg + alpha * (value - avg);
}

/** Percentage of the busiest core between two readings.
 *
 * @param now The new reading
 * @param last The previous reading
 * @return Percent the busiest core spent working
 */
static double box_cpu_peak(
    const box_cpu_sample_t *now,
    const box_cpu_sample_t *last)
{
    double peak = 0;
    int32_t i;
    for (i = 0; i < now->cores; i ++) {
        double total = now->core_total[i] - last->core_total[i];
        if (total <= 0) {
            continue;
        }

        double busy = (now->core_busy[i] - last->core_busy[i]) / total;
        if (busy > peak) {
            peak = busy;
        }
    }

    return peak * 100.0;
}

/** Sample the CPU and publish the moving averages on the CpuMonitor singleton.
 *
 * @param it Iterator over the CpuMonitor singleton
 */
static void BoxSampleCpu(
    ecs_iter_t *it)
{
    CpuMonitor *m = ecs_field(it, CpuMonitor, 0);
    box_cpu_monitor_t *ctx = it->ctx;
    box_cpu_sample_t now;
    box_cpu_sample(&now);

    m->cores = now.cores;
    m->budget_sleep_ms = box_cpu_sleep_ms();

    double window = m->window_s > 0 ? m->window_s : BOX_MONITOR_WINDOW;
    double dt = now.wall_time - ctx->last.wall_time;
    if (!ctx->sampled || dt <= 0) {
        ctx->last = now;
        ctx->sampled = true;
        goto done;
    }

    double process = (now.process_time - ctx->last.process_time) / dt * 100.0;
    m->process_percent = box_cpu_blend(m->process_percent, process, dt, window);

    double total = now.total_time - ctx->last.total_time;
    if (total > 0) {
        double busy = (now.busy_time - ctx->last.busy_time) / total * 100.0;
        m->system_percent = box_cpu_blend(m->system_percent, busy, dt, window);
    }

    double peak = box_cpu_peak(&now, &ctx->last);
    m->core_percent_max = box_cpu_blend(m->core_percent_max, peak, dt, window);
    ctx->last = now;
done:
    return;
}

/** Free the moving average state of the sampling system.
 *
 * @param ptr The state
 */
static void box_cpu_monitor_free(
    void *ptr)
{
    ecs_os_free(ptr);
}

void box_cpu_monitor_init(
    ecs_world_t *world)
{
    box_cpu_monitor_t *ctx = ecs_os_calloc_t(box_cpu_monitor_t);
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BoxSampleCpu" }),
        .phase = EcsOnLoad,
        .query.terms = {{ .id = ecs_id(CpuMonitor), .src.id = ecs_id(CpuMonitor) }},
        .callback = BoxSampleCpu,
        .ctx = ctx,
        .ctx_free = box_cpu_monitor_free
    });
}

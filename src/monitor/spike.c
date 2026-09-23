/**
 * @file monitor/spike.c
 * @brief Frame time collector that finds the frames above a spike budget.
 *
 * A harness records one frame per step with its slowest system. The evaluation
 * reports the mean, percentiles and the frames over budget, worst first, with
 * what each spent its time in.
 */

#include <stdlib.h>
#include "monitor_internal.h"

/** Compare two frame times for qsort.
 *
 * @param a First frame time
 * @param b Second frame time
 * @return Ordering of the two values
 */
static int box_spike_compare(
    const void *a,
    const void *b)
{
    double da = *(const double*)a, db = *(const double*)b;

    return (da > db) - (da < db);
}

/** Insert a frame into a list that keeps the worst entries, worst first.
 *
 * @param worst The list
 * @param count Entries of the list
 * @param frame The frame to insert
 */
static void box_spike_insert(
    box_spike_frame_t *worst,
    int32_t count,
    const box_spike_frame_t *frame)
{
    int32_t i, slot = -1;
    for (i = 0; i < count; i ++) {
        if (frame->time_ms > worst[i].time_ms) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        goto done;
    }

    for (i = count - 1; i > slot; i --) {
        worst[i] = worst[i - 1];
    }

    worst[slot] = *frame;
done:
    return;
}

/** Fill the distribution of a run.
 *
 * @param spikes The collector
 * @param report The summary to fill
 */
static void box_spike_distribution(
    const box_spikes_t *spikes,
    box_spike_report_t *report)
{
    int32_t count = spikes->count, i;
    double *sorted = ecs_os_malloc_n(double, count);
    double sum = 0;
    for (i = 0; i < count; i ++) {
        sorted[i] = spikes->frames[i].time_ms;
        sum += sorted[i];
    }

    qsort(sorted, (size_t)count, sizeof(double), box_spike_compare);
    report->mean_ms = sum / count;
    report->p50_ms = sorted[count / 2];
    report->p99_ms = sorted[(count * 99) / 100];
    report->max_ms = sorted[count - 1];
    ecs_os_free(sorted);
}

void box_spikes_init(
    box_spikes_t *spikes,
    int32_t capacity,
    double factor)
{
    ecs_os_memset_t(spikes, 0, box_spikes_t);
    spikes->factor = factor > 0 ? factor : 0;
    spikes->capacity = capacity > 0 ? capacity : 1;
    spikes->frames = ecs_os_calloc_n(box_spike_frame_t, spikes->capacity);
}

void box_spikes_fini(
    box_spikes_t *spikes)
{
    ecs_os_free(spikes->frames);
    spikes->frames = NULL;
    spikes->count = 0;
    spikes->capacity = 0;
}

void box_spikes_add(
    box_spikes_t *spikes,
    double time_ms,
    ecs_entity_t system,
    double system_ms)
{
    if (spikes->count == spikes->capacity) {
        spikes->capacity *= 2;
        spikes->frames = ecs_os_realloc_n(
            spikes->frames, box_spike_frame_t, spikes->capacity);
    }

    box_spike_frame_t *frame = &spikes->frames[spikes->count];
    frame->frame = spikes->count;
    frame->time_ms = time_ms;
    frame->system = system;
    frame->system_ms = system_ms;
    spikes->count ++;
}

void box_spikes_eval(
    const box_spikes_t *spikes,
    box_spike_report_t *report)
{
    ecs_os_memset_t(report, 0, box_spike_report_t);
    if (!spikes->count) {
        goto done;
    }

    box_spike_distribution(spikes, report);
    if (spikes->factor <= 0) {
        goto done;
    }

    report->budget_ms = report->mean_ms * spikes->factor;
    int32_t i;
    for (i = 0; i < spikes->count; i ++) {
        if (spikes->frames[i].time_ms <= report->budget_ms) {
            continue;
        }

        report->count ++;
        box_spike_insert(report->worst, BOX_SPIKE_MAX, &spikes->frames[i]);
    }
done:
    return;
}

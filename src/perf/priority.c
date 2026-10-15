/**
 * @file perf/priority.c
 * @brief Real-time schedule of the thread that paces a performance run.
 *
 * On macOS the pacing thread gets a time-constraint policy for the duration of a run, so it wakes
 * on time from its sleeps on a loaded machine; on Linux SCHED_FIFO when the user may raise it.
 * Other platforms keep their schedule and rely on the wake up latency the pacer learns.
 */

#include "perf.h"

#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)

#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <pthread.h>

/** Convert seconds to mach absolute time units.
 *
 * @param seconds The duration
 * @return The duration in mach absolute time units
 */
static uint32_t box_perf_abs(
    double seconds)
{
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);

    return (uint32_t)(seconds * 1e9 * (double)tb.denom / (double)tb.numer);
}

bool box_perf_priority_set(
    box_perf_priority_t *priority,
    double interval,
    double computation)
{
    mach_port_t thread = pthread_mach_thread_np(pthread_self());
    double c = computation < BOX_PERF_MIN_COMPUTATION ? BOX_PERF_MIN_COMPUTATION : computation;
    thread_time_constraint_policy_data_t policy;
    kern_return_t kr;
    if (priority->active && priority->thread != thread) {
        box_perf_priority_reset(priority);
    }

    c = c > interval * BOX_PERF_MAX_COMPUTATION ? interval * BOX_PERF_MAX_COMPUTATION : c;
    policy.period = box_perf_abs(interval);
    policy.computation = box_perf_abs(c);
    policy.constraint = box_perf_abs(interval);
    policy.preemptible = TRUE;
    kr = thread_policy_set(thread, THREAD_TIME_CONSTRAINT_POLICY,
        (thread_policy_t)&policy, THREAD_TIME_CONSTRAINT_POLICY_COUNT);
    if (kr == KERN_SUCCESS) {
        priority->active = true;
        priority->thread = thread;
    } else {
        ecs_warn("perf: no time-constraint policy for the pacing thread (%d)", kr);
    }

    return kr == KERN_SUCCESS;
}

void box_perf_priority_reset(
    box_perf_priority_t *priority)
{
    thread_standard_policy_data_t policy = { 0 };
    if (priority->active) {
        thread_policy_set((mach_port_t)priority->thread, THREAD_STANDARD_POLICY,
            (thread_policy_t)&policy, THREAD_STANDARD_POLICY_COUNT);
        priority->active = false;
        priority->thread = 0;
    }
}

#elif defined(__linux__)

#include <pthread.h>
#include <sched.h>

bool box_perf_priority_set(
    box_perf_priority_t *priority,
    double interval,
    double computation)
{
    struct sched_param param = { .sched_priority = sched_get_priority_min(SCHED_FIFO) };
    int err;
    (void)interval;
    (void)computation;
    if (priority->active) {
        goto done;
    }

    err = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
    if (!err) {
        priority->active = true;
        priority->thread = (uint64_t)pthread_self();
    } else {
        ecs_warn("perf: no SCHED_FIFO for the pacing thread (%d)", err);
    }

done:
    return priority->active;
}

void box_perf_priority_reset(
    box_perf_priority_t *priority)
{
    struct sched_param param = { .sched_priority = 0 };
    if (priority->active) {
        pthread_setschedparam((pthread_t)priority->thread, SCHED_OTHER, &param);
        priority->active = false;
        priority->thread = 0;
    }
}

#else

bool box_perf_priority_set(
    box_perf_priority_t *priority,
    double interval,
    double computation)
{
    (void)priority;
    (void)interval;
    (void)computation;

    return false;
}

void box_perf_priority_reset(
    box_perf_priority_t *priority)
{
    priority->active = false;
}

#endif

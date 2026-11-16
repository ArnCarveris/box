/**
 * @file app/load.c
 * @brief Thread clocks: the seconds a thread worked and waited, and the clock bound to every
 * thread.
 */

#include "load.h"

#if defined(_MSC_VER)
/** Storage of a variable of which every thread has its own */
#define BOX_LOAD_THREAD_LOCAL __declspec(thread)
#else
/** Storage of a variable of which every thread has its own */
#define BOX_LOAD_THREAD_LOCAL _Thread_local
#endif

/** The clock bound to the calling thread, NULL for none */
static BOX_LOAD_THREAD_LOCAL box_load_clock_t *box_load_current;

/** Read the clock the frame loop measures with.
 *
 * @return Seconds on the clock of ecs_os_get_time
 */
static double box_load_now(void)
{
    ecs_time_t t;
    ecs_os_get_time(&t);

    return (double)t.sec + (double)t.nanosec * 1e-9;
}

/** Add the time since the state began to the total of the state, called with the lock held.
 *
 * @param clock The clock
 * @param now Seconds on the clock of ecs_os_get_time
 * @param busy Receives the seconds busy up to now
 * @param wait Receives the seconds waiting up to now
 */
static void box_load_clock_totals(
    const box_load_clock_t *clock,
    double now,
    double *busy,
    double *wait)
{
    double span = now > clock->since ? now - clock->since : 0;
    *busy = clock->busy + (clock->state == BOX_LOAD_BUSY ? span : 0);
    *wait = clock->wait + (clock->state == BOX_LOAD_WAIT ? span : 0);
}

void box_load_clock_init(
    box_load_clock_t *clock)
{
    ecs_os_memset_t(clock, 0, box_load_clock_t);
    clock->lock = ecs_os_mutex_new();
    clock->since = box_load_now();
}

void box_load_clock_fini(
    box_load_clock_t *clock)
{
    if (clock->lock) {
        ecs_os_mutex_free(clock->lock);
    }

    clock->lock = 0;
}

int32_t box_load_clock_enter(
    box_load_clock_t *clock,
    int32_t state)
{
    double now = box_load_now();
    int32_t result;
    ecs_os_mutex_lock(clock->lock);
    result = clock->state;
    box_load_clock_totals(clock, now, &clock->busy, &clock->wait);
    clock->state = state;
    clock->since = now;
    ecs_os_mutex_unlock(clock->lock);

    return result;
}

void box_load_clock_read(
    box_load_clock_t *clock,
    double *busy,
    double *wait)
{
    double now = box_load_now();
    ecs_os_mutex_lock(clock->lock);
    box_load_clock_totals(clock, now, busy, wait);
    ecs_os_mutex_unlock(clock->lock);
}

box_load_bound_t box_load_bind(
    box_load_clock_t *clock)
{
    box_load_bound_t result = { box_load_current, BOX_LOAD_IDLE };
    if (result.clock) {
        result.state = box_load_clock_enter(result.clock, BOX_LOAD_IDLE);
    }

    box_load_current = clock;

    return result;
}

void box_load_restore(
    box_load_bound_t prev)
{
    box_load_current = prev.clock;
    if (prev.clock) {
        box_load_clock_enter(prev.clock, prev.state);
    }
}

int32_t box_load_wait(void)
{
    int32_t result = -1;
    if (box_load_current) {
        result = box_load_clock_enter(box_load_current, BOX_LOAD_WAIT);
    }

    return result;
}

void box_load_resume(
    int32_t prev)
{
    if (box_load_current && prev >= 0) {
        box_load_clock_enter(box_load_current, prev);
    }
}

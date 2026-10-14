/**
 * @file fake.h
 * @brief Deterministic clock and frame cost callbacks shared by the perf tests.
 */

#ifndef BOX_PERF_TEST_FAKE_H
#define BOX_PERF_TEST_FAKE_H

#include <box.h>

extern double (*box_perf_clock)(void);
extern void (*box_perf_sleep)(double seconds);

/** Seconds on the fake clock */
extern double fake_time;

/** Milliseconds every frame adds to the fake clock through the FakeWork system */
extern double fake_work_ms;

/** Milliseconds every frame adds to AppStatus.slept through the FakeWork system */
extern double fake_slept_ms;

/** Read the fake clock, every read advances it by one microsecond.
 *
 * @return The fake time in seconds
 */
double fake_now(void);

/** Sleep on the fake clock.
 *
 * @param seconds Seconds to advance the fake clock by
 */
void fake_sleep(
    double seconds);

/** System that spends fake_work_ms of fake time and reports fake_slept_ms of budget sleep.
 *
 * @param it The iterator of the system
 */
void fake_work(
    ecs_iter_t *it);

#endif

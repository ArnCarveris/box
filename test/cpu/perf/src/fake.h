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

/** Sleep on the fake clock like a busy machine: with 50% timer leeway and a millisecond of
 * wake up latency.
 *
 * @param seconds Seconds asked for
 */
void fake_sleep_late(
    double seconds);

/** OS clock callback that reads the fake clock without advancing it, for ecs_os_api.now_.
 *
 * @return The fake time in nanoseconds
 */
uint64_t fake_os_now(void);

/** OS sleep callback that advances the fake clock, for ecs_os_api.sleep_.
 *
 * @param sec Seconds to sleep
 * @param nanosec Nanoseconds to sleep
 */
void fake_os_sleep(
    int32_t sec,
    int32_t nanosec);

/** System that spends fake_work_ms of fake time and reports fake_slept_ms of budget sleep.
 *
 * @param it The iterator of the system
 */
void fake_work(
    ecs_iter_t *it);

#endif

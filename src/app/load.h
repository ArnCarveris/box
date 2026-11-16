/**
 * @file app/load.h
 * @brief Thread clocks: the seconds a thread of box worked and waited, which the load panel
 * samples every frame.
 *
 * A clock is bound to the thread it measures. Code that blocks, such as a wait for the GPU or for
 * a free render packet, marks the clock of its thread as waiting without knowing which it is.
 */

#ifndef BOX_APP_LOAD_H
#define BOX_APP_LOAD_H

#include <box.h>

/** State of a thread that counts toward neither busy nor wait */
#define BOX_LOAD_IDLE (0)

/** State of a thread that works */
#define BOX_LOAD_BUSY (1)

/** State of a thread that blocks on another thread or the GPU */
#define BOX_LOAD_WAIT (2)

/** The seconds a thread spent busy and waiting, read from any thread */
typedef struct box_load_clock_t {
    ecs_os_mutex_t lock;    /**< Guards the fields below */
    int32_t state;          /**< BOX_LOAD_IDLE, BOX_LOAD_BUSY or BOX_LOAD_WAIT */
    double since;           /**< Seconds on the clock of ecs_os_get_time the state began */
    double busy;            /**< Seconds busy before since */
    double wait;            /**< Seconds waiting before since */
} box_load_clock_t;

/** The clock bound to a thread and its state, which box_load_bind returns to restore */
typedef struct box_load_bound_t {
    box_load_clock_t *clock;    /**< The clock, NULL for none */
    int32_t state;              /**< Its state when it was replaced */
} box_load_bound_t;

/** Create a clock, idle.
 *
 * @param clock The clock
 */
void box_load_clock_init(
    box_load_clock_t *clock);

/** Free a clock.
 *
 * @param clock The clock
 */
void box_load_clock_fini(
    box_load_clock_t *clock);

/** Change the state of a clock, adding the time of the state it leaves to its totals.
 *
 * @param clock The clock
 * @param state The new state
 * @return The state it left
 */
int32_t box_load_clock_enter(
    box_load_clock_t *clock,
    int32_t state);

/** Read the seconds a clock spent busy and waiting, the current state up to now included.
 *
 * @param clock The clock
 * @param busy Receives the seconds busy
 * @param wait Receives the seconds waiting
 */
void box_load_clock_read(
    box_load_clock_t *clock,
    double *busy,
    double *wait);

/** Bind a clock to the calling thread. The clock bound before goes idle until it is restored,
 * so time the thread spends for another clock counts once.
 *
 * @param clock The clock, NULL to bind none
 * @return The clock bound before and its state, for box_load_restore
 */
box_load_bound_t box_load_bind(
    box_load_clock_t *clock);

/** Put back the clock box_load_bind replaced, in the state it had.
 *
 * @param prev What box_load_bind returned
 */
void box_load_restore(
    box_load_bound_t prev);

/** Mark the clock of the calling thread as waiting, when one is bound.
 *
 * @return The state it left, for box_load_resume, -1 without a clock
 */
int32_t box_load_wait(void);

/** End a wait of box_load_wait: the clock of the calling thread goes back to its state.
 *
 * @param prev What box_load_wait returned
 */
void box_load_resume(
    int32_t prev);

#endif

/**
 * @file platform/win32/pthread.h
 * @brief The subset of POSIX threads box uses, on Win32 threads, SRW locks
 * and condition variables.
 */

#ifndef BOX_WIN32_PTHREAD_H
#define BOX_WIN32_PTHREAD_H

#include "box_win32.h"

/** Function a thread runs */
typedef void* (*box_win32_thread_fn)(
    void *arg);

/** A thread handle */
typedef struct box_win32_thread_t *pthread_t;

/** A mutex, laid out as an SRWLOCK */
typedef struct pthread_mutex_t {
    void *ptr; /**< SRWLOCK state */
} pthread_mutex_t;

/** A condition variable, laid out as a CONDITION_VARIABLE */
typedef struct pthread_cond_t {
    void *ptr; /**< CONDITION_VARIABLE state */
} pthread_cond_t;

/** Unused thread, mutex and condition attributes */
typedef void pthread_attr_t;

/** Unused mutex attributes */
typedef void pthread_mutexattr_t;

/** Unused condition attributes */
typedef void pthread_condattr_t;

#define PTHREAD_MUTEX_INITIALIZER {0}
#define PTHREAD_COND_INITIALIZER {0}

/** Start a thread.
 *
 * @param thread Receives the handle
 * @param attr Ignored
 * @param fn The thread function
 * @param arg Argument of the thread function
 * @return 0 on success, non zero on failure
 */
int pthread_create(
    pthread_t *thread,
    const pthread_attr_t *attr,
    box_win32_thread_fn fn,
    void *arg);

/** Wait for a thread to end and release it.
 *
 * @param thread The thread
 * @param result Receives the return value of the thread function, may be NULL
 * @return 0
 */
int pthread_join(
    pthread_t thread,
    void **result);

/** Initialize a mutex.
 *
 * @param m The mutex
 * @param attr Ignored
 * @return 0
 */
int pthread_mutex_init(
    pthread_mutex_t *m,
    const pthread_mutexattr_t *attr);

/** Destroy a mutex.
 *
 * @param m The mutex
 * @return 0
 */
int pthread_mutex_destroy(
    pthread_mutex_t *m);

/** Lock a mutex.
 *
 * @param m The mutex
 * @return 0
 */
int pthread_mutex_lock(
    pthread_mutex_t *m);

/** Unlock a mutex.
 *
 * @param m The mutex
 * @return 0
 */
int pthread_mutex_unlock(
    pthread_mutex_t *m);

/** Initialize a condition variable.
 *
 * @param c The condition
 * @param attr Ignored
 * @return 0
 */
int pthread_cond_init(
    pthread_cond_t *c,
    const pthread_condattr_t *attr);

/** Destroy a condition variable.
 *
 * @param c The condition
 * @return 0
 */
int pthread_cond_destroy(
    pthread_cond_t *c);

/** Wait on a condition variable.
 *
 * @param c The condition
 * @param m The locked mutex, released while waiting
 * @return 0
 */
int pthread_cond_wait(
    pthread_cond_t *c,
    pthread_mutex_t *m);

/** Wake one waiter of a condition variable.
 *
 * @param c The condition
 * @return 0
 */
int pthread_cond_signal(
    pthread_cond_t *c);

/** Wake every waiter of a condition variable.
 *
 * @param c The condition
 * @return 0
 */
int pthread_cond_broadcast(
    pthread_cond_t *c);

#endif

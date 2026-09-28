/**
 * @file platform/win32/sys/resource.h
 * @brief Process CPU times of POSIX, implemented with GetProcessTimes.
 */

#ifndef BOX_WIN32_SYS_RESOURCE_H
#define BOX_WIN32_SYS_RESOURCE_H

#include "../box_win32.h"

#define RUSAGE_SELF (0)

/** Seconds and microseconds of a CPU time */
struct box_win32_time {
    long tv_sec;  /**< Whole seconds */
    long tv_usec; /**< Microseconds */
};

/** CPU times of a process */
struct rusage {
    struct box_win32_time ru_utime; /**< Time spent in user mode */
    struct box_win32_time ru_stime; /**< Time spent in kernel mode */
};

/** Get the CPU times of the current process.
 *
 * @param who Only RUSAGE_SELF is supported
 * @param usage Receives the times
 * @return 0 on success, -1 on failure
 */
int getrusage(
    int who,
    struct rusage *usage);

#endif

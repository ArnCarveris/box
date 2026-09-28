/**
 * @file core/cpu.c
 * @brief CPU measurement primitives, the oversubscription check and budget pacing.
 *
 * The monitor module publishes the same primitives as a component, so the budget
 * and the CpuMonitor measure the process with one piece of code.
 */

#include "core_internal.h"
#include <unistd.h>
#include <sys/resource.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <mach/processor_info.h>
#endif

/** Milliseconds the last budget pace slept, read by the CPU monitor */
static double box_cpu_slept_ms = 0;

double box_cpu_process_time(void)
{
#ifdef _WIN32
    double result = box_win32_process_time();
#else
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    double result = (double)usage.ru_utime.tv_sec + (double)usage.ru_utime.tv_usec * 1e-6 +
        (double)usage.ru_stime.tv_sec + (double)usage.ru_stime.tv_usec * 1e-6;
#endif

    return result;
}

double box_cpu_wall_time(void)
{
    ecs_time_t t;
    ecs_os_get_time(&t);

    return (double)t.sec + (double)t.nanosec * 1e-9;
}

int32_t box_cpu_cores(void)
{
    int32_t result = 1;
#ifndef BOX_PLATFORM_WEB
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (cores > 1) {
        result = (int32_t)cores;
    }
#endif

    return result;
}

#ifdef __APPLE__
/** Read busy and total ticks of every core from the mach host.
 *
 * @param sample Receives the machine totals and the per core totals
 */
static void box_cpu_host(
    box_cpu_sample_t *sample)
{
    natural_t count = 0;
    processor_info_array_t info = NULL;
    mach_msg_type_number_t len = 0;
    if (host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO,
        &count, &info, &len) != KERN_SUCCESS)
    {
        goto done;
    }

    processor_cpu_load_info_t load = (processor_cpu_load_info_t)info;
    int32_t i, cores = (int32_t)count;
    if (cores > BOX_CPU_CORE_MAX) {
        cores = BOX_CPU_CORE_MAX;
    }

    for (i = 0; i < cores; i ++) {
        double idle = (double)load[i].cpu_ticks[CPU_STATE_IDLE];
        double total = idle +
            (double)load[i].cpu_ticks[CPU_STATE_USER] +
            (double)load[i].cpu_ticks[CPU_STATE_SYSTEM] +
            (double)load[i].cpu_ticks[CPU_STATE_NICE];
        sample->core_busy[i] = total - idle;
        sample->core_total[i] = total;
        sample->busy_time += total - idle;
        sample->total_time += total;
    }

    sample->cores = cores;
    vm_deallocate(mach_task_self(), (vm_address_t)info,
        (vm_size_t)len * sizeof(int));
done:
    return;
}
#elif !defined(BOX_PLATFORM_WEB)
/** Read busy and total jiffies of every core from /proc/stat.
 *
 * @param sample Receives the machine totals and the per core totals
 */
static void box_cpu_host(
    box_cpu_sample_t *sample)
{
    FILE *f = fopen("/proc/stat", "r");
    if (!f) {
        goto done;
    }

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] != 'c' || line[1] != 'p' || line[2] != 'u') {
            continue;
        }

        double v[8] = {0};
        int32_t core = -1;
        const char *ptr = &line[3];
        if (*ptr != ' ') {
            core = atoi(ptr);
        }

        while (*ptr && *ptr != ' ') {
            ptr ++;
        }

        int32_t i, fields = sscanf(ptr, "%lf %lf %lf %lf %lf %lf %lf %lf",
            &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]);
        double total = 0;
        for (i = 0; i < fields; i ++) {
            total += v[i];
        }

        double busy = total - v[3] - v[4];
        if (core < 0) {
            sample->busy_time = busy;
            sample->total_time = total;
        } else if (core < BOX_CPU_CORE_MAX) {
            sample->core_busy[core] = busy;
            sample->core_total[core] = total;
            sample->cores = core + 1;
        }
    }

    fclose(f);
done:
    return;
}
#else
/** Report no machine load, which the web platform does not expose.
 *
 * @param sample Receives the machine totals and the per core totals
 */
static void box_cpu_host(
    box_cpu_sample_t *sample)
{
    (void)sample;
}
#endif

void box_cpu_sample(
    box_cpu_sample_t *sample)
{
    ecs_os_memset_t(sample, 0, box_cpu_sample_t);
    sample->process_time = box_cpu_process_time();
    sample->wall_time = box_cpu_wall_time();
    box_cpu_host(sample);
    if (!sample->cores) {
        sample->cores = box_cpu_cores();
    }
}

double box_cpu_sleep_ms(void)
{
    return box_cpu_slept_ms;
}

bool box_cpu_oversubscribed(void)
{
    bool result = false;
#ifndef BOX_PLATFORM_WEB
    double load = 0;
    int32_t cores = box_cpu_cores();
    if (getloadavg(&load, 1) == 1 && load > (double)cores) {
        ecs_err("cpu oversubscribed: load %.1f on %d cores", load, cores);
        result = true;
    }
#endif

    return result;
}

void box_cpu_pace(
    box_cpu_t *cpu,
    double budget)
{
    double cpu_now = box_cpu_process_time();
    double wall_now = box_cpu_wall_time();
    box_cpu_slept_ms = 0;
    if (cpu->wall_time != 0) {
        double cpu_used = cpu_now - cpu->cpu_time;
        double wall_used = wall_now - cpu->wall_time;
        double wall_needed = cpu_used / budget;
        if (wall_needed > wall_used) {
            double sleep = wall_needed - wall_used;
            ecs_os_sleep((int32_t)sleep, (int32_t)((sleep - (int32_t)sleep) * 1e9));
            box_cpu_slept_ms = sleep * 1000.0;
            wall_now = box_cpu_wall_time();
        }
    }

    cpu->cpu_time = cpu_now;
    cpu->wall_time = wall_now;
}

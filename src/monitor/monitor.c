/**
 * @file monitor/monitor.c
 * @brief Registers the box.monitor module and the defaults of its singletons.
 */

#define BOX_MONITOR_IMPL
#include "monitor_internal.h"

/** CpuMonitor constructor: the moving average covers one second.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_cpu_monitor_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    ecs_os_memset(ptr, 0, (ecs_size_t)count * (ecs_size_t)sizeof(CpuMonitor));
    CpuMonitor *m = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        m[i].window_s = BOX_MONITOR_WINDOW;
    }
}

/** EcsStats constructor: collect once a second and keep sixty frame times.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_stats_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    ecs_os_memset(ptr, 0, (ecs_size_t)count * (ecs_size_t)sizeof(EcsStats));
    EcsStats *s = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        s[i].period_s = BOX_MONITOR_PERIOD;
        s[i].history = BOX_MONITOR_HISTORY;
    }
}

void box_monitor_top(
    SystemTime *top,
    int32_t count,
    ecs_entity_t system,
    double time_ms)
{
    int32_t i, slot = -1;
    for (i = 0; i < count; i ++) {
        if (time_ms > top[i].time_ms) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        goto done;
    }

    for (i = count - 1; i > slot; i --) {
        top[i] = top[i - 1];
    }

    top[slot].system = system;
    top[slot].time_ms = time_ms;
done:
    return;
}

void BoxMonitorImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, BoxMonitor);
    ECS_IMPORT(world, BoxTypes);
    ECS_IMPORT(world, BoxUi);

    ECS_META_COMPONENT(world, SystemTime);
    ECS_META_COMPONENT(world, PhaseTime);
    ECS_META_COMPONENT(world, CpuMonitor);
    ECS_META_COMPONENT(world, EcsStats);
    ECS_META_COMPONENT(world, SystemMonitor);
    ECS_META_COMPONENT(world, GpuPassTime);
    ECS_META_COMPONENT(world, GpuTimings);
    ECS_META_COMPONENT(world, Fps);

    ecs_set_hooks(world, SystemTime, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, PhaseTime, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, CpuMonitor, { .ctor = box_cpu_monitor_ctor });
    ecs_set_hooks(world, EcsStats, { .ctor = box_stats_ctor });
    ecs_set_hooks(world, SystemMonitor, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, GpuPassTime, { .ctor = flecs_default_ctor });
    ecs_set_hooks(world, GpuTimings, { .ctor = flecs_default_ctor });

    box_cpu_monitor_init(world);
    box_stats_monitor_init(world);
    box_system_monitor_init(world);
    box_gpu_monitor_init(world);
    box_fps_monitor_init(world);
}

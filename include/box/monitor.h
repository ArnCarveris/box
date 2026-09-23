/**
 * @file box/monitor.h
 * @brief CPU, flecs statistics and per system frame timings published as components.
 *
 * A source is only sampled while its singleton exists, so a monitor that is
 * absent costs nothing. Every value is a reflected field, so the terminal, the
 * REST API and the status page read it without a dedicated endpoint.
 */

#ifndef BOX_MONITOR_H
#define BOX_MONITOR_H

#include "types.h"

#ifdef BOX_MONITOR_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Entries of the slowest lists of a monitor, which the ECS_STRUCTs spell out */
#define BOX_MONITOR_TOP (8)

/** Entries of the phase list of a system monitor, which the ECS_STRUCT spells out */
#define BOX_MONITOR_PHASES (8)

/** Render passes a GPU monitor reports, which the ECS_STRUCT spells out */
#define BOX_MONITOR_PASSES (16)

/** Seconds a CPU monitor averages its samples over by default */
#define BOX_MONITOR_WINDOW (1.0)

/** Seconds between two statistics collections by default */
#define BOX_MONITOR_PERIOD (1.0)

/** Frame times a statistics collection keeps by default */
#define BOX_MONITOR_HISTORY (60)

/** Frame times a statistics collection can keep */
#define BOX_MONITOR_HISTORY_MAX (240)

/** Process and machine CPU usage, sampled every frame while the singleton exists.
 *
 * The percentages are a moving average over window_s, where process_percent is a
 * share of one core and system_percent a share of every core.
 */
ECS_STRUCT(CpuMonitor, {
    double window_s;         /**< Seconds the moving average covers, default 1 */
    int32_t cores;           /**< Cores of the machine, written by the engine */
    double process_percent;  /**< Percent of one core this process used */
    double system_percent;   /**< Percent of every core the machine used */
    double core_percent_max; /**< Percent the busiest core of the machine used */
    double budget_sleep_ms;  /**< Milliseconds the CPU budget slept last frame */
});

/** A system and the time it spent in one frame */
ECS_STRUCT(SystemTime, {
    ecs_entity_t system; /**< The system that ran */
    double time_ms;      /**< Milliseconds it spent */
});

/** A pipeline phase and the time its systems spent in one frame */
ECS_STRUCT(PhaseTime, {
    ecs_entity_t phase; /**< The phase its systems depend on */
    double time_ms;     /**< Milliseconds those systems spent */
});

/** Flecs statistics mirrored into fields while the singleton exists.
 *
 * Setting the component imports the flecs stats addon and starts collection;
 * removing it stops collection. Values are refreshed every period_s seconds and
 * frame_time_avg_ms averages the last history frame times.
 */
ECS_STRUCT(EcsStats, {
    double period_s;          /**< Seconds between two collections, default 1 */
    int32_t history;          /**< Frame times kept, default 60, at most 240 */
    int32_t entity_count;     /**< Alive entities at the last collection */
    int32_t table_count;      /**< Tables at the last collection */
    int32_t systems_ran;      /**< Systems that ran in the last frame */
    int32_t collections;      /**< Collections since the component was set */
    double frame_time_ms;     /**< Milliseconds of the last frame */
    double frame_time_avg_ms; /**< Milliseconds of the frames in the window */
    double system_time_ms;    /**< Milliseconds the last frame spent in systems */
    double merge_time_ms;     /**< Milliseconds the last frame spent merging */
    double fps;               /**< Frames per second at the last collection */
    SystemTime slowest[8];    /**< Slowest systems of the period, slowest first */
});

/** Per system and per phase frame timings, collected while enabled.
 *
 * Setting enabled turns on the flecs system time measurement, which the monitor
 * reads per frame; the totals cover the frame before the one being collected.
 */
ECS_STRUCT(SystemMonitor, {
    bool enabled;          /**< Collect per system timings, default false */
    double frame_ms;       /**< Milliseconds every system spent in the frame */
    int32_t system_count;  /**< Systems that spent time in the frame */
    SystemTime slowest[8]; /**< Slowest systems of the frame, slowest first */
    PhaseTime phases[8];   /**< Milliseconds per phase, in pipeline order */
});

/** Name of a render pass and the time it spent on the GPU in one frame */
ECS_STRUCT(GpuPassTime, {
    char *name;     /**< Name of the pass, NULL when the entry is unused */
    double time_ms; /**< Milliseconds the GPU spent in the pass */
});

/** Per pass GPU timings of the rendered frames, collected while the singleton exists.
 *
 * Timestamps are read back one or two frames later, the frame the numbers name.
 * A pass name recorded by several views or cascades sums their time. A device
 * without timestamp queries reports no passes.
 */
ECS_STRUCT(GpuTimings, {
    bool supported;         /**< The device can time its passes, written by the engine */
    int32_t frame;          /**< Frame of the renderer the passes were measured in */
    int32_t pass_count;     /**< Passes that frame recorded, at most sixteen */
    double frame_ms;        /**< Milliseconds those passes add up to */
    GpuPassTime passes[16]; /**< The passes, in the order the frame recorded them */
});

/** Frame times the history of the Fps singleton keeps, which the ECS_STRUCT spells out */
#define BOX_MONITOR_FPS_HISTORY (120)

/** Script the frame rate panel is loaded from at import, into box.monitor.fps_panel */
#define BOX_MONITOR_FPS_PANEL "etc/ui/fps.flecs"

/** Frame rate of the last second and the most recent frame times.
 *
 * Filled and marked modified every frame. The panel shows while the variable
 * box.monitor.fps_panel.visible is true. The history is oldest first, and slots
 * before the first recorded frame are zero.
 */
ECS_STRUCT(Fps, {
    double fps;          /**< Frames per second over the last second */
    double mean_ms;      /**< Mean frame time of the last second */
    double worst_ms;     /**< Worst frame time of the last second */
    double frame_ms;     /**< Milliseconds of the most recent frame */
    int32_t count;       /**< Frame times the history holds, at most its size */
    float history[120];  /**< Most recent frame times in milliseconds, newest last */
});

/** Register the box.monitor module.
 *
 * @param world The world
 */
void BoxMonitorImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif

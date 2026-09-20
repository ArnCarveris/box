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
 * share of one core and system_percent a share of every core. The same sampling
 * primitives drive the CPU budget of a headless instance, so budget_sleep_ms
 * shows what that budget cost the last frame.
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
 * Setting the component makes the renderer write a timestamp around every pass
 * it records; removing it stops the queries and releases what they need, so a
 * world without the component pays for nothing. The timestamps of a frame are
 * read back one or two frames later, which is the frame the numbers name. The
 * passes are those of the renderer and of the effect chain of a view, and a
 * name that several views or shadow cascades record holds what they spent
 * together. A device without the timestamp query feature reports no passes.
 */
ECS_STRUCT(GpuTimings, {
    bool supported;         /**< The device can time its passes, written by the engine */
    int32_t frame;          /**< Frame of the renderer the passes were measured in */
    int32_t pass_count;     /**< Passes that frame recorded, at most sixteen */
    double frame_ms;        /**< Milliseconds those passes add up to */
    GpuPassTime passes[16]; /**< The passes, in the order the frame recorded them */
});

/** Corner of a view an overlay panel is pinned to */
ECS_ENUM(FpsCorner, {
    FpsTopLeft = 0,     /**< Top left corner of the view */
    FpsTopRight = 1,    /**< Top right corner of the view */
    FpsBottomLeft = 2,  /**< Bottom left corner of the view */
    FpsBottomRight = 3  /**< Bottom right corner of the view */
});

/** Frame rate panel drawn on a view, or on every view as a singleton.
 *
 * The panel background, its two reference lines and the most recent hundred
 * and twenty frame times as one bar each, green while the frame stayed above
 * sixty frames per second, yellow above thirty and red below, are drawn after the effect chain
 * of the view and before its capture, so a headless run sees them, and a view
 * without the component pays for nothing. The frame rate itself is drawn as
 * regular UI text, in the corner the panel is pinned to, through a UI element
 * box.monitor owns. The last three fields are written by the engine from the
 * last second of frames, the window the text covers.
 */
ECS_STRUCT(FpsOverlay, {
    bool enabled;      /**< Draw the panel, default true */
    FpsCorner corner;  /**< Corner of the view the panel is pinned to */
    float scale;       /**< Size multiplier of the panel, in points, default 1 */
    float opacity;     /**< Opacity of the panel background, default 0.7 */
    double fps;        /**< Frames per second of the window, written by the engine */
    double mean_ms;    /**< Mean frame time of the window, written by the engine */
    double worst_ms;   /**< Worst frame time of the window, written by the engine */
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

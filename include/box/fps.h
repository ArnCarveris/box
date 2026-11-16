/**
 * @file box/fps.h
 * @brief The fps panel: a chart of the recent frame times and the mean, worst and rate of the
 * last second, at the top right of the surface. The load panel: a chart per thread of the time
 * it worked in the recent frames.
 *
 * FpsPanel opens and closes it, from C, a script, the REST API or the fps command. A scene under
 * BOX_FPS_UI drawn by BOX_FPS_VIEW at BOX_VIEW_ORDER_FPS, over the editor and under the console,
 * holds a panel at the top right of the surface; closing deletes the view and the content.
 *
 * The panel is the template BOX_FPS_TEMPLATE of BOX_FPS_SCRIPT_PATH, reloaded when it changes.
 * It reads FpsSummary, what box.fps writes to BOX_FPS_BAR_MUT and BOX_FPS_BARS_MUT, and the
 * colors and limits below, exported as box.fps.green, yellow, red, fast_ms and slow_ms.
 */

#ifndef BOX_FPS_H
#define BOX_FPS_H

#include "types.h"
#include "app.h"

#ifdef BOX_FPS_IMPL
#define ECS_META_IMPL IMPL
#else
#define ECS_META_IMPL EXTERN
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Name of the root entity of the panel content and its view */
#define BOX_FPS_SCOPE "fps"

/** Path of the scene root of the panel content while it is open */
#define BOX_FPS_UI "fps.ui"

/** Path of the view of the panel content while it is open */
#define BOX_FPS_VIEW "fps.view"

/** Path of the layout root that fills the surface, a column of the open panels at its top
 * right: the fps panel above the load panel */
#define BOX_FPS_SCREEN "fps.ui.screen"

/** Path of the panel, anchored to the top right of the surface, the instance of
 * BOX_FPS_TEMPLATE */
#define BOX_FPS_PANEL "fps.ui.screen.panel"

/** Path of the heading of the panel, a Text above the chart */
#define BOX_FPS_HEADING BOX_FPS_PANEL ".heading"

/** Path of the chart. Its children are BOX_FPS_CHART_BARS, the parent of the bars, and the
 * reference lines line_60 and line_30 over them. It has no Layout, so a bar that changes lays
 * nothing out. */
#define BOX_FPS_CHART BOX_FPS_PANEL ".chart"

/** Path of the parent of the bars, an instance of BOX_FPS_BARS_TEMPLATE without a Layout. Its
 * children are the bars bar_0 to bar_N, N BOX_FPS_BARS - 1, instances of BOX_FPS_BAR that show
 * the oldest sample on the left to the newest on the right.
 *
 * A bar keeps its name and entity but not its place: the bars rotate, so bar_N is not always
 * the newest sample. */
#define BOX_FPS_CHART_BARS BOX_FPS_CHART ".bars"

/** Path of the summary row under the chart. Its children are the Texts times, the mean and
 * worst frame time on the left, and rate, the fps on the right. */
#define BOX_FPS_SUMMARY BOX_FPS_PANEL ".summary"

/** The script of the panel content, found in the working directory or an ancestor of it */
#define BOX_FPS_SCRIPT_PATH "etc/fps/fps.flecs"

/** Path of the managed script entity of BOX_FPS_SCRIPT_PATH */
#define BOX_FPS_SCRIPT "box.fps.script"

/** Path of the template of the script that the panel instantiates */
#define BOX_FPS_TEMPLATE "box.fps.FpsWidget"

/** Path of the template of the script of a chart: the bars in bars and the reference lines
 * line_60 and line_30 over them, which the fps panel and every row of the load panel have */
#define BOX_FPS_CHART_TEMPLATE "box.fps.FpsChart"

/** Path of the template of the script that every bar of the chart instantiates */
#define BOX_FPS_BAR "box.fps.FpsBar"

/** Path of the mut variables of BOX_FPS_BAR, set as a whole: ms, an f64, the frame time in
 * milliseconds of the sample the bar shows, 0 without a sample, and col, an i32, its column. The
 * script draws the bar from them, col - first slots from the left, first of BOX_FPS_BARS_MUT.
 *
 * box.fps writes them while the panel is open, only where they changed. A new sample goes to the
 * bar of the oldest sample in the column right of the newest, so it evaluates one bar and the
 * parent.
 *
 * Every BOX_FPS_BARS samples, and when the samples changed otherwise, bar_i gets the sample of
 * age BOX_FPS_BARS - 1 - i in column i again. */
#define BOX_FPS_BAR_MUT BOX_FPS_BAR ".mut"

/** Path of the template of the script that the parent of the bars, BOX_FPS_CHART_BARS,
 * instantiates */
#define BOX_FPS_BARS_TEMPLATE "box.fps.FpsBars"

/** Path of the mut variables of BOX_FPS_BARS_TEMPLATE, which box.fps sets as a whole: first, an
 * i32, the column of the bar at the left of the chart. The script moves the bars first slots
 * left, so a new sample moves one entity instead of every bar. */
#define BOX_FPS_BARS_MUT BOX_FPS_BARS_TEMPLATE ".mut"

/** Points between the panel and the top and right edges of the surface */
#define BOX_FPS_MARGIN (10.0f)

/** Bars of the chart, one per frame sample, the newest on the right */
#define BOX_FPS_BARS (120)

/** Width of a bar in points */
#define BOX_FPS_BAR_WIDTH (3.0f)

/** Width of the slot of a bar in points: the bar and the gap to the next one */
#define BOX_FPS_BAR_SLOT (4.0f)

/** Width of the chart in points */
#define BOX_FPS_CHART_WIDTH (BOX_FPS_BARS * BOX_FPS_BAR_SLOT)

/** Height of the chart in points */
#define BOX_FPS_CHART_HEIGHT (60.0f)

/** Frame time at the top of the chart in milliseconds. The vertical axis is linear in frame
 * time, from 0 at the bottom; a bar is a whole number of points high, at least BOX_FPS_BAR_WIDTH
 * and at most the chart. */
#define BOX_FPS_CHART_MS (50.0)

/** Milliseconds of samples the summary covers, the last second */
#define BOX_FPS_SUMMARY_MS (1000.0)

/** Milliseconds a frame may exceed 1000 / 60 and stay green, for the jitter of vsync */
#define BOX_FPS_TOLERANCE_MS (0.5)

/** Longest frame time of a green bar in milliseconds, exported as box.fps.fast_ms */
#define BOX_FPS_FAST_MS (1000.0 / 60.0 + BOX_FPS_TOLERANCE_MS)

/** Longest frame time of a yellow bar in milliseconds, exported as box.fps.slow_ms */
#define BOX_FPS_SLOW_MS (1000.0 / 30.0)

/** Color of a bar of a frame at 60 fps or faster, exported as box.fps.green */
#define BOX_FPS_GREEN ((rgb){ 70, 200, 90 })

/** Color of a bar of a frame under 60 fps, down to 30 fps, exported as box.fps.yellow */
#define BOX_FPS_YELLOW ((rgb){ 235, 200, 50 })

/** Color of a bar of a frame under 30 fps, exported as box.fps.red */
#define BOX_FPS_RED ((rgb){ 230, 65, 55 })

/** Path of the load panel, anchored to the top right of the surface below the fps panel, the
 * instance of BOX_LOAD_TEMPLATE */
#define BOX_LOAD_PANEL "fps.ui.screen.load"

/** Path of the heading of the load panel, a Text above the rows */
#define BOX_LOAD_HEADING BOX_LOAD_PANEL ".heading"

/** Path of the template of the script that the load panel instantiates */
#define BOX_LOAD_TEMPLATE "box.fps.LoadWidget"

/** Rows of the load panel, one per member of LoadSamples */
#define BOX_LOAD_ROWS (5)

/** Paths of the rows of the load panel in the order of LoadSamples. A row has a head, the Texts
 * name, times and percent, over a chart like BOX_FPS_CHART, whose bars are in chart.bars. */
#define BOX_LOAD_MAIN BOX_LOAD_PANEL ".main"

/** Path of the row of the time the renderer worked */
#define BOX_LOAD_RENDER BOX_LOAD_PANEL ".render"

/** Path of the row of the time the renderer waited on the GPU */
#define BOX_LOAD_GPU BOX_LOAD_PANEL ".gpu"

/** Path of the row of the time the audio thread rendered */
#define BOX_LOAD_AUDIO BOX_LOAD_PANEL ".audio"

/** Path of the row of the time the busiest job worker worked */
#define BOX_LOAD_JOBS BOX_LOAD_PANEL ".jobs"

/** Name of the phase of box.fps that ends the frame of the main thread for LoadSamples, after
 * TaskPublish */
#define BOX_LOAD_END_PHASE "box.fps.LoadEnd"

/** The fps panel singleton: whether it is shown.
 *
 * A change applies in the next frame. The panel shows FrameSamples (box/app.h), takes neither
 * the pointer nor the keyboard and takes its background, text and border from UiTheme.
 */
ECS_STRUCT(FpsPanel, {
    bool open;          /**< Shown, the fps command flips it */
});

/** The frames of the last second of FrameSamples, written by box.fps while the panel is open
 * whenever FrameSamples is set or notified, read by the summary of the panel.
 *
 * The last second is the newest samples up to the first one that brings their sum to
 * BOX_FPS_SUMMARY_MS, or all of them when they sum to less.
 */
ECS_STRUCT(FpsSummary, {
    int32_t frames;     /**< Samples of the last second, 0 without samples */
    double mean_ms;     /**< Mean frame time in milliseconds, 0 without samples */
    double worst_ms;    /**< Longest frame time in milliseconds, 0 without samples */
    double fps;         /**< Frames per second, 1000 / mean_ms, 0 without samples */
});

/** The load panel singleton: whether it is shown.
 *
 * A change applies in the next frame. The panel shows LoadSamples, takes neither the pointer
 * nor the keyboard and takes its background, text and border from UiTheme.
 */
ECS_STRUCT(LoadPanel, {
    bool open;          /**< Shown, the load command flips it */
});

/** Milliseconds the threads of box worked in the recent frames, one sample per frame, written by
 * box.fps at the start of every frame into each member whose hold is false.
 *
 * A sample is the time a thread worked between the starts of the frame before and this one, so
 * it lines up with FrameSamples. A thread that waits for another or for the GPU does not work.
 */
ECS_STRUCT(LoadSamples, {
    FrameSamples main;      /**< The main thread, without its waits for a free render packet
                             *   or a drain of the renderer, its sleeps and the frame pacing */
    FrameSamples render;    /**< The renderer, the render thread or the main thread that renders
                             *   for it, without its waits on the GPU */
    FrameSamples gpu;       /**< The renderer waiting on the GPU: for the window image, which
                             *   includes vsync, the oldest frame in flight and GPU reads */
    FrameSamples audio;     /**< The audio thread rendering, 0 without a device */
    FrameSamples jobs;      /**< The job worker that worked the longest, 0 without workers */
});

/** Time a thread worked per frame over the last second of LoadSamples */
ECS_STRUCT(LoadStat, {
    double mean_ms;     /**< Mean milliseconds per frame, 0 without samples */
    double worst_ms;    /**< Longest milliseconds of a frame, 0 without samples */
    double share;       /**< Percent of the frame time the thread worked, 0 without samples */
});

/** The last second of LoadSamples, written by box.fps while the load panel is open whenever
 * LoadSamples is set or notified, read by the heads of the rows of the panel.
 *
 * The last second is the frames of FpsSummary: the newest frame samples up to the first one that
 * brings their sum to BOX_FPS_SUMMARY_MS, or all of them when they sum to less.
 */
ECS_STRUCT(LoadSummary, {
    int32_t frames;     /**< Frames of the last second, 0 without frame samples */
    int32_t workers;    /**< Job workers, 0 before the first job or without workers */
    LoadStat main;      /**< The main thread */
    LoadStat render;    /**< The renderer working */
    LoadStat gpu;       /**< The renderer waiting on the GPU */
    LoadStat audio;     /**< The audio thread */
    LoadStat jobs;      /**< The busiest job worker */
});

/** Compute the summary of the last second of frame samples.
 *
 * @param samples The samples, NULL for none
 * @param out Receives the summary
 */
void box_fps_summarize(
    const FrameSamples *samples,
    FpsSummary *out);

/** Compute the summary of the last second of thread samples.
 *
 * @param frames The frame samples that set the last second, NULL for none
 * @param samples The thread samples, NULL for none
 * @param workers The number of job workers
 * @param out Receives the summary
 */
void box_load_summarize(
    const FrameSamples *frames,
    const LoadSamples *samples,
    int32_t workers,
    LoadSummary *out);

/** Register the box.fps module.
 *
 * Creates the FpsPanel and LoadPanel singletons, closed, and the FpsSummary, LoadSamples and
 * LoadSummary singletons. The content, its view and the script are created when a panel first
 * opens.
 *
 * @param world The world
 */
void BoxFpsImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#undef ECS_META_IMPL

#endif

/**
 * @file box/fps.h
 * @brief The fps panel: a chart of the recent frame times and the mean, worst and rate of the
 * last second, at the top right of the surface.
 *
 * FpsPanel opens and closes it, from C, a script, the REST API or the fps command. A scene under
 * BOX_FPS_UI drawn by BOX_FPS_VIEW at BOX_VIEW_ORDER_FPS, over the editor and under the console,
 * holds a panel at the top right of the surface; closing deletes the view and the content.
 *
 * The panel is the template BOX_FPS_TEMPLATE of BOX_FPS_SCRIPT_PATH, reloaded when it changes.
 * It reads FpsSummary, the frame time box.fps writes to the BOX_FPS_BAR_MUT of every bar, and the
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

/** Path of the layout root that fills the surface */
#define BOX_FPS_SCREEN "fps.ui.screen"

/** Path of the panel, anchored to the top right of the surface, the instance of
 * BOX_FPS_TEMPLATE */
#define BOX_FPS_PANEL "fps.ui.screen.panel"

/** Path of the heading of the panel, a Text above the chart */
#define BOX_FPS_HEADING BOX_FPS_PANEL ".heading"

/** Path of the chart. Its children are the bars bar_0 to bar_N, N BOX_FPS_BARS - 1, instances
 * of BOX_FPS_BAR from the oldest sample on the left to the newest on the right, and the reference
 * lines line_60 and line_30 over them. It has no Layout, so a bar that changes lays nothing out. */
#define BOX_FPS_CHART BOX_FPS_PANEL ".chart"

/** Path of the summary row under the chart. Its children are the Texts times, the mean and
 * worst frame time on the left, and rate, the fps on the right. */
#define BOX_FPS_SUMMARY BOX_FPS_PANEL ".summary"

/** The script of the panel content, found in the working directory or an ancestor of it */
#define BOX_FPS_SCRIPT_PATH "etc/fps/fps.flecs"

/** Path of the managed script entity of BOX_FPS_SCRIPT_PATH */
#define BOX_FPS_SCRIPT "box.fps.script"

/** Path of the template of the script that the panel instantiates */
#define BOX_FPS_TEMPLATE "box.fps.FpsWidget"

/** Path of the template of the script that every bar of the chart instantiates */
#define BOX_FPS_BAR "box.fps.FpsBar"

/** Path of the mut variables of BOX_FPS_BAR. Its member ms, an f64, is the frame time in
 * milliseconds of the sample the bar shows, 0 without a sample, from which the script draws it.
 *
 * box.fps writes it while the panel is open, only to the bars whose sample changed, so a new
 * sample evaluates only those bars again. */
#define BOX_FPS_BAR_MUT BOX_FPS_BAR ".mut"

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

/** Compute the summary of the last second of frame samples.
 *
 * @param samples The samples, NULL for none
 * @param out Receives the summary
 */
void box_fps_summarize(
    const FrameSamples *samples,
    FpsSummary *out);

/** Register the box.fps module.
 *
 * Creates the FpsPanel singleton, closed, and the FpsSummary singleton. The content, its view
 * and the script are created when the panel first opens.
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

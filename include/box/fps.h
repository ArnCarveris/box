/**
 * @file box/fps.h
 * @brief The fps panel: a small chart of the recent frames, one bar per frame, as Tracy draws them.
 *
 * The FpsPanel singleton opens and closes it, from C, a script, the REST API or the fps command.
 * Its content is a scene under BOX_FPS_UI drawn by BOX_FPS_VIEW at BOX_VIEW_ORDER_FPS: over the
 * editor, under the console. Closing deletes the view and the content.
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

/** Path of the panel, anchored to the top right of the surface */
#define BOX_FPS_PANEL "fps.ui.screen.panel"

/** Path of the text line above the chart: the fps and the last frame time */
#define BOX_FPS_LABEL "fps.ui.screen.panel.header.label"

/** Path of the chart, whose children are the bars bar_0 (oldest) to bar_239 (newest) */
#define BOX_FPS_CHART "fps.ui.screen.panel.chart"

/** Width of the panel in points */
#define BOX_FPS_WIDTH (320.0f)

/** Height of the panel in points */
#define BOX_FPS_HEIGHT (120.0f)

/** Points between the panel and the top and right edges of the surface */
#define BOX_FPS_MARGIN (10.0f)

/** Bars of the chart, one per frame, the newest on the right */
#define BOX_FPS_BARS BOX_FRAME_WINDOW

/** Frame time at the top of the chart in milliseconds; longer frames are clamped to it */
#define BOX_FPS_CHART_MS (40.0)

/** Milliseconds a frame may exceed 1000 / 60 and stay green, for the jitter of vsync */
#define BOX_FPS_TOLERANCE_MS (0.5)

/** Color of a bar of a frame at 60 fps or faster */
#define BOX_FPS_GREEN ((rgb){ 70, 200, 90 })

/** Color of a bar of a frame under 60 fps, down to 30 fps */
#define BOX_FPS_YELLOW ((rgb){ 235, 200, 50 })

/** Color of a bar of a frame under 30 fps */
#define BOX_FPS_RED ((rgb){ 230, 65, 55 })

/** The fps panel singleton: whether it is shown.
 *
 * A change applies in the next frame. The panel reads FrameSamples (box/app.h), takes neither
 * the pointer nor the keyboard and takes its background, text and border from UiTheme.
 */
ECS_STRUCT(FpsPanel, {
    bool open;          /**< Shown, the fps command flips it */
});

/** Get the color of the bar of a frame from its wall time.
 *
 * @param interval_ms The time between the start of the frame and the next one in milliseconds
 * @return BOX_FPS_GREEN up to 1000 / 60 + BOX_FPS_TOLERANCE_MS, BOX_FPS_YELLOW up to 1000 / 30,
 * else BOX_FPS_RED
 */
rgb box_fps_bar_color(
    double interval_ms);

/** Get the height of the bar of a frame in a chart.
 *
 * @param interval_ms The wall time of the frame in milliseconds
 * @param chart_height Height of the chart in points
 * @return The height in points, chart_height at BOX_FPS_CHART_MS and over, 0 without a time
 */
float box_fps_bar_height(
    double interval_ms,
    float chart_height);

/** Register the box.fps module.
 *
 * Creates the FpsPanel singleton, closed. The content and its view are created when it opens.
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

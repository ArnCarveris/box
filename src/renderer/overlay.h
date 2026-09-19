/**
 * @file renderer/overlay.h
 * @brief Frame time ring, the color class of a frame and the panel it is drawn as.
 *
 * The panel itself is a background rectangle and the last second of frame
 * times as one bar each plus two reference lines, so every part of it the
 * renderer draws is a rectangle. The frame rate is drawn separately, as regular
 * UI text box.monitor owns. The ring and the thresholds live here because the
 * bars, the published numbers and the command that prints them share them, and
 * because neither side of the frame packet may include the other.
 */

#ifndef BOX_RENDERER_OVERLAY_H
#define BOX_RENDERER_OVERLAY_H

#include <stdint.h>
#include <stdbool.h>

/** Frame times a ring keeps, a second of frames at 240 per second */
#define BOX_FPS_HISTORY (240)

/** Milliseconds the window of the bars and of the published numbers covers */
#define BOX_FPS_WINDOW_MS (1000.0f)

/** Frame time of sixty frames per second, the fastest class ends here */
#define BOX_FPS_GREEN_MS (1000.0f / 60.0f)

/** Frame time of thirty frames per second, the middle class ends here */
#define BOX_FPS_YELLOW_MS (1000.0f / 30.0f)

/** Frame time a bar of the full chart height stands for */
#define BOX_FPS_BAR_MS (50.0f)

/** Milliseconds a frame may exceed a threshold by and still count as the faster class.
 *
 * A fixed step of exactly sixty frames per second lands a hair above its own
 * threshold once the interval has been through a float, which would color a
 * perfect run yellow.
 */
#define BOX_FPS_EPSILON_MS (0.05f)

/** Class of a frame that stayed above sixty frames per second */
#define BOX_FPS_GREEN (0)

/** Class of a frame that stayed above thirty frames per second */
#define BOX_FPS_YELLOW (1)

/** Class of a frame that fell below thirty frames per second */
#define BOX_FPS_RED (2)

/** Frame times of the most recent frames, oldest overwritten first */
typedef struct box_fps_ring_t {
    float times_ms[BOX_FPS_HISTORY]; /**< Frame times in the order they arrived */
    int32_t count;                   /**< Frame times the ring holds, at most its size */
    int32_t next;                    /**< Slot the next frame time takes */
} box_fps_ring_t;

/** Frame rate and frame time distribution of the frames a window covers */
typedef struct box_fps_stats_t {
    int32_t frames; /**< Frames the window covered */
    float fps;      /**< Frames per second over those frames */
    float mean_ms;  /**< Mean frame time of the window */
    float worst_ms; /**< Worst frame time of the window */
} box_fps_stats_t;

/** Corner constant of a panel pinned to the top left of its view */
#define BOX_OVERLAY_TOP_LEFT (0)

/** Corner constant of a panel pinned to the top right of its view */
#define BOX_OVERLAY_TOP_RIGHT (1)

/** Corner constant of a panel pinned to the bottom left of its view */
#define BOX_OVERLAY_BOTTOM_LEFT (2)

/** Corner constant of a panel pinned to the bottom right of its view */
#define BOX_OVERLAY_BOTTOM_RIGHT (3)

/** Bars the chart of a panel draws at most, the newest frame on the right */
#define BOX_OVERLAY_BARS (120)

/** Rectangles a panel holds: its background, its two reference lines and its bars */
#define BOX_OVERLAY_RECT_MAX (BOX_OVERLAY_BARS + 3)

/** Pixels between the panel and the edges of its view, before the scale */
#define BOX_OVERLAY_MARGIN (8.0f)

/** Pixels between the panel border and its contents, before the scale */
#define BOX_OVERLAY_PAD (4.0f)

/** Width of one bar of the chart in pixels, before the scale */
#define BOX_OVERLAY_BAR_W (1.0f)

/** Height of the chart in pixels, before the scale */
#define BOX_OVERLAY_CHART_H (32.0f)

/** Height of the row the frame rate label takes above the chart, before the scale */
#define BOX_OVERLAY_LABEL_H (16.0f)

/** Width of the panel in pixels, before the scale */
#define BOX_OVERLAY_WIDTH (BOX_OVERLAY_BARS * BOX_OVERLAY_BAR_W + 2.0f * BOX_OVERLAY_PAD)

/** Height of the panel in pixels, before the scale */
#define BOX_OVERLAY_HEIGHT (BOX_OVERLAY_LABEL_H + BOX_OVERLAY_CHART_H + 3.0f * BOX_OVERLAY_PAD)

/** One rectangle of a panel, in pixels of its view */
typedef struct box_overlay_rect_t {
    float rect[4];  /**< Left, top, width and height in pixels from the top left */
    float color[4]; /**< Color the rectangle is filled with, display encoded RGBA */
} box_overlay_rect_t;

/** Where and how large the panel of a view is drawn */
typedef struct box_overlay_layout_t {
    float width;    /**< Width of the view in pixels */
    float height;   /**< Height of the view in pixels */
    float scale;    /**< Size multiplier of the panel, one for its natural size */
    float opacity;  /**< Opacity of the panel background, one for opaque */
    int32_t corner; /**< Corner of the view the panel is pinned to */
} box_overlay_layout_t;

/** Record one frame time, overwriting the oldest one when the ring is full.
 *
 * @param ring The ring
 * @param time_ms Milliseconds the frame took, negative values are ignored
 */
void box_fps_ring_push(
    box_fps_ring_t *ring,
    float time_ms);

/** Frame time of one of the frames the ring holds, counted back from the newest.
 *
 * @param ring The ring
 * @param back Frames back from the newest one, zero for the newest
 * @return The frame time, zero when the ring holds no such frame
 */
float box_fps_ring_get(
    const box_fps_ring_t *ring,
    int32_t back);

/** Frames of the ring whose times fit in a window, counted back from the newest.
 *
 * The frame that crosses the end of the window is part of it, so a ring with
 * frames always reports at least one.
 *
 * @param ring The ring
 * @param window_ms Milliseconds the window covers
 * @return The number of frames
 */
int32_t box_fps_ring_window(
    const box_fps_ring_t *ring,
    float window_ms);

/** Summarize the frames of the ring that a window covers.
 *
 * @param ring The ring
 * @param window_ms Milliseconds the window covers
 * @param out Receives the frame rate and the frame time distribution
 */
void box_fps_ring_stats(
    const box_fps_ring_t *ring,
    float window_ms,
    box_fps_stats_t *out);

/** Color class of a frame time.
 *
 * The thresholds allow BOX_FPS_EPSILON_MS, so a frame at exactly the rate of a
 * class belongs to it.
 *
 * @param time_ms Milliseconds the frame took
 * @return BOX_FPS_GREEN above sixty fps, BOX_FPS_YELLOW above thirty, else BOX_FPS_RED
 */
int32_t box_fps_class(
    float time_ms);

/** Color a class is drawn with.
 *
 * @param cls The class
 * @param out Receives the display encoded red, green and blue of the class
 */
void box_fps_class_color(
    int32_t cls,
    float *out);

/** Build the rectangles of a panel from the frames of a ring.
 *
 * @param ring The ring the bars come from
 * @param layout Size and corner of the panel
 * @param out Receives the rectangles, background first
 * @param max Rectangles the output holds
 * @return The number of rectangles written
 */
int32_t box_overlay_build(
    const box_fps_ring_t *ring,
    const box_overlay_layout_t *layout,
    box_overlay_rect_t *out,
    int32_t max);

#endif

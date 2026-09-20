/**
 * @file monitor/fps.c
 * @brief Defaults of the FpsOverlay component, the window it publishes and its text.
 *
 * The panel background, its two reference lines and its bars are drawn by the
 * renderer from the intervals between the packets it processes, at a point
 * that has no world and no font atlas to draw text with. This file keeps the
 * same window on the game side, so the frame rate, the mean and the worst
 * frame time of the last second are reflected fields that the terminal, the
 * REST API and the status page read without an endpoint of their own, and it
 * owns the UI text element the number is drawn with, built out of the regular
 * box.ui Text and Font an application text uses. Nothing is sampled and no
 * text element exists while no entity and no singleton carries the component.
 */

#include "monitor_internal.h"
#include "../renderer/overlay.h"

/** Frame times the published numbers of every overlay are taken from */
typedef struct box_fps_monitor_t {
    box_fps_ring_t ring; /**< Frame times of the most recent frames */
    int64_t frame;       /**< World frame the last sample was taken in, -1 for none */
    ecs_entity_t text;   /**< UI element the frame rate is drawn with, 0 until published */
} box_fps_monitor_t;

/** Horizontal and vertical alignment a corner of the panel pins its text to.
 *
 * @param corner Corner the FpsOverlay component selects
 * @param h Receives the horizontal alignment
 * @param v Receives the vertical alignment
 */
static void box_fps_text_align(
    FpsCorner corner,
    Align *h,
    VAlign *v)
{
    *h = (corner == FpsTopLeft || corner == FpsBottomLeft) ? AlignLeft : AlignRight;
    *v = (corner == FpsTopLeft || corner == FpsTopRight) ? VAlignTop : VAlignBottom;
}

/** Logical width and height of the view the root of the text tree pins against.
 *
 * The layout pass places an anchored child against the box its Layout or Rect
 * gives it, not against the extent the camera happens to show, so the root
 * needs that same box or an anchored child collapses to its origin, the
 * centre of the view. This follows Surface the way the view itself does, in
 * the points the window has at Ui.scale one, and falls back to the size a
 * headless run without a Surface singleton renders at.
 *
 * @param world The world
 * @param out Receives the width and the height
 */
static void box_fps_text_extent(
    ecs_world_t *world,
    float *out)
{
    out[0] = 960.0f;
    out[1] = 540.0f;
    const Surface *surface = ecs_id(Surface) ? ecs_singleton_get(world, Surface) : NULL;
    if (!surface) {
        goto done;
    }

    if (surface->point_width > 0) {
        out[0] = (float)surface->point_width;
    }

    if (surface->point_height > 0) {
        out[1] = (float)surface->point_height;
    }

done:
    return;
}

/** Ensure the UI element the frame rate is drawn with exists.
 *
 * The element is a Text of its own, pinned to the corner of the view its
 * FpsOverlay selects with the same margin the panel uses, in the points the
 * panel is sized in, so the regular UI text path draws the number where the
 * seven segment digits used to sit. A tree that draws only this element follows the surface, so it
 * keeps the same corner across a resize.
 *
 * @param world The world
 * @param ctx The monitor state the element is kept in
 * @return The element
 */
static ecs_entity_t box_fps_text_ensure(
    ecs_world_t *world,
    box_fps_monitor_t *ctx)
{
    if (ctx->text && ecs_is_alive(world, ctx->text)) {
        goto done;
    }

    ecs_entity_t root = ecs_new(world);
    ecs_set(world, root, Ui, { .height = 0 });
    ctx->text = ecs_entity(world, { .parent = root });

done:
    return ctx->text;
}

/** Position, size and content of the text element with the settings of one overlay.
 *
 * The number sits at the padding from the side of the panel and a quarter of
 * the panel height from its top, whichever corner the panel is pinned to.
 *
 * @param world The world
 * @param text The text element
 * @param overlay The overlay the corner, the scale and the frame rate come from
 */
static void box_fps_text_update(
    ecs_world_t *world,
    ecs_entity_t text,
    const FpsOverlay *overlay)
{
    float scale = overlay->scale > 0.0f ? overlay->scale : 1.0f;
    float offset = (BOX_OVERLAY_MARGIN + BOX_OVERLAY_PAD) * scale;
    float label = BOX_OVERLAY_LABEL_Y - BOX_OVERLAY_LABEL_H / 2.0f;
    if (overlay->corner == FpsBottomLeft || overlay->corner == FpsBottomRight) {
        label = BOX_OVERLAY_HEIGHT - BOX_OVERLAY_LABEL_Y - BOX_OVERLAY_LABEL_H / 2.0f;
    }

    float top = (BOX_OVERLAY_MARGIN + label) * scale;
    ecs_entity_t root = ecs_get_target(world, text, EcsChildOf, 0);
    float extent[2];
    box_fps_text_extent(world, extent);
    ecs_set(world, root, Layout, { .width = extent[0], .height = extent[1] });

    Align h;
    VAlign v;
    box_fps_text_align(overlay->corner, &h, &v);
    ecs_set(world, text, Anchor, { .h = h, .v = v, .x = offset, .y = top });
    ecs_set(world, text, Font, { .size = BOX_OVERLAY_LABEL_H * scale });

    int32_t value = (int32_t)(overlay->fps + 0.5);
    if (value > 999) {
        value = 999;
    }

    float color[3];
    box_fps_class_color(box_fps_class((float)overlay->mean_ms), color);
    char buf[16];
    ecs_os_snprintf(buf, sizeof(buf), "%d", value);
    ecs_set(world, text, Text, { .value = ecs_os_strdup(buf), .align = AlignRight });
    ecs_set(world, text, Rgb, {
        (uint8_t)(color[0] * 255.0f + 0.5f),
        (uint8_t)(color[1] * 255.0f + 0.5f),
        (uint8_t)(color[2] * 255.0f + 0.5f)
    });
}

/** FpsOverlay constructor: enabled, in the top right corner and mostly opaque.
 *
 * @param ptr The components
 * @param count Number of components
 * @param ti Type info
 */
static void box_fps_overlay_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    ecs_os_memset(ptr, 0, (ecs_size_t)count * (ecs_size_t)sizeof(FpsOverlay));
    FpsOverlay *o = ptr;
    int32_t i;
    for (i = 0; i < count; i ++) {
        o[i].enabled = true;
        o[i].corner = FpsTopRight;
        o[i].scale = 1.0f;
        o[i].opacity = 0.7f;
    }
}

/** Sample the frame and publish the window on every overlay of the world.
 *
 * @param it Iterator over the FpsOverlay components
 */
static void BoxPublishFps(
    ecs_iter_t *it)
{
    FpsOverlay *overlay = ecs_field(it, FpsOverlay, 0);
    box_fps_monitor_t *ctx = it->ctx;
    const ecs_world_info_t *info = ecs_get_world_info(it->world);
    if (ctx->frame != info->frame_count_total) {
        ctx->frame = info->frame_count_total;
        box_fps_ring_push(&ctx->ring, (float)(info->delta_time * 1000.0));
    }

    box_fps_stats_t stats;
    box_fps_ring_stats(&ctx->ring, BOX_FPS_WINDOW_MS, &stats);
    int32_t i;
    for (i = 0; i < it->count; i ++) {
        overlay[i].fps = stats.fps;
        overlay[i].mean_ms = stats.mean_ms;
        overlay[i].worst_ms = stats.worst_ms;
    }

    for (i = 0; i < it->count; i ++) {
        if (!overlay[i].enabled) {
            continue;
        }

        ecs_entity_t text = box_fps_text_ensure(it->world, ctx);
        box_fps_text_update(it->world, text, &overlay[i]);
        break;
    }
}

/** Free the window of the overlay monitor.
 *
 * @param ptr The window
 */
static void box_fps_monitor_free(
    void *ptr)
{
    ecs_os_free(ptr);
}

void box_fps_overlay_init(
    ecs_world_t *world)
{
    ecs_set_hooks(world, FpsOverlay, { .ctor = box_fps_overlay_ctor });

    box_fps_monitor_t *ctx = ecs_os_calloc_t(box_fps_monitor_t);
    ctx->frame = -1;
    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "BoxPublishFps" }),
        .phase = EcsPostFrame,
        .query.terms = {{ .id = ecs_id(FpsOverlay) }},
        .callback = BoxPublishFps,
        .ctx = ctx,
        .ctx_free = box_fps_monitor_free,
        .immediate = true
    });
}

/**
 * @file command/cmd_frames.c
 * @brief The fps and frames commands: the frame timing of the recent frames.
 *
 * Both print FrameTiming and RenderStatus in a few lines: the rate, how the frames are paced,
 * the delta against the wall time, the presents and the late and dropped frames.
 */

#include "command.h"

/** Append the line of a range of frame times.
 *
 * @param buf The output
 * @param name Name of the range, padded to one width
 * @param last The last value
 * @param range The range of the recent frames
 */
static void box_cmd_frames_range(
    ecs_strbuf_t *buf,
    const char *name,
    double last,
    const FrameRange *range)
{
    ecs_strbuf_append(buf, "%-9s%7.2f ms  min %.2f  mean %.2f  max %.2f\n",
        name, last, range->min, range->mean, range->max);
}

/** Append how the frames are paced: the cap, vsync, the swap interval and the smoothing.
 *
 * @param buf The output
 * @param timing The frame timing
 * @param render The render status, NULL when box.render is not imported
 */
static void box_cmd_frames_pacing(
    ecs_strbuf_t *buf,
    const FrameTiming *timing,
    const RenderStatus *render)
{
    double fps = timing->interval.mean > 0 ? 1000.0 / timing->interval.mean : 0;
    ecs_strbuf_append(buf, "%.1f fps", fps);
    if (timing->target_ms > 0) {
        ecs_strbuf_append(buf, ", cap %.0f", 1000.0 / timing->target_ms);
    } else {
        ecs_strbuf_appendstr(buf, ", no cap");
    }

    if (timing->fixed) {
        ecs_strbuf_appendstr(buf, ", fixed delta");
    } else if (render && render->vsync && render->swap_interval > 0) {
        ecs_strbuf_append(buf, ", vsync paces: %d refresh(es) of %.0f Hz per frame",
            render->swap_interval, (double)render->refresh_rate);
    } else if (render && render->vsync) {
        ecs_strbuf_appendstr(buf, ", vsync, the cap sleeps");
    } else {
        ecs_strbuf_appendstr(buf, ", no vsync");
    }

    if (timing->smoothing) {
        ecs_strbuf_append(buf, ", delta snapped to %.0f Hz", (double)timing->refresh_rate);
    }

    ecs_strbuf_appendch(buf, '\n');
}

/** The fps and frames commands: print the timing of the recent frames.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The session
 * @param result Receives the output
 */
static void box_cmd_frames(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    const FrameTiming *timing = ecs_singleton_get(ctx->world, FrameTiming);
    const RenderStatus *render = ecs_singleton_get(ctx->world, RenderStatus);
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    (void)argc;
    (void)argv;
    if (!timing || !timing->frames) {
        box_command_error(ctx, result, "no frame timing yet, run a frame first");
        goto done;
    }

    box_cmd_frames_pacing(&buf, timing, render);
    box_cmd_frames_range(&buf, "delta", timing->delta_ms, &timing->delta);
    box_cmd_frames_range(&buf, "interval", timing->interval_ms, &timing->interval);
    box_cmd_frames_range(&buf, "error", timing->error.mean, &timing->error);
    if (timing->present.max > 0) {
        box_cmd_frames_range(&buf, "present", render ? render->present_ms : 0, &timing->present);
    }

    ecs_strbuf_append(&buf,
        "last %d frames: %d late (|delta - interval| > %.1f ms), %d dropped; "
        "since start %lld frames, %lld late, %lld dropped\n",
        timing->frames, timing->late, BOX_FRAME_TOLERANCE_MS, timing->dropped,
        (long long)timing->total, (long long)timing->total_late,
        (long long)timing->total_dropped);
    *(char**)result->ptr = ecs_strbuf_get(&buf);
done:
    return;
}

void box_cmd_frames_init(
    ecs_world_t *world)
{
    box_command_register(world, &(box_command_desc_t){
        .name = "fps",
        .brief = "Frame rate and timing: delta against wall time, presents, late and dropped",
        .callback = box_cmd_frames
    });

    box_command_register(world, &(box_command_desc_t){
        .name = "frames",
        .brief = "Same as fps: the timing of the recent frames",
        .callback = box_cmd_frames
    });
}

/**
 * @file command/cmd_frames.c
 * @brief The fps and frames commands: the frame rate cap and the timing of the recent frames.
 *
 * Both print FrameTiming and RenderStatus in a few lines: the rate, the cap, vsync, the delta
 * against the wall time, the presents and the late and dropped frames. fps <rate> sets App.fps.
 */

#include "command.h"
#include <float.h>
#include <math.h>

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

/** Append the rate of the frames, the cap and whether they present with vsync.
 *
 * @param buf The output
 * @param timing The frame timing
 * @param render The render status, NULL when box.render is not imported
 */
static void box_cmd_frames_rate(
    ecs_strbuf_t *buf,
    const FrameTiming *timing,
    const RenderStatus *render)
{
    double fps = timing->interval.mean > 0 ? 1000.0 / timing->interval.mean : 0;
    double cap = timing->target_ms > 0 ? 1000.0 / timing->target_ms : 0;
    ecs_strbuf_append(buf, "%.1f fps", fps);
    if (cap > 0) {
        ecs_strbuf_append(buf, ", cap %.0f", cap);
    } else {
        ecs_strbuf_appendstr(buf, ", no cap");
    }

    if (render && render->vsync && render->refresh_rate > 0) {
        ecs_strbuf_append(buf, ", vsync %.0f Hz", (double)render->refresh_rate);
    } else if (render && render->vsync) {
        ecs_strbuf_appendstr(buf, ", vsync");
    } else {
        ecs_strbuf_appendstr(buf, ", no vsync");
    }

    ecs_strbuf_appendch(buf, '\n');
}

/** Print the timing of the recent frames.
 *
 * @param ctx The call context
 * @param result Receives the output
 */
static void box_cmd_frames_print(
    const ecs_function_ctx_t *ctx,
    ecs_value_t *result)
{
    const FrameTiming *timing = ecs_singleton_get(ctx->world, FrameTiming);
    const RenderStatus *render = ecs_singleton_get(ctx->world, RenderStatus);
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    if (!timing || !timing->frames) {
        ecs_err("no frame timing yet, run a frame first");
        goto done;
    }

    box_cmd_frames_rate(&buf, timing, render);
    box_cmd_frames_range(&buf, "delta", timing->delta_ms, &timing->delta);
    box_cmd_frames_range(&buf, "interval", timing->interval_ms, &timing->interval);
    box_cmd_frames_range(&buf, "error", timing->error.mean, &timing->error);
    if (timing->present.max > 0) {
        box_cmd_frames_range(&buf, "present", render ? render->present_ms : 0, &timing->present);
    }

    ecs_strbuf_append(&buf,
        "last %d frames: %d late (|delta - interval| > %.1f ms), %d dropped (> %.1f ms); "
        "since start %lld frames, %lld late, %lld dropped\n",
        timing->frames, timing->late, BOX_FRAME_TOLERANCE_MS, timing->dropped,
        timing->target_ms * BOX_FRAME_DROPPED, (long long)timing->total,
        (long long)timing->total_late, (long long)timing->total_dropped);
    if (render && render->threaded) {
        ecs_strbuf_append(&buf, "packets: %d in flight, %d frames waited for a free one\n",
            render->in_flight, render->stalls);
    }

    *(char**)result->ptr = ecs_strbuf_get(&buf);
done:
    return;
}

/** The frames command: print the timing of the recent frames.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv No arguments
 * @param result Receives the output
 */
static void box_cmd_frames(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    (void)argv;
    box_cmd_frames_print(ctx, result);
}

/** The fps command: set App.fps, the target fps of flecs, when given a rate, else print the
 * timing of the recent frames like frames.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The rate
 * @param result Receives the output
 */
static void box_cmd_fps(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    double fps;
    App *app;
    if (argc < 1) {
        box_cmd_frames_print(ctx, result);
        goto done;
    }

    fps = *(ecs_f64_t*)argv[0].ptr;
    if (!isfinite(fps) || fps < 0 || fps > FLT_MAX) {
        ecs_err("invalid rate %g, expected a number >= 0", fps);
        goto done;
    }

    app = ecs_singleton_get_mut(ctx->world, App);
    if (!app) {
        ecs_err("box.app is not imported");
        goto done;
    }

    app->fps = (float)fps;
    ecs_singleton_modified(ctx->world, App);
    if (fps > 0) {
        *(char**)result->ptr = flecs_asprintf("cap %g fps\n", fps);
    } else {
        *(char**)result->ptr = ecs_os_strdup("no cap\n");
    }

done:
    return;
}

void box_cmd_frames_init(
    ecs_world_t *world)
{
    box_command_register(world,
        "Set the frame rate cap, 0 for none, or show the frame timing like frames",
        (ecs_function_desc_t){
            .name = "fps",
            .return_type = ecs_id(ecs_string_t),
            .params = {
                { .name = "rate", .type = ecs_id(ecs_f64_t), .optional = true,
                  .brief = "Frame rate cap, 0 for none (default: show the timing)" }
            },
            .callback = box_cmd_fps
        });

    box_command_register(world,
        "Show the frame rate and timing: delta against wall time, presents, late, dropped",
        (ecs_function_desc_t){
            .name = "frames",
            .return_type = ecs_id(ecs_string_t),
            .callback = box_cmd_frames
        });
}

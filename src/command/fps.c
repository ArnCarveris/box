/**
 * @file command/fps.c
 * @brief The fps command: the frame rate of the last second and the overlay switch.
 *
 * The numbers come from the FpsOverlay of the view, which the monitor fills
 * from the same window the panel draws, so the command adds no source of its
 * own. A world that carries no overlay yet gets one from the first call, off
 * unless the line asked for it, because nothing is sampled while the component
 * is absent; that call reports the frame the world is in, which is the whole of
 * what it has measured.
 */

#include "command_internal.h"
#include "../render/render_internal.h"

/** Entity the overlay of the world is set on.
 *
 * The active view owns the panel once the world renders views of its own; a
 * world without them keeps it on the singleton, which every view falls back to.
 *
 * @param world The world
 * @return The active view, or the FpsOverlay singleton when there is none
 */
static ecs_entity_t box_fps_target(
    ecs_world_t *world)
{
    ecs_entity_t result = 0;
    ecs_entity_t registry = ecs_lookup(world, "box.render.RenderState");
    if (registry) {
        const RenderState *state = ecs_get_id(world, registry, registry);
        result = state ? box_render_primary_view(
            ECS_CONST_CAST(RenderState*, state)) : 0;
    }

    if (!result) {
        result = ecs_id(FpsOverlay);
    }

    return result;
}

/** The fps command: print the window of the last second or switch the panel.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The arguments
 * @param result Receives the output string
 */
static void box_fps_command(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    ecs_world_t *world = ctx->world;
    const char *arg = *(char**)argv[1].ptr;
    bool on = arg && !ecs_os_strcmp(arg, "on");
    bool off = arg && !ecs_os_strcmp(arg, "off");
    char *out = NULL;
    if (arg && arg[0] && !on && !off) {
        out = flecs_asprintf(BOX_COMMAND_ERROR_PREFIX
            "expected 'on' or 'off', got '%s'", arg);
        goto done;
    }

    bool enable = !off;
    ecs_entity_t target = box_fps_target(world);
    const FpsOverlay *overlay = ecs_get(world, target, FpsOverlay);
    const FpsOverlay *window = overlay ? overlay : ecs_singleton_get(world, FpsOverlay);
    if (!overlay || overlay->enabled != enable) {
        FpsOverlay *next = ecs_ensure(world, target, FpsOverlay);
        if (overlay) {
            *next = *overlay;
        }

        next->enabled = enable;
        ecs_modified(world, target, FpsOverlay);
    }

    if (on || off) {
        out = flecs_asprintf("fps overlay %s\n", on ? "on" : "off");
        goto done;
    }

    double rate = window ? window->fps : 0.0;
    double mean = window ? window->mean_ms : 0.0;
    double worst = window ? window->worst_ms : 0.0;
    if (rate <= 0.0) {
        mean = (double)ecs_get_world_info(world)->delta_time * 1000.0;
        worst = mean;
        rate = mean > 0.0 ? 1000.0 / mean : 0.0;
    }

    out = flecs_asprintf("%.0f fps, mean %.2f ms, worst %.2f ms\n", rate, mean, worst);
done:
    *(char**)result->ptr = out;
}

void box_fps_cmd_init(
    ecs_world_t *world)
{
    ECS_IMPORT(world, BoxMonitor);

    box_command_param_t params[] = {{
        .name = "state", .type = ecs_id(ecs_string_t), .optional = true
    }};

    box_command_register(world, "fps",
        "Print the frame rate of the last second, or switch the overlay on or off",
        params, 1, box_fps_command);
}

/**
 * @file command/fps.c
 * @brief The fps command: the frame rate of the last second and the panel switch.
 *
 * The numbers come from the Fps singleton that box.monitor fills every frame.
 * The panel is the box.monitor.fps_panel script and is switched through its
 * exported mut variable. Every call but the off form shows the panel.
 */

#include "command_internal.h"

/** Path of the exported mut variable the frame rate panel script draws on */
#define BOX_FPS_PANEL_VISIBLE "box.monitor.fps_panel.visible"

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

    bool visible = !off;
    if (ecs_mut_var_set_t(world, BOX_FPS_PANEL_VISIBLE, ecs_bool_t, {visible})) {
        out = flecs_asprintf(BOX_COMMAND_ERROR_PREFIX
            "no fps panel at '%s'", BOX_FPS_PANEL_VISIBLE);
        goto done;
    }

    if (on || off) {
        out = flecs_asprintf("fps panel %s\n", on ? "on" : "off");
        goto done;
    }

    const Fps *window = ecs_singleton_get(world, Fps);
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
        "Print the frame rate of the last second, or switch the panel on or off",
        params, 1, box_fps_command);
}

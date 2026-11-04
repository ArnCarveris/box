/**
 * @file command/cmd_audio.c
 * @brief The audio command: the status of the audio and its buses, the master volume and
 * mute, and captures of the master output to a WAV resource.
 *
 * volume and mute only write AudioSettings, which box.audio sends to the core at the end of
 * the frame, as for a change from a script or the REST API. A capture taps the core, so it
 * works with every output; in the browser it is written to the file system of the page.
 */

#include "command.h"
#include "../audio/module.h"
#include <stdlib.h>

/** Folder of the captures in the file system of the page */
#define BOX_CMD_AUDIO_WEB_DIR "/tmp"

/** Get the name of an audio state as the audio command prints it.
 *
 * @param state The state
 * @return The name
 */
static const char* box_cmd_audio_state(
    AudioState state)
{
    const char *result = "off";
    switch (state) {
    case AudioOff: result = "off"; break;
    case AudioNoDevice: result = "no device"; break;
    case AudioWaitingForGesture: result = "waiting for gesture"; break;
    case AudioRunning: result = "running"; break;
    case AudioFailed: result = "failed"; break;
    case AudioStarting: result = "starting"; break;
    case AudioSuspended: result = "suspended"; break;
    }

    return result;
}

/** Append the name of a bus, master for 0 or an entity that is no bus.
 *
 * @param world The world
 * @param buf The output
 * @param bus The bus
 */
static void box_cmd_audio_bus_name(
    ecs_world_t *world,
    ecs_strbuf_t *buf,
    ecs_entity_t bus)
{
    char *path = bus && box_audio_module_bus_index(world, bus) ? ecs_get_path(world, bus) : NULL;
    ecs_strbuf_appendstr(buf, path ? path : "master");
    ecs_os_free(path);
}

/** Append the line of a bus: its parameters, its output and its send.
 *
 * @param world The world
 * @param buf The output
 * @param e The bus
 * @param in_order Whether the bus is in the processing order
 */
static void box_cmd_audio_bus(
    ecs_world_t *world,
    ecs_strbuf_t *buf,
    ecs_entity_t e,
    bool in_order)
{
    const AudioBus *bus = ecs_get(world, e, AudioBus);
    char *path = ecs_get_path(world, e);
    ecs_strbuf_append(buf, "\n  %s: volume %.2f pan %.2f%s%s -> ", path, (double)bus->volume,
        (double)bus->pan, bus->mute ? " muted" : "",
        ecs_has_id(world, e, EcsDisabled) ? " disabled" : "");
    box_cmd_audio_bus_name(world, buf, bus->output);
    if (bus->send && box_audio_module_bus_index(world, bus->send)) {
        ecs_strbuf_append(buf, ", send %.2f -> ", (double)bus->send_level);
        box_cmd_audio_bus_name(world, buf, bus->send);
    }

    const Reverb *reverb = ecs_get(world, e, Reverb);
    const AudioReverbState *rs = ecs_get(world, e, AudioReverbState);
    if (reverb) {
        ecs_strbuf_append(buf, ", reverb decay %.2f damping %.2f predelay %.3f size %.2f "
            "mix %.2f%s", (double)reverb->decay, (double)reverb->damping,
            (double)reverb->predelay, (double)reverb->size, (double)reverb->mix,
            rs && rs->refused ? " (dry, every slot taken)" : "");
    }

    if (!in_order) {
        ecs_strbuf_appendstr(buf, " (not in the order, silent)");
    }

    ecs_os_free(path);
}

/** Print the status of the audio and the buses in processing order.
 *
 * @param world The world
 * @return The text, which the caller frees
 */
static char* box_cmd_audio_print(
    ecs_world_t *world)
{
    const AudioStatus *status = ecs_singleton_get(world, AudioStatus);
    const AudioSettings *settings = ecs_singleton_get(world, AudioSettings);
    ecs_entity_t order[BOX_AUDIO_BUS_MAX];
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    int32_t i, k, count = box_audio_module_order(world, order, BOX_AUDIO_BUS_MAX);
    AudioStatus s = status ? *status : (AudioStatus){0};
    ecs_strbuf_append(&buf, "%s, %d Hz, %d voices, %lld stolen, load %.1f%%",
        box_cmd_audio_state(s.state), s.sample_rate, s.voices, (long long)s.voices_stolen,
        (double)(s.load * 100.0f));
    ecs_strbuf_append(&buf, "\nclock: %lld frames (%.3f s)", (long long)s.clock,
        s.sample_rate > 0 ? (double)s.clock / (double)s.sample_rate : 0.0);
    ecs_strbuf_append(&buf, "\nmaster: volume %.2f%s",
        settings ? (double)settings->master_volume : 1.0, settings && settings->mute ?
            " muted" : "");
    for (i = 0; i < count; i ++) {
        box_cmd_audio_bus(world, &buf, order[i], true);
    }

    ecs_iter_t it = ecs_each(world, AudioBus);
    while (ecs_each_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            for (k = 0; k < count && order[k] != it.entities[i]; k ++) { }
            if (k == count) {
                box_cmd_audio_bus(world, &buf, it.entities[i], false);
            }
        }
    }

    return ecs_strbuf_get(&buf);
}

/** Set the master volume.
 *
 * @param world The world
 * @param value The volume as text
 * @return The text, which the caller frees, NULL on an error
 */
static char* box_cmd_audio_volume(
    ecs_world_t *world,
    const char *value)
{
    char *end = NULL;
    double volume = value ? strtod(value, &end) : 0;
    AudioSettings *settings;
    char *result = NULL;
    if (!value || !value[0] || !end || end[0] || !(volume >= 0) ||
        volume > BOX_AUDIO_VOLUME_MAX)
    {
        ecs_err("invalid volume '%s', expected 0 to %.0f", value ? value : "",
            (double)BOX_AUDIO_VOLUME_MAX);
        goto done;
    }

    settings = ecs_singleton_ensure(world, AudioSettings);
    settings->master_volume = (float)volume;
    ecs_singleton_modified(world, AudioSettings);
    result = flecs_asprintf("master volume %.2f", volume);
done:
    return result;
}

/** Set or toggle the mute of everything.
 *
 * @param world The world
 * @param value on or off, NULL or empty to toggle
 * @return The text, which the caller frees, NULL on an error
 */
static char* box_cmd_audio_mute(
    ecs_world_t *world,
    const char *value)
{
    AudioSettings *settings = ecs_singleton_ensure(world, AudioSettings);
    char *result = NULL;
    bool mute;
    if (!value || !value[0]) {
        mute = !settings->mute;
    } else if (!ecs_os_strcmp(value, "on")) {
        mute = true;
    } else if (!ecs_os_strcmp(value, "off")) {
        mute = false;
    } else {
        ecs_err("invalid state '%s', expected on or off", value);
        goto done;
    }

    settings->mute = mute;
    ecs_singleton_modified(world, AudioSettings);
    result = ecs_os_strdup(mute ? "mute on" : "mute off");
done:
    return result;
}

/** Start a capture of the master output to a WAV resource, or to the file system of the page
 * in the browser.
 *
 * @param world The world
 * @param value The seconds as text
 * @return The name of the resource, or the file in the browser, which the caller frees, NULL
 * on an error
 */
static char* box_cmd_audio_capture(
    ecs_world_t *world,
    const char *value)
{
    char *end = NULL, *path = NULL, *result = NULL;
    double seconds = value ? strtod(value, &end) : 0;
    if (!value || !value[0] || !end || end[0]) {
        ecs_err("invalid seconds '%s'", value ? value : "");
        goto done;
    }

    if (box_audio_capture_pending(world)) {
        ecs_err("box.audio: a capture is already recording");
        goto done;
    }

#ifdef __EMSCRIPTEN__
    static int32_t box_cmd_audio_captures;
    path = flecs_asprintf("%s/audio_capture_%d.wav", BOX_CMD_AUDIO_WEB_DIR,
        ++ box_cmd_audio_captures);
    if (!box_audio_capture(world, (float)seconds, path)) {
        result = path;
        path = NULL;
    }
#else
    ecs_shell_resource_t resource = {0};
    if (!(seconds > 0) || seconds > BOX_AUDIO_CAPTURE_MAX) {
        ecs_err("box.audio: a capture takes more than 0 and at most %.0f seconds",
            (double)BOX_AUDIO_CAPTURE_MAX);
        goto done;
    }

    path = box_cmd_render_resource(world, "audio/wav", &resource);
    if (path && !box_audio_capture(world, (float)seconds, path)) {
        result = ecs_os_strdup(resource.name);
    }

    ecs_os_free(resource.name);
    ecs_os_free(resource.content_type);
#endif
done:
    ecs_os_free(path);

    return result;
}

/** The audio command: print the status, or set the volume or the mute, or start a capture.
 *
 * @param ctx The call context
 * @param argc Number of arguments
 * @param argv The action and its value, both optional
 * @param result Receives the output
 */
static void box_cmd_audio(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    ecs_world_t *world = ctx->world;
    const char *action = argc > 0 ? *(const char**)argv[0].ptr : NULL;
    const char *value = argc > 1 ? *(const char**)argv[1].ptr : NULL;
    char *out = NULL;
    if (!ecs_id(AudioSettings) || !box_audio_module_get(world)) {
        ecs_err("box.audio is not imported");
        goto done;
    }

    if (!action || !action[0]) {
        out = box_cmd_audio_print(world);
    } else if (!ecs_os_strcmp(action, "volume")) {
        out = box_cmd_audio_volume(world, value);
    } else if (!ecs_os_strcmp(action, "mute")) {
        out = box_cmd_audio_mute(world, value);
    } else if (!ecs_os_strcmp(action, "capture")) {
        out = box_cmd_audio_capture(world, value);
    } else {
        ecs_err("unknown action '%s', expected volume, mute or capture", action);
    }

    *(char**)result->ptr = out;
done:
    return;
}

void box_cmd_audio_init(
    ecs_world_t *world)
{
    ECS_IMPORT(world, BoxAudio);
    box_command_register(world,
        "Print the audio status and buses, set the volume or mute, or capture a WAV",
        (ecs_function_desc_t){
            .name = "audio",
            .return_type = ecs_id(ecs_string_t),
            .params = {
                { .name = "action", .type = ecs_id(ecs_string_t), .optional = true,
                  .brief = "volume, mute or capture (default: print the status)" },
                { .name = "value", .type = ecs_id(ecs_string_t), .optional = true,
                  .brief = "volume 0 to 4, mute on or off (default: toggle), capture seconds" }
            },
            .callback = box_cmd_audio
        });
}

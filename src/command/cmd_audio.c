/**
 * @file command/cmd_audio.c
<<<<<<< HEAD
 * @brief The audio command: the status of the audio, its buses and their insert slots, the
 * master volume and mute, and captures of the master output to a WAV resource.
=======
 * @brief The audio command: the status of the audio, its transport and its buses, the master
 * volume and mute, play, stop, seek and tempo, and captures of the master output to a WAV.
>>>>>>> 58db0f88561a6a93fa04df5c5bb60914947cdba3
 *
 * The actions only write AudioSettings or Transport, which box.audio sends to the core at the
 * end of the frame, as for a change from a script or the REST API. A capture taps the core, so
 * it works with every output; in the browser it is written to the file system of the page.
 */

#include "command.h"
#include "../audio/module.h"
#include <stdlib.h>
#include <math.h>

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

/** Get the name of a channel kind as the audio command prints it.
 *
 * @param kind The kind
 * @return The name
 */
static const char* box_cmd_audio_kind(
    AudioChannelKind kind)
{
    const char *result = "group";
    switch (kind) {
    case AudioChannelGroup: result = "group"; break;
    case AudioChannelReturn: result = "return"; break;
    case AudioChannelSfx: result = "sfx"; break;
    }

    return result;
}

/** Tell whether the effect of an insert slot runs, and why not.
 *
 * @param world The world
 * @param channel The bus entity of the slot
 * @param j Index of the slot
 * @param e The effect entity
 * @return "runs", "bypassed" or why it does not run
 */
static const char* box_cmd_audio_effect_state(
    ecs_world_t *world,
    ecs_entity_t channel,
    int32_t j,
    ecs_entity_t e)
{
    box_audio_module_t *state = box_audio_module_get(world);
    const AudioEffectState *es = ecs_get(world, e, AudioEffectState);
    int32_t slot = box_audio_module_effect_slot(world, e);
    uint32_t kind = box_audio_module_effect_kind(world, e, NULL);
    const char *result = "runs";
    if (!state) {
        result = "does not run: no output";
    } else if (es && state->effect_frame && es->frame == state->effect_frame &&
        (es->channel != channel || es->index != j))
    {
        result = "does not run: in another slot";
    } else if (slot < 0 && es && es->refused) {
        result = "does not run: every effect of the pool is taken";
    } else if (!state->serial) {
        result = "does not run: no output";
    } else if (slot < 0) {
        result = "does not run: no slot yet";
    } else if (ecs_has_id(world, e, EcsDisabled)) {
        result = "bypassed";
    } else if (box_audio_effect_floats(kind, state->rate) > 0 &&
        (state->effects[slot].memory != state->serial ||
            state->effects[slot].floats < box_audio_effect_floats(kind, state->rate)))
    {
        result = "does not run: waits for its memory";
    }

    return result;
}

/** Append the insert slots of a bus: each effect with its kind, its parameters and whether it
 * runs.
 *
 * @param world The world
 * @param buf The output
 * @param channel The bus entity
 * @param bus The bus
 */
static void box_cmd_audio_effects(
    ecs_world_t *world,
    ecs_strbuf_t *buf,
    ecs_entity_t channel,
    const AudioBus *bus)
{
    int32_t j;
    for (j = 0; j < BOX_AUDIO_EFFECT_SLOTS; j ++) {
        ecs_entity_t e = bus->effects[j];
        const Reverb *reverb;
        char *path;
        if (!e) {
            continue;
        }

        if (!ecs_is_alive(world, e) ||
            box_audio_module_effect_kind(world, e, NULL) == BOX_AUDIO_EFFECT_NONE)
        {
            ecs_strbuf_append(buf, ", insert %d: no effect", j);
            continue;
        }

        path = ecs_get_path(world, e);
        ecs_strbuf_append(buf, ", insert %d %s: ", j, path);
        reverb = ecs_get(world, e, Reverb);
        if (reverb) {
            ecs_strbuf_append(buf, "reverb decay %.2f damping %.2f predelay %.3f size %.2f "
                "mix %.2f", (double)reverb->decay, (double)reverb->damping,
                (double)reverb->predelay, (double)reverb->size, (double)reverb->mix);
        }

        ecs_strbuf_append(buf, " (%s)", box_cmd_audio_effect_state(world, channel, j, e));
        ecs_os_free(path);
    }
}

/** Append the line of a bus: its kind, its parameters, its output, its sends and its insert
 * slots.
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
    const AudioSend *sends = ecs_vec_first(&bus->sends);
    int32_t j, count = ecs_vec_count(&bus->sends);
    ecs_strbuf_append(buf, "\n  %s: %s volume %.2f pan %.2f width %.2f%s%s%s -> ", path,
        box_cmd_audio_kind(bus->kind), (double)bus->volume, (double)bus->pan,
        (double)bus->width, bus->solo ? " solo" : "", bus->mute ? " muted" : "",
        ecs_has_id(world, e, EcsDisabled) ? " disabled" : "");
    box_cmd_audio_bus_name(world, buf, bus->output);
    for (j = 0; j < count; j ++) {
        if (j >= BOX_AUDIO_SENDS) {
            ecs_strbuf_append(buf, ", %d more sends ignored", count - BOX_AUDIO_SENDS);
            break;
        }

        if (sends[j].bus && box_audio_module_bus_index(world, sends[j].bus)) {
            ecs_strbuf_append(buf, ", send%s %.2f -> ", sends[j].pre_fader ? " pre" : "",
                (double)sends[j].level);
            box_cmd_audio_bus_name(world, buf, sends[j].bus);
        }
    }

    box_cmd_audio_effects(world, buf, e, bus);

    if (!in_order) {
        ecs_strbuf_appendstr(buf, " (not in the order, silent)");
    }

    ecs_os_free(path);
}

/** Append the line of the transport: tempo, signature, position as bar.beat, playing, loop.
 *
 * @param world The world
 * @param buf The output
 */
static void box_cmd_audio_transport(
    ecs_world_t *world,
    ecs_strbuf_t *buf)
{
    const Transport *tr = ecs_singleton_get(world, Transport);
    Transport t = tr ? *tr : (Transport){ .tempo = 120, .beats_per_bar = 4, .beat_unit = 4 };
    int32_t top = t.beats_per_bar > 0 ? t.beats_per_bar : 1;
    int32_t unit = t.beat_unit > 0 ? t.beat_unit : 1;
    double bar = (double)top * 4.0 / (double)unit;
    double position = isfinite(t.position) ? t.position : 0.0;
    double bars = floor(position / bar);
    double beat = floor(position - bars * bar);
    ecs_strbuf_append(buf, "\ntransport: %.1f bpm %d/%d, position %.0f.%.0f (%.3f beats), %s",
        (double)t.tempo, top, unit, bars + 1.0, beat + 1.0, position,
        t.playing ? "playing" : "stopped");
    if (t.loop) {
        ecs_strbuf_append(buf, ", loop %.3f to %.3f", t.loop_start, t.loop_end);
    } else {
        ecs_strbuf_appendstr(buf, ", loop off");
    }

    if (t.metronome) {
        ecs_strbuf_appendstr(buf, ", metronome");
    }
}

/** Play, stop, seek or set the tempo of the transport.
 *
 * @param world The world
 * @param action play, stop, seek or tempo
 * @param value Beats of a seek, quarter notes per minute of a tempo
 * @return The text, which the caller frees, NULL on an error
 */
static char* box_cmd_audio_transport_set(
    ecs_world_t *world,
    const char *action,
    const char *value)
{
    char *end = NULL, *result = NULL;
    double number = value ? strtod(value, &end) : 0;
    bool valid = value && value[0] && end && !end[0] && isfinite(number);
    Transport *tr = ecs_singleton_ensure(world, Transport);
    if (!ecs_os_strcmp(action, "play")) {
        tr->playing = true;
        result = flecs_asprintf("playing from %.3f", tr->position);
    } else if (!ecs_os_strcmp(action, "stop")) {
        tr->playing = false;
        result = ecs_os_strdup("stopped");
    } else if (!ecs_os_strcmp(action, "seek") && valid) {
        tr->position = number;
        result = flecs_asprintf("position %.3f", number);
    } else if (!ecs_os_strcmp(action, "tempo") && valid && number >= 20 && number <= 999) {
        tr->tempo = (float)number;
        result = flecs_asprintf("tempo %.1f", number);
    } else if (!ecs_os_strcmp(action, "seek")) {
        ecs_err("invalid position '%s', expected beats", value ? value : "");
    } else {
        ecs_err("invalid tempo '%s', expected 20 to 999", value ? value : "");
    }

    if (result) {
        ecs_singleton_modified(world, Transport);
    }

    return result;
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
    box_cmd_audio_transport(world, &buf);
    ecs_entity_t master = ecs_lookup(world, "box.audio.master");
    const AudioBus *mb = master ? ecs_get(world, master, AudioBus) : NULL;
    ecs_strbuf_append(&buf, "\nmaster: volume %.2f%s",
        settings ? (double)settings->master_volume : 1.0, settings && settings->mute ?
            " muted" : "");
    if (mb) {
        ecs_strbuf_append(&buf, ", bus volume %.2f pan %.2f width %.2f%s", (double)mb->volume,
            (double)mb->pan, (double)mb->width, mb->mute ? " muted" : "");
        box_cmd_audio_effects(world, &buf, master, mb);
    }

    for (i = 0; i < count; i ++) {
        box_cmd_audio_bus(world, &buf, order[i], true);
    }

    ecs_iter_t it = ecs_each(world, AudioBus);
    while (ecs_each_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            for (k = 0; k < count && order[k] != it.entities[i]; k ++) { }
            if (k == count && it.entities[i] != master) {
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

/** The audio command: print the status, set the volume or the mute, drive the transport or
 * start a capture.
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
    } else if (!ecs_os_strcmp(action, "play") || !ecs_os_strcmp(action, "stop") ||
        !ecs_os_strcmp(action, "seek") || !ecs_os_strcmp(action, "tempo"))
    {
        out = box_cmd_audio_transport_set(world, action, value);
    } else {
        ecs_err("unknown action '%s', expected volume, mute, capture, play, stop, seek or "
            "tempo", action);
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
        "Print the audio status, transport and buses, set the volume or mute, play, stop, "
        "seek or set the tempo, or capture a WAV",
        (ecs_function_desc_t){
            .name = "audio",
            .return_type = ecs_id(ecs_string_t),
            .params = {
                { .name = "action", .type = ecs_id(ecs_string_t), .optional = true,
                  .brief = "volume, mute, capture, play, stop, seek or tempo (default: print "
                    "the status)" },
                { .name = "value", .type = ecs_id(ecs_string_t), .optional = true,
                  .brief = "volume 0 to 4, mute on or off (default: toggle), capture seconds, "
                    "seek beats, tempo 20 to 999" }
            },
            .callback = box_cmd_audio
        });
}

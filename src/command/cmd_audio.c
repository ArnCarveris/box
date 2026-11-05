/**
 * @file command/cmd_audio.c
 * @brief The audio command: the status of the audio, its transport, its buses and their insert
 * slots, its instruments, its tracks with their note and audio placements, sounding notes and
 * playing audio clips, the master
 * volume and mute, play, stop, seek and tempo, and captures of the master output to a WAV
 * resource.
 *
 * The actions only write AudioSettings or Transport, which box.audio sends to the core at the
 * end of the frame, as for a change from a script or the REST API. A capture taps the core, so
 * it works with every output; in the browser it is written to the file system of the page.
 */

#include "command.h"
#include "../audio/module.h"
#include <stdlib.h>
#include <math.h>

/** Sounding notes listed per track at most */
#define BOX_AUDIO_SEQ_LIST_MAX 16

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

/** Name a kind of a band of an equalizer.
 *
 * @param kind The kind
 * @return Its name
 */
static const char* box_cmd_audio_eq_kind(
    AudioEqKind kind)
{
    static const char *names[] = {
        "off", "peak", "low shelf", "high shelf", "low cut", "high cut"
    };

    return (uint32_t)kind < 6 ? names[kind] : "?";
}

/** Name a kind of a filter.
 *
 * @param kind The kind
 * @return Its name
 */
static const char* box_cmd_audio_filter_kind(
    SynthFilterKind kind)
{
    static const char *names[] = { "lowpass", "highpass", "bandpass", "notch" };

    return (uint32_t)kind < 4 ? names[kind] : "?";
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
        const Equalizer *eq;
        const AudioFilter *filter;
        const Compressor *comp;
        const Gate *gate;
        const Delay *delay;
        const Phaser *phaser;
        const Chorus *chorus;
        const Saturator *saturator;
        const TransientShaper *shaper;
        const box_audio_module_t *state = box_audio_module_get(world);
        char *path;
        int32_t b, slot;
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
        } else if ((eq = ecs_get(world, e, Equalizer))) {
            ecs_strbuf_appendstr(buf, "eq");
            for (b = 0; b < BOX_AUDIO_EQ_BANDS; b ++) {
                const AudioEqBand *band = &eq->bands[b];
                if (band->kind == AudioEqOff) {
                    ecs_strbuf_appendstr(buf, " [off]");
                } else {
                    ecs_strbuf_append(buf, " [%s %.0f Hz %.1f dB q %.2f]",
                        box_cmd_audio_eq_kind(band->kind), (double)band->frequency,
                        (double)band->gain, (double)band->q);
                }
            }
        } else if ((filter = ecs_get(world, e, AudioFilter))) {
            ecs_strbuf_append(buf, "filter %s %d dB cutoff %.0f resonance %.2f mix %.2f",
                box_cmd_audio_filter_kind(filter->kind),
                filter->slope == SynthSlope24 ? 24 : 12, (double)filter->cutoff,
                (double)filter->resonance, (double)filter->mix);
        } else if ((comp = ecs_get(world, e, Compressor))) {
            ecs_strbuf_append(buf, "compressor threshold %.1f dB ratio %.1f attack %.3f "
                "release %.3f knee %.1f dB makeup %.1f dB mix %.2f", (double)comp->threshold,
                (double)comp->ratio, (double)comp->attack, (double)comp->release,
                (double)comp->knee, (double)comp->makeup, (double)comp->mix);
            if (comp->sidechain) {
                char *key = ecs_is_alive(world, comp->sidechain) ?
                    ecs_get_path(world, comp->sidechain) : NULL;
                ecs_strbuf_append(buf, " sidechain %s", key ? key : "(deleted)");
                ecs_os_free(key);
            }
        } else if ((gate = ecs_get(world, e, Gate))) {
            ecs_strbuf_append(buf, "gate threshold %.1f dB attack %.4f hold %.3f release %.3f "
                "range %.1f dB", (double)gate->threshold, (double)gate->attack,
                (double)gate->hold, (double)gate->release, (double)gate->range);
        } else if ((delay = ecs_get(world, e, Delay))) {
            if (delay->sync > 0) {
                ecs_strbuf_append(buf, "delay sync %.3f beats", (double)delay->sync);
            } else {
                ecs_strbuf_append(buf, "delay time %.3f", (double)delay->time);
            }

            ecs_strbuf_append(buf, " feedback %.2f damping %.0f Hz mix %.2f%s",
                (double)delay->feedback, (double)delay->damping, (double)delay->mix,
                delay->ping_pong ? " ping pong" : "");
        } else if ((phaser = ecs_get(world, e, Phaser))) {
            ecs_strbuf_append(buf, "phaser stages %d rate %.2f Hz depth %.2f center %.0f Hz "
                "feedback %.2f mix %.2f stereo %.2f", phaser->stages, (double)phaser->rate,
                (double)phaser->depth, (double)phaser->center, (double)phaser->feedback,
                (double)phaser->mix, (double)phaser->stereo);
        } else if ((chorus = ecs_get(world, e, Chorus))) {
            ecs_strbuf_append(buf, "chorus voices %d rate %.2f Hz depth %.2f ms delay %.2f ms "
                "mix %.2f spread %.2f feedback %.2f", chorus->voices, (double)chorus->rate,
                (double)chorus->depth, (double)chorus->delay, (double)chorus->mix,
                (double)chorus->spread, (double)chorus->feedback);
        } else if ((saturator = ecs_get(world, e, Saturator))) {
            static const char *modes[] = { "tanh", "tube", "hard", "fold", "rectify" };
            ecs_strbuf_appendstr(buf, "saturator");
            if (saturator->mode != SaturatorTanh && saturator->mode >= 0 &&
                saturator->mode <= SaturatorRectify)
            {
                ecs_strbuf_append(buf, " %s", modes[saturator->mode]);
            }

            ecs_strbuf_append(buf, " drive %.2f asymmetry %.2f",
                (double)saturator->drive, (double)saturator->asymmetry);
            if (saturator->low_cut > 0) {
                ecs_strbuf_append(buf, " low cut %.0f Hz", (double)saturator->low_cut);
            }

            if (saturator->tone != 0) {
                ecs_strbuf_append(buf, " tone %.2f", (double)saturator->tone);
            }

            if (saturator->high_cut > 0) {
                ecs_strbuf_append(buf, " high cut %.0f Hz", (double)saturator->high_cut);
            }

            if (saturator->rate > 0) {
                ecs_strbuf_append(buf, " rate %.0f Hz", (double)saturator->rate);
            }

            if (saturator->bits < 16) {
                ecs_strbuf_append(buf, " bits %.1f", (double)saturator->bits);
            } else {
                ecs_strbuf_appendstr(buf, " bits off");
            }

            ecs_strbuf_append(buf, " mix %.2f", (double)saturator->mix);
        } else if ((shaper = ecs_get(world, e, TransientShaper))) {
            ecs_strbuf_append(buf, "transient shaper attack %.2f sustain %.2f",
                (double)shaper->attack, (double)shaper->sustain);
        }

        slot = box_audio_module_effect_slot(world, e);
        if ((ecs_has(world, e, Compressor) || ecs_has(world, e, Gate) ||
            ecs_has(world, e, Saturator)) && state && slot >= 0)
        {
            ecs_strbuf_append(buf, " reduction %.1f dB", (double)state->reduction[slot]);
        }

        ecs_strbuf_append(buf, " (%s)", box_cmd_audio_effect_state(world, channel, j, e));
        ecs_os_free(path);
    }
}

/** Append a linear level in dB, -inf for silence.
 *
 * @param buf The output
 * @param level The level
 */
static void box_cmd_audio_db(
    ecs_strbuf_t *buf,
    float level)
{
    if (level > 1e-10f) {
        ecs_strbuf_append(buf, "%.1f", 20.0 * log10((double)level));
    } else {
        ecs_strbuf_appendstr(buf, "-inf");
    }
}

/** Append the levels the module measured for a channel in the last frame: the peak and the
 * RMS of each channel in dB, left/right.
 *
 * @param world The world
 * @param buf The output
 * @param e The master or a bus entity
 */
static void box_cmd_audio_meter(
    ecs_world_t *world,
    ecs_strbuf_t *buf,
    ecs_entity_t e)
{
    AudioMeter m;
    if (!box_audio_module_meter(world, e, &m)) {
        goto done;
    }

    ecs_strbuf_appendstr(buf, ", peak ");
    box_cmd_audio_db(buf, m.peak_left);
    ecs_strbuf_appendstr(buf, "/");
    box_cmd_audio_db(buf, m.peak_right);
    ecs_strbuf_appendstr(buf, " dB, rms ");
    box_cmd_audio_db(buf, m.rms_left);
    ecs_strbuf_appendstr(buf, "/");
    box_cmd_audio_db(buf, m.rms_right);
    ecs_strbuf_appendstr(buf, " dB");
done:
    return;
}

/** Append the line of a bus: its kind, its parameters, its output, its sends, its insert
 * slots and its levels.
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
    if (in_order) {
        box_cmd_audio_meter(world, buf, e);
    }

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

/** Append the lines of the instruments: synth or sampler, the sound, the root, the velocity
 * curve and the gate with its release.
 *
 * @param world The world
 * @param buf The output
 */
static void box_cmd_audio_instruments(
    ecs_world_t *world,
    ecs_strbuf_t *buf)
{
    int32_t i;
    ecs_iter_t it = ecs_each(world, Instrument);
    while (ecs_each_next(&it)) {
        const Instrument *inst = ecs_field(&it, Instrument, 0);
        for (i = 0; i < it.count; i ++) {
            ecs_entity_t sound = inst[i].sound;
            bool alive = sound && ecs_is_alive(world, sound);
            bool synth = alive && ecs_has(world, sound, SynthSound);
            char *path = ecs_get_path(world, it.entities[i]);
            char *name = alive ? ecs_get_path(world, sound) : NULL;
            ecs_strbuf_append(buf, "\n  instrument %s: %s %s, root %.2f, velocity curve %.2f",
                path, synth ? "synth" : (alive ? "sampler" : "no sound"), name ? name : "",
                (double)inst[i].root, (double)inst[i].velocity_curve);
            if (!inst[i].gate) {
                ecs_strbuf_appendstr(buf, ", no gate");
            } else if (synth) {
                ecs_strbuf_appendstr(buf, ", gate, envelope release");
            } else {
                ecs_strbuf_append(buf, ", gate, release %.3f s", (double)inst[i].release);
            }

            ecs_os_free(name);
            ecs_os_free(path);
        }
    }
}

/** Append the placements of a track, note and audio ones, each with its clip, start, duration,
 * loop, offset, gain and transpose.
 *
 * @param world The world
 * @param buf The output
 * @param track The track
 */
static void box_cmd_audio_placements(
    ecs_world_t *world,
    ecs_strbuf_t *buf,
    ecs_entity_t track)
{
    int32_t i;
    ecs_iter_t it = ecs_children(world, track);
    while (ecs_children_next(&it)) {
        for (i = 0; i < it.count; i ++) {
            const ClipPlacement *p = ecs_get(world, it.entities[i], ClipPlacement);
            const NoteClip *clip;
            const AudioClip *audio;
            char *path, *name, *sound;
            bool alive;
            if (!p) {
                continue;
            }

            alive = p->clip && ecs_is_alive(world, p->clip);
            clip = alive ? ecs_get(world, p->clip, NoteClip) : NULL;
            audio = alive ? ecs_get(world, p->clip, AudioClip) : NULL;
            path = ecs_get_path(world, it.entities[i]);
            name = clip || audio ? ecs_get_path(world, p->clip) : NULL;
            ecs_strbuf_append(buf, "\n    placement %s: clip %s", path, name ? name : "none");
            if (clip) {
                ecs_strbuf_append(buf, " (%d notes, %.3f beats)",
                    ecs_vec_count(&clip->notes), (double)clip->length);
            }

            if (audio) {
                sound = audio->sound && ecs_is_alive(world, audio->sound) ?
                    ecs_get_path(world, audio->sound) : NULL;
                ecs_strbuf_append(buf, " (audio %s from %.3f s, %.3f beats)",
                    sound ? sound : "none", (double)audio->offset, (double)audio->length);
                ecs_os_free(sound);
            }

            ecs_strbuf_append(buf, ", start %.3f", p->start);
            if (p->duration > 0) {
                ecs_strbuf_append(buf, ", duration %.3f", p->duration);
            } else {
                ecs_strbuf_appendstr(buf, ", duration of the clip");
            }

            if (p->loop) {
                ecs_strbuf_appendstr(buf, ", loop");
            }

            if (p->offset != 0) {
                ecs_strbuf_append(buf, ", offset %.3f", p->offset);
            }

            ecs_strbuf_append(buf, ", gain %.2f, transpose %.2f%s", (double)p->gain,
                (double)p->transpose, ecs_has_id(world, it.entities[i], EcsDisabled) ?
                    ", disabled" : "");
            ecs_os_free(name);
            ecs_os_free(path);
        }
    }
}

/** Append the lines of the tracks: the instrument, the placements, the notes that sound and
 * the audio clips that play.
 *
 * @param world The world
 * @param buf The output
 */
static void box_cmd_audio_tracks(
    ecs_world_t *world,
    ecs_strbuf_t *buf)
{
    box_audio_module_t *state = box_audio_module_get(world);
    const box_audio_sounding_t *sounding = ecs_vec_first(&state->sequencer.sounding);
    const box_audio_clip_voice_t *voices = ecs_vec_first(&state->sequencer.voices);
    int32_t i, k, count = ecs_vec_count(&state->sequencer.sounding), listed, total;
    int32_t voice_count = ecs_vec_count(&state->sequencer.voices);
    ecs_query_t *q = ecs_query(world, {
        .terms = {{ .id = ecs_id(Track), .inout = EcsIn, .src.id = EcsSelf }},
        .flags = EcsQueryMatchDisabled
    });
    ecs_iter_t it = ecs_query_iter(world, q);
    while (ecs_query_next(&it)) {
        const Track *tk = ecs_field(&it, Track, 0);
        for (i = 0; i < it.count; i ++) {
            ecs_entity_t inst = tk[i].instrument;
            bool alive = inst && ecs_is_alive(world, inst);
            char *path = ecs_get_path(world, it.entities[i]);
            char *name = alive ? ecs_get_path(world, inst) : NULL;
            ecs_strbuf_append(buf, "\n  track %s: instrument %s%s, priority %d%s", path,
                name ? name : "none", alive && !ecs_has(world, inst, Instrument) ?
                    " (no Instrument)" : "", tk[i].priority,
                ecs_has_id(world, it.entities[i], EcsDisabled) ? ", disabled" : "");
            box_cmd_audio_placements(world, buf, it.entities[i]);
            for (k = 0, listed = 0, total = 0; k < count; k ++) {
                if (sounding[k].track != it.entities[i]) {
                    continue;
                }

                total ++;
                if (listed < BOX_AUDIO_SEQ_LIST_MAX) {
                    ecs_strbuf_append(buf, "\n    sounding: pitch %.2f until beat %.3f%s",
                        (double)sounding[k].pitch, sounding[k].end,
                        sounding[k].release ? ", released" : "");
                    listed ++;
                }
            }

            if (total > listed) {
                ecs_strbuf_append(buf, "\n    %d more sounding", total - listed);
            }

            for (k = 0, listed = 0, total = 0; k < voice_count; k ++) {
                if (voices[k].track != it.entities[i]) {
                    continue;
                }

                total ++;
                if (listed < BOX_AUDIO_SEQ_LIST_MAX) {
                    ecs_strbuf_append(buf, "\n    playing: audio until beat %.3f%s",
                        voices[k].end, voices[k].stop ? ", stopped" : "");
                    listed ++;
                }
            }

            if (total > listed) {
                ecs_strbuf_append(buf, "\n    %d more playing", total - listed);
            }

            ecs_os_free(name);
            ecs_os_free(path);
        }
    }

    ecs_query_fini(q);
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
    ecs_strbuf_append(&buf, "%s, %d Hz, %d voices, %lld stolen, %lld refused, load %.1f%%",
        box_cmd_audio_state(s.state), s.sample_rate, s.voices, (long long)s.voices_stolen,
        (long long)s.voices_refused, (double)(s.load * 100.0f));
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

    box_cmd_audio_meter(world, &buf, master);

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

    box_cmd_audio_instruments(world, &buf);
    box_cmd_audio_tracks(world, &buf);

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
        "Print the audio status, transport, buses, instruments and tracks, set the volume or mute, "
        "play, stop, seek or set the tempo, or capture a WAV",
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

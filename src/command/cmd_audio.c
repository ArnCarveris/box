/**
 * @file command/cmd_audio.c
 * @brief The audio command: the status of the audio, its transport, its buses and their insert
 * slots, its instruments, its tracks with their note and audio placements, sounding notes and
 * playing audio clips, its emitters with their plays and skips, its macros with their maps, the
 * master volume and mute, play, stop, seek and tempo, the song of AudioSong, captures of the
 * master output to a WAV resource, the analysis of a WAV (cmd_audio_analyze.c), the
 * comparison of two (cmd_audio_diff.c), its plots (cmd_audio_plot.c), the levels of the buses
 * over a span (cmd_audio_meter.c) and the dispatch of audio.render (cmd_audio_render.c) and
 * audio.check (cmd_audio_check.c). Solo,
 * mute of a bus, bypass and note are in cmd_audio_audition.c.
 *
 * The actions only write AudioSettings, Transport or AudioSong, which box.audio applies at the
 * end of the frame as for a change from a script or the REST API. A capture taps the core, so
 * it works with every output; in the browser it goes to the file system of the page.
 */

#include "command.h"
#include "../audio/module.h"
#include <stdlib.h>
#include <math.h>

/** Sounding notes listed per track at most */
#define BOX_AUDIO_SEQ_LIST_MAX 16

#define BOX_CMD_AUDIO_OPTIONS_MAX 9

#define BOX_CMD_AUDIO_NOTE_DUR 0.5

#ifdef __EMSCRIPTEN__
#define BOX_CMD_AUDIO_RESOURCE ecs_id(ecs_string_t)
#else
#define BOX_CMD_AUDIO_RESOURCE ecs_id(ecs_shell_resource_t)
#endif

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
    } else if (ecs_has_id(world, e, EcsDisabled)) {
        result = "bypassed";
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

/** Append the modulators that move a member of a bus or an effect: each with its source, its
 * timing, its depth, its member and whether it runs.
 *
 * @param world The world
 * @param buf The output
 * @param target The bus or effect entity
 */
static void box_cmd_audio_modulators(
    ecs_world_t *world,
    ecs_strbuf_t *buf,
    ecs_entity_t target)
{
    static const char *waves[] = { "sine", "triangle", "square", "saw up", "saw down",
        "random" };
    ecs_iter_t it = ecs_each(world, AudioModulator);
    int32_t i;
    while (ecs_each_next(&it)) {
        const AudioModulator *m = ecs_field(&it, AudioModulator, 0);
        for (i = 0; i < it.count; i ++) {
            char *path;
            if (m[i].target != target) {
                continue;
            }

            path = ecs_get_path(world, it.entities[i]);
            ecs_strbuf_append(buf, ", modulator %s: ", path);
            if (m[i].source == AudioModSteps) {
                ecs_strbuf_append(buf, "%d steps smooth %.2f", m[i].step_count,
                    (double)m[i].smooth);
            } else if (m[i].source == AudioModFollower) {
                ecs_strbuf_appendstr(buf, "follower of ");
                if (m[i].key && ecs_is_alive(world, m[i].key)) {
                    char *key = ecs_get_path(world, m[i].key);
                    ecs_strbuf_appendstr(buf, key);
                    ecs_os_free(key);
                } else {
                    ecs_strbuf_appendstr(buf, "its bus");
                }

                ecs_strbuf_append(buf, " attack %.3f release %.3f", (double)m[i].attack,
                    (double)m[i].release);
            } else {
                ecs_strbuf_append(buf, "lfo %s%s",
                    (uint32_t)m[i].wave <= (uint32_t)AudioModRandom ? waves[m[i].wave] : "?",
                    m[i].unipolar ? " unipolar" : "");
            }

            if (m[i].source != AudioModFollower) {
                if (m[i].sync > 0) {
                    ecs_strbuf_append(buf, " sync %.3f beats", (double)m[i].sync);
                } else {
                    ecs_strbuf_append(buf, " rate %.2f Hz", (double)m[i].rate);
                }

                if (m[i].phase != 0) {
                    ecs_strbuf_append(buf, " phase %.2f", (double)m[i].phase);
                }
            }

            ecs_strbuf_append(buf, " depth %.2f%s on %s (%s)", (double)m[i].depth,
                m[i].scale == AudioModOctaves ? " oct" : "",
                m[i].member ? m[i].member : "(no member)",
                box_audio_modulator_status(world, it.entities[i]));
            ecs_os_free(path);
        }
    }
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
        const FrequencyShifter *shifter;
        const PitchShifter *pitch;
        const PatternGate *pattern;
        const Rotary *rotary;
        const BeatRepeat *repeat;
        const TapeStop *stop;
        const Vocoder *vocoder;
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
            if (reverb->shimmer > 0) {
                ecs_strbuf_append(buf, " shimmer %.2f pitch %+.1f", (double)reverb->shimmer,
                    (double)reverb->shimmer_pitch);
            }
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
        } else if ((shifter = ecs_get(world, e, FrequencyShifter))) {
            ecs_strbuf_append(buf, "frequency shifter %s %+.1f Hz stereo %.2f feedback %.2f "
                "mix %.2f", shifter->mode == FrequencyRing ? "ring" : "shift",
                (double)shifter->shift, (double)shifter->stereo, (double)shifter->feedback,
                (double)shifter->mix);
        } else if ((pitch = ecs_get(world, e, PitchShifter))) {
            ecs_strbuf_append(buf, "pitch shifter semitones %+.1f cents %+.1f stereo %.2f "
                "window %.1f ms feedback %.2f mix %.2f", (double)pitch->semitones,
                (double)pitch->cents, (double)pitch->stereo, (double)pitch->window,
                (double)pitch->feedback, (double)pitch->mix);
        } else if ((pattern = ecs_get(world, e, PatternGate))) {
            int32_t length = pattern->length < 1 ? 1 : (pattern->length > 16 ? 16 :
                pattern->length);
            ecs_strbuf_appendstr(buf, "pattern gate steps [");
            for (b = 0; b < length; b ++) {
                ecs_strbuf_append(buf, b ? " %g" : "%g", (double)pattern->steps[b]);
            }

            ecs_strbuf_append(buf, "] rate %.3f beats swing %.2f gate %.2f attack %.4f "
                "release %.3f floor %.1f dB mix %.2f", (double)pattern->rate,
                (double)pattern->swing, (double)pattern->gate, (double)pattern->attack,
                (double)pattern->release, (double)pattern->floor, (double)pattern->mix);
        } else if ((rotary = ecs_get(world, e, Rotary))) {
            ecs_strbuf_append(buf, "rotary speed %.2f horn %.2f-%.2f Hz drum %.2f-%.2f Hz "
                "acceleration %.2f s balance %.2f spread %.2f drive %.2f mix %.2f",
                (double)rotary->speed, (double)rotary->horn_slow, (double)rotary->horn_fast,
                (double)rotary->drum_slow, (double)rotary->drum_fast,
                (double)rotary->acceleration, (double)rotary->balance,
                (double)rotary->spread, (double)rotary->drive, (double)rotary->mix);
        } else if ((repeat = ecs_get(world, e, BeatRepeat))) {
            ecs_strbuf_append(buf, "beat repeat %s slice %.4g beats", repeat->active >= 0.5f ?
                "active" : "inactive", (double)repeat->slice);
            if (repeat->grid < 0) {
                ecs_strbuf_appendstr(buf, " grid slice");
            } else {
                ecs_strbuf_append(buf, " grid %.4g", (double)repeat->grid);
            }

            ecs_strbuf_append(buf, " decay %.2f pitch %+.1f gate %.2f mix %.2f%s",
                (double)repeat->decay, (double)repeat->pitch, (double)repeat->gate,
                (double)repeat->mix, repeat->reverse >= 0.5f ? " reverse" : "");
        } else if ((stop = ecs_get(world, e, TapeStop))) {
            ecs_strbuf_append(buf, "tape stop %s stop %.3f s start %.3f s curve %+.2f",
                stop->active >= 0.5f ? "active" : "inactive", (double)stop->stop_time,
                (double)stop->start_time, (double)stop->curve);
        } else if ((vocoder = ecs_get(world, e, Vocoder))) {
            char *key = vocoder->modulator && ecs_is_alive(world, vocoder->modulator) ?
                ecs_get_path(world, vocoder->modulator) : NULL;
            ecs_strbuf_append(buf, "vocoder modulator %s bands %d low %.0f Hz high %.0f Hz "
                "q %.2f attack %.4f release %.3f shift %.2f unvoiced %.2f mix %.2f",
                key ? key : (vocoder->modulator ? "(deleted)" : "(none)"), vocoder->bands,
                (double)vocoder->low, (double)vocoder->high, (double)vocoder->q,
                (double)vocoder->attack, (double)vocoder->release, (double)vocoder->shift,
                (double)vocoder->unvoiced, (double)vocoder->mix);
            ecs_os_free(key);
        }

        slot = box_audio_module_effect_slot(world, e);
        if ((ecs_has(world, e, Compressor) || ecs_has(world, e, Gate) ||
            ecs_has(world, e, Saturator) || ecs_has(world, e, PatternGate)) && state &&
            slot >= 0)
        {
            ecs_strbuf_append(buf, " reduction %.1f dB", (double)state->reduction[slot]);
        }

        ecs_strbuf_append(buf, " (%s)", box_cmd_audio_effect_state(world, channel, j, e));
        box_cmd_audio_modulators(world, buf, e);
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

    box_cmd_audio_modulators(world, buf, e);
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

/** Get the name of the state of a song as the audio command prints it.
 *
 * @param state The state
 * @return The name
 */
static const char* box_cmd_audio_song_state(
    SongState state)
{
    const char *result = "none";
    if (state == SongLoading) {
        result = "loading";
    } else if (state == SongReady) {
        result = "ready";
    } else if (state == SongFailed) {
        result = "failed";
    }

    return result;
}

/** Append the line of the song: its file, state, title, length and lanes, or the error.
 *
 * @param world The world
 * @param buf The output
 */
static void box_cmd_audio_song(
    ecs_world_t *world,
    ecs_strbuf_t *buf)
{
    const AudioSongStatus *status = ecs_singleton_get(world, AudioSongStatus);
    const Song *song = ecs_singleton_get(world, Song);
    AudioSongStatus s = status ? *status : (AudioSongStatus){ .state = SongNone };
    ecs_strbuf_append(buf, "song: %s", box_cmd_audio_song_state(s.state));
    if (s.file) {
        ecs_strbuf_append(buf, " '%s'", s.file);
    }

    if (s.state == SongReady && song) {
        ecs_strbuf_append(buf, ", \"%s\", %.0f beats, %d lanes", song->title ? song->title : "",
            song->beats, ecs_vec_count(&song->lanes));
    }

    if (s.foreign) {
        ecs_strbuf_append(buf, ", changes %d entities it did not create", s.foreign);
    }

    if (s.state == SongFailed && s.error) {
        ecs_strbuf_append(buf, ": %s", s.error);
    }
}

/** Show the song, load another one or unload it.
 *
 * @param world The world
 * @param value The file, none to unload, NULL or empty to show the song
 * @return The text, which the caller frees
 */
static char* box_cmd_audio_song_set(
    ecs_world_t *world,
    const char *value)
{
    ecs_strbuf_t buf = ECS_STRBUF_INIT;
    char *result = NULL;
    if (!value || !value[0]) {
        box_cmd_audio_song(world, &buf);
        result = ecs_strbuf_get(&buf);
    } else if (!ecs_os_strcmp(value, "none")) {
        box_audio_song_unload(world);
        result = ecs_os_strdup("unloading the song");
    } else {
        box_audio_song_load(world, value);
        result = flecs_asprintf("loading '%s'", value);
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

/** Get the name of an emitter shape as the audio command prints it.
 *
 * @param shape The shape
 * @return The name
 */
static const char* box_cmd_audio_shape(
    AudioEmitterShape shape)
{
    const char *result = "point";
    switch (shape) {
    case AudioEmitterPoint: result = "point"; break;
    case AudioEmitterDisc: result = "disc"; break;
    case AudioEmitterSphere: result = "sphere"; break;
    case AudioEmitterHemisphere: result = "hemisphere"; break;
    case AudioEmitterBox: result = "box"; break;
    }

    return result;
}

/** Append the lines of the emitters: the sound, the shape, the intervals, the plays sent and
 * skipped and the plays that sound.
 *
 * @param world The world
 * @param buf The output
 */
static void box_cmd_audio_emitters(
    ecs_world_t *world,
    ecs_strbuf_t *buf)
{
    int32_t i;
    ecs_query_t *q = ecs_query(world, {
        .terms = {
            { .id = ecs_id(AudioEmitter), .inout = EcsIn, .src.id = EcsSelf },
            { .id = ecs_id(AudioEmitterState), .inout = EcsIn, .src.id = EcsSelf,
                .oper = EcsOptional }
        },
        .flags = EcsQueryMatchDisabled
    });
    ecs_iter_t it = ecs_query_iter(world, q);
    while (ecs_query_next(&it)) {
        const AudioEmitter *em = ecs_field(&it, AudioEmitter, 0);
        const AudioEmitterState *es = ecs_field(&it, AudioEmitterState, 1);
        bool disabled = ecs_table_has_id(world, it.table, EcsDisabled);
        for (i = 0; i < it.count; i ++) {
            ecs_entity_t sound = em[i].sound;
            bool alive = sound && ecs_is_alive(world, sound);
            char *path = ecs_get_path(world, it.entities[i]);
            char *name = alive ? ecs_get_path(world, sound) : NULL;
            ecs_strbuf_append(buf, "\n  emitter %s: sound %s, %s, every %.3f to %.3f s, "
                "%lld plays, %lld skipped, %d sounding", path, name ? name : "none",
                box_cmd_audio_shape(em[i].shape), (double)em[i].interval_min,
                (double)em[i].interval_max, es ? (long long)es[i].plays : 0LL,
                es ? (long long)es[i].skipped : 0LL, es ? es[i].active : 0);
            if (em[i].max_active > 0) {
                ecs_strbuf_append(buf, " of %d", em[i].max_active);
            }

            if (!em[i].playing) {
                ecs_strbuf_appendstr(buf, ", stopped");
            }

            if (disabled) {
                ecs_strbuf_appendstr(buf, ", disabled");
            }

            ecs_os_free(name);
            ecs_os_free(path);
        }
    }

    ecs_query_fini(q);
}

/** Append the macros: each with its value and whether it runs, and its maps with their member,
 * range, curve, the value the member has at the value of the component and whether they run.
 *
 * @param world The world
 * @param buf The output
 */
static void box_cmd_audio_macros(
    ecs_world_t *world,
    ecs_strbuf_t *buf)
{
    ecs_iter_t it = ecs_each(world, AudioMacro);
    int32_t i, k;
    while (ecs_each_next(&it)) {
        const AudioMacro *macro = ecs_field(&it, AudioMacro, 0);
        for (i = 0; i < it.count; i ++) {
            char *path = ecs_get_path(world, it.entities[i]);
            ecs_iter_t cit = ecs_children(world, it.entities[i]);
            ecs_strbuf_append(buf, "\n  macro %s: value %.2f (%s)", path,
                (double)macro[i].value, box_audio_macro_status(world, it.entities[i]));
            while (ecs_children_next(&cit)) {
                for (k = 0; k < cit.count; k ++) {
                    const AudioMacroMap *m = ecs_get(world, cit.entities[k], AudioMacroMap);
                    char *map_path, *target;
                    const char *component;
                    float value;
                    if (!m) {
                        continue;
                    }

                    map_path = ecs_get_path(world, cit.entities[k]);
                    target = m->target && ecs_is_alive(world, m->target) ?
                        ecs_get_path(world, m->target) : NULL;
                    component = m->component && ecs_is_alive(world, m->component) ?
                        ecs_get_name(world, m->component) : NULL;
                    value = box_audio_macro_map_value(macro[i].value, m->min, m->max,
                        m->curve, m->scale == AudioModOctaves ? BOX_AUDIO_MOD_OCTAVES :
                        BOX_AUDIO_MOD_LINEAR);
                    ecs_strbuf_append(buf, "\n    map %s: %s %s.%s %.2f..%.2f%s", map_path,
                        target ? target : "(no target)", component ? component : "?",
                        m->member ? m->member : "(no member)", (double)m->min,
                        (double)m->max, m->scale == AudioModOctaves ? " oct" : "");
                    if (m->curve != 0) {
                        ecs_strbuf_append(buf, " curve %.2f", (double)m->curve);
                    }

                    ecs_strbuf_append(buf, " = %.2f (%s)", (double)value,
                        box_audio_macro_status(world, cit.entities[k]));
                    ecs_os_free(target);
                    ecs_os_free(map_path);
                }
            }

            ecs_os_free(path);
        }
    }
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
    ecs_strbuf_appendch(&buf, '\n');
    box_cmd_audio_song(world, &buf);
    ecs_entity_t master = ecs_lookup(world, "box.audio.master");
    const AudioBus *mb = master ? ecs_get(world, master, AudioBus) : NULL;
    ecs_strbuf_append(&buf, "\nmaster: volume %.2f%s",
        settings ? (double)settings->master_volume : 1.0, settings && settings->mute ?
            " muted" : "");
    if (mb) {
        ecs_strbuf_append(&buf, ", bus volume %.2f pan %.2f width %.2f%s", (double)mb->volume,
            (double)mb->pan, (double)mb->width, mb->mute ? " muted" : "");
        box_cmd_audio_modulators(world, &buf, master);
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
    box_cmd_audio_emitters(world, &buf);
    box_cmd_audio_macros(world, &buf);

    return ecs_strbuf_get(&buf);
}

static char* box_cmd_audio_volume(
    ecs_world_t *world,
    double volume)
{
    AudioSettings *settings;
    char *result = NULL;
    if (!(volume >= 0) || volume > BOX_AUDIO_VOLUME_MAX) {
        ecs_err("invalid volume %g, expected 0 to %.0f", volume,
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

static char* box_cmd_audio_mute(
    ecs_world_t *world,
    box_cmd_switch_t state)
{
    AudioSettings *settings = ecs_singleton_ensure(world, AudioSettings);
    bool mute = state == BoxCmdSwitchToggle ? !settings->mute : state == BoxCmdSwitchOn;
    settings->mute = mute;
    ecs_singleton_modified(world, AudioSettings);

    return ecs_os_strdup(mute ? "mute on" : "mute off");
}

static char* box_cmd_audio_capture(
    ecs_world_t *world,
    double seconds,
    ecs_shell_resource_t *resource)
{
    char *path = NULL, *result = NULL;
    if (!(seconds > 0) || seconds > BOX_AUDIO_CAPTURE_MAX) {
        ecs_err("box.audio: a capture takes more than 0 and at most %.0f seconds",
            (double)BOX_AUDIO_CAPTURE_MAX);
        goto done;
    }

    if (box_audio_capture_pending(world)) {
        ecs_err("box.audio: a capture is already recording");
        goto done;
    }

#ifdef __EMSCRIPTEN__
    static int32_t box_cmd_audio_captures;
    (void)resource;
    path = flecs_asprintf("%s/audio_capture_%d.wav", BOX_CMD_AUDIO_WEB_DIR,
        ++ box_cmd_audio_captures);
    if (!box_audio_capture(world, (float)seconds, path)) {
        result = path;
        path = NULL;
    }
#else
    path = box_cmd_render_resource(world, "audio/wav", resource);
    if (path && !box_audio_capture(world, (float)seconds, path)) {
        result = ecs_os_strdup(resource->name);
    }
#endif
done:
    ecs_os_free(path);

    return result;
}

static bool box_cmd_audio_ready(
    ecs_world_t *world)
{
    bool result = ecs_id(AudioSettings) && box_audio_module_get(world);
    if (!result) {
        ecs_err("box.audio is not imported");
    }

    return result;
}

static bool box_cmd_audio_sequencer_ready(
    ecs_world_t *world)
{
    bool result = ecs_id(Track) != 0;
    if (!result) {
        ecs_err("box.audio is not imported");
    }

    return result;
}

static const char* box_cmd_audio_arg(
    int32_t argc,
    const ecs_value_t *argv,
    int32_t index)
{
    const char *result = argc > index ? *(const char**)argv[index].ptr : NULL;

    return result ? result : "";
}

static box_cmd_switch_t box_cmd_audio_arg_switch(
    int32_t argc,
    const ecs_value_t *argv,
    int32_t index)
{
    return argc > index ? (box_cmd_switch_t)*(const int32_t*)argv[index].ptr :
        BoxCmdSwitchToggle;
}

static double box_cmd_audio_arg_number(
    int32_t argc,
    const ecs_value_t *argv,
    int32_t index,
    double fallback)
{
    return argc > index ? *(const double*)argv[index].ptr : fallback;
}

static int32_t box_cmd_audio_arg_list(
    int32_t argc,
    const ecs_value_t *argv,
    int32_t first,
    const char **out)
{
    int32_t i, count = 0;
    for (i = first; i < argc && count < BOX_CMD_AUDIO_OPTIONS_MAX + 1; i ++) {
        out[count ++] = box_cmd_audio_arg(argc, argv, i);
    }

    return count;
}

static ecs_shell_resource_t* box_cmd_audio_resource(
    ecs_value_t *result)
{
    ecs_shell_resource_t *out = NULL;
#ifdef __EMSCRIPTEN__
    (void)result;
#else
    out = result->ptr;
#endif

    return out;
}

static void box_cmd_audio_resource_set(
    ecs_value_t *result,
    char *out)
{
#ifdef __EMSCRIPTEN__
    *(char**)result->ptr = out;
#else
    (void)result;
    ecs_os_free(out);
#endif
}

static void box_cmd_audio_status_cmd(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    (void)argv;
    if (box_cmd_audio_ready(ctx->world)) {
        *(char**)result->ptr = box_cmd_audio_print(ctx->world);
    }
}

static void box_cmd_audio_volume_cmd(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    if (box_cmd_audio_ready(ctx->world)) {
        *(char**)result->ptr = box_cmd_audio_volume(ctx->world, *(const float*)argv[0].ptr);
    }
}

static void box_cmd_audio_mute_cmd(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    if (box_cmd_audio_ready(ctx->world)) {
        *(char**)result->ptr = box_cmd_audio_mute(ctx->world,
            box_cmd_audio_arg_switch(argc, argv, 0));
    }
}

static void box_cmd_audio_mute_bus_cmd(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    if (box_cmd_audio_ready(ctx->world)) {
        *(char**)result->ptr = box_cmd_audio_bus_mute(ctx->world,
            box_cmd_audio_arg(argc, argv, 0), box_cmd_audio_arg_switch(argc, argv, 1));
    }
}

static void box_cmd_audio_solo_cmd(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    if (box_cmd_audio_ready(ctx->world)) {
        *(char**)result->ptr = box_cmd_audio_solo(ctx->world,
            box_cmd_audio_arg(argc, argv, 0), box_cmd_audio_arg_switch(argc, argv, 1));
    }
}

static void box_cmd_audio_bypass_cmd(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    if (box_cmd_audio_ready(ctx->world)) {
        *(char**)result->ptr = box_cmd_audio_bypass(ctx->world,
            box_cmd_audio_arg(argc, argv, 0), box_cmd_audio_arg_switch(argc, argv, 1));
    }
}

static void box_cmd_audio_note_cmd(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    if (box_cmd_audio_ready(ctx->world)) {
        *(char**)result->ptr = box_cmd_audio_note(ctx->world,
            box_cmd_audio_arg(argc, argv, 0), box_cmd_audio_arg(argc, argv, 1),
            argc > 2 ? (double)*(const float*)argv[2].ptr : 1.0,
            argc > 3 ? (double)*(const float*)argv[3].ptr : BOX_CMD_AUDIO_NOTE_DUR);
    }
}

static void box_cmd_audio_play_cmd(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    (void)argv;
    if (box_cmd_audio_ready(ctx->world)) {
        Transport *tr = ecs_singleton_ensure(ctx->world, Transport);
        tr->playing = true;
        *(char**)result->ptr = flecs_asprintf("playing from %.3f", tr->position);
        ecs_singleton_modified(ctx->world, Transport);
    }
}

static void box_cmd_audio_stop_cmd(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    (void)argv;
    if (box_cmd_audio_ready(ctx->world)) {
        ecs_singleton_ensure(ctx->world, Transport)->playing = false;
        ecs_singleton_modified(ctx->world, Transport);
        *(char**)result->ptr = ecs_os_strdup("stopped");
    }
}

static void box_cmd_audio_seek_cmd(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    double beats = box_cmd_audio_arg_number(argc, argv, 0, 0);
    if (!box_cmd_audio_ready(ctx->world)) {
        goto done;
    }

    if (!isfinite(beats)) {
        ecs_err("invalid position %g, expected beats", beats);
        goto done;
    }

    ecs_singleton_ensure(ctx->world, Transport)->position = beats;
    ecs_singleton_modified(ctx->world, Transport);
    *(char**)result->ptr = flecs_asprintf("position %.3f", beats);
done:
    return;
}

static void box_cmd_audio_tempo_cmd(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    double bpm = box_cmd_audio_arg_number(argc, argv, 0, 0);
    if (!box_cmd_audio_ready(ctx->world)) {
        goto done;
    }

    if (!(bpm >= 20) || bpm > 999) {
        ecs_err("invalid tempo %g, expected 20 to 999", bpm);
        goto done;
    }

    ecs_singleton_ensure(ctx->world, Transport)->tempo = (float)bpm;
    ecs_singleton_modified(ctx->world, Transport);
    *(char**)result->ptr = flecs_asprintf("tempo %.1f", bpm);
done:
    return;
}

static void box_cmd_audio_song_cmd(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    if (box_cmd_audio_ready(ctx->world)) {
        *(char**)result->ptr = box_cmd_audio_song_set(ctx->world,
            box_cmd_audio_arg(argc, argv, 0));
    }
}

static void box_cmd_audio_capture_cmd(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    (void)argc;
    if (box_cmd_audio_ready(ctx->world)) {
        box_cmd_audio_resource_set(result, box_cmd_audio_capture(ctx->world,
            (double)*(const float*)argv[0].ptr, box_cmd_audio_resource(result)));
    }
}

static void box_cmd_audio_render_cmd(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    const char *options[BOX_CMD_AUDIO_OPTIONS_MAX + 1];
    int32_t count = box_cmd_audio_arg_list(argc, argv, 1, options);
    if (box_cmd_audio_ready(ctx->world)) {
        box_cmd_audio_resource_set(result, box_cmd_audio_render(ctx->world,
            box_cmd_audio_arg(argc, argv, 0), count, options,
            box_cmd_audio_resource(result)));
    }
}

static void box_cmd_audio_analyze_cmd(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    if (box_cmd_audio_ready(ctx->world)) {
        *(char**)result->ptr = box_cmd_audio_analyze(ctx->world,
            box_cmd_audio_arg(argc, argv, 0), box_cmd_audio_arg_number(argc, argv, 1, 0),
            box_cmd_audio_arg_number(argc, argv, 2, -1));
    }
}

static void box_cmd_audio_diff_cmd(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    if (box_cmd_audio_ready(ctx->world)) {
        *(char**)result->ptr = box_cmd_audio_diff(ctx->world,
            box_cmd_audio_arg(argc, argv, 0), box_cmd_audio_arg(argc, argv, 1),
            box_cmd_audio_arg_number(argc, argv, 2, 0),
            box_cmd_audio_arg_number(argc, argv, 3, -1));
    }
}

static void box_cmd_audio_plot_cmd(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    const char *args[BOX_CMD_AUDIO_OPTIONS_MAX + 1];
    int32_t count = box_cmd_audio_arg_list(argc, argv, 0, args);
    if (box_cmd_audio_ready(ctx->world)) {
        box_cmd_audio_resource_set(result, box_cmd_audio_plot(ctx->world, count, args,
            box_cmd_audio_resource(result)));
    }
}

static void box_cmd_audio_meter_cmd(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    const char *args[BOX_CMD_AUDIO_OPTIONS_MAX + 1];
    int32_t count = box_cmd_audio_arg_list(argc, argv, 0, args);
    if (box_cmd_audio_ready(ctx->world)) {
        *(char**)result->ptr = box_cmd_audio_meter_span(ctx->world, count, args);
    }
}

static void box_cmd_audio_check_cmd(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    const char *args[BOX_CMD_AUDIO_OPTIONS_MAX + 1];
    int32_t count = box_cmd_audio_arg_list(argc, argv, 0, args);
    if (box_cmd_audio_ready(ctx->world)) {
        *(char**)result->ptr = box_cmd_audio_check(ctx->world, count, args);
    }
}

static void box_cmd_audio_notes_cmd(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    const char *args[BOX_CMD_AUDIO_OPTIONS_MAX + 1];
    int32_t count = box_cmd_audio_arg_list(argc, argv, 0, args);
    if (box_cmd_audio_sequencer_ready(ctx->world)) {
        *(char**)result->ptr = box_cmd_audio_notes(ctx->world, count, args);
    }
}

static void box_cmd_audio_chords_cmd(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_value_t *result)
{
    const char *args[BOX_CMD_AUDIO_OPTIONS_MAX + 1];
    int32_t count = box_cmd_audio_arg_list(argc, argv, 0, args);
    if (box_cmd_audio_sequencer_ready(ctx->world)) {
        *(char**)result->ptr = box_cmd_audio_chords(ctx->world, count, args);
    }
}

static void box_cmd_audio_options(
    ecs_function_desc_t *desc,
    int32_t first,
    const char *name,
    const char *brief)
{
    int32_t i;
    for (i = first; i < first + BOX_CMD_AUDIO_OPTIONS_MAX; i ++) {
        desc->params[i] = (ecs_script_parameter_t){
            .name = name, .type = ecs_id(ecs_string_t), .optional = true, .brief = brief };
    }
}

static void box_cmd_audio_sub(
    ecs_world_t *world,
    ecs_entity_t audio,
    const char *brief,
    ecs_function_desc_t desc)
{
    desc.parent = audio;
    box_command_register(world, brief, desc);
}

static void box_cmd_audio_init_mixer(
    ecs_world_t *world,
    ecs_entity_t audio,
    ecs_entity_t sw)
{
    box_cmd_audio_sub(world, audio, "Set the master volume",
        (ecs_function_desc_t){
            .name = "volume",
            .return_type = ecs_id(ecs_string_t),
            .params = {
                { .name = "volume", .type = ecs_id(ecs_f32_t), .brief = "0 to 4" }
            },
            .callback = box_cmd_audio_volume_cmd
        });

    box_cmd_audio_sub(world, audio, "Set or toggle the mute of everything",
        (ecs_function_desc_t){
            .name = "mute",
            .return_type = ecs_id(ecs_string_t),
            .params = {
                { .name = "state", .type = sw, .optional = true,
                  .brief = "on or off (default: toggle)" }
            },
            .callback = box_cmd_audio_mute_cmd
        });

    box_cmd_audio_sub(world, audio, "Set or toggle the mute of a bus",
        (ecs_function_desc_t){
            .name = "mute_bus",
            .return_type = ecs_id(ecs_string_t),
            .params = {
                { .name = "bus", .type = ecs_id(ecs_string_t),
                  .brief = "Path of the bus, master for the master bus" },
                { .name = "state", .type = sw, .optional = true,
                  .brief = "on or off (default: toggle)" }
            },
            .callback = box_cmd_audio_mute_bus_cmd
        });

    box_cmd_audio_sub(world, audio, "Set or toggle the solo of a bus",
        (ecs_function_desc_t){
            .name = "solo",
            .return_type = ecs_id(ecs_string_t),
            .params = {
                { .name = "bus", .type = ecs_id(ecs_string_t),
                  .brief = "Path of the bus, none clears every solo" },
                { .name = "state", .type = sw, .optional = true,
                  .brief = "on or off (default: toggle)" }
            },
            .callback = box_cmd_audio_solo_cmd
        });

    box_cmd_audio_sub(world, audio, "Bypass an effect, or stop bypassing it",
        (ecs_function_desc_t){
            .name = "bypass",
            .return_type = ecs_id(ecs_string_t),
            .params = {
                { .name = "effect", .type = ecs_id(ecs_string_t),
                  .brief = "Path of the effect entity" },
                { .name = "state", .type = sw, .optional = true,
                  .brief = "on or off (default: toggle)" }
            },
            .callback = box_cmd_audio_bypass_cmd
        });

    box_cmd_audio_sub(world, audio, "Play one note of a sound or an instrument now",
        (ecs_function_desc_t){
            .name = "note",
            .return_type = ecs_id(ecs_string_t),
            .params = {
                { .name = "subject", .type = ecs_id(ecs_string_t),
                  .brief = "Path of the sound or the instrument" },
                { .name = "note", .type = ecs_id(ecs_string_t),
                  .brief = "Note name such as C4 or F#3, or a MIDI number 0 to 127" },
                { .name = "velocity", .type = ecs_id(ecs_f32_t), .optional = true,
                  .brief = "0 to 1 (default: 1)" },
                { .name = "duration", .type = ecs_id(ecs_f32_t), .optional = true,
                  .brief = "Seconds (default: 0.5)" }
            },
            .callback = box_cmd_audio_note_cmd
        });
}

static void box_cmd_audio_init_transport(
    ecs_world_t *world,
    ecs_entity_t audio)
{
    box_cmd_audio_sub(world, audio, "Play the transport",
        (ecs_function_desc_t){
            .name = "play",
            .return_type = ecs_id(ecs_string_t),
            .callback = box_cmd_audio_play_cmd
        });

    box_cmd_audio_sub(world, audio, "Stop the transport",
        (ecs_function_desc_t){
            .name = "stop",
            .return_type = ecs_id(ecs_string_t),
            .callback = box_cmd_audio_stop_cmd
        });

    box_cmd_audio_sub(world, audio, "Seek the transport",
        (ecs_function_desc_t){
            .name = "seek",
            .return_type = ecs_id(ecs_string_t),
            .params = {
                { .name = "beats", .type = ecs_id(ecs_f64_t), .brief = "Position in beats" }
            },
            .callback = box_cmd_audio_seek_cmd
        });

    box_cmd_audio_sub(world, audio, "Set the tempo of the transport",
        (ecs_function_desc_t){
            .name = "tempo",
            .return_type = ecs_id(ecs_string_t),
            .params = {
                { .name = "bpm", .type = ecs_id(ecs_f64_t),
                  .brief = "Quarter notes per minute, 20 to 999" }
            },
            .callback = box_cmd_audio_tempo_cmd
        });

    box_cmd_audio_sub(world, audio, "Show the song, load a song file or unload it",
        (ecs_function_desc_t){
            .name = "song",
            .return_type = ecs_id(ecs_string_t),
            .params = {
                { .name = "file", .type = ecs_id(ecs_string_t), .optional = true,
                  .brief = "Song file to load, none to unload (default: show the song)" }
            },
            .callback = box_cmd_audio_song_cmd
        });
}

static void box_cmd_audio_init_wav(
    ecs_world_t *world,
    ecs_entity_t audio)
{
    box_cmd_audio_sub(world, audio, "Record the master output to a 16 bit stereo WAV resource",
        (ecs_function_desc_t){
            .name = "capture",
            .return_type = BOX_CMD_AUDIO_RESOURCE,
            .params = {
                { .name = "seconds", .type = ecs_id(ecs_f32_t),
                  .brief = "Seconds to record, at most 60" }
            },
            .callback = box_cmd_audio_capture_cmd
        });

    ecs_function_desc_t render = {
        .name = "render",
        .return_type = BOX_CMD_AUDIO_RESOURCE,
        .params = {
            { .name = "subject", .type = ecs_id(ecs_string_t),
              .brief = "song, or the path of a sound, an instrument or a bus" }
        },
        .callback = box_cmd_audio_render_cmd
    };
    box_cmd_audio_options(&render, 1, "options",
        "key=value: note, vel, dur, tail, bars, beats, solo or mute");
    box_cmd_audio_sub(world, audio,
        "Render a sound, instrument, bus stem or song section on its own, faster than real "
        "time, to a WAV resource", render);

    box_cmd_audio_sub(world, audio,
        "Print loudness, peaks, bands, stereo, onsets, tail, clicks and pitch of a WAV",
        (ecs_function_desc_t){
            .name = "analyze",
            .return_type = ecs_id(ecs_string_t),
            .params = {
                { .name = "wav", .type = ecs_id(ecs_string_t),
                  .brief = "WAV resource of the session or file" },
                { .name = "from", .type = ecs_id(ecs_f64_t), .optional = true,
                  .brief = "First second of the span (default: the start)" },
                { .name = "to", .type = ecs_id(ecs_f64_t), .optional = true,
                  .brief = "Last second of the span (default: the end)" }
            },
            .callback = box_cmd_audio_analyze_cmd
        });

    box_cmd_audio_sub(world, audio,
        "Print how WAV b differs from WAV a: bit identity, loudness, bands, stereo and a null "
        "test",
        (ecs_function_desc_t){
            .name = "diff",
            .return_type = ecs_id(ecs_string_t),
            .params = {
                { .name = "a", .type = ecs_id(ecs_string_t),
                  .brief = "First WAV resource or file" },
                { .name = "b", .type = ecs_id(ecs_string_t),
                  .brief = "Second WAV resource or file" },
                { .name = "from", .type = ecs_id(ecs_f64_t), .optional = true,
                  .brief = "First second of the span (default: the start)" },
                { .name = "to", .type = ecs_id(ecs_f64_t), .optional = true,
                  .brief = "Last second of the span (default: the end)" }
            },
            .callback = box_cmd_audio_diff_cmd
        });

    ecs_function_desc_t plot = {
        .name = "plot",
        .return_type = BOX_CMD_AUDIO_RESOURCE,
        .params = {
            { .name = "wav", .type = ecs_id(ecs_string_t),
              .brief = "WAV resource of the session or file" }
        },
        .callback = box_cmd_audio_plot_cmd
    };
    box_cmd_audio_options(&plot, 1, "options",
        "spectrogram (default), waveform, spectrum or loudness, size=WxH, from=s, to=s, "
        "min_db=dB");
    box_cmd_audio_sub(world, audio, "Draw a PNG resource of a WAV", plot);
}

static void box_cmd_audio_init_sequencer(
    ecs_world_t *world,
    ecs_entity_t audio)
{
    ecs_function_desc_t meter = {
        .name = "meter",
        .return_type = ecs_id(ecs_string_t),
        .callback = box_cmd_audio_meter_cmd
    };
    box_cmd_audio_options(&meter, 0, "options",
        "Buses (default: every bus and the master), seconds or bars=a..b; without options "
        "print the progress or the table");
    box_cmd_audio_sub(world, audio,
        "Measure peak, RMS and loudness per bus and the gain reduction of its effects over a "
        "span of the live output", meter);

    ecs_function_desc_t notes = {
        .name = "notes",
        .return_type = ecs_id(ecs_string_t),
        .params = {
            { .name = "track", .type = ecs_id(ecs_string_t), .brief = "Path of the track" }
        },
        .callback = box_cmd_audio_notes_cmd
    };
    box_cmd_audio_options(&notes, 1, "options", "bars=a..b or roll");
    box_cmd_audio_sub(world, audio, "Print the notes of a track per bar, or as a piano roll",
        notes);

    ecs_function_desc_t chords = {
        .name = "chords",
        .return_type = ecs_id(ecs_string_t),
        .callback = box_cmd_audio_chords_cmd
    };
    box_cmd_audio_options(&chords, 0, "options",
        "Tracks (default: every track) and bars=a..b");
    box_cmd_audio_sub(world, audio, "Print the chords and the key of a section from the notes",
        chords);

    ecs_function_desc_t check = {
        .name = "check",
        .return_type = ecs_id(ecs_string_t),
        .callback = box_cmd_audio_check_cmd
    };
    box_cmd_audio_options(&check, 0, "options", "static and bars=a..b");
    box_cmd_audio_sub(world, audio,
        "Lint the song, or the tracks of the scene: notes, clips, routing, effects and lanes, "
        "then voices, steals, clipping, the limiter, loudness and silent tracks from one "
        "offline play", check);
}

void box_cmd_audio_init(
    ecs_world_t *world)
{
    ECS_IMPORT(world, BoxAudio);
    box_cmd_audio_render_init(world);

    ecs_entity_t sw = ecs_enum_init(world, &(ecs_enum_desc_t){
        .entity = ecs_entity(world, {
            .name = "Switch", .parent = ecs_lookup(world, "box.command") }),
        .constants = {
            { .name = "off", .value = BoxCmdSwitchOff },
            { .name = "on", .value = BoxCmdSwitchOn }
        }
    });

    ecs_entity_t audio = box_command_register(world,
        "Print the audio state, transport, song, buses, instruments, tracks, emitters and "
        "macros; the audio.* commands change and inspect it",
        (ecs_function_desc_t){
            .name = "audio",
            .return_type = ecs_id(ecs_string_t),
            .callback = box_cmd_audio_status_cmd
        });

    box_cmd_audio_init_mixer(world, audio, sw);
    box_cmd_audio_init_transport(world, audio);
    box_cmd_audio_init_wav(world, audio);
    box_cmd_audio_init_sequencer(world, audio);
}

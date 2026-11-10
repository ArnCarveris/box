/**
 * @file command/cmd_audio_effects.c
 * @brief The parameters of the effects in the insert slots of a bus, as the audio command
 * prints them.
 */

#include "command.h"
#include "../audio/module.h"

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

/** Append the parameters of a reverb, equalizer, filter, compressor, gate or delay.
 *
 * @param world The world
 * @param buf The output
 * @param e The effect entity
 * @return Whether the entity has one of these effects
 */
static bool box_cmd_audio_effect_core(
    ecs_world_t *world,
    ecs_strbuf_t *buf,
    ecs_entity_t e)
{
    const Reverb *reverb;
    const Equalizer *eq;
    const AudioFilter *filter;
    const Compressor *comp;
    const Gate *gate;
    const Delay *delay;
    bool result = true;
    int32_t b;
    if ((reverb = ecs_get(world, e, Reverb))) {
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
    } else {
        result = false;
    }

    return result;
}

/** Append the parameters of a phaser, chorus, saturator, transient shaper, frequency shifter
 * or pitch shifter.
 *
 * @param world The world
 * @param buf The output
 * @param e The effect entity
 * @return Whether the entity has one of these effects
 */
static bool box_cmd_audio_effect_color(
    ecs_world_t *world,
    ecs_strbuf_t *buf,
    ecs_entity_t e)
{
    const Phaser *phaser;
    const Chorus *chorus;
    const Saturator *saturator;
    const TransientShaper *shaper;
    const FrequencyShifter *shifter;
    const PitchShifter *pitch;
    bool result = true;
    if ((phaser = ecs_get(world, e, Phaser))) {
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
    } else {
        result = false;
    }

    return result;
}

/** Append the parameters of a pattern gate, rotary, beat repeat, tape stop or vocoder.
 *
 * @param world The world
 * @param buf The output
 * @param e The effect entity
 * @return Whether the entity has one of these effects
 */
static bool box_cmd_audio_effect_rhythm(
    ecs_world_t *world,
    ecs_strbuf_t *buf,
    ecs_entity_t e)
{
    const PatternGate *pattern;
    const Rotary *rotary;
    const BeatRepeat *repeat;
    const TapeStop *stop;
    const Vocoder *vocoder;
    bool result = true;
    int32_t b;
    if ((pattern = ecs_get(world, e, PatternGate))) {
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
    } else {
        result = false;
    }

    return result;
}

bool box_cmd_audio_effect_params(
    ecs_world_t *world,
    ecs_strbuf_t *buf,
    ecs_entity_t e)
{
    bool result = box_cmd_audio_effect_core(world, buf, e) ||
        box_cmd_audio_effect_color(world, buf, e) ||
        box_cmd_audio_effect_rhythm(world, buf, e);

    return result;
}

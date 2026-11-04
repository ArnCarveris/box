/**
 * @file audio/core/synth_patch.c
 * @brief The patches of synth sounds in the core: the SYNTH command, which clamps every value of
 * a patch into a slot of the core, and the parts and strings of the core that voices take for
 * their layers. The voices themselves render in synth.c.
 */

#include "internal.h"

/** Clamp a value of a patch; NaN becomes the low end.
 *
 * @param v The value
 * @param lo Low end
 * @param hi High end
 * @return The clamped value
 */
static float box_audio_synth_clamp(
    float v,
    float lo,
    float hi)
{
    if (!(v >= lo)) {
        v = lo;
    } else if (v > hi) {
        v = hi;
    }

    return v;
}

/** Clamp a curve of an envelope; NaN and curves too small to bend a segment are linear.
 *
 * @param v The curve
 * @return The curve, 0 for linear
 */
static float box_audio_synth_curve_clamp(
    float v)
{
    float a = v < 0 ? -v : v;
    if (!(a >= BOX_AUDIO_SYNTH_CURVE_MIN)) {
        v = 0;
    } else if (a > BOX_AUDIO_SYNTH_CURVE_MAX) {
        v = v < 0 ? -BOX_AUDIO_SYNTH_CURVE_MAX : BOX_AUDIO_SYNTH_CURVE_MAX;
    }

    return v;
}

/** Clamp the times, the level and the curves of an envelope.
 *
 * @param e The envelope
 */
static void box_audio_synth_envelope_clamp(
    box_audio_synth_envelope_t *e)
{
    e->attack = box_audio_synth_clamp(e->attack, 0, BOX_AUDIO_SYNTH_LENGTH_MAX);
    e->decay = box_audio_synth_clamp(e->decay, 0, BOX_AUDIO_SYNTH_LENGTH_MAX);
    e->sustain = box_audio_synth_clamp(e->sustain, 0, 1);
    e->release = box_audio_synth_clamp(e->release, 0, BOX_AUDIO_SYNTH_LENGTH_MAX);
    e->hold = box_audio_synth_clamp(e->hold, 0, BOX_AUDIO_SYNTH_LENGTH_MAX);
    e->curve = box_audio_synth_curve_clamp(e->curve);
    e->attack_curve = box_audio_synth_curve_clamp(e->attack_curve);
}

/** Clamp a depth of an LFO, either way; NaN is 0.
 *
 * @param v The depth
 * @param max Largest depth
 * @return The clamped depth
 */
static float box_audio_synth_depth_clamp(
    float v,
    float max)
{
    return v == v ? box_audio_synth_clamp(v, -max, max) : 0.0f;
}

/** Clamp a pulse width; NaN and widths that are not positive are the default.
 *
 * @param v The width
 * @return The width, BOX_AUDIO_SYNTH_PULSE_MIN..1 minus it
 */
static float box_audio_synth_width_clamp(
    float v)
{
    return v > 0 ? box_audio_synth_clamp(v, BOX_AUDIO_SYNTH_PULSE_MIN,
        1 - BOX_AUDIO_SYNTH_PULSE_MIN) : BOX_AUDIO_SYNTH_PULSE_DEFAULT;
}

/** Clamp a frequency a sweep ends at; NaN and values that are not positive mean no sweep.
 *
 * @param v The frequency
 * @param lo Lowest frequency
 * @return The frequency, 0 for no sweep
 */
static float box_audio_synth_end_clamp(
    float v,
    float lo)
{
    return v > 0 ? box_audio_synth_clamp(v, lo, BOX_AUDIO_SYNTH_HZ_MAX) : 0.0f;
}

/** Clamp every value of a patch, so a voice never sees NaN, a negative time or an unknown
 * kind.
 *
 * @param p The patch
 */
static void box_audio_synth_patch_clamp(
    box_audio_synth_patch_t *p)
{
    uint32_t i;
    if (p->layers > BOX_AUDIO_SYNTH_LAYERS) {
        p->layers = BOX_AUDIO_SYNTH_LAYERS;
    }

    p->pitch_variance = box_audio_synth_clamp(p->pitch_variance, 0, 1);
    p->level_variance = box_audio_synth_clamp(p->level_variance, 0, 1);
    p->glide = box_audio_synth_clamp(p->glide, 0, BOX_AUDIO_SYNTH_GLIDE_MAX);
    box_audio_synth_envelope_clamp(&p->envelope);
    if (p->filter > BOX_AUDIO_SYNTH_BANDPASS) {
        p->filter = BOX_AUDIO_SYNTH_FILTER_NONE;
    }

    p->cutoff = box_audio_synth_clamp(p->cutoff, BOX_AUDIO_SYNTH_CUTOFF_MIN,
        BOX_AUDIO_SYNTH_HZ_MAX);
    p->resonance = box_audio_synth_clamp(p->resonance, 0, 1);
    if (p->slope > BOX_AUDIO_SYNTH_SLOPE_24) {
        p->slope = BOX_AUDIO_SYNTH_SLOPE_12;
    }

    p->filter_amount = p->filter_amount == p->filter_amount ? box_audio_synth_clamp(
        p->filter_amount, -BOX_AUDIO_SYNTH_OCTAVES_MAX, BOX_AUDIO_SYNTH_OCTAVES_MAX) : 0.0f;
    box_audio_synth_envelope_clamp(&p->filter_envelope);
    for (i = p->layers; i < BOX_AUDIO_SYNTH_LAYERS; i ++) {
        p->layer[i] = (box_audio_synth_layer_t){0};
    }

    for (i = 0; i < p->layers; i ++) {
        box_audio_synth_layer_t *l = &p->layer[i];
        float lo;
        if (l->wave > BOX_AUDIO_SYNTH_PLUCK) {
            l->wave = BOX_AUDIO_SYNTH_SINE;
        }

        lo = l->wave == BOX_AUDIO_SYNTH_PLUCK ? BOX_AUDIO_SYNTH_PLUCK_MIN :
            BOX_AUDIO_SYNTH_FREQUENCY_MIN;
        l->has_envelope = l->has_envelope != 0;
        l->frequency = box_audio_synth_clamp(l->frequency, lo, BOX_AUDIO_SYNTH_HZ_MAX);
        l->frequency_end = box_audio_synth_end_clamp(l->frequency_end, lo);
        l->sweep = box_audio_synth_clamp(l->sweep, 0, BOX_AUDIO_SYNTH_LENGTH_MAX);
        l->level = box_audio_synth_clamp(l->level, 0, BOX_AUDIO_VOLUME_MAX);
        box_audio_synth_envelope_clamp(&l->envelope);
        l->modulates = l->modulates <= i ? l->modulates : 0;
        l->modulation = l->modulation == BOX_AUDIO_SYNTH_RING ? BOX_AUDIO_SYNTH_RING :
            BOX_AUDIO_SYNTH_PHASE;
        l->index = box_audio_synth_clamp(l->index, 0, l->modulation == BOX_AUDIO_SYNTH_RING ?
            1.0f : BOX_AUDIO_SYNTH_INDEX_MAX);
        l->feedback = box_audio_synth_clamp(l->feedback, 0, BOX_AUDIO_SYNTH_FEEDBACK_MAX);
        l->audible = l->audible != 0;
        l->voices = l->voices < 1 ? 1 :
            (l->voices > BOX_AUDIO_SYNTH_UNISON_MAX ? BOX_AUDIO_SYNTH_UNISON_MAX : l->voices);
        l->detune = box_audio_synth_clamp(l->detune, 0, BOX_AUDIO_SYNTH_DETUNE_MAX);
        l->width = box_audio_synth_clamp(l->width, 0, 1);
        l->decay = box_audio_synth_clamp(l->decay, BOX_AUDIO_SYNTH_PLUCK_DECAY_MIN,
            BOX_AUDIO_SYNTH_PLUCK_DECAY_MAX);
        l->brightness = box_audio_synth_clamp(l->brightness, 0, 1);
        l->hardness = box_audio_synth_clamp(l->hardness, 0, 1);
        l->rings_free = l->rings_free && l->wave == BOX_AUDIO_SYNTH_PLUCK && !l->has_envelope;
        l->pulse_width = box_audio_synth_width_clamp(l->pulse_width);
    }

    if (p->lfos > BOX_AUDIO_SYNTH_LFOS) {
        p->lfos = BOX_AUDIO_SYNTH_LFOS;
    }

    for (i = 0; i < BOX_AUDIO_SYNTH_LFOS; i ++) {
        box_audio_synth_lfo_t *lfo = &p->lfo[i];
        if (lfo->wave > BOX_AUDIO_SYNTH_LFO_RANDOM) {
            lfo->wave = BOX_AUDIO_SYNTH_LFO_SINE;
        }

        lfo->rate = box_audio_synth_clamp(lfo->rate, 0, BOX_AUDIO_SYNTH_LFO_RATE_MAX);
        lfo->delay = box_audio_synth_clamp(lfo->delay, 0, BOX_AUDIO_SYNTH_LENGTH_MAX);
        lfo->fade = box_audio_synth_clamp(lfo->fade, 0, BOX_AUDIO_SYNTH_LENGTH_MAX);
        lfo->phase = box_audio_synth_clamp(lfo->phase, 0, 1);
        lfo->phase = lfo->phase < 1 ? lfo->phase : 0.0f;
        lfo->random_phase = lfo->random_phase != 0;
        lfo->layer = lfo->layer <= BOX_AUDIO_SYNTH_LAYERS ? lfo->layer : 0;
        lfo->pitch = box_audio_synth_depth_clamp(lfo->pitch, BOX_AUDIO_SYNTH_CENTS_MAX);
        lfo->amplitude = box_audio_synth_clamp(lfo->amplitude, 0, 1);
        lfo->cutoff = box_audio_synth_depth_clamp(lfo->cutoff, BOX_AUDIO_SYNTH_OCTAVES_MAX);
        lfo->pulse_width = box_audio_synth_depth_clamp(lfo->pulse_width,
            0.5f - BOX_AUDIO_SYNTH_PULSE_MIN);
    }
}

void box_audio_synth_parts_init(
    box_audio_core_t *core)
{
    int32_t i;
    for (i = 0; i < BOX_AUDIO_SYNTH_PARTS; i ++) {
        core->synth_free[i] = (uint16_t)(BOX_AUDIO_SYNTH_PARTS - 1 - i);
    }

    core->synth_free_count = BOX_AUDIO_SYNTH_PARTS;
    for (i = 0; i < BOX_AUDIO_SYNTH_STRINGS; i ++) {
        core->string_free[i] = (uint16_t)(BOX_AUDIO_SYNTH_STRINGS - 1 - i);
    }

    core->string_free_count = BOX_AUDIO_SYNTH_STRINGS;
}

bool box_audio_synth_fits(
    const box_audio_core_t *core,
    uint32_t sound)
{
    return box_audio_synth_parts(core, sound) <= core->synth_free_count &&
        box_audio_synth_strings_fit(core, sound);
}

int32_t box_audio_synth_parts(
    const box_audio_core_t *core,
    uint32_t sound)
{
    return (int32_t)core->synths[core->sounds[sound].synth - 1].patch.layers;
}

bool box_audio_synth_strings_fit(
    const box_audio_core_t *core,
    uint32_t sound)
{
    return core->synths[core->sounds[sound].synth - 1].strings <= core->string_free_count;
}

void box_audio_synth_end(
    box_audio_core_t *core,
    box_audio_synth_source_t *src)
{
    box_audio_synth_string_t *s, *next;
    int32_t l;
    for (l = src->layers - 1; l >= 0; l --) {
        for (s = src->part[l]->string; s; s = next) {
            next = s->next;
            s->next = NULL;
            core->string_free[core->string_free_count ++] = (uint16_t)(s - core->synth_strings);
        }

        src->part[l]->string = NULL;

        core->synth_free[core->synth_free_count ++] =
            (uint16_t)(src->part[l] - core->synth_parts);
        src->part[l] = NULL;
    }

    src->layers = 0;
    src->strings = 0;
}

void box_audio_synth_command(
    box_audio_core_t *core,
    const box_audio_cmd_synth_t *cmd)
{
    box_audio_sound_t *s;
    int32_t slot = -1, i;
    if (cmd->sound >= BOX_AUDIO_SOUND_MAX) {
        goto ignored;
    }

    s = &core->sounds[cmd->sound];
    if (s->state == BOX_AUDIO_SOUND_READY && s->synth) {
        slot = s->synth - 1;
    } else if (s->state == BOX_AUDIO_SOUND_FREE) {
        for (i = 0; i < BOX_AUDIO_SYNTH_MAX && slot < 0; i ++) {
            if (!core->synths[i].used) {
                slot = i;
            }
        }

        if (slot >= 0) {
            core->synths[slot].used = true;
            core->synths[slot].plays = 0;
            s->data = NULL;
            s->frames = 0;
            s->channels = 1;
            s->rate = 1;
            s->users = 0;
            s->state = BOX_AUDIO_SOUND_READY;
            s->synth = slot + 1;
        }
    }

    if (slot < 0) {
        goto ignored;
    }

    core->synths[slot].patch = cmd->patch;
    box_audio_synth_patch_clamp(&core->synths[slot].patch);
    core->synths[slot].strings = 0;
    for (i = 0; i < (int32_t)core->synths[slot].patch.layers; i ++) {
        if (core->synths[slot].patch.layer[i].wave == BOX_AUDIO_SYNTH_PLUCK) {
            core->synths[slot].strings += (int32_t)core->synths[slot].patch.layer[i].voices;
        }
    }

    goto done;
ignored:
    box_audio_core_event(core, BOX_AUDIO_EVENT_IGNORED, BOX_AUDIO_CMD_SYNTH, 0);
done:
    return;
}

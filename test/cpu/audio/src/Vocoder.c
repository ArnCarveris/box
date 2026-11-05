/**
 * @file Vocoder.c
 * @brief The vocoder, measured in offline renders of white noise carriers under sine, noise and
 * burst modulators: where the output lies in the spectrum, its level, silence, the timing of
 * bursts, the resolution of the band count, block sizes, and the component in a world with a
 * missing, deleted and cyclic modulator.
 */

#include "world.h"
#include <stdio.h>
#include <stdlib.h>

/** Frames of the longest render of a case, two seconds */
#define VC_FRAMES (AUDIO_RATE * 2)

/** Frames rendered before the sounds play, so the vocoder enters at once and the volume of
 * the silent bus settles */
#define VC_WARM (AUDIO_RATE / 10)

/** Bins on either side of a frequency that vc_power sums */
#define VC_BINS 16

/** Output of the last render */
static float vc_l[VC_FRAMES], vc_r[VC_FRAMES];

/** Output of a second render that a case compares */
static float vc_b_l[VC_FRAMES], vc_b_r[VC_FRAMES];

/** The carrier, sound 1, and the modulator, sound 2 */
static float vc_carrier[VC_FRAMES], vc_mod[VC_FRAMES];

/** Scratch of the warm up render */
static float vc_warm_l[VC_WARM], vc_warm_r[VC_WARM];

/** Memory of the vocoder of the core */
static float *vc_memory;

/** Fill a buffer with white noise that fills -level..level evenly, from a seed.
 *
 * @param x The buffer
 * @param frames Frames
 * @param level Peak level
 * @param seed Seed, not 0
 */
static void vc_noise(
    float *x,
    int32_t frames,
    float level,
    uint32_t seed)
{
    uint32_t s = seed;
    int32_t i;
    for (i = 0; i < frames; i ++) {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        x[i] = level * (float)((double)(int32_t)s / 2147483648.0);
    }
}

/** The parameters of a vocoder at the defaults of the component but its band count.
 *
 * @param bands Bands
 * @return The parameters
 */
static box_audio_vocoder_params_t vc_params(
    float bands)
{
    return (box_audio_vocoder_params_t){
        .bands = bands, .low = 100, .high = 8000, .q = 1, .attack = 0.005f,
        .release = 0.05f, .shift = 0, .unvoiced = 0, .mix = 1 };
}

/** Send a vocoder command for slot 0, insert 0 of bus 1.
 *
 * @param core The core
 * @param key Modulator bus, BOX_AUDIO_BUS_NONE for none
 * @param p The parameters
 */
static void vc_effect(
    box_audio_core_t *core,
    uint32_t key,
    box_audio_vocoder_params_t p)
{
    uint8_t bytes[256];
    float params[BOX_AUDIO_EFFECT_PARAMS] = {0};
    box_audio_writer_t w;
    memcpy(params, &p, sizeof(p));
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_effect(&w, 0, BOX_AUDIO_EFFECT_VOCODER, 1, 0, key, params));
    box_audio_core_apply(core, w.data, w.size);
}

/** A new core with a vocoder on bus 1, the carrier, into the master, keyed on bus 2, the
 * modulator, which runs first into bus 3 at volume 0, so the master hears only bus 1.
 *
 * @param p The parameters of the vocoder
 * @param key Modulator bus, BOX_AUDIO_BUS_NONE for none
 * @return The core
 */
static box_audio_core_t* vc_core(
    box_audio_vocoder_params_t p,
    uint32_t key)
{
    box_audio_core_t *core = audio_new();
    box_audio_route_t routes[] = {
        AUDIO_OUT(2, 3), AUDIO_OUT(1, 0), AUDIO_OUT(3, 0), AUDIO_MASTER };
    int32_t floats = box_audio_effect_floats(BOX_AUDIO_EFFECT_VOCODER, AUDIO_RATE);
    test_assert(floats > 0);
    audio_order(core, routes, 4);
    audio_bus_set(core, 3, BOX_AUDIO_PARAM_VOLUME, 0);
    vc_memory = calloc((size_t)floats, sizeof(float));
    test_assert(box_audio_core_effect_memory(core, 0, vc_memory, floats));
    vc_effect(core, key, p);

    return core;
}

/** Free a core and the memory of its vocoder.
 *
 * @param core The core
 */
static void vc_free(
    box_audio_core_t *core)
{
    box_audio_core_free(core);
    free(vc_memory);
    vc_memory = NULL;
}

/** Render frames of a core into buffers in blocks of a size.
 *
 * @param core The core
 * @param l Left samples
 * @param r Right samples
 * @param at First frame
 * @param frames Frames
 * @param size Frames of a block, 0 for sizes that change from block to block
 */
static void vc_render(
    box_audio_core_t *core,
    float *l,
    float *r,
    int32_t at,
    int32_t frames,
    int32_t size)
{
    int32_t end = at + frames, k = 0;
    while (at < end) {
        int32_t n = size ? size : 1 + (k * 97) % 613;
        n = n < end - at ? n : end - at;
        box_audio_core_render(core, l + at, r + at, n);
        at += n;
        k ++;
    }
}

/** Warm a core up, then play vc_carrier on bus 1 and vc_mod on bus 2, both looped, from the
 * first frame of the next render.
 *
 * @param core The core
 * @param carrier Frames of the carrier
 * @param mod Frames of the modulator
 */
static void vc_play(
    box_audio_core_t *core,
    int32_t carrier,
    int32_t mod)
{
    vc_render(core, vc_warm_l, vc_warm_r, 0, VC_WARM, BOX_AUDIO_CORE_QUANTUM);
    test_assert(box_audio_core_sound(core, 1, vc_carrier, carrier, 1, AUDIO_RATE));
    test_assert(box_audio_core_sound(core, 2, vc_mod, mod, 1, AUDIO_RATE));
    audio_play(core, 1, 1, 1, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);
    audio_play(core, 2, 2, 2, 1, 0, 1, BOX_AUDIO_PLAY_LOOP);
}

/** Power of a range at a frequency, by the Goertzel filter.
 *
 * @param x Samples
 * @param from First frame
 * @param to End frame
 * @param f Hz
 * @return The power of the bin
 */
static double vc_goertzel(
    const float *x,
    int32_t from,
    int32_t to,
    double f)
{
    double c = 2 * cos(2 * AUDIO_PI * f / AUDIO_RATE), s1 = 0, s2 = 0;
    int32_t i;
    for (i = from; i < to; i ++) {
        double s0 = (double)x[i] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }

    return s1 * s1 + s2 * s2 - c * s1 * s2;
}

/** Power of a range around a frequency: the bins of the range within VC_BINS of it summed,
 * so the power of noise reads steadily.
 *
 * @param x Samples
 * @param from First frame
 * @param to End frame
 * @param f Hz
 * @return The power
 */
static double vc_power(
    const float *x,
    int32_t from,
    int32_t to,
    double f)
{
    double bin = (double)AUDIO_RATE / (double)(to - from), result = 0;
    int32_t k;
    for (k = -VC_BINS; k <= VC_BINS; k ++) {
        result += vc_goertzel(x, from, to, f + bin * k);
    }

    return result;
}

/** RMS level of a range.
 *
 * @param x Samples
 * @param from First frame
 * @param to End frame
 * @return The level
 */
static double vc_rms(
    const float *x,
    int32_t from,
    int32_t to)
{
    double sum = 0;
    int32_t i;
    for (i = from; i < to; i ++) {
        sum += (double)x[i] * (double)x[i];
    }

    return to > from ? sqrt(sum / (double)(to - from)) : 0;
}

/** Level in dB of a ratio of powers.
 *
 * @param a The power
 * @param b The power it is compared to
 * @return The ratio in dB
 */
static double vc_db(
    double a,
    double b)
{
    return 10 * log10((a + 1e-30) / (b + 1e-30));
}

/** Render a second of white noise vocoded by a sine of 1 kHz into vc_l and vc_r.
 *
 * @param bands Bands of the vocoder
 */
static void vc_sine(
    float bands)
{
    box_audio_core_t *core = vc_core(vc_params(bands), 2);
    vc_noise(vc_carrier, AUDIO_RATE, 0.2f, 1);
    audio_sine(vc_mod, AUDIO_RATE, 1000, 0.3f);
    vc_play(core, AUDIO_RATE, AUDIO_RATE);
    vc_render(core, vc_l, vc_r, 0, AUDIO_RATE, BOX_AUDIO_CORE_QUANTUM);
    vc_free(core);
}

void Vocoder_sine_band(void) {
    // audio/vocoder
    const int32_t from = AUDIO_RATE / 2, to = AUDIO_RATE;
    const double hz[] = { 250, 500, 1000, 2000, 4000 };
    double p[5];
    int32_t i;
    vc_sine(16);
    for (i = 0; i < 5; i ++) {
        p[i] = vc_power(vc_l, from, to, hz[i]);
    }

    printf("noise under a 1 kHz sine: %.1f dB at 250 Hz, %.1f at 500, %.1f at 2000, %.1f at "
        "4000 against 1000\n", vc_db(p[0], p[2]), vc_db(p[1], p[2]), vc_db(p[3], p[2]),
        vc_db(p[4], p[2]));
    test_assert(vc_rms(vc_l, from, to) > 0.005);
    test_assert(vc_db(p[1], p[2]) < -12);
    test_assert(vc_db(p[3], p[2]) < -12);
    test_assert(vc_db(p[0], p[2]) < -25);
    test_assert(vc_db(p[4], p[2]) < -25);
    test_assert(audio_hash(vc_l, AUDIO_RATE) == audio_hash(vc_r, AUDIO_RATE));
}

void Vocoder_level(void) {
    // audio/vocoder
    const int32_t from = AUDIO_RATE / 2, to = AUDIO_RATE;
    box_audio_core_t *core = vc_core(vc_params(16), 2);
    double carrier, out;
    vc_noise(vc_carrier, AUDIO_RATE, 0.2f, 1);
    vc_noise(vc_mod, AUDIO_RATE, 1.0f, 7);
    vc_play(core, AUDIO_RATE, AUDIO_RATE);
    vc_render(core, vc_l, vc_r, 0, AUDIO_RATE, BOX_AUDIO_CORE_QUANTUM);
    vc_free(core);
    carrier = vc_rms(vc_carrier, 0, AUDIO_RATE);
    out = vc_rms(vc_l, from, to);
    printf("a full band modulator at %.3f RMS gives %.2f dB against the carrier\n",
        vc_rms(vc_mod, 0, AUDIO_RATE), 20 * log10(out / carrier));
    test_assert(fabs(20 * log10(out / carrier)) < 6);
}

void Vocoder_silent_modulator(void) {
    // audio/vocoder
    box_audio_vocoder_params_t p = vc_params(16);
    box_audio_core_t *core;
    p.unvoiced = 0.5f;
    core = vc_core(p, 2);
    vc_noise(vc_carrier, AUDIO_RATE, 0.3f, 3);
    audio_fill(vc_mod, AUDIO_RATE, 0);
    vc_play(core, AUDIO_RATE, AUDIO_RATE);
    vc_render(core, vc_l, vc_r, 0, AUDIO_RATE, BOX_AUDIO_CORE_QUANTUM);
    vc_free(core);
    test_flt(audio_peak(vc_l, 0, AUDIO_RATE), 0);
    test_flt(audio_peak(vc_r, 0, AUDIO_RATE), 0);
}

void Vocoder_bursts(void) {
    // audio/vocoder
    const int32_t period = AUDIO_RATE / 5, on = AUDIO_RATE / 20, ms = AUDIO_RATE / 1000;
    box_audio_core_t *core = vc_core(vc_params(16), 2);
    int32_t i, n;
    vc_noise(vc_carrier, AUDIO_RATE, 0.2f, 5);
    vc_noise(vc_mod, AUDIO_RATE, 0.5f, 11);
    for (i = 0; i < AUDIO_RATE; i ++) {
        if (i % period >= on) {
            vc_mod[i] = 0;
        }
    }

    vc_play(core, AUDIO_RATE, AUDIO_RATE);
    vc_render(core, vc_l, vc_r, 0, AUDIO_RATE, BOX_AUDIO_CORE_QUANTUM);
    vc_free(core);
    for (n = 1; n < 5; n ++) {
        int32_t s = n * period, e = s + on, rise = -1;
        double before = vc_rms(vc_l, s - 10 * ms, s);
        double during = vc_rms(vc_l, s + 10 * ms, e);
        double after = vc_rms(vc_l, e + 120 * ms, e + 145 * ms);
        for (i = s - 10 * ms; i < e && rise < 0; i ++) {
            if (vc_rms(vc_l, i - ms, i) > 0.5 * during) {
                rise = i - s;
            }
        }

        printf("burst %d: %.1f dB before, %.1f dB after its end, rises %.2f ms after it "
            "starts\n", n, 20 * log10(before / during + 1e-30),
            20 * log10(after / during + 1e-30), (double)rise / ms);
        test_assert(before < 0.125 * during);
        test_assert(after < 0.2 * during);
        test_assert(rise >= 0 && rise <= 12 * ms);
        test_assert(vc_rms(vc_l, s - 2 * ms, s) < 0.2 * during);
    }
}

void Vocoder_band_count(void) {
    // audio/vocoder
    const int32_t from = AUDIO_RATE / 2, to = AUDIO_RATE;
    double coarse, fine, center, side;
    vc_sine(8);
    center = vc_power(vc_l, from, to, 1000);
    side = vc_power(vc_l, from, to, 707) + vc_power(vc_l, from, to, 1414);
    coarse = vc_db(side, center);
    vc_sine(32);
    center = vc_power(vc_l, from, to, 1000);
    side = vc_power(vc_l, from, to, 707) + vc_power(vc_l, from, to, 1414);
    fine = vc_db(side, center);
    printf("half an octave off a 1 kHz sine: %.1f dB with 8 bands, %.1f dB with 32\n", coarse,
        fine);
    test_assert(fine < coarse - 12);
}

/** Render a vocoder with a shift, unvoiced noise and a change of all its parameters halfway,
 * the band count included, in blocks of a size.
 *
 * @param l Left samples, AUDIO_RATE frames
 * @param r Right samples
 * @param size Frames of a block, 0 for sizes that change from block to block
 */
static void vc_sizes(
    float *l,
    float *r,
    int32_t size)
{
    box_audio_vocoder_params_t p = vc_params(24);
    box_audio_core_t *core;
    int32_t i;
    p.q = 1.5f;
    p.shift = 0.3f;
    p.unvoiced = 0.4f;
    p.mix = 0.9f;
    core = vc_core(p, 2);
    vc_noise(vc_carrier, AUDIO_RATE, 0.2f, 13);
    for (i = 0; i < AUDIO_RATE; i ++) {
        vc_carrier[i] += 0.2f * (float)(2.0 * ((i * 110) % AUDIO_RATE) / AUDIO_RATE - 1);
    }

    vc_noise(vc_mod, AUDIO_RATE / 3, 0.4f, 17);
    for (i = 0; i < AUDIO_RATE / 3; i ++) {
        if (i % (AUDIO_RATE / 8) > AUDIO_RATE / 30) {
            vc_mod[i] *= 0.05f;
        }
    }

    vc_play(core, AUDIO_RATE, AUDIO_RATE / 3);
    vc_render(core, l, r, 0, AUDIO_RATE / 2, size);
    p.bands = 12;
    p.low = 150;
    p.high = 6000;
    p.q = 0.7f;
    p.shift = -0.5f;
    p.attack = 0.002f;
    p.release = 0.1f;
    p.unvoiced = 0.1f;
    p.mix = 0.7f;
    vc_effect(core, 2, p);
    vc_render(core, l, r, AUDIO_RATE / 2, AUDIO_RATE / 2, size);
    vc_free(core);
}

void Vocoder_render_sizes(void) {
    // audio/vocoder
    vc_sizes(vc_l, vc_r, BOX_AUDIO_CORE_QUANTUM);
    vc_sizes(vc_b_l, vc_b_r, 0);
    test_assert(audio_hash(vc_l, AUDIO_RATE) == audio_hash(vc_b_l, AUDIO_RATE));
    test_assert(audio_hash(vc_r, AUDIO_RATE) == audio_hash(vc_b_r, AUDIO_RATE));
    test_assert(audio_peak(vc_l, 0, AUDIO_RATE / 2) > 0.01f);
    test_assert(audio_peak(vc_l, AUDIO_RATE * 3 / 4, AUDIO_RATE) > 0.01f);

    /* The same render again gives the same bits */
    vc_sizes(vc_b_l, vc_b_r, BOX_AUDIO_CORE_QUANTUM);
    test_assert(audio_hash(vc_l, AUDIO_RATE) == audio_hash(vc_b_l, AUDIO_RATE));
    test_assert(audio_hash(vc_r, AUDIO_RATE) == audio_hash(vc_b_r, AUDIO_RATE));
}

void Vocoder_no_key_passes(void) {
    // audio/vocoder
    box_audio_core_t *core = vc_core(vc_params(16), BOX_AUDIO_BUS_NONE);
    audio_sine(vc_carrier, AUDIO_RATE, 440, 0.25f);
    audio_sine(vc_mod, AUDIO_RATE, 1000, 0.3f);
    vc_play(core, AUDIO_RATE, AUDIO_RATE);
    vc_render(core, vc_l, vc_r, 0, AUDIO_RATE / 4, BOX_AUDIO_CORE_QUANTUM);
    vc_free(core);
    test_assert(fabs(audio_peak(vc_l, AUDIO_RATE / 8, AUDIO_RATE / 4) - 0.25f) < 1e-3f);
}

void Vocoder_component(void) {
    // audio/vocoder
    ecs_world_t *world = audio_world();
    ecs_entity_t e = ecs_new(world), drums = audio_bus(world, "drums", 0, 1);
    float params[BOX_AUDIO_EFFECT_PARAMS];
    const Vocoder *v;
    ecs_add(world, e, Vocoder);
    v = ecs_get(world, e, Vocoder);
    test_assert(v->modulator == 0);
    test_int(v->bands, 16);
    test_flt(v->low, 100);
    test_flt(v->high, 8000);
    test_flt(v->q, 1);
    test_flt(v->attack, 0.005f);
    test_flt(v->release, 0.05f);
    test_flt(v->shift, 0);
    test_flt(v->unvoiced, 0);
    test_flt(v->mix, 1);
    test_assert(ecs_has(world, e, AudioEffectState));
    ecs_set(world, e, Vocoder, { .modulator = drums, .bands = 24, .low = 120, .high = 6000,
        .q = 2, .attack = 0.002f, .release = 0.1f, .shift = -0.3f, .unvoiced = 0.25f,
        .mix = 0.8f });
    test_int(box_audio_module_effect_kind(world, e, params), BOX_AUDIO_EFFECT_VOCODER);
    test_flt(params[0], 24);
    test_flt(params[1], 120);
    test_flt(params[2], 6000);
    test_flt(params[3], 2);
    test_flt(params[4], 0.002f);
    test_flt(params[5], 0.1f);
    test_flt(params[6], -0.3f);
    test_flt(params[7], 0.25f);
    test_flt(params[8], 0.8f);
    test_assert(box_audio_effect_floats(BOX_AUDIO_EFFECT_VOCODER, AUDIO_RATE) > 0);
    ecs_fini(world);
}

/** The key sent for an effect entity.
 *
 * @param world The world
 * @param e The effect entity
 * @return The key bus sent, BOX_AUDIO_BUS_NONE for none
 */
static uint32_t vc_sent_key(
    ecs_world_t *world,
    ecs_entity_t e)
{
    int32_t slot = box_audio_module_effect_slot(world, e);
    test_assert(slot >= 0);

    return box_audio_module_get(world)->effects[slot].key;
}

/** Peak of the left channel of the tap over its last tenth of a second.
 *
 * @return The peak
 */
static float vc_tap_peak(void)
{
    return audio_peak(audio_tap_l, audio_tap_count - AUDIO_RATE / 10, audio_tap_count);
}

void Vocoder_missing_modulator(void) {
    // audio/vocoder
    ecs_world_t *world = audio_world();
    ecs_entity_t pad = audio_bus(world, "pad", 0, 1);
    ecs_entity_t voc = ecs_entity(world, { .name = "pad_vocoder" });
    ecs_entity_t lead;
    ecs_add(world, voc, Vocoder);
    audio_insert(world, pad, 0, voc);
    audio_source(world, audio_tone(world, NULL, 440, 0.2f, AUDIO_RATE), 1, true, pad);
    audio_log_start();
    audio_run(world, 10);
    audio_log_stop();
    printf("%s\n", audio_log_last);
    test_int(audio_log_errors, 1);
    test_assert(strstr(audio_log_last, "pad_vocoder") != NULL);
    test_assert(strstr(audio_log_last, "modulator") != NULL);
    test_int(vc_sent_key(world, voc), BOX_AUDIO_BUS_NONE);
    test_assert(fabs(vc_tap_peak() - 0.2f) < 2e-3f);

    /* It does not log again while nothing changes */
    audio_log_start();
    audio_run(world, 5);
    audio_log_stop();
    test_int(audio_log_errors, 0);

    /* A modulator keys it without an error */
    lead = audio_bus(world, "lead", 0, 1);
    ecs_ensure(world, voc, Vocoder)->modulator = lead;
    ecs_modified(world, voc, Vocoder);
    audio_log_start();
    audio_run(world, 5);
    audio_log_stop();
    test_int(audio_log_errors, 0);
    test_int(vc_sent_key(world, voc), box_audio_bus_of(world, box_audio_module_get(world),
        lead));
    test_assert(audio_position(world, lead) < audio_position(world, pad));

    /* A deleted modulator logs one error and passes the input */
    ecs_delete(world, lead);
    audio_log_start();
    audio_run(world, 10);
    audio_log_stop();
    printf("%s\n", audio_log_last);
    test_int(audio_log_errors, 1);
    test_assert(strstr(audio_log_last, "pad_vocoder") != NULL);
    test_int(vc_sent_key(world, voc), BOX_AUDIO_BUS_NONE);
    test_assert(fabs(vc_tap_peak() - 0.2f) < 2e-3f);
    ecs_fini(world);
}

void Vocoder_cyclic_modulator(void) {
    // audio/vocoder
    ecs_world_t *world = audio_world();
    ecs_entity_t group = audio_bus(world, "voice_group", 0, 1);
    ecs_entity_t voice = audio_bus(world, "voice_bus", group, 1);
    ecs_entity_t voc = ecs_entity(world, { .name = "voice_vocoder" });
    ecs_set(world, voc, Vocoder, { .modulator = group, .bands = 16, .low = 100, .high = 8000,
        .q = 1, .attack = 0.005f, .release = 0.05f, .mix = 1 });
    audio_insert(world, voice, 0, voc);
    audio_source(world, audio_tone(world, NULL, 440, 0.2f, AUDIO_RATE), 1, true, voice);
    audio_log_start();
    audio_run(world, 10);
    audio_log_stop();
    printf("%s\n", audio_log_last);
    test_int(audio_log_errors, 1);
    test_assert(strstr(audio_log_last, "voice_bus") != NULL);
    test_assert(strstr(audio_log_last, "voice_group") != NULL);
    test_int(vc_sent_key(world, voc), BOX_AUDIO_BUS_NONE);
    test_assert(audio_position(world, voice) < audio_position(world, group));
    test_assert(fabs(vc_tap_peak() - 0.2f) < 2e-3f);

    /* Another change of the order does not log it again */
    audio_bus(world, "other_bus", 0, 1);
    audio_log_start();
    audio_run(world, 3);
    audio_log_stop();
    test_int(audio_log_errors, 0);

    /* Its own channel as the modulator is a cycle too */
    ecs_ensure(world, voc, Vocoder)->modulator = voice;
    ecs_modified(world, voc, Vocoder);
    audio_log_start();
    audio_run(world, 3);
    audio_log_stop();
    test_int(audio_log_errors, 1);
    test_assert(strstr(audio_log_last, "voice_vocoder") != NULL);
    test_int(vc_sent_key(world, voc), BOX_AUDIO_BUS_NONE);
    ecs_fini(world);
}

void Vocoder_script(void) {
    // audio/vocoder
    ecs_world_t *world = audio_world();
    char *code = audio_read("test/assets/audio/vocoder.flecs");
    ecs_entity_t drums, pad, voc, beat;
    const Vocoder *v;
    test_assert(ecs_script_run(world, "vocoder", code, NULL) == 0);
    ecs_os_free(code);
    drums = ecs_lookup(world, "drums");
    pad = ecs_lookup(world, "pad");
    voc = ecs_lookup(world, "pad_vocoder");
    test_assert(drums && pad && voc);
    v = ecs_get(world, voc, Vocoder);
    test_assert(v != NULL);
    test_assert(v->modulator == drums);
    test_int(v->bands, 16);
    test_flt(v->attack, 0.002f);
    test_flt(v->release, 0.08f);
    test_flt(v->unvoiced, 0.2f);

    beat = audio_source(world, audio_tone(world, NULL, 1000, 0.3f, AUDIO_RATE), 1, true,
        drums);
    audio_source(world, audio_tone(world, NULL, 1000, 0.2f, AUDIO_RATE), 1, true, pad);
    audio_log_start();
    audio_run(world, 20);
    audio_log_stop();
    test_int(audio_log_errors, 0);
    test_assert(box_audio_module_effect_slot(world, voc) >= 0);
    test_int(vc_sent_key(world, voc), box_audio_bus_of(world, box_audio_module_get(world),
        drums));
    test_assert(audio_position(world, drums) < audio_position(world, pad));
    test_assert(vc_tap_peak() > 0.01f);

    /* Without the drums the pad falls silent */
    ecs_delete(world, beat);
    audio_run(world, 60);
    test_assert(vc_tap_peak() < 1e-3f);
    ecs_fini(world);
}

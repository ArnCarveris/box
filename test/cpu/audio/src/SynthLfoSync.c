/**
 * @file SynthLfoSync.c
 * @brief Synth LFOs synced to the tempo of the Transport, SynthLfo.sync: the rate of tempo /
 * sync, the start phase locked to the beat of a play, on a core through play and legato
 * commands, and through notes on a world with the null output, of the timeline, without a
 * Transport and across a tempo change.
 *
 * The sounds are a sine at 2000 Hz under a tremolo of depth 1, whose square holds the LFO: the
 * phase and the strength of the LFO at a frequency come from the correlation of the square
 * with a sine and a cosine at it over whole periods.
 */

#include "world.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Frames of a beat at 120 bpm */
#define LFOSYNC_BEAT 24000

/** Turns two phases of the LFO that should agree may differ by */
#define LFOSYNC_TURNS 0.01

/** Room for the commands of a test */
#define LFOSYNC_BYTES 256

/** Strength and phase of the LFO in the square of a range of samples at a frequency.
 *
 * @param x Samples
 * @param from First frame
 * @param frames Frames, whole periods of hz
 * @param hz Frequency
 * @param phase Receives the phase at from in turns, 0..1, NULL for none
 * @return The strength
 */
static double lfosync_bin(
    const float *x,
    int32_t from,
    int32_t frames,
    double hz,
    double *phase)
{
    double s = 0, c = 0, w = 2 * AUDIO_PI * hz / AUDIO_RATE, y, t;
    int32_t i;
    for (i = 0; i < frames; i ++) {
        y = (double)x[from + i];
        y *= y;
        s += y * sin(w * (double)i);
        c += y * cos(w * (double)i);
    }

    if (phase) {
        t = atan2(c, s) / (2 * AUDIO_PI);
        *phase = t - floor(t);
    }

    return sqrt(s * s + c * c) / (double)frames;
}

/** Distance of two phases in turns, 0..0.5.
 *
 * @param a A phase
 * @param b Another phase
 * @return The distance
 */
static double lfosync_diff(
    double a,
    double b)
{
    double d = a - b;
    d -= floor(d + 0.5);

    return fabs(d);
}

/** A sine at 2000 Hz that sustains for two seconds under a tremolo of depth 1, optionally as
 * a unison of 3 copies over the full width, which renders in stereo.
 *
 * @param wave Wave of the LFO
 * @param rate Hz of the LFO
 * @param phase Start phase of the LFO
 * @param sync Beats of a cycle of the LFO, 0 for rate
 * @param wide Whether it renders in stereo
 * @return The patch
 */
static box_audio_synth_patch_t lfosync_patch(
    uint32_t wave,
    float rate,
    float phase,
    float sync,
    bool wide)
{
    box_audio_synth_patch_t p = {0};
    p.layers = 1;
    p.envelope = (box_audio_synth_envelope_t){ .attack = 0.001f, .sustain = 1, .hold = 2,
        .release = 0.05f };
    p.layer[0] = (box_audio_synth_layer_t){
        .wave = BOX_AUDIO_SYNTH_SINE, .frequency = 2000, .level = 0.5f };
    if (wide) {
        p.layer[0].voices = 3;
        p.layer[0].detune = 20;
        p.layer[0].width = 1;
    }

    p.lfos = 1;
    p.lfo[0] = (box_audio_synth_lfo_t){ .wave = wave, .rate = rate, .phase = phase,
        .sync = sync, .amplitude = 1 };

    return p;
}

/** Give sound 1 of a new core a patch.
 *
 * @param p The patch
 * @return The core
 */
static box_audio_core_t* lfosync_core(
    const box_audio_synth_patch_t *p)
{
    box_audio_core_t *core = audio_new();
    uint8_t bytes[sizeof(box_audio_cmd_synth_t)];
    box_audio_writer_t w;
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_synth(&w, 1, p));
    box_audio_core_apply(core, w.data, w.size);

    return core;
}

/** Hash both channels of the last render.
 *
 * @param frames Frames
 * @return The hash
 */
static uint64_t lfosync_hash(
    int32_t frames)
{
    return audio_hash(audio_l, frames) * 1000003u ^ audio_hash(audio_r, frames);
}

/** Render a play of a patch at a beat and a tempo on a core of its own.
 *
 * @param p The patch
 * @param beat Beat of the play
 * @param tempo Beats per second of the play
 * @param frames Frames to render
 * @return The hash of the render, which stays in audio_l and audio_r
 */
static uint64_t lfosync_render(
    const box_audio_synth_patch_t *p,
    float beat,
    float tempo,
    int32_t frames)
{
    box_audio_core_t *core = lfosync_core(p);
    uint8_t bytes[LFOSYNC_BYTES];
    box_audio_writer_t w;
    uint64_t result;
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_play_note(&w, 1, 1, 0, 1, 0, 1, 1, beat, tempo, 0, 0, 0));
    box_audio_core_apply(core, w.data, w.size);
    audio_render(core, 0, frames);
    result = lfosync_hash(frames);
    box_audio_core_free(core);

    return result;
}

/** Render a plain play of a patch, box_audio_write_play, on a core of its own.
 *
 * @param p The patch
 * @param frames Frames to render
 * @return The hash of the render
 */
static uint64_t lfosync_render_plain(
    const box_audio_synth_patch_t *p,
    int32_t frames)
{
    box_audio_core_t *core = lfosync_core(p);
    uint64_t result;
    audio_play(core, 1, 1, 0, 1, 0, 1, 0);
    audio_render(core, 0, frames);
    result = lfosync_hash(frames);
    box_audio_core_free(core);

    return result;
}

/** At 120 bpm an LFO with sync 1 has a period of 0.5 s: the strongest of 1 to 6 Hz is 2 Hz, its
 * phase is the same half a second later, and it renders bit-identical to a rate of 2 Hz. */
void SynthLfoSync_period(void) {
    // audio/synth
    box_audio_synth_patch_t synced = lfosync_patch(BOX_AUDIO_SYNTH_LFO_SINE, 7, 0, 1, false);
    box_audio_synth_patch_t unsynced = lfosync_patch(BOX_AUDIO_SYNTH_LFO_SINE, 2, 0, 0, false);
    double a, b, strength[7];
    uint64_t h;
    int32_t hz;
    h = lfosync_render(&synced, 0, 2, AUDIO_FRAMES);
    for (hz = 1; hz <= 6; hz ++) {
        strength[hz] = lfosync_bin(audio_l, 0, AUDIO_FRAMES, hz, NULL);
        printf("sync 1 at 120 bpm: %d Hz %f\n", hz, strength[hz]);
    }

    for (hz = 1; hz <= 6; hz ++) {
        test_assert(hz == 2 || strength[hz] * 3 < strength[2]);
    }

    lfosync_bin(audio_l, 0, LFOSYNC_BEAT, 2, &a);
    lfosync_bin(audio_l, LFOSYNC_BEAT, LFOSYNC_BEAT, 2, &b);
    printf("phase %f, %f half a second later\n", a, b);
    test_assert(lfosync_diff(a, b) < LFOSYNC_TURNS);
    test_assert(h == lfosync_render_plain(&unsynced, AUDIO_FRAMES));
}

/** The rate is tempo / sync: sync 1 at 240 bpm runs at 4 Hz, sync 0.5 at 180 bpm at 6 Hz, sync
 * 4 at 120 bpm at 0.5 Hz, each bit-identical to that rate without sync. */
void SynthLfoSync_rate(void) {
    // audio/synth
    box_audio_synth_patch_t p;
    uint64_t a, b;
    p = lfosync_patch(BOX_AUDIO_SYNTH_LFO_TRIANGLE, 1, 0, 1, false);
    a = lfosync_render(&p, 0, 4, AUDIO_FRAMES / 2);
    p = lfosync_patch(BOX_AUDIO_SYNTH_LFO_TRIANGLE, 4, 0, 0, false);
    b = lfosync_render_plain(&p, AUDIO_FRAMES / 2);
    test_assert(a == b);

    p = lfosync_patch(BOX_AUDIO_SYNTH_LFO_TRIANGLE, 1, 0, 0.5f, false);
    a = lfosync_render(&p, 0, 3, AUDIO_FRAMES / 2);
    p = lfosync_patch(BOX_AUDIO_SYNTH_LFO_TRIANGLE, 6, 0, 0, false);
    b = lfosync_render_plain(&p, AUDIO_FRAMES / 2);
    test_assert(a == b);

    p = lfosync_patch(BOX_AUDIO_SYNTH_LFO_TRIANGLE, 1, 0, 4, false);
    a = lfosync_render(&p, 0, 2, AUDIO_FRAMES / 2);
    p = lfosync_patch(BOX_AUDIO_SYNTH_LFO_TRIANGLE, 0.5f, 0, 0, false);
    b = lfosync_render_plain(&p, AUDIO_FRAMES / 2);
    test_assert(a == b);
}

/** The start phase is phase + fract(beat / sync): at beat 0.5 sync 1 starts half a cycle on,
 * as a phase of 0.5 does; beat 0.5 with sync 0.5 is the phase of beat 0; beat 1.25 with sync 1
 * and phase 0.5 starts at 0.75; and the measured phases are half a cycle apart. */
void SynthLfoSync_phase(void) {
    // audio/synth
    box_audio_synth_patch_t p = lfosync_patch(BOX_AUDIO_SYNTH_LFO_SINE, 2, 0, 1, false);
    box_audio_synth_patch_t q;
    double a, b;
    uint64_t h;
    h = lfosync_render(&p, 0, 2, LFOSYNC_BEAT);
    lfosync_bin(audio_l, 0, LFOSYNC_BEAT, 2, &a);
    test_assert(h == lfosync_render(&p, 1, 2, LFOSYNC_BEAT));
    test_assert(h == lfosync_render(&p, 3, 2, LFOSYNC_BEAT));
    h = lfosync_render(&p, 0.5f, 2, LFOSYNC_BEAT);
    lfosync_bin(audio_l, 0, LFOSYNC_BEAT, 2, &b);
    printf("phase at beat 0: %f, at beat 0.5: %f\n", a, b);
    test_assert(lfosync_diff(lfosync_diff(a, b), 0.5) < LFOSYNC_TURNS);
    q = lfosync_patch(BOX_AUDIO_SYNTH_LFO_SINE, 2, 0.5f, 0, false);
    test_assert(h == lfosync_render_plain(&q, LFOSYNC_BEAT));

    p.lfo[0].sync = 0.5f;
    h = lfosync_render(&p, 0, 2, LFOSYNC_BEAT);
    test_assert(h == lfosync_render(&p, 0.5f, 2, LFOSYNC_BEAT));
    test_assert(h == lfosync_render(&p, 2.5f, 2, LFOSYNC_BEAT));
    test_assert(h != lfosync_render(&p, 0.25f, 2, LFOSYNC_BEAT));

    p = lfosync_patch(BOX_AUDIO_SYNTH_LFO_SINE, 2, 0.5f, 1, false);
    h = lfosync_render(&p, 1.25f, 2, LFOSYNC_BEAT);
    q = lfosync_patch(BOX_AUDIO_SYNTH_LFO_SINE, 2, 0.75f, 0, false);
    test_assert(h == lfosync_render_plain(&q, LFOSYNC_BEAT));
}

/** The stereo path of a wide unison takes the rate and the phase of sync the same way. */
void SynthLfoSync_wide(void) {
    // audio/synth
    box_audio_synth_patch_t p = lfosync_patch(BOX_AUDIO_SYNTH_LFO_SQUARE, 9, 0, 1, true);
    box_audio_synth_patch_t q = lfosync_patch(BOX_AUDIO_SYNTH_LFO_SQUARE, 2, 0.5f, 0, true);
    uint64_t h = lfosync_render(&p, 0.5f, 2, AUDIO_FRAMES / 2);
    int32_t i;
    bool differ = false;
    for (i = 0; i < AUDIO_FRAMES / 2; i ++) {
        differ = differ || audio_l[i] != audio_r[i];
    }

    test_assert(differ);
    test_assert(h == lfosync_render_plain(&q, AUDIO_FRAMES / 2));
    test_assert(h != lfosync_render(&p, 0, 2, AUDIO_FRAMES / 2));
}

/** The random wave steps once per synced cycle: sync 1 at 120 bpm renders bit-identical to
 * the random wave at 2 Hz, and holds its value through each half second. */
void SynthLfoSync_random(void) {
    // audio/synth
    box_audio_synth_patch_t p = lfosync_patch(BOX_AUDIO_SYNTH_LFO_RANDOM, 9, 0, 1, false);
    box_audio_synth_patch_t q = lfosync_patch(BOX_AUDIO_SYNTH_LFO_RANDOM, 2, 0, 0, false);
    double lo, hi;
    uint64_t h = lfosync_render(&p, 0, 2, AUDIO_FRAMES);
    lo = lfosync_bin(audio_l, 2400, LFOSYNC_BEAT / 2, 0, NULL);
    hi = lfosync_bin(audio_l, 2400 + LFOSYNC_BEAT / 2, LFOSYNC_BEAT / 2 - 2400, 0, NULL);
    printf("random: level %f then %f in one cycle\n", lo, hi);
    test_assert(fabs(lo - hi) < 0.05 * (lo + hi) + 1e-6);
    test_assert(h == lfosync_render_plain(&q, AUDIO_FRAMES));
}

/** With sync 0 the beat and the tempo of a play change nothing: plays at any beat and tempo
 * render bit-identical to a plain play, mono and wide, with every wave and a random phase. */
void SynthLfoSync_zero_identical(void) {
    // audio/synth
    const uint32_t waves[] = { BOX_AUDIO_SYNTH_LFO_SINE, BOX_AUDIO_SYNTH_LFO_TRIANGLE,
        BOX_AUDIO_SYNTH_LFO_SQUARE, BOX_AUDIO_SYNTH_LFO_SAW, BOX_AUDIO_SYNTH_LFO_RANDOM };
    box_audio_synth_patch_t p;
    uint64_t h;
    int32_t i, wide;
    for (wide = 0; wide < 2; wide ++) {
        for (i = 0; i < 5; i ++) {
            p = lfosync_patch(waves[i], 3, 0.3f, 0, wide != 0);
            p.lfo[0].random_phase = i == 4;
            p.lfo[0].pitch = 30;
            h = lfosync_render_plain(&p, AUDIO_FRAMES / 4);
            test_assert(h == lfosync_render(&p, 0, 0, AUDIO_FRAMES / 4));
            test_assert(h == lfosync_render(&p, 3.7f, 5, AUDIO_FRAMES / 4));
            test_assert(h == lfosync_render(&p, 700.25f, 0.5f, AUDIO_FRAMES / 4));
            p.lfo[0].sync = -2;
            test_assert(h == lfosync_render(&p, 0.5f, 3, AUDIO_FRAMES / 4));
            p.lfo[0].sync = NAN;
            test_assert(h == lfosync_render(&p, 0.5f, 3, AUDIO_FRAMES / 4));
        }
    }
}

/** A tempo of 0, below 0 or NaN plays at 120 bpm, one out of 20..999 bpm at its end, and a
 * beat that is NaN, negative or past the wrap at beat 0. */
void SynthLfoSync_clamped(void) {
    // audio/synth
    box_audio_synth_patch_t p = lfosync_patch(BOX_AUDIO_SYNTH_LFO_SINE, 7, 0, 1, false);
    uint64_t h = lfosync_render(&p, 0, 2, AUDIO_FRAMES / 4);
    uint64_t lo, hi;
    test_assert(h == lfosync_render(&p, 0, 0, AUDIO_FRAMES / 4));
    test_assert(h == lfosync_render(&p, 0, -3, AUDIO_FRAMES / 4));
    test_assert(h == lfosync_render(&p, 0, NAN, AUDIO_FRAMES / 4));
    test_assert(h == lfosync_render(&p, NAN, 2, AUDIO_FRAMES / 4));
    test_assert(h == lfosync_render(&p, -0.5f, 2, AUDIO_FRAMES / 4));
    test_assert(h == lfosync_render(&p, 1e9f, 2, AUDIO_FRAMES / 4));
    lo = lfosync_render(&p, 0, 20.0f / 60.0f, AUDIO_FRAMES / 4);
    test_assert(lo == lfosync_render(&p, 0, 0.01f, AUDIO_FRAMES / 4));
    hi = lfosync_render(&p, 0, 999.0f / 60.0f, AUDIO_FRAMES / 4);
    test_assert(hi == lfosync_render(&p, 0, 1000, AUDIO_FRAMES / 4));
    test_assert(hi == lfosync_render(&p, 0, INFINITY, AUDIO_FRAMES / 4));
    test_assert(lo != h && hi != h);
}

/** A legato command that starts a new voice locks its LFOs to its beat and tempo like a
 * play. */
void SynthLfoSync_legato_new(void) {
    // audio/synth
    box_audio_synth_patch_t p = lfosync_patch(BOX_AUDIO_SYNTH_LFO_SINE, 7, 0, 1, false);
    box_audio_synth_patch_t q = lfosync_patch(BOX_AUDIO_SYNTH_LFO_SINE, 4, 0.5f, 0, false);
    box_audio_core_t *core = lfosync_core(&p);
    uint8_t bytes[LFOSYNC_BYTES];
    box_audio_writer_t w;
    uint64_t h;
    box_audio_writer_init(&w, bytes, sizeof(bytes));
    test_assert(box_audio_write_legato_note(&w, 2, 1, 1, 0, 1, 0, 1, 1, 2.5f, 4, 0, 0, 0));
    box_audio_core_apply(core, w.data, w.size);
    audio_render(core, 0, AUDIO_FRAMES / 2);
    h = lfosync_hash(AUDIO_FRAMES / 2);
    box_audio_core_free(core);
    test_assert(h == lfosync_render_plain(&q, AUDIO_FRAMES / 2));
}

/** Create a world with the null output and the sounds of test/assets/audio/synth_lfo_sync.flecs.
 *
 * @return The world
 */
static ecs_world_t* lfosync_world(void) {
    ecs_world_t *world = audio_world();
    char *code = audio_read("test/assets/audio/synth_lfo_sync.flecs");
    test_assert(ecs_script_run(world, "lfo_sync", code, NULL) == 0);
    ecs_os_free(code);
    audio_run(world, 1);

    return world;
}

/** Frame a note starts at: the frame before the first sample of the tap in a range that is
 * not 0, as the sine and the envelope of the sounds start at 0.
 *
 * @param from First frame
 * @param to End frame
 * @return The frame
 */
static int32_t lfosync_onset(
    int32_t from,
    int32_t to)
{
    int32_t i, result = -1;
    for (i = from; i < to && result < 0; i ++) {
        if (audio_tap_l[i] != 0.0f) {
            result = i - 1;
        }
    }

    test_assert(result >= 0);

    return result;
}

/** The sounds of the script compile their sync, the default is 0, and hard sync is apart. */
void SynthLfoSync_script(void) {
    // audio/synth
    ecs_world_t *world = lfosync_world();
    ecs_entity_t beat = ecs_lookup(world, "Beat"), eighth = ecs_lookup(world, "Eighth");
    ecs_entity_t unsynced = ecs_lookup(world, "Free"), e = ecs_new(world);
    const box_audio_synth_patch_t *p;
    test_assert(beat && eighth && unsynced);
    ecs_add(world, e, SynthLfo);
    test_flt(ecs_get(world, e, SynthLfo)->sync, 0);
    test_flt(ecs_get(world, e, SynthLfo)->hard_sync, 0);
    test_flt(ecs_get(world, beat, SynthLfo)->sync, 1);
    p = &ecs_get(world, beat, AudioSynth)->patch;
    test_int(p->lfos, 1);
    test_flt(p->lfo[0].sync, 1);
    test_flt(p->lfo[0].rate, 7);
    test_flt(p->lfo[0].hard_sync, 0);
    test_flt(ecs_get(world, eighth, AudioSynth)->patch.lfo[0].sync, 0.5f);
    test_flt(ecs_get(world, unsynced, AudioSynth)->patch.lfo[0].sync, 0);
    ecs_fini(world);
}

/** Two notes on the timeline at beats 1 and 4.5 at 120 bpm: under an LFO of sync 1 their
 * phases from their starts are half a cycle apart, under one of sync 0.5 the same.
 *
 * @param name Name of the instrument
 * @param hz Frequency of its LFO
 * @return Distance of the phases in turns
 */
static double lfosync_timeline(
    const char *name,
    double hz)
{
    ecs_world_t *world = lfosync_world();
    ecs_entity_t inst = ecs_lookup(world, name), clip, track, place;
    const Note notes[] = {
        { .start = 1, .duration = 2, .pitch = 69, .velocity = 1 },
        { .start = 4.5f, .duration = 2, .pitch = 69, .velocity = 1 } };
    NoteClip *c;
    ClipPlacement *pl;
    int32_t start, a, b;
    double pa, pb;
    test_assert(inst != 0);
    clip = ecs_new(world);
    c = ecs_ensure(world, clip, NoteClip);
    c->length = 8;
    ecs_vec_set_count_t(NULL, &c->notes, Note, 2);
    memcpy(ecs_vec_first(&c->notes), notes, sizeof(notes));
    ecs_modified(world, clip, NoteClip);
    track = ecs_new(world);
    ecs_set(world, track, Track, { .instrument = inst });
    place = ecs_new_w_pair(world, EcsChildOf, track);
    pl = ecs_ensure(world, place, ClipPlacement);
    pl->clip = clip;
    ecs_modified(world, place, ClipPlacement);
    audio_run(world, 1);

    start = audio_tap_count;
    ecs_singleton_ensure(world, Transport)->playing = true;
    ecs_singleton_modified(world, Transport);
    while (audio_tap_count < start + 7 * LFOSYNC_BEAT) {
        audio_run(world, 1);
    }

    a = lfosync_onset(start + LFOSYNC_BEAT / 2, start + 2 * LFOSYNC_BEAT);
    b = lfosync_onset(start + 4 * LFOSYNC_BEAT, start + 5 * LFOSYNC_BEAT);
    printf("%s: notes at frames %d and %d, %d apart\n", name, a - start, b - start, b - a);
    test_assert(abs(b - a - 7 * LFOSYNC_BEAT / 2) < 2);
    lfosync_bin(audio_tap_l, a + 1200, LFOSYNC_BEAT, hz, &pa);
    lfosync_bin(audio_tap_l, b + 1200, LFOSYNC_BEAT, hz, &pb);
    printf("%s: phases %f and %f\n", name, pa, pb);
    ecs_fini(world);

    return lfosync_diff(pa, pb);
}

/** Notes half a beat apart on the timeline see LFO phases half a cycle apart with sync 1 and
 * the same phase with sync 0.5. */
void SynthLfoSync_timeline(void) {
    // audio/synth
    test_assert(lfosync_diff(lfosync_timeline("beat_lead", 2), 0.5) < LFOSYNC_TURNS);
    test_assert(lfosync_timeline("eighth_lead", 4) < LFOSYNC_TURNS);
}

/** Without a Transport a synced LFO runs at 120 bpm from beat 0: sync 1 is 2 Hz, at the phase
 * of the same LFO at a rate of 2 Hz. */
void SynthLfoSync_no_transport(void) {
    // audio/synth
    ecs_world_t *world = lfosync_world();
    ecs_entity_t beat = ecs_lookup(world, "beat_lead");
    ecs_entity_t unsynced = ecs_lookup(world, "free_lead");
    double one, two, pa, pb;
    int32_t at, a, b;
    ecs_singleton_remove(world, Transport);
    audio_run(world, 1);
    test_assert(ecs_singleton_get(world, Transport) == NULL);

    at = audio_tap_count;
    test_assert(box_audio_play_note(world, beat, 69, 1, 3));
    audio_run(world, 100);
    a = lfosync_onset(at, audio_tap_count);
    test_assert(a + 1200 + LFOSYNC_BEAT * 2 < audio_tap_count);
    one = lfosync_bin(audio_tap_l, a + 1200, LFOSYNC_BEAT * 2, 1, NULL);
    two = lfosync_bin(audio_tap_l, a + 1200, LFOSYNC_BEAT * 2, 2, &pa);
    printf("no transport: %f at 1 Hz, %f at 2 Hz\n", one, two);
    test_assert(one * 3 < two);

    at = audio_tap_count;
    test_assert(box_audio_play_note(world, unsynced, 69, 1, 3));
    audio_run(world, 100);
    b = lfosync_onset(at, audio_tap_count);
    lfosync_bin(audio_tap_l, b + 1200, LFOSYNC_BEAT * 2, 2, &pb);
    printf("no transport: phase %f synced, %f at 2 Hz\n", pa, pb);
    test_assert(lfosync_diff(pa, pb) < LFOSYNC_TURNS);
    ecs_fini(world);
}

/** A tempo change applies to the next note: a voice that started at 120 bpm keeps 2 Hz after
 * the Transport moves to 60 bpm, and the next note runs at 1 Hz. */
void SynthLfoSync_tempo_next_note(void) {
    // audio/synth
    ecs_world_t *world = lfosync_world();
    ecs_entity_t beat = ecs_lookup(world, "beat_lead");
    double one, two;
    int32_t at, a, b;
    test_assert(beat != 0);
    at = audio_tap_count;
    test_assert(box_audio_play_note(world, beat, 69, 1, 4));
    audio_run(world, 15);
    ecs_singleton_ensure(world, Transport)->tempo = 60;
    ecs_singleton_modified(world, Transport);
    audio_run(world, 120);
    a = lfosync_onset(at, audio_tap_count);
    test_assert(a + 2 * AUDIO_RATE < audio_tap_count);
    one = lfosync_bin(audio_tap_l, a + AUDIO_RATE / 2, AUDIO_RATE, 1, NULL);
    two = lfosync_bin(audio_tap_l, a + AUDIO_RATE / 2, AUDIO_RATE, 2, NULL);
    printf("held through the change: %f at 1 Hz, %f at 2 Hz\n", one, two);
    test_assert(one * 3 < two);

    at = audio_tap_count;
    test_assert(box_audio_play_note(world, beat, 69, 1, 2));
    audio_run(world, 150);
    b = lfosync_onset(at, audio_tap_count);
    test_assert(b + AUDIO_RATE / 4 + AUDIO_RATE < audio_tap_count);
    one = lfosync_bin(audio_tap_l, b + AUDIO_RATE / 4, AUDIO_RATE, 1, NULL);
    two = lfosync_bin(audio_tap_l, b + AUDIO_RATE / 4, AUDIO_RATE, 2, NULL);
    printf("next note at 60 bpm: %f at 1 Hz, %f at 2 Hz\n", one, two);
    test_assert(two * 3 < one);
    ecs_fini(world);
}

/** A note while the Transport is stopped takes its position: at position 0.5 an LFO of sync 1
 * starts half a cycle from one at position 0. */
void SynthLfoSync_stopped_position(void) {
    // audio/synth
    ecs_world_t *world = lfosync_world();
    ecs_entity_t beat = ecs_lookup(world, "beat_lead");
    double pa, pb;
    int32_t at, a, b;
    at = audio_tap_count;
    test_assert(box_audio_play_note(world, beat, 69, 1, 2));
    audio_run(world, 75);
    a = lfosync_onset(at, audio_tap_count);
    lfosync_bin(audio_tap_l, a + 1200, LFOSYNC_BEAT, 2, &pa);

    ecs_singleton_ensure(world, Transport)->position = 0.5;
    ecs_singleton_modified(world, Transport);
    audio_run(world, 1);
    at = audio_tap_count;
    test_assert(box_audio_play_note(world, beat, 69, 1, 2));
    audio_run(world, 75);
    b = lfosync_onset(at, audio_tap_count);
    lfosync_bin(audio_tap_l, b + 1200, LFOSYNC_BEAT, 2, &pb);
    printf("stopped: phase %f at position 0, %f at 0.5\n", pa, pb);
    test_assert(lfosync_diff(lfosync_diff(pa, pb), 0.5) < LFOSYNC_TURNS);
    ecs_fini(world);
}

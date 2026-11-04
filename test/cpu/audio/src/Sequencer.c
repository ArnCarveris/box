/**
 * @file Sequencer.c
 * @brief Note clips and tracks through the null transport at a fixed delta: the frames of the
 * starts and the releases of the notes, the transpose, gain and pan of a placement, loops,
 * offsets and durations, the loop of the transport, stops, seeks, tempo changes, edits and
 * deletes while playing, the one shots a stop leaves alone, a script and identical captures.
 *
 * Most cases play a sampler of a constant level with a velocity curve of 1 and the shortest
 * release: a note is exactly its level from its start frame, and its release is the first
 * frame below it.
 */

#include "world.h"
#include <stdio.h>
#include <stdlib.h>

/** Level of the sampler of the tests */
#define SEQ_LEVEL 0.5f

/** Frames of a beat at 120 bpm */
#define SEQ_BEAT 24000

/** Frames of the fade of a release of the sampler, 5 ms */
#define SEQ_FADE 240

/** Get the frames of a beat at a tempo at AUDIO_RATE.
 *
 * @param tempo Quarter notes per minute
 * @return Frames
 */
static double seq_fpb(
    double tempo)
{
    return (double)AUDIO_RATE * 60.0 / tempo;
}

/** Create a sampler instrument of a constant level at the root 60, with a velocity curve of 1
 * and a release of 5 ms.
 *
 * @param world The world
 * @param level The level of its sound
 * @return The instrument
 */
static ecs_entity_t seq_sampler(
    ecs_world_t *world,
    float level)
{
    ecs_entity_t sound = audio_dc(world, NULL, level, AUDIO_RATE * 6);
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Instrument, { .sound = sound, .root = 60, .velocity_curve = 1,
        .gate = true, .release = 0.005f });

    return e;
}

/** Create a clip.
 *
 * @param world The world
 * @param length Beats of the clip
 * @param notes The notes
 * @param count Number of notes
 * @return The clip
 */
static ecs_entity_t seq_clip(
    ecs_world_t *world,
    float length,
    const Note *notes,
    int32_t count)
{
    ecs_entity_t e = ecs_new(world);
    NoteClip *c = ecs_ensure(world, e, NoteClip);
    c->length = length;
    ecs_vec_set_count_t(NULL, &c->notes, Note, count);
    if (count) {
        memcpy(ecs_vec_first(&c->notes), notes, sizeof(Note) * (size_t)count);
    }

    ecs_modified(world, e, NoteClip);

    return e;
}

/** Create a track.
 *
 * @param world The world
 * @param instrument The instrument
 * @return The track
 */
static ecs_entity_t seq_track(
    ecs_world_t *world,
    ecs_entity_t instrument)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, Track, { .instrument = instrument });

    return e;
}

/** Place a clip on a track at unity gain without a transpose.
 *
 * @param world The world
 * @param track The track
 * @param clip The clip
 * @param start Beat on the timeline
 * @param duration Beats, 0 for the clip
 * @param loop Whether it loops
 * @param offset Beats into the clip
 * @return The placement
 */
static ecs_entity_t seq_place(
    ecs_world_t *world,
    ecs_entity_t track,
    ecs_entity_t clip,
    double start,
    double duration,
    bool loop,
    double offset)
{
    ecs_entity_t e = ecs_new_w_pair(world, EcsChildOf, track);
    ClipPlacement *p = ecs_ensure(world, e, ClipPlacement);
    p->clip = clip;
    p->start = start;
    p->duration = duration;
    p->loop = loop;
    p->offset = offset;
    ecs_modified(world, e, ClipPlacement);

    return e;
}

/** Set the tempo of the transport.
 *
 * @param world The world
 * @param tempo Quarter notes per minute
 */
static void seq_tempo(
    ecs_world_t *world,
    float tempo)
{
    ecs_singleton_ensure(world, Transport)->tempo = tempo;
    ecs_singleton_modified(world, Transport);
}

/** Start or stop the transport.
 *
 * @param world The world
 * @param playing Whether it plays
 * @return The frame at which it starts, the clock of the next frame
 */
static int64_t seq_play(
    ecs_world_t *world,
    bool playing)
{
    ecs_singleton_ensure(world, Transport)->playing = playing;
    ecs_singleton_modified(world, Transport);

    return audio_tap_count;
}

/** Run frames until the tap holds a frame.
 *
 * @param world The world
 * @param frame The frame
 */
static void seq_run_to(
    ecs_world_t *world,
    int64_t frame)
{
    while (audio_tap_count <= frame + AUDIO_FRAME) {
        audio_run(world, 1);
    }
}

/** First frame of the tap in a range that is not 0.
 *
 * @param from First frame
 * @param to End frame
 * @return The frame, -1 when the range is silent
 */
static int64_t seq_onset(
    int64_t from,
    int64_t to)
{
    int64_t i, result = -1;
    for (i = from; i < to && result < 0; i ++) {
        if (audio_tap_l[i] != 0.0f || audio_tap_r[i] != 0.0f) {
            result = i;
        }
    }

    return result;
}

/** First frame of the left channel of the tap in a range that is not a level.
 *
 * @param from First frame
 * @param to End frame
 * @param level The level
 * @return The frame, -1 when the whole range is at the level
 */
static int64_t seq_edge(
    int64_t from,
    int64_t to,
    float level)
{
    int64_t i, result = -1;
    for (i = from; i < to && result < 0; i ++) {
        if (fabsf(audio_tap_l[i] - level) > 1e-6f) {
            result = i;
        }
    }

    return result;
}

/** Check a note of the sampler: silent just before its start frame, at a level from it to its
 * release frame, then fading out.
 *
 * @param start Frame of the start
 * @param release Frame of the release
 * @param level Its level
 */
static void seq_note(
    int64_t start,
    int64_t release,
    float level)
{
    test_assert(release + SEQ_FADE < audio_tap_count);
    test_flt(audio_tap_l[start - 1], 0.0f);
    test_flt(audio_tap_l[start], level);
    test_int(seq_edge(start, release + 1, level), release);
    test_flt(audio_tap_l[release + SEQ_FADE], 0.0f);
}

/** The defaults: a note at velocity 1, a clip of 4 beats, a placement at unity gain, and a
 * track is a mixer channel. */
void Sequencer_defaults(void) {
    // audio/sequencer
    ecs_world_t *world = audio_world();
    ecs_entity_t e = ecs_new(world);
    const NoteClip *clip;
    const ClipPlacement *p;
    ecs_add(world, e, NoteClip);
    clip = ecs_get(world, e, NoteClip);
    test_flt(clip->length, 4.0f);
    test_int(ecs_vec_count(&clip->notes), 0);
    ecs_add(world, e, ClipPlacement);
    p = ecs_get(world, e, ClipPlacement);
    test_flt(p->gain, 1.0f);
    test_flt(p->transpose, 0.0f);
    test_flt(p->duration, 0.0);
    test_bool(p->loop, false);
    ecs_add(world, e, Track);
    test_assert(ecs_has(world, e, AudioBus));
    test_flt(ecs_get(world, e, AudioBus)->volume, 1.0f);
    test_assert(ecs_script_run(world, "note",
        "using box.audio\n"
        "clip { NoteClip: {notes: [{start: 1, duration: 2, pitch: 64}]} }\n", NULL) == 0);
    clip = ecs_get(world, ecs_lookup(world, "clip"), NoteClip);
    test_assert(clip != NULL);
    test_int(ecs_vec_count(&clip->notes), 1);
    test_flt(ecs_vec_first_t(&clip->notes, Note)->velocity, 1.0f);
    test_flt(ecs_vec_first_t(&clip->notes, Note)->pitch, 64.0f);
    test_flt(clip->length, 4.0f);
    ecs_fini(world);
}

/** Notes start on the frame of their beat at a tempo. */
static void seq_starts(
    float tempo)
{
    ecs_world_t *world = audio_world();
    ecs_entity_t inst = seq_sampler(world, SEQ_LEVEL);
    Note notes[] = {
        { .start = 0, .duration = 0.25f, .pitch = 60, .velocity = 1 },
        { .start = 1, .duration = 0.25f, .pitch = 60, .velocity = 1 },
        { .start = 2.5f, .duration = 0.25f, .pitch = 60, .velocity = 1 }
    };
    ecs_entity_t clip = seq_clip(world, 4, notes, 3);
    ecs_entity_t track = seq_track(world, inst);
    double fpb = seq_fpb(tempo);
    int64_t c0, f;
    int32_t i;
    seq_place(world, track, clip, 0, 0, false, 0);
    seq_tempo(world, tempo);
    audio_run(world, 2);
    c0 = seq_play(world, true);
    seq_run_to(world, c0 + llround(3.0 * fpb));
    for (i = 0; i < 3; i ++) {
        f = c0 + llround((double)notes[i].start * fpb);
        if (i) {
            test_int(seq_onset(f - 1000, f), -1);
        }

        seq_note(f, c0 + llround(((double)notes[i].start + 0.25) * fpb), SEQ_LEVEL);
    }

    test_int(box_audio_module_get(world)->sequencer.stats.notes, 3);
    ecs_fini(world);
}

/** Notes start on their exact frames at 120 and at 97 bpm. */
void Sequencer_start_frames(void) {
    // audio/sequencer
    seq_starts(120.0f);
    seq_starts(97.0f);
}

/** The duration of a note ends it with a release on its frame. */
void Sequencer_release_frames(void) {
    // audio/sequencer
    ecs_world_t *world = audio_world();
    ecs_entity_t inst = seq_sampler(world, SEQ_LEVEL);
    Note notes[] = {
        { .start = 0, .duration = 0.5f, .pitch = 60, .velocity = 1 },
        { .start = 1, .duration = 1.25f, .pitch = 60, .velocity = 1 }
    };
    ecs_entity_t clip = seq_clip(world, 4, notes, 2);
    ecs_entity_t track = seq_track(world, inst);
    int64_t c0;
    seq_place(world, track, clip, 0, 0, false, 0);
    audio_run(world, 2);
    c0 = seq_play(world, true);
    seq_run_to(world, c0 + 3 * SEQ_BEAT);
    seq_note(c0, c0 + SEQ_BEAT / 2, SEQ_LEVEL);
    seq_note(c0 + SEQ_BEAT, c0 + SEQ_BEAT * 9 / 4, SEQ_LEVEL);
    test_int(box_audio_module_get(world)->sequencer.stats.releases, 2);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);
    ecs_fini(world);
}

/** The gain of a placement multiplies the gain of the velocity, the pan of a note pans its
 * voice and the transpose moves the pitch: a sampler of 440 Hz at its root an octave up plays
 * 880 Hz. */
void Sequencer_transpose_gain_pan(void) {
    // audio/sequencer
    ecs_world_t *world = audio_world();
    ecs_entity_t inst = seq_sampler(world, SEQ_LEVEL);
    ecs_entity_t tone = audio_tone(world, NULL, 440, 0.5f, AUDIO_RATE * 4);
    ecs_entity_t sine = ecs_new(world);
    Note left[] = {{ .start = 0, .duration = 1, .pitch = 60, .velocity = 0.5f, .pan = 1 }};
    Note a4[] = {{ .start = 0, .duration = 1, .pitch = 69, .velocity = 1 }};
    ecs_entity_t clip = seq_clip(world, 1, left, 1);
    ecs_entity_t clip_a4 = seq_clip(world, 1, a4, 1);
    ecs_entity_t track = seq_track(world, inst);
    ecs_entity_t track_a4, p, q;
    int64_t c0;
    double f;
    ecs_set(world, sine, Instrument, { .sound = tone, .root = 69, .velocity_curve = 1,
        .gate = true, .release = 0.005f });
    track_a4 = seq_track(world, sine);
    p = seq_place(world, track, clip, 0, 0, false, 0);
    ecs_get_mut(world, p, ClipPlacement)->gain = 0.5f;
    ecs_modified(world, p, ClipPlacement);
    q = seq_place(world, track_a4, clip_a4, 2, 0, false, 0);
    ecs_get_mut(world, q, ClipPlacement)->transpose = 12;
    ecs_modified(world, q, ClipPlacement);
    audio_run(world, 2);
    c0 = seq_play(world, true);
    seq_run_to(world, c0 + 3 * SEQ_BEAT);
    test_flt(audio_tap_l[c0 + 1000], 0.0f);
    test_assert(fabsf(audio_tap_r[c0 + 1000] - SEQ_LEVEL * 0.25f * 1.41421356f) < 1e-4f);
    f = audio_frequency(audio_tap_l, (int32_t)(c0 + 2 * SEQ_BEAT + 2400),
        (int32_t)(c0 + 3 * SEQ_BEAT - 2400));
    printf("transposed: %f Hz\n", f);
    test_assert(fabs(f - 880) < 1);
    ecs_fini(world);
}

/** A looping placement repeats its clip every length beats from its offset: a clip of 2 beats
 * with notes at 0 and 1, from an offset of 1 at beat 4 for 6 beats, plays on every beat from 4
 * to 9. */
void Sequencer_loop_offset(void) {
    // audio/sequencer
    ecs_world_t *world = audio_world();
    ecs_entity_t inst = seq_sampler(world, SEQ_LEVEL);
    Note notes[] = {
        { .start = 0, .duration = 0.25f, .pitch = 60, .velocity = 1 },
        { .start = 1, .duration = 0.25f, .pitch = 60, .velocity = 1 }
    };
    ecs_entity_t clip = seq_clip(world, 2, notes, 2);
    ecs_entity_t track = seq_track(world, inst);
    int64_t c0, f;
    int32_t b;
    seq_place(world, track, clip, 4, 6, true, 1);
    audio_run(world, 2);
    c0 = seq_play(world, true);
    seq_run_to(world, c0 + 11 * SEQ_BEAT);
    test_int(seq_onset(c0, c0 + 4 * SEQ_BEAT), -1);
    for (b = 4; b < 10; b ++) {
        f = c0 + b * SEQ_BEAT;
        test_int(seq_onset(f - SEQ_BEAT / 2, f), -1);
        seq_note(f, f + SEQ_BEAT / 4, SEQ_LEVEL);
    }

    test_int(seq_onset(c0 + 9 * SEQ_BEAT + SEQ_BEAT / 2, c0 + 11 * SEQ_BEAT), -1);
    test_int(box_audio_module_get(world)->sequencer.stats.notes, 6);
    ecs_fini(world);
}

/** A placement shorter than its clip cuts it: a note that lasts past the end of the placement
 * releases there and a later note does not play. A duration of 0 is the length of the clip
 * minus the offset. */
void Sequencer_duration_cuts(void) {
    // audio/sequencer
    ecs_world_t *world = audio_world();
    ecs_entity_t inst = seq_sampler(world, SEQ_LEVEL);
    Note notes[] = {
        { .start = 0, .duration = 3, .pitch = 60, .velocity = 1 },
        { .start = 3, .duration = 0.5f, .pitch = 60, .velocity = 1 }
    };
    Note short_notes[] = {
        { .start = 0, .duration = 0.5f, .pitch = 60, .velocity = 1 },
        { .start = 1, .duration = 2, .pitch = 60, .velocity = 1 }
    };
    ecs_entity_t clip = seq_clip(world, 4, notes, 2);
    ecs_entity_t clip_short = seq_clip(world, 2, short_notes, 2);
    ecs_entity_t track = seq_track(world, inst);
    int64_t c0;
    seq_place(world, track, clip, 0, 2, false, 0);
    seq_place(world, track, clip_short, 6, 0, false, 0.5);
    audio_run(world, 2);
    c0 = seq_play(world, true);
    seq_run_to(world, c0 + 8 * SEQ_BEAT);
    seq_note(c0, c0 + 2 * SEQ_BEAT, SEQ_LEVEL);
    test_int(seq_onset(c0 + 2 * SEQ_BEAT + SEQ_FADE, c0 + 6 * SEQ_BEAT + SEQ_BEAT / 2), -1);
    seq_note(c0 + 6 * SEQ_BEAT + SEQ_BEAT / 2, c0 + 7 * SEQ_BEAT + SEQ_BEAT / 2, SEQ_LEVEL);
    test_int(seq_onset(c0 + 7 * SEQ_BEAT + SEQ_BEAT / 2 + SEQ_FADE, c0 + 8 * SEQ_BEAT), -1);
    test_int(box_audio_module_get(world)->sequencer.stats.notes, 2);
    ecs_fini(world);
}

/** Two placements of one clip play it twice, and a third on another track a third time. */
void Sequencer_two_placements(void) {
    // audio/sequencer
    ecs_world_t *world = audio_world();
    ecs_entity_t inst = seq_sampler(world, SEQ_LEVEL);
    Note notes[] = {{ .start = 0, .duration = 0.5f, .pitch = 60, .velocity = 1 }};
    ecs_entity_t clip = seq_clip(world, 1, notes, 1);
    ecs_entity_t track = seq_track(world, inst);
    ecs_entity_t other = seq_track(world, inst);
    int64_t c0;
    seq_place(world, track, clip, 0, 0, false, 0);
    seq_place(world, track, clip, 2, 0, false, 0);
    seq_place(world, other, clip, 3, 0, false, 0);
    audio_run(world, 2);
    c0 = seq_play(world, true);
    seq_run_to(world, c0 + 4 * SEQ_BEAT);
    seq_note(c0, c0 + SEQ_BEAT / 2, SEQ_LEVEL);
    seq_note(c0 + 2 * SEQ_BEAT, c0 + 2 * SEQ_BEAT + SEQ_BEAT / 2, SEQ_LEVEL);
    seq_note(c0 + 3 * SEQ_BEAT, c0 + 3 * SEQ_BEAT + SEQ_BEAT / 2, SEQ_LEVEL);
    test_int(box_audio_module_get(world)->sequencer.stats.notes, 3);
    ecs_fini(world);
}

/** A loop of the transport from beat 0 to 2 wraps the notes on its exact frame, and a note that
 * lasts past loop_end releases there. */
void Sequencer_transport_loop(void) {
    // audio/sequencer
    ecs_world_t *world = audio_world();
    ecs_entity_t inst = seq_sampler(world, SEQ_LEVEL);
    Note notes[] = {{ .start = 1.5f, .duration = 1, .pitch = 60, .velocity = 1 }};
    ecs_entity_t clip = seq_clip(world, 4, notes, 1);
    ecs_entity_t track = seq_track(world, inst);
    Transport *tr;
    int64_t c0;
    int32_t pass;
    seq_place(world, track, clip, 0, 0, false, 0);
    tr = ecs_singleton_ensure(world, Transport);
    tr->loop = true;
    tr->loop_start = 0;
    tr->loop_end = 2;
    ecs_singleton_modified(world, Transport);
    audio_run(world, 2);
    c0 = seq_play(world, true);
    seq_run_to(world, c0 + 7 * SEQ_BEAT);
    for (pass = 0; pass < 3; pass ++) {
        int64_t at = c0 + pass * 2 * SEQ_BEAT;
        test_int(seq_onset(at + SEQ_FADE, at + SEQ_BEAT * 3 / 2), -1);
        seq_note(at + SEQ_BEAT * 3 / 2, at + 2 * SEQ_BEAT, SEQ_LEVEL);
    }

    test_int(box_audio_module_get(world)->sequencer.stats.notes, 3);
    ecs_fini(world);
}

/** A stop releases the notes that sound at once and cancels the ones scheduled that have not
 * started. */
void Sequencer_stop(void) {
    // audio/sequencer
    ecs_world_t *world = audio_world();
    box_audio_module_t *state = box_audio_module_get(world);
    ecs_entity_t inst = seq_sampler(world, SEQ_LEVEL);
    Note notes[] = {
        { .start = 0, .duration = 4, .pitch = 60, .velocity = 1 },
        { .start = 0.25f, .duration = 1, .pitch = 60, .velocity = 1 }
    };
    ecs_entity_t clip = seq_clip(world, 4, notes, 2);
    ecs_entity_t track = seq_track(world, inst);
    int64_t c0;
    seq_place(world, track, clip, 0, 0, false, 0);
    audio_run(world, 2);
    c0 = seq_play(world, true);
    audio_run(world, 1);
    test_int(state->sequencer.stats.notes, 2);
    test_int(ecs_vec_count(&state->sequencer.sounding), 2);
    seq_play(world, false);
    audio_run(world, 1);
    test_int(state->timeline.stats.cancels, 1);
    test_int(state->sequencer.stats.releases, 1);
    test_int(ecs_vec_count(&state->sequencer.sounding), 0);
    audio_run(world, 40);
    test_flt(audio_tap_l[c0 + 700], SEQ_LEVEL);
    test_int(seq_onset(c0 + 1600, audio_tap_count), -1);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);
    test_int(state->sequencer.stats.notes, 2);
    ecs_fini(world);
}

/** A seek releases the notes that sound and goes on at the new position: a note that started
 * before it does not start in its middle, the next one starts on its frame. */
void Sequencer_seek(void) {
    // audio/sequencer
    ecs_world_t *world = audio_world();
    box_audio_module_t *state = box_audio_module_get(world);
    ecs_entity_t inst = seq_sampler(world, SEQ_LEVEL);
    Note notes[] = {
        { .start = 0, .duration = 4, .pitch = 60, .velocity = 1 },
        { .start = 2, .duration = 0.5f, .pitch = 60, .velocity = 1 }
    };
    ecs_entity_t clip = seq_clip(world, 4, notes, 2);
    ecs_entity_t track = seq_track(world, inst);
    int64_t c0, c1;
    seq_place(world, track, clip, 0, 0, false, 0);
    audio_run(world, 2);
    c0 = seq_play(world, true);
    audio_run(world, 10);
    test_flt(audio_tap_l[c0 + 4000], SEQ_LEVEL);
    ecs_singleton_ensure(world, Transport)->position = 1;
    ecs_singleton_modified(world, Transport);
    c1 = audio_tap_count;
    seq_run_to(world, c1 + 2 * SEQ_BEAT);
    test_int(seq_onset(c1 + 1000, c1 + SEQ_BEAT), -1);
    seq_note(c1 + SEQ_BEAT, c1 + SEQ_BEAT * 3 / 2, SEQ_LEVEL);
    test_int(state->sequencer.stats.notes, 2);
    ecs_fini(world);
}

/** A tempo change in the middle of a note moves its release: the end in beats is mapped to a
 * frame when it falls in a window, at the tempo of then. */
void Sequencer_tempo_moves_release(void) {
    // audio/sequencer
    ecs_world_t *world = audio_world();
    ecs_entity_t inst = seq_sampler(world, SEQ_LEVEL);
    Note notes[] = {{ .start = 0, .duration = 2, .pitch = 60, .velocity = 1 }};
    ecs_entity_t clip = seq_clip(world, 4, notes, 1);
    ecs_entity_t track = seq_track(world, inst);
    int64_t c0, end;
    seq_place(world, track, clip, 0, 0, false, 0);
    audio_run(world, 2);
    c0 = seq_play(world, true);
    audio_run(world, 20);
    seq_tempo(world, 60);
    audio_run(world, 1);
    end = box_audio_timeline_frame(world, 2.0);
    printf("release at 120: %lld, after the change to 60: %lld\n",
        (long long)(c0 + 2 * SEQ_BEAT), (long long)end);
    test_assert(end > c0 + 2 * SEQ_BEAT + SEQ_BEAT / 2);
    seq_run_to(world, end + SEQ_FADE);
    seq_note(c0, end, SEQ_LEVEL);
    ecs_fini(world);
}

/** An edit of a clip while it plays changes only the notes not scheduled yet. */
void Sequencer_edit_clip(void) {
    // audio/sequencer
    ecs_world_t *world = audio_world();
    ecs_entity_t inst = seq_sampler(world, SEQ_LEVEL);
    Note notes[] = {
        { .start = 0, .duration = 0.25f, .pitch = 60, .velocity = 1 },
        { .start = 1, .duration = 0.25f, .pitch = 60, .velocity = 1 },
        { .start = 2, .duration = 0.25f, .pitch = 60, .velocity = 1 },
        { .start = 3, .duration = 0.25f, .pitch = 60, .velocity = 1 }
    };
    ecs_entity_t clip = seq_clip(world, 4, notes, 4);
    ecs_entity_t track = seq_track(world, inst);
    NoteClip *c;
    Note *n;
    int64_t c0;
    int32_t i;
    seq_place(world, track, clip, 0, 0, false, 0);
    audio_run(world, 2);
    c0 = seq_play(world, true);
    audio_run(world, 15);
    c = ecs_get_mut(world, clip, NoteClip);
    n = ecs_vec_first(&c->notes);
    for (i = 0; i < ecs_vec_count(&c->notes); i ++) {
        n[i].velocity = 0.5f;
    }

    ecs_modified(world, clip, NoteClip);
    seq_run_to(world, c0 + 4 * SEQ_BEAT);
    seq_note(c0, c0 + SEQ_BEAT / 4, SEQ_LEVEL);
    for (i = 1; i < 4; i ++) {
        seq_note(c0 + i * SEQ_BEAT, c0 + i * SEQ_BEAT + SEQ_BEAT / 4, SEQ_LEVEL * 0.5f);
    }

    ecs_fini(world);
}

/** Deleting a track releases its notes and schedules nothing more for it. */
void Sequencer_delete_track(void) {
    // audio/sequencer
    ecs_world_t *world = audio_world();
    box_audio_module_t *state = box_audio_module_get(world);
    ecs_entity_t inst = seq_sampler(world, SEQ_LEVEL);
    Note notes[] = {
        { .start = 0, .duration = 4, .pitch = 60, .velocity = 1 },
        { .start = 2, .duration = 1, .pitch = 60, .velocity = 1 }
    };
    ecs_entity_t clip = seq_clip(world, 4, notes, 2);
    ecs_entity_t track = seq_track(world, inst);
    int64_t c0, c1;
    seq_place(world, track, clip, 0, 0, false, 0);
    audio_run(world, 2);
    c0 = seq_play(world, true);
    audio_run(world, 30);
    test_flt(audio_tap_l[audio_tap_count - 1], SEQ_LEVEL);
    ecs_delete(world, track);
    c1 = audio_tap_count;
    seq_run_to(world, c0 + 4 * SEQ_BEAT);
    test_int(seq_onset(c1 + 1000, audio_tap_count), -1);
    test_int(state->sequencer.stats.notes, 1);
    test_int(ecs_vec_count(&state->sequencer.sounding), 0);
    ecs_fini(world);
}

/** Disabling a placement releases its notes; enabled again it plays the notes that start
 * after. */
void Sequencer_disable_placement(void) {
    // audio/sequencer
    ecs_world_t *world = audio_world();
    ecs_entity_t inst = seq_sampler(world, SEQ_LEVEL);
    Note notes[] = {
        { .start = 0, .duration = 2, .pitch = 60, .velocity = 1 },
        { .start = 3, .duration = 0.5f, .pitch = 60, .velocity = 1 }
    };
    ecs_entity_t clip = seq_clip(world, 4, notes, 2);
    ecs_entity_t track = seq_track(world, inst);
    ecs_entity_t p;
    int64_t c0, c1;
    p = seq_place(world, track, clip, 0, 0, false, 0);
    audio_run(world, 2);
    c0 = seq_play(world, true);
    audio_run(world, 20);
    ecs_enable(world, p, false);
    c1 = audio_tap_count;
    audio_run(world, 20);
    ecs_enable(world, p, true);
    seq_run_to(world, c0 + 4 * SEQ_BEAT);
    test_int(seq_onset(c1 + 1000, c0 + 3 * SEQ_BEAT), -1);
    seq_note(c0 + 3 * SEQ_BEAT, c0 + 3 * SEQ_BEAT + SEQ_BEAT / 2, SEQ_LEVEL);
    ecs_fini(world);
}

/** The transport only drives tracks: a stop leaves a looping AudioSource, a box.audio.play and
 * a box.audio.play_note playing. */
void Sequencer_oneshots_survive_stop(void) {
    // audio/sequencer
    ecs_world_t *world = audio_world();
    ecs_entity_t inst = seq_sampler(world, 0.5f);
    ecs_entity_t loop = audio_dc(world, NULL, 0.1f, AUDIO_RATE);
    ecs_entity_t shot = audio_dc(world, NULL, 0.05f, AUDIO_RATE * 4);
    Note notes[] = {{ .start = 0, .duration = 8, .pitch = 60, .velocity = 0.2f }};
    ecs_entity_t clip = seq_clip(world, 8, notes, 1);
    ecs_entity_t track = seq_track(world, inst);
    seq_place(world, track, clip, 0, 0, false, 0);
    audio_source(world, loop, 1, true, 0);
    audio_run(world, 2);
    seq_play(world, true);
    test_assert(box_audio_play(world, shot, 1));
    test_assert(box_audio_play_note(world, inst, 60, 0.2f, 16));
    audio_run(world, 10);
    test_assert(fabsf(audio_level(AUDIO_FRAME) - 0.35f) < 1e-4f);
    seq_play(world, false);
    audio_run(world, 10);
    test_assert(fabsf(audio_level(AUDIO_FRAME) - 0.25f) < 1e-4f);
    ecs_fini(world);
}

/** A track whose instrument cannot play drops its notes and logs one warning. */
void Sequencer_not_ready(void) {
    // audio/sequencer
    ecs_world_t *world = audio_world();
    ecs_entity_t none = ecs_new(world);
    Note notes[] = {
        { .start = 0, .duration = 0.25f, .pitch = 60, .velocity = 1 },
        { .start = 1, .duration = 0.25f, .pitch = 60, .velocity = 1 }
    };
    ecs_entity_t clip = seq_clip(world, 2, notes, 2);
    ecs_entity_t track = seq_track(world, none);
    seq_place(world, track, clip, 0, 8, true, 0);
    audio_run(world, 2);
    audio_log_start();
    seq_play(world, true);
    audio_run(world, 120);
    audio_log_stop();
    test_int(audio_log_warnings, 1);
    test_int(box_audio_module_get(world)->sequencer.stats.notes, 0);
    test_int(seq_onset(0, audio_tap_count), -1);
    ecs_fini(world);
}

/** A transport that does not play costs nothing, and one that plays visits only the
 * placements that overlap its window. */
void Sequencer_cost(void) {
    // audio/sequencer
    ecs_world_t *world = audio_world();
    box_audio_module_t *state = box_audio_module_get(world);
    ecs_entity_t inst = seq_sampler(world, SEQ_LEVEL);
    Note notes[] = {{ .start = 0, .duration = 0.25f, .pitch = 60, .velocity = 0.1f }};
    ecs_entity_t clip = seq_clip(world, 1, notes, 1);
    ecs_entity_t track = seq_track(world, inst);
    int64_t commands;
    int32_t i;
    for (i = 0; i < 64; i ++) {
        seq_place(world, track, clip, i, 0, false, 0);
    }

    audio_run(world, 4);
    commands = box_audio_module_stats(world).commands;
    audio_run(world, 10);
    test_int(box_audio_module_stats(world).commands, commands);
    test_int(state->sequencer.stats.visits, 0);
    seq_play(world, true);
    audio_run(world, 60);
    printf("visits in 60 frames: %lld\n", (long long)state->sequencer.stats.visits);
    test_assert(state->sequencer.stats.visits > 0);
    test_assert(state->sequencer.stats.visits <= 2 * 61);
    test_int(state->sequencer.stats.rebuilds, 1);
    ecs_fini(world);
}

/** Run frames until the capture of a world wrote its file.
 *
 * @param world The world
 */
static void seq_capture_wait(
    ecs_world_t *world)
{
    int32_t i;
    for (i = 0; i < 400 && box_audio_capture_pending(world); i ++) {
        audio_run(world, 1);
    }

    test_bool(box_audio_capture_pending(world), false);
}

/** Create a world and run test/assets/audio/sequencer.flecs in it.
 *
 * @return The world
 */
static ecs_world_t* seq_script_world(void)
{
    ecs_world_t *world = audio_world();
    char *code = audio_read("test/assets/audio/sequencer.flecs");
    test_assert(ecs_script_run(world, "sequencer", code, NULL) == 0);
    ecs_os_free(code);

    return world;
}

/** Record the script through a loop of the transport and a tempo change.
 *
 * @param path The WAV to write
 */
static void seq_scene(
    const char *path)
{
    ecs_world_t *world = seq_script_world();
    test_int(box_audio_capture(world, 3.0f, path), 0);
    audio_run(world, 100);
    seq_tempo(world, 150);
    seq_capture_wait(world);
    ecs_fini(world);
}

/** Read a whole file.
 *
 * @param path The file
 * @param size Receives its size
 * @return The bytes, which the caller frees, NULL when it cannot be read
 */
static uint8_t* seq_read(
    const char *path,
    long *size)
{
    FILE *f = fopen(path, "rb");
    uint8_t *result = NULL;
    if (f) {
        fseek(f, 0, SEEK_END);
        *size = ftell(f);
        fseek(f, 0, SEEK_SET);
        result = ecs_os_malloc((ecs_size_t)*size);
        test_assert(fread(result, 1, (size_t)*size, f) == (size_t)*size);
        fclose(f);
    }

    return result;
}

/** The script: a prefab clip with the defaults of its notes, a track with two placements and
 * the notes they play. */
void Sequencer_script(void) {
    // audio/sequencer
    ecs_world_t *world = seq_script_world();
    ecs_entity_t riff = ecs_lookup(world, "riff");
    ecs_entity_t bass = ecs_lookup(world, "bass");
    ecs_entity_t answer = ecs_lookup(world, "bass.answer");
    const NoteClip *clip;
    const Note *n;
    test_assert(riff != 0 && bass != 0 && answer != 0);
    clip = ecs_get(world, riff, NoteClip);
    test_assert(clip != NULL);
    test_int(ecs_vec_count(&clip->notes), 3);
    n = ecs_vec_first(&clip->notes);
    test_flt(n[0].velocity, 0.8f);
    test_flt(n[1].velocity, 1.0f);
    test_flt(n[2].pan, -0.5f);
    test_flt(ecs_get(world, bass, AudioBus)->volume, 0.8f);
    test_flt(ecs_get(world, answer, ClipPlacement)->gain, 0.5f);
    test_flt(ecs_get(world, answer, ClipPlacement)->transpose, 12.0f);
    audio_run(world, 150);
    test_int(box_audio_module_get(world)->sequencer.stats.notes, 7);
    test_assert(audio_peak(audio_tap_l, 0, audio_tap_count) > 0.1f);
    ecs_fini(world);
}

/** Two runs of the script capture bit identical audio. */
void Sequencer_bit_identical(void) {
    // audio/sequencer
    const char *a = "audio_sequencer_a.wav", *b = "audio_sequencer_b.wav";
    long size_a = 0, size_b = 0;
    uint8_t *wav_a, *wav_b;
    int32_t i, loud = 0;
    seq_scene(a);
    seq_scene(b);
    wav_a = seq_read(a, &size_a);
    wav_b = seq_read(b, &size_b);
    test_assert(wav_a != NULL);
    test_assert(wav_b != NULL);
    test_int(size_a, 44 + 3 * 48000 * 4);
    test_int(size_a, size_b);
    test_assert(!memcmp(wav_a, wav_b, (size_t)size_a));
    for (i = 44; i < size_a; i += 2) {
        int16_t v = (int16_t)(wav_a[i] | (wav_a[i + 1] << 8));
        loud += v > 3000 || v < -3000;
    }

    test_assert(loud > 1000);
    ecs_os_free(wav_a);
    ecs_os_free(wav_b);
    remove(a);
    remove(b);
}

/** The audio command prints the tracks with their placements and sounding notes. */
void Sequencer_command(void) {
    // audio/sequencer
    ecs_world_t *world = seq_script_world();
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = false;
    char *out;
    audio_run(world, 30);
    out = box_command_run(world, s, "audio", &error);
    test_bool(error, false);
    test_assert(out != NULL);
    printf("%s\n", out);
    test_assert(strstr(out, "track bass: instrument pluck") != NULL);
    test_assert(strstr(out, "placement bass.intro: clip riff (3 notes, 2.000 beats)") != NULL);
    test_assert(strstr(out, ", loop") != NULL);
    test_assert(strstr(out, "placement bass.answer") != NULL);
    test_assert(strstr(out, "transpose 12.00") != NULL);
    test_assert(strstr(out, "sounding: pitch 64.00") != NULL);
    ecs_os_free(out);
    ecs_fini(world);
}

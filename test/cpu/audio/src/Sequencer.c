/**
 * @file Sequencer.c
 * @brief Note clips and tracks through the null transport at a fixed delta: the frames of the
 * starts and the releases of the notes, the transpose, gain and pan of a placement, loops,
 * offsets and durations, the loop of the transport, stops, seeks, tempo changes, edits and
 * deletes while playing, the one shots a stop leaves alone, a script and identical captures.
 *
 * Most cases play a sampler of a constant level with a velocity curve of 1 and the shortest
 * release: a note is exactly its level from its start frame, and its release is the first
 * frame below it. The audio clip cases play a ramp whose every frame tells its position.
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

/** Step of the ramp sound of the audio clip tests: its frame i holds (i + 1) * SEQ_STEP, so a
 * frame of the tap tells which frame of the sound plays */
#define SEQ_STEP 1e-6f

/** Frames of the ramp sound, six seconds */
#define SEQ_RAMP_FRAMES (AUDIO_RATE * 6)

/** Create a mono ramp sound at AUDIO_RATE whose frame i holds (i + 1) * SEQ_STEP.
 *
 * @param world The world
 * @param frames Frames of the sound
 * @return The sound
 */
static ecs_entity_t seq_ramp(
    ecs_world_t *world,
    int32_t frames)
{
    float *data = ecs_os_malloc_n(float, frames);
    ecs_entity_t e;
    int32_t i;
    for (i = 0; i < frames; i ++) {
        data[i] = (float)(i + 1) * SEQ_STEP;
    }

    e = box_audio_sound_from_pcm(world, 0, data, frames, 1, AUDIO_RATE);
    ecs_os_free(data);
    test_assert(e != 0);

    return e;
}

/** Get the frame of the ramp sound that a frame of the tap plays at unity gain.
 *
 * @param frame Frame of the tap
 * @return Frame of the sound, -1 for silence
 */
static int64_t seq_pos(
    int64_t frame)
{
    return llround((double)audio_tap_l[frame] / (double)SEQ_STEP) - 1;
}

/** Create an audio clip.
 *
 * @param world The world
 * @param sound The sound
 * @param offset Seconds into the sound
 * @param length Beats of a pass
 * @return The clip
 */
static ecs_entity_t seq_audio_clip(
    ecs_world_t *world,
    ecs_entity_t sound,
    float offset,
    float length)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, AudioClip, { .sound = sound, .offset = offset, .length = length });

    return e;
}

/** The defaults of an audio clip: no sound, from its start, 4 beats long. */
void Sequencer_audio_defaults(void) {
    // audio/sequencer
    ecs_world_t *world = audio_world();
    ecs_entity_t e = ecs_new(world);
    const AudioClip *clip;
    ecs_add(world, e, AudioClip);
    clip = ecs_get(world, e, AudioClip);
    test_uint(clip->sound, 0);
    test_flt(clip->offset, 0.0f);
    test_flt(clip->length, 4.0f);
    ecs_fini(world);
}

/** An audio clip starts on the frame of its placement and plays from the offset of the clip,
 * on a track without an instrument. */
void Sequencer_audio_start(void) {
    // audio/sequencer
    ecs_world_t *world = audio_world();
    box_audio_module_t *state = box_audio_module_get(world);
    ecs_entity_t sound = seq_ramp(world, SEQ_RAMP_FRAMES);
    ecs_entity_t clip = seq_audio_clip(world, sound, 0.25f, 2);
    ecs_entity_t track = seq_track(world, 0);
    int64_t c0, f;
    seq_place(world, track, clip, 1, 0, false, 0);
    audio_run(world, 2);
    audio_log_start();
    c0 = seq_play(world, true);
    seq_run_to(world, c0 + 4 * SEQ_BEAT);
    audio_log_stop();
    test_int(audio_log_warnings, 0);
    f = c0 + SEQ_BEAT;
    test_int(seq_onset(c0, f), -1);
    test_int(seq_pos(f), AUDIO_RATE / 4);
    test_int(seq_pos(f + 1), AUDIO_RATE / 4 + 1);
    test_int(seq_pos(f + 1000), AUDIO_RATE / 4 + 1000);
    test_int(seq_pos(f + 2 * SEQ_BEAT - 1), AUDIO_RATE / 4 + 2 * SEQ_BEAT - 1);
    test_int(seq_onset(f + 2 * SEQ_BEAT + SEQ_FADE, c0 + 4 * SEQ_BEAT), -1);
    test_int(state->sequencer.stats.passes, 1);
    test_int(state->sequencer.stats.seeks, 1);
    test_int(state->sequencer.stats.notes, 0);
    test_int(ecs_vec_count(&state->sequencer.voices), 0);
    ecs_fini(world);
}

/** The offset of a placement in beats is converted at the tempo and added to the offset of
 * the clip: half a beat at 120 bpm after 0.1 seconds is frame 4800 + 12000 of the sound, and
 * at 97 bpm frame 4800 + 0.5 * 60 / 97 * 48000. */
void Sequencer_audio_placement_offset(void) {
    // audio/sequencer
    float tempi[] = { 120.0f, 97.0f };
    int32_t t;
    for (t = 0; t < 2; t ++) {
        ecs_world_t *world = audio_world();
        ecs_entity_t sound = seq_ramp(world, SEQ_RAMP_FRAMES);
        ecs_entity_t clip = seq_audio_clip(world, sound, 0.1f, 4);
        ecs_entity_t track = seq_track(world, 0);
        double fpb = seq_fpb(tempi[t]);
        int64_t c0, f, at;
        seq_place(world, track, clip, 1, 0, false, 0.5);
        seq_tempo(world, tempi[t]);
        audio_run(world, 2);
        c0 = seq_play(world, true);
        f = c0 + llround(fpb);
        seq_run_to(world, f + 2 * SEQ_BEAT);
        at = llround(0.1 * AUDIO_RATE + 0.5 * fpb);
        test_int(seq_onset(c0, f), -1);
        test_assert(llabs(seq_pos(f) - at) <= 1);
        test_int(seq_pos(f + 500) - seq_pos(f), 500);
        ecs_fini(world);
    }
}

/** A looping placement starts a new voice at the offset of the clip on every pass, on the
 * frame where the voice before stops, without a gap. */
void Sequencer_audio_loop(void) {
    // audio/sequencer
    ecs_world_t *world = audio_world();
    box_audio_module_t *state = box_audio_module_get(world);
    ecs_entity_t sound = seq_ramp(world, SEQ_RAMP_FRAMES);
    ecs_entity_t clip = seq_audio_clip(world, sound, 0.5f, 1);
    ecs_entity_t track = seq_track(world, 0);
    int64_t c0, f, i;
    int32_t pass;
    seq_place(world, track, clip, 0, 4, true, 0);
    audio_run(world, 2);
    c0 = seq_play(world, true);
    seq_run_to(world, c0 + 6 * SEQ_BEAT);
    for (i = c0; i < c0 + 4 * SEQ_BEAT; i ++) {
        if (audio_tap_l[i] < (float)(AUDIO_RATE / 2) * SEQ_STEP) {
            test_assert(false);
        }
    }

    for (pass = 0; pass < 4; pass ++) {
        f = c0 + pass * SEQ_BEAT;
        test_int(seq_pos(f + SEQ_FADE + 10), AUDIO_RATE / 2 + SEQ_FADE + 10);
        test_int(seq_pos(f + SEQ_BEAT - 1), AUDIO_RATE / 2 + SEQ_BEAT - 1);
    }

    test_int(seq_onset(c0 + 4 * SEQ_BEAT + SEQ_FADE, c0 + 6 * SEQ_BEAT), -1);
    test_int(state->sequencer.stats.passes, 4);
    test_int(state->sequencer.stats.stops, 4);
    ecs_fini(world);
}

/** A clip whose sound lasts longer than its pass stops at the end of the pass, also in a
 * placement that lasts longer; a sound that ends first ends the pass. */
void Sequencer_audio_pass_end(void) {
    // audio/sequencer
    ecs_world_t *world = audio_world();
    ecs_entity_t sound = seq_ramp(world, SEQ_RAMP_FRAMES);
    ecs_entity_t short_sound = seq_ramp(world, SEQ_BEAT / 2);
    ecs_entity_t clip = seq_audio_clip(world, sound, 0, 1);
    ecs_entity_t clip_short = seq_audio_clip(world, short_sound, 0, 2);
    ecs_entity_t track = seq_track(world, 0);
    int64_t c0;
    seq_place(world, track, clip, 0, 3, false, 0);
    seq_place(world, track, clip_short, 4, 0, false, 0);
    audio_run(world, 2);
    c0 = seq_play(world, true);
    seq_run_to(world, c0 + 7 * SEQ_BEAT);
    test_int(seq_pos(c0), 0);
    test_int(seq_pos(c0 + SEQ_BEAT - 1), SEQ_BEAT - 1);
    test_int(seq_onset(c0 + SEQ_BEAT + SEQ_FADE, c0 + 4 * SEQ_BEAT), -1);
    test_int(seq_pos(c0 + 4 * SEQ_BEAT + 100), 100);
    test_int(seq_pos(c0 + 4 * SEQ_BEAT + SEQ_BEAT / 2 - 1), SEQ_BEAT / 2 - 1);
    test_int(seq_onset(c0 + 4 * SEQ_BEAT + SEQ_BEAT / 2 + SEQ_FADE, c0 + 7 * SEQ_BEAT), -1);
    ecs_fini(world);
}

/** A seek goes on in the middle of the clip that covers the new position, at its sample. */
void Sequencer_audio_seek(void) {
    // audio/sequencer
    ecs_world_t *world = audio_world();
    box_audio_module_t *state = box_audio_module_get(world);
    ecs_entity_t sound = seq_ramp(world, SEQ_RAMP_FRAMES);
    ecs_entity_t clip = seq_audio_clip(world, sound, 0, 8);
    ecs_entity_t track = seq_track(world, 0);
    int64_t c0, c1;
    seq_place(world, track, clip, 0, 0, false, 0);
    audio_run(world, 2);
    c0 = seq_play(world, true);
    audio_run(world, 10);
    test_int(seq_pos(c0 + 4000), 4000);
    ecs_singleton_ensure(world, Transport)->position = 2;
    ecs_singleton_modified(world, Transport);
    c1 = audio_tap_count;
    seq_run_to(world, c1 + 2 * SEQ_BEAT);
    test_int(seq_pos(c1 + SEQ_FADE + 100), 2 * SEQ_BEAT + SEQ_FADE + 100);
    test_int(seq_pos(c1 + SEQ_BEAT), 3 * SEQ_BEAT);
    test_int(state->sequencer.stats.passes, 2);
    test_int(state->sequencer.stats.seeks, 1);
    ecs_fini(world);
}

/** A loop of the transport from beat 1 to 3 stops a clip on the frame of loop_end and goes on
 * in its middle at loop_start. */
void Sequencer_audio_transport_loop(void) {
    // audio/sequencer
    ecs_world_t *world = audio_world();
    ecs_entity_t sound = seq_ramp(world, SEQ_RAMP_FRAMES);
    ecs_entity_t clip = seq_audio_clip(world, sound, 0, 4);
    ecs_entity_t track = seq_track(world, 0);
    Transport *tr;
    int64_t c0, at;
    int32_t pass;
    seq_place(world, track, clip, 0, 0, false, 0);
    tr = ecs_singleton_ensure(world, Transport);
    tr->loop = true;
    tr->loop_start = 1;
    tr->loop_end = 3;
    ecs_singleton_modified(world, Transport);
    audio_run(world, 2);
    c0 = seq_play(world, true);
    seq_run_to(world, c0 + 8 * SEQ_BEAT);
    test_int(seq_pos(c0 + 100), 100);
    test_int(seq_pos(c0 + 3 * SEQ_BEAT - 1), 3 * SEQ_BEAT - 1);
    for (pass = 0; pass < 2; pass ++) {
        at = c0 + 3 * SEQ_BEAT + pass * 2 * SEQ_BEAT;
        test_int(seq_pos(at + SEQ_FADE + 10), SEQ_BEAT + SEQ_FADE + 10);
        test_int(seq_pos(at + 2 * SEQ_BEAT - 1), 3 * SEQ_BEAT - 1);
    }

    ecs_fini(world);
}

/** A transpose of 12 doubles the rate of the sound. */
void Sequencer_audio_transpose(void) {
    // audio/sequencer
    ecs_world_t *world = audio_world();
    ecs_entity_t sound = seq_ramp(world, SEQ_RAMP_FRAMES);
    ecs_entity_t clip = seq_audio_clip(world, sound, 0.1f, 2);
    ecs_entity_t track = seq_track(world, 0);
    ecs_entity_t p;
    int64_t c0, f;
    p = seq_place(world, track, clip, 1, 0, false, 0);
    ecs_get_mut(world, p, ClipPlacement)->transpose = 12;
    ecs_modified(world, p, ClipPlacement);
    audio_run(world, 2);
    c0 = seq_play(world, true);
    f = c0 + SEQ_BEAT;
    seq_run_to(world, f + 2 * SEQ_BEAT);
    test_int(seq_pos(f), AUDIO_RATE / 10);
    test_int(seq_pos(f + 1000), AUDIO_RATE / 10 + 2000);
    test_int(seq_pos(f + 2 * SEQ_BEAT - 1), AUDIO_RATE / 10 + 2 * (2 * SEQ_BEAT - 1));
    test_int(seq_onset(f + 2 * SEQ_BEAT + SEQ_FADE, f + 2 * SEQ_BEAT + 2000), -1);
    ecs_fini(world);
}

/** The gain of a placement is the volume of its voice. */
void Sequencer_audio_gain(void) {
    // audio/sequencer
    ecs_world_t *world = audio_world();
    ecs_entity_t sound = audio_dc(world, NULL, 0.4f, AUDIO_RATE * 2);
    ecs_entity_t clip = seq_audio_clip(world, sound, 0, 1);
    ecs_entity_t track = seq_track(world, 0);
    ecs_entity_t p;
    int64_t c0;
    p = seq_place(world, track, clip, 0, 0, false, 0);
    ecs_get_mut(world, p, ClipPlacement)->gain = 0.5f;
    ecs_modified(world, p, ClipPlacement);
    audio_run(world, 2);
    c0 = seq_play(world, true);
    seq_run_to(world, c0 + 2 * SEQ_BEAT);
    test_flt(audio_tap_l[c0 - 1], 0.0f);
    test_flt(audio_tap_l[c0], 0.2f);
    test_flt(audio_tap_r[c0 + 1000], 0.2f);
    test_int(seq_edge(c0, c0 + SEQ_BEAT, 0.2f), -1);
    ecs_fini(world);
}

/** A track with a note placement and an audio placement plays both: the note on its
 * instrument, the audio clip on its bus. */
void Sequencer_audio_with_notes(void) {
    // audio/sequencer
    ecs_world_t *world = audio_world();
    box_audio_module_t *state = box_audio_module_get(world);
    ecs_entity_t inst = seq_sampler(world, SEQ_LEVEL);
    ecs_entity_t sound = audio_dc(world, NULL, 0.25f, AUDIO_RATE * 2);
    Note notes[] = {{ .start = 0, .duration = 1, .pitch = 60, .velocity = 1 }};
    ecs_entity_t clip = seq_clip(world, 1, notes, 1);
    ecs_entity_t audio = seq_audio_clip(world, sound, 0, 2);
    ecs_entity_t track = seq_track(world, inst);
    int64_t c0;
    seq_place(world, track, clip, 0, 0, false, 0);
    seq_place(world, track, audio, 0, 0, false, 0);
    audio_run(world, 2);
    c0 = seq_play(world, true);
    seq_run_to(world, c0 + 3 * SEQ_BEAT);
    test_flt(audio_tap_l[c0], SEQ_LEVEL + 0.25f);
    test_int(seq_edge(c0, c0 + SEQ_BEAT + 1, SEQ_LEVEL + 0.25f), c0 + SEQ_BEAT);
    test_int(seq_edge(c0 + SEQ_BEAT + SEQ_FADE, c0 + 2 * SEQ_BEAT, 0.25f), -1);
    test_int(seq_onset(c0 + 2 * SEQ_BEAT + SEQ_FADE, c0 + 3 * SEQ_BEAT), -1);
    test_int(state->sequencer.stats.notes, 1);
    test_int(state->sequencer.stats.passes, 1);
    ecs_fini(world);
}

/** A stop of the transport stops the voices of audio clips at once and cancels the passes
 * scheduled that have not started. */
void Sequencer_audio_stop(void) {
    // audio/sequencer
    ecs_world_t *world = audio_world();
    box_audio_module_t *state = box_audio_module_get(world);
    ecs_entity_t sound = seq_ramp(world, SEQ_RAMP_FRAMES);
    ecs_entity_t clip = seq_audio_clip(world, sound, 0, 4);
    ecs_entity_t later = seq_audio_clip(world, sound, 0, 1);
    ecs_entity_t track = seq_track(world, 0);
    int64_t c0;
    seq_place(world, track, clip, 0, 0, false, 0);
    seq_place(world, track, later, 0.25, 0, false, 0);
    audio_run(world, 2);
    c0 = seq_play(world, true);
    audio_run(world, 1);
    test_int(state->sequencer.stats.passes, 2);
    test_int(ecs_vec_count(&state->sequencer.voices), 2);
    seq_play(world, false);
    audio_run(world, 1);
    test_int(state->timeline.stats.cancels, 1);
    test_int(ecs_vec_count(&state->sequencer.voices), 0);
    audio_run(world, 40);
    test_int(seq_pos(c0 + 700), 700);
    test_int(seq_onset(c0 + 1600 + SEQ_FADE, audio_tap_count), -1);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 0);
    ecs_fini(world);
}

/** Deleting the track and disabling the placement stop their voices at once. */
void Sequencer_audio_delete_disable(void) {
    // audio/sequencer
    ecs_world_t *world = audio_world();
    box_audio_module_t *state = box_audio_module_get(world);
    ecs_entity_t sound = seq_ramp(world, SEQ_RAMP_FRAMES);
    ecs_entity_t clip = seq_audio_clip(world, sound, 0, 4);
    ecs_entity_t track = seq_track(world, 0);
    ecs_entity_t other = seq_track(world, 0);
    ecs_entity_t p;
    int64_t c0, c1;
    seq_place(world, track, clip, 0, 0, false, 0);
    p = seq_place(world, other, clip, 0, 0, false, 0);
    audio_run(world, 2);
    c0 = seq_play(world, true);
    audio_run(world, 20);
    test_int(seq_pos(audio_tap_count - 1), 2 * (audio_tap_count - 1 - c0) + 1);
    ecs_delete(world, track);
    ecs_enable(world, p, false);
    c1 = audio_tap_count;
    seq_run_to(world, c0 + 5 * SEQ_BEAT);
    test_int(seq_onset(c1 + AUDIO_FRAME + SEQ_FADE, audio_tap_count), -1);
    test_int(ecs_vec_count(&state->sequencer.voices), 0);
    test_int(state->sequencer.stats.passes, 2);
    ecs_fini(world);
}

/** A sound that is not ready skips its passes and logs once. */
void Sequencer_audio_not_ready(void) {
    // audio/sequencer
    ecs_world_t *world = audio_world();
    box_audio_module_t *state = box_audio_module_get(world);
    ecs_entity_t none = ecs_new(world);
    ecs_entity_t clip = seq_audio_clip(world, none, 0, 1);
    ecs_entity_t track = seq_track(world, 0);
    seq_place(world, track, clip, 0, 8, true, 0);
    audio_run(world, 2);
    audio_log_start();
    seq_play(world, true);
    audio_run(world, 120);
    audio_log_stop();
    test_int(audio_log_warnings, 1);
    test_int(state->sequencer.stats.passes, 0);
    test_assert(state->sequencer.stats.skipped >= 4);
    test_int(seq_onset(0, audio_tap_count), -1);
    ecs_fini(world);
}

/** Record audio clips, looped, transposed, with offsets, a note track and an audio track, a
 * tempo change and a seek.
 *
 * @param path The WAV to write
 */
static void seq_audio_scene(
    const char *path)
{
    ecs_world_t *world = audio_world();
    ecs_entity_t inst = seq_sampler(world, 0.2f);
    ecs_entity_t ramp = seq_ramp(world, SEQ_RAMP_FRAMES);
    ecs_entity_t tone = audio_tone(world, NULL, 330, 0.3f, AUDIO_RATE * 3);
    Note notes[] = {
        { .start = 0, .duration = 0.5f, .pitch = 60, .velocity = 1 },
        { .start = 1.5f, .duration = 0.25f, .pitch = 67, .velocity = 0.7f }
    };
    ecs_entity_t clip = seq_clip(world, 2, notes, 2);
    ecs_entity_t loop = seq_audio_clip(world, tone, 0.013f, 0.75f);
    ecs_entity_t stem = seq_audio_clip(world, ramp, 0.2f, 8);
    ecs_entity_t track = seq_track(world, inst);
    ecs_entity_t audio = seq_track(world, 0);
    ecs_entity_t p;
    seq_place(world, track, clip, 0, 8, true, 0);
    p = seq_place(world, track, loop, 0.5, 6, true, 0.3);
    ecs_get_mut(world, p, ClipPlacement)->transpose = 7;
    ecs_get_mut(world, p, ClipPlacement)->gain = 0.8f;
    ecs_modified(world, p, ClipPlacement);
    seq_place(world, audio, stem, 1, 0, false, 0.25);
    seq_tempo(world, 132);
    audio_run(world, 2);
    seq_play(world, true);
    test_int(box_audio_capture(world, 3.0f, path), 0);
    audio_run(world, 60);
    seq_tempo(world, 100);
    audio_run(world, 30);
    ecs_singleton_ensure(world, Transport)->position = 2.3;
    ecs_singleton_modified(world, Transport);
    seq_capture_wait(world);
    ecs_fini(world);
}

/** Two runs of audio clips capture bit identical audio. */
void Sequencer_audio_bit_identical(void) {
    // audio/sequencer
    const char *a = "audio_clip_a.wav", *b = "audio_clip_b.wav";
    long size_a = 0, size_b = 0;
    uint8_t *wav_a, *wav_b;
    int32_t i, loud = 0;
    seq_audio_scene(a);
    seq_audio_scene(b);
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

/** The audio command lists audio placements with the note placements. */
void Sequencer_audio_command(void) {
    // audio/sequencer
    ecs_world_t *world = audio_world();
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    ecs_entity_t sound = audio_dc(world, "vocal", 0.1f, AUDIO_RATE * 4);
    ecs_entity_t clip = ecs_entity(world, { .name = "verse" });
    ecs_entity_t track = ecs_entity(world, { .name = "vox" });
    ecs_entity_t p = ecs_entity(world, { .name = "take", .parent = track });
    bool error = false;
    char *out;
    ecs_set(world, clip, AudioClip, { .sound = sound, .offset = 0.5f, .length = 8 });
    ecs_set(world, track, Track, { 0 });
    ecs_set(world, p, ClipPlacement, { .clip = clip, .start = 0, .gain = 1 });
    audio_run(world, 2);
    seq_play(world, true);
    audio_run(world, 10);
    out = box_command_run(world, s, "audio", &error);
    test_bool(error, false);
    test_assert(out != NULL);
    printf("%s\n", out);
    test_assert(strstr(out, "track vox: instrument none") != NULL);
    test_assert(strstr(out, "placement vox.take: clip verse (audio vocal from 0.500 s, "
        "8.000 beats)") != NULL);
    test_assert(strstr(out, "playing: audio until beat 8.000") != NULL);
    ecs_os_free(out);
    ecs_fini(world);
}

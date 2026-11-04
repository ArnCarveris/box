/**
 * @file Timeline.c
 * @brief The Transport singleton through the null transport at a fixed delta: the position in
 * beats follows the frames rendered, seeks, loops and tempo changes, the lookahead window
 * covers every frame once and the metronome clicks on exactly the frame of each beat.
 */

#include "world.h"
#include <stdio.h>

/** Frames of the default lookahead of 0.15 seconds at AUDIO_RATE */
#define TIMELINE_AHEAD 7200

/** Get the transport to write it; timeline_done marks it modified.
 *
 * @param world The world
 * @return The transport
 */
static Transport* timeline_edit(
    ecs_world_t *world)
{
    return ecs_singleton_ensure(world, Transport);
}

/** Mark the transport modified after timeline_edit.
 *
 * @param world The world
 */
static void timeline_done(
    ecs_world_t *world)
{
    ecs_singleton_modified(world, Transport);
}

/** Get the position the module wrote.
 *
 * @param world The world
 * @return Beats
 */
static double timeline_position(
    ecs_world_t *world)
{
    return ecs_singleton_get(world, Transport)->position;
}

/** Get the frames of a beat at a tempo at AUDIO_RATE.
 *
 * @param tempo Quarter notes per minute
 * @return Frames
 */
static double timeline_fpb(
    double tempo)
{
    return (double)AUDIO_RATE * 60.0 / tempo;
}

/** Find the first sample of the tap that is not 0 in a range.
 *
 * @param from First frame
 * @param to End frame
 * @return The frame, -1 when the range is silent
 */
static int64_t timeline_onset(
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

/** Check that the clicks of beats start on their frame, silent before, and measure them.
 *
 * @param start Frame of the first beat
 * @param count Beats
 * @param fpb Frames of a beat
 * @param peaks Receives the peak of the first 30 ms of each click
 */
static void timeline_clicks(
    int64_t start,
    int32_t count,
    double fpb,
    float *peaks)
{
    int32_t b;
    for (b = 0; b < count; b ++) {
        int64_t f = start + llround((double)b * fpb);
        int64_t quiet = f - 10000 > start ? f - 10000 : start;
        int64_t onset = timeline_onset(f, f + 100);
        test_assert(f + 1440 < audio_tap_count);
        test_int(timeline_onset(quiet, f), -1);
        test_assert(onset >= f);
        test_assert(onset <= f + 3);
        peaks[b] = audio_peak(audio_tap_l, (int32_t)f, (int32_t)f + 1440);
        test_assert(peaks[b] > 0.05f);
    }
}

void Timeline_defaults(void) {
    // audio/timeline
    ecs_world_t *world = audio_world();
    const Transport *tr = ecs_singleton_get(world, Transport);
    test_assert(tr != NULL);
    test_flt(tr->tempo, 120.0f);
    test_int(tr->beats_per_bar, 4);
    test_int(tr->beat_unit, 4);
    test_flt(tr->position, 0.0);
    test_bool(tr->playing, false);
    test_bool(tr->loop, false);
    test_flt(tr->lookahead, 0.15f);
    test_bool(tr->metronome, false);
    audio_run(world, 10);
    test_flt(timeline_position(world), 0.0);
    int64_t commands = box_audio_module_stats(world).commands;
    audio_run(world, 10);
    test_int(box_audio_module_stats(world).commands, commands);
    int32_t count = 0;
    test_assert(box_audio_timeline_window(world, &count) == NULL);
    test_int(count, 0);
    ecs_fini(world);
}

void Timeline_advances(void) {
    // audio/timeline
    ecs_world_t *world = audio_world();
    int32_t start = audio_tap_count;
    timeline_edit(world)->playing = true;
    timeline_done(world);
    audio_run(world, 1);
    test_flt(timeline_position(world), 0.0);
    audio_run(world, 29);
    double expected = (double)(audio_tap_count - AUDIO_FRAME - start) / timeline_fpb(120);
    test_assert(fabs(timeline_position(world) - expected) < 1e-9);
    test_assert(box_audio_timeline_beat(world, (uint64_t)(audio_tap_count - AUDIO_FRAME)) ==
        timeline_position(world));
    test_int(box_audio_timeline_frame(world, 1.0), start + 24000);
    ecs_fini(world);
}

void Timeline_stopped_holds(void) {
    // audio/timeline
    ecs_world_t *world = audio_world();
    int32_t start = audio_tap_count;
    timeline_edit(world)->playing = true;
    timeline_done(world);
    audio_run(world, 10);
    int32_t stop = audio_tap_count;
    timeline_edit(world)->playing = false;
    timeline_done(world);
    audio_run(world, 1);
    double held = timeline_position(world);
    test_assert(fabs(held - (double)(stop - start) / 24000.0) < 1e-9);
    audio_run(world, 20);
    test_assert(timeline_position(world) == held);
    test_int(box_audio_timeline_frame(world, 1.0), -1);

    int32_t resume = audio_tap_count;
    timeline_edit(world)->playing = true;
    timeline_done(world);
    audio_run(world, 1);
    test_assert(timeline_position(world) == held);
    audio_run(world, 10);
    double expected = held + (double)(audio_tap_count - AUDIO_FRAME - resume) / 24000.0;
    test_assert(fabs(timeline_position(world) - expected) < 1e-9);
    ecs_fini(world);
}

void Timeline_seek(void) {
    // audio/timeline
    ecs_world_t *world = audio_world();
    timeline_edit(world)->playing = true;
    timeline_done(world);
    audio_run(world, 5);
    int32_t at = audio_tap_count;
    timeline_edit(world)->position = 8.0;
    timeline_done(world);
    audio_run(world, 1);
    test_assert(timeline_position(world) == 8.0);
    audio_run(world, 10);
    double expected = 8.0 + (double)(audio_tap_count - AUDIO_FRAME - at) / 24000.0;
    test_assert(fabs(timeline_position(world) - expected) < 1e-9);

    at = audio_tap_count;
    timeline_edit(world)->position = -1.0;
    timeline_done(world);
    audio_run(world, 1);
    test_assert(timeline_position(world) == -1.0);
    test_int(box_audio_timeline_frame(world, 0.0), at + 24000);
    ecs_fini(world);
}

void Timeline_seek_stopped(void) {
    // audio/timeline
    ecs_world_t *world = audio_world();
    timeline_edit(world)->position = 3.0;
    timeline_done(world);
    audio_run(world, 5);
    test_assert(timeline_position(world) == 3.0);
    int32_t start = audio_tap_count;
    timeline_edit(world)->playing = true;
    timeline_done(world);
    audio_run(world, 1);
    test_assert(timeline_position(world) == 3.0);
    audio_run(world, 10);
    double expected = 3.0 + (double)(audio_tap_count - AUDIO_FRAME - start) / 24000.0;
    test_assert(fabs(timeline_position(world) - expected) < 1e-9);
    ecs_fini(world);
}

void Timeline_loop_wraps(void) {
    // audio/timeline
    ecs_world_t *world = audio_world();
    double fpb = timeline_fpb(130);
    int32_t start = audio_tap_count;
    Transport *tr = timeline_edit(world);
    tr->tempo = 130.0f;
    tr->loop = true;
    tr->loop_start = 1.0;
    tr->loop_end = 3.0;
    tr->playing = true;
    timeline_done(world);
    int64_t wrap = start + llround(3.0 * fpb);
    audio_run(world, 76);
    test_assert(audio_tap_count - AUDIO_FRAME < wrap);
    test_assert(audio_tap_count - AUDIO_FRAME + TIMELINE_AHEAD >= wrap);
    test_assert(box_audio_timeline_beat(world, (uint64_t)wrap) == 1.0);
    double before = box_audio_timeline_beat(world, (uint64_t)wrap - 1);
    test_assert(before < 3.0);
    test_assert(before > 3.0 - 1.0 / fpb - 1e-9);
    test_int(box_audio_timeline_frame(world, 1.5), wrap + llround(0.5 * fpb));

    audio_run(world, 20);
    double expected = 1.0 + (double)(audio_tap_count - AUDIO_FRAME - wrap) / fpb;
    test_assert(fabs(timeline_position(world) - expected) < 1e-9);
    test_assert(box_audio_module_get(world)->timeline.stats.anchors >= 2);
    ecs_fini(world);
}

void Timeline_loop_past_end(void) {
    // audio/timeline
    ecs_world_t *world = audio_world();
    int32_t start = audio_tap_count;
    Transport *tr = timeline_edit(world);
    tr->loop = true;
    tr->loop_start = 0.0;
    tr->loop_end = 2.0;
    tr->position = 5.0;
    tr->playing = true;
    timeline_done(world);
    audio_run(world, 90);
    double expected = 5.0 + (double)(audio_tap_count - AUDIO_FRAME - start) / 24000.0;
    test_assert(fabs(timeline_position(world) - expected) < 1e-9);

    int32_t at = audio_tap_count;
    timeline_edit(world)->position = 1.0;
    timeline_done(world);
    audio_run(world, 60);
    int64_t clock = audio_tap_count - AUDIO_FRAME;
    test_assert(clock > at + 24000);
    expected = (double)(clock - (at + 24000)) / 24000.0;
    test_assert(fabs(timeline_position(world) - expected) < 1e-9);
    ecs_fini(world);
}

void Timeline_tempo_continuous(void) {
    // audio/timeline
    ecs_world_t *world = audio_world();
    int32_t start = audio_tap_count, i;
    timeline_edit(world)->playing = true;
    timeline_done(world);
    audio_run(world, 10);
    double prev = timeline_position(world), slow = AUDIO_FRAME / 32000.0;
    double fast = AUDIO_FRAME / 24000.0, d = 0;
    int32_t change = audio_tap_count;
    timeline_edit(world)->tempo = 90.0f;
    timeline_done(world);
    for (i = 0; i < 30; i ++) {
        audio_run(world, 1);
        d = timeline_position(world) - prev;
        test_assert(d >= slow - 1e-9);
        test_assert(d <= fast + 1e-9);
        prev = timeline_position(world);
    }

    test_assert(fabs(d - slow) < 1e-9);
    int64_t anchor = change + TIMELINE_AHEAD - AUDIO_FRAME;
    double beat = (double)(anchor - start) / 24000.0;
    double expected = beat + (double)(audio_tap_count - AUDIO_FRAME - anchor) / 32000.0;
    test_assert(fabs(timeline_position(world) - expected) < 1e-9);

    timeline_edit(world)->tempo = 5000.0f;
    timeline_done(world);
    audio_run(world, 20);
    d = timeline_position(world) - prev;
    test_assert(d > 0);
    test_flt(box_audio_module_get(world)->timeline.tempo, 999.0f);
    ecs_fini(world);
}

/** Check that a segment starts where the one before ended, in beats: the same beat, or the
 * start of the loop after its end.
 *
 * @param end Beat the segment before ended at
 * @param start Beat the segment starts at
 * @return Whether it wrapped
 */
static bool timeline_continues(
    double end,
    double start)
{
    bool wrap = end == 2.0 && start == 0.0;
    test_assert(end == start || wrap);

    return wrap;
}

void Timeline_window_coverage(void) {
    // audio/timeline
    ecs_world_t *world = audio_world();
    Transport *tr = timeline_edit(world);
    tr->loop = true;
    tr->loop_start = 0.0;
    tr->loop_end = 2.0;
    tr->playing = true;
    timeline_done(world);
    uint64_t prev_end = 0;
    double prev_beat = 0;
    int32_t f, i, count, wraps = 0, multi = 0;
    for (f = 0; f < 200; f ++) {
        if (f == 50 || f == 120) {
            timeline_edit(world)->tempo = f == 50 ? 140.0f : 77.7f;
            timeline_done(world);
        }

        uint64_t clock = (uint64_t)audio_tap_count;
        audio_run(world, 1);
        const box_audio_segment_t *segs = box_audio_timeline_window(world, &count);
        test_assert(segs != NULL);
        test_assert(count >= 1);
        test_assert(segs[count - 1].frame_end == clock + TIMELINE_AHEAD);
        if (f) {
            test_assert(segs[0].frame == prev_end);
            wraps += timeline_continues(prev_beat, segs[0].start);
        } else {
            test_assert(segs[0].frame == clock);
            test_assert(segs[0].start == 0.0);
        }

        for (i = 0; i < count; i ++) {
            test_assert(segs[i].frame < segs[i].frame_end);
            test_assert(segs[i].start < segs[i].end);
            if (i) {
                test_assert(segs[i].frame == segs[i - 1].frame_end);
                wraps += timeline_continues(segs[i - 1].end, segs[i].start);
            }
        }

        multi += count > 1;
        prev_end = segs[count - 1].frame_end;
        prev_beat = segs[count - 1].end;
    }

    test_assert(wraps >= 3);
    test_assert(multi >= 1);
    ecs_fini(world);
}

void Timeline_metronome_4_4(void) {
    // audio/timeline
    ecs_world_t *world = audio_world();
    double fpb = timeline_fpb(130);
    float peaks[8];
    int32_t start = audio_tap_count;
    Transport *tr = timeline_edit(world);
    tr->tempo = 130.0f;
    tr->metronome = true;
    tr->playing = true;
    timeline_done(world);
    audio_run(world, 200);
    timeline_clicks(start, 8, fpb, peaks);
    test_assert(peaks[0] > 1.5f * peaks[1]);
    test_assert(peaks[4] > 1.5f * peaks[3]);
    test_assert(fabsf(peaks[1] - peaks[2]) < 0.1f * peaks[1]);
    test_assert(fabsf(peaks[2] - peaks[3]) < 0.1f * peaks[2]);
    test_assert(fabsf(peaks[0] - peaks[4]) < 0.1f * peaks[0]);
    test_assert(box_audio_module_get(world)->timeline.stats.accents >= 2);
    ecs_fini(world);
}

void Timeline_metronome_6_8(void) {
    // audio/timeline
    ecs_world_t *world = audio_world();
    double fpb = timeline_fpb(100);
    float peaks[9];
    int32_t start = audio_tap_count, b;
    Transport *tr = timeline_edit(world);
    tr->tempo = 100.0f;
    tr->beats_per_bar = 6;
    tr->beat_unit = 8;
    tr->metronome = true;
    tr->playing = true;
    timeline_done(world);
    audio_run(world, 300);
    timeline_clicks(start, 9, fpb, peaks);
    for (b = 0; b < 9; b ++) {
        if (b % 3) {
            test_assert(peaks[b - b % 3] > 1.5f * peaks[b]);
        }
    }

    ecs_fini(world);
}

void Timeline_metronome_loop(void) {
    // audio/timeline
    ecs_world_t *world = audio_world();
    float peaks[1];
    int32_t start = audio_tap_count;
    Transport *tr = timeline_edit(world);
    tr->loop = true;
    tr->loop_start = 1.0;
    tr->loop_end = 3.0;
    tr->metronome = true;
    tr->playing = true;
    timeline_done(world);
    audio_run(world, 160);
    timeline_clicks(start + 72000, 1, 24000.0, peaks);
    timeline_clicks(start + 120000, 1, 24000.0, peaks);
    test_int(timeline_onset(start + 72000 + 3000, start + 96000), -1);
    test_int(box_audio_module_get(world)->timeline.stats.accents, 1);
    ecs_fini(world);
}

void Timeline_stop_cancels(void) {
    // audio/timeline
    ecs_world_t *world = audio_world();
    box_audio_module_t *state = box_audio_module_get(world);
    int32_t start = audio_tap_count;
    Transport *tr = timeline_edit(world);
    tr->metronome = true;
    tr->playing = true;
    timeline_done(world);
    audio_run(world, 23);
    test_int(state->timeline.stats.clicks, 2);
    test_assert(timeline_onset(start, start + 100) >= 0);
    timeline_edit(world)->playing = false;
    timeline_done(world);
    audio_run(world, 1);
    test_int(state->timeline.stats.cancels, 1);
    audio_run(world, 20);
    test_assert(audio_tap_count > start + 30000);
    test_int(timeline_onset(start + 4000, audio_tap_count), -1);
    test_int(state->timeline.stats.clicks, 2);
    ecs_fini(world);
}

/** Run frames until the capture of a world wrote its file.
 *
 * @param world The world
 */
static void timeline_capture_wait(
    ecs_world_t *world)
{
    int32_t i;
    for (i = 0; i < 400 && box_audio_capture_pending(world); i ++) {
        audio_run(world, 1);
    }

    test_bool(box_audio_capture_pending(world), false);
}

/** Record the metronome through a loop, a tempo change and a seek.
 *
 * @param path The WAV to write
 */
static void timeline_scene(
    const char *path)
{
    ecs_world_t *world = audio_world();
    Transport *tr = timeline_edit(world);
    tr->tempo = 137.0f;
    tr->beats_per_bar = 3;
    tr->loop = true;
    tr->loop_end = 5.0;
    tr->metronome = true;
    tr->playing = true;
    timeline_done(world);
    test_int(box_audio_capture(world, 3.0f, path), 0);
    audio_run(world, 60);
    timeline_edit(world)->tempo = 160.0f;
    timeline_done(world);
    audio_run(world, 30);
    timeline_edit(world)->position = 2.0;
    timeline_done(world);
    timeline_capture_wait(world);
    ecs_fini(world);
}

/** Read a whole file.
 *
 * @param path The file
 * @param size Receives its size
 * @return The bytes, which the caller frees, NULL when it cannot be read
 */
static uint8_t* timeline_read(
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

void Timeline_bit_identical(void) {
    // audio/timeline
    const char *a = "audio_timeline_a.wav", *b = "audio_timeline_b.wav";
    timeline_scene(a);
    timeline_scene(b);
    long size_a = 0, size_b = 0;
    uint8_t *wav_a = timeline_read(a, &size_a);
    uint8_t *wav_b = timeline_read(b, &size_b);
    test_assert(wav_a != NULL);
    test_assert(wav_b != NULL);
    test_int(size_a, 44 + 3 * 48000 * 4);
    test_int(size_a, size_b);
    test_assert(!memcmp(wav_a, wav_b, (size_t)size_a));

    int32_t i, loud = 0;
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

void Timeline_new_output(void) {
    // audio/timeline
    ecs_world_t *world = audio_world();
    box_audio_module_t *state = box_audio_module_get(world);
    Transport *tr = timeline_edit(world);
    tr->metronome = true;
    tr->playing = true;
    timeline_done(world);
    audio_run(world, 40);
    double before = timeline_position(world);
    box_audio_output_open(world, &(box_audio_output_desc_t){
        .headless = true, .output = audio_world_tap });
    audio_tap_count = 0;
    audio_run(world, 1);
    test_assert(timeline_position(world) == before);
    test_int(state->timeline.stats.cancels, 0);
    audio_run(world, 30);
    double expected = before + (double)(audio_tap_count - AUDIO_FRAME) / 24000.0;
    test_assert(fabs(timeline_position(world) - expected) < 1e-9);
    int64_t f = llround((2.0 - before) * 24000.0);
    test_assert(timeline_onset(f, f + 4) >= 0);
    ecs_fini(world);
}

void Timeline_script(void) {
    // audio/timeline
    ecs_world_t *world = audio_world();
    char *code = audio_read("test/assets/audio/transport.flecs");
    int32_t start = audio_tap_count;
    float peaks[3];
    test_assert(ecs_script_run(world, "transport", code, NULL) == 0);
    ecs_os_free(code);
    const Transport *tr = ecs_singleton_get(world, Transport);
    test_flt(tr->tempo, 90.0f);
    test_int(tr->beats_per_bar, 6);
    test_int(tr->beat_unit, 8);
    test_bool(tr->playing, true);
    test_flt(tr->lookahead, 0.15f);
    audio_run(world, 1);
    test_assert(timeline_position(world) == 2.0);
    audio_run(world, 99);
    double expected = 2.0 + (double)(audio_tap_count - AUDIO_FRAME - start) / 32000.0;
    test_assert(fabs(timeline_position(world) - expected) < 1e-9);
    timeline_clicks(start, 3, 32000.0, peaks);
    test_assert(peaks[1] > 1.5f * peaks[0]);
    test_assert(peaks[1] > 1.5f * peaks[2]);
    ecs_fini(world);
}

void Timeline_command(void) {
    // audio/timeline
    ecs_world_t *world = audio_world();
    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = false;
    char *out = box_command_run(world, s, "audio tempo 90", &error);
    test_bool(error, false);
    test_assert(out != NULL && strstr(out, "tempo 90.0") != NULL);
    ecs_os_free(out);
    out = box_command_run(world, s, "audio seek 4", &error);
    test_bool(error, false);
    ecs_os_free(out);
    out = box_command_run(world, s, "audio play", &error);
    test_bool(error, false);
    ecs_os_free(out);
    int32_t start = audio_tap_count;
    audio_run(world, 1);
    test_assert(timeline_position(world) == 4.0);
    audio_run(world, 10);
    double expected = 4.0 + (double)(audio_tap_count - AUDIO_FRAME - start) / 32000.0;
    test_assert(fabs(timeline_position(world) - expected) < 1e-9);

    out = box_command_run(world, s, "audio", &error);
    test_bool(error, false);
    test_assert(out != NULL);
    test_assert(strstr(out, "transport: 90.0 bpm 4/4, position 2.1") != NULL);
    test_assert(strstr(out, "playing, loop off") != NULL);
    ecs_os_free(out);

    out = box_command_run(world, s, "audio stop", &error);
    test_bool(error, false);
    ecs_os_free(out);
    audio_run(world, 1);
    test_bool(ecs_singleton_get(world, Transport)->playing, false);

    out = box_command_run(world, s,
        "set box.audio.Transport box.audio.Transport {position: 1}", &error);
    test_bool(error, false);
    ecs_os_free(out);
    out = box_command_run(world, s, "audio play", &error);
    ecs_os_free(out);
    audio_run(world, 1);
    test_assert(timeline_position(world) == 1.0);

    audio_log_start();
    out = box_command_run(world, s, "audio tempo 5", &error);
    test_bool(error, true);
    ecs_os_free(out);
    out = box_command_run(world, s, "audio seek later", &error);
    test_bool(error, true);
    ecs_os_free(out);
    audio_log_stop();
    test_flt(ecs_singleton_get(world, Transport)->tempo, 90.0f);
    ecs_fini(world);
}

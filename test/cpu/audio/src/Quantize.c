/**
 * @file Quantize.c
 * @brief box_audio_play_quantized through the null transport at a fixed delta: a play starts on
 * exactly the frame of the next beat or bar, through a loop wrap, at once while the transport is
 * stopped, and neither a stop nor a seek of the transport cancels or moves a one shot.
 */

#include "world.h"

/** Frames of the DC sounds of the cases: past the ramp of a start, short enough to end before
 * the next boundary */
#define QUANTIZE_SHORT 1000

/** Start the transport at beat 0 on the clock of the next frame.
 *
 * @param world The world
 * @param tempo Quarter notes per minute
 * @param top Beats per bar
 * @param unit Beat unit
 */
static void quantize_start(
    ecs_world_t *world,
    float tempo,
    int32_t top,
    int32_t unit)
{
    Transport *tr = ecs_singleton_ensure(world, Transport);
    tr->tempo = tempo;
    tr->beats_per_bar = top;
    tr->beat_unit = unit;
    tr->playing = true;
    ecs_singleton_modified(world, Transport);
}

/** Find the first sample of the tap that is not 0 in a range.
 *
 * @param from First frame
 * @param to End frame
 * @return The frame, -1 when the range is silent
 */
static int64_t quantize_onset(
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

/** Get the frame of the first boundary of a grid at or after a frame, on a timeline that
 * started at beat 0 on a frame and plays at one tempo.
 *
 * @param start Frame of beat 0
 * @param clock The frame
 * @param fpb Frames of a beat
 * @param unit Beats of the grid
 * @return The frame
 */
static int64_t quantize_expected(
    int64_t start,
    int64_t clock,
    double fpb,
    double unit)
{
    double n = floor((double)(clock - start) / fpb / unit) * unit;
    while (start + llround(n * fpb) < clock) {
        n += unit;
    }

    return start + llround(n * fpb);
}

/** Run frames until the tap holds a frame and a few more.
 *
 * @param world The world
 * @param frame The frame
 */
static void quantize_run_past(
    ecs_world_t *world,
    int64_t frame)
{
    while (audio_tap_count < frame + AUDIO_FRAME + 10) {
        audio_run(world, 1);
    }
}

/** Play to the beat and to the bar at a tempo and a time signature and check that each starts
 * on its frame: a sound of 0.25 on the beat, one of 0.5 on the bar.
 *
 * @param tempo Quarter notes per minute
 * @param top Beats per bar
 * @param unit Beat unit
 */
static void quantize_grid(
    float tempo,
    int32_t top,
    int32_t unit)
{
    ecs_world_t *world = audio_world();
    ecs_entity_t on_beat = audio_dc(world, NULL, 0.25f, QUANTIZE_SHORT);
    ecs_entity_t on_bar = audio_dc(world, NULL, 0.5f, QUANTIZE_SHORT);
    double fpb = (double)AUDIO_RATE * 60.0 / (double)tempo;
    double bar = (double)top * 4.0 / (double)unit;
    int64_t start = audio_tap_count;
    quantize_start(world, tempo, top, unit);
    audio_run(world, 50);
    int64_t clock = audio_tap_count;
    int64_t beat_at = quantize_expected(start, clock, fpb, 1.0);
    int64_t bar_at = quantize_expected(start, clock, fpb, bar);
    test_assert(beat_at > clock);
    test_assert(bar_at > beat_at + QUANTIZE_SHORT);
    test_assert(box_audio_play_quantized(world, on_beat, 1.0f, AudioQuantizeBeat));
    test_assert(box_audio_play_quantized(world, on_bar, 1.0f, AudioQuantizeBar));
    quantize_run_past(world, bar_at);
    test_int(quantize_onset(clock, bar_at + 10), beat_at);
    test_assert(fabsf(audio_tap_l[beat_at + AUDIO_RAMP] - 0.25f) < 0.01f);
    test_int(quantize_onset(beat_at + QUANTIZE_SHORT + AUDIO_RAMP, bar_at + 10), bar_at);
    test_assert(fabsf(audio_tap_l[bar_at + AUDIO_RAMP] - 0.5f) < 0.01f);
    test_int(box_audio_module_get(world)->timeline.stats.cancels, 0);
    ecs_fini(world);
}

void Quantize_beat_bar_120_4_4(void) {
    // audio/quantize
    quantize_grid(120.0f, 4, 4);
}

void Quantize_beat_bar_97_4_4(void) {
    // audio/quantize
    quantize_grid(97.0f, 4, 4);
}

void Quantize_beat_bar_120_7_8(void) {
    // audio/quantize
    quantize_grid(120.0f, 7, 8);
}

void Quantize_beat_bar_97_7_8(void) {
    // audio/quantize
    quantize_grid(97.0f, 7, 8);
}

void Quantize_on_boundary(void) {
    // audio/quantize
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, NULL, 0.5f, QUANTIZE_SHORT);
    int64_t start = audio_tap_count;
    quantize_start(world, 120.0f, 4, 4);
    audio_run(world, 30);
    int64_t clock = audio_tap_count;
    test_int(clock, start + 24000);
    test_assert(box_audio_play_quantized(world, dc, 1.0f, AudioQuantizeBeat));
    audio_run(world, 1);
    test_int(quantize_onset(clock, audio_tap_count), clock);

    audio_run(world, 89);
    clock = audio_tap_count;
    test_int(clock, start + 96000);
    test_assert(box_audio_play_quantized(world, dc, 1.0f, AudioQuantizeBar));
    audio_run(world, 1);
    test_int(quantize_onset(clock, audio_tap_count), clock);
    ecs_fini(world);
}

void Quantize_loop_wrap_beat(void) {
    // audio/quantize
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, NULL, 0.5f, QUANTIZE_SHORT);
    int64_t start = audio_tap_count;
    Transport *tr = ecs_singleton_ensure(world, Transport);
    tr->loop = true;
    tr->loop_start = 0.5;
    tr->loop_end = 2.75;
    tr->playing = true;
    ecs_singleton_modified(world, Transport);
    audio_run(world, 78);
    int64_t clock = audio_tap_count;
    test_int(clock, start + 62400);
    test_assert(box_audio_play_quantized(world, dc, 1.0f, AudioQuantizeBeat));
    int64_t at = start + 66000 + 12000;
    quantize_run_past(world, at);
    test_int(quantize_onset(clock, at + 10), at);
    ecs_fini(world);
}

void Quantize_loop_wrap_bar(void) {
    // audio/quantize
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, NULL, 0.5f, QUANTIZE_SHORT);
    int64_t start = audio_tap_count;
    Transport *tr = ecs_singleton_ensure(world, Transport);
    tr->loop = true;
    tr->loop_start = 0.0;
    tr->loop_end = 3.5;
    tr->playing = true;
    ecs_singleton_modified(world, Transport);
    audio_run(world, 60);
    int64_t clock = audio_tap_count;
    test_int(clock, start + 48000);
    test_assert(box_audio_play_quantized(world, dc, 1.0f, AudioQuantizeBar));
    int64_t at = start + 84000;
    quantize_run_past(world, at);
    test_int(quantize_onset(clock, at + 10), at);
    ecs_fini(world);
}

void Quantize_stopped(void) {
    // audio/quantize
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, NULL, 0.5f, QUANTIZE_SHORT);
    Transport *tr = ecs_singleton_ensure(world, Transport);
    tr->position = 0.5;
    ecs_singleton_modified(world, Transport);
    audio_run(world, 5);
    int64_t clock = audio_tap_count;
    test_assert(box_audio_play_quantized(world, dc, 1.0f, AudioQuantizeBar));
    audio_run(world, 1);
    test_int(quantize_onset(clock, audio_tap_count), clock);

    audio_run(world, 5);
    ecs_singleton_remove(world, Transport);
    audio_run(world, 1);
    clock = audio_tap_count;
    test_assert(box_audio_play_quantized(world, dc, 1.0f, AudioQuantizeBeat));
    audio_run(world, 1);
    test_int(quantize_onset(clock, audio_tap_count), clock);
    ecs_fini(world);
}

void Quantize_none_at_once(void) {
    // audio/quantize
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, NULL, 0.5f, QUANTIZE_SHORT);
    quantize_start(world, 120.0f, 4, 4);
    audio_run(world, 15);
    int64_t clock = audio_tap_count;
    test_assert(box_audio_play_quantized(world, dc, 1.0f, AudioQuantizeNone));
    audio_run(world, 1);
    test_int(quantize_onset(clock, audio_tap_count), clock);
    ecs_fini(world);
}

/** Queue a play on the next bar at beat 0.5, then stop or seek the transport before it starts,
 * and check that it starts on the frame of that bar and plays on after a later stop.
 *
 * @param seek Whether to seek instead of stop
 */
static void quantize_keeps(
    bool seek)
{
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, NULL, 0.5f, AUDIO_RATE);
    int64_t start = audio_tap_count;
    quantize_start(world, 120.0f, 4, 4);
    audio_run(world, 15);
    int64_t clock = audio_tap_count;
    test_assert(box_audio_play_quantized(world, dc, 1.0f, AudioQuantizeBar));
    audio_run(world, 2);
    Transport *tr = ecs_singleton_ensure(world, Transport);
    if (seek) {
        tr->position = 10.25;
    } else {
        tr->playing = false;
    }

    ecs_singleton_modified(world, Transport);

    int64_t at = start + 96000;
    quantize_run_past(world, at);
    test_int(quantize_onset(clock, at + 10), at);

    tr = ecs_singleton_ensure(world, Transport);
    tr->playing = !tr->playing;
    ecs_singleton_modified(world, Transport);
    audio_run(world, 1);
    tr = ecs_singleton_ensure(world, Transport);
    tr->playing = false;
    ecs_singleton_modified(world, Transport);
    audio_run(world, 10);
    test_flt(audio_level(AUDIO_FRAME), 0.5f);
    test_int(box_audio_module_get(world)->timeline.stats.cancels, 0);
    ecs_fini(world);
}

void Quantize_stop_keeps(void) {
    // audio/quantize
    quantize_keeps(false);
}

void Quantize_seek_keeps(void) {
    // audio/quantize
    quantize_keeps(true);
}

void Quantize_play_through_stop(void) {
    // audio/quantize
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, NULL, 0.5f, AUDIO_RATE);
    Transport *tr = ecs_singleton_ensure(world, Transport);
    tr->metronome = true;
    tr->playing = true;
    ecs_singleton_modified(world, Transport);
    audio_run(world, 5);
    test_assert(box_audio_play(world, dc, 1.0f));
    audio_run(world, 5);
    tr = ecs_singleton_ensure(world, Transport);
    tr->playing = false;
    ecs_singleton_modified(world, Transport);
    audio_run(world, 10);
    test_flt(audio_level(AUDIO_FRAME), 0.5f);
    tr = ecs_singleton_ensure(world, Transport);
    tr->position = 3.0;
    ecs_singleton_modified(world, Transport);
    audio_run(world, 5);
    test_flt(audio_level(AUDIO_FRAME), 0.5f);
    test_int(ecs_singleton_get(world, AudioStatus)->voices, 1);
    ecs_fini(world);
}

void Quantize_on_bus(void) {
    // audio/quantize
    ecs_world_t *world = audio_world();
    ecs_entity_t bus = audio_bus(world, "stingers", 0, 0.5f);
    ecs_entity_t dc = audio_dc(world, NULL, 0.5f, QUANTIZE_SHORT);
    ecs_set(world, dc, AudioOutput, { .bus = bus });
    int64_t start = audio_tap_count;
    quantize_start(world, 120.0f, 4, 4);
    audio_run(world, 15);
    int64_t clock = audio_tap_count;
    test_assert(box_audio_play_quantized(world, dc, 1.0f, AudioQuantizeBeat));
    int64_t at = start + 24000;
    quantize_run_past(world, at + AUDIO_RAMP);
    test_int(quantize_onset(clock, at + 10), at);
    test_assert(fabsf(audio_tap_l[at + AUDIO_RAMP] - 0.25f) < 0.01f);
    ecs_fini(world);
}

void Quantize_script(void) {
    // audio/quantize
    ecs_world_t *world = audio_world();
    char *code = audio_read("test/assets/audio/quantize.flecs");
    int64_t clock = audio_tap_count;
    test_assert(ecs_script_run(world, "quantize", code, NULL) == 0);
    ecs_os_free(code);
    int64_t at = clock + 12000;
    quantize_run_past(world, at);
    test_int(quantize_onset(clock, at + 10), at);
    test_assert(audio_tap_l[at] > 0.0f);
    ecs_fini(world);
}

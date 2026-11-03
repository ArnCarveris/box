/**
 * @file Synth.c
 * @brief Benchmarks for the synth of the audio core: 64 looping synth voices on the master,
 * one item per sample of the output, with the patch of three layers or one of FM.
 *
 * Each iteration renders one quantum of 128 stereo frames, so the time per item is the time
 * per sample of the whole mix of 64 voices.
 */

#include <core.h>
#include <bake_bench.h>

/** Sample rate of the benchmarks */
#define BENCH_RATE 48000

/** Create a core with 64 looping voices of a patch of three layers: a saw that sweeps up, a
 * square with its own envelope and noise, into a lowpass.
 *
 * With fm the patch is four sines instead: two carriers, each phase modulated by a modulator
 * with its own envelope, one of them with feedback.
 *
 * @param sweep Whether the cutoff of the filter sweeps during the whole envelope
 * @param fm Whether the patch is the one of phase modulation
 * @return The core
 */
static box_audio_core_t* bench_synth_core(
    bool sweep,
    bool fm)
{
    static uint8_t bytes[8192];
    box_audio_core_desc_t desc = { .sample_rate = BENCH_RATE };
    box_audio_core_t *core = box_audio_core_new(&desc);
    box_audio_synth_patch_t p = {0};
    box_audio_writer_t w;
    uint32_t i;
    p.layers = 3;
    p.seed = 3;
    p.envelope = (box_audio_synth_envelope_t){
        .attack = 0.01f, .decay = 0.1f, .sustain = 0.6f, .hold = 2.0f, .release = 0.2f };
    p.filter = BOX_AUDIO_SYNTH_LOWPASS;
    p.cutoff = 400;
    p.cutoff_end = sweep ? 9000 : 0;
    p.cutoff_sweep = sweep ? 2.3f : 0;
    p.resonance = 0.6f;
    p.layer[0] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SAW, .frequency = 110,
        .frequency_end = 220, .sweep = 2.0f, .level = 0.4f };
    p.layer[1] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_SQUARE, .frequency = 330,
        .level = 0.2f, .has_envelope = 1,
        .envelope = { .attack = 0.005f, .decay = 0.3f, .sustain = 0.3f, .hold = 2.0f,
            .release = 0.1f } };
    p.layer[2] = (box_audio_synth_layer_t){ .wave = BOX_AUDIO_SYNTH_NOISE, .level = 0.1f };
    if (fm) {
        box_audio_synth_envelope_t decay = {
            .attack = 0.002f, .decay = 1.5f, .sustain = 0.2f, .hold = 2.0f, .release = 0.1f };
        p.layers = 4;
        p.layer[0] = (box_audio_synth_layer_t){ .frequency = 220, .level = 0.4f };
        p.layer[1] = (box_audio_synth_layer_t){ .frequency = 308, .level = 1,
            .modulates = 1, .index = 3, .has_envelope = 1, .envelope = decay };
        p.layer[2] = (box_audio_synth_layer_t){ .frequency = 440, .level = 0.2f };
        p.layer[3] = (box_audio_synth_layer_t){ .frequency = 440, .level = 1,
            .modulates = 3, .index = 1.5f, .feedback = 0.8f, .has_envelope = 1,
            .envelope = decay };
    }

    box_audio_writer_init(&w, bytes, sizeof(bytes));
    box_audio_write_synth(&w, 1, &p);
    for (i = 0; i < BOX_AUDIO_VOICE_MAX; i ++) {
        box_audio_write_play(&w, i, 1, 0, 0.02f, -0.9f + 0.03f * (float)i,
            0.75f + 0.01f * (float)i, BOX_AUDIO_PLAY_LOOP);
    }

    box_audio_core_apply(core, w.data, w.size);

    return core;
}

/** Render quanta of a core until the benchmark ends.
 *
 * @param b The benchmark
 * @param core The core
 */
static void bench_synth_render(
    bench_t *b,
    box_audio_core_t *core)
{
    static float left[BOX_AUDIO_CORE_QUANTUM], right[BOX_AUDIO_CORE_QUANTUM];
    box_audio_event_t events[64];
    int32_t i;
    for (i = 0; i < 64; i ++) {
        box_audio_core_render(core, left, right, BOX_AUDIO_CORE_QUANTUM);
    }

    bench_set_items(b, BOX_AUDIO_CORE_QUANTUM);
    while (bench_iter(b)) {
        box_audio_core_render(core, left, right, BOX_AUDIO_CORE_QUANTUM);
        box_audio_core_read_events(core, events, 64);
        bench_keep(left);
    }
}

void Synth_voices_64(
    bench_t *b)
{
    // audio/synth
    box_audio_core_t *core = bench_synth_core(false, false);
    bench_synth_render(b, core);
    box_audio_core_free(core);
}

void Synth_voices_64_filter_sweep(
    bench_t *b)
{
    // audio/synth
    box_audio_core_t *core = bench_synth_core(true, false);
    bench_synth_render(b, core);
    box_audio_core_free(core);
}

void Synth_voices_64_fm(
    bench_t *b)
{
    // audio/synth
    box_audio_core_t *core = bench_synth_core(false, true);
    bench_synth_render(b, core);
    box_audio_core_free(core);
}

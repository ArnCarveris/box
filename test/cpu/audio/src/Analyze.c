/**
 * @file Analyze.c
 * @brief The analysis of recordings of audio/analyze.c: loudness, true peak, bands, stereo,
 * tail, clicks and pitch of the WAV files of make_sounds.py, the FFT, the WAV reader, the
 * same result twice and the song chunk of a capture of a song.
 */

#include "world.h"
#include <analyze.h>
#include "../render/preprocess.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/** Folder of the recordings, from the repository root */
#define ANALYZE_DIR "test/assets/audio/"

/** Loudness of analyze_pink.wav that make_sounds.py prints, from the coefficients of BS.1770 */
#define ANALYZE_PINK_LUFS (-20.46f)

/** Read a file.
 *
 * @param name The file in ANALYZE_DIR, or a path when dir is false
 * @param dir Whether the file is in ANALYZE_DIR
 * @param size Receives its size
 * @return The bytes, which the caller frees
 */
static uint8_t* analyze_bytes(
    const char *name,
    bool dir,
    size_t *size)
{
    char path[512];
    ecs_os_set_api_defaults();
    snprintf(path, sizeof(path), "%s%s", dir ? ANALYZE_DIR : "", name);
    char *file = dir ? box_shader_locate(path) : ecs_os_strdup(path);
    test_assert(file != NULL);
    FILE *f = fopen(file, "rb");
    test_assert(f != NULL);
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *result = malloc((size_t)(len ? len : 1));
    test_assert(fread(result, 1, (size_t)len, f) == (size_t)len);
    fclose(f);
    ecs_os_free(file);
    *size = (size_t)len;

    return result;
}

/** Read a WAV of ANALYZE_DIR.
 *
 * @param name The file
 * @param wav Receives the frames
 */
static void analyze_wav(
    const char *name,
    box_audio_wav_t *wav)
{
    size_t size = 0;
    uint8_t *bytes = analyze_bytes(name, true, &size);
    const char *error = box_audio_wav_read(bytes, size, wav);
    test_assert(error == NULL);
    free(bytes);
}

/** Analyze a WAV of ANALYZE_DIR.
 *
 * @param name The file
 * @param out Receives the analysis
 */
static void analyze_file(
    const char *name,
    box_audio_analysis_t *out)
{
    box_audio_wav_t wav;
    analyze_wav(name, &wav);
    test_int(box_audio_analyze(wav.data, wav.frames, wav.channels, wav.sample_rate, out), 0);
    box_audio_wav_fini(&wav);
}

void Analyze_full_scale_sine(void) {
    box_audio_analysis_t a;
    analyze_file("analyze_sine_1k.wav", &a);
    test_int(a.channels, 1);
    test_int(a.frames, 96000);
    test_assert(fabsf(a.integrated - -3.01f) < 0.1f);
    test_assert(fabsf(a.momentary_max - -3.01f) < 0.1f);
    test_assert(fabsf(a.sample_peak) < 0.01f);
    test_assert(a.true_peak >= a.sample_peak - 0.01f);
    test_assert(a.true_peak < 0.2f);
    test_assert(fabsf(a.rms - -3.01f) < 0.05f);
    test_assert(fabsf(a.crest - 3.01f) < 0.05f);
    test_int(a.clipped, 0);
    test_assert(fabsf(a.dc[0]) < 1e-4f);
    test_flt(a.correlation, 1.0f);
    test_assert(a.range < 0.1f);
    test_assert(a.bands[3] > -0.1f);
    int32_t b;
    for (b = 0; b < BOX_AUDIO_ANALYZE_BANDS; b ++) {
        if (b != 3) {
            test_assert(a.bands[b] < -30.0f);
        }
    }

    test_assert(fabsf(a.centroid - 1000.0f) < 20.0f);
    test_assert(a.pitched);
    test_assert(fabsf(a.pitch - 1000.0f) < 1.0f);
    test_assert(a.pitch_spread < 5.0f);
    test_assert(a.hnr > 20.0f);
    char note[8];
    float cents = box_audio_note_name(a.pitch, note);
    test_str(note, "B5");
    test_assert(fabsf(cents - 21.0f) < 2.0f);
    test_int(a.onset_count, 1);
    test_assert(a.onsets[0].time < 0.01f);
    test_int(a.jump_count, 0);
    test_assert(a.first < 0.001f);
}

void Analyze_pink_noise(void) {
    box_audio_analysis_t a;
    analyze_file("analyze_pink.wav", &a);
    test_assert(fabsf(a.integrated - ANALYZE_PINK_LUFS) < 1.0f);
    test_assert(fabsf(a.rms - -20.0f) < 0.1f);
    test_assert(!a.pitched);
    test_assert(a.attack >= 0 && a.attack < 0.05f);
    int32_t b;
    float per_octave[BOX_AUDIO_ANALYZE_BANDS], mean = 0;
    for (b = 1; b < 6; b ++) {
        float octaves = log2f(box_audio_analyze_band_high(b) / box_audio_analyze_band_low(b));
        per_octave[b] = a.bands[b] - 10.0f * log10f(octaves);
        mean += per_octave[b] / 5.0f;
    }

    for (b = 1; b < 6; b ++) {
        test_assert(fabsf(per_octave[b] - mean) < 1.5f);
    }

    float sub = a.bands[0] - 10.0f * log10f(log2f(60.0f / 20.0f));
    float air = a.bands[6] - 10.0f * log10f(log2f(20000.0f / 12000.0f));
    test_assert(fabsf(sub - mean) < 3.0f);
    test_assert(fabsf(air - mean) < 3.0f);
}

void Analyze_inverted_stereo(void) {
    box_audio_analysis_t a;
    analyze_file("analyze_inverted.wav", &a);
    test_int(a.channels, 2);
    test_assert(a.correlation < -0.999f);
    test_assert(a.mono_loss > 40.0f);
    test_assert(a.side_mid > 40.0f);
    test_assert(fabsf(a.dc[0] + a.dc[1]) < 1e-5f);
}

void Analyze_true_peak(void) {
    box_audio_analysis_t a;
    analyze_file("analyze_true_peak.wav", &a);
    test_assert(fabsf(a.sample_peak - -3.01f) < 0.05f);
    test_assert(a.true_peak > a.sample_peak + 2.0f);
    test_assert(fabsf(a.true_peak) < 0.5f);
    test_int(a.jump_count, 0);
}

void Analyze_decay_tail(void) {
    box_audio_analysis_t a;
    analyze_file("analyze_decay.wav", &a);
    float expect = 0.2f * logf(1000.0f);
    test_int(a.onset_count, 1);
    test_assert(a.onsets[0].time < 0.01f);
    test_bool(a.tail_cut, false);
    test_assert(fabsf(a.tail - expect) < expect * 0.05f);
    test_assert(a.attack >= 0 && a.attack < 0.005f);
    test_assert(a.pitched);
    test_assert(fabsf(a.pitch - 440.0f) < 2.0f);
    test_assert(a.first < 0.001f);
    test_assert(a.leading < 0.001f);
}

void Analyze_cut_tail(void) {
    box_audio_wav_t wav;
    box_audio_analysis_t a;
    analyze_wav("analyze_decay.wav", &wav);
    test_int(box_audio_analyze(wav.data, 24000, 1, wav.sample_rate, &a), 0);
    test_bool(a.tail_cut, true);
    test_assert(a.tail_drop > 15.0f && a.tail_drop < 30.0f);
    box_audio_wav_fini(&wav);
}

void Analyze_start_click(void) {
    box_audio_analysis_t a;
    analyze_file("analyze_click.wav", &a);
    test_assert(fabsf(a.first - 0.5f) < 0.001f);
    test_assert(a.first >= powf(10.0f, BOX_AUDIO_ANALYZE_CLICK_DB / 20.0f));
    test_int(a.jump_count, 0);
}

void Analyze_discontinuity(void) {
    float data[4800];
    box_audio_analysis_t a;
    int32_t i;
    for (i = 0; i < 4800; i ++) {
        data[i] = 0.5f * sinf(2.0f * 3.14159265f * 220.0f * (float)i / 48000.0f) +
            (i >= 2400 ? 0.4f : 0.0f);
    }

    test_int(box_audio_analyze(data, 4800, 1, 48000, &a), 0);
    test_int(a.jump_count, 1);
    test_assert(fabsf(a.jumps[0].time - 0.05f) < 0.001f);
}

void Analyze_same_twice(void) {
    box_audio_analysis_t a, b;
    analyze_file("analyze_pink.wav", &a);
    analyze_file("analyze_pink.wav", &b);
    test_assert(!memcmp(&a, &b, sizeof(a)));
}

void Analyze_attack_swell(void) {
    int32_t i, frames = 48000;
    float *x = malloc(sizeof(float) * (size_t)frames);
    for (i = 0; i < frames; i ++) {
        float t = (float)i / 48000.0f;
        float env = t < 0.1f ? 0 : (t < 0.4f ? (t - 0.1f) / 0.3f : 1.0f);
        x[i] = 0.5f * env * sinf(2.0f * (float)M_PI * 440.0f * t);
    }

    box_audio_analysis_t a;
    test_int(box_audio_analyze(x, frames, 1, 48000, &a), 0);
    test_assert(a.onset_count >= 1);
    test_assert(fabsf(a.onsets[0].time - 0.1f) < 0.02f);
    test_assert(fabsf(a.attack - 0.24f) < 0.24f * 0.05f);
    free(x);
}

void Analyze_fft(void) {
    box_audio_fft_t fft;
    float in[64], re[33], im[33];
    int32_t i;
    test_int(box_audio_fft_init(&fft, 64), 0);
    for (i = 0; i < 64; i ++) {
        in[i] = cosf(2.0f * 3.14159265f * 5.0f * (float)i / 64.0f) + 0.25f;
    }

    box_audio_fft_real(&fft, in, re, im);
    test_assert(fabsf(re[0] - 16.0f) < 1e-3f);
    test_assert(fabsf(re[5] - 32.0f) < 1e-3f);
    test_assert(fabsf(im[5]) < 1e-3f);
    for (i = 1; i <= 32; i ++) {
        if (i != 5) {
            test_assert(fabsf(re[i]) < 1e-3f && fabsf(im[i]) < 1e-3f);
        }
    }

    box_audio_fft_fini(&fft);
    test_assert(box_audio_fft_init(&fft, 48) != 0);
}

void Analyze_wav_formats(void) {
    box_audio_wav_t wav;
    analyze_wav("sine_16.wav", &wav);
    test_int(wav.channels, 1);
    test_int(wav.frames, 12000);
    test_flt(wav.sample_rate, 48000);
    test_bool(wav.has_song, false);
    box_audio_wav_fini(&wav);
    analyze_wav("sine_float.wav", &wav);
    test_int(wav.frames, 12000);
    box_audio_wav_fini(&wav);
    analyze_wav("stereo.wav", &wav);
    test_int(wav.channels, 2);
    box_audio_wav_fini(&wav);

    size_t size = 0;
    uint8_t *bytes = analyze_bytes("analyze_adpcm.wav", true, &size);
    test_str(box_audio_wav_read(bytes, size, &wav), "unsupported WAV format: compressed samples");
    test_assert(wav.data == NULL);
    free(bytes);
    bytes = analyze_bytes("surround.wav", true, &size);
    test_str(box_audio_wav_read(bytes, size, &wav),
        "unsupported WAV format: more than 2 channels");
    free(bytes);
    bytes = analyze_bytes("corrupt.wav", true, &size);
    test_str(box_audio_wav_read(bytes, size, &wav), "not a WAV file");
    free(bytes);
    test_str(box_audio_wav_read(NULL, 0, &wav), "not a WAV file");
}

void Analyze_segments(void) {
    box_audio_wav_t wav;
    box_audio_analysis_t a;
    analyze_wav("analyze_sine_1k.wav", &wav);
    test_int(box_audio_analyze(wav.data, wav.frames, 1, wav.sample_rate, &a), 0);
    box_audio_analyze_segment_t segs[2] = {
        { .start = 0, .end = 48000 }, { .start = 48000, .end = 96000 }
    };
    test_int(box_audio_analyze_segments(wav.data, wav.frames, 1, wav.sample_rate, segs, 2), 0);
    test_assert(fabsf(segs[0].loudness - -3.01f) < 0.1f);
    test_assert(fabsf(segs[1].loudness - -3.01f) < 0.1f);
    test_assert(fabsf(segs[1].bands[3] - a.bands[3]) < 0.1f);
    box_audio_wav_fini(&wav);
}

void Analyze_song_capture(void) {
    // audio/song
    const char *path = "audio_analyze_song.wav";
    ecs_world_t *world = audio_world();
    box_audio_song_load(world, ANALYZE_DIR "songs/basic.flecs");
    int32_t i;
    for (i = 0; i < 600; i ++) {
        audio_run(world, 1);
        if (ecs_singleton_get(world, AudioSongStatus)->state != SongLoading) {
            break;
        }
    }

    test_int(ecs_singleton_get(world, AudioSongStatus)->state, SongReady);
    Transport *tr = ecs_singleton_ensure(world, Transport);
    tr->playing = true;
    ecs_singleton_modified(world, Transport);
    audio_run(world, 2);
    test_int(box_audio_capture(world, 4.0f, path), 0);
    for (i = 0; i < 600 && box_audio_capture_pending(world); i ++) {
        audio_run(world, 1);
    }

    test_bool(box_audio_capture_pending(world), false);

    size_t size = 0;
    uint8_t *bytes = analyze_bytes(path, false, &size);
    box_audio_wav_t wav;
    test_assert(box_audio_wav_read(bytes, size, &wav) == NULL);
    test_bool(wav.has_song, true);
    test_flt(wav.song.tempo, 96);
    test_flt(wav.song.bar_beats, 3);
    test_assert(wav.song.start_beat >= 0 && wav.song.start_beat < 1);
    test_int(wav.frames, 4 * 48000);
    box_audio_wav_fini(&wav);
    free(bytes);

    ecs_entity_t s = ecs_shell_session_init(world, NULL);
    bool error = true;
    char *out = box_command_run(world, s, "audio analyze audio_analyze_song.wav", &error);
    test_bool(error, false);
    test_assert(strstr(out, "\nbars: 96.0 bpm, 3.00 beats per bar") != NULL);
    test_assert(strstr(out, "\nbar 1") != NULL);
    test_assert(strstr(out, "\nbar 2: ") != NULL);
    test_assert(strstr(out, " LUFS, ") != NULL);
    ecs_os_free(out);
    ecs_fini(world);
    remove(path);
}

void Analyze_capture_without_song(void) {
    const char *path = "audio_analyze_plain.wav";
    ecs_world_t *world = audio_world();
    ecs_entity_t dc = audio_dc(world, "dc", 0.5f, AUDIO_RATE);
    audio_source(world, dc, 1.0f, true, 0);
    audio_run(world, 3);
    test_int(box_audio_capture(world, 0.5f, path), 0);
    int32_t i;
    for (i = 0; i < 200 && box_audio_capture_pending(world); i ++) {
        audio_run(world, 1);
    }

    ecs_fini(world);
    size_t size = 0;
    uint8_t *bytes = analyze_bytes(path, false, &size);
    test_int((int32_t)size, 44 + 24000 * 4);
    box_audio_wav_t wav;
    test_assert(box_audio_wav_read(bytes, size, &wav) == NULL);
    test_bool(wav.has_song, false);
    box_audio_wav_fini(&wav);
    free(bytes);
    remove(path);
}

/** Measure the integrated loudness of a WAV of ANALYZE_DIR with a loudness meter, in chunks.
 *
 * @param name The file
 * @param chunk Frames per call
 * @param analysis Receives the integrated loudness of box_audio_analyze
 * @return The integrated loudness of the meter
 */
static float analyze_meter(
    const char *name,
    int32_t chunk,
    float *analysis)
{
    box_audio_wav_t wav;
    box_audio_analysis_t a;
    box_audio_loudness_meter_t m;
    analyze_wav(name, &wav);
    test_int(box_audio_analyze(wav.data, wav.frames, wav.channels, wav.sample_rate, &a), 0);
    *analysis = a.integrated;
    float *left = malloc(sizeof(float) * (size_t)wav.frames);
    float *right = malloc(sizeof(float) * (size_t)wav.frames);
    int32_t i;
    for (i = 0; i < wav.frames; i ++) {
        left[i] = wav.data[i * wav.channels];
        right[i] = wav.data[i * wav.channels + (wav.channels > 1 ? 1 : 0)];
    }

    box_audio_loudness_meter_init(&m, wav.channels, wav.sample_rate);
    for (i = 0; i < wav.frames; i += chunk) {
        int32_t n = wav.frames - i < chunk ? wav.frames - i : chunk;
        box_audio_loudness_meter_add(&m, left + i, wav.channels > 1 ? right + i : NULL, n);
    }

    float result = box_audio_loudness_meter_integrated(&m);
    box_audio_loudness_meter_fini(&m);
    free(left);
    free(right);
    box_audio_wav_fini(&wav);

    return result;
}

void Analyze_loudness_meter(void) {
    float expect;
    float sine = analyze_meter("analyze_sine_1k.wav", 4096, &expect);
    test_assert(fabsf(sine - -3.01f) < 0.1f);
    test_assert(fabsf(sine - expect) < 0.05f);

    float pink = analyze_meter("analyze_pink.wav", 4096, &expect);
    test_assert(fabsf(pink - ANALYZE_PINK_LUFS) < 1.0f);
    test_assert(fabsf(pink - expect) < 0.1f);
    test_flt(analyze_meter("analyze_pink.wav", 77, &expect), pink);

    float stereo = analyze_meter("analyze_inverted.wav", 1000, &expect);
    test_assert(fabsf(stereo - expect) < 0.1f);

    box_audio_loudness_meter_t m;
    box_audio_loudness_meter_init(&m, 2, 48000);
    test_flt(box_audio_loudness_meter_integrated(&m), BOX_AUDIO_ANALYZE_FLOOR);
    float silence[480] = {0};
    box_audio_loudness_meter_add(&m, silence, silence, 480);
    test_flt(box_audio_loudness_meter_integrated(&m), BOX_AUDIO_ANALYZE_FLOOR);
    box_audio_loudness_meter_fini(&m);
}

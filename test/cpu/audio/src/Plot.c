/**
 * @file Plot.c
 * @brief The plots of audio/plot.c: the brightest row of a spectrogram of a sine sweep
 * follows the log frequency axis, a waveform of silence and a tone is empty where the
 * silence is, the same input gives the same pixels and bad arguments fail.
 */

#include "world.h"
#include <plot.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/** Sample rate of the signals of the tests */
#define PLOT_RATE (48000)

/** Get the luminance of a pixel of a plot.
 *
 * @param p The plot
 * @param x The column
 * @param y The row
 * @return The luminance, 0..255000
 */
static int32_t plot_luma(
    const box_audio_plot_t *p,
    int32_t x,
    int32_t y)
{
    const uint8_t *px = p->pixels + ((int64_t)y * p->width + x) * 3;

    return 299 * px[0] + 587 * px[1] + 114 * px[2];
}

/** Get whether a pixel of a plot has a color.
 *
 * @param p The plot
 * @param x The column
 * @param y The row
 * @param rgb The color, 0xRRGGBB
 * @return Whether the pixel has the color
 */
static bool plot_is(
    const box_audio_plot_t *p,
    int32_t x,
    int32_t y,
    uint32_t rgb)
{
    const uint8_t *px = p->pixels + ((int64_t)y * p->width + x) * 3;

    return px[0] == ((rgb >> 16) & 0xFF) && px[1] == ((rgb >> 8) & 0xFF) &&
        px[2] == (rgb & 0xFF);
}

/** Make a stereo signal that is the same on every run: a tone with a little noise.
 *
 * @param frames Number of frames
 * @return Interleaved frames, which the caller frees
 */
static float* plot_signal(
    int32_t frames)
{
    float *data = malloc(sizeof(float) * (size_t)frames * 2);
    uint32_t seed = 12345;
    int32_t i;
    for (i = 0; i < frames; i ++) {
        float tone = 0.4f * (float)sin(2.0 * M_PI * 220.0 * i / PLOT_RATE);
        seed = seed * 1664525u + 1013904223u;
        float noise = ((float)(seed >> 8) / 16777216.0f - 0.5f) * 0.1f;
        data[i * 2] = tone + noise;
        data[i * 2 + 1] = tone * 0.5f - noise;
    }

    return data;
}

void Plot_sweep_spectrogram(void) {
    const double f0 = 100, f1 = 10000, seconds = 4;
    int32_t frames = (int32_t)(PLOT_RATE * seconds), i, x, y;
    float *data = malloc(sizeof(float) * (size_t)frames);
    double k = log(f1 / f0);
    for (i = 0; i < frames; i ++) {
        double t = (double)i / PLOT_RATE;
        double phase = 2.0 * M_PI * f0 * seconds / k * (exp(t / seconds * k) - 1.0);
        data[i] = 0.5f * (float)sin(phase);
    }

    box_audio_plot_desc_t desc = {
        .kind = BoxAudioPlotSpectrogram,
        .data = data,
        .frames = frames,
        .channels = 1,
        .sample_rate = PLOT_RATE
    };

    box_audio_plot_t p;
    test_int(box_audio_plot(&desc, &p), 0);
    test_int(p.width, BOX_AUDIO_PLOT_WIDTH);
    test_int(p.height, BOX_AUDIO_PLOT_HEIGHT);
    test_assert(p.w > 0 && p.h > 0);
    test_assert(p.x + p.w <= p.width && p.y + p.h <= p.height);

    int32_t first = -1, last = -1, checked = 0;
    for (x = p.w / 20; x < p.w - p.w / 20; x ++) {
        int32_t best = -1, best_luma = -1;
        for (y = p.y; y < p.y + p.h; y ++) {
            int32_t l = plot_luma(&p, p.x + x, y);
            if (l > best_luma) {
                best_luma = l;
                best = y;
            }
        }

        double t = (x + 0.5) / p.w * seconds;
        float expect = box_audio_plot_hz_row(&p, (float)(f0 * exp(t / seconds * k)));
        test_assert(fabsf((float)best - expect) <= 8.0f);
        first = first < 0 ? best : first;
        last = best;
        checked ++;
    }

    test_assert(checked > p.w / 2);
    test_assert(first - last > p.h / 2);
    box_audio_plot_fini(&p);
    test_assert(p.pixels == NULL);
    free(data);
}

void Plot_waveform_silence_then_tone(void) {
    int32_t frames = PLOT_RATE * 2, i, x, y;
    float *data = calloc((size_t)frames, sizeof(float));
    for (i = frames / 2; i < frames; i ++) {
        data[i] = 0.5f * (float)sin(2.0 * M_PI * 440.0 * i / PLOT_RATE);
    }

    box_audio_plot_desc_t desc = {
        .kind = BoxAudioPlotWaveform,
        .data = data,
        .frames = frames,
        .channels = 1,
        .sample_rate = PLOT_RATE
    };

    box_audio_plot_t p;
    test_int(box_audio_plot(&desc, &p), 0);
    for (x = 0; x < p.w; x ++) {
        int32_t filled = 0;
        for (y = p.y; y < p.y + p.h; y ++) {
            filled += plot_is(&p, p.x + x, y, BOX_AUDIO_PLOT_PEAK) ||
                plot_is(&p, p.x + x, y, BOX_AUDIO_PLOT_RMS);
        }

        if (x < p.w / 2 - 1) {
            test_int(filled, 0);
        } else if (x > p.w / 2 + 1) {
            test_assert(filled > p.h / 3);
        }
    }

    box_audio_plot_fini(&p);
    free(data);
}

void Plot_same_twice(void) {
    int32_t frames = PLOT_RATE * 3 / 2, kind;
    float *data = plot_signal(frames);
    box_audio_wav_song_t song = { .start_beat = 2, .tempo = 120, .bar_beats = 4 };
    for (kind = BoxAudioPlotSpectrogram; kind <= BoxAudioPlotLoudness; kind ++) {
        box_audio_plot_desc_t desc = {
            .kind = (box_audio_plot_kind_t)kind,
            .data = data,
            .frames = frames,
            .channels = 2,
            .sample_rate = PLOT_RATE,
            .start = 0.5,
            .width = 800,
            .height = 400,
            .title = "same twice",
            .has_song = true,
            .song = song
        };

        box_audio_plot_t a, b;
        test_int(box_audio_plot(&desc, &a), 0);
        test_int(box_audio_plot(&desc, &b), 0);
        test_int(a.width, 800);
        test_int(a.height, 400);
        test_int(b.width, a.width);
        test_int(b.height, a.height);
        test_assert(!memcmp(a.pixels, b.pixels, (size_t)a.width * a.height * 3));
        box_audio_plot_fini(&a);
        box_audio_plot_fini(&b);
    }

    test_str(box_audio_plot_kind_name(BoxAudioPlotSpectrogram), "spectrogram");
    test_str(box_audio_plot_kind_name(BoxAudioPlotWaveform), "waveform");
    test_str(box_audio_plot_kind_name(BoxAudioPlotSpectrum), "spectrum");
    test_str(box_audio_plot_kind_name(BoxAudioPlotLoudness), "loudness");
    free(data);
}

void Plot_spectrum_sine_peak(void) {
    int32_t frames = PLOT_RATE, i, x, y;
    float *data = malloc(sizeof(float) * (size_t)frames);
    for (i = 0; i < frames; i ++) {
        data[i] = (float)sin(2.0 * M_PI * 1000.0 * i / PLOT_RATE);
    }

    box_audio_plot_desc_t desc = {
        .kind = BoxAudioPlotSpectrum,
        .data = data,
        .frames = frames,
        .channels = 1,
        .sample_rate = PLOT_RATE
    };

    box_audio_plot_t p;
    test_int(box_audio_plot(&desc, &p), 0);
    test_flt(p.min_db, BOX_AUDIO_PLOT_MIN_DB);

    int32_t best = -1, best_y = p.y + p.h;
    for (x = 0; x < p.w; x ++) {
        for (y = p.y; y < best_y; y ++) {
            if (plot_is(&p, p.x + x, y, 0x8CC8FF)) {
                best = x;
                best_y = y;
                break;
            }
        }
    }

    test_assert(best >= 0);
    test_assert(fabsf((float)(p.x + best) - box_audio_plot_hz_column(&p, 1000)) <= 3.0f);
    test_assert(best_y <= p.y + p.h / 50 + 2);
    box_audio_plot_fini(&p);
    free(data);
}

void Plot_bad_arguments(void) {
    float data[4] = {0};
    box_audio_plot_t p;
    box_audio_plot_desc_t desc = {
        .kind = BoxAudioPlotWaveform,
        .data = data,
        .frames = 4,
        .channels = 1,
        .sample_rate = PLOT_RATE
    };

    test_int(box_audio_plot(&desc, &p), 0);
    box_audio_plot_fini(&p);

    desc.frames = 0;
    test_int(box_audio_plot(&desc, &p), -1);
    test_assert(p.pixels == NULL);
    desc.frames = 4;
    desc.channels = 3;
    test_int(box_audio_plot(&desc, &p), -1);
    desc.channels = 1;
    desc.width = BOX_AUDIO_PLOT_MIN_WIDTH - 1;
    test_int(box_audio_plot(&desc, &p), -1);
    desc.width = 0;
    desc.height = BOX_AUDIO_PLOT_MAX_SIZE + 1;
    test_int(box_audio_plot(&desc, &p), -1);
    desc.height = 0;
    desc.sample_rate = 0;
    test_int(box_audio_plot(&desc, &p), -1);
    desc.sample_rate = PLOT_RATE;
    desc.data = NULL;
    test_int(box_audio_plot(&desc, &p), -1);
}

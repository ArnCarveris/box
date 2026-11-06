/**
 * @file audio/plot.h
 * @brief Images of recordings in plain C: a spectrogram, a waveform, an average spectrum and
 * the loudness over time, drawn on the CPU into RGB pixels with labeled axes in a built-in
 * bitmap font, and bar and beat lines for a capture of a song. No Flecs and no platform code.
 *
 * The audio.plot command encodes the pixels as PNG. The FFT and the window are those of
 * analyze.h. The same input gives the same pixels.
 */

#ifndef BOX_AUDIO_PLOT_H
#define BOX_AUDIO_PLOT_H

#include "analyze.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Default width of a plot in pixels */
#define BOX_AUDIO_PLOT_WIDTH (1200)

/** Default height of a plot in pixels */
#define BOX_AUDIO_PLOT_HEIGHT (600)

/** Smallest width of a plot in pixels */
#define BOX_AUDIO_PLOT_MIN_WIDTH (240)

/** Smallest height of a plot in pixels */
#define BOX_AUDIO_PLOT_MIN_HEIGHT (160)

/** Largest width or height of a plot in pixels */
#define BOX_AUDIO_PLOT_MAX_SIZE (4096)

/** Lowest frequency of the frequency axes in Hz */
#define BOX_AUDIO_PLOT_LOW_HZ (20.0f)

/** Highest frequency of the frequency axes in Hz */
#define BOX_AUDIO_PLOT_HIGH_HZ (20000.0f)

/** Default bottom of the dB scale of a spectrogram and a spectrum */
#define BOX_AUDIO_PLOT_MIN_DB (-100.0f)

/** Default bottom of the LUFS scale of a loudness plot */
#define BOX_AUDIO_PLOT_MIN_LUFS (-60.0f)

/** Color of the peak envelope of a waveform, 0xRRGGBB */
#define BOX_AUDIO_PLOT_PEAK (0x3C78B4)

/** Color of the RMS envelope of a waveform, 0xRRGGBB */
#define BOX_AUDIO_PLOT_RMS (0x8CC8FF)

/** Kind of a plot */
typedef enum box_audio_plot_kind_t {
    BoxAudioPlotSpectrogram, /**< Level per time and log frequency, a fixed dB color scale */
    BoxAudioPlotWaveform,   /**< Peak and RMS envelope per channel over time */
    BoxAudioPlotSpectrum,   /**< Average level per log frequency, the bands of the analysis */
    BoxAudioPlotLoudness    /**< Short-term and momentary loudness over time in LUFS */
} box_audio_plot_kind_t;

/** What to plot */
typedef struct box_audio_plot_desc_t {
    box_audio_plot_kind_t kind; /**< The kind */
    const float *data;      /**< Interleaved frames of the span */
    int32_t frames;         /**< Frames of the span, at least 1 */
    int32_t channels;       /**< 1 or 2 */
    float sample_rate;      /**< Frames per second */
    double start;           /**< Seconds of the first frame in the recording, for the axis */
    int32_t width;          /**< Width in pixels, 0 for the default */
    int32_t height;         /**< Height in pixels, 0 for the default */
    float min_db;           /**< Bottom of the dB or LUFS scale, 0 for the default */
    const char *title;      /**< Text at the top left, NULL for none */
    bool has_song;          /**< Whether to draw bars and beats */
    box_audio_wav_song_t song; /**< Beat of the first frame of the recording, tempo and bar */
} box_audio_plot_desc_t;

/** A plot: RGB pixels and where its data area is */
typedef struct box_audio_plot_t {
    uint8_t *pixels;        /**< 3 bytes per pixel, rows top to bottom, freed with fini */
    int32_t width;          /**< Width in pixels */
    int32_t height;         /**< Height in pixels */
    int32_t x;              /**< Left column of the data area */
    int32_t y;              /**< Top row of the data area */
    int32_t w;              /**< Columns of the data area */
    int32_t h;              /**< Rows of the data area */
    float min_db;           /**< Bottom of the dB or LUFS scale */
} box_audio_plot_t;

/** Get the name of a kind of plot, such as "spectrogram".
 *
 * @param kind The kind
 * @return Its name
 */
const char* box_audio_plot_kind_name(
    box_audio_plot_kind_t kind);

/** Draw a plot.
 *
 * @param desc What to plot
 * @param out Receives the pixels
 * @return 0 on success, -1 for bad arguments or out of memory
 */
int box_audio_plot(
    const box_audio_plot_desc_t *desc,
    box_audio_plot_t *out);

/** Free the pixels of a plot.
 *
 * @param plot The plot
 */
void box_audio_plot_fini(
    box_audio_plot_t *plot);

/** Get the row of a frequency on the log frequency axis of a spectrogram.
 *
 * @param plot The plot
 * @param hz The frequency
 * @return The row in the image, fractional, the top of the data area at 20 kHz
 */
float box_audio_plot_hz_row(
    const box_audio_plot_t *plot,
    float hz);

/** Get the column of a frequency on the log frequency axis of a spectrum.
 *
 * @param plot The plot
 * @param hz The frequency
 * @return The column in the image, fractional
 */
float box_audio_plot_hz_column(
    const box_audio_plot_t *plot,
    float hz);

#ifdef __cplusplus
}
#endif

#endif
